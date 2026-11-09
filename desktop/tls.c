#include "bearssl.h"
#include "os.h"
#include "tls_roots.h"
static br_ssl_client_context client;
static br_x509_minimal_context chain;
static br_hmac_drbg_context entropy;
static int entropy_initialized, active, local_error;
static unsigned char buffer[BR_SSL_BUFSIZE_BIDI];
int tls_start(const char *host) {
  u32 days, seconds;
  active = 0;
  local_error = 0;
  if (!platform_utc(&days, &seconds)) {
    local_error = -1;
    return 0;
  }
  if (!boot_entropy_ready) {
    local_error = -2;
    return 0;
  }
  if (!entropy_initialized) {
    br_hmac_drbg_init(&entropy, &br_sha256_vtable, boot_entropy,
                      sizeof(boot_entropy));
    memset(boot_entropy, 0, sizeof(boot_entropy));
    entropy_initialized = 1;
  }
  br_ssl_client_init_full(&client, &chain, tls_roots,
                          sizeof(tls_roots) / sizeof(tls_roots[0]));
  br_ssl_engine_set_versions(&client.eng, BR_TLS12, BR_TLS12);
  br_x509_minimal_set_time(&chain, days, seconds);
  br_ssl_engine_set_buffer(&client.eng, buffer, sizeof(buffer), 1);
  u8 seed[64];
  br_hmac_drbg_generate(&entropy, seed, sizeof(seed));
  br_ssl_engine_inject_entropy(&client.eng, seed, sizeof(seed));
  memset(seed, 0, sizeof(seed));
  if (!br_ssl_client_reset(&client, host, 0)) {
    local_error = br_ssl_engine_last_error(&client.eng);
    return 0;
  }
  active = 1;
  return 1;
}
int tls_error(void) {
  return local_error ? local_error
         : active    ? br_ssl_engine_last_error(&client.eng)
                     : 0;
}
int tls_ready(void) {
  return active && (br_ssl_engine_current_state(&client.eng) & BR_SSL_SENDAPP);
}
size_t tls_feed(const void *data, size_t count) {
  if (!active)
    return 0;
  size_t cap = 0;
  unsigned char *p = br_ssl_engine_recvrec_buf(&client.eng, &cap);
  if (!p || !cap)
    return 0;
  if (count > cap)
    count = cap;
  memcpy(p, data, count);
  br_ssl_engine_recvrec_ack(&client.eng, count);
  return count;
}
size_t tls_plain(void *data, size_t cap) {
  if (!active)
    return 0;
  size_t n = 0;
  unsigned char *p = br_ssl_engine_recvapp_buf(&client.eng, &n);
  if (!p || !n)
    return 0;
  if (n > cap)
    n = cap;
  memcpy(data, p, n);
  br_ssl_engine_recvapp_ack(&client.eng, n);
  return n;
}
size_t tls_write(const void *data, size_t n) {
  if (!active)
    return 0;
  size_t cap = 0;
  unsigned char *p = br_ssl_engine_sendapp_buf(&client.eng, &cap);
  if (!p || !cap)
    return 0;
  if (n > cap)
    n = cap;
  memcpy(p, data, n);
  br_ssl_engine_sendapp_ack(&client.eng, n);
  br_ssl_engine_flush(&client.eng, 0);
  return n;
}
const void *tls_output(size_t *n) {
  *n = 0;
  return active ? br_ssl_engine_sendrec_buf(&client.eng, n) : 0;
}
void tls_output_ack(size_t n) {
  if (active)
    br_ssl_engine_sendrec_ack(&client.eng, n);
}
void tls_stop(void) {
  active = 0;
  memset(&client, 0, sizeof(client));
  memset(&chain, 0, sizeof(chain));
  memset(buffer, 0, sizeof(buffer));
}
