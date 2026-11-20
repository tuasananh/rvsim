# Recursive factorial of 1..10, exits with 10! mod 256 via Exit2.
    .data
eq:     .asciz "! = "
nl:     .asciz "\n"

    .text
    .globl _start
_start:
    li      s0, 1
loop:
    mv      a0, s0
    li      a7, 1
    ecall
    la      a0, eq
    li      a7, 4
    ecall
    mv      a0, s0
    jal     factorial
    mv      s1, a0
    li      a7, 1
    ecall
    la      a0, nl
    li      a7, 4
    ecall
    addi    s0, s0, 1
    li      t0, 10
    ble     s0, t0, loop

    andi    a0, s1, 0xff
    li      a7, 93
    ecall

# int factorial(int n) using shift-and-add multiplication (RV32I has no mul).
factorial:
    addi    sp, sp, -16
    sw      ra, 12(sp)
    sw      a0, 8(sp)
    li      t0, 1
    bgt     a0, t0, 1f
    li      a0, 1
    j       2f
1:
    addi    a0, a0, -1
    jal     factorial           # a0 = (n-1)!
    lw      a1, 8(sp)           # a1 = n
    jal     multiply
2:
    lw      ra, 12(sp)
    addi    sp, sp, 16
    ret

# a0 = a0 * a1 (unsigned, low XLEN bits)
multiply:
    li      t1, 0
3:
    beqz    a1, 4f
    andi    t2, a1, 1
    beqz    t2, 5f
    add     t1, t1, a0
5:
    slli    a0, a0, 1
    srli    a1, a1, 1
    j       3b
4:
    mv      a0, t1
    ret
