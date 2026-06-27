# strcpy.s - copy a null-terminated string from src to dst
#
# After ecall, dst contains a copy of src.
# Step through to watch each character move through a2.

.section .text
.globl _start
_start:
    la    a0, src
    la    a1, dst

loop:
    lb    a2, 0(a0)
    sb    a2, 0(a1)
    beq   a2, zero, done       # stop after copying null terminator
    addi  a0, a0, 1
    addi  a1, a1, 1
    jal   zero, loop

done:
    addi  a0, zero, 1
    ecall

.balign 4
src:
    .asciz "StakRV"

.balign 4
dst:
    .zero 8
    