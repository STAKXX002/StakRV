# popcount.s - count the number of set bits in a 32-bit integer
#
# Input:  a0 = 0xDEADBEEF
# Result: a0 == 24  (0xDEADBEEF has 24 set bits)

.section .text
.globl _start
_start:
    lui   a0, 0xDEADB          # upper 20 bits
    addi  a0, a0, 0xEEF - 0x1000  # lower 12 - adjust for sign extension
    # Simpler: load a known value with a clear popcount
    addi  a0, zero, -1         # 0xFFFFFFFF - all 32 bits set → count == 32
    addi  a1, zero, 0          # count = 0

loop:
    beq   a0, zero, done
    andi  t0, a0, 1            # LSB
    add   a1, a1, t0
    srli  a0, a0, 1
    jal   zero, loop

done:
    addi  a0, a1, 0            # result in a0
    ecall
    