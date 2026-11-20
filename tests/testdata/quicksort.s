# Sorts a .data array of 32-bit words in place with recursive quicksort and
# prints the result.
    .data
array:  .word 34, -7, 23, 32, 5, 62, -32, 0, 7, 7, 100, -1, 15, 2, 9, 41
array_end:
    .equ COUNT, (array_end - array) / 4
space:  .asciz " "

    .text
main:
    la      a0, array
    li      a1, 0
    li      a2, COUNT - 1
    call    quicksort

    la      s0, array
    li      s1, COUNT
1:
    lw      a0, 0(s0)
    li      a7, 1
    ecall
    la      a0, space
    li      a7, 4
    ecall
    addi    s0, s0, 4
    addi    s1, s1, -1
    bnez    s1, 1b

    li      a0, '\n'
    li      a7, 11
    ecall
    li      a7, 10
    ecall

# void quicksort(int *a, int lo, int hi) — Lomuto partition.
quicksort:
    bge     a1, a2, .Lqs_done
    addi    sp, sp, -32
    sw      ra, 28(sp)
    sw      s0, 24(sp)
    sw      s1, 20(sp)
    sw      s2, 16(sp)
    sw      s3, 12(sp)
    mv      s0, a0              # base
    mv      s1, a1              # lo
    mv      s2, a2              # hi

    slli    t0, s2, 2
    add     t0, s0, t0
    lw      t1, 0(t0)           # pivot = a[hi]
    addi    s3, s1, -1          # i = lo - 1
    mv      t2, s1              # j = lo
.Lqs_loop:
    bge     t2, s2, .Lqs_partitioned
    slli    t3, t2, 2
    add     t3, s0, t3
    lw      t4, 0(t3)
    bgt     t4, t1, .Lqs_next
    addi    s3, s3, 1
    slli    t5, s3, 2
    add     t5, s0, t5
    lw      t6, 0(t5)
    sw      t4, 0(t5)
    sw      t6, 0(t3)
.Lqs_next:
    addi    t2, t2, 1
    j       .Lqs_loop
.Lqs_partitioned:
    addi    s3, s3, 1           # pivot index
    slli    t5, s3, 2
    add     t5, s0, t5
    lw      t6, 0(t5)
    slli    t0, s2, 2
    add     t0, s0, t0
    lw      t1, 0(t0)
    sw      t1, 0(t5)
    sw      t6, 0(t0)

    mv      a0, s0
    mv      a1, s1
    addi    a2, s3, -1
    call    quicksort
    mv      a0, s0
    addi    a1, s3, 1
    mv      a2, s2
    call    quicksort

    lw      ra, 28(sp)
    lw      s0, 24(sp)
    lw      s1, 20(sp)
    lw      s2, 16(sp)
    lw      s3, 12(sp)
    addi    sp, sp, 32
.Lqs_done:
    ret
