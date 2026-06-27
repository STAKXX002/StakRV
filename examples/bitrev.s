# bitrev.s - reverse the bits of a 32-bit integer
#
# Input:  a0 = 0x00000001  (bit 0 set)
# Result: a0 = 0x80000000  (bit 31 set)

.section .text
.globl _start
_start:
    addi  a0, zero, 1          # input: 0x00000001
    addi  a1, zero, 0          # result = 0
    addi  a2, zero, 32         # bit counter

loop:
    beq   a2, zero, done
    slli  a1, a1, 1            # result <<= 1
    andi  t0, a0, 1            # LSB of input
    or    a1, a1, t0           # result |= LSB
    srli  a0, a0, 1            # input >>= 1
    addi  a2, a2, -1
    jal   zero, loop

done:
    addi  a0, a1, 0
    ecall
    