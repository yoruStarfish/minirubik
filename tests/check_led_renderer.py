"""Validate conditional assembly and renderer with a source-level RV32I interpreter.
Not an assembler, ELF test, Ripes GUI test, or instruction-count measurement.
Usage: python tests/check_led_renderer.py solver_assembly.s [original.s]
"""
import re,sys,tempfile,random
from pathlib import Path
from check_embedded_asm import load,run,expand

BASE=0x180000 # Mock MMIO RAM, never a hardware address assumption.

# World coordinates x=right,y=up,z=front; normals identify sticker faces.
coords=[(1,1,1),(1,-1,1),(-1,-1,1),(1,1,-1),(1,-1,-1),(-1,-1,-1),(-1,1,-1),(-1,1,1)]
normals=[(0,1,0),(-1,0,0),(0,0,1),(1,0,0),(0,0,-1),(0,-1,0)]
palette=[0xffffff,0xff8000,0x00ff00,0xff0000,0x0000ff,0xffff00]
faces=[(9,1,[6,3,7,0]),(0,9,[6,7,5,2]),(9,9,[7,0,2,1]),(18,9,[0,3,1,4]),(27,9,[3,6,4,5]),(9,17,[2,1,5,4])]
src=[[1,4,2,0,3,5,6],[0,1,2,4,5,6,3],[0,2,5,3,1,4,6]]
twist=[[1,2,0,2,1,0,0],[0,0,0,1,2,1,2],[0]*7]

def physical_turn(stickers,face):
    def selected(p):return p[0]==1 if face==0 else (p[2]==-1 if face==1 else p[1]==-1)
    def rotate(v):
        x,y,z=v
        return (x,z,-y) if face==0 else ((-y,x,z) if face==1 else (z,y,-x))
    return {(rotate(p),rotate(n)) if selected(p) else (p,n):c for (p,n),c in stickers.items()}

def check_frame(mem,stickers):
    expected=[0]*875
    for f,(x,y,corners) in enumerate(faces):
        for slot,corner in enumerate(corners):
            color=palette[stickers[(coords[corner],normals[f])]]
            for dy in range(3):
                for dx in range(4):expected[(y+slot//2*3+dy)*35+x+slot%2*4+dx]=color
    actual=[int.from_bytes(mem[BASE+i*4:BASE+i*4+4],'little') for i in range(875)]
    assert actual==expected,'Rendered colors disagree with independent geometric rotation'
    assert mem[BASE-4:BASE]==b'\x00'*4 and mem[BASE+3500:BASE+3504]==b'\x00'*4

def main():
    source=Path(sys.argv[1]).read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory() as tmp:
        tmp=Path(tmp);off=tmp/'off.s';off.write_text(expand(source,0));offparsed=load(off)
        assert not any('render' in label or 'draw_' in label for label in offparsed[1])
        if len(sys.argv)>2:
            old=load(sys.argv[2]);assert offparsed==old,'Renderer-off differs from original code/data'
            print('OFF: instructions, labels, data, storage identical to original')
        print('OFF: source data size',offparsed[-1])
        on=expand(source,1)
        # Replace main for fast focused renderer checks; all renderer instructions are unchanged.
        start=on.index('main:');end=on.index('.size main,',start)
        prefix='''main:
addi sp,sp,-32
sw ra,28(sp)
lui a0,%hi(target_input)
addi a0,a0,%lo(target_input)
mv a1,sp
call parse_target
mv a0,sp
call render_init
'''
        suffix='''li a0,0
lw ra,28(sp)
addi sp,sp,32
ret
'''
        rng=random.Random(42)
        for move in range(9):
            p=list(range(7));o=[0]*7
            stickers={(pos,n):f for pos in coords for f,n in enumerate(normals) if sum(a*b for a,b in zip(pos,n))==1}
            for scramble in [rng.randrange(9) for _ in range(12)]:
                for _ in range(scramble%3+1):
                    f=scramble//3;p,o=[p[src[f][i]] for i in range(7)],[(o[src[f][i]]+twist[f][i])%3 for i in range(7)]
                    stickers=physical_turn(stickers,f)
            text=''.join(str(x+1) for x in p+o)
            for apply in (False,True):
                body=prefix+(f'li a0,{move}\ncall render_apply_move\ncall draw_cube\n' if apply else '')+suffix
                path=tmp/'on.s';path.write_text(on[:start]+body+on[end:]);parsed=load(path)
                if apply:
                    for _ in range(move%3+1):stickers=physical_turn(stickers,move//3)
                run(parsed,text,lambda mem:check_frame(mem,stickers))
        # Full 11-move playback from the user's target; verify final cubies + solved display.
        body=prefix+'call render_solution\n'+suffix
        path.write_text(on[:start]+body+on[end:]);parsed=load(path)
        mem=parsed[3];symbols=parsed[2]
        ptr=symbols['result_length'];mem[ptr:ptr+4]=(11).to_bytes(4,'little')
        ptr=symbols['solution_path'];mem[ptr:ptr+11]=bytes([0,5,7,2,3,2,5,0,7,0,3])
        solved={(pos,n):f for pos in coords for f,n in enumerate(normals) if sum(a*b for a,b in zip(pos,n))==1}
        def final(mem):
            ptr=symbols['render_state'];assert mem[ptr:ptr+14]==bytes(list(range(7))+[0]*7)
            check_frame(mem,solved)
        run(parsed,'21345671111111',final)
        print('ON: nine moves on scrambled states match 3D sticker rotations; 11-move replay solves target; ABI preserved')
        print('ON: source data size',parsed[-1])
if __name__=='__main__':main()
