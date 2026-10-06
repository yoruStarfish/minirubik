# Embedded hand-written assembly

solver.s now contains only main, parse_target, evaluate_heuristic and ida_star,
plus the transition/PDB tables and search storage they require. Host gates,
oracle code, output functions and arithmetic helper calls were removed.
solver.c and gcc_solver.s were not changed.

The fixed target_input is 21345671111111. main parses it, searches, stores the
length in result_length and move IDs in solution_path, then returns:
0 = solved, 1 = search failed, 2 = invalid input. No solution is printed.
Move IDs 0..8 mean R, R2, R', B, B2, B', D, D2, D'.

The earlier manual optimization is retained: s9 points to the current six-byte
search frame, advancing on push and retreating on pop. Its original value is
saved/restored according to the ABI. Table anchor offsets remain compatible
with the original generated search instructions.

Build with your RISC-V toolchain:

    riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 solver.s -o solver.elf

The assembly already contains the embedded entry point; no -D is needed.
A compatible startup/runtime must be provided by your toolchain as before.
For GCC comparisons, generate an embedded baseline from solver.c using -O2
and -DMINIRUBIK_EMBEDDED. The existing host baseline is not comparable.

Validation:

    python tests/check_embedded_asm.py solver.s

This limited source-instruction interpreter checks solved/invalid inputs,
callee-saved registers and stack restoration, and the fixed target. The target
returns length 11 and path [0,5,7,2,3,2,5,0,7,0,3]; independent move replay
confirms it solves the cube. Source data including alignment is 107016 bytes.
This is not an ELF execution test, exhaustive H1-H4 validation, or a retired
instruction measurement. Check final linked memory size and performance in
Ripes. No RISC-V assembler is available in the editing environment.
