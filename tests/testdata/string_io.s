# Reads a name and a number from stdin, then greets and echoes the number
# doubled. Exercises ReadString, ReadInt, PrintString, PrintInt, and Sbrk.
    .data
prompt: .asciz "name? "
hello:  .asciz "Hello, "
twice:  .asciz "twice: "

    .text
main:
    la      a0, prompt
    li      a7, 4
    ecall

    li      a0, 64              # buffer = sbrk(64)
    li      a7, 9
    ecall
    mv      s0, a0

    mv      a0, s0
    li      a1, 64
    li      a7, 8               # ReadString(buffer, 64)
    ecall

    la      a0, hello
    li      a7, 4
    ecall
    mv      a0, s0
    li      a7, 4
    ecall

    li      a7, 5               # ReadInt
    ecall
    mv      s1, a0
    la      a0, twice
    li      a7, 4
    ecall
    add     a0, s1, s1
    li      a7, 1
    ecall
    li      a0, '\n'
    li      a7, 11
    ecall

    li      a7, 10
    ecall
