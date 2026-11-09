#include "web.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned painted;
void serial(const char *s){(void)s;}
void gui_web_pixel(int x,int y,u32 c,unsigned a){assert(x>=10&&x<30&&y>=10&&y<30);assert(c<=0xffffff&&a<=255);painted++;}
int main(int argc,char **argv){
 assert(argc==4);unsigned char data[65536],mutation[65536];
 for(int file=1;file<argc;file++){
  FILE *fp=fopen(argv[file],"rb");assert(fp);unsigned n=fread(data,1,sizeof(data),fp);fclose(fp);assert(n&&n<sizeof(data));
  web_image_reset();assert(web_image_load(7,data,n));int w,h;web_image_size(7,&w,&h);assert(w==32&&h==24);
  painted=0;assert(web_image_draw(7,0,0,64,48,10,10,20,20));assert(painted==400);
  for(unsigned round=0;round<800;round++){
   for(unsigned k=0;k<n;k++)mutation[k]=data[k];
   unsigned pos=(round*127)%n;mutation[pos]^=(round*17)|1;
   unsigned length=round%4? n:round%n;
   web_image_reset();web_image_load(0,mutation,length);
  }
 }
 web_image_reset();puts("PASS: PNG/JPEG/GIF decoding, clipped painting and 2400 malformed image mutations under sanitizers");
}
