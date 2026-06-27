# fib.s - compute fib(10) iteratively
#
# Result ends up in a0 (== 55) when the emulator halts on ecall.
#
# Assemble:
#   riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib \
#       -Ttext=0x80000000 -o fib.elf fib.s
#   riscv64-unknown-elf-objcopy -O binary fib.elf fib.bin
#
# Run:
#   stakrv fib.bin

.section .text
.globl _start
_start:
    addi  a0, zero, 0     # a = fib(0) = 0
    addi  a1, zero, 1     # b = fib(1) = 1
    addi  a2, zero, 10    # n = 10 iterations
loop:
    beq   a2, zero, done
    add   a3, a0, a1      # tmp = a + b
    addi  a0, a1, 0       # a = b
    addi  a1, a3, 0       # b = tmp
    addi  a2, a2, -1      # n--
    jal   zero, loop
done:
    ecall                 # halt — a0 == 55
    