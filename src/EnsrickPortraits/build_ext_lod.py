import os,sys,struct,zlib
from PIL import Image
src,out=sys.argv[1],sys.argv[2]
SIZES={'HPL':(58,64),'HPS':(48,32)}
RESERVED=bytes.fromhex('00ffff ff96ff ff64ff ff32ff ff00ff ffff00 b400ff 00ff00 ff80ff ff80ff'.replace(' ',''))
NRES=len(RESERVED)//3
best={}
for root,_,files in os.walk(src):
    tag=os.path.basename(root)
    if not tag.startswith('Data') or 'alternate_variants' in root: continue
    scale=int(tag[4]) if tag[4:5].isdigit() else 1
    for fn in files:
        stem,ext=os.path.splitext(fn)
        if ext.lower()!='.png' or stem[:3].upper() not in SIZES: continue
        k=stem.upper()
        if k not in best or scale>best[k][0]: best[k]=(scale,os.path.join(root,fn))
ents=[]
for k,(scale,p) in sorted(best.items()):
    w,h=SIZES[k[:3]]
    im=Image.open(p).convert('RGB')
    if im.size!=(w*scale,h*scale): print('odd size',k,im.size,scale)
    # Indices 0-9 are the engine's transparency/shadow keys; stock portraits never use 1-9.
    im=im.resize((w,h),Image.LANCZOS).quantize(256-NRES,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    pal=bytes(RESERVED)+bytes((im.getpalette()+[0]*768)[:3*(256-NRES)])
    px=bytes(i+NRES for i in im.tobytes())
    raw=struct.pack('<III',w*h,w,h)+px+pal
    ents.append((k+'.pcx',raw))
ents.sort(key=lambda e:e[0].upper())
hdr=b'LOD\0'+struct.pack('<II',200,len(ents))+b'\0'*80
off=len(hdr)+32*len(ents); table=b''; data=b''
for name,raw in ents:
    c=zlib.compress(raw,9)
    table+=struct.pack('<16sIIII',name.encode(),off+len(data),len(raw),16,len(c)); data+=c
open(out,'wb').write(hdr+table+data)
print('wrote',len(ents),'entries',os.path.getsize(out),'bytes')
