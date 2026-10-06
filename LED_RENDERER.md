# LED animation for solver_assembly.s

RENDER defaults to 0 for official performance measurement. Select GUI
animation with `-Wa,--defsym,RENDER=1`; no source edit is needed. All rendering calls, code, tables,
and state buffers are inside `.if RENDER`; the default is 0.

The default input remains 21345671111111. Select the four SDD cases with
`-Wa,--defsym,TEST_CASE=0` through `3`; see STAGE4.md. The GUI version shows the input while
IDA* searches, then replays the final solution, one frame per move (R2/R' are
one move each). It does not animate the search tree. It returns normally and
leaves the solved cube on the LEDs. result_length and solution_path retain
the search result. No host H1-H4 gates are included.

## CLI / measurement build

From HW1/minirubik, with RENDER=0:

```powershell
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 solver_assembly.s -o solver-asm.elf
.\Ripes.exe --mode cli --src .\solver-asm.elf -t elf --proc RV32_ISS --iret --output assembly-result.txt
```

Use the actual path to Ripes.exe. No LED symbols or peripheral are needed for
this build. Do not pass -DMINIRUBIK_EMBEDDED to try to change an existing .s.

## GUI build

1. In Ripes I/O, add LED Matrix; set Width=35, Height=25.
2. Read LED_MATRIX_0_BASE, LED_MATRIX_0_WIDTH and LED_MATRIX_0_HEIGHT from its
   exported symbols. Device addresses are assigned by Ripes; do not assume a
   particular address. If your matrix instance is not instance 0, pass that
   instance's actual values under the names used below.
3. Select RENDER=1 through the assembler option shown below.
4. Build using those values. External GCC cannot automatically read the GUI's
   exported symbols; pass them to GNU as explicitly:

```powershell
$ledBase = Read-Host 'LED_MATRIX_0_BASE shown in Ripes (hexadecimal)'
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 `
  '-Wa,--defsym,RENDER=1' `
  "-Wa,--defsym,LED_MATRIX_0_BASE=$ledBase" `
  '-Wa,--defsym,LED_MATRIX_0_WIDTH=35' `
  '-Wa,--defsym,LED_MATRIX_0_HEIGHT=25' `
  solver_assembly.s -o solver-asm-render.elf
```

5. Load solver-asm-render.elf in the same configured GUI and run. Expected:
   input cube, 11 solution moves, then six uniformly colored solved faces;
   result_length=11 and exit code 0. Rebuild if the peripheral address changes.
6. Rebuild with `-Wa,--defsym,RENDER=0` before measuring performance.

The assembler reports an error if LED symbols are missing or dimensions differ
from 35x25. The MMIO base is never hardcoded in the program. RENDER_DELAY is a
busy-loop iteration count, not a time in milliseconds. Adjust it for the GUI's
simulation speed if frames pass too quickly. The test harness sets this delay
to zero; the actual GUI build uses the value in the assembly.

## Display mapping

The net matches report.md: U above F; L F R B across; D below F. Each face has
four 4x3 pixel facelets. Face origins are U(9,1), L(0,9), F(9,9), R(18,9),
B(27,9), D(9,17); all writes stay within 35x25. Background pixels are black.
Colors: U white, L orange, F green, R red, B blue, D yellow.

The renderer keeps its own 14-byte cubie state and 14-byte temporary state.
Full nine-move maps match solver.c. Cubie color triples use the same twist
convention as the solver, including the fixed front-upper-left corner.
draw_pixel takes a0=x, a1=y, a2=0x00RRGGBB; it uses shifts/additions for y*35.
No multiply, divide, floating point, or output-library calls were added.

## Validation and limits

```powershell
python tests/check_led_renderer.py solver_assembly.s
python tests/check_embedded_asm.py solver_assembly.s
```

The first test checks all nine moves on scrambled states against independent 3D
sticker rotations, and replays the 11-move solution. It checks framebuffer
colors/background and callee-saved register/stack restoration. The second
runs the original solver checks with rendering conditionally excluded.

Source data layout is 107016 bytes off, 107314 bytes on; final ELF memory use
also depends on the linked runtime. These are source-instruction simulations,
not actual ELF assembly, GUI validation, or retired instruction measurements.
No RISC-V assembler or Ripes GUI was available in the editing environment.

Ripes references:
- https://github.com/mortbopet/Ripes/blob/master/docs/mmio.md
- https://github.com/mortbopet/Ripes/blob/master/src/io/ioledmatrix.cpp
