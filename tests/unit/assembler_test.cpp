#include "simulator/loader/assembler.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "simulator/core/decoder.h"
#include "simulator/core/disassembler.h"

namespace rvsim {
namespace {

AssemblyResult AssembleAt0(std::string_view source, Xlen xlen = Xlen::k32) {
  return Assemble(source, AssemblerOptions{.xlen = xlen, .text_base = 0});
}

std::string DumpDiagnostics(const AssemblyResult& result) {
  std::string out;
  for (const Diagnostic& d : result.diagnostics) {
    out += std::format("{}:{}: {}\n", d.line, d.column, d.message);
  }
  return out;
}

std::vector<uint32_t> TextWords(const ProgramImage& image) {
  const Segment* text = image.FindSegment(".text");
  std::vector<uint32_t> words;
  if (text == nullptr) return words;
  for (size_t i = 0; i + 4 <= text->data.size(); i += 4) {
    uint32_t w = 0;
    std::memcpy(&w, text->data.data() + i, 4);
    words.push_back(w);
  }
  return words;
}

std::vector<std::string> TextDisasm(const ProgramImage& image) {
  std::vector<std::string> out;
  const Segment* text = image.FindSegment(".text");
  uint64_t pc = text->address;
  for (uint32_t w : TextWords(image)) {
    out.push_back(Disassemble(Decode(w, image.xlen), pc));
    pc += 4;
  }
  return out;
}

// Source assembled by GNU as 2.45 (-march=rv32i, .option norelax) on
// Compiler Explorer. Branch/jump words are taken verbatim from the listing;
// PC-relative auipc pairs (la, call, tail, %pcrel_*, lw/sw symbol) are left
// as relocations by gas, so their low halves hold the linker-resolved offset.
constexpr std::string_view kPseudoSource = R"(
.text
_start:
  nop
  li a0, 5
  li a0, -2048
  li a0, 2048
  li a0, 0x12345678
  li a0, 0xFFFFFFFF
  li a0, 0x80000000
  li t0, 0x7ffff800
  la a1, target
  lla a2, target
  mv a3, a4
  not a5, a6
  neg s0, s1
  seqz t0, t1
  snez t0, t1
  sltz t0, t1
  sgtz t0, t1
  beqz a0, target
  bnez a0, target
  blez a0, target
  bgez a0, target
  bltz a0, target
  bgtz a0, target
  bgt a0, a1, target
  ble a0, a1, target
  bgtu a0, a1, target
  bleu a0, a1, target
  j target
  jal target
  jr t0
  jalr t0
  ret
  call target
  tail target
1:
  auipc a0, %pcrel_hi(target)
  addi a0, a0, %pcrel_lo(1b)
  lui a0, %hi(0x12345fff)
  addi a0, a0, %lo(0x12345fff)
  lw a0, target
  sw a0, target, t1
  jalr a0, 4(a1)
  jalr a0, a1, -4
  add a0, a1, 10
  and a0, a1, 0xff
  sll a0, a1, 3
  fence rw, rw
  beq a0, a1, 1b
  j 1f
  addi a0, a0, 1
1:
target:
  ebreak
)";

constexpr uint32_t kPseudoGolden[] = {
    0x00000013, 0x00500513, 0x80000513, 0x00001537, 0x80050513, 0x12345537,
    0x67850513, 0xfff00513, 0x80000537, 0x800002b7, 0x80028293, 0x00000597,
    0x0bc58593, 0x00000617, 0x0b460613, 0x00070693, 0xfff84793, 0x40900433,
    0x00133293, 0x006032b3, 0x000322b3, 0x006022b3, 0x08050863, 0x08051663,
    0x08a05463, 0x08055263, 0x08054063, 0x06a04e63, 0x06a5cc63, 0x06a5da63,
    0x06a5e863, 0x06a5f663, 0x0680006f, 0x064000ef, 0x00028067, 0x000280e7,
    0x00008067, 0x00000097, 0x054080e7, 0x00000317, 0x04c30067, 0x00000517,
    0x04450513, 0x12346537, 0xfff50513, 0x00000517, 0x03452503, 0x00000317,
    0x02a32623, 0x00458567, 0xffc58567, 0x00a58513, 0x0ff5f513, 0x00359513,
    0x0330000f, 0xfcb504e3, 0x0080006f, 0x00150513, 0x00100073};

TEST(AssemblerTest, Rv32PseudoInstructionsMatchGnuAs) {
  const AssemblyResult result = AssembleAt0(kPseudoSource);
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  const std::vector<uint32_t> words = TextWords(*result.image);
  ASSERT_EQ(words.size(), std::size(kPseudoGolden));
  for (size_t i = 0; i < words.size(); ++i) {
    EXPECT_EQ(words[i], kPseudoGolden[i])
        << std::format("word {} at {:#x}: {}", i, i * 4,
                       Disassemble(Decode(kPseudoGolden[i], Xlen::k32), i * 4));
  }
}

TEST(AssemblerTest, Rv64LoadImmediateMatchesGnuAs) {
  const AssemblyResult result = AssembleAt0(R"(
    li a0, 5
    li a0, 0x12345678
    li a0, 0x80000000
    li a0, 0x123456789abcdef0
    li a0, -1
    li a0, 0xfffff000
    li a0, 0x7ffff800
    li a0, 4096
  )",
                                            Xlen::k64);
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  const std::vector<std::string> expected = {
      "addi a0, zero, 5",    "lui a0, 0x12345",   "addiw a0, a0, 1656",
      "addiw a0, zero, 1",   "slli a0, a0, 31",   "lui a0, 0x247",
      "addiw a0, a0, -1875", "slli a0, a0, 14",   "addi a0, a0, -947",
      "slli a0, a0, 12",     "addi a0, a0, 1511", "slli a0, a0, 13",
      "addi a0, a0, -272",   "addi a0, zero, -1", "lui a0, 0x100",
      "addiw a0, a0, -1",    "slli a0, a0, 12",   "lui a0, 0x80000",
      "addiw a0, a0, -2048", "lui a0, 0x1",
  };
  EXPECT_EQ(TextDisasm(*result.image), expected);
}

TEST(AssemblerTest, Rv64OnlyInstructionsRejectedOnRv32) {
  const AssemblyResult result = AssembleAt0("ld a0, 0(sp)\naddw a0, a0, a1\n");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error_count(), 2u);
}

TEST(AssemblerTest, DataDirectivesAndLayout) {
  const AssemblyResult result = Assemble(R"(
    .data
  bytes: .byte 1, -1, 'A'
  halfs: .half 0x1234
    .align 2
  words: .word 0xdeadbeef, words
  dw:    .dword 0x1122334455667788
  str:   .asciz "hi\n"
  raw:   .ascii "ab", "c"
    .zero 3
    .balign 8
  after: .byte 9
    .rodata
  ro:    .string "ro"
    .bss
  buf:   .space 100
    .text
  main:  ret
  )");
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  const ProgramImage& image = *result.image;
  const Segment* data = image.FindSegment(".data");
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->address, 0x10000000u);
  const std::vector<uint8_t> expected = {
      1,    0xFF, 'A',  0x34, 0x12, 0,    0,    0,     // bytes, halfs, pad
      0xEF, 0xBE, 0xAD, 0xDE, 0x08, 0x00, 0x00, 0x10,  // words
      0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,  // dw
      'h',  'i',  '\n', 0,    'a',  'b',  'c',  0,
      0,    0,    0,    0,         // str raw zero pad
      0,    0,    0,    0,    9};  // balign 8, after
  EXPECT_EQ(data->data, expected);
  EXPECT_EQ(*image.LookupSymbol("after"), 0x10000000u + 40);
  const Segment* ro = image.FindSegment(".rodata");
  ASSERT_NE(ro, nullptr);
  EXPECT_EQ(ro->address, 0x10000000u + 48);
  const Segment* bss = image.FindSegment(".bss");
  ASSERT_NE(bss, nullptr);
  EXPECT_EQ(bss->mem_size, 100u);
  EXPECT_TRUE(bss->data.empty());
  EXPECT_EQ(image.entry_point, 0x00400000u);
  EXPECT_EQ(*image.initial_gp, 0x10000800u);
}

TEST(AssemblerTest, EquExpressionsAndDot) {
  const AssemblyResult result = AssembleAt0(R"(
    .equ COUNT, 4 * (2 + 3) - 1
    SHIFTED = COUNT << 2
    .data
  msg: .ascii "hello"
    .equ LEN, . - msg
  end: .word LEN, SHIFTED, COUNT % 7, ~0 & 0xff, -8 >> 1, 1 | 6 ^ 3
    .text
    li a0, LEN
    li a1, COUNT
  )");
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  const Segment* data = result.image->FindSegment(".data");
  int32_t words[6];
  std::memcpy(words, data->data.data() + 5, sizeof(words));
  EXPECT_EQ(words[0], 5);
  EXPECT_EQ(words[1], 76);
  EXPECT_EQ(words[2], 5);
  EXPECT_EQ(words[3], 255);
  EXPECT_EQ(words[4], -4);
  EXPECT_EQ(words[5], 1 | (6 ^ 3));
  const std::vector<std::string> expected = {"addi a0, zero, 5",
                                             "addi a1, zero, 19"};
  EXPECT_EQ(TextDisasm(*result.image), expected);
}

TEST(AssemblerTest, LocalLabelsResolveNearestDefinition) {
  const AssemblyResult result = AssembleAt0(R"(
  1: j 1f
  1: j 1b
     j 2f
  2: j 1b
  )");
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  const std::vector<std::string> expected = {"jal zero, 0x4", "jal zero, 0x4",
                                             "jal zero, 0xc", "jal zero, 0x4"};
  EXPECT_EQ(TextDisasm(*result.image), expected);
}

TEST(AssemblerTest, NumericBranchTargetsAreOffsets) {
  const AssemblyResult result = AssembleAt0("nop\nbeq a0, a1, -4\nj 8\n");
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  const std::vector<std::string> expected = {
      "addi zero, zero, 0", "beq a0, a1, 0x0", "jal zero, 0x10"};
  EXPECT_EQ(TextDisasm(*result.image), expected);
}

TEST(AssemblerTest, CrossSectionLoadAddress) {
  const AssemblyResult result = Assemble(R"(
    .data
    .word 0
  value: .word 42
    .text
  main:
    la a0, value
    lw a1, value
    lui a2, %hi(value)
    lw a2, %lo(value)(a2)
  )");
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  // value = 0x10000004; pc = 0x00400000.
  const std::vector<std::string> expected = {
      "auipc a0, 0xfc00", "addi a0, a0, 4",  "auipc a1, 0xfc00",
      "lw a1, -4(a1)",    "lui a2, 0x10000", "lw a2, 4(a2)"};
  EXPECT_EQ(TextDisasm(*result.image), expected);
}

TEST(AssemblerTest, SourceMapAndSymbols) {
  const AssemblyResult result = Assemble(R"(.text
.globl main
helper:
  ret
main:
  li a0, 0x12345678
  call helper
)");
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  const ProgramImage& image = *result.image;
  EXPECT_EQ(image.entry_point, 0x00400004u);
  EXPECT_EQ(image.source_map.LineForAddress(0x00400000), 4);
  EXPECT_EQ(image.source_map.LineForAddress(0x00400008), 6);
  EXPECT_EQ(image.source_map.LineForAddress(0x0040000C), 7);
  EXPECT_EQ(image.source_map.AddressForLine(6), 0x00400004u);
  EXPECT_EQ(image.source_map.AddressesForLine(7).size(), 2u);
  ASSERT_NE(image.SymbolAt(0x00400004), nullptr);
  EXPECT_EQ(image.SymbolAt(0x00400004)->name, "main");
  EXPECT_TRUE(image.SymbolAt(0x00400004)->global);
  EXPECT_EQ(image.NearestSymbol(0x0040000C)->name, "main");
}

TEST(AssemblerTest, StartSymbolTakesPrecedenceOverMain) {
  const AssemblyResult result = Assemble("main: nop\n_start: nop\n");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.image->entry_point, 0x00400004u);
}

TEST(AssemblerTest, CollectsAllDiagnosticsWithPositions) {
  const AssemblyResult result = Assemble(R"(.text
  addi a0, a0, 5000
  frobnicate a0
  beq a0, a1, nowhere
  lw a0, 0(q9)
  .word 1
  li a0, later
  addi a0, a0
  .data
  add a0, a0, a0
  .byte 300
)");
  EXPECT_FALSE(result.ok());
  std::vector<int> lines;
  for (const Diagnostic& d : result.diagnostics) {
    if (d.severity == Severity::kError) lines.push_back(d.line);
  }
  // Lines 2 (immediate range), 4 (undefined symbol), and 11 (.byte range) are
  // pass-2 errors, reported once pass 1 succeeds; every pass-1 error is
  // collected.
  const std::vector<int> expected = {3, 5, 7, 8, 10};
  EXPECT_EQ(lines, expected) << DumpDiagnostics(result);
  EXPECT_EQ(result.diagnostics[0].column, 3);
  EXPECT_NE(result.diagnostics[0].message.find("frobnicate"),
            std::string::npos);
  EXPECT_NE(result.diagnostics[1].message.find("unknown register 'q9'"),
            std::string::npos);
}

TEST(AssemblerTest, Pass2DiagnosticsReported) {
  const AssemblyResult result = Assemble(R"(.text
  beq a0, a1, nowhere
  .data
  .byte 300
  .half -40000
)");
  EXPECT_FALSE(result.ok());
  ASSERT_EQ(result.error_count(), 3u) << DumpDiagnostics(result);
  EXPECT_EQ(result.diagnostics[0].line, 2);
  EXPECT_NE(result.diagnostics[0].message.find("nowhere"), std::string::npos);
  EXPECT_EQ(result.diagnostics[1].line, 4);
  EXPECT_EQ(result.diagnostics[2].line, 5);
}

TEST(AssemblerTest, ImmediateOutOfRange) {
  const AssemblyResult result = Assemble("nop\naddi a0, a0, 5000\n");
  ASSERT_EQ(result.error_count(), 1u);
  EXPECT_EQ(result.diagnostics[0].line, 2);
  EXPECT_NE(result.diagnostics[0].message.find("out of range"),
            std::string::npos);
}

TEST(AssemblerTest, BranchOutOfRange) {
  const AssemblyResult result = AssembleAt0(R"(
    beq a0, a1, far
    .zero 8192
  far: nop
  )");
  EXPECT_FALSE(result.ok());
  ASSERT_EQ(result.error_count(), 1u);
  EXPECT_EQ(result.diagnostics[0].line, 2);
}

TEST(AssemblerTest, DuplicateLabel) {
  const AssemblyResult result = Assemble("a: nop\na: nop\n");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.diagnostics[0].line, 2);
}

TEST(AssemblerTest, CommentsAndSeparators) {
  const AssemblyResult result = AssembleAt0(
      "# full line\n"
      "nop // trailing\n"
      "nop; nop /* block\n"
      " comment */ nop\n");
  ASSERT_TRUE(result.ok()) << DumpDiagnostics(result);
  EXPECT_EQ(TextWords(*result.image).size(), 4u);
}

TEST(AssemblerTest, LexicalErrors) {
  const AssemblyResult result = Assemble("li a0, 0x\n.ascii \"open\n");
  EXPECT_FALSE(result.ok());
  EXPECT_GE(result.error_count(), 2u);
}

TEST(AssemblerTest, EmptySourceProducesEmptyImage) {
  const AssemblyResult result = Assemble("  # nothing\n");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.image->segments.empty());
}

}  // namespace
}  // namespace rvsim
