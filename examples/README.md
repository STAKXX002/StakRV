# Examples

Short RV32I programs to try with StakRV. Each one is self-contained and ends
with `ecall` to halt the emulator.

## Building

You need the RISC-V GNU toolchain (`riscv64-unknown-elf-gcc`).

```bash
make        # build all examples
make sum    # build one
make clean
```

Then run with:

```bash
./build/stakrv examples/sum.bin
```

## Programs

| File | What it does | Result in `a0` |
|------|-------------|----------------|
| `sum.s` | Sums integers 1–10 with a loop | `0x37` (55) |
| `fib.s` | Computes fib(10) iteratively | `0x37` (55) |
| `factorial.s` | Computes 7! recursively | `0x13B0` (5040) |
| `bubble_sort.s` | Sorts an array of 8 integers in-place | `0x1` |
| `strlen.s` | Length of a null-terminated string | `0xD` (13) |
| `strcpy.s` | Copies a null-terminated string byte by byte | `0x1` |
| `popcount.s` | Counts set bits in 0xFFFFFFFF | `0x20` (32) |
| `bitrev.s` | Reverses bits of 0x00000001 | `0x80000000` |
| `fizzbuzz.s` | FizzBuzz 1–20, results stored in memory | `0x1` |

## Tips

- `factorial.s` and `bubble_sort.s` are good for watching the **stack gauge** - the
  stack grows visibly during recursive calls and nested loops.
- `strcpy.s` and `memcpy.s` are best in **single-step mode** (`s` key) - you can
  watch each byte move through the registers one at a time.
- `fizzbuzz.s` is branch-heavy - useful for seeing how the disassembly lookahead
  panel tracks through conditional jumps.