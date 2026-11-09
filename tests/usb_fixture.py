"""Deterministic FAT16 test media; no host devices or formatting tools needed."""
from pathlib import Path
import struct

def make_image(path, partition=True, large=False):
    offset=2048 if partition else 0
    sectors=65536
    image=bytearray((sectors+offset)*512)
    def put16(p,v):struct.pack_into('<H',image,p,v)
    def put32(p,v):struct.pack_into('<I',image,p,v)
    if offset:
        image[446+4]=6;put32(446+8,offset);put32(446+12,sectors);put16(510,0xaa55)
    base=offset*512
    image[base:base+3]=b'\xeb\x3c\x90';image[base+3:base+11]=b'BETAUSB '
    put16(base+11,512);image[base+13]=2;put16(base+14,1);image[base+16]=2
    put16(base+17,512);put32(base+32,sectors);image[base+21]=0xf8;put16(base+22,128)
    image[base+38]=0x29;image[base+43:base+54]=b'BETA USB   ';image[base+54:base+62]=b'FAT16   ';put16(base+510,0xaa55)
    for table in (offset+1,offset+129):
        for cluster in range(9):put16(table*512+cluster*2,0xfff8 if cluster==0 else 0xffff)
    root=(offset+257)*512;data=offset+289
    def entry(where,name,attr,cluster,size):
        image[where:where+11]=name;image[where+11]=attr;put16(where+26,cluster);put32(where+28,size)
    def content(cluster,data_bytes):
        where=(data+(cluster-2)*2)*512;image[where:where+len(data_bytes)]=data_bytes
    readme=b'REAL USB STORAGE / native xHCI FAT16 reader\n'
    entry(root,b'README  TXT',32,3,len(readme));content(3,readme)
    entry(root+32,b'DOCS       ',16,2,0)
    bex=Path('build/programs/fib.bex').read_bytes();entry(root+64,b'FIB     BEX',32,4,len(bex));content(4,bex)
    sub=(data)*512;entry(sub,b'.          ',16,2,0);entry(sub+32,b'..         ',16,0,0)
    body=b'Nested folder works.\n';entry(sub+64,b'NESTED  TXT',32,5,len(body));content(5,body)
    # Two LFN records, validated against the associated 8.3 alias checksum.
    alias=b'LONGFI~1TXT';checksum=0
    for byte in alias:checksum=(((checksum&1)<<7)+(checksum>>1)+byte)&255
    name='Long filename from USB.txt';units=[ord(c) for c in name]+[0]
    units += [0xffff]*((-len(units))%13)
    for ordinal in (2,1):
        at=root+(3+2-ordinal)*32;image[at]=ordinal|(0x40 if ordinal==2 else 0);image[at+11]=15;image[at+13]=checksum
        for position,value in zip((1,3,5,7,9,14,16,18,20,22,24,28,30),units[(ordinal-1)*13:ordinal*13]):put16(at+position,value)
    entry(root+160,alias,32,6,9);content(6,b'LFN works')
    if large:
        blob=b'BEX1'+struct.pack('<III',65536,0,1)+b'\xc3'+b'\x90'*65535
        entry(root+192,b'BIG     BEX',32,100,len(blob))
        for fat in (offset+1,offset+129):
            for cluster in range(100,165):put16(fat*512+cluster*2,0xffff if cluster==164 else cluster+1)
        content(100,blob)
    Path(path).write_bytes(image)
    return Path(path)

def make_variant(path, bits=32, table='mbr'):
    """Generate additional real USB geometry paths without mounting host media."""
    import binascii
    original=make_image(path,partition=False).read_bytes()
    if bits==16:
        volume=bytearray(original)
    else:
        volume=bytearray(131072*512)
        def p16(at,value):struct.pack_into('<H',volume,at,value)
        def p32(at,value):struct.pack_into('<I',volume,at,value)
        volume[:3]=b'\xeb\x58\x90';volume[3:11]=b'BETAUSB '
        p16(11,512);volume[13]=1;p16(14,32);volume[16]=2;volume[21]=0xf8
        p32(32,131072);p32(36,1024);p32(44,10);p16(48,1);p16(50,6)
        volume[66]=0x29;volume[71:82]=b'BETA USB   ';volume[82:90]=b'FAT32   ';p16(510,0xaa55)
        for fat in (32,1056):
            for cluster in range(11):p32(fat*512+cluster*4,0x0ffffff8 if not cluster else 0x0fffffff)
        for cluster in range(2,7):
            old=(289+(cluster-2)*2)*512;new=(2080+cluster-2)*512
            volume[new:new+512]=original[old:old+512]
        root=(2080+8)*512;volume[root:root+512]=original[257*512:258*512]
    if table=='super':Path(path).write_bytes(volume);return Path(path)
    offset=2048;sectors=len(volume)//512
    image=bytearray((offset+sectors+(33 if table=='gpt' else 0))*512)
    image[offset*512:(offset+sectors)*512]=volume;image[510:512]=b'\x55\xaa'
    struct.pack_into('<B3sB3sII',image,446,0,b'\0'*3,0xee if table=='gpt' else 12 if bits==32 else 6,b'\0'*3,1 if table=='gpt' else offset,len(image)//512-1 if table=='gpt' else sectors)
    if table=='gpt':
        entries=bytearray(128*128);entries[:16]=bytes(range(1,17));entries[16:32]=bytes(range(17,33))
        struct.pack_into('<QQ',entries,32,offset,offset+sectors-1)
        last=len(image)//512-1
        def header(here,other,location):
            h=bytearray(512)
            struct.pack_into('<8sIIIIQQQQ16sQIII',h,0,b'EFI PART',0x10000,92,0,0,here,other,34,last-33,bytes(range(16)),location,128,128,binascii.crc32(entries))
            struct.pack_into('<I',h,16,binascii.crc32(h[:92]));return h
        image[512:1024]=header(1,last,2);image[1024:1024+len(entries)]=entries
        image[(last-32)*512:last*512]=entries;image[last*512:(last+1)*512]=header(last,1,last-32)
    Path(path).write_bytes(image);return Path(path)

if __name__=='__main__':
    import sys
    make_image(sys.argv[1])
