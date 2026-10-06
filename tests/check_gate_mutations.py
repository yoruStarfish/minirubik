"""Ensure the host gates reject representative corruptions (native compiler)."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (root / 'solver.c').read_text(encoding='utf-8')
    pdb = (root / 'pdb_data.h').read_text(encoding='utf-8')
    # Bypass smoke tests so failures must come from the named full-domain gate.
    source = source.replace('int main(int argc, char **argv)', 'int original_main(int argc, char **argv)')
    source += '\nint main(void) { return host_gates(true, 0, 64) ? 0 : 1; }\n'
    cases = [
        ('H1 inflated heuristic', 'return ori > perm ? ori : perm;',
         'return (uint8_t)((ori > perm ? ori : perm) + 1);', 'H1 rank='),
        ('H2 successor table', '{6,6,6,6,6,6,6,6,6,9}',
         '{5,6,6,6,6,6,6,6,6,9}', 'H2: support table mismatch'),
        ('H3 wrong returned length', 'return length;', 'return length + 1;', 'H3 rank='),
        ('H4 swapped nibble parity', '((index & 1U) << 2)',
         '(((index ^ 1U) & 1U) << 2)', 'H2/H4 orientation index='),
    ]
    with tempfile.TemporaryDirectory(prefix='gate-mutations-', dir=root / 'tests') as directory:
        work = Path(directory)
        (work / 'tests').mkdir()
        (work / 'transition_data.h').write_text((root / 'transition_data.h').read_text(encoding='utf-8'))
        (work / 'tests/host_gates.h').write_text((root / 'tests/host_gates.h').read_text(encoding='utf-8'), encoding='utf-8')
        for name, old, new, diagnostic in cases + [('H2 unpopulated PDB', '', '', 'H2/H4 orientation index=')]:
            if old:
                assert old in source
            (work / 'solver.c').write_text(source.replace(old, new) if old else source, encoding='utf-8')
            damaged_pdb = re.sub(r'0x([0-9a-f])([0-9a-f])', r'0x\1f', pdb, count=1) if not old else pdb
            (work / 'pdb_data.h').write_text(damaged_pdb)
            binary = work / 'mutant.exe'
            subprocess.run([args.cc, '-O2', '-std=c99', str(work / 'solver.c'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
            output = result.stdout + result.stderr
            assert result.returncode != 0 and diagnostic in output, (name, output)
            assert 'H1-H4 PASS' not in output
            print(f'{name}: rejected by gate', flush=True)


if __name__ == '__main__':
    main()
