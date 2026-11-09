#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "os.h"
void strcopy(char *d,const char *s,size_t cap){size_t i=0;while(i+1<cap&&s[i]){d[i]=s[i];i++;}if(cap)d[i]=0;}
int starts(const char *a,const char *b){return strncmp(a,b,strlen(b))==0;}
int main(void){
 u8 bytes[65552];char error[128];
 const char *program="bits 64\nstart:\npush rbx\nmov rbx,rdi\nlea rdi,[rel message]\ncall [rbx+8]\npop rbx\nret\nmessage: db \"Hello; not a comment\",10,0\n";
 int n=assemble(program,bytes,sizeof(bytes),error,sizeof(error));assert(n>16);assert(!memcmp(bytes,"BEX1",4));assert(read_le32(bytes+4)==(u32)n-16);assert(bytes[16]==0x53);assert(bytes[17]==0x48&&bytes[18]==0x89&&bytes[19]==0xfb);
 // LEA's relative displacement must point to the literal after the return.
 u32 site=23;int32_t displacement=(int32_t)read_le32(bytes+site);assert(!strcmp((char *)bytes+site+4+displacement,"Hello; not a comment\n"));
 n=assemble("start: mov r12,3\nloop: dec r12\njnz loop\nret\n",bytes,sizeof(bytes),error,sizeof(error));assert(n>0);assert(bytes[29]==0x0f&&bytes[30]==0x85);assert((int32_t)read_le32(bytes+31)==-9);
 assert(assemble("mov rax,-1\nret\n",bytes,sizeof(bytes),error,sizeof(error))>0);for(unsigned i=18;i<26;i++)assert(bytes[i]==255);
 assert(!assemble("jmp missing\n",bytes,sizeof(bytes),error,sizeof(error)));assert(strstr(error,"Undefined label"));
 assert(!assemble("mov potato,1\n",bytes,sizeof(bytes),error,sizeof(error)));assert(strstr(error,"destination"));
 assert(!assemble("db 999\n",bytes,sizeof(bytes),error,sizeof(error)));
 assert(!assemble("push rbx\nret\n",bytes,17,error,sizeof(error)));
 assert(!assemble("a: nop\na: ret\n",bytes,sizeof(bytes),error,sizeof(error)));
 puts("PASS: native assembler instructions, relocations, literals, branches, diagnostics and bounds");
}
