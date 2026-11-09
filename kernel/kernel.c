#include <stdint.h>
#include <stddef.h>

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static size_t length(const char *s) { size_t n=0; while(s[n]) n++; return n; }
static int equal(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static void copy(char *d,const char *s) { while((*d++=*s++)) {} }
static volatile uint16_t *const screen=(uint16_t *)0xb8000;
static unsigned row, col;
static uint8_t color=0x0b;
static volatile uint32_t ticks;
static volatile unsigned key_read,key_write;
static char keys[128];
static int shift,extended;

static void cursor(void) {
    unsigned pos=row*80+col;
    outb(0x3d4,14); outb(0x3d5,pos>>8);
    outb(0x3d4,15); outb(0x3d5,pos);
}
static void clear(void) {
    for(unsigned i=0;i<2000;i++) screen[i]=((uint16_t)color<<8)|' ';
    row=col=0; cursor();
}
static void put(char c) {
    if(c=='\n') { while(!(inb(0x3fd)&32)) {} outb(0x3f8,'\r'); }
    while(!(inb(0x3fd)&32)) {}
    outb(0x3f8,c);
    if(c=='\n') { col=0; row++; }
    else if(c=='\b') { if(col) { col--; screen[row*80+col]=((uint16_t)color<<8)|' '; } }
    else if(c>=' ') { screen[row*80+col]=((uint16_t)color<<8)|(uint8_t)c; if(++col==80) { col=0; row++; } }
    if(row>=25) {
        for(unsigned i=0;i<1920;i++) screen[i]=screen[i+80];
        for(unsigned i=1920;i<2000;i++) screen[i]=((uint16_t)color<<8)|' ';
        row=24;
    }
    cursor();
}
static void print(const char *s) { while(*s) put(*s++); }
static void number(uint32_t v) { char b[11]; unsigned n=0; do { b[n++]='0'+v%10; v/=10; } while(v); while(n) put(b[--n]); }

struct gate { uint16_t low,selector; uint8_t zero,flags; uint16_t high; } __attribute__((packed));
static struct gate idt[256];
extern void irq_timer(void),irq_keyboard(void),exception_stub(void);
static void gate(unsigned n,void (*fn)(void)) {
    uint32_t a=(uint32_t)fn;
    idt[n]=(struct gate){a,8,0,0x8e,a>>16};
}
void timer_handler(void) { ticks++; outb(0x20,0x20); }
static const char normal[128]={
    [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
    [12]='-',[13]='=',[14]='\b',[15]='\t',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',
    [21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\n',
    [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
    [39]=';',[40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',
    [48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[57]=' '
};
static const char shifted[128]={ [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
    [12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',[51]='<',[52]='>',[53]='?' };
void keyboard_handler(void) {
    uint8_t sc=inb(0x60);
    if(sc==0xe0 || sc==0xe1) { extended=1; goto done; }
    if(extended) { extended=0; goto done; }
    if(sc==42 || sc==54) { shift=1; goto done; }
    if(sc==170 || sc==182) { shift=0; goto done; }
    if(sc&128) goto done;
    char c=normal[sc];
    if(shift) { if(c>='a' && c<='z') c-=32; else if(shifted[sc]) c=shifted[sc]; }
    unsigned next=(key_write+1)%128;
    if(c && next!=key_read) { keys[key_write]=c; key_write=next; }
done:
    outb(0x20,0x20);
}
void panic(void) { print("\nKERNEL PANIC: processor exception. Reboot required.\n"); for(;;) __asm__ volatile("cli; hlt"); }
static void hardware_init(void) {
    outb(0x3f9,0); outb(0x3fb,0x80); outb(0x3f8,1); outb(0x3f9,0);
    outb(0x3fb,3); outb(0x3fa,0xc7); outb(0x3fc,0x0b);
    for(unsigned i=0;i<256;i++) gate(i,exception_stub);
    gate(32,irq_timer); gate(33,irq_keyboard);
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) descriptor={sizeof(idt)-1,(uint32_t)idt};
    __asm__ volatile("lidt %0"::"m"(descriptor));
    outb(0x20,0x11); outb(0xa0,0x11);
    outb(0x21,32); outb(0xa1,40); outb(0x21,4); outb(0xa1,2);
    outb(0x21,1); outb(0xa1,1); outb(0x21,0xfc); outb(0xa1,0xff);
    outb(0x43,0x36); outb(0x40,11932&255); outb(0x40,11932>>8);
    __asm__ volatile("sti");
}

#define FILE_COUNT 16
#define FILE_SIZE 512
struct file { char name[32]; char data[FILE_SIZE]; };
static struct file files[FILE_COUNT];
static struct file *find(const char *name) { for(unsigned i=0;i<FILE_COUNT;i++) if(equal(files[i].name,name)) return &files[i]; return NULL; }
static char *word(char *s) { while(*s && *s!=' ') s++; if(*s) *s++=0; while(*s==' ') s++; return s; }
static void command(char *line) {
    while(*line==' ') line++;
    char *args=word(line);
    if(!*line) return;
    if(equal(line,"help")) print("help about clear echo uptime mem ls write cat rm color reboot halt\nwrite NAME TEXT  - store a file (RAM only, lost on reboot)\ncat NAME / rm NAME / color HEX (0-f)\n");
    else if(equal(line,"about")) print("BETAOS 0.1 | x86 protected mode | built from source\nBIOS loader, C kernel, IRQ timer, PS/2 keyboard, VGA + COM1.\n");
    else if(equal(line,"clear")) clear();
    else if(equal(line,"echo")) { print(args); put('\n'); }
    else if(equal(line,"uptime")) { number(ticks/100); print(" seconds\n"); }
    else if(equal(line,"mem")) print("RAM filesystem: 16 slots, 512 bytes per file.\nKernel at 0x10000; stack at 0x90000. No heap or paging.\n");
    else if(equal(line,"ls")) { unsigned n=0; for(unsigned i=0;i<FILE_COUNT;i++) if(files[i].name[0]) { print(files[i].name); print("  "); number(length(files[i].data)); print(" bytes\n"); n++; } if(!n) print("No files.\n"); }
    else if(equal(line,"write")) {
        char *data=word(args);
        if(!*args || length(args)>31 || length(data)>=FILE_SIZE) { print("Usage: write NAME TEXT (name <=31, text <=511 bytes)\n"); return; }
        struct file *f=find(args);
        if(!f) for(unsigned i=0;i<FILE_COUNT;i++) if(!files[i].name[0]) { f=&files[i]; break; }
        if(!f) { print("Error: filesystem full.\n"); return; }
        copy(f->name,args); copy(f->data,data); print("Saved "); print(args); put('\n');
    }
    else if(equal(line,"cat") || equal(line,"rm")) {
        struct file *f=*args?find(args):NULL;
        if(!f) print("Error: file not found.\n");
        else if(equal(line,"rm")) { f->name[0]=0; print("Removed.\n"); }
        else { print(f->data); put('\n'); }
    }
    else if(equal(line,"color")) {
        char c=*args;
        if(args[0] && !args[1] && ((c>='0'&&c<='9') || (c>='a'&&c<='f'))) color=c<='9'?c-'0':c-'a'+10;
        else print("Usage: color HEX (0-f)\n");
    }
    else if(equal(line,"reboot")) { print("Rebooting...\n"); while(inb(0x64)&2) {} outb(0x64,0xfe); }
    else if(equal(line,"halt")) { print("System halted.\n"); for(;;) __asm__ volatile("cli; hlt"); }
    else { print("Unknown command: "); print(line); print(". Type help.\n"); }
}
void kernel_main(void) {
    hardware_init(); clear();
    print("\n  B E T A O S  /  0.1\n  Your machine. Your kernel.\n\nBoot OK: protected mode, interrupts, timer, keyboard.\nType help for commands. Files live in RAM.\n\nbeta> ");
    char line[640]; unsigned used=0; int cr=0,overflow=0;
    for(;;) {
        char c=0;
        if(key_read!=key_write) { c=keys[key_read]; key_read=(key_read+1)%128; }
        else if(inb(0x3fd)&1) c=inb(0x3f8);
        if(!c) { __asm__ volatile("hlt"); continue; }
        if(c=='\n' && cr) { cr=0; continue; }
        cr=c=='\r';
        if(c=='\r' || c=='\n') {
            put('\n'); line[used]=0;
            if(overflow) print("Error: command too long.\n"); else command(line);
            used=0; overflow=0; print("beta> ");
        } else if(c=='\b' || c==127) { if(used && !overflow) { used--; put('\b'); print(" \b"); } }
        else if(c>=32 && c<=126) { if(used<sizeof(line)-1 && !overflow) { line[used++]=c; put(c); } else overflow=1; }
    }
}
