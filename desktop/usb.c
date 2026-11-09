#include "os.h"
/* Independent polled xHCI driver: direct root ports, MSC/SCSI Bulk-Only,
 * LUN 0, 512-byte sectors. No USB write command is exposed or generated.
 * All rings, contexts and transfer data use reserved DMA below 4 GiB. */
#define DEVICES 4
#define TRBS 64
struct Trb {
  u64 pointer;
  u32 status, control;
};
struct Ring {
  struct Trb *trb;
  unsigned next, cycle;
};
struct Device {
  unsigned slot, port, speed, in, out;
  struct Ring control, receive, send;
  u8 *context, *input, *buffer;
  u64 sectors;
  int online;
  u32 generation;
};
static struct Device devices[DEVICES];
static unsigned count, ports, ctxsize, event_index, event_cycle;
static volatile u32 *op, *runtime, *doorbell;
static struct Ring commands;
static struct Trb *events;
static u64 *dcbaa;
static u8 *arena;
static unsigned allocated;
static int ready, broken;
static u32 tag;
static u32 media_generation;
static char status_text[128] = "No xHCI controller";
static void fence(void) { __asm__ volatile("mfence" ::: "memory"); }
static void delay(unsigned ms) {
  u64 until = cycles() + clock_hz * ms / 1000;
  while (cycles() < until)
    __asm__ volatile("pause");
}
static void *alloc(unsigned n, unsigned alignment) {
  unsigned offset =
      (((u64)arena + allocated + alignment - 1) & ~(u64)(alignment - 1)) -
      (u64)arena;
  if (offset > 896 * 1024 || n > 896 * 1024 - offset)
    return 0;
  void *p = arena + offset;
  allocated = offset + n;
  memset(p, 0, n);
  return p;
}
static u32 pci(unsigned b, unsigned d, unsigned f, unsigned off) {
  out32(0xcf8, 0x80000000 | (b << 16) | (d << 11) | (f << 8) | (off & 252));
  return in32(0xcfc);
}
static int wait_reg(volatile u32 *p, u32 mask, u32 value, unsigned ms) {
  u64 until = cycles() + clock_hz * ms / 1000;
  do {
    if ((*p & mask) == value)
      return 1;
    __asm__ volatile("pause");
  } while (cycles() < until);
  return 0;
}
static int ring_init(struct Ring *r) {
  if (!r->trb)
    r->trb = alloc(TRBS * sizeof(struct Trb), 64);
  if (!r->trb)
    return 0;
  memset(r->trb, 0, TRBS * sizeof(struct Trb));
  r->next = 0;
  r->cycle = 1;
  return 1;
}
static u64 enqueue(struct Ring *r, u64 pointer, u32 status, u32 control) {
  if (r->next == TRBS - 1) {
    struct Trb *link = &r->trb[r->next];
    link->pointer = (u64)r->trb;
    link->status = 0;
    fence();
    link->control = (6 << 10) | 2 | r->cycle;
    fence();
    r->next = 0;
    r->cycle ^= 1;
  }
  struct Trb *t = &r->trb[r->next++];
  t->pointer = pointer;
  t->status = status;
  fence();
  t->control = control | r->cycle;
  fence();
  return (u64)t;
}
static void write64(volatile u32 *p, u64 value) {
  p[0] = value;
  p[1] = value >> 32;
}
/* Only one synchronous command/transfer outstanding. Port events are drained;
 * timeout poisons this controller until reboot, preventing stale DMA reuse. */
static int completion(unsigned type, u64 pointer, unsigned slot, unsigned ep,
                      unsigned *result) {
  u64 until = cycles() + clock_hz * 2;
  while (cycles() < until) {
    volatile struct Trb *t = &events[event_index];
    if ((t->control & 1) != event_cycle) {
      __asm__ volatile("pause");
      continue;
    }
    fence();
    u64 ptr = t->pointer;
    u32 state = t->status, control = t->control;
    if (++event_index == 256) {
      event_index = 0;
      event_cycle ^= 1;
    }
    write64(runtime + 0x38 / 4, (u64)&events[event_index] | 8);
    unsigned kind = (control >> 10) & 63;
    if (kind == type && (type == 33 ? ptr == pointer
                                    : ((control >> 24) == slot &&
                                       ((control >> 16) & 31) == ep))) {
      unsigned code = state >> 24;
      if (result)
        *result = type == 32 ? (state & 0xffffff) : control >> 24;
      if (code == 1 || (type == 32 && code == 13))
        return 1;
      serial("USB completion error ");
      serial_num(code);
      serial("\n");
      return 0;
    }
  }
  broken = 1;
  strcopy(status_text, "USB timeout; reboot required before further USB access",
          sizeof(status_text));
  serial("USB transfer timeout\n");
  return 0;
}
static int command(unsigned type, u64 pointer, unsigned slot,
                   unsigned *result) {
  if (broken)
    return 0;
  u64 submitted = enqueue(&commands, pointer, 0, (type << 10) | (slot << 24));
  doorbell[0] = 0;
  return completion(33, submitted, 0, 0, result);
}
static int connected(const struct Device *d) {
  return ready && !broken && (op[0x400 / 4 + (d->port - 1) * 4] & 3) == 3;
}
static int control(struct Device *d, u8 request_type, u8 request, u16 value,
                   u16 index, u16 length) {
  if (!connected(d))
    return 0;
  u64 setup = request_type | ((u64)request << 8) | ((u64)value << 16) |
              ((u64)index << 32) | ((u64)length << 48);
  unsigned input = request_type & 128;
  enqueue(&d->control, setup, 8,
          (2 << 10) | (1 << 6) | (length ? (input ? 3u : 2u) << 16 : 0));
  if (length)
    enqueue(&d->control, (u64)d->buffer, length,
            (3 << 10) | (input ? (1 << 16) : 0));
  u64 last =
      enqueue(&d->control, 0, 0,
              (4 << 10) | (1 << 5) | ((!length || !input) ? (1 << 16) : 0));
  doorbell[d->slot] = 1;
  return completion(32, last, d->slot, 1, 0);
}
static int bulk(struct Device *d, int input, unsigned bytes) {
  if (!connected(d) || bytes > 65536)
    return 0;
  struct Ring *r = input ? &d->receive : &d->send;
  unsigned ep = input ? d->in : d->out;
  /* Shared buffer is 64KiB-aligned: one Normal TRB cannot cross its boundary.
   */
  u64 last = enqueue(r, (u64)d->buffer, bytes, (1 << 10) | (1 << 5));
  doorbell[d->slot] = ep;
  unsigned residue = 0;
  return completion(32, last, d->slot, ep, &residue) && residue == 0;
}
static u32 be32(const u8 *p) {
  return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static int scsi(struct Device *d, const u8 *cdb, unsigned cdb_size, void *out,
                unsigned bytes) {
  if (cdb_size > 16 || bytes > 65536)
    return 0;
  memset(d->buffer, 0, 31);
  write_le32(d->buffer, 0x43425355);
  u32 current = ++tag;
  write_le32(d->buffer + 4, current);
  write_le32(d->buffer + 8, bytes);
  d->buffer[12] = bytes ? 0x80 : 0;
  d->buffer[14] = cdb_size;
  memcpy(d->buffer + 15, cdb, cdb_size);
  if (!bulk(d, 0, 31))
    return 0;
  if (bytes) {
    memset(d->buffer, 0, bytes);
    if (!bulk(d, 1, bytes))
      return 0;
    memcpy(out, d->buffer, bytes);
  }
  memset(d->buffer, 0, 13);
  if (!bulk(d, 1, 13) || read_le32(d->buffer) != 0x53425355 ||
      read_le32(d->buffer + 4) != current || read_le32(d->buffer + 8) > bytes)
    return 0;
  return d->buffer[12] == 0 && read_le32(d->buffer + 8) == 0;
}
static void endpoint(struct Device *d, unsigned ep, unsigned packet,
                     unsigned burst) {
  u8 *p = d->input + (ep + 1) * ctxsize;
  struct Ring *r = ep == d->in ? &d->receive : &d->send;
  write_le32(p + 4, (3 << 1) | ((ep & 1 ? 6u : 2u) << 3) | (burst << 8) |
                        (packet << 16));
  write_le64(p + 8, (u64)r->trb | 1);
  write_le32(p + 16, 512);
}
static int enumerate(struct Device *d, unsigned port) {
  d->in = d->out = 0;
  d->online = 0;
  d->sectors = 0;
  volatile u32 *p = op + 0x400 / 4 + (port - 1) * 4;
  /* Preserve RW bits only; PED and change bits are W1C. Power on first. */
  u32 neutral = (*p & ((15u << 5) | (1u << 9) | (3u << 14) | (7u << 25)));
  *p = neutral | (1 << 9);
  delay(20);
  if (!(*p & 1))
    return 0;
  /* Reset even an enabled port: firmware may have left its device addressed. */
  *p = neutral | (1 << 9) | (1 << 4);
  if (!wait_reg(p, 1 << 4, 0, 1000) || !wait_reg(p, 3, 3, 1000))
    return 0;
  delay(20);
  if ((*p & 3) != 3)
    return 0;
  d->port = port;
  d->speed = (*p >> 10) & 15;
  if (!command(9, 0, 0, &d->slot) || !d->slot || d->slot > 32)
    return 0;
  if (!d->context)
    d->context = alloc(ctxsize * 32, 64);
  if (!d->input)
    d->input = alloc(ctxsize * 33, 64);
  if (!d->buffer)
    d->buffer = alloc(65536, 65536);
  if (!d->context || !d->input || !d->buffer || !ring_init(&d->control) ||
      !ring_init(&d->receive) || !ring_init(&d->send))
    return 0;
  memset(d->context, 0, ctxsize * 32);
  memset(d->input, 0, ctxsize * 33);
  dcbaa[d->slot] = (u64)d->context;
  write_le32(d->input + 4, 3);
  write_le32(d->input + ctxsize, (d->speed << 20) | (1u << 27));
  write_le32(d->input + ctxsize + 4, port << 16);
  unsigned packet = d->speed >= 4 ? 512 : d->speed == 3 ? 64 : 8;
  u8 *ep = d->input + 2 * ctxsize;
  write_le32(ep + 4, (3 << 1) | (4 << 3) | (packet << 16));
  write_le64(ep + 8, (u64)d->control.trb | 1);
  write_le32(ep + 16, 8);
  if (!command(11, (u64)d->input, d->slot, 0))
    return 0;
  if (!control(d, 0x80, 6, 0x100, 0, 18))
    return 0;
  if (d->buffer[0] != 18 || d->buffer[1] != 1)
    return 0;
  unsigned maxpacket =
      d->speed >= 4 ? (1u << (d->buffer[7] & 15)) : d->buffer[7];
  if (maxpacket != packet) {
    if (maxpacket != 8 && maxpacket != 16 && maxpacket != 32 &&
        maxpacket != 64 && maxpacket != 512)
      return 0;
    memset(d->input, 0, ctxsize * 33);
    write_le32(d->input + 4, 2);
    memcpy(d->input + 2 * ctxsize, d->context + ctxsize, ctxsize);
    write_le32(d->input + 2 * ctxsize + 4,
               (3 << 1) | (4 << 3) | (maxpacket << 16));
    if (!command(13, (u64)d->input, d->slot, 0))
      return 0;
  }
  if (!control(d, 0x80, 6, 0x200, 0, 9))
    return 0;
  if (d->buffer[0] != 9 || d->buffer[1] != 2)
    return 0;
  unsigned length = read_le16(d->buffer + 2);
  if (length < 9 || length > 4096)
    return 0;
  if (!control(d, 0x80, 6, 0x200, 0, length))
    return 0;
  unsigned config = d->buffer[5], in_packet = 0, out_packet = 0, in_burst = 0,
           out_burst = 0, last_ep = 0;
  int match = 0;
  for (unsigned at = 0; at + 2 <= length;) {
    unsigned n = d->buffer[at], kind = d->buffer[at + 1];
    if (n < 2 || at + n > length)
      return 0;
    const u8 *desc = d->buffer + at;
    if (kind == 4 && n >= 9) {
      if (d->in && d->out)
        break;
      match = desc[3] == 0 && desc[5] == 8 && desc[6] == 6 && desc[7] == 0x50;
    }
    if (match && kind == 5 && n >= 7 && (desc[3] & 3) == 2) {
      unsigned address = desc[2], number = address & 15;
      if (!number)
        return 0;
      last_ep = number * 2 + !!(address & 128);
      if (address & 128) {
        d->in = last_ep;
        in_packet = read_le16(desc + 4) & 2047;
      } else {
        d->out = last_ep;
        out_packet = read_le16(desc + 4) & 2047;
      }
    }
    if (match && kind == 48 && n >= 6) {
      if (last_ep == d->in)
        in_burst = desc[2];
      else if (last_ep == d->out)
        out_burst = desc[2];
    }
    at += n;
  }
  if (!d->in || !d->out || !in_packet || !out_packet || in_packet > 1024 ||
      out_packet > 1024)
    return 0;
  memset(d->input, 0, ctxsize * 33);
  write_le32(d->input + 4, 1 | (1u << d->in) | (1u << d->out));
  memcpy(d->input + ctxsize, d->context, ctxsize);
  unsigned highest = d->in > d->out ? d->in : d->out;
  write_le32(d->input + ctxsize, (d->speed << 20) | (highest << 27));
  endpoint(d, d->in, in_packet, in_burst);
  endpoint(d, d->out, out_packet, out_burst);
  if (!command(12, (u64)d->input, d->slot, 0) ||
      !control(d, 0, 9, config, 0, 0))
    return 0;
  u8 response[8], capacity[10] = {0x25}, sense[6] = {3, 0, 0, 0, 18, 0},
                  sense_data[18];
  int ok = 0;
  for (unsigned attempt = 0; attempt < 3 && !ok; attempt++) {
    ok = scsi(d, capacity, 10, response, 8);
    if (!ok) {
      scsi(d, sense, 6, sense_data, 18);
      delay(50);
    }
  }
  if (!ok || be32(response + 4) != 512 || be32(response) == 0xffffffffu)
    return 0;
  d->sectors = (u64)be32(response) + 1;
  d->online = 1;
  d->generation = ++media_generation;
  return 1;
}
void usb_rescan(void) {
  if (!ready || broken)
    return;
  /* Reuse a device's DMA storage only after Disable Slot completed. */
  for (unsigned i = 0; i < count; i++)
    if (devices[i].online && !connected(&devices[i])) {
      if (!broken && command(10, 0, devices[i].slot, 0)) {
        dcbaa[devices[i].slot] = 0;
        devices[i].slot = 0;
      } else
        broken = 1;
      devices[i].online = 0;
    }
  if (broken) {
    strcopy(status_text, "USB controller needs reboot", sizeof(status_text));
    return;
  }
  for (unsigned port = 1; port <= ports && !broken; port++) {
    int known = 0;
    for (unsigned i = 0; i < count; i++)
      if (devices[i].port == port && devices[i].online)
        known = 1;
    if (known || !(op[0x400 / 4 + (port - 1) * 4] & 1))
      continue;
    unsigned index = 0;
    while (index < count && (devices[index].online || devices[index].slot))
      index++;
    if (index == DEVICES)
      break;
    struct Device *d = &devices[index];
    if (enumerate(d, port)) {
      serial("USB STORAGE: ");
      serial_num(index);
      serial(" sectors=");
      serial_num(d->sectors);
      serial("\n");
      if (index == count)
        count++;
    } else {
      if (d->slot && !broken && command(10, 0, d->slot, 0)) {
        dcbaa[d->slot] = 0;
        d->slot = 0;
      }
      if (d->slot)
        broken = 1;
      strcopy(status_text,
              broken
                  ? "USB controller needs reboot"
                  : "USB device unsupported or not ready (need MSC Bulk-Only)",
              sizeof(status_text));
    }
  }
  unsigned online = 0;
  for (unsigned i = 0; i < count; i++)
    online += devices[i].online && connected(&devices[i]);
  if (online)
    strcopy(status_text, "USB xHCI / Bulk-Only / read only",
            sizeof(status_text));
  else if (!broken && (count || status_text[0] == 'R'))
    strcopy(status_text, "No USB storage attached; use USB refresh",
            sizeof(status_text));
}
unsigned usb_total(void) { return count; }
static struct Device *device(unsigned index) {
  return index < count && devices[index].online && connected(&devices[index])
             ? &devices[index]
             : 0;
}
u64 usb_sectors(unsigned index) {
  struct Device *d = device(index);
  return d ? d->sectors : 0;
}
u32 usb_generation(unsigned index) {
  struct Device *d = device(index);
  return d ? d->generation : 0;
}
const char *usb_status(void) { return status_text; }
int usb_read_sector(unsigned index, u64 lba, void *out) {
  struct Device *d = device(index);
  if (!d || !out || lba >= d->sectors || lba > 0xffffffffu)
    return 0;
  u8 cdb[10] = {0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, 0, 1, 0};
  return scsi(d, cdb, 10, out, 512);
}
void usb_init(void) {
  for (unsigned b = 0; b < 256; b++)
    for (unsigned d = 0; d < 32; d++)
      for (unsigned f = 0; f < 8; f++) {
        if ((pci(b, d, f, 8) >> 8) != 0x0c0330)
          continue;
        strcopy(status_text, "xHCI initialization failed (ownership/reset/DMA)", sizeof(status_text));
        u32 low = pci(b, d, f, 16);
        if (!low || (low & 1))
          continue;
        u64 base = low & ~15u;
        if ((low & 6) == 4)
          base |= (u64)pci(b, d, f, 20) << 32;
        u32 cmd = pci(b, d, f, 4);
        out32(0xcfc, (cmd & 65535) | 6);
        volatile u32 *cap = (volatile u32 *)base;
        unsigned xecp = cap[4] >> 16;
        for (unsigned limit = 0; xecp && limit < 64; limit++) {
          volatile u32 *ext = cap + xecp;
          u32 v = *ext;
          if ((v & 255) == 1) {
            *ext = v | (1u << 24);
            if (!wait_reg(ext, 1 << 16, 0, 2000))
              return;
            ext[1] = 0;
          }
          unsigned next = (v >> 8) & 255;
          if (!next)
            break;
          xecp += next;
        }
        op = (volatile u32 *)(base + (cap[0] & 255));
        if (op[0] & 1)
          op[0] &= ~1u;
        if (!wait_reg(op + 1, 1, 1, 1000))
          return;
        op[0] = 2;
        if (!wait_reg(op, 2, 0, 2000) || !wait_reg(op + 1, 1 << 11, 0, 2000))
          return;
        if (!(op[2] & 1))
          return;
        ports = cap[1] >> 24;
        ctxsize = (cap[4] & 4) ? 64 : 32;
        arena = dma + 128 * 1024;
        allocated = 0;
        dcbaa = alloc(256 * 8, 64);
        events = alloc(256 * sizeof(struct Trb), 64);
        u8 *table = alloc(16, 64);
        if (!dcbaa || !events || !table || !ring_init(&commands))
          return;
        unsigned scratch = ((cap[2] >> 27) & 31) | (((cap[2] >> 21) & 31) << 5);
        if (scratch > 64)
          return;
        if (scratch) {
          u64 *array = alloc(scratch * 8, 64);
          if (!array)
            return;
          dcbaa[0] = (u64)array;
          for (unsigned i = 0; i < scratch; i++) {
            void *page = alloc(4096, 4096);
            if (!page)
              return;
            array[i] = (u64)page;
          }
        }
        write64(op + 0x30 / 4, (u64)dcbaa);
        write64(op + 0x18 / 4, (u64)commands.trb | 1);
        unsigned slots = cap[1] & 255;
        op[0x38 / 4] = slots < 32 ? slots : 32;
        runtime = (volatile u32 *)(base + (cap[6] & ~31u));
        doorbell = (volatile u32 *)(base + (cap[5] & ~3u));
        write_le64(table, (u64)events);
        write_le32(table + 8, 256);
        runtime[0x20 / 4] = 0;
        runtime[0x28 / 4] = 1;
        write64(runtime + 0x30 / 4, (u64)table);
        write64(runtime + 0x38 / 4, (u64)events);
        event_index = 0;
        event_cycle = 1;
        fence();
        op[0] = 1;
        if (!wait_reg(op + 1, 1, 0, 1000))
          return;
        ready = 1;
        strcopy(status_text, "Ready / USB xHCI", sizeof(status_text));
        serial("USB: xHCI controller ready\n");
        usb_rescan();
        return;
      }
}
