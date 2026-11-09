#include "os.h"
#define MAX_DISKS 8
#define STATE_LBA 136192
struct Disk {
  volatile u32 *p;
  u8 *mem;
  u64 sectors;
  char model[41];
};
static struct Disk disks[MAX_DISKS];
static unsigned disk_count;
static int mounted = -1;
static u8 sector[512], verify_sector[512];
u8 *boot_image;
u64 boot_image_size;
static u32 pci(unsigned b, unsigned d, unsigned f, unsigned off) {
  out32(0xcf8, 0x80000000 | (b << 16) | (d << 11) | (f << 8) | (off & 252));
  return in32(0xcfc);
}
static void pci_set(unsigned b, unsigned d, unsigned f, unsigned off,
                    u32 value) {
  out32(0xcf8, 0x80000000 | (b << 16) | (d << 11) | (f << 8) | (off & 252));
  out32(0xcfc, value);
}
static int wait_bits(volatile u32 *p, u32 mask, u32 value) {
  u64 end = cycles() + clock_hz * 2;
  while ((*p & mask) != value)
    if (cycles() > end)
      return 0;
  return 1;
}
/* One polled AHCI command; all descriptors and DMA buffers are below 4 GiB. */
static int command(struct Disk *d, u8 op, u64 lba, int write) {
  volatile u32 *p = d->p;
  u8 *mem = d->mem;
  if (!wait_bits(p + 8, 0x88, 0))
    return 0;
  memset(mem, 0, 32);
  memset(mem + 1280, 0, 256);
  write_le16(mem, 5 | (write ? 64 : 0));
  write_le16(mem + 2, op == 0xea ? 0 : 1);
  write_le32(mem + 8, (u32)(u64)(mem + 1280));
  u8 *fis = mem + 1280;
  fis[0] = 0x27;
  fis[1] = 0x80;
  fis[2] = op;
  fis[4] = lba;
  fis[5] = lba >> 8;
  fis[6] = lba >> 16;
  fis[7] = 0x40;
  fis[8] = lba >> 24;
  fis[9] = lba >> 32;
  fis[10] = lba >> 40;
  fis[12] = 1;
  write_le32(mem + 1408, (u32)(u64)(mem + 2048));
  write_le32(mem + 1420, 511);
  p[4] = 0xffffffff;
  p[12] = 0xffffffff;
  __asm__ volatile("mfence" ::: "memory");
  p[14] = 1;
  if (!wait_bits(p + 14, 1, 0))
    return 0;
  __asm__ volatile("mfence" ::: "memory");
  return !(p[4] & (1u << 30)) && !(p[8] & 1);
}
static int read_sector(unsigned disk, u64 lba, void *out) {
  if (disk >= disk_count || lba >= disks[disk].sectors)
    return 0;
  struct Disk *d = &disks[disk];
  if (!command(d, 0x25, lba, 0))
    return 0;
  memcpy(out, d->mem + 2048, 512);
  return 1;
}
static int write_sector(unsigned disk, u64 lba, const void *in) {
  if (disk >= disk_count || lba >= disks[disk].sectors)
    return 0;
  struct Disk *d = &disks[disk];
  memcpy(d->mem + 2048, in, 512);
  return command(d, 0x35, lba, 1);
}
static int flush(unsigned disk) { return command(&disks[disk], 0xea, 0, 0); }
static void num(void (*out)(const char *), u64 n) {
  char a[24], b[24];
  unsigned i = 0, j = 0;
  do {
    a[i++] = '0' + n % 10;
    n /= 10;
  } while (n);
  while (i)
    b[j++] = a[--i];
  b[j] = 0;
  out(b);
}
u32 crc32(const void *data, size_t n) {
  const u8 *p = data;
  u32 c = ~0u;
  while (n--) {
    c ^= *p++;
    for (unsigned i = 0; i < 8; i++)
      c = (c >> 1) ^ (0xedb88320u & -(c & 1));
  }
  return ~c;
}
void disk_init(void) {
  for (unsigned b = 0; b < 256; b++)
    for (unsigned d = 0; d < 32; d++)
      for (unsigned f = 0; f < 8; f++) {
        u32 cls = pci(b, d, f, 8);
        if ((cls >> 8) != 0x010601)
          continue;
        u32 bar = pci(b, d, f, 0x24);
        if ((bar & 1) || !bar)
          continue;
        pci_set(b, d, f, 4, (pci(b, d, f, 4) & 65535) | 6);
        volatile u32 *h = (volatile u32 *)(u64)(bar & ~15u);
        if (h[0x28 / 4] & 1) {
          h[0x28 / 4] |= 2;
          u64 end = cycles() + clock_hz * 2;
          while ((h[0x28 / 4] & 0x11) && cycles() < end) {
          }
          if (h[0x28 / 4] & 0x11)
            continue;
        }
        h[1] = (h[1] | (1u << 31)) & ~2u;
        u32 ports = h[3];
        for (unsigned port = 0; port < 32 && disk_count < MAX_DISKS; port++)
          if (ports & (1u << port)) {
            volatile u32 *p = h + 0x40 + port * 0x20;
            if ((p[10] & 0xf) != 3 || ((p[10] >> 8) & 15) != 1 || p[9] != 0x101)
              continue;
            p[6] &= ~1u;
            if (!wait_bits(p + 6, 1u << 15, 0))
              continue;
            p[6] &= ~16u;
            if (!wait_bits(p + 6, 1u << 14, 0))
              continue;
            struct Disk *dev = &disks[disk_count];
            dev->p = p;
            dev->mem = dma + 65536 + disk_count * 4096;
            memset(dev->mem, 0, 4096);
            p[0] = (u32)(u64)dev->mem;
            p[1] = 0;
            p[2] = (u32)(u64)(dev->mem + 1024);
            p[3] = 0;
            p[5] = 0;
            p[12] = 0xffffffff;
            p[4] = 0xffffffff;
            p[6] |= 16;
            p[6] |= 1;
            if (!command(dev, 0xec, 0, 0))
              continue;
            u16 *id = (u16 *)(dev->mem + 2048);
            if (!(id[83] & (1 << 10)))
              continue;
            if ((id[106] & 0xd000) == 0x5000 && (id[117] != 256 || id[118]))
              continue;
            dev->sectors = (u64)id[100] | ((u64)id[101] << 16) |
                           ((u64)id[102] << 32) | ((u64)id[103] << 48);
            if (!dev->sectors)
              continue;
            for (unsigned i = 0; i < 20; i++) {
              dev->model[i * 2] = id[27 + i] >> 8;
              dev->model[i * 2 + 1] = id[27 + i];
            }
            dev->model[40] = 0;
            serial("AHCI DISK: ");
            serial_num(disk_count);
            serial(" sectors=");
            serial_num(dev->sectors);
            serial("\n");
            disk_count++;
          }
      }
  for (unsigned i = 0; i < disk_count; i++)
    if (read_sector(i, STATE_LBA, sector) &&
        !strcmp((char *)sector, "BETA STATE 1")) {
      mounted = i;
      break;
    }
}
void disk_list(void (*out)(const char *)) {
  if (!disk_count) {
    out("No SATA/AHCI disks detected. NVMe and USB disks are not supported.\n");
    return;
  }
  for (unsigned i = 0; i < disk_count; i++) {
    out("disk ");
    num(out, i);
    out(" / ");
    num(out, disks[i].sectors / 2048);
    out(" MiB / ");
    out(disks[i].model);
    out((int)i == mounted ? " [BETA DATA]\n" : "\n");
  }
}
static u8 entries[16384];
static void guid(u8 *p, u32 first, u16 second, u16 third, const u8 tail[8]) {
  memcpy(p, &first, 4);
  memcpy(p + 4, &second, 2);
  memcpy(p + 6, &third, 2);
  memcpy(p + 8, tail, 8);
}
static int gpt_header(unsigned disk, u64 where, u64 other, u64 table,
                      u64 last) {
  memset(sector, 0, 512);
  memcpy(sector, "EFI PART", 8);
  write_le32(sector + 8, 0x10000);
  write_le32(sector + 12, 92);
  write_le64(sector + 24, where);
  write_le64(sector + 32, other);
  write_le64(sector + 40, 34);
  write_le64(sector + 48, last - 33);
  static const u8 tail[8] = {0x80, 0, 0, 0, 0, 0, 0, 2};
  guid(sector + 56, 0x42455441, 4, 0x4000, tail);
  write_le64(sector + 72, table);
  write_le32(sector + 80, 128);
  write_le32(sector + 84, 128);
  write_le32(sector + 88, crc32(entries, sizeof(entries)));
  write_le32(sector + 16, crc32(sector, 92));
  return write_sector(disk, where, sector);
}
static void directory_entry(u8 *p, const char *name, u8 attr, u16 cluster,
                            u32 size) {
  memset(p, 0, 32);
  memcpy(p, name, 11);
  p[11] = attr;
  write_le16(p + 26, cluster);
  write_le32(p + 28, size);
}
static int install_fail(void (*out)(const char *), const char *message) {
  out(message);
  out("\nInstallation failed; target may be incomplete.\n");
  gui_progress("INSTALLATION FAILED", message, 1, 1);
  return 0;
}
static unsigned boot_config(char *out) {
  strcopy(out, "resolution=", 64);
  unsigned at = 11;
  u32 values[] = {fb_width, fb_height};
  for (unsigned axis = 0; axis < 2; axis++) {
    char reverse[12];
    unsigned n = 0;
    u32 v = values[axis];
    do {
      reverse[n++] = '0' + v % 10;
      v /= 10;
    } while (v);
    while (n)
      out[at++] = reverse[--n];
    out[at++] = axis ? '\n' : 'x';
  }
  out[at] = 0;
  return at;
}
int disk_install(unsigned disk, void (*out)(const char *)) {
  if (disk >= disk_count) {
    out("Invalid disk number. Use disks.\n");
    gui_progress("INSTALLATION FAILED", "Invalid disk number. No disk was changed.", 1, 1);
    return 0;
  }
  if (disks[disk].sectors < 262144) {
    out("Disk must be at least 128 MiB.\n");
    gui_progress("INSTALLATION FAILED", "Disk must be at least 128 MiB. No disk was changed.", 1, 1);
    return 0;
  }
  if (!boot_image || boot_image_size < 512 ||
      boot_image_size > 16 * 1024 * 1024) {
    out("Boot executable could not be read from the boot medium.\n");
    gui_progress("INSTALLATION FAILED", "Boot executable unavailable. No disk was changed.", 1, 1);
    return 0;
  }
  char config[64];
  unsigned config_size = boot_config(config);
  u64 last = disks[disk].sectors - 1;
  u32 sectors = (boot_image_size + 511) / 512;
  gui_progress("INSTALLING BETAOS", "Creating GPT partitions", 0, 100);
  memset(entries, 0, sizeof(entries));
  static const u8 esp_tail[8] = {0xba, 0x4b, 0, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b},
                  data_tail[8] = {0x80, 0, 0, 0, 0, 0, 0, 3};
  guid(entries, 0xc12a7328, 0xf81f, 0x11d2, esp_tail);
  guid(entries + 16, 0x42455441, 4, 0x4000, data_tail);
  write_le64(entries + 32, 2048);
  write_le64(entries + 40, 2048 + 131072 - 1);
  const char *label = "BETAOS EFI";
  for (unsigned i = 0; label[i]; i++)
    entries[56 + i * 2] = label[i];
  guid(entries + 128, 0x42455441, 4, 0x4000, data_tail);
  guid(entries + 144, 0x42455442, 4, 0x4000, data_tail);
  write_le64(entries + 160, 135168);
  write_le64(entries + 168, last - 33);
  label = "BETA DATA";
  for (unsigned i = 0; label[i]; i++)
    entries[184 + i * 2] = label[i];
  /* Invalidate the old primary GPT first; publish a new valid header last. */
  memset(sector, 0, 512);
  if (!write_sector(disk, 1, sector))
    return install_fail(out, "Cannot write target GPT");
  for (unsigned i = 0; i < 32; i++)
    if (!write_sector(disk, 2 + i, entries + i * 512) ||
        !write_sector(disk, last - 32 + i, entries + i * 512))
      return install_fail(out, "Partition table write failed");
  memset(sector, 0, 512);
  sector[446 + 4] = 0xee;
  sector[447] = 0;
  sector[448] = 2;
  sector[451] = sector[452] = sector[453] = 255;
  write_le32(sector + 454, 1);
  write_le32(sector + 458, last > 0xffffffff ? 0xffffffff : last);
  sector[510] = 0x55;
  sector[511] = 0xaa;
  if (!write_sector(disk, 0, sector))
    return install_fail(out, "MBR write failed");
  gui_progress("INSTALLING BETAOS", "Formatting EFI system partition", 10, 100);
  /* A fixed disk uses a 64 MiB FAT32 EFI System Partition. */
  memset(sector, 0, 512);
  sector[0] = 0xeb;
  sector[1] = 0x58;
  sector[2] = 0x90;
  memcpy(sector + 3, "BETAOS  ", 8);
  write_le16(sector + 11, 512);
  sector[13] = 1;
  write_le16(sector + 14, 32);
  sector[16] = 2;
  sector[21] = 0xf8;
  write_le16(sector + 24, 63);
  write_le16(sector + 26, 255);
  write_le32(sector + 28, 2048);
  write_le32(sector + 32, 131072);
  write_le32(sector + 36, 1024);
  write_le32(sector + 44, 2);
  write_le16(sector + 48, 1);
  write_le16(sector + 50, 6);
  sector[64] = 0x80;
  sector[66] = 0x29;
  write_le32(sector + 67, 0x42455441);
  memcpy(sector + 71, "BETAOS EFI ", 11);
  memcpy(sector + 82, "FAT32   ", 8);
  sector[510] = 0x55;
  sector[511] = 0xaa;
  if (!write_sector(disk, 2048, sector) || !write_sector(disk, 2054, sector))
    return install_fail(out, "FAT32 boot sector write failed");
  for (unsigned i = 1; i < 32; i++) {
    if (i == 6)
      continue;
    memset(sector, 0, 512);
    if (i == 1 || i == 7) {
      write_le32(sector, 0x41615252);
      write_le32(sector + 484, 0x61417272);
      write_le32(sector + 488, 0xffffffff);
      write_le32(sector + 492, 0xffffffff);
      write_le32(sector + 508, 0xaa550000);
    }
    if (!write_sector(disk, 2048 + i, sector))
      return install_fail(out, "FAT32 reserved sector write failed");
  }
  for (unsigned f = 0; f < 1024; f++) {
    memset(sector, 0, 512);
    for (unsigned i = 0; i < 128; i++) {
      u32 cluster = f * 128 + i, value = 0;
      if (cluster == 0)
        value = 0x0ffffff8;
      else if (cluster < 5)
        value = 0x0fffffff;
      else if (cluster == 5 + sectors)
        value = 0x0fffffff;
      else if (cluster < 5 + sectors)
        value = cluster == 4 + sectors ? 0x0fffffff : cluster + 1;
      write_le32(sector + i * 4, value);
    }
    if (!write_sector(disk, 2080 + f, sector) ||
        !write_sector(disk, 3104 + f, sector))
      return install_fail(out, "FAT32 allocation table write failed");
    if (!(f % 64))
      gui_progress("INSTALLING BETAOS", "Formatting FAT32 system partition",
                   10 + f * 15 / 1024, 100);
  }
  memset(sector, 0, 512);
  directory_entry(sector, "EFI        ", 0x10, 3, 0);
  directory_entry(sector + 32, "BETA    CFG", 0x20, 5 + sectors, config_size);
  if (!write_sector(disk, 4128, sector))
    return install_fail(out, "Root directory write failed");
  memset(sector, 0, 512);
  directory_entry(sector, ".          ", 0x10, 3, 0);
  directory_entry(sector + 32, "..         ", 0x10, 0, 0);
  directory_entry(sector + 64, "BOOT       ", 0x10, 4, 0);
  if (!write_sector(disk, 4129, sector))
    return install_fail(out, "EFI directory write failed");
  memset(sector, 0, 512);
  directory_entry(sector, ".          ", 0x10, 4, 0);
  directory_entry(sector + 32, "..         ", 0x10, 3, 0);
  directory_entry(sector + 64, "BOOTX64 EFI", 0x20, 5, boot_image_size);
  if (!write_sector(disk, 4130, sector))
    return install_fail(out, "Boot directory write failed");
  for (u32 i = 0; i < sectors; i++) {
    memset(sector, 0, 512);
    u64 left = boot_image_size - (u64)i * 512;
    memcpy(sector, boot_image + i * 512, left > 512 ? 512 : left);
    if (!write_sector(disk, 4131 + i, sector))
      return install_fail(out, "OS executable write failed");
    if (!(i % 32))
      gui_progress("INSTALLING BETAOS", "Copying native boot executable",
                   25 + i * 40 / sectors, 100);
  }
  memset(sector, 0, 512);
  memcpy(sector, config, config_size);
  if (!write_sector(disk, 4131 + sectors, sector))
    return install_fail(out, "Display configuration write failed");
  if (!flush(disk))
    return install_fail(out, "Disk flush failed");
  if (!read_sector(disk, 4131 + sectors, verify_sector) ||
      crc32(verify_sector, config_size) != crc32(config, config_size))
    return install_fail(out, "Display configuration verification failed");
  for (u32 i = 0; i < sectors; i++) {
    if (!read_sector(disk, 4131 + i, verify_sector))
      return install_fail(out, "Readback failed");
    unsigned n = boot_image_size - (u64)i * 512 > 512
                     ? 512
                     : boot_image_size - (u64)i * 512;
    for (unsigned j = 0; j < n; j++)
      if (verify_sector[j] != boot_image[i * 512 + j])
        return install_fail(out, "OS verification mismatch");
    if (!(i % 32))
      gui_progress("INSTALLING BETAOS", "Verifying written executable",
                   65 + i * 25 / sectors, 100);
  }
  if (!gpt_header(disk, last, 1, last - 32, last) ||
      !gpt_header(disk, 1, last, 2, last) || !flush(disk))
    return install_fail(out, "GPT finalization failed");
  mounted = disk;
  out("Boot executable verified. Saving persistent state...\n");
  if (!desktop_save())
    return install_fail(out, "System copied, but persistent state save failed");
  gui_progress("BETAOS INSTALLED",
               "Verified. Remove install media and boot this disk.", 100, 100);
  out("INSTALL OK: native UEFI OS installed and verified.\nRemove installation "
      "media and boot the target disk.\n");
  return 1;
}
int disk_save(const void *data, u32 bytes) {
  if (mounted < 0 || bytes > 4 * 1024 * 1024)
    goto failed;
  gui_progress("SAVING STATE", "Writing apps and files", 0, 100);
  for (u32 i = 0; i < (bytes + 511) / 512; i++) {
    memset(sector, 0, 512);
    u32 n = bytes - i * 512;
    if (n > 512)
      n = 512;
    memcpy(sector, (const u8 *)data + i * 512, n);
    if (!write_sector(mounted, STATE_LBA + 1 + i, sector))
      goto failed;
    if (!(i % 64))
      gui_progress("SAVING STATE", "Writing apps and files",
                   i * 512 * 100 / bytes, 100);
  }
  if (!flush(mounted))
    goto failed;
  memset(sector, 0, 512);
  memcpy(sector, "BETA STATE 1", 13);
  write_le32(sector + 16, bytes);
  write_le32(sector + 20, crc32(data, bytes));
  if (!write_sector(mounted, STATE_LBA, sector) || !flush(mounted))
    goto failed;
  gui_progress("STATE SAVED", "Apps and files stored on disk", 100, 100);
  return 1;
failed:
  serial("STATE SAVE FAILED\n");
  gui_progress("SAVE FAILED", "Disk I/O failed. Data may not have been saved.",
               1, 1);
  return 0;
}
int disk_load(void *data, u32 capacity, u32 *size) {
  if (mounted < 0 || !read_sector(mounted, STATE_LBA, sector))
    return 0;
  u32 bytes = read_le32(sector + 16), crc = read_le32(sector + 20);
  if (bytes > capacity || !bytes)
    return 0;
  for (u32 i = 0; i < (bytes + 511) / 512; i++) {
    if (!read_sector(mounted, STATE_LBA + 1 + i, sector))
      return 0;
    u32 n = bytes - i * 512;
    if (n > 512)
      n = 512;
    memcpy((u8 *)data + i * 512, sector, n);
  }
  if (crc32(data, bytes) != crc)
    return 0;
  *size = bytes;
  return 1;
}
int disk_mounted(void) { return mounted >= 0; }
unsigned disk_total(void) { return disk_count; }
const char *disk_name(unsigned i) {
  return i < disk_count ? disks[i].model : "";
}
u64 disk_mebibytes(unsigned i) {
  return i < disk_count ? disks[i].sectors / 2048 : 0;
}
