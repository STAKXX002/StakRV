# bubble_sort.s - sort an array of 8 integers in-place using bubble sort
#
# After ecall, the array at `arr` is sorted in ascending order.
# Step through in single-step mode to watch swaps happen in a0/a1.
#
# Result: a0 == 1 (sorted flag)

.section .text
.globl _start
_start:
    la    t0, arr
    addi  t1, zero, 8          # n = 8

outer:
    addi  t1, t1, -1           # n--
    beq   t1, zero, done
    addi  t2, zero, 0          # i = 0
    addi  t3, zero, 0          # swapped = 0

inner:
    bge   t2, t1, check
    slli  t4, t2, 2            # byte offset = i * 4
    add   t5, t0, t4           # &arr[i]
    lw    a0, 0(t5)            # arr[i]
    lw    a1, 4(t5)            # arr[i+1]
    ble   a0, a1, no_swap
    sw    a1, 0(t5)
    sw    a0, 4(t5)
    addi  t3, zero, 1          # swapped = 1
no_swap:
    addi  t2, t2, 1
    jal   zero, inner

check:
    beq   t3, zero, done       # no swaps → sorted
    jal   zero, outer

done:
    addi  a0, zero, 1
    ecall

.balign 4
arr:
    .word 42, 7, 19, 3, 55, 1, 28, 13
    