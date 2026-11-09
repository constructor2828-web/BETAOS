#include "web.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
u64 clock_hz=1000000000;
char web_title[96],web_text[16384];
void web_image_reset(void){}
int web_image_load(int n,const void *p,unsigned b){(void)n;(void)p;(void)b;return 0;}
void web_image_size(int n,int *w,int *h){(void)n;*w=*h=0;}
int web_image_draw(int n,int x,int y,int w,int h,int a,int b,int c,int d){(void)n;(void)x;(void)y;(void)w;(void)h;(void)a;(void)b;(void)c;(void)d;return 0;}
void gui_web_clip(int x,int y,int w,int h){(void)x;(void)y;(void)w;(void)h;}
void gui_web_unclip(void){}
void serial(const char *s){(void)s;}
void gui_status(const char *s){(void)s;}
static unsigned redraws;
void gui_web_redraw(void){redraws++;}
void strcopy(char *d,const char *s,size_t cap){if(cap){size_t n=strlen(s);if(n>=cap)n=cap-1;memcpy(d,s,n);d[n]=0;}}
int starts(const char *a,const char *b){return !strncmp(a,b,strlen(b));}
int font_web_advance(int size){return (size*3+2)/5;}
int font_web_measure(const char *s,unsigned n,int z){(void)s;return n*(z*3+2)/5;}
void font_draw_web_mono(int x,int y,const char *s,u32 c,int z){(void)x;(void)y;(void)s;(void)c;(void)z;}
void font_draw_web(int x,int y,const char *s,u32 c,int z){(void)x;(void)y;(void)s;(void)c;(void)z;}
void gui_web_rect(int x,int y,int w,int h,u32 c){(void)x;(void)y;(void)w;(void)h;(void)c;}
void gui_web_round(int x,int y,int w,int h,int r,u32 c){(void)x;(void)y;(void)w;(void)h;(void)r;(void)c;}
void web_js_reset(void){}
void web_js_eval(const char *s,const char *u){(void)s;(void)u;}
int web_js_event(int i,const char *s){(void)i;(void)s;return 0;}
void web_js_loaded(void){}
void web_js_tick(void){}
void web_js_fetch_result(int n,const char *s,int c){(void)n;(void)s;(void)c;}
int browser_busy(void){return 0;}
int browser_load(const char *s){(void)s;return 1;}
int browser_resource_load(const char *s,int k,int n){(void)s;(void)k;(void)n;return 1;}
#include "../desktop/web.c"
static unsigned seed=42;
static unsigned random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 web_document("https://example.test/a/index.html","<style>#card{color:#123456;padding:12px}</style><body><div id='card'>Hello &amp; world</div></body>");
 for(unsigned i=0;i<12;i++)web_poll();assert(redraws==1);
 int card=web_find("card");assert(card>0&&!strcmp(web_get_text(card),"Hello & world"));
 web_paint(10,20,800,400,0);assert(web_nodes[card].style.color==0x123456);
 assert(web_nodes[card].style.padding[0]==12);
 int child=web_create("div");web_append(card,child);web_append(child,card);
 assert(web_nodes[card].parent!=child); /* Cycles rejected. */
 assert(!web_fetch("http://example.test/plain",0));
 assert(!web_fetch("https://example.test.evil/secret",0));
 assert(!web_fetch("https://other.test/",0));
 assert(web_fetch("/data.json",0));
 char url[2048];int kind,node;
 assert(web_next_request(url,sizeof(url),&kind,&node));assert(!strcmp(url,"https://example.test/data.json"));
 web_set_html(card,"<p id='replacement'>Changed</p>");assert(web_find("replacement")>0);
 web_document("https://example.test/", "<html><head><style>:root{--brand:#2367ac}*{box-sizing:border-box}body{margin:0}.wrapper{width:80%;max-width:600px;margin:0 auto;padding:20px;background:var(--brand)}.row{display:flex;gap:16px;align-items:center}.grow{flex:1}#priority{color:red!important}input{display:inline-block}input[type=hidden]{display:block!important}@media(max-width:500px){.wrapper{background:#ee1122}}</style></head><body><main id='wrapper' class='wrapper'><div id='row' class='row'><div id='left' class='grow'>Left</div><div id='right' class='grow'>Right</div></div><p id='priority' style='color:blue'>Priority</p><input id='secret' type='hidden' value='MUST_NOT_RENDER'></main></body></html>");
 web_paint(0,0,800,600,0);
 int wrapper=web_find("wrapper"),left=web_find("left"),right=web_find("right"),secret=web_find("secret");
 assert(web_nodes[wrapper].w==600&&web_nodes[wrapper].x==100);
 assert(web_nodes[wrapper].style.bg==0x2367ac);
 assert(web_nodes[right].x==web_nodes[left].x+web_nodes[left].w+16);
 assert(!web_nodes[secret].style.display);
 assert(web_nodes[web_find("priority")].style.color==0xff0000);
 assert(web_query("main > .row #right")==right);
 assert(web_query("input[type=hidden]")==secret);
 for(unsigned b=0;b<box_count;b++)assert(boxes[b].node!=secret);
 unsigned arena_before=strings_used;
 for(unsigned r=0;r<100;r++)web_paint(0,0,r&1?800:400,600,r%20);
 assert(strings_used==arena_before);
 web_paint(0,0,400,600,0);assert(web_nodes[wrapper].style.bg==0xee1122&&web_nodes[wrapper].w==320&&web_nodes[wrapper].x==40);
 web_document("https://example.test/", "<form action='/search'><input name='token' type='hidden' value='secret'><input name='q' id='query' maxlength='4'><input name='btnG' type='submit' value='Search'><input name='btnI' type='submit' value='Lucky'></form>");
 web_paint(0,0,800,600,0);web_key('\t');web_key('a');web_key('b');web_key('c');web_key('d');assert(web_key('e'));assert(!strcmp(web_get_attr(web_find("query"),"value"),"abcd"));
 web_key(20);web_key(20);web_key(7);web_key('x');assert(!strcmp(web_get_attr(web_find("query"),"value"),"abxd"));
 web_key(137);web_key('c');web_key('a');web_key('t');web_key('\n');assert(!strcmp(navigation,"https://example.test/search?token=secret&q=cat"));
 char resolved[2048];
 resolve_from("https://example.test/a/b/index.html?old=1","../theme.css",resolved,sizeof(resolved));assert(!strcmp(resolved,"https://example.test/a/theme.css"));
 resolve_from("https://example.test/a/b/index.html?old=1","?new=2",resolved,sizeof(resolved));assert(!strcmp(resolved,"https://example.test/a/b/index.html?new=2"));
 resolve_from("https://example.test/a/b/index.html?old=1","#part",resolved,sizeof(resolved));assert(!strcmp(resolved,"https://example.test/a/b/index.html?old=1#part"));
 resolve_from("https://example.test/a/b/","//cdn.test/image.png",resolved,sizeof(resolved));assert(!strcmp(resolved,"https://cdn.test/image.png"));
 web_document("https://example.test/", "<link rel='stylesheet' href='/early.css'><style>#cascade{color:blue}</style><p id='cascade'>Cascade</p><noscript id='fallback'>Enable JavaScript</noscript>");
 assert(web_next_request(url,sizeof(url),&kind,&node)&&kind==1);
 web_resource(kind,node,"#cascade{color:red}",19,200);
 web_paint(0,0,800,600,0);
 assert(web_nodes[web_find("cascade")].style.color==0x0000ff);
 assert(web_nodes[web_find("fallback")].style.display==0);
 int dynamic = web_create("style");
 web_set_text(dynamic,"#cascade{color:green}");web_append(0,dynamic);
 web_paint(0,0,800,600,0);assert(web_nodes[web_find("cascade")].style.color==0x008000);
 web_set_text(dynamic,"#cascade{color:red}");
 web_paint(0,0,800,600,0);assert(web_nodes[web_find("cascade")].style.color==0xff0000);
 char buffer[4096];
 const char alphabet[]="<>/='\" abcdef0123456789&;:#{}()[]\n";
 for(unsigned iteration=0;iteration<3000;iteration++){
   unsigned n=random32()%(sizeof(buffer)-1);
   for(unsigned i=0;i<n;i++)buffer[i]=alphabet[random32()%(sizeof(alphabet)-1)];buffer[n]=0;
   web_document("https://example.test/",buffer);
   web_paint(0,0,320+(iteration%1000),400,iteration%100);
   web_click(iteration%800,iteration%600);web_key('\t');web_key('a');
   assert(web_node_count<=WEB_NODES&&strings_used<=sizeof(strings));
 }
 puts("PASS: DOM/CSS semantics, cycle prevention, origin/mixed-content guards and 3000 bounded HTML mutations");
}
