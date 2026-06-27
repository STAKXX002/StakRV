# strlen.s - compute the length of a null-terminated string
#
# Result: a0 == length of the string (not including null terminator)
# For the string below, a0 == 13 ("Hello, RISC-V" is 13 chars)

.section .text
.globl _start
_start:
    la    a0, str
    addi  a1, zero, 0          # length = 0

loop:
    lb    t0, 0(a0)
    beq   t0, zero, done       # null terminator
    addi  a0, a0, 1
    addi  a1, a1, 1
    jal   zero, loop

done:
    addi  a0, a1, 0            # return length in a0
    ecall

.balign 4
str:
    .asciz "Hello, RISC-V"
    