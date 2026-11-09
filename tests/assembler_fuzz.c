#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "os.h"
void strcopy(char *d,const char *s,size_t cap){size_t i=0;while(i+1<cap&&s[i]){d[i]=s[i];i++;}if(cap)d[i]=0;}
int starts(const char *a,const char *b){return strncmp(a,b,strlen(b))==0;}
static u32 rng=0x5eed1234;
static u32 next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(void){
 char source[2048],message[128];u8 output[65552];
 const char *tokens[]={"mov rax,1\n","jmp missing\n","label: db \"text\",10,0\n","lea rdi,[rel label]\n","push r12\n","pop r12\n","call [rbx+8]\n","ret\n","xor r8,r8\n","; comment\n","db -129\n","start:\n"};
 for(unsigned pass=0;pass<20000;pass++){
   unsigned n=0,lines=next()%18;
   for(unsigned i=0;i<lines;i++){const char *token=tokens[next()%(sizeof(tokens)/sizeof(tokens[0]))];size_t bytes=strlen(token);if(n+bytes>=sizeof(source))break;memcpy(source+n,token,bytes);n+=bytes;}
   if(pass&1)for(unsigned i=0;i<(next()%12)&&n;i++)source[next()%n]=(char)(next()%128);
   source[n]=0;u32 capacity=next()%256,error_capacity=next()%sizeof(message);
   int result=assemble(source,output,capacity,message,error_capacity);
   if(result){assert(result>=17&&(u32)result<=capacity);assert(read_le32(output+4)==(u32)result-16);assert(read_le32(output+8)<(u32)result-16);}
 }
 assert(!assemble("ret\nstart:\n",output,sizeof(output),message,sizeof(message)));
 assert(!assemble(0,output,sizeof(output),message,sizeof(message)));
 assert(!assemble("ret",output,sizeof(output),0,128));
 puts("PASS: 20000 deterministic assembler mutations, output/message bounds and entry checks");
}
