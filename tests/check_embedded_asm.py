"""Source-level RV32I subset interpreter; not an ELF assembler or Ripes substitute."""
import ast,re,sys,time
from pathlib import Path
MASK=0xffffffff

def signed(x): return x if x<0x80000000 else x-0x100000000

def expand(source,render,test_case=0):
    constants={'RENDER':render,'TEST_CASE':test_case,'LED_MATRIX_0_BASE':0x180000,'LED_MATRIX_0_WIDTH':35,'LED_MATRIX_0_HEIGHT':25}
    active=[True];out=[]
    def value(expr):return eval(expr,{'__builtins__':{}},constants)
    for raw in source.splitlines():
        line=raw.split('#',1)[0].strip()
        if line.startswith('.ifndef '):
            active.append(active[-1] and line.split()[1] not in constants);continue
        if line.startswith('.if '):
            active.append(active[-1] and bool(value(line[4:])));continue
        if line=='.endif':active.pop();continue
        if not active[-1]:continue
        if line.startswith('.error'):raise AssertionError(line)
        if line.startswith('.equ '):
            name,expr=line[5:].split(',',1)
            constants[name.strip()]=render if name.strip()=='RENDER' else (0 if name.strip()=='RENDER_DELAY' else value(expr))
            continue
        for name,v in constants.items():line=re.sub(r'\b'+name+r'\b',str(v),line)
        if line.startswith('li '):
            op,arg=line.split(',',1)
            try:line=op+','+str(value(arg))
            except (NameError,SyntaxError):pass
        out.append(line)
    assert len(active)==1
    return '\n'.join(out)+'\n'

def load(path):
    code=[]; labels={}; symbols={}; mem=bytearray(2**21); addr=0x10000; section='text'
    source=Path(path).read_text(encoding='utf-8')
    if re.search(r'\.equ\s+RENDER\s*,',source): source=expand(source,0)
    for line in source.splitlines():
        line=line.split('#',1)[0].strip()
        if not line: continue
        if line=='.text': section='text'; continue
        if line=='.bss': section='data'; continue
        if line.startswith('.section'):
            section='text' if '.text' in line else 'data'; continue
        if line.endswith(':'):
            if section=='text': labels[line[:-1]]=len(code)
            else: symbols[line[:-1]]=addr
            continue
        parts=line.split(None,1); op=parts[0]; arg=parts[1] if len(parts)>1 else ''
        if op=='.align':
            if section=='data': addr=(addr+(1<<int(arg))-1)&~((1<<int(arg))-1)
        elif op=='.byte':
            b=bytes(int(x)&255 for x in arg.split(',')); mem[addr:addr+len(b)]=b;addr+=len(b)
        elif op=='.word':
            for value in arg.split(','):
                mem[addr:addr+4]=int(value,0).to_bytes(4,'little');addr+=4
        elif op=='.zero': addr+=int(arg)
        elif op=='.asciz':
            b=ast.literal_eval(arg).encode()+b'\0';mem[addr:addr+len(b)]=b;addr+=len(b)
        elif not op.startswith('.'):
            assert section=='text',line
            code.append((op,[x.strip() for x in arg.split(',')]))
    return code,labels,symbols,mem,addr-0x10000

def run(parsed,text,inspect=None):
    code,labels,symbols,initial,size=parsed;mem=initial.copy()
    start=symbols['target_input'];b=text.encode()+b'\0';mem[start:start+len(b)]=b
    regs={'sp':0x1f0000,'ra':0xffffffff}
    saved={f's{i}':0x12340000+i for i in range(12)};regs.update(saved)
    def reg(r): return 0 if r=='zero' else regs.get(r,0)
    def expr(s):
        m=re.fullmatch(r'([.\w]+)([+]\d+)?',s)
        if m and m[1] in symbols:return symbols[m[1]]+int(m[2] or 0)
        return int(s,0)
    def imm(s):
        if s.startswith('%hi('):return (expr(s[4:-1])+0x800)>>12
        if s.startswith('%lo('):return ((expr(s[4:-1])+0x800)&4095)-0x800
        return int(s,0)
    def address(s):
        p=s.rfind('(');return (reg(s[p+1:-1])+imm(s[:p]))&MASK
    pc=labels['main'];steps=0
    while pc!=0xffffffff:
        op,a=code[pc];pc+=1;steps+=1;v=None
        if op=='li':v=imm(a[1])
        elif op=='lui':v=imm(a[1])<<12
        elif op=='mv':v=reg(a[1])
        elif op=='addi':v=reg(a[1])+imm(a[2])
        elif op=='add':v=reg(a[1])+reg(a[2])
        elif op=='sub':v=reg(a[1])-reg(a[2])
        elif op=='neg':v=-reg(a[1])
        elif op=='andi':v=reg(a[1])&imm(a[2])
        elif op=='and':v=reg(a[1])&reg(a[2])
        elif op=='or':v=reg(a[1])|reg(a[2])
        elif op=='seqz':v=int(reg(a[1])==0)
        elif op=='sltu':v=int(reg(a[1])<reg(a[2]))
        elif op in ('slli','sll'):v=reg(a[1])<<((imm(a[2]) if op=='slli' else reg(a[2]))&31)
        elif op in ('srli','srl'):v=reg(a[1])>>((imm(a[2]) if op=='srli' else reg(a[2]))&31)
        elif op in ('srai','sra'):v=signed(reg(a[1]))>>((imm(a[2]) if op=='srai' else reg(a[2]))&31)
        elif op in ('lbu','lhu','lw'):
            n={'lbu':1,'lhu':2,'lw':4}[op];p=address(a[1]);assert p%n==0
            v=int.from_bytes(mem[p:p+n],'little')
        elif op in ('sb','sh','sw'):
            n={'sb':1,'sh':2,'sw':4}[op];p=address(a[1]);assert p%n==0
            mem[p:p+n]=(reg(a[0])&((1<<(n*8))-1)).to_bytes(n,'little')
        elif op in ('beq','bne','bgeu','bgtu','bleu','bltu','bgt','bge'):
            x,y=reg(a[0]),reg(a[1])
            take={'beq':x==y,'bne':x!=y,'bgeu':x>=y,'bgtu':x>y,'bleu':x<=y,'bltu':x<y,'bgt':signed(x)>signed(y),'bge':signed(x)>=signed(y)}[op]
            if take:pc=labels[a[2]]
        elif op=='j':pc=labels[a[0]]
        elif op=='call':regs['ra']=pc;pc=labels[a[0]]
        elif op=='ecall':
            # Model only Ripes Exit2, not an actual simulator/system call.
            assert reg('a7')==93, 'Unsupported syscall'
            pc=0xffffffff
        elif op in ('ret','jr'):pc=reg('ra' if op=='ret' else a[0])
        else:raise ValueError((op,a))
        if v is not None and a[0]!='zero':regs[a[0]]=v&MASK
        if steps>60000000:raise RuntimeError('instruction-step limit')
    assert regs['sp']==0x1f0000 and all(regs[k]==v for k,v in saved.items()),'ABI restoration failed'
    p=symbols['result_length'];length=signed(int.from_bytes(mem[p:p+4],'little'))
    p=symbols['solution_path'];path=list(mem[p:p+max(length,0)])
    if inspect is not None: inspect(mem)
    return regs.get('a0'),length,path,steps

if __name__=='__main__':
    parsed=load(sys.argv[1]);print('Source data layout:',parsed[-1],'bytes',flush=True)
    source=((1,4,2,0,3,5,6),(0,1,2,4,5,6,3),(0,2,5,3,1,4,6))
    twist=((1,2,0,2,1,0,0),(0,0,0,1,2,1,2),(0,)*7)
    for text,expected in [('12345671111111',0),('123',-2),('11345671111111',-2),('12345671111112',-2),('21345671111111',11)]:
        start=time.perf_counter();status,length,path,steps=run(parsed,text)
        assert length==expected and status==(2 if expected==-2 else 0),(text,status,length)
        if length>=0:
            p=[int(x)-1 for x in text[:7]];o=[int(x)-1 for x in text[7:]]
            for m in path:
                assert 0<=m<9
                for _ in range(m%3+1):
                    f=m//3;p,o=[p[source[f][i]] for i in range(7)],[(o[source[f][i]]+twist[f][i])%3 for i in range(7)]
            assert p==list(range(7)) and o==[0]*7
        print(text,'exit=',status,'length=',length,'path=',path,'source-steps=',steps,'seconds=',round(time.perf_counter()-start,3),flush=True)