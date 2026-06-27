# memcpy.s - copy 8 bytes from src to dst using LB/SB
#
# Step through this in single-step mode to watch the byte-by-byte
# copy in the register panel.
#
# Assemble:
#   riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib \
#       -Ttext=0x80000000 -o memcpy.elf memcpy.s
#   riscv64-unknown-elf-objcopy -O binary memcpy.elf memcpy.bin
#
# Run:
#   stakrv memcpy.bin

.section .text
.globl _start
_start:
    la    a0, src
    la    a1, dst
    addi  a2, zero, 8

loop:
    beq   a2, zero, done
    lb    a3, 0(a0)
    sb    a3, 0(a1)
    addi  a0, a0, 1
    addi  a1, a1, 1
    addi  a2, a2, -1
    jal   zero, loop

done:
    ecall

.balign 4
src:
    .byte 0xDE, 0xAD, 0xBE, 0xEF
    .byte 0x01, 0x02, 0x03, 0x04

.balign 4
dst:
    .zero 8
    