#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "os.h"
u64 clock_hz=1000000000;u8 *dma;
void gui_status(const char *s){(void)s;}
void serial(const char *s){(void)s;}
void serial_num(u64 n){(void)n;}
void strcopy(char *d,const char *s,size_t cap){size_t i=0;while(i+1<cap&&s[i]){d[i]=s[i];i++;}if(cap)d[i]=0;}
int starts(const char *a,const char *b){return strncmp(a,b,strlen(b))==0;}
int tls_start(const char *s){(void)s;return 0;}int tls_ready(void){return 0;}int tls_error(void){return 0;}
size_t tls_feed(const void *p,size_t n){(void)p;return n;}
size_t tls_plain(void *p,size_t n){(void)p;(void)n;return 0;}
size_t tls_write(const void *p,size_t n){(void)p;return n;}
const void *tls_output(size_t *n){*n=0;return 0;}void tls_output_ack(size_t n){(void)n;}void tls_stop(void){}
void web_document(const char *u,const char *h){(void)u;(void)h;}
void web_resource(int k,int n,const char *b,unsigned s,int c){(void)k;(void)n;(void)b;(void)s;(void)c;}
void web_cancel_pending(void){}
void web_home(void){}
#include "../desktop/net.c"
static u32 rng=0xcafe1234;
static u32 random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static struct HttpMeta meta(const char *text){strcopy(response,text,sizeof(response));response_len=strlen(response);return http_meta();}
static u16 independent_checksum(const u8 *p,unsigned n){u32 sum=0;for(unsigned i=0;i<n;i+=2)sum+=(u16)p[i]*256+(i+1<n?p[i+1]:0);while(sum>65535)sum=(sum&65535)+(sum>>16);return (u16)~sum;}
int main(void){
 struct HttpMeta h=meta("HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello");assert(h.has_length&&h.length==5&&!h.invalid&&h.end);
 h=meta("HTTP/1.0 200 OK\r\nX-Content-Length: 5\r\n\r\nhello");assert(!h.has_length);
 h=meta("HTTP/1.0 200 OK\r\nContent-Length: 5\r\ncontent-length: 6\r\n\r\nhello");assert(h.invalid);
 h=meta("HTTP/1.0 200 OK\r\nContent-Length: -1\r\n\r\n");assert(h.invalid);
 h=meta("HTTP/1.0 200 OK\r\nContent-Length: 999999999\r\n\r\n");assert(h.invalid);
 h=meta("HTTP/1.0 200 OK\r\nTRANSFER-ENCODING: Chunked\r\n\r\n");assert(h.chunked&&!h.unsupported);
 h=meta("HTTP/1.0 200 OK\r\nContent-Encoding: identity\r\n\r\n");assert(!h.unsupported);
 const u8 answer[]={0x42,0x42,0x81,0x80,0,1,0,1,0,0,0,0,4,'b','e','t','a',7,'e','x','a','m','p','l','e',0,0,1,0,1,0xc0,0x0c,0,1,0,1,0,0,0,60,0,4,10,0,2,100};
 dns_wait=1;dns_id=0x4242;dns_result=0;dns_receive(answer,sizeof(answer));assert(!dns_wait&&dns_result==0x0a000264);
 u8 frame[1518],packet[1024];
 local_ip=0x0a00020f;dns_ip=0x0a000203;server_ip=0x0a000264;server_port=80;client_port=49152;
 for(unsigned iteration=0;iteration<20000;iteration++){
   unsigned n=random32()%sizeof(packet);for(unsigned i=0;i<n;i++)packet[i]=random32();
   if(n>=12){packet[0]=0x42;packet[1]=0x42;packet[2]=0x81;packet[3]=0x80;packet[4]=0;packet[5]=random32()%10;packet[6]=0;packet[7]=random32()%70;}
   dns_wait=1;dns_receive(packet,n);
   n=random32()%32767;for(unsigned i=0;i<n;i++)response[i]=(char)random32();response[n]=0;response_len=n;http_meta();
   unsigned payload=random32()%1400;unsigned protocol=iteration&1?6:17;unsigned transport=protocol==6?20:8,total=14+20+transport+payload;
   memset(frame,0,total);frame[12]=8;frame[14]=0x45;frame[16]=(total-14)>>8;frame[17]=total-14;frame[22]=64;frame[23]=protocol;
   const u8 source_tcp[]={10,0,2,100},source_dns[]={10,0,2,3},destination[]={10,0,2,15};memcpy(frame+26,protocol==6?source_tcp:source_dns,4);memcpy(frame+30,destination,4);
   u16 sum=independent_checksum(frame+14,20);frame[24]=sum>>8;frame[25]=sum;
   for(unsigned i=0;i<payload;i++)frame[34+transport+i]=random32();
   if(protocol==17){frame[34]=0;frame[35]=53;frame[36]=53000>>8;frame[37]=53000&255;unsigned length=payload+8;frame[38]=length>>8;frame[39]=length;}
   else{frame[34]=0;frame[35]=80;frame[36]=client_port>>8;frame[37]=client_port;frame[46]=0x50;frame[47]=0x18;u8 pseudo[1510];memcpy(pseudo,frame+26,8);pseudo[8]=0;pseudo[9]=6;pseudo[10]=(payload+20)>>8;pseudo[11]=payload+20;memcpy(pseudo+12,frame+34,payload+20);sum=independent_checksum(pseudo,payload+32);frame[50]=sum>>8;frame[51]=sum;tcp_state=2;ack=0;response_len=random32()%32768;response_full=0;}
   receive(frame,total);
 }
 tcp_state=0;browser_state=B_IDLE;dhcp_state=0;
 browser_load("google");assert(!strcmp(browser_address(),"https://www.google.com/"));
 browser_load("native asm search");assert(!strcmp(browser_address(),"https://www.google.com/search?gbv=1&udm=14&hl=en&q=native%20asm%20search"));
 browser_load("caf\xe9");assert(strstr(browser_address(),"caf%C3%A9"));
 browser_load("example.org/docs");assert(!strcmp(browser_address(),"https://example.org/docs"));
 const char *wire="4;ext=yes\r\nWiki\r\n5\r\npedia\r\n0\r\nTrailer: yes\r\n\r\n";
 memset(response,0,sizeof(response));response[0]='H';response_len=1;
 chunk_at=chunk_write=chunk_left=chunk_state=chunk_digits=0;
 for(unsigned i=0;wire[i];i++){response[response_len++]=wire[i];int result=chunks(1);assert(result>=0);}
 assert(chunk_state==7&&chunk_write==9&&!memcmp(response+1,"Wikipedia",9));
 puts("PASS: HTTP metadata edge cases, compressed DNS A reply and 20000 bounded DNS/HTTP/IPv4/TCP mutations");
}
