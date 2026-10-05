# Stage 3: packed-PDB IDA* solver

`solver.c` now uses IDA*, not the exhaustive full-state BFS described in
`report.md`. The previous implementation is retained in `solver_bfs.c`.
The 14-digit input and exit codes are unchanged, but an equally short solution
can have a different move sequence. Host `--self-test` runs exhaustive H1-H4
checks against an independently built exact BFS distance table.

The two exact projected pattern databases are generated offline by
`tools/generate_pdb.py` and checked into `pdb_data.h`. Their packed sizes are
365 bytes (orientation) and 2520 bytes (permutation), totaling 2885 bytes,
approximately 2.82 KiB. This is PDB storage only, not total program memory.
No database construction or heap allocation takes place when solving a query.

Orientation indexing uses the first orientation as the least-significant
base-3 digit, as in the Stage 3 specification. This differs from the older BFS
rank convention; the generated databases use the new convention consistently.
Orientation weights are selected with masks and shifts. Permutation ranking
adds factorial weights for smaller following cubies. Neither indexing function
uses multiplication, division, or remainder operators. Move face/count and
orientation wrapping also use small tables.

The successor lookup removes the repeated same-face rejection branch; it does
not make the entire search branchless. The root enumerates nine moves and
other nodes enumerate six. The heuristic is the maximum of the two projected
distances. Each projected solution distance is a lower bound for the complete
cube, so pruning preserves optimality. Consecutive same-face moves can always
be combined or canceled, so excluding them cannot remove a shortest solution.

The search uses an 11-byte path and a static array of 12 explicit DFS frames.
Each frame stores a 14-byte cube, the previous move, and a successor cursor
(16 bytes per frame, 192 bytes total). There are no recursive calls or dynamic
allocations in IDA*. Pushing copies a child state; popping resumes the parent's
cursor. It returns the actual solution length and guards depth before writing
the path or pushing. Static storage means search calls are not reentrant or
thread-safe; concurrent host validation uses separate processes.

Build and checks on a hosted C99 environment:

```sh
make
make check
make check-fast          # skips H3; does not claim all gates passed
make solver_bfs
python3 tests/check_ida.py ./solver ./solver_bfs
make pdb                 # optional: regenerate committed PDB data
make check-legacy        # old BFS/mini byte-exact vectors
make prove               # original Frama-C contracts in solver_bfs.c only
python3 tests/run_host_gates.py ./solver --jobs 4  # disjoint full-domain H3
python3 tests/check_gate_mutations.py --cc gcc    # deliberate failures must fail
```

`tests/check_pdb.c` exhaustively checks all 5769 projected indices, the unique
zero-distance state, edge consistency, and a descending edge from every
non-goal state. Together these verify exact projected shortest distances.
The CLI tests replay solutions through an independent move implementation and
check known optimal lengths, malformed inputs, and deterministic scrambles.
The optional BFS executable supplies independent optimal-length comparisons.

Host verification modes:

- `--self-test`: full H1-H4, including an IDA* search and independent replay
  for every one of the 3,674,160 states. H3 prints wall-clock time and progress.
- `--self-test-fast`: exhaustive H1/H2/H4 only, explicitly reports H3 skipped.
- `--self-test-quick`: basic smoke tests only, used by CLI regression tests.
- `--self-test-shard K N`: exhaustive H1/H2/H4 plus the disjoint H3 rank interval
  `[floor(STATES*K/N), floor(STATES*(K+1)/N))`. This is a partial H3 result,
  never reported as a whole-domain pass. The Python runner verifies all ranges
  and counts before publishing `tests/h1-h4-results.txt` with aggregate time.

The host-only oracle in `tests/host_gates.h` uses independent source-to-destination
move maps, arithmetic ranking, and a full unpacked distance BFS. It checks
connectivity, solved distance, diameter 11, and the published level histogram.
H1 checks both PDB components and their maximum against exact distance on the
whole domain. H2 checks every PDB entry (no sentinel gaps), solved entries and
maxima 6/7, plus all indexing/move/successor support tables. H4 compares both
packed PDBs at all even/odd indices with unpacked references obtained by taking
the minimum exact distance over each projection; it also checks all 256 byte
patterns and the unused final nibble. H3 calls the production search without
oracle hints, checks exact length, replays the path with the independent model,
checks path guards, and verifies that the input remains unchanged.

`tests/check_search_stack.c` additionally covers bound-zero and depth-eleven
boundaries, an explicit subtree entered at depth ten, invalid depth/move
arguments, repeated searches with static storage, and path guard bytes.
`tests/check_gate_mutations.py` verifies that inflated heuristics, incomplete
PDB data, incorrect successor lists, swapped nibble parity, and wrong returned
solution lengths are rejected by the gates themselves (not just smoke tests).

Measured validation run (Windows x64, GCC 8.3.0, `-O3 -std=c99`, eight separate
worker processes): all H1-H4 gates passed over the full 3,674,160-state domain.
H3-only wall-clock duration per shard was 627.218-633.637 seconds. Total elapsed
time for the complete parallel run, including the oracle and H1/H2/H4 in each
worker, was **636.681 seconds (10 minutes 36.681 seconds)**. These are measured
parallel wall times, not a single-process benchmark or a sum of CPU times.
The full output and exact disjoint ranges are in `tests/h1-h4-results.txt`.
No sampling, cached solutions, or exact-distance hints were used by IDA*.
Native warning-enabled builds, both host/embedded CLI regressions, static-stack
boundary tests, projected-table tests, and all five gate mutation checks passed.
The Makefile targets were updated but not executed on this Windows host, which
has no `make`/POSIX shell available; the C and Python checks were run directly.

Only the host oracle allocates the full BFS table/queue. Define
`MINIRUBIK_EMBEDDED` to omit it (and its large host-only work arrays and timing
APIs) from embedded builds. The search itself still uses only static frames
and the packed databases; full-domain host verification is not run in Ripes.

RV32I/Ripes integration still requires a suitable compiler, startup code,
linker configuration, and input/output support. The current CLI uses hosted
`stdio` and command-line arguments; it is not a standalone Ripes image.
Native C checks do not establish RV32I instruction counts or simulator timing.
When cross-compiling, use `-march=rv32i -mabi=ilp32` and inspect the emitted
code for arithmetic helper calls as well as multiply/divide instructions.
