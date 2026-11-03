import sys,re
BASE=0x204000
def calls(dis):
    L=open(dis).read().splitlines()
    i=next(k for k,l in enumerate(L) if 'csrrw\tzero,pmpaddr0' in l)
    reg={}; out=[]; tgt=None
    for l in L[i:i+400]:
        f=l.split('\t')
        if len(f)<3: continue
        op=f[2].strip(); args=f[3].split('#')[0].strip() if len(f)>3 else ''
        m=re.match(r'(a[0-2]),(?:zero,)?(-?\d+)$',args)
        if op in('c.li','addi') and m: reg[m.group(1)]=int(m.group(2)); continue
        if op=='jal' and args.startswith('ra,'):
            t=int(args.split(',')[1],16)
            if tgt is None: tgt=t
            if t==tgt: out.append((reg.get('a0'),reg.get('a1'),reg.get('a2')))
            reg={}
        if op in('c.jr','jalr') and 'ra' in args and op=='c.jr': break
    return tgt,out
def find_table(fw,ref):
    return fw.find(ref)
def lookup(fw,toff,port,pin,func):
    idx={0:pin,1:pin+5,2:pin+10,3:pin+16}[port]
    e=toff+idx*0x78
    for k in range(14):
        fid=int.from_bytes(fw[e+8*k:e+8*k+4],'little'); v=fw[e+8*k+4]
        if v==0xff: break
        if fid==func: return v
    return None
ref=open(sys.argv[1]+'/vrxpro.fw','rb').read()
REFTAB=ref[0x270320-BASE:0x270320-BASE+0x78*29]
for n in sys.argv[2:]:
    fw=open(f'{sys.argv[1]}/{n}.fw','rb').read()
    t,cs=calls(f'{sys.argv[1]}/{n}.dis')
    toff=fw.find(REFTAB)
    print(f'== {n}: pinmux_set@0x{t:x} table@{"0x%x"%(toff+BASE) if toff>=0 else "NOT FOUND"} calls={len(cs)}')
    rec={}
    for a0,a1,a2 in cs:
        v=lookup(fw,toff,a0,a1,a2) if toff>=0 else None
        if a2 is not None and a2<9: v=f'gpio{a2}'
        print(f'   port{a0} pin{a1:2d} func=0x{a2:02x} -> mux {v}')
        if a0 in(0,1): rec[(a0,a1)]=v
    print('   RECORDS:', ', '.join(f'{b},{p},{rec.get((b,p))},0' for b in(0,1) for p in range(5)))
