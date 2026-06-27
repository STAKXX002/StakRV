# factorial.s - compute 7! recursively
#
# Good for watching the stack grow and shrink in the stack gauge.
# Result: a0 == 5040 (0x13B0)
#
# Note: uses RV32I only - multiplication done via repeated addition.

.section .text
.globl _start
_start:
    addi  a0, zero, 7
    jal   ra, fact
    ecall

# int fact(int n) — n in a0, result in a0
fact:
    addi  sp, sp, -8
    sw    ra, 4(sp)
    sw    a0, 0(sp)

    addi  t0, zero, 1
    ble   a0, t0, base         # n <= 1 → return 1

    addi  a0, a0, -1
    jal   ra, fact             # a0 = fact(n-1)

    lw    t0, 0(sp)            # t0 = n
    # multiply: a0 = a0 * t0 via repeated addition
    addi  t1, a0, 0            # t1 = fact(n-1)
    addi  a0, zero, 0          # a0 = 0 (accumulator)
mul_loop:
    beq   t0, zero, mul_done
    add   a0, a0, t1
    addi  t0, t0, -1
    jal   zero, mul_loop
mul_done:
    jal   zero, ret

base:
    addi  a0, zero, 1

ret:
    lw    ra, 4(sp)
    addi  sp, sp, 8
    jalr  zero, ra, 0
    