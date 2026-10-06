# Stage 4 completion status (SDD)

Build-time selectors in solver_assembly.s:

| TEST_CASE | Input | Exact BFS distance | Purpose |
| --- | --- | --- | --- |
| 0 (default) | 21345671111111 | 11 | Required T6 state |
| 1 | 12345671111111 | 0 | Solved state |
| 2 | 25314672313211 | 1 | Short scramble |
| 3 | 12345672121232 | 11 | Own distance-11 state |

Only one 15-byte NUL-terminated input is emitted. RENDER defaults to 0 but
can be selected with GNU as --defsym. Neither selector adds runtime work.
The search and its result_length/solution_path interface are unchanged.
T5 checking is external so it does not inflate the performance measurement.

## Repeatable source checks

From HW1/minirubik:

```powershell
gcc -O2 solver_bfs.c -o bfs-oracle.exe
python tests/check_stage4.py solver_assembly.s --oracle .\bfs-oracle.exe --output tests/stage4-source-results.json
python tests/check_led_renderer.py solver_assembly.s
```

The test selects each input through conditional assembly, runs the source
instruction interpreter, reads result_length/solution_path, replays every
move from the original input, and compares all 14 entries against
12345671111111. It also compares each length to the native C exact BFS oracle.
Case 3 was selected by enumerating that oracle's domain and choosing a state
at distance 11, independently of the assembly search.

T6 source result: R B' D2 R' B R' B' R D2 R B, length 11, replay solved.
Detailed case results are in tests/stage4-source-results.json. Source steps
and Python wall times in that file are NOT Ripes --iret or execution times.
The renderer test checks nine moves against geometric sticker rotations.

## Final ELF / T7 measurements (pending on Ripes)

The editing environment has native C GCC but no available RISC-V GCC or Ripes
executable. Source checks do not satisfy the final ELF measurement gates.
Use your existing Ripes-compatible toolchain/startup. Obtain the pipelined
processor ID from your installed Ripes --help output.

```powershell
$ripes = Read-Host 'Full path to Ripes.exe'
$pipeline = Read-Host 'Pipelined processor ID from Ripes --help'
New-Item -ItemType Directory -Force stage4-results | Out-Null
foreach ($case in 0..3) {
  $elf = "stage4-results/case-$case.elf"
  riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 `
    '-Wa,--defsym,RENDER=0' "-Wa,--defsym,TEST_CASE=$case" `
    solver_assembly.s -o $elf
  if ($LASTEXITCODE -ne 0) { throw 'ELF build failed' }
  python tools/check_data_size.py $elf --limit 131072
  if ($LASTEXITCODE -ne 0) { throw 'ELF memory gate failed' }
  riscv64-unknown-elf-size -A $elf | Out-File "stage4-results/size-$case.txt"
  riscv64-unknown-elf-objdump -d $elf | Out-File "stage4-results/disasm-$case.txt"
  foreach ($proc in @('RV32_ISS', $pipeline)) {
    & $ripes --mode cli --src $elf -t elf --proc $proc --iret `
      --output "stage4-results/case-$case-$proc.txt"
    if ($LASTEXITCODE -ne 0) { throw 'Ripes run failed' }
  }
}
```

Inspect final disassemblies for mul/mulh/mulhu/mulhsu, div/divu, rem/remu and
arithmetic helper calls, including linked startup/library code. Record .text
and the sum of .data/.rodata/.bss (including small-data variants). Final static
data must not exceed 131072 bytes. Source layout is 107016 bytes off and
107314 bytes on; that is not a substitute for linked ELF section sizes.

For each ELF run, also inspect result_length and solution_path in Ripes and
replay the recorded moves. Exit code 0 alone does not establish path validity.
Do not mark final-ELF T5/T6 complete based only on source simulation.

| Case | RV32_ISS --iret | Pipelined --iret | ELF path replay | Final ELF sizes |
| --- | --- | --- | --- | --- |
| 0 | pending | pending | pending | pending |
| 1 | pending | pending | pending | pending |
| 2 | pending | pending | pending | pending |
| 3 | pending | pending | pending | pending |

GUI animation with exported LED symbols remains a separate validation step;
see LED_RENDERER.md. No GUI screenshot or real Ripes performance is claimed.
