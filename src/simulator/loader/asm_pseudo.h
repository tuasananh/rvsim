#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simulator/core/instruction.h"
#include "simulator/core/types.h"
#include "simulator/loader/asm_lexer.h"

namespace rvsim {

enum class RelocKind : uint8_t { kNone, kHi, kLo, kPcrelHi, kPcrelLo };

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

// Assembler expression tree.
struct Expr {
  enum class Kind : uint8_t {
    kNumber,
    kSymbol,
    kLocalLabel,  // 1f / 1b
    kDot,         // current location "."
    kUnary,       // op lhs
    kBinary,      // lhs op rhs
    kReloc,       // %hi(lhs), %lo(lhs), ...
  };

  Kind kind = Kind::kNumber;
  int64_t number = 0;
  std::string name{};
  bool forward = false;
  TokenKind op = TokenKind::kPlus;
  RelocKind reloc = RelocKind::kNone;
  ExprPtr lhs{};
  ExprPtr rhs{};
  int column = 0;

  // True if the expression references labels, local labels, or ".".
  bool HasSymbols() const;
};

ExprPtr MakeNumber(int64_t value, int column = 0);

// One parsed instruction operand.
struct Operand {
  enum class Kind : uint8_t {
    kRegister,    // a0
    kExpression,  // 42, label+4, %hi(sym)
    kMemory,      // offset(reg) or (reg); `expr` may be null for "(reg)"
  };

  Kind kind = Kind::kExpression;
  uint8_t reg = 0;
  ExprPtr expr{};
  int column = 0;
};

// How the immediate of a pending instruction is computed in pass 2.
enum class ImmMode : uint8_t {
  kConstant,     // `imm` is final.
  kValue,        // eval(expr)
  kUpperValue,   // eval(expr) is a raw 20-bit lui/auipc field
  kPcRel,        // eval(expr) - pc
  kHi,           // %hi(eval(expr))
  kLo,           // %lo(eval(expr))
  kPcrelHi,      // %pcrel_hi(eval(expr)) relative to this pc
  kPcrelLo,      // %pcrel_lo(label): label marks the matching auipc
  kPcrelLoPrev,  // %pcrel_lo relative to the auipc 4 bytes earlier
};

// A real machine instruction whose immediate may still be symbolic.
struct PendingInst {
  Opcode op = Opcode::kInvalid;
  uint8_t rd = 0;
  uint8_t rs1 = 0;
  uint8_t rs2 = 0;
  ImmMode mode = ImmMode::kConstant;
  int64_t imm = 0;
  ExprPtr expr{};
  int column = 0;
};

struct AsmError {
  std::string message;
  int column = 0;
};

struct ExpandContext {
  Xlen xlen = Xlen::k32;
  int mnemonic_column = 0;
  // Evaluates an expression that must be an absolute constant during pass 1
  // (used by `li`). Returns std::nullopt if it cannot be resolved yet.
  std::function<std::optional<int64_t>(const Expr&)> eval_constant;
};

// Expands a real or pseudo instruction into one or more machine instructions.
// The number of emitted instructions is fixed at this point (pass 1).
std::expected<std::vector<PendingInst>, AsmError> ExpandInstruction(
    std::string_view mnemonic, std::span<const Operand> operands,
    const ExpandContext& context);

// Builds the `li` sequence for a constant exactly as GNU as does.
std::vector<PendingInst> ExpandLoadImmediate(uint8_t rd, int64_t value,
                                             Xlen xlen);

}  // namespace rvsim
