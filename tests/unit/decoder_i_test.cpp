#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

#include "simulator/core/decoder.h"
#include "simulator/core/disassembler.h"
#include "simulator/core/encoder.h"

namespace rvsim {
namespace {

struct GoldenVector {
  uint64_t pc;
  uint32_t raw;
  std::string_view disasm;
};

// Golden encodings produced by GNU as (binutils 2.45, Compiler Explorer
// gnuasriscv32g1510 / gnuasriscv64g1510, -march=rv32i / -march=rv64i).
constexpr GoldenVector kRv32Vectors[] = {
    {0x0, 0x12345537, "lui a0, 0x12345"},
    {0x4, 0xfffff297, "auipc t0, 0xfffff"},
    {0x8, 0xff9ff0ef, "jal ra, 0x0"},
    {0xc, 0xffc300e7, "jalr ra, -4(t1)"},
    {0x10, 0xfeb508e3, "beq a0, a1, 0x0"},
    {0x14, 0xfe9416e3, "bne s0, s1, 0x0"},
    {0x18, 0x0862c863, "blt t0, t1, 0xa8"},
    {0x1c, 0x08d65663, "bge a2, a3, 0xa8"},
    {0x20, 0x08f76463, "bltu a4, a5, 0xa8"},
    {0x24, 0x09397263, "bgeu s2, s3, 0xa8"},
    {0x28, 0xfff10503, "lb a0, -1(sp)"},
    {0x2c, 0x00211583, "lh a1, 2(sp)"},
    {0x30, 0x7ff12603, "lw a2, 2047(sp)"},
    {0x34, 0x8001c683, "lbu a3, -2048(gp)"},
    {0x38, 0x0002d703, "lhu a4, 0(t0)"},
    {0x3c, 0xfea10fa3, "sb a0, -1(sp)"},
    {0x40, 0x00b41323, "sh a1, 6(s0)"},
    {0x44, 0x00112623, "sw ra, 12(sp)"},
    {0x48, 0xff010113, "addi sp, sp, -16"},
    {0x4c, 0xffb32293, "slti t0, t1, -5"},
    {0x50, 0x00533293, "sltiu t0, t1, 5"},
    {0x54, 0xfff54513, "xori a0, a0, -1"},
    {0x58, 0x7ff56513, "ori a0, a0, 2047"},
    {0x5c, 0x0ff57513, "andi a0, a0, 255"},
    {0x60, 0x01f51513, "slli a0, a0, 31"},
    {0x64, 0x0015d593, "srli a1, a1, 1"},
    {0x68, 0x40765613, "srai a2, a2, 7"},
    {0x6c, 0x00c58533, "add a0, a1, a2"},
    {0x70, 0x41248433, "sub s0, s1, s2"},
    {0x74, 0x01ee9e33, "sll t3, t4, t5"},
    {0x78, 0x00b52fb3, "slt t6, a0, a1"},
    {0x7c, 0x00b03533, "sltu a0, zero, a1"},
    {0x80, 0x00f746b3, "xor a3, a4, a5"},
    {0x84, 0x0088d833, "srl a6, a7, s0"},
    {0x88, 0x413954b3, "sra s1, s2, s3"},
    {0x8c, 0x016aea33, "or s4, s5, s6"},
    {0x90, 0x019c7bb3, "and s7, s8, s9"},
    {0x94, 0x0ff0000f, "fence iorw, iorw"},
    {0x98, 0x0210000f, "fence r, w"},
    {0x9c, 0x8330000f, "fence.tso"},
    {0xa0, 0x00000073, "ecall"},
    {0xa4, 0x00100073, "ebreak"},
    {0xa8, 0x0000006f, "jal zero, 0xa8"},
};

constexpr GoldenVector kRv64Vectors[] = {
    {0x0, 0x00416503, "lwu a0, 4(sp)"},
    {0x4, 0xff813083, "ld ra, -8(sp)"},
    {0x8, 0x7e813c23, "sd s0, 2040(sp)"},
    {0xc, 0xfff5851b, "addiw a0, a1, -1"},
    {0x10, 0x01f5151b, "slliw a0, a0, 31"},
    {0x14, 0x0035d59b, "srliw a1, a1, 3"},
    {0x18, 0x4016561b, "sraiw a2, a2, 1"},
    {0x1c, 0x00c5853b, "addw a0, a1, a2"},
    {0x20, 0x40f706bb, "subw a3, a4, a5"},
    {0x24, 0x007312bb, "sllw t0, t1, t2"},
    {0x28, 0x01eede3b, "srlw t3, t4, t5"},
    {0x2c, 0x413954bb, "sraw s1, s2, s3"},
    {0x30, 0x03f51513, "slli a0, a0, 63"},
    {0x34, 0x0205d593, "srli a1, a1, 32"},
    {0x38, 0x42165613, "srai a2, a2, 33"},
};

void CheckVector(const GoldenVector& v, Xlen xlen) {
  SCOPED_TRACE(v.disasm);
  const DecodedInst inst = Decode(v.raw, xlen);
  ASSERT_TRUE(inst.valid());
  EXPECT_EQ(Disassemble(inst, v.pc), v.disasm);
  auto encoded = Encode(inst, xlen);
  ASSERT_TRUE(encoded) << encoded.error();
  EXPECT_EQ(*encoded, v.raw);
}

TEST(DecoderTest, Rv32GoldenVectors) {
  for (const GoldenVector& v : kRv32Vectors) {
    CheckVector(v, Xlen::k32);
  }
}

TEST(DecoderTest, Rv32VectorsAlsoDecodeOnRv64) {
  for (const GoldenVector& v : kRv32Vectors) {
    CheckVector(v, Xlen::k64);
  }
}

TEST(DecoderTest, Rv64GoldenVectors) {
  for (const GoldenVector& v : kRv64Vectors) {
    CheckVector(v, Xlen::k64);
  }
}

TEST(DecoderTest, Rv64OnlyInstructionsInvalidOnRv32) {
  for (const GoldenVector& v : kRv64Vectors) {
    SCOPED_TRACE(v.disasm);
    // *W, LD, SD, LWU and shift amounts >= 32 are illegal on RV32.
    EXPECT_FALSE(Decode(v.raw, Xlen::k32).valid());
  }
}

TEST(DecoderTest, ImmediateExtraction) {
  // beq zero, zero, -4096 (most negative branch offset)
  EXPECT_EQ(Decode(0x80000063, Xlen::k32).imm, -4096);
  // jal zero, +1048574 (most positive jump offset)
  EXPECT_EQ(Decode(0x7FFFF06F, Xlen::k32).imm, 1048574);
  // jal zero, -1048576
  EXPECT_EQ(Decode(0x8000006F, Xlen::k32).imm, -1048576);
  // lui a0, 0x80000 sign-extends.
  EXPECT_EQ(Decode(0x80000537, Xlen::k64).imm, -2147483648LL);
}

TEST(DecoderTest, IllegalEncodings) {
  EXPECT_FALSE(Decode(0x00000000, Xlen::k32).valid());
  EXPECT_FALSE(Decode(0xFFFFFFFF, Xlen::k32).valid());
  // Reserved funct7 on ADD.
  EXPECT_FALSE(Decode(0x02B50533 | 0x10000000, Xlen::k32).valid());
  // JALR with funct3 != 0.
  EXPECT_FALSE(Decode(0x000010E7, Xlen::k32).valid());
  // Compressed encodings are not supported until Checkpoint 6.
  EXPECT_FALSE(Decode(0x00004501, Xlen::k32).valid());
  // CSR instructions are not supported until Checkpoint 5.
  EXPECT_FALSE(Decode(0xC0002573, Xlen::k32).valid());
}

TEST(DisassemblerTest, NumericRegisterNames) {
  const DecodedInst inst = Decode(0xFF010113, Xlen::k32);
  EXPECT_EQ(Disassemble(inst, 0, {.abi_names = false}), "addi x2, x2, -16");
}

TEST(DisassemblerTest, InvalidPrintsWord) {
  EXPECT_EQ(Disassemble(Decode(0, Xlen::k32), 0), ".word 0x00000000");
}

TEST(EncoderTest, RejectsOutOfRangeImmediates) {
  EXPECT_FALSE(Encode(Opcode::kAddi, 1, 1, 0, 2048, Xlen::k32));
  EXPECT_FALSE(Encode(Opcode::kAddi, 1, 1, 0, -2049, Xlen::k32));
  EXPECT_FALSE(Encode(Opcode::kBeq, 0, 1, 2, 4096, Xlen::k32));
  EXPECT_FALSE(Encode(Opcode::kBeq, 0, 1, 2, 3, Xlen::k32));
  EXPECT_FALSE(Encode(Opcode::kJal, 1, 0, 0, 1 << 20, Xlen::k32));
  EXPECT_FALSE(Encode(Opcode::kSlli, 1, 1, 0, 32, Xlen::k32));
  EXPECT_TRUE(Encode(Opcode::kSlli, 1, 1, 0, 63, Xlen::k64));
  EXPECT_FALSE(Encode(Opcode::kSlliw, 1, 1, 0, 32, Xlen::k64));
  EXPECT_FALSE(Encode(Opcode::kLui, 1, 0, 0, 0x123, Xlen::k32));
  EXPECT_FALSE(Encode(Opcode::kLd, 1, 2, 0, 0, Xlen::k32));
}

}  // namespace
}  // namespace rvsim
