# Prints the first 16 Fibonacci numbers iteratively, then fib(15) computed
# recursively. Works on RV32I and RV64I.
    .data
sep:    .asciz " "
header: .asciz "fib: "
rec:    .asciz "\nrecursive fib(15) = "
nl:     .asciz "\n"

    .text
    .globl main
main:
    la      a0, header
    li      a7, 4
    ecall

    li      s0, 0               # fib(i)
    li      s1, 1               # fib(i+1)
    li      s2, 16              # count
1:
    mv      a0, s0
    li      a7, 1
    ecall
    la      a0, sep
    li      a7, 4
    ecall
    add     t0, s0, s1
    mv      s0, s1
    mv      s1, t0
    addi    s2, s2, -1
    bnez    s2, 1b

    la      a0, rec
    li      a7, 4
    ecall
    li      a0, 15
    call    fib
    li      a7, 1
    ecall
    la      a0, nl
    li      a7, 4
    ecall

    li      a0, 0
    li      a7, 93
    ecall

# int fib(int n) — naive recursion.
fib:
    li      t0, 2
    blt     a0, t0, 2f
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      s0, 8(sp)
    sw      a0, 4(sp)
    addi    a0, a0, -1
    call    fib
    mv      s0, a0
    lw      a0, 4(sp)
    addi    a0, a0, -2
    call    fib
    add     a0, a0, s0
    lw      s0, 8(sp)
    lw      ra, 12(sp)
    addi    sp, sp, 16
2:
    ret
