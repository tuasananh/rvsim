# RV64-only: sums 64-bit values with ld/sd and *W arithmetic.
    .data
values: .dword 0x100000000, 0x200000000, -1
result: .dword 0

    .text
main:
    la      t0, values
    ld      a0, 0(t0)
    ld      a1, 8(t0)
    ld      a2, 16(t0)
    add     a0, a0, a1
    add     a0, a0, a2          # 0x2ffffffff
    la      t1, result
    sd      a0, 0(t1)
    li      a7, 34              # PrintHex
    ecall
    li      a0, '\n'
    li      a7, 11
    ecall
    li      t2, 0x7fffffff
    addiw   a0, t2, 1           # wraps to INT32_MIN
    li      a7, 1
    ecall
    li      a0, 0
    li      a7, 93
    ecall
