"""Run disjoint H3 ranges in separate processes; require exact full coverage.

No threads share the non-reentrant static search stack. Each worker runs the
same production ida_star, with no oracle information passed into the search.
"""
import argparse
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('solver', type=Path)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent / 'h1-h4-results.txt')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 64:
        parser.error('--jobs must be between 1 and 64')
    solver = args.solver.resolve()
    workers = []
    started = time.perf_counter()
    try:
        for shard in range(args.jobs):
            log = args.output.with_name(f'h3-shard-{shard}.log')
            stream = log.open('w')
            proc = subprocess.Popen([str(solver), '--self-test-shard', str(shard), str(args.jobs)],
                                    stdout=stream, stderr=subprocess.STDOUT)
            workers.append((proc, stream, log))
        while any(proc.poll() is None for proc, _, _ in workers):
            if any(proc.poll() not in (None, 0) for proc, _, _ in workers):
                raise RuntimeError('A gate failed; inspect shard logs')
            time.sleep(5)
        elapsed = time.perf_counter() - started
        reports = []
        total = 0
        for shard, (proc, stream, log) in enumerate(workers):
            stream.close()
            report = log.read_text()
            assert proc.returncode == 0, report
            match = re.search(r'H3 PASS: ranks \[(\d+),(\d+)\); (\d+) optimal.*; ([\d.]+) s wall', report)
            assert match, report
            begin, end, count = map(int, match.group(1, 2, 3))
            assert begin == 3674160 * shard // args.jobs
            assert end == 3674160 * (shard + 1) // args.jobs and count == end - begin
            assert 'H1/H2/H4 PASS; H3 shard PASS' in report or 'H1-H4 PASS' in report
            total += count
            reports.append(report)
        assert total == 3674160
        summary = (f'H1-H4 PASS: {total} states; {args.jobs} separate worker processes.\n'
                   f'Complete sharded run wall-clock time (includes oracle/H1/H2/H4): {elapsed:.3f} s\n'
                   'H3-only wall-clock durations for each disjoint shard are recorded below.\n')
        args.output.write_text(summary + '\n'.join(reports))
        print(summary, flush=True)
    finally:
        for proc, stream, _ in workers:
            if proc.poll() is None:
                proc.terminate()
                proc.wait()
            stream.close()


if __name__ == '__main__':
    main()
