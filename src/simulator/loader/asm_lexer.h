#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "simulator/loader/program_image.h"

namespace rvsim {

enum class TokenKind : uint8_t {
  kIdentifier,     // labels, mnemonics, directives (".text"), registers
  kInteger,        // numeric and character literals (value in `value`)
  kString,         // "..." (decoded bytes in `str`)
  kLocalLabelRef,  // 1f / 1b (number in `value`, direction in `forward`)
  kComma,
  kLParen,
  kRParen,
  kColon,
  kEquals,
  kPlus,
  kMinus,
  kStar,
  kSlash,
  kPercent,
  kShl,
  kShr,
  kAmp,
  kPipe,
  kCaret,
  kTilde,
  kNewline,  // end of statement ('\n' or ';')
  kEnd,
};

struct Token {
  TokenKind kind = TokenKind::kEnd;
  std::string_view text;  // Source spelling.
  int64_t value = 0;
  bool forward = false;
  std::string str{};
  int line = 1;    // 1-based
  int column = 1;  // 1-based
};

// Splits GNU as-style RISC-V assembly into tokens. Supports `#`, `//`, and
// `/* */` comments, `;` statement separators, decimal / 0x / 0b / 0o /
// leading-zero octal integers, character literals, and escaped strings.
// Lexical errors are appended to `diagnostics`; the offending character is
// skipped so lexing can continue. The token stream always ends with kEnd.
std::vector<Token> Tokenize(std::string_view source,
                            std::vector<Diagnostic>& diagnostics);

}  // namespace rvsim
