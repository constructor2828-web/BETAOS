#include "os.h"
#include "web.h"
char web_text[16384], web_title[96];
static u16 io;
static u8 mac[6], gateway_mac[6];
static u32 local_ip, router_ip, dns_ip, mask_ip = 0xffffff00;
static u16 rx_offset;
static u64 rx_packets, rx_crossings, rx_bad_checksums, rx_resets;
static unsigned tx_index;
static u64 dhcp_time;
static int dhcp_state;
static char status_text[128] = "No RTL8139 adapter found";
static u32 server_ip, seq, ack;
static u16 server_port, client_port = 49152;
static int tcp_state;
static char response[1024 * 1024];
static int secure;
static unsigned redirects;
static int resource_kind, resource_node;
static char document_url[2048];
static char navigation_history[32][2048];
static unsigned navigation_count, navigation_index;
static int navigation_moving, document_verified;
static void history_record(const char *url) {
  if (navigation_moving)
    return;
  if (navigation_count && !strcmp(navigation_history[navigation_index], url))
    return;
  if (navigation_count)
    navigation_count = navigation_index + 1;
  if (navigation_count == 32) {
    memmove(navigation_history, navigation_history + 1,
            31 * sizeof(navigation_history[0]));
    navigation_count--;
  }
  navigation_index = navigation_count++;
  strcopy(navigation_history[navigation_index], url,
          sizeof(navigation_history[0]));
}
int browser_can_back(void) { return navigation_count && navigation_index > 0; }
int browser_can_forward(void) {
  return navigation_index + 1 < navigation_count;
}
int browser_verified(void) { return document_verified; }
void browser_back(void) {
  if (browser_can_back()) {
    navigation_index--;
    navigation_moving = 1;
    browser_load(navigation_history[navigation_index]);
    navigation_moving = 0;
  }
}
void browser_forward(void) {
  if (browser_can_forward()) {
    navigation_index++;
    navigation_moving = 1;
    browser_load(navigation_history[navigation_index]);
    navigation_moving = 0;
  }
}
void browser_home(void) { browser_load("about:start"); }

static char current_url[2048];
static u8 tls_wire[65536];
static unsigned wire_used;
static u8 pending_data[1200];
static unsigned pending_size, pending_tls, pending_tries;
static u32 pending_seq;
static u64 pending_at;
static char request_data[2304];
static unsigned request_size, request_sent;
static int browser_load_url(const char *, int);
static void tls_pump(void);
static unsigned response_len;
static int response_full;
static u16 dns_id = 0x4242;
static u32 dns_result;
static int dns_wait;
enum BrowserState {
  B_IDLE,
  B_DNS_ARP,
  B_DNS,
  B_HOST_ARP,
  B_CONNECT,
  B_TLS,
  B_RECEIVE
};
static enum BrowserState browser_state;
static char browser_host[128], browser_path[1792], browser_note[128] = "Ready";
static u64 browser_deadline, browser_retry, browser_started;
static u32 arp_expected_ip, last_response_len;
static void browser_step(void);
int browser_busy(void) { return browser_state != B_IDLE; }
const char *browser_status(void) { return browser_note; }
static void browser_status_set(const char *s) {
  strcopy(browser_note, s, sizeof(browser_note));
  gui_status(s);
}
static u16 be16(const u8 *p) { return ((u16)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) {
  return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static void put16(u8 *p, u16 v) {
  p[0] = v >> 8;
  p[1] = v;
}
static void put32(u8 *p, u32 v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}
static u32 sum_bytes(const u8 *p, unsigned n) {
  u32 s = 0;
  while (n > 1) {
    s += be16(p);
    p += 2;
    n -= 2;
  }
  if (n)
    s += (u16)*p << 8;
  return s;
}
static u16 finish_sum(u32 s) {
  while (s >> 16)
    s = (s & 65535) + (s >> 16);
  return ~s;
}
static void status(const char *s) {
  strcopy(status_text, s, sizeof(status_text));
  gui_status(s);
  serial("NET: ");
  serial(s);
  serial("\n");
}
int net_ready(void) { return dhcp_state == 3; }
const char *net_status(void) { return status_text; }
static u32 pci_read(u8 bus, u8 dev, u8 fn, u8 reg) {
  out32(0xcf8, 0x80000000 | ((u32)bus << 16) | ((u32)dev << 11) |
                   ((u32)fn << 8) | (reg & 0xfc));
  return in32(0xcfc);
}
static void pci_write(u8 bus, u8 dev, u8 fn, u8 reg, u32 value) {
  out32(0xcf8, 0x80000000 | ((u32)bus << 16) | ((u32)dev << 11) |
                   ((u32)fn << 8) | (reg & 0xfc));
  out32(0xcfc, value);
}
static void ethernet(const u8 *frame, unsigned n) {
  if (!io || n > 1514)
    return;
  unsigned index = tx_index++ % 4;
  u64 until = cycles() + clock_hz / 10;
  while (!(in32(io + 0x10 + index * 4) & (1 << 13))) {
    if (cycles() > until)
      return;
  }
  u8 *buf = dma + 16384 + 2048 * index;
  memcpy(buf, frame, n);
  if (n < 60) {
    memset(buf + n, 0, 60 - n);
    n = 60;
  }
  __asm__ volatile("" ::: "memory");
  out32(io + 0x20 + index * 4, (u32)(u64)buf);
  out32(io + 0x10 + index * 4, n);
}
static void arp_request(u32 ip) {
  u8 p[42] = {0};
  memset(p, 255, 6);
  memcpy(p + 6, mac, 6);
  put16(p + 12, 0x806);
  put16(p + 14, 1);
  put16(p + 16, 0x800);
  p[18] = 6;
  p[19] = 4;
  put16(p + 20, 1);
  memcpy(p + 22, mac, 6);
  put32(p + 28, local_ip);
  put32(p + 38, ip);
  ethernet(p, sizeof(p));
}
static int mac_known(void) {
  for (unsigned i = 0; i < 6; i++)
    if (gateway_mac[i])
      return 1;
  return 0;
}
static void ipv4(u32 dest, u8 protocol, const u8 *data, unsigned n,
                 int broadcast) {
  if (n > 1480)
    return;
  u8 p[1514] = {0};
  if (broadcast)
    memset(p, 255, 6);
  else
    memcpy(p, gateway_mac, 6);
  memcpy(p + 6, mac, 6);
  put16(p + 12, 0x800);
  u8 *ip = p + 14;
  ip[0] = 0x45;
  put16(ip + 2, 20 + n);
  ip[8] = 64;
  ip[9] = protocol;
  put32(ip + 12, local_ip);
  put32(ip + 16, dest);
  put16(ip + 10, finish_sum(sum_bytes(ip, 20)));
  memcpy(ip + 20, data, n);
  ethernet(p, 34 + n);
}
static void udp(u32 dest, u16 src, u16 dst, const u8 *data, unsigned n,
                int broadcast) {
  u8 p[1480] = {0};
  if (n > 1472)
    return;
  put16(p, src);
  put16(p + 2, dst);
  put16(p + 4, n + 8);
  memcpy(p + 8, data, n);
  ipv4(dest, 17, p, n + 8, broadcast);
}
static const u32 transaction = 0x42455441;
static u32 offered, dhcp_server;
static void dhcp_send(int request) {
  u8 p[320] = {0};
  p[0] = 1;
  p[1] = 1;
  p[2] = 6;
  put32(p + 4, transaction);
  put16(p + 10, 0x8000);
  memcpy(p + 28, mac, 6);
  put32(p + 236, 0x63825363);
  unsigned at = 240;
  p[at++] = 53;
  p[at++] = 1;
  p[at++] = request ? 3 : 1;
  p[at++] = 61;
  p[at++] = 7;
  p[at++] = 1;
  memcpy(p + at, mac, 6);
  at += 6;
  if (request) {
    p[at++] = 50;
    p[at++] = 4;
    put32(p + at, offered);
    at += 4;
    p[at++] = 54;
    p[at++] = 4;
    put32(p + at, dhcp_server);
    at += 4;
  }
  p[at++] = 55;
  p[at++] = 3;
  p[at++] = 1;
  p[at++] = 3;
  p[at++] = 6;
  p[at++] = 255;
  udp(0xffffffff, 68, 67, p, at, 1);
  dhcp_time = cycles();
}
static void dhcp_receive(const u8 *p, unsigned n) {
  if (n < 240 || p[0] != 2 || be32(p + 4) != transaction ||
      be32(p + 236) != 0x63825363)
    return;
  for (unsigned i = 0; i < 6; i++)
    if (p[28 + i] != mac[i])
      return;
  u8 type = 0;
  u32 gateway = 0, dns = 0, server = 0, mask = mask_ip;
  for (unsigned i = 240; i < n;) {
    u8 tag = p[i++];
    if (tag == 255)
      break;
    if (!tag)
      continue;
    if (i >= n)
      break;
    unsigned l = p[i++];
    if (l > n - i)
      return;
    if (tag == 53 && l == 1)
      type = p[i];
    if (tag == 54 && l == 4)
      server = be32(p + i);
    if (tag == 3 && l >= 4)
      gateway = be32(p + i);
    if (tag == 6 && l >= 4)
      dns = be32(p + i);
    if (tag == 1 && l == 4)
      mask = be32(p + i);
    i += l;
  }
  if (type == 2 && dhcp_state == 1) {
    offered = be32(p + 16);
    dhcp_server = server;
    dhcp_state = 2;
    dhcp_send(1);
  } else if (type == 5 && dhcp_state == 2) {
    local_ip = be32(p + 16);
    router_ip = gateway;
    dns_ip = dns;
    mask_ip = mask;
    dhcp_state = 3;
    status("Ethernet connected / DHCP configured");
    if (router_ip) {
      arp_expected_ip = router_ip;
      arp_request(router_ip);
    }
  }
}
static unsigned skip_name(const u8 *p, unsigned n, unsigned at) {
  for (unsigned count = 0; count < 128 && at < n; count++) {
    u8 len = p[at++];
    if (!len)
      return at;
    if ((len & 0xc0) == 0xc0)
      return at < n ? at + 1 : n + 1;
    if (len > 63 || at + len > n)
      return n + 1;
    at += len;
  }
  return n + 1;
}
static void dns_receive(const u8 *p, unsigned n) {
  if (!dns_wait || n < 12 || be16(p) != dns_id || !(p[2] & 0x80) || (p[3] & 15))
    return;
  unsigned at = 12, q = be16(p + 4), answers = be16(p + 6);
  if (q > 8 || answers > 64)
    return;
  for (unsigned i = 0; i < q; i++) {
    at = skip_name(p, n, at);
    if (at + 4 > n)
      return;
    at += 4;
  }
  for (unsigned i = 0; i < answers; i++) {
    at = skip_name(p, n, at);
    if (at + 10 > n)
      return;
    u16 type = be16(p + at), klass = be16(p + at + 2), len = be16(p + at + 8);
    at += 10;
    if (at + len > n)
      return;
    if (type == 1 && klass == 1 && len == 4) {
      dns_result = be32(p + at);
      dns_wait = 0;
      return;
    }
    at += len;
  }
}
static void tcp_send(u8 flags, const char *data, unsigned n) {
  u8 p[1480] = {0};
  if (n > 1400)
    return;
  put16(p, client_port);
  put16(p + 2, server_port);
  put32(p + 4, seq);
  put32(p + 8, ack);
  p[12] = 0x50;
  p[13] = flags;
  put16(p + 14, 8192);
  if (n)
    memcpy(p + 20, data, n);
  u8 pseudo[12];
  put32(pseudo, local_ip);
  put32(pseudo + 4, server_ip);
  pseudo[8] = 0;
  pseudo[9] = 6;
  put16(pseudo + 10, n + 20);
  put16(p + 16, finish_sum(sum_bytes(pseudo, 12) + sum_bytes(p, n + 20)));
  ipv4(server_ip, 6, p, n + 20, 0);
}
static void response_add(const void *data, unsigned n) {
  unsigned take = n;
  if (take > sizeof(response) - 1 - response_len) {
    take = sizeof(response) - 1 - response_len;
    response_full = 1;
  }
  memcpy(response + response_len, data, take);
  response_len += take;
  response[response_len] = 0;
}
static int tcp_queue(const void *data, unsigned n, int tls) {
  if (pending_size || !n || n > sizeof(pending_data))
    return 0;
  memcpy(pending_data, data, n);
  pending_size = n;
  pending_tls = tls;
  pending_tries = 0;
  pending_seq = seq;
  pending_at = cycles();
  tcp_send(0x18, (const char *)pending_data, n);
  seq += n;
  return 1;
}
static void tcp_receive(u32 source, const u8 *p, unsigned n) {
  if (!tcp_state || source != server_ip || n < 20 || be16(p) != server_port ||
      be16(p + 2) != client_port)
    return;
  unsigned header = (p[12] >> 4) * 4;
  if (header < 20 || header > n)
    return;
  if (p[13] & 4) {
    tcp_state = -1;
    return;
  }
  if (pending_size && (p[13] & 16) &&
      be32(p + 8) == pending_seq + pending_size) {
    unsigned sent = pending_size, encrypted = pending_tls;
    pending_size = 0;
    if (encrypted)
      tls_output_ack(sent);
  }
  u32 their_seq = be32(p + 4);
  unsigned payload = n - header;
  if (tcp_state == 1 && (p[13] & 0x12) == 0x12 && be32(p + 8) == seq + 1) {
    seq++;
    ack = their_seq + 1;
    tcp_send(0x10, 0, 0);
    tcp_state = 2;
    return;
  }
  if (tcp_state >= 2 && payload) {
    if (their_seq == ack) {
      if (secure) {
        if (payload > sizeof(tls_wire) - wire_used) {
          tcp_send(0x10, 0, 0);
          return;
        }
        memcpy(tls_wire + wire_used, p + header, payload);
        wire_used += payload;
      } else
        response_add(p + header, payload);
      ack += payload;
    }
    tcp_send(0x10, 0, 0);
  }
  if (tcp_state >= 2 && (p[13] & 1) && their_seq + payload == ack) {
    ack++;
    tcp_send(0x10, 0, 0);
    tcp_state = 4;
  }
}
static void receive(const u8 *p, unsigned n) {
  if (n < 14)
    return;
  u16 type = be16(p + 12);
  if (type == 0x806 && n >= 42 && be16(p + 14) == 1 && be16(p + 16) == 0x800 &&
      p[18] == 6 && p[19] == 4) {
    u32 sender = be32(p + 28);
    if (be16(p + 20) == 2 && be32(p + 38) == local_ip) {
      if (sender == arp_expected_ip)
        memcpy(gateway_mac, p + 22, 6);
    }
    if (be16(p + 20) == 1 && be32(p + 38) == local_ip && local_ip) {
      u8 reply[42];
      memcpy(reply, p, 42);
      memcpy(reply, p + 6, 6);
      memcpy(reply + 6, mac, 6);
      put16(reply + 20, 2);
      memcpy(reply + 32, p + 22, 6);
      put32(reply + 38, sender);
      memcpy(reply + 22, mac, 6);
      put32(reply + 28, local_ip);
      ethernet(reply, 42);
    }
    return;
  }
  if (type != 0x800 || n < 34)
    return;
  const u8 *ip = p + 14;
  unsigned h = (ip[0] & 15) * 4, total = be16(ip + 2);
  if ((ip[0] >> 4) != 4 || h < 20 || total < h || total > n - 14 ||
      (be16(ip + 6) & 0x3fff) || finish_sum(sum_bytes(ip, h)))
    return;
  u32 dest = be32(ip + 16);
  if (dest != local_ip && dest != 0xffffffff && dest != offered)
    return;
  const u8 *data = ip + h;
  unsigned len = total - h;
  if (ip[9] == 17 && len >= 8) {
    unsigned ul = be16(data + 4);
    if (ul < 8 || ul > len)
      return;
    u16 port = be16(data + 2);
    if (port == 68)
      dhcp_receive(data + 8, ul - 8);
    else if (port == 53000 && be16(data) == 53 && be32(ip + 12) == dns_ip)
      dns_receive(data + 8, ul - 8);
  }
  if (ip[9] == 6) {
    u8 pseudo[12];
    memcpy(pseudo, ip + 12, 8);
    pseudo[8] = 0;
    pseudo[9] = 6;
    put16(pseudo + 10, len);
    if (!finish_sum(sum_bytes(pseudo, 12) + sum_bytes(data, len)))
      tcp_receive(be32(ip + 12), data, len);
    else
      rx_bad_checksums++;
  }
}
void net_init(void) {
  for (unsigned b = 0; b < 256 && !io; b++)
    for (unsigned d = 0; d < 32 && !io; d++)
      for (unsigned f = 0; f < 8; f++)
        if (pci_read(b, d, f, 0) == 0x813910ec) {
          u32 bar = pci_read(b, d, f, 0x10);
          if (!(bar & 1))
            continue;
          io = bar & ~3;
          u32 cmd = pci_read(b, d, f, 4);
          pci_write(b, d, f, 4, (cmd & 0xffff) | 5);
          break;
        }
  if (!io) {
    status("No RTL8139 adapter; network unavailable");
    return;
  }
  out8(io + 0x52, 0);
  out8(io + 0x37, 0x10);
  u64 until = cycles() + clock_hz;
  while (in8(io + 0x37) & 0x10)
    if (cycles() > until) {
      io = 0;
      status("RTL8139 reset timed out");
      return;
    }
  for (unsigned i = 0; i < 6; i++)
    mac[i] = in8(io + i);
  out32(io + 0x30, (u32)(u64)dma);
  out16(io + 0x3c, 0);
  out16(io + 0x3e, 0xffff);
  out32(io + 0x44, 0x8f);
  out32(io + 0x40, 0x03000700);
  out8(io + 0x37, 0x0c);
  out16(io + 0x38, 0xfff0);
  status("RTL8139 detected; requesting DHCP lease");
  dhcp_state = 1;
  dhcp_send(0);
}
void net_poll(void) {
  if (!io)
    return;
  for (unsigned count = 0; count < 16 && !(in8(io + 0x37) & 1); count++) {
    __asm__ volatile("" ::: "memory");
    u16 flags = *(volatile u16 *)(dma + rx_offset),
        len = *(volatile u16 *)(dma + rx_offset + 2);
    if (!(flags & 1) || len < 4 || len > 1518) {
      rx_resets++;
      out8(io + 0x37, 0);
      rx_offset = 0;
      out32(io + 0x30, (u32)(u64)dma);
      out8(io + 0x37, 0x0c);
      out16(io + 0x38, 0xfff0);
      break;
    }
    rx_packets++;
    if (rx_offset + 4u + len > 8192)
      rx_crossings++;
    u8 frame[1518];
    for (unsigned i = 0; i < (unsigned)len - 4; i++)
      /* RCR.NoWrap places a crossing payload contiguously in the reserved spill
       * area. */
      frame[i] = dma[rx_offset + 4 + i];
    receive(frame, len - 4);
    rx_offset = (rx_offset + len + 4 + 3) & ~3;
    rx_offset %= 8192;
    out16(io + 0x38, rx_offset - 16);
  }
  out16(io + 0x3e, 0xffff);
  if (dhcp_state > 0 && dhcp_state < 3 && cycles() - dhcp_time > clock_hz * 3)
    dhcp_send(dhcp_state == 2);
  browser_step();
}
static u32 parse_ip(const char *s) {
  u32 result = 0;
  for (unsigned i = 0; i < 4; i++) {
    unsigned n = 0, digits = 0;
    while (*s >= '0' && *s <= '9') {
      n = n * 10 + (*s++ - '0');
      if (++digits > 3 || n > 255)
        return 0;
    }
    if (!digits)
      return 0;
    result = (result << 8) | n;
    if (i < 3) {
      if (*s++ != '.')
        return 0;
    } else if (*s)
      return 0;
  }
  return result;
}
static int dns_start(void) {
  const char *host = browser_host;
  u8 p[256] = {0};
  if (!dns_wait)
    dns_id++;
  put16(p, dns_id);
  put16(p + 2, 0x100);
  put16(p + 4, 1);
  unsigned at = 12;
  const char *s = host;
  while (*s) {
    unsigned start = at++;
    unsigned len = 0;
    while (*s && *s != '.') {
      if (at >= 249 || ++len > 63)
        return 0;
      p[at++] = *s++;
    }
    if (!len)
      return 0;
    p[start] = len;
    if (*s)
      s++;
  }
  p[at++] = 0;
  put16(p + at, 1);
  put16(p + at + 2, 1);
  at += 4;
  dns_wait = 1;
  dns_result = 0;
  udp(dns_ip, 53000, 53, p, at, 0);
  return 1;
}
static int begin_arp(u32 dest, enum BrowserState next) {
  arp_expected_ip = (dest & mask_ip) == (local_ip & mask_ip) ? dest : router_ip;
  if (!arp_expected_ip)
    return 0;
  memset(gateway_mac, 0, sizeof(gateway_mac));
  browser_state = next;
  browser_deadline = cycles() + clock_hz * 2;
  browser_retry = cycles() + clock_hz;
  arp_request(arp_expected_ip);
  return 1;
}
void browser_cancel(void) {
  if (browser_state == B_IDLE)
    return;
  if (tcp_state >= 1)
    tcp_send(0x14, 0, 0);
  tcp_state = 0;
  pending_size = wire_used = request_size = request_sent = 0;
  tls_stop();
  dns_wait = 0;
  browser_state = B_IDLE;
  browser_status_set("Loading canceled");
  serial("HTTP CANCELLED\n");
}
static int fail(const char *why) {
  if (tcp_state >= 1)
    tcp_send(0x14, 0, 0);
  tcp_state = 0;
  pending_size = wire_used = request_size = request_sent = 0;
  tls_stop();
  dns_wait = 0;
  browser_state = B_IDLE;
  strcopy(browser_note, why, sizeof(browser_note));
  if (resource_kind) {
    int kind = resource_kind, node = resource_node;
    resource_kind = 0;
    web_resource(kind, node, "", 0, 0);
    gui_status(why);
    return 0;
  }
  strcopy(web_title, "BETA Browser / unable to load", sizeof(web_title));
  strcopy(web_text, why, sizeof(web_text));
  web_document("about:error", why);
  gui_status(why);
  serial("HTTP ERROR: ");
  serial(why);
  serial("\n");
  return 0;
}
static void render_html(const char *body) {
  unsigned at = 0;
  int tag = 0, hidden = 0, space = 0;
  char name[24];
  unsigned ni = 0;
  for (const char *s = body; *s && at < sizeof(web_text) - 2; s++) {
    if (*s == '<') {
      tag = 1;
      ni = 0;
      continue;
    }
    if (tag) {
      if (*s == '>') {
        name[ni] = 0;
        tag = 0;
        if (starts(name, "script") || starts(name, "style"))
          hidden = 1;
        if (starts(name, "/script") || starts(name, "/style"))
          hidden = 0;
        if (!hidden && (!strcmp(name, "br") || !strcmp(name, "br/") ||
                        starts(name, "/p") || starts(name, "/h") ||
                        starts(name, "/div") || starts(name, "/li"))) {
          if (at && web_text[at - 1] != '\n')
            web_text[at++] = '\n';
          space = 1;
        }
      } else if (ni < sizeof(name) - 1) {
        char c = *s;
        if (c >= 'A' && c <= 'Z')
          c += 32;
        name[ni++] = c;
      }
      continue;
    }
    if (hidden)
      continue;
    char c = *s;
    if (c == '&') {
      if (starts(s, "&amp;")) {
        c = '&';
        s += 4;
      } else if (starts(s, "&lt;")) {
        c = '<';
        s += 3;
      } else if (starts(s, "&gt;")) {
        c = '>';
        s += 3;
      } else if (starts(s, "&nbsp;")) {
        c = ' ';
        s += 5;
      } else if (starts(s, "&quot;")) {
        c = '"';
        s += 5;
      }
    }
    if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
      if (!space && at)
        web_text[at++] = ' ';
      space = 1;
    } else if ((u8)c >= 32 && (u8)c < 127) {
      web_text[at++] = c;
      space = 0;
    }
  }
  web_text[at] = 0;
}
struct HttpMeta {
  unsigned end, length;
  int has_length, invalid, unsupported, chunked;
};
static int header_name(const char *line, unsigned length, const char *name) {
  unsigned n = strlen(name);
  if (length < n + 1 || line[n] != ':')
    return 0;
  for (unsigned i = 0; i < n; i++) {
    char c = line[i];
    if (c >= 'A' && c <= 'Z')
      c += 32;
    if (c != name[i])
      return 0;
  }
  return 1;
}
static struct HttpMeta http_meta(void) {
  struct HttpMeta meta = {0};
  for (unsigned i = 0; i + 3 < response_len; i++)
    if (response[i] == '\r' && response[i + 1] == '\n' &&
        response[i + 2] == '\r' && response[i + 3] == '\n') {
      meta.end = i + 4;
      break;
    }
  if (!meta.end)
    return meta;
  unsigned line = 0;
  while (line + 1 < meta.end &&
         !(response[line] == '\r' && response[line + 1] == '\n'))
    line++;
  line += 2;
  while (line + 2 < meta.end) {
    unsigned end = line;
    while (end + 1 < meta.end &&
           !(response[end] == '\r' && response[end + 1] == '\n'))
      end++;
    if (header_name(response + line, end - line, "transfer-encoding")) {
      unsigned pos = line + 18;
      while (pos < end && (response[pos] == ' ' || response[pos] == '\t'))
        pos++;
      int yes = end - pos == 7;
      for (unsigned j = 0; yes && j < 7; j++) {
        char c = response[pos + j];
        if (c >= 'A' && c <= 'Z')
          c += 32;
        if (c != "chunked"[j])
          yes = 0;
      }
      if (yes)
        meta.chunked = 1;
      else
        meta.unsupported = 1;
    }
    if (header_name(response + line, end - line, "content-encoding")) {
      unsigned at = line + 17;
      while (at < end && (response[at] == ' ' || response[at] == '\t'))
        at++;
      int identity = end - at == 8;
      for (unsigned j = 0; identity && j < 8; j++) {
        char c = response[at + j];
        if (c >= 'A' && c <= 'Z')
          c += 32;
        if (c != "identity"[j])
          identity = 0;
      }
      if (!identity)
        meta.unsupported = 1;
    }
    if (header_name(response + line, end - line, "content-length")) {
      unsigned at = line + 15, value = 0, digits = 0;
      while (at < end && (response[at] == ' ' || response[at] == '\t'))
        at++;
      while (at < end && response[at] >= '0' && response[at] <= '9') {
        if (++digits > 8) {
          meta.invalid = 1;
          break;
        }
        value = value * 10 + response[at++] - '0';
      }
      while (at < end && (response[at] == ' ' || response[at] == '\t'))
        at++;
      if (!digits || at != end || (meta.has_length && meta.length != value))
        meta.invalid = 1;
      meta.has_length = 1;
      meta.length = value;
    }
    line = end + 2;
  }
  if (meta.chunked && meta.has_length)
    meta.invalid = 1;
  return meta;
}
static unsigned chunk_at, chunk_write, chunk_left, chunk_state, chunk_digits;
static int chunks(unsigned end) {
  if (!chunk_at)
    chunk_at = end;
  while (chunk_at < response_len) {
    u8 c = response[chunk_at];
    if (chunk_state == 0) {
      if (c == '\r' && chunk_digits) {
        chunk_state = 1;
        chunk_at++;
        continue;
      }
      if (c == ';' && chunk_digits) {
        chunk_state = 6;
        chunk_at++;
        continue;
      }
      unsigned v = c >= '0' && c <= '9'   ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                          : 16;
      if (v == 16 || chunk_digits++ >= 8 ||
          chunk_left > (sizeof(response) >> 4))
        return -1;
      chunk_left = chunk_left * 16 + v;
      chunk_at++;
    } else if (chunk_state == 1) {
      if (c != '\n')
        return -1;
      chunk_at++;
      chunk_state = chunk_left ? 2 : 5;
    } else if (chunk_state == 2) {
      unsigned n = response_len - chunk_at;
      if (n > chunk_left)
        n = chunk_left;
      memmove(response + end + chunk_write, response + chunk_at, n);
      chunk_write += n;
      chunk_at += n;
      chunk_left -= n;
      if (!chunk_left)
        chunk_state = 3;
    } else if (chunk_state == 3) {
      if (c != '\r')
        return -1;
      chunk_at++;
      chunk_state = 4;
    } else if (chunk_state == 4) {
      if (c != '\n')
        return -1;
      chunk_at++;
      chunk_state = chunk_left = chunk_digits = 0;
    } else if (chunk_state == 5) {
      /* Trailer terminates at an empty CRLF line; bound its total length. */
      unsigned pos = chunk_at;
      while (pos + 1 < response_len &&
             !(response[pos] == '\r' && response[pos + 1] == '\n'))
        pos++;
      if (pos + 1 == response_len)
        return 0;
      if (pos - chunk_at > 8192)
        return -1;
      if (pos == chunk_at) {
        chunk_at += 2;
        chunk_state = 7;
        return 1;
      }
      chunk_at = pos + 2;
    } else if (chunk_state == 6) {
      if (c == '\r')
        chunk_state = 1;
      else if (c == '\n' || c < 32)
        return -1;
      chunk_at++;
    } else
      return 1;
  }
  return chunk_state == 7;
}
static int location(char *out, unsigned cap) {
  struct HttpMeta m = http_meta();
  for (unsigned line = 0; line < m.end;) {
    unsigned end = line;
    while (end + 1 < m.end &&
           !(response[end] == '\r' && response[end + 1] == '\n'))
      end++;
    if (header_name(response + line, end - line, "location")) {
      unsigned at = line + 9;
      while (at < end && (response[at] == ' ' || response[at] == '\t'))
        at++;
      unsigned n = end - at;
      if (!n || n >= cap)
        return 0;
      memcpy(out, response + at, n);
      out[n] = 0;
      return 1;
    }
    line = end + 2;
  }
  return 0;
}
static void tls_pump(void) {
  for (unsigned round = 0; round < 16; round++) {
    unsigned progress = 0;
    u8 data[4096];
    size_t n = tls_plain(data, sizeof(data));
    if (n) {
      response_add(data, n);
      progress = 1;
    }
    if (wire_used) {
      size_t got = tls_feed(tls_wire, wire_used);
      if (got) {
        memmove(tls_wire, tls_wire + got, wire_used - got);
        wire_used -= got;
        progress = 1;
      }
    }
    if (!pending_size) {
      size_t count = 0;
      const void *out = tls_output(&count);
      if (count) {
        if (count > 1200)
          count = 1200;
        tcp_queue(out, count, 1);
      }
    }
    if (!progress)
      break;
  }
}
static void browser_finish(int complete) {
  struct HttpMeta meta = http_meta();
  tcp_send(0x14, 0, 0);
  tcp_state = 0;
  pending_size = 0;
  int connection_verified = secure && tls_ready();
  tls_stop();
  browser_state = B_IDLE;
  if (!response_len) {
    fail("No HTTP response. Request timed out or connection reset.");
    return;
  }
  if (!meta.end || meta.end > 8192) {
    fail("Malformed or oversized HTTP headers.");
    return;
  }
  if (!starts(response, "HTTP/1.0 ") && !starts(response, "HTTP/1.1 ")) {
    fail("Unsupported response protocol.");
    return;
  }
  if (meta.invalid) {
    fail("Invalid or conflicting Content-Length header.");
    return;
  }
  if (meta.unsupported) {
    fail("Unsupported Content-Encoding or Transfer-Encoding. Request identity "
         "encoding.");
    return;
  }
  if (response[9] < '0' || response[9] > '9' || response[10] < '0' ||
      response[10] > '9' || response[11] < '0' || response[11] > '9') {
    fail("Malformed HTTP status code.");
    return;
  }
  unsigned code = (response[9] - '0') * 100 + (response[10] - '0') * 10 +
                  response[11] - '0';
  if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
    char target[2048], next[2048];
    if (!location(target, sizeof(target)) || ++redirects > 8) {
      fail("Missing redirect target or too many redirects.");
      return;
    }
    if (starts(target, "https://") || starts(target, "http://"))
      strcopy(next, target, sizeof(next));
    else if (starts(target, "//")) {
      strcopy(next, secure ? "https:" : "http:", sizeof(next));
      strcopy(next + strlen(next), target, sizeof(next) - strlen(next));
    } else {
      const char *scheme = secure ? "https://" : "http://";
      strcopy(next, scheme, sizeof(next));
      strcopy(next + strlen(next), browser_host, sizeof(next) - strlen(next));
      if (server_port != (secure ? 443 : 80)) {
        char digits[8], rev[8];
        unsigned n = 0, at = 0, v = server_port;
        do {
          rev[n++] = '0' + v % 10;
          v /= 10;
        } while (v);
        digits[at++] = ':';
        while (n)
          digits[at++] = rev[--n];
        digits[at] = 0;
        strcopy(next + strlen(next), digits, sizeof(next) - strlen(next));
      }
      if (target[0] != '/') {
        char base[1792];
        strcopy(base, browser_path, sizeof(base));
        unsigned n = strlen(base);
        while (n && base[n - 1] != '/')
          n--;
        base[n] = 0;
        strcopy(next + strlen(next), base, sizeof(next) - strlen(next));
      }
      strcopy(next + strlen(next), target, sizeof(next) - strlen(next));
    }
    if (secure && starts(next, "http://")) {
      fail("HTTPS redirect to HTTP refused.");
      return;
    }
    serial("HTTP REDIRECT: ");
    serial(next);
    serial("\n");
    browser_load_url(next, 1);
    return;
  }
  if (meta.chunked)
    response[meta.end + chunk_write] = 0;
  if (meta.has_length && response_len - meta.end < meta.length)
    complete = 0;
  if (meta.has_length && meta.length <= response_len - meta.end)
    response[meta.end + meta.length] = 0;
  if (resource_kind) {
    int kind = resource_kind, node = resource_node;
    resource_kind = 0;
    web_resource(kind, node, response + meta.end,
                 meta.chunked ? chunk_write
                 : meta.has_length && meta.length < response_len - meta.end
                     ? meta.length
                     : response_len - meta.end,
                 complete && !response_full ? code : 0);
    browser_status_set("Resources loaded / experimental browser");
    return;
  }
  strcopy(web_title, browser_host, sizeof(web_title));
  document_verified = connection_verified;
  strcopy(document_url, current_url, sizeof(document_url));
  if (navigation_count)
    strcopy(navigation_history[navigation_index], current_url,
            sizeof(navigation_history[0]));
  web_document(current_url, response + meta.end);
  render_html(response + meta.end);
  serial("PAGE TEXT: ");
  char preview[257];
  strcopy(preview, web_text, sizeof(preview));
  serial(preview);
  serial("\n");
  unsigned length = strlen(web_text);
  if (length > 320) {
    serial("PAGE TAIL: ");
    serial(web_text + length - 64);
    serial("\n");
  }
  browser_status_set(response_full ? "Page truncated at 1 MiB"
                     : complete    ? (secure ? "HTTPS / certificate verified"
                                             : "HTTP page loaded")
                                   : "Partial response / connection closed");
  serial(complete && !response_full ? "HTTP OK: " : "HTTP PARTIAL: ");
  serial(browser_host);
  serial(" / ");
  serial_num(response_len);
  serial(" bytes\n");
}
static void send_request(void) {
  char *request = request_data;
  strcopy(request, "GET ", sizeof(request_data));
  strcopy(request + strlen(request), browser_path,
          sizeof(request_data) - strlen(request));
  strcopy(request + strlen(request),
          " HTTP/1.1\r\nHost: ", sizeof(request_data) - strlen(request));
  strcopy(request + strlen(request), browser_host,
          sizeof(request_data) - strlen(request));
  if (server_port != (secure ? 443 : 80)) {
    char reverse[8], port[8];
    unsigned i = 0, j = 0, v = server_port;
    do {
      reverse[i++] = '0' + v % 10;
      v /= 10;
    } while (v);
    port[j++] = ':';
    while (i)
      port[j++] = reverse[--i];
    port[j] = 0;
    strcopy(request + strlen(request), port,
            sizeof(request_data) - strlen(request));
  }
  strcopy(
      request + strlen(request),
      "\r\nUser-Agent: BETAOS/" BETA_VERSION "\r\nAccept: "
      "text/html,application/xhtml+xml,*/*;q=0.8\r\nAccept-Language: "
      "en-US,en;q=0.9\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
      sizeof(request_data) - strlen(request));
  request_size = strlen(request);
  request_sent = 0;
  tcp_state = 3;
  browser_state = B_RECEIVE;
  browser_started = cycles();
  browser_deadline = cycles() + clock_hz * 15;
  last_response_len = 0;
  browser_status_set("Receiving page / Esc cancels");
}
static void browser_step(void) {
  if (browser_state == B_IDLE)
    return;
  u64 now = cycles();
  if (pending_size && now - pending_at > clock_hz / 2) {
    if (++pending_tries > 8) {
      fail("TCP retransmission timeout.");
      return;
    }
    u32 saved = seq;
    seq = pending_seq;
    tcp_send(0x18, (const char *)pending_data, pending_size);
    seq = saved;
    pending_at = now;
  }
  if (secure && (browser_state == B_TLS || browser_state == B_RECEIVE)) {
    tls_pump();
    int error = tls_error();
    if (error) {
      serial("TLS ERROR: ");
      serial_num(error);
      serial("\n");
      fail("TLS validation failed: certificate, hostname, date or protocol "
           "error.");
      return;
    }
  }
  if (browser_state == B_TLS) {
    if (tls_ready()) {
      serial("TLS VERIFIED: ");
      serial(browser_host);
      serial("\n");
      send_request();
    } else if (tcp_state < 0 || tcp_state == 4 || now > browser_deadline)
      fail("TLS handshake failed or timed out.");
    return;
  }
  if (request_sent < request_size && browser_state == B_RECEIVE) {
    unsigned n = request_size - request_sent;
    if (secure)
      request_sent += tls_write(request_data + request_sent, n);
    else if (!pending_size) {
      if (n > 1200)
        n = 1200;
      if (tcp_queue(request_data + request_sent, n, 0))
        request_sent += n;
    }
  }
  if (browser_state == B_DNS_ARP || browser_state == B_HOST_ARP) {
    if (mac_known()) {
      if (browser_state == B_DNS_ARP) {
        if (!dns_start()) {
          fail("Invalid DNS hostname.");
          return;
        }
        browser_state = B_DNS;
        browser_deadline = now + clock_hz * 3;
        browser_retry = now + clock_hz;
        browser_status_set("Resolving DNS name");
      } else {
        seq = (u32)cycles();
        ack = 0;
        client_port++;
        if (client_port < 49152)
          client_port = 49152;
        tcp_state = 1;
        response_len = 0;
        response_full = 0;
        response[0] = 0;
        wire_used = pending_size = 0;
        chunk_at = chunk_write = chunk_left = chunk_state = chunk_digits = 0;
        tcp_send(2, 0, 0);
        browser_state = B_CONNECT;
        browser_deadline = now + clock_hz * 4;
        browser_retry = now + clock_hz;
        browser_status_set("Connecting TCP / Esc cancels");
      }
    } else if (now > browser_deadline)
      fail("ARP resolution failed. Gateway or host did not respond.");
    else if (now > browser_retry) {
      arp_request(arp_expected_ip);
      browser_retry = now + clock_hz;
    }
    return;
  }
  if (browser_state == B_DNS) {
    if (!dns_wait && dns_result) {
      server_ip = dns_result;
      if (!begin_arp(server_ip, B_HOST_ARP))
        fail("No route to host.");
    } else if (now > browser_deadline)
      fail("DNS lookup failed. Check the DHCP DNS server or use an IPv4 "
           "address.");
    else if (now > browser_retry) {
      dns_start();
      browser_retry = now + clock_hz;
    }
    return;
  }
  if (browser_state == B_CONNECT) {
    if (tcp_state == 2) {
      if (secure) {
        if (!tls_start(browser_host)) {
          int error = tls_error();
          fail(error == -1 ? "TLS requires a valid firmware UTC clock."
               : error == -2
                   ? "TLS requires firmware RNG or RDRAND (QEMU: -cpu max)."
                   : "TLS initialization failed.");
          return;
        }
        browser_state = B_TLS;
        browser_deadline = now + clock_hz * 15;
        browser_status_set("Negotiating TLS / verifying certificate");
      } else
        send_request();
    } else if (tcp_state < 0 || now > browser_deadline)
      fail("TCP connection failed or timed out.");
    else if (now > browser_retry) {
      tcp_send(2, 0, 0);
      browser_retry = now + clock_hz;
    }
    return;
  }
  if (browser_state == B_RECEIVE) {
    struct HttpMeta meta = http_meta();
    if (response_len != last_response_len) {
      last_response_len = response_len;
      browser_deadline = now + clock_hz * 8;
    }
    int chunk_status = meta.end && meta.chunked ? chunks(meta.end) : 0;
    if (chunk_status < 0) {
      fail("Malformed chunked HTTP response.");
      return;
    }
    if (meta.invalid || meta.unsupported || chunk_status > 0 ||
        (meta.end && meta.has_length &&
         response_len - meta.end >= meta.length)) {
      browser_finish(!meta.invalid && !meta.unsupported);
      return;
    }
    if ((tcp_state == 4 && !wire_used) || tcp_state < 0 || response_full ||
        now > browser_deadline || now - browser_started > clock_hz * 30)
      browser_finish(tcp_state == 4);
  }
}
static int browser_load_url(const char *url, int redirect) {
  browser_cancel();
  if (!redirect)
    redirects = 0;
  if (!url)
    return fail("URL required.");
  secure = starts(url, "https://");
  if (!secure && !starts(url, "http://"))
    return fail("Use an HTTP or HTTPS URL.");
  strcopy(current_url, url, sizeof(current_url));
  if (!net_ready())
    return fail("Ethernet is not configured. An RTL8139 adapter and a DHCP "
                "server are required. Try again after a few seconds.");
  char *host = browser_host, *path = browser_path;
  unsigned at = 0;
  const char *s = url + (secure ? 8 : 7);
  while (*s && *s != '/' && *s != ':' && *s != '?' && *s != '#') {
    if (at >= sizeof(browser_host) - 1 || *s <= 32 || *s == '@')
      return fail("Invalid or oversized hostname.");
    host[at++] = *s++;
  }
  host[at] = 0;
  if (!at)
    return fail("Hostname required.");
  server_port = secure ? 443 : 80;
  if (*s == ':') {
    s++;
    unsigned port = 0, digits = 0;
    while (*s >= '0' && *s <= '9') {
      port = port * 10 + *s++ - '0';
      if (++digits > 5 || port > 65535)
        return fail("Invalid port.");
    }
    if (!digits || !port)
      return fail("Invalid port.");
    server_port = port;
  }
  if (*s && *s != '/' && *s != '?' && *s != '#')
    return fail("Invalid URL.");
  if (strlen(s) >= sizeof(browser_path))
    return fail("URL path too long.");
  if (*s == '?' || *s == '#') {
    path[0] = '/';
    strcopy(path + 1, s, sizeof(browser_path) - 1);
  } else
    strcopy(path, *s ? s : "/", sizeof(browser_path));
  for (unsigned i = 0; path[i]; i++)
    if (path[i] == '#') {
      path[i] = 0;
      break;
    }
  for (unsigned i = 0; path[i]; i++)
    if (path[i] <= 32)
      return fail("URL contains whitespace.");
  if (!resource_kind)
    strcopy(web_title, host, sizeof(web_title));
  strcopy(web_text,
          "Loading page... You can keep using other "
          "applications.\nEsc cancels loading.",
          sizeof(web_text));
  server_ip = parse_ip(host);
  if (server_ip) {
    browser_status_set("Looking for host on the network");
    if (!begin_arp(server_ip, B_HOST_ARP))
      return fail("No route to host.");
  } else {
    if (!dns_ip)
      return fail("No DNS server configured.");
    browser_status_set("Looking for DNS server");
    if (!begin_arp(dns_ip, B_DNS_ARP))
      return fail("No route to DNS server.");
  }
  return 1;
}

static void stat_number(void (*out)(const char *), u64 n) {
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
void net_stats(void (*out)(const char *)) {
  out("NETSTATS packets=");
  stat_number(out, rx_packets);
  out(" crossings=");
  stat_number(out, rx_crossings);
  out(" bad_checksums=");
  stat_number(out, rx_bad_checksums);
  out(" resets=");
  stat_number(out, rx_resets);
  out("\n");
}

int browser_load(const char *text) {
  char url[2048];
  if (!text)
    return fail("URL or search required.");
  while (*text == ' ')
    text++;
  if (!strcmp(text, "about:start")) {
    resource_kind = 0;
    browser_cancel();
    web_cancel_pending();
    document_verified = 0;
    strcopy(document_url, "about:start", sizeof(document_url));
    history_record(document_url);
    web_home();
    browser_status_set("Ready / Ctrl-L: address or search");
    return 1;
  }
  if (!*text || !strcmp(text, "google"))
    strcopy(url, "https://www.google.com/", sizeof(url));
  else if (starts(text, "http://") || starts(text, "https://"))
    strcopy(url, text, sizeof(url));
  else {
    int domain = 0, space = 0;
    for (const char *p = text; *p; p++) {
      domain |= *p == '.' || *p == ':';
      space |= (u8)*p <= 32;
    }
    if (domain && !space) {
      strcopy(url, "https://", sizeof(url));
      strcopy(url + 8, text, sizeof(url) - 8);
    } else {
      strcopy(url, "https://www.google.com/search?gbv=1&udm=14&hl=en&q=",
              sizeof(url));
      unsigned at = strlen(url);
      const char *hex = "0123456789ABCDEF";
      for (const u8 *p = (const u8 *)text; *p; p++) {
        u8 bytes[2] = {*p, 0};
        unsigned n = 1;
        if (*p >= 128) {
          bytes[0] = 0xc0 | (*p >> 6);
          bytes[1] = 0x80 | (*p & 63);
          n = 2;
        }
        for (unsigned j = 0; j < n; j++) {
          u8 c = bytes[j];
          if (at + 4 >= sizeof(url))
            return fail("Search too long.");
          if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            url[at++] = c;
          else {
            url[at++] = '%';
            url[at++] = hex[c >> 4];
            url[at++] = hex[c & 15];
          }
        }
      }
      url[at] = 0;
    }
  }
  resource_kind = 0;
  document_verified = 0;
  history_record(url);
  web_cancel_pending();
  strcopy(document_url, url, sizeof(document_url));
  serial("NAVIGATE: ");
  serial(url);
  serial("\n");
  return browser_load_url(url, 0);
}

int browser_resource_load(const char *url, int kind, int node) {
  resource_kind = kind;
  resource_node = node;
  return browser_load_url(url, 0);
}
const char *browser_address(void) {
  return *document_url ? document_url : "Search Google or enter a URL";
}
