"""Check linked ELF data usage, including small-data section variants."""
import argparse
import subprocess

p=argparse.ArgumentParser()
p.add_argument('elf')
p.add_argument('--size-tool', default='riscv64-unknown-elf-size')
p.add_argument('--limit', type=int, default=128000, help='bytes; conservative 128 KB default')
a=p.parse_args()
r=subprocess.run([a.size_tool,'-A',a.elf],capture_output=True,text=True,check=True)
used=0
found=False
for line in r.stdout.splitlines():
    parts=line.split()
    if len(parts)<2: continue
    name=parts[0]
    if name.startswith(('.data','.rodata','.bss','.sdata','.srodata','.sbss','.rdata','.noinit')):
        n=int(parts[1]); used+=n; found=True
        print(f'{name}: {n} bytes')
if not found:
    raise SystemExit('No data sections found; cannot establish memory usage')
print(f'Data total: {used} bytes; limit: {a.limit}; remaining: {a.limit-used}')
raise SystemExit(0 if used<=a.limit else 1)
