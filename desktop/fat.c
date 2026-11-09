#include "os.h"
/* Read-only FAT16/FAT32 on superfloppy, MBR or GPT. Every media-derived
 * address is checked against the mounted volume and block device. */
static unsigned drive, spc, fat_bits;
static u64 begin, end, fat_lba, data_lba, root_lba;
static u32 clusters, root_cluster, root_sectors;
static u8 sector[512], cache[512];
static u64 cached_lba = ~0ull;
static int mounted;
static u32 generation;
static char status_text[128] = "USB not mounted";
const char *fat_status(void) { return status_text; }
static int fail(const char *s) {
  strcopy(status_text, s, sizeof(status_text));
  return 0;
}
static int read(u64 lba, void *out) {
  return generation == usb_generation(drive) && lba >= begin && lba < end &&
         usb_read_sector(drive, lba, out);
}
static int boot(unsigned disk, u64 start, u64 limit) {
  u64 device_size = usb_sectors(disk);
  if (!limit || start >= device_size || limit > device_size - start ||
      !usb_read_sector(disk, start, sector))
    return 0;
  if (read_le16(sector + 510) != 0xaa55 || read_le16(sector + 11) != 512)
    return 0;
  unsigned unit = sector[13], reserved = read_le16(sector + 14),
           fats = sector[16], roots = read_le16(sector + 17);
  u32 total = read_le16(sector + 19);
  if (!total)
    total = read_le32(sector + 32);
  u32 fat_size = read_le16(sector + 22);
  if (!fat_size)
    fat_size = read_le32(sector + 36);
  u32 root_size = (roots * 32 + 511) / 512;
  u64 overhead = reserved + (u64)fats * fat_size + root_size;
  if (!unit || unit > 128 || (unit & (unit - 1)) || !reserved || !fats ||
      fats > 2 || !fat_size || total > limit || overhead >= total)
    return 0;
  u32 count = (total - overhead) / unit;
  unsigned bits = count < 4085 ? 12 : count < 65525 ? 16 : 32;
  if (bits == 12 || count > 0x0ffffff5 ||
      ((u64)fat_size * 512 / (bits / 8)) < (u64)count + 2)
    return 0;
  if ((bits == 32 && (roots || read_le16(sector + 22))) ||
      (bits == 16 && !roots))
    return 0;
  unsigned active = 0;
  if (bits == 32 && (read_le16(sector + 40) & 128)) {
    active = read_le16(sector + 40) & 15;
    if (active >= fats)
      return 0;
  }
  drive = disk;
  begin = start;
  end = start + total;
  spc = unit;
  fat_bits = bits;
  clusters = count;
  fat_lba = start + reserved + (u64)active * fat_size;
  root_lba = start + reserved + (u64)fats * fat_size;
  root_sectors = root_size;
  data_lba = root_lba + root_size;
  root_cluster = bits == 32 ? read_le32(sector + 44) & 0x0fffffff : 0;
  if (bits == 32 && (root_cluster < 2 || root_cluster >= count + 2))
    return 0;
  cached_lba = ~0ull;
  generation = usb_generation(drive);
  mounted = 1;
  strcopy(status_text,
          bits == 32 ? "USB FAT32 / solo lectura" : "USB FAT16 / solo lectura",
          sizeof(status_text));
  return 1;
}
int fat_mount(unsigned disk) {
  mounted = 0;
  cached_lba = ~0ull;
  u64 size = usb_sectors(disk);
  if (!size)
    return fail("USB desconectado o no disponible");
  if (boot(disk, 0, size))
    return 1;
  u8 mbr[512];
  if (!usb_read_sector(disk, 0, mbr) || read_le16(mbr + 510) != 0xaa55)
    return fail("USB sin particion FAT16/FAT32 valida");
  for (unsigned i = 0; i < 4; i++) {
    const u8 *p = mbr + 446 + i * 16;
    unsigned type = p[4];
    if (type == 0xee) {
      u8 header[512];
      if (!usb_read_sector(disk, 1, header) ||
          read_le64(header) != 0x5452415020494645ull)
        continue;
      u32 bytes = read_le32(header + 12), expected = read_le32(header + 16);
      if (bytes < 92 || bytes > 512)
        continue;
      write_le32(header + 16, 0);
      if (crc32(header, bytes) != expected)
        continue;
      u64 table = read_le64(header + 72);
      u32 entries = read_le32(header + 80), entry_size = read_le32(header + 84);
      if (entry_size != 128 || !entries || entries > 4096 || table >= size ||
          (u64)entries * 128 > (size - table) * 512)
        continue;
      u8 page[512];
      for (unsigned entry = 0; entry < entries; entry++) {
        if (entry % 4 == 0 && !usb_read_sector(disk, table + entry / 4, page))
          break;
        const u8 *e = page + (entry % 4) * 128;
        u64 first = read_le64(e + 32), last = read_le64(e + 40);
        int used = 0;
        for (unsigned k = 0; k < 16; k++)
          used |= e[k];
        if (used && last >= first && last < size &&
            boot(disk, first, last - first + 1))
          return 1;
      }
    } else if (type == 4 || type == 6 || type == 14 || type == 11 ||
               type == 12) {
      if (boot(disk, read_le32(p + 8), read_le32(p + 12)))
        return 1;
    }
  }
  return fail("USB: solo FAT16/FAT32; exFAT/NTFS no soportados");
}
u32 fat_root(void) { return root_cluster; }
static int valid_cluster(u32 c) { return c >= 2 && c < clusters + 2; }
static int next(u32 c, u32 *out) {
  if (!valid_cluster(c))
    return 0;
  u64 offset = (u64)c * (fat_bits / 8), lba = fat_lba + offset / 512;
  if (lba != cached_lba) {
    if (!read(lba, cache))
      return 0;
    cached_lba = lba;
  }
  u32 n = fat_bits == 16 ? read_le16(cache + offset % 512)
                         : read_le32(cache + offset % 512) & 0x0fffffff;
  if (n >= (fat_bits == 16 ? 0xfff8u : 0x0ffffff8u)) {
    *out = 0;
    return 1;
  }
  if (!valid_cluster(n))
    return 0;
  *out = n;
  return 1;
}
static u8 short_checksum(const u8 *name) {
  u8 sum = 0;
  for (unsigned i = 0; i < 11; i++)
    sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
  return sum;
}
int fat_list(u32 directory, struct FatEntry *out, unsigned cap) {
  if (!mounted || !out || !cap)
    return -1;
  u32 current = directory;
  unsigned count = 0, steps = 0;
  char long_name[128];
  memset(long_name, 0, sizeof(long_name));
  unsigned sequence = 0;
  int long_valid = 0;
  u32 visited[256];
  unsigned sector_budget = 512;
  u64 deadline = cycles() + clock_hz * 3;
  u8 checksum = 0;
  if (directory && !valid_cluster(directory))
    return -1;
  do {
    if (++steps > clusters + 1 || steps > 256)
      return fail("USB: directorio ciclico o demasiado grande"), -1;
    for (unsigned i = 0; i + 1 < steps; i++)
      if (directory && visited[i] == current)
        return fail("USB: directorio ciclico"), -1;
    visited[steps - 1] = current;
    unsigned sectors = directory ? spc : root_sectors;
    for (unsigned s = 0; s < sectors; s++) {
      if (!sector_budget-- || cycles() > deadline)
        return fail("USB: directorio supera el limite de lectura"), count;
      u64 lba =
          directory ? data_lba + (u64)(current - 2) * spc + s : root_lba + s;
      if (!read(lba, sector))
        return fail("USB: error al leer directorio"), -1;
      for (unsigned at = 0; at < 512; at += 32) {
        const u8 *e = sector + at;
        if (!e[0])
          return count;
        if (e[0] == 0xe5) {
          sequence = 0;
          long_valid = 0;
          continue;
        }
        if (e[11] == 15) {
          unsigned order = e[0] & 31;
          if (e[0] & 64) {
            memset(long_name, 0, sizeof(long_name));
            sequence = order;
            checksum = e[13];
            long_valid = 1;
          }
          if (!long_valid || !order || order > 10 || order != sequence ||
              e[13] != checksum || e[12] || read_le16(e + 26)) {
            sequence = 0;
            long_valid = 0;
            continue;
          }
          static const u8 positions[] = {1,  3,  5,  7,  9,  14, 16,
                                         18, 20, 22, 24, 28, 30};
          for (unsigned k = 0; k < 13; k++) {
            unsigned pos = (order - 1) * 13 + k;
            u16 ch = read_le16(e + positions[k]);
            if (pos < sizeof(long_name) - 1)
              long_name[pos] = !ch || ch == 0xffff ? 0
                               : ch < 256          ? (char)ch
                                                   : '?';
          }
          sequence--;
          continue;
        }
        if (e[11] & 8) {
          sequence = 0;
          long_valid = 0;
          long_name[0] = 0;
          continue;
        }
        if (count == cap)
          return fail("USB: lista limitada; directorio contiene mas entradas"),
                 count;
        struct FatEntry *v = &out[count];
        memset(v, 0, sizeof(*v));
        if (long_valid && !sequence && long_name[0] &&
            checksum == short_checksum(e))
          strcopy(v->name, long_name, sizeof(v->name));
        else {
          unsigned n = 0;
          for (unsigned k = 0; k < 8 && e[k] != ' '; k++)
            v->name[n++] = e[k] == 5 ? (char)0xe5
                           : (e[12] & 8) && e[k] >= 'A' && e[k] <= 'Z'
                               ? e[k] + 32
                               : e[k];
          if (e[8] != ' ') {
            v->name[n++] = '.';
            for (unsigned k = 8; k < 11 && e[k] != ' '; k++)
              v->name[n++] =
                  (e[12] & 16) && e[k] >= 'A' && e[k] <= 'Z' ? e[k] + 32 : e[k];
          }
        }
        sequence = 0;
        long_valid = 0;
        long_name[0] = 0;
        if (!strcmp(v->name, ".") || !strcmp(v->name, ".."))
          continue;
        v->cluster = read_le16(e + 26) |
                     (fat_bits == 32 ? (u32)read_le16(e + 20) << 16 : 0);
        v->cluster &= 0x0fffffff;
        v->size = read_le32(e + 28);
        v->directory = !!(e[11] & 16);
        if ((v->directory || v->size) && !valid_cluster(v->cluster))
          return fail("USB: entrada FAT no valida"), -1;
        count++;
      }
    }
    if (!directory)
      break;
    if (!next(current, &current))
      return fail("USB: cadena FAT no valida"), -1;
  } while (current);
  return count;
}
static int read_file(const struct FatEntry *entry, void *buffer, u32 cap,
                     u32 *got, int prefix) {
  if (got)
    *got = 0;
  if (!mounted || !entry || !buffer || !got || entry->directory)
    return fail("USB: selecciona un archivo");
  if (generation != usb_generation(drive))
    return fail("USB: lectura interrumpida; comprueba la conexion");
  if (entry->size > cap && !prefix)
    return fail("USB: archivo supera el limite de lectura");
  u32 length = entry->size < cap ? entry->size : cap;
  if (length > 65552)
    return fail("USB: limite de lectura de 65552 bytes");
  if (!length)
    return 1;
  u32 current = entry->cluster, used = 0, steps = 0;
  u64 deadline = cycles() + clock_hz * 5;
  u32 visited[129];
  while (used < length) {
    if (!valid_cluster(current) || steps >= 129 || steps >= clusters)
      return fail("USB: cadena FAT ciclica o no valida");
    for (unsigned i = 0; i < steps; i++)
      if (visited[i] == current)
        return fail("USB: cadena FAT ciclica o no valida");
    visited[steps++] = current;
    for (unsigned s = 0; s < spc && used < length; s++) {
      if (cycles() > deadline)
        return fail("USB: lectura supera el limite de tiempo");
      if (!read(data_lba + (u64)(current - 2) * spc + s, sector))
        return fail("USB: lectura interrumpida; comprueba la conexion");
      unsigned n = length - used;
      if (n > 512)
        n = 512;
      memcpy((u8 *)buffer + used, sector, n);
      used += n;
    }
    if (used < length && (!next(current, &current) || !current))
      return fail("USB: archivo truncado");
  }
  *got = used;
  return 1;
}
int fat_read(const struct FatEntry *entry, void *buffer, u32 cap, u32 *got) {
  return read_file(entry, buffer, cap, got, 0);
}
int fat_read_prefix(const struct FatEntry *entry, void *buffer, u32 cap,
                    u32 *got) {
  return read_file(entry, buffer, cap, got, 1);
}
