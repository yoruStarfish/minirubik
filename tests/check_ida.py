"""Check solution semantics and optimal lengths, independent of tie-breaking."""
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = "R R2 R' B B2 B' D D2 D'".split()
# Model maps each old position to its destination, unlike solver.c's pull map.
DEST = ((3, 0, 2, 4, 1, 5, 6), (0, 1, 2, 6, 3, 4, 5),
        (0, 4, 1, 3, 5, 2, 6))
DELTA = ((2, 1, 0, 1, 2, 0, 0), (0, 0, 0, 2, 1, 2, 1), (0,) * 7)
SOLVED = (tuple(range(7)), (0,) * 7)


def move(state, name):
    m = NAMES.index(name)
    f = m // 3
    for _ in range(m % 3 + 1):
        p, o = [0] * 7, [0] * 7
        for old, new in enumerate(DEST[f]):
            p[new] = state[0][old]
            o[new] = (state[1][old] + DELTA[f][old]) % 3
        state = tuple(p), tuple(o)
    return state


def run(binary, args):
    return subprocess.run([binary, *args], capture_output=True, text=True, timeout=60)


def check(binary, text, optimal=None):
    result = run(binary, [text])
    assert result.returncode == 0, (text, result.stderr)
    moves = result.stdout.split()
    assert result.stdout == ' '.join(moves) + '\n', repr(result.stdout)
    assert len(moves) <= 11
    if optimal is not None:
        assert len(moves) == optimal, (text, len(moves), optimal)
    state = tuple(int(x) - 1 for x in text[:7]), tuple(int(x) - 1 for x in text[7:])
    for name in moves:
        state = move(state, name)
    assert state == SOLVED, (text, moves)
    return len(moves)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    assert run(binary, ['--self-test-quick']).returncode == 0
    for line in (ROOT / 'tests/solutions.txt').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        text, solution = line.split('|')
        check(binary, text, len(solution.split()))
    invalid = ['1234567111111', '123456711111111', '02345671111111',
               '82345671111111', '12345671111110', '12345671111114',
               '1234567111111a', '11345671111111', '12345671111112']
    for args in [[x] for x in invalid] + [[], ['12345671111111'] * 2,
            ['--self-test-shard', '0', '0'], ['--self-test-shard', '1', '1'],
            ['--self-test-shard', 'x', '4'], ['--self-test-shard', '0', '65']]:
        assert run(binary, args).returncode == 2, args
    rng = random.Random(42)
    for _ in range(40):
        state = SOLVED
        for _ in range(20):
            state = move(state, rng.choice(NAMES))
        text = ''.join(str(x + 1) for part in state for x in part)
        expected = None
        if len(sys.argv) > 2:
            oracle = run(str(Path(sys.argv[2]).resolve()), [text])
            assert oracle.returncode == 0
            expected = len(oracle.stdout.split())
        check(binary, text, expected)
    print('8 optimal vectors, 40 scrambles, self-test and input rejection passed')


if __name__ == '__main__':
    main()
