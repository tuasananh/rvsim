#include "simulator/loader/asm_lexer.h"

#include <format>
#include <optional>

namespace rvsim {
namespace {

constexpr bool IsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         c == '.' || c == '$';
}

constexpr bool IsIdentChar(char c) {
  return IsIdentStart(c) || (c >= '0' && c <= '9');
}

constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }

constexpr int DigitValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return 99;
}

class Lexer {
 public:
  Lexer(std::string_view source, std::vector<Diagnostic>& diagnostics)
      : src_(source), diagnostics_(diagnostics) {}

  std::vector<Token> Run() {
    while (pos_ < src_.size()) {
      const char c = src_[pos_];
      if (c == '\n') {
        Emit(TokenKind::kNewline, 1);
        ++line_;
        line_start_ = pos_;
        continue;
      }
      if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
        ++pos_;
        continue;
      }
      if (c == '#' || (c == '/' && Peek(1) == '/')) {
        while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
        continue;
      }
      if (c == '/' && Peek(1) == '*') {
        SkipBlockComment();
        continue;
      }
      if (IsIdentStart(c)) {
        LexIdentifier();
        continue;
      }
      if (IsDigit(c)) {
        LexNumber();
        continue;
      }
      if (c == '\'') {
        LexChar();
        continue;
      }
      if (c == '"') {
        LexString();
        continue;
      }
      if (!LexPunct(c)) {
        Error(std::format("unexpected character '{}'", c));
        ++pos_;
      }
    }
    Token end;
    end.kind = TokenKind::kEnd;
    end.line = line_;
    end.column = Column();
    tokens_.push_back(end);
    return std::move(tokens_);
  }

 private:
  char Peek(size_t ahead) const {
    return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
  }
  int Column() const { return static_cast<int>(pos_ - line_start_) + 1; }

  void Error(std::string message) {
    diagnostics_.push_back(
        Diagnostic{Severity::kError, line_, Column(), std::move(message)});
  }

  Token& Emit(TokenKind kind, size_t length) {
    Token token;
    token.kind = kind;
    token.text = src_.substr(pos_, length);
    token.line = line_;
    token.column = Column();
    pos_ += length;
    tokens_.push_back(std::move(token));
    return tokens_.back();
  }

  void SkipBlockComment() {
    const int start_line = line_;
    const int start_col = Column();
    pos_ += 2;
    while (pos_ < src_.size()) {
      if (src_[pos_] == '*' && Peek(1) == '/') {
        pos_ += 2;
        return;
      }
      if (src_[pos_] == '\n') {
        // Like GNU as, a newline inside a block comment ends the statement.
        Emit(TokenKind::kNewline, 1);
        ++line_;
        line_start_ = pos_;
        continue;
      }
      ++pos_;
    }
    diagnostics_.push_back(Diagnostic{Severity::kError, start_line, start_col,
                                      "unterminated block comment"});
  }

  void LexIdentifier() {
    size_t end = pos_;
    while (end < src_.size() && IsIdentChar(src_[end])) ++end;
    Emit(TokenKind::kIdentifier, end - pos_);
  }

  void LexNumber() {
    const size_t start = pos_;
    // Local label reference: digits followed by 'f'/'b' and a non-identifier.
    size_t end = pos_;
    while (end < src_.size() && IsDigit(src_[end])) ++end;
    const bool binary_prefix = src_[start] == '0' && end == start + 1 &&
                               end < src_.size() &&
                               (src_[end] == 'b' || src_[end] == 'B') &&
                               (Peek(2) == '0' || Peek(2) == '1');
    if (!binary_prefix && end < src_.size() &&
        (src_[end] == 'f' || src_[end] == 'b') &&
        (end + 1 >= src_.size() || !IsIdentChar(src_[end + 1]))) {
      uint64_t number = 0;
      for (size_t i = start; i < end; ++i) {
        number = number * 10 + static_cast<uint64_t>(src_[i] - '0');
      }
      Token& token = Emit(TokenKind::kLocalLabelRef, end + 1 - start);
      token.value = static_cast<int64_t>(number);
      token.forward = src_[end] == 'f';
      return;
    }

    int base = 10;
    size_t digits = start;
    if (src_[start] == '0' && start + 1 < src_.size()) {
      const char p = src_[start + 1];
      if (p == 'x' || p == 'X') {
        base = 16;
        digits = start + 2;
      } else if (p == 'b' || p == 'B') {
        base = 2;
        digits = start + 2;
      } else if (p == 'o' || p == 'O') {
        base = 8;
        digits = start + 2;
      } else if (IsDigit(p)) {
        base = 8;
        digits = start + 1;
      }
    }
    end = digits;
    while (end < src_.size() && IsIdentChar(src_[end])) ++end;

    uint64_t value = 0;
    bool overflow = false;
    bool valid = end > digits;
    for (size_t i = digits; i < end && valid; ++i) {
      const int d = DigitValue(src_[i]);
      if (d >= base) {
        valid = false;
        break;
      }
      const uint64_t next =
          value * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
      if (next / static_cast<uint64_t>(base) != value) overflow = true;
      value = next;
    }
    const int column = Column();
    Token& token = Emit(TokenKind::kInteger, end - start);
    token.value = static_cast<int64_t>(value);
    if (!valid) {
      diagnostics_.push_back(
          Diagnostic{Severity::kError, line_, column,
                     std::format("invalid number '{}'", token.text)});
    } else if (overflow) {
      diagnostics_.push_back(Diagnostic{
          Severity::kError, line_, column,
          std::format("number '{}' does not fit in 64 bits", token.text)});
    }
  }

  // Parses one (possibly escaped) character at pos_; advances pos_.
  std::optional<uint8_t> LexCharBody() {
    if (pos_ >= src_.size() || src_[pos_] == '\n') {
      return std::nullopt;
    }
    char c = src_[pos_++];
    if (c != '\\') {
      return static_cast<uint8_t>(c);
    }
    if (pos_ >= src_.size()) {
      return std::nullopt;
    }
    c = src_[pos_++];
    switch (c) {
      case 'n':
        return '\n';
      case 't':
        return '\t';
      case 'r':
        return '\r';
      case 'b':
        return '\b';
      case 'f':
        return '\f';
      case 'v':
        return '\v';
      case 'a':
        return '\a';
      case 'e':
        return 0x1B;
      case '\\':
        return '\\';
      case '\'':
        return '\'';
      case '"':
        return '"';
      case 'x':
      case 'X': {
        unsigned value = 0;
        int count = 0;
        while (pos_ < src_.size() && DigitValue(src_[pos_]) < 16) {
          value = (value << 4) | static_cast<unsigned>(DigitValue(src_[pos_]));
          ++pos_;
          ++count;
        }
        if (count == 0) {
          Error("\\x used with no following hex digits");
        }
        return static_cast<uint8_t>(value);
      }
      default:
        if (c >= '0' && c <= '7') {
          unsigned value = static_cast<unsigned>(c - '0');
          for (int i = 0; i < 2 && pos_ < src_.size() && src_[pos_] >= '0' &&
                          src_[pos_] <= '7';
               ++i) {
            value = (value << 3) | static_cast<unsigned>(src_[pos_++] - '0');
          }
          return static_cast<uint8_t>(value);
        }
        return static_cast<uint8_t>(c);
    }
  }

  void LexChar() {
    const size_t start = pos_;
    const int column = Column();
    ++pos_;
    auto ch = LexCharBody();
    if (!ch || pos_ >= src_.size() || src_[pos_] != '\'') {
      diagnostics_.push_back(Diagnostic{Severity::kError, line_, column,
                                        "unterminated character literal"});
      while (pos_ < src_.size() && src_[pos_] != '\n' && src_[pos_] != '\'')
        ++pos_;
      if (pos_ < src_.size() && src_[pos_] == '\'') ++pos_;
      return;
    }
    ++pos_;
    Token token;
    token.kind = TokenKind::kInteger;
    token.text = src_.substr(start, pos_ - start);
    token.value = *ch;
    token.line = line_;
    token.column = column;
    tokens_.push_back(std::move(token));
  }

  void LexString() {
    const size_t start = pos_;
    const int column = Column();
    ++pos_;
    std::string value;
    while (pos_ < src_.size() && src_[pos_] != '"' && src_[pos_] != '\n') {
      auto ch = LexCharBody();
      if (!ch) break;
      value.push_back(static_cast<char>(*ch));
    }
    if (pos_ >= src_.size() || src_[pos_] != '"') {
      diagnostics_.push_back(Diagnostic{Severity::kError, line_, column,
                                        "unterminated string literal"});
    } else {
      ++pos_;
    }
    Token token;
    token.kind = TokenKind::kString;
    token.text = src_.substr(start, pos_ - start);
    token.str = std::move(value);
    token.line = line_;
    token.column = column;
    tokens_.push_back(std::move(token));
  }

  bool LexPunct(char c) {
    switch (c) {
      case ',':
        Emit(TokenKind::kComma, 1);
        return true;
      case '(':
        Emit(TokenKind::kLParen, 1);
        return true;
      case ')':
        Emit(TokenKind::kRParen, 1);
        return true;
      case ':':
        Emit(TokenKind::kColon, 1);
        return true;
      case '=':
        Emit(TokenKind::kEquals, 1);
        return true;
      case '+':
        Emit(TokenKind::kPlus, 1);
        return true;
      case '-':
        Emit(TokenKind::kMinus, 1);
        return true;
      case '*':
        Emit(TokenKind::kStar, 1);
        return true;
      case '/':
        Emit(TokenKind::kSlash, 1);
        return true;
      case '%':
        Emit(TokenKind::kPercent, 1);
        return true;
      case '&':
        Emit(TokenKind::kAmp, 1);
        return true;
      case '|':
        Emit(TokenKind::kPipe, 1);
        return true;
      case '^':
        Emit(TokenKind::kCaret, 1);
        return true;
      case '~':
        Emit(TokenKind::kTilde, 1);
        return true;
      case ';':
        Emit(TokenKind::kNewline, 1);
        return true;
      case '<':
        if (Peek(1) == '<') {
          Emit(TokenKind::kShl, 2);
          return true;
        }
        return false;
      case '>':
        if (Peek(1) == '>') {
          Emit(TokenKind::kShr, 2);
          return true;
        }
        return false;
      default:
        return false;
    }
  }

  std::string_view src_;
  std::vector<Diagnostic>& diagnostics_;
  std::vector<Token> tokens_;
  size_t pos_ = 0;
  size_t line_start_ = 0;
  int line_ = 1;
};

}  // namespace

std::vector<Token> Tokenize(std::string_view source,
                            std::vector<Diagnostic>& diagnostics) {
  return Lexer(source, diagnostics).Run();
}

}  // namespace rvsim
