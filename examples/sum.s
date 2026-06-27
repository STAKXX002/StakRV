# sum.s - sum integers 1 to 10
#
# Result ends up in a0 (== 55) when the emulator halts on ecall.
#
# Assemble:
#   riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib \
#       -Ttext=0x80000000 -o sum.elf sum.s
#   riscv64-unknown-elf-objcopy -O binary sum.elf sum.bin
#
# Run:
#   stakrv sum.bin

.section .text
.globl _start
_start:
    addi  a0, zero, 0     # accumulator = 0
    addi  a1, zero, 1     # i = 1
    addi  a2, zero, 11    # limit = 11
loop:
    bge   a1, a2, done    # if i >= 11, stop
    add   a0, a0, a1      # acc += i
    addi  a1, a1, 1       # i++
    jal   zero, loop
done:
    ecall                 # halt - inspect a0 in the register panel
    
