# fizzbuzz.s - run FizzBuzz from 1 to 20, storing results in memory
#
# Encoding: 0 = plain number, 1 = Fizz, 2 = Buzz, 3 = FizzBuzz
# After ecall, inspect the `out` array to verify.
#
# Step through slowly to watch the modulo loops and branch logic.

.section .text
.globl _start
_start:
    la    a0, out
    addi  a1, zero, 1          # i = 1
    addi  a2, zero, 21         # limit

loop:
    bge   a1, a2, done

    # compute i % 3  (via repeated subtraction)
    addi  t0, a1, 0
mod3:
    addi  t1, zero, 3
    blt   t0, t1, got_mod3
    sub   t0, t0, t1
    jal   zero, mod3
got_mod3:                      # t0 = i % 3

    # compute i % 5
    addi  t2, a1, 0
mod5:
    addi  t3, zero, 5
    blt   t2, t3, got_mod5
    sub   t2, t2, t3
    jal   zero, mod5
got_mod5:                      # t2 = i % 5

    addi  t4, zero, 0          # result = 0 (plain)
    bne   t0, zero, check_buzz
    addi  t4, t4, 1            # Fizz
check_buzz:
    bne   t2, zero, store
    addi  t4, t4, 2            # Buzz (or FizzBuzz if t4 was already 1)
store:
    sw    t4, 0(a0)
    addi  a0, a0, 4
    addi  a1, a1, 1
    jal   zero, loop

done:
    addi  a0, zero, 1
    ecall

.balign 4
out:
    .zero 80                   # 20 words
    