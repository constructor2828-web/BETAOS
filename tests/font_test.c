#include "os.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
u32 *gui_canvas,*gui_previous;
u32 fb_width=320,fb_height=160,fb_stride=327,fb_format;
void serial(const char *s){(void)s;}
int main(void){
 size_t count=(size_t)fb_stride*fb_height;
 u32 *allocation=malloc((count+32)*4);assert(allocation);
 gui_canvas=allocation+16;font_init();
 for(fb_format=0;fb_format<2;fb_format++){
   for(size_t i=0;i<count+32;i++)allocation[i]=0x31415926;
   for(size_t i=0;i<count;i++)gui_canvas[i]=0;
   assert(font_draw_ui(4,4,"BETAOS smooth text",0xffffff,2));
   font_draw_web(5,50,"HTML + CSS + JavaScript",0xffffff,18);
   unsigned intermediate=0;
   for(size_t i=0;i<count;i++)if(gui_canvas[i]&&gui_canvas[i]!=0xffffff)intermediate++;
   assert(intermediate>100);
   assert(font_label_width("WWW",14)>font_label_width("iii",14));
   font_draw_label(4,80,"Interface labels",0xffffff,14);
   font_clip(20,20,40,30);font_draw_label(-5,5,"Clipped interface",0xffffff,32);font_unclip();
   for(int pos=-50;pos<360;pos+=9){
     font_draw_ui(pos,pos/2,"Edges",0xaabbcc,3);
     font_draw_web(pos,pos/2,"\xc3\xb1\xe2\x82\xac\xe2",0xabcdef,32);
   }
   for(unsigned i=0;i<16;i++)assert(allocation[i]==0x31415926&&allocation[count+16+i]==0x31415926);
 }
 free(allocation);puts("PASS: real embedded TrueType antialiasing and clipped RGB/BGR glyphs under sanitizers");
}
