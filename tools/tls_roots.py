"""Compile a pinned PEM CA bundle to BearSSL trust anchors, without host libraries."""
import base64,re,sys
from pathlib import Path

def tlv(data,at=0):
    start=at;tag=data[at];at+=1;length=data[at];at+=1
    if length&128:
        n=length&127
        if not 1<=n<=4:raise ValueError('DER length')
        length=int.from_bytes(data[at:at+n],'big');at+=n
    end=at+length
    if end>len(data):raise ValueError('DER bounds')
    return tag,data[at:end],end,data[start:end]
def children(data):
    at=0;out=[]
    while at<len(data):
        t,v,at,raw=tlv(data,at);out.append((t,v,raw))
    return out

def generate(pem):
    arrays=[];anchors=[]
    def blob(name,data):
        arrays.append('static const unsigned char '+name+'[]={'+','.join(str(x) for x in data)+'};')
        return '(unsigned char *)'+name
    for encoded in re.findall(r'-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----',pem,re.S):
        der=base64.b64decode(encoded);cert=children(tlv(der)[1]);fields=children(cert[0][1]);offset=1 if fields[0][0]==0xa0 else 0
        dn=fields[4+offset][2];spki=children(fields[5+offset][1]);algorithm=children(spki[0][1]);key=spki[1][1]
        if not key or key[0]:raise ValueError('DER bit string')
        number=len(anchors);prefix=f'root_{number}_';dn_name=blob(prefix+'dn',dn)
        oid=algorithm[0][1]
        if oid==bytes.fromhex('2a864886f70d010101'):
            rsa=children(tlv(key[1:])[1]);n=rsa[0][1].lstrip(b'\0');e=rsa[1][1].lstrip(b'\0')
            n_name=blob(prefix+'n',n);e_name=blob(prefix+'e',e)
            pkey='{BR_KEYTYPE_RSA,{.rsa={'+f'{n_name},{len(n)},{e_name},{len(e)}'+'}}}'
        elif oid==bytes.fromhex('2a8648ce3d0201'):
            curve={bytes.fromhex('2a8648ce3d030107'):23,bytes.fromhex('2b81040022'):24,bytes.fromhex('2b81040023'):25}.get(algorithm[1][1])
            if not curve:continue
            q=key[1:];q_name=blob(prefix+'q',q)
            pkey='{BR_KEYTYPE_EC,{.ec={'+f'{curve},{q_name},{len(q)}'+'}}}'
        else:continue
        anchors.append('{{'+f'{dn_name},{len(dn)}'+'},BR_X509_TA_CA,'+pkey+'}')
    if not anchors:raise ValueError('No supported CA certificates')
    return '/* Generated trust anchors; source PEM is versioned. */\n'+'\n'.join(arrays)+'\nstatic const br_x509_trust_anchor tls_roots[]={\n'+',\n'.join(anchors)+'\n};\n'
if __name__=='__main__':Path(sys.argv[2]).write_text(generate(Path(sys.argv[1]).read_text()))
