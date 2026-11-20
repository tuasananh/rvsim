#include "simulator/loader/assembler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "simulator/core/encoder.h"
#include "simulator/core/registers.h"
#include "simulator/loader/asm_lexer.h"
#include "simulator/loader/asm_pseudo.h"

namespace rvsim {

size_t AssemblyResult::error_count() const {
  return static_cast<size_t>(
      std::ranges::count(diagnostics, Severity::kError, &Diagnostic::severity));
}

namespace {

enum Section : int { kText = 0, kData = 1, kRodata = 2, kBss = 3 };
constexpr int kNumSections = 4;
constexpr int kAbsolute = -1;
constexpr std::array<std::string_view, kNumSections> kSectionNames = {
    ".text", ".data", ".rodata", ".bss"};
constexpr uint64_t kMaxSectionSize = uint64_t{64} << 20;  // 64 MiB
constexpr size_t kMaxDiagnostics = 500;
constexpr uint32_t kNopWord = 0x00000013;

// Value of an expression: absolute, or an offset into a section (pass 1).
struct Value {
  int64_t v = 0;
  int section = kAbsolute;
};

struct EvalError {
  bool unresolved = false;  // Forward reference; may succeed in pass 2.
  std::string message;
  int column = 0;
};

struct EvalContext {
  bool final = false;  // Pass 2: all labels have absolute addresses.
  int dot_section = kText;
  uint64_t dot_offset = 0;
  size_t seq = 0;  // Source statement sequence number (for 1f/1b).
};

struct SymbolDef {
  enum class Kind : uint8_t { kLabel, kEqu };
  Kind kind = Kind::kLabel;
  int section = kText;
  uint64_t offset = 0;
  ExprPtr expr{};
  EvalContext ctx{};
  int line = 0;
  mutable bool evaluating = false;
};

struct LocalDef {
  size_t seq;
  int section;
  uint64_t offset;
};

struct Statement {
  enum class Kind : uint8_t { kInstructions, kData, kBytes };
  Kind kind = Kind::kBytes;
  int line = 0;
  int column = 0;
  size_t seq = 0;
  int section = kText;
  uint64_t offset = 0;
  uint64_t size = 0;
  std::vector<PendingInst> insts{};
  int unit = 0;
  std::vector<ExprPtr> values{};
  std::vector<uint8_t> bytes{};
  uint64_t fill_count = 0;  // Zero/fill bytes appended after `bytes`.
  uint8_t fill_value = 0;
};

struct Span {
  size_t begin;
  size_t end;
  bool empty() const { return begin >= end; }
};

int64_t SignExtend12(int64_t v) { return ((v & 0xFFF) ^ 0x800) - 0x800; }

std::string Lowercase(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

class Assembler {
 public:
  Assembler(std::string_view source, const AssemblerOptions& options)
      : options_(options) {
    tokens_ = Tokenize(source, diagnostics_);
  }

  AssemblyResult Run() {
    Pass1();
    Layout();
    std::optional<ProgramImage> image;
    if (!HasErrors()) {
      image = Pass2();
    }
    AssemblyResult result;
    std::ranges::stable_sort(
        diagnostics_, [](const Diagnostic& a, const Diagnostic& b) {
          return std::tie(a.line, a.column) < std::tie(b.line, b.column);
        });
    if (diagnostics_.size() > kMaxDiagnostics) {
      diagnostics_.resize(kMaxDiagnostics);
    }
    result.diagnostics = std::move(diagnostics_);
    if (result.error_count() == 0) {
      result.image = std::move(image);
    }
    return result;
  }

 private:
  // ---------------------------------------------------------------- helpers
  bool HasErrors() const {
    return std::ranges::any_of(diagnostics_, [](const Diagnostic& d) {
      return d.severity == Severity::kError;
    });
  }

  void Error(int line, int column, std::string message) {
    diagnostics_.push_back(
        Diagnostic{Severity::kError, line, column, std::move(message)});
  }

  void Warning(int line, int column, std::string message) {
    diagnostics_.push_back(
        Diagnostic{Severity::kWarning, line, column, std::move(message)});
  }

  const Token& Tok(size_t index) const {
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
  }

  uint64_t& Offset() { return section_size_[current_section_]; }

  // ------------------------------------------------------ expression parser
  class ExprParser {
   public:
    ExprParser(const std::vector<Token>& tokens, Span span)
        : tokens_(tokens), pos_(span.begin), end_(span.end) {}

    std::expected<ExprPtr, AsmError> ParseAll() {
      if (pos_ >= end_) {
        return std::unexpected(AsmError{"expected an expression", Column()});
      }
      auto expr = ParseBinary(0);
      if (!expr) return expr;
      if (pos_ < end_) {
        return std::unexpected(AsmError{
            std::format("unexpected '{}' in expression", tokens_[pos_].text),
            Column()});
      }
      return expr;
    }

   private:
    int Column() const {
      const size_t index = std::min(pos_, end_ > 0 ? end_ - 1 : 0);
      return index < tokens_.size() ? tokens_[index].column : 0;
    }

    static int Precedence(TokenKind kind) {
      switch (kind) {
        case TokenKind::kPipe:
          return 1;
        case TokenKind::kCaret:
          return 2;
        case TokenKind::kAmp:
          return 3;
        case TokenKind::kShl:
        case TokenKind::kShr:
          return 4;
        case TokenKind::kPlus:
        case TokenKind::kMinus:
          return 5;
        case TokenKind::kStar:
        case TokenKind::kSlash:
        case TokenKind::kPercent:
          return 6;
        default:
          return -1;
      }
    }

    std::expected<ExprPtr, AsmError> ParseBinary(int min_prec) {
      auto lhs = ParseUnary();
      if (!lhs) return lhs;
      while (pos_ < end_) {
        const Token& op = tokens_[pos_];
        const int prec = Precedence(op.kind);
        if (prec < 0 || prec < min_prec) break;
        ++pos_;
        auto rhs = ParseBinary(prec + 1);
        if (!rhs) return rhs;
        auto node = std::make_shared<Expr>();
        node->kind = Expr::Kind::kBinary;
        node->op = op.kind;
        node->lhs = *lhs;
        node->rhs = *rhs;
        node->column = op.column;
        lhs = node;
      }
      return lhs;
    }

    std::expected<ExprPtr, AsmError> ParseUnary() {
      if (pos_ >= end_) {
        return std::unexpected(AsmError{"expected an operand", Column()});
      }
      const Token& tok = tokens_[pos_];
      if (tok.kind == TokenKind::kMinus || tok.kind == TokenKind::kTilde ||
          tok.kind == TokenKind::kPlus) {
        ++pos_;
        auto operand = ParseUnary();
        if (!operand) return operand;
        if (tok.kind == TokenKind::kPlus) return operand;
        auto node = std::make_shared<Expr>();
        node->kind = Expr::Kind::kUnary;
        node->op = tok.kind;
        node->lhs = *operand;
        node->column = tok.column;
        return node;
      }
      return ParsePrimary();
    }

    std::expected<ExprPtr, AsmError> ParsePrimary() {
      const Token& tok = tokens_[pos_];
      auto node = std::make_shared<Expr>();
      node->column = tok.column;
      switch (tok.kind) {
        case TokenKind::kInteger:
          ++pos_;
          node->kind = Expr::Kind::kNumber;
          node->number = tok.value;
          return node;
        case TokenKind::kLocalLabelRef:
          ++pos_;
          node->kind = Expr::Kind::kLocalLabel;
          node->number = tok.value;
          node->forward = tok.forward;
          return node;
        case TokenKind::kIdentifier:
          ++pos_;
          if (tok.text == ".") {
            node->kind = Expr::Kind::kDot;
          } else {
            node->kind = Expr::Kind::kSymbol;
            node->name = std::string(tok.text);
          }
          return node;
        case TokenKind::kLParen: {
          ++pos_;
          auto inner = ParseBinary(0);
          if (!inner) return inner;
          if (pos_ >= end_ || tokens_[pos_].kind != TokenKind::kRParen) {
            return std::unexpected(AsmError{"expected ')'", Column()});
          }
          ++pos_;
          return inner;
        }
        case TokenKind::kPercent: {
          ++pos_;
          if (pos_ >= end_ || tokens_[pos_].kind != TokenKind::kIdentifier) {
            return std::unexpected(
                AsmError{"expected relocation name after '%'", Column()});
          }
          const std::string_view name = tokens_[pos_].text;
          if (name == "hi") {
            node->reloc = RelocKind::kHi;
          } else if (name == "lo") {
            node->reloc = RelocKind::kLo;
          } else if (name == "pcrel_hi") {
            node->reloc = RelocKind::kPcrelHi;
          } else if (name == "pcrel_lo") {
            node->reloc = RelocKind::kPcrelLo;
          } else {
            return std::unexpected(AsmError{
                std::format("unknown relocation '%{}'", name), Column()});
          }
          ++pos_;
          if (pos_ >= end_ || tokens_[pos_].kind != TokenKind::kLParen) {
            return std::unexpected(
                AsmError{"expected '(' after relocation", Column()});
          }
          ++pos_;
          auto inner = ParseBinary(0);
          if (!inner) return inner;
          if (pos_ >= end_ || tokens_[pos_].kind != TokenKind::kRParen) {
            return std::unexpected(AsmError{"expected ')'", Column()});
          }
          ++pos_;
          node->kind = Expr::Kind::kReloc;
          node->lhs = *inner;
          return node;
        }
        default:
          return std::unexpected(
              AsmError{std::format("unexpected '{}' in expression", tok.text),
                       tok.column});
      }
    }

    const std::vector<Token>& tokens_;
    size_t pos_;
    size_t end_;
  };

  std::expected<ExprPtr, AsmError> ParseExpr(Span span) const {
    return ExprParser(tokens_, span).ParseAll();
  }

  // Splits [begin, end) on top-level commas.
  std::vector<Span> SplitOperands(Span span) const {
    std::vector<Span> parts;
    if (span.empty()) return parts;
    int depth = 0;
    size_t start = span.begin;
    for (size_t i = span.begin; i < span.end; ++i) {
      const TokenKind kind = tokens_[i].kind;
      if (kind == TokenKind::kLParen) ++depth;
      if (kind == TokenKind::kRParen) --depth;
      if (kind == TokenKind::kComma && depth == 0) {
        parts.push_back({start, i});
        start = i + 1;
      }
    }
    parts.push_back({start, span.end});
    return parts;
  }

  std::expected<Operand, AsmError> ParseOperand(Span span) const {
    if (span.empty()) {
      return std::unexpected(AsmError{"empty operand", Tok(span.begin).column});
    }
    Operand op;
    op.column = tokens_[span.begin].column;
    // Single register.
    if (span.end - span.begin == 1 &&
        tokens_[span.begin].kind == TokenKind::kIdentifier) {
      if (auto r = ParseGpr(tokens_[span.begin].text)) {
        op.kind = Operand::Kind::kRegister;
        op.reg = *r;
        return op;
      }
    }
    // Memory operand: [expr] ( reg )
    if (span.end - span.begin >= 3 &&
        tokens_[span.end - 1].kind == TokenKind::kRParen &&
        tokens_[span.end - 3].kind == TokenKind::kLParen &&
        tokens_[span.end - 2].kind == TokenKind::kIdentifier) {
      const Token& base = tokens_[span.end - 2];
      // "0(q9)", "(q9)" or "%lo(x)(q9)" can only be a memory operand.
      const bool must_be_memory =
          span.end - 3 == span.begin ||
          tokens_[span.end - 4].kind == TokenKind::kRParen ||
          tokens_[span.end - 4].kind == TokenKind::kInteger;
      if (must_be_memory && !ParseGpr(base.text)) {
        return std::unexpected(AsmError{
            std::format("unknown register '{}'", base.text), base.column});
      }
      if (auto r = ParseGpr(base.text)) {
        op.kind = Operand::Kind::kMemory;
        op.reg = *r;
        const Span offset{span.begin, span.end - 3};
        if (!offset.empty()) {
          auto expr = ParseExpr(offset);
          if (!expr) return std::unexpected(expr.error());
          op.expr = *expr;
        }
        return op;
      }
    }
    auto expr = ParseExpr(span);
    if (!expr) return std::unexpected(expr.error());
    op.kind = Operand::Kind::kExpression;
    op.expr = *expr;
    return op;
  }

  // ------------------------------------------------------------ evaluation
  std::expected<Value, EvalError> LabelValue(int section, uint64_t offset,
                                             const EvalContext& ctx) const {
    if (ctx.final) {
      return Value{static_cast<int64_t>(section_base_[section] + offset),
                   kAbsolute};
    }
    return Value{static_cast<int64_t>(offset), section};
  }

  std::expected<Value, EvalError> Eval(const Expr& expr,
                                       const EvalContext& ctx) const {
    switch (expr.kind) {
      case Expr::Kind::kNumber:
        return Value{expr.number, kAbsolute};
      case Expr::Kind::kDot:
        return LabelValue(ctx.dot_section, ctx.dot_offset, ctx);
      case Expr::Kind::kSymbol:
        return EvalSymbol(expr, ctx);
      case Expr::Kind::kLocalLabel:
        return EvalLocal(expr, ctx);
      case Expr::Kind::kReloc:
        return std::unexpected(EvalError{
            false, "relocation functions cannot be nested in expressions",
            expr.column});
      case Expr::Kind::kUnary: {
        auto v = Eval(*expr.lhs, ctx);
        if (!v) return v;
        if (v->section != kAbsolute) {
          return std::unexpected(EvalError{
              false, "unary operator applied to an address", expr.column});
        }
        const auto u = static_cast<uint64_t>(v->v);
        return Value{
            static_cast<int64_t>(expr.op == TokenKind::kMinus ? 0 - u : ~u),
            kAbsolute};
      }
      case Expr::Kind::kBinary:
        return EvalBinary(expr, ctx);
    }
    return std::unexpected(EvalError{false, "invalid expression", 0});
  }

  std::expected<Value, EvalError> EvalBinary(const Expr& expr,
                                             const EvalContext& ctx) const {
    auto l = Eval(*expr.lhs, ctx);
    if (!l) return l;
    auto r = Eval(*expr.rhs, ctx);
    if (!r) return r;
    const auto lu = static_cast<uint64_t>(l->v);
    const auto ru = static_cast<uint64_t>(r->v);
    if (expr.op == TokenKind::kPlus) {
      if (l->section != kAbsolute && r->section != kAbsolute) {
        return std::unexpected(
            EvalError{false, "cannot add two addresses", expr.column});
      }
      return Value{static_cast<int64_t>(lu + ru),
                   l->section != kAbsolute ? l->section : r->section};
    }
    if (expr.op == TokenKind::kMinus) {
      if (l->section != kAbsolute && r->section == l->section) {
        return Value{static_cast<int64_t>(lu - ru), kAbsolute};
      }
      if (r->section != kAbsolute) {
        return std::unexpected(EvalError{
            false, "cannot subtract addresses from different sections",
            expr.column});
      }
      return Value{static_cast<int64_t>(lu - ru), l->section};
    }
    if (l->section != kAbsolute || r->section != kAbsolute) {
      return std::unexpected(EvalError{
          false, "only + and - may be applied to addresses", expr.column});
    }
    switch (expr.op) {
      case TokenKind::kStar:
        return Value{static_cast<int64_t>(lu * ru), kAbsolute};
      case TokenKind::kSlash:
      case TokenKind::kPercent:
        if (r->v == 0) {
          return std::unexpected(
              EvalError{false, "division by zero", expr.column});
        }
        if (l->v == std::numeric_limits<int64_t>::min() && r->v == -1) {
          return Value{expr.op == TokenKind::kSlash ? l->v : 0, kAbsolute};
        }
        return Value{expr.op == TokenKind::kSlash ? l->v / r->v : l->v % r->v,
                     kAbsolute};
      case TokenKind::kShl:
        return Value{ru >= 64 ? 0 : static_cast<int64_t>(lu << ru), kAbsolute};
      case TokenKind::kShr:
        return Value{ru >= 64 ? (l->v < 0 ? -1 : 0) : (l->v >> ru), kAbsolute};
      case TokenKind::kAmp:
        return Value{static_cast<int64_t>(lu & ru), kAbsolute};
      case TokenKind::kPipe:
        return Value{static_cast<int64_t>(lu | ru), kAbsolute};
      case TokenKind::kCaret:
        return Value{static_cast<int64_t>(lu ^ ru), kAbsolute};
      default:
        break;
    }
    return std::unexpected(EvalError{false, "invalid operator", expr.column});
  }

  std::expected<Value, EvalError> EvalSymbol(const Expr& expr,
                                             const EvalContext& ctx) const {
    const auto it = symbols_.find(expr.name);
    if (it == symbols_.end()) {
      return std::unexpected(
          EvalError{!ctx.final, std::format("undefined symbol '{}'", expr.name),
                    expr.column});
    }
    const SymbolDef& def = it->second;
    if (def.kind == SymbolDef::Kind::kLabel) {
      return LabelValue(def.section, def.offset, ctx);
    }
    if (def.evaluating) {
      return std::unexpected(EvalError{
          false, std::format("symbol '{}' is defined recursively", expr.name),
          expr.column});
    }
    def.evaluating = true;
    EvalContext inner = def.ctx;
    inner.final = ctx.final;
    auto value = Eval(*def.expr, inner);
    def.evaluating = false;
    return value;
  }

  std::expected<Value, EvalError> EvalLocal(const Expr& expr,
                                            const EvalContext& ctx) const {
    const auto it = locals_.find(expr.number);
    const LocalDef* found = nullptr;
    if (it != locals_.end()) {
      for (const LocalDef& def : it->second) {
        if (expr.forward) {
          if (def.seq > ctx.seq) {
            found = &def;
            break;
          }
        } else if (def.seq <= ctx.seq) {
          found = &def;
        }
      }
    }
    if (found == nullptr) {
      return std::unexpected(
          EvalError{expr.forward && !ctx.final,
                    std::format("undefined local label '{}{}'", expr.number,
                                expr.forward ? 'f' : 'b'),
                    expr.column});
    }
    return LabelValue(found->section, found->offset, ctx);
  }

  EvalContext Pass1Context() const {
    return EvalContext{false, current_section_, section_size_[current_section_],
                       seq_};
  }

  // Pass-1 constant evaluation (absolute values only).
  std::expected<int64_t, EvalError> EvalConstantNow(const Expr& expr) const {
    auto value = Eval(expr, Pass1Context());
    if (!value) return std::unexpected(value.error());
    if (value->section != kAbsolute) {
      return std::unexpected(
          EvalError{false, "expected a constant, not an address", expr.column});
    }
    return value->v;
  }

  // ----------------------------------------------------------------- pass 1
  void Pass1() {
    size_t i = 0;
    while (i < tokens_.size() && tokens_[i].kind != TokenKind::kEnd) {
      size_t end = i;
      while (tokens_[end].kind != TokenKind::kNewline &&
             tokens_[end].kind != TokenKind::kEnd) {
        ++end;
      }
      if (end > i) {
        ParseStatement({i, end});
        ++seq_;
      }
      if (stop_) break;
      i = end;
      if (tokens_[i].kind == TokenKind::kNewline) ++i;
    }
  }

  void DefineLabel(const Token& tok) {
    const std::string name(tok.text);
    if (symbols_.contains(name)) {
      Error(tok.line, tok.column,
            std::format("symbol '{}' is already defined", name));
      return;
    }
    if (ParseGpr(name)) {
      Warning(tok.line, tok.column,
              std::format("label '{}' shadows a register name", name));
    }
    SymbolDef def;
    def.kind = SymbolDef::Kind::kLabel;
    def.section = current_section_;
    def.offset = Offset();
    def.line = tok.line;
    symbols_.emplace(name, std::move(def));
    label_order_.push_back(name);
  }

  void ParseStatement(Span span) {
    size_t i = span.begin;
    // Labels.
    while (i + 1 < span.end && tokens_[i + 1].kind == TokenKind::kColon) {
      const Token& tok = tokens_[i];
      if (tok.kind == TokenKind::kIdentifier) {
        DefineLabel(tok);
      } else if (tok.kind == TokenKind::kInteger &&
                 std::ranges::all_of(
                     tok.text, [](char c) { return c >= '0' && c <= '9'; })) {
        locals_[tok.value].push_back(
            LocalDef{seq_, current_section_, Offset()});
      } else {
        Error(tok.line, tok.column,
              std::format("invalid label '{}'", tok.text));
      }
      i += 2;
    }
    if (i >= span.end) return;

    const Token& head = tokens_[i];
    if (head.kind != TokenKind::kIdentifier) {
      Error(head.line, head.column,
            std::format("expected an instruction or directive, found '{}'",
                        head.text));
      return;
    }
    if (i + 1 < span.end && tokens_[i + 1].kind == TokenKind::kEquals) {
      DefineEqu(head, {i + 2, span.end});
      return;
    }
    if (head.text.size() > 1 && head.text.front() == '.') {
      ParseDirective(head, {i + 1, span.end});
      return;
    }
    ParseInstruction(head, {i + 1, span.end});
  }

  void DefineEqu(const Token& name_tok, Span expr_span) {
    auto expr = ParseExpr(expr_span);
    if (!expr) {
      Error(name_tok.line, expr.error().column, expr.error().message);
      return;
    }
    const std::string name(name_tok.text);
    auto it = symbols_.find(name);
    if (it != symbols_.end() && it->second.kind == SymbolDef::Kind::kLabel) {
      Error(name_tok.line, name_tok.column,
            std::format("symbol '{}' is already defined as a label", name));
      return;
    }
    SymbolDef def;
    def.kind = SymbolDef::Kind::kEqu;
    def.expr = *expr;
    def.ctx = Pass1Context();
    def.line = name_tok.line;
    // .set may redefine; evaluate eagerly when possible so that later
    // redefinitions do not change earlier uses.
    if (auto value = Eval(**expr, def.ctx);
        value && value->section == kAbsolute) {
      def.expr = MakeNumber(value->v, (*expr)->column);
    }
    symbols_.insert_or_assign(name, std::move(def));
  }

  void SwitchSection(const Token& tok, std::string_view name) {
    const std::string lower = Lowercase(name);
    auto matches = [&](std::string_view prefix) {
      return lower == prefix || lower.starts_with(std::string(prefix) + ".");
    };
    if (matches(".text")) {
      current_section_ = kText;
    } else if (matches(".data") || matches(".sdata")) {
      current_section_ = kData;
    } else if (matches(".rodata") || matches(".srodata")) {
      current_section_ = kRodata;
    } else if (matches(".bss") || matches(".sbss")) {
      current_section_ = kBss;
    } else {
      Error(tok.line, tok.column,
            std::format("unsupported section '{}'", name));
    }
  }

  void AddStatement(Statement stmt) {
    stmt.seq = seq_;
    stmt.section = current_section_;
    stmt.offset = Offset();
    if (stmt.size > kMaxSectionSize || Offset() + stmt.size > kMaxSectionSize) {
      Error(
          stmt.line, stmt.column,
          std::format("section {} exceeds the {} MiB limit",
                      kSectionNames[current_section_], kMaxSectionSize >> 20));
      stop_ = true;
      return;
    }
    if (current_section_ == kBss &&
        (stmt.kind != Statement::Kind::kBytes || !stmt.bytes.empty() ||
         stmt.fill_value != 0)) {
      Error(stmt.line, stmt.column,
            "only zero-initialized space (.zero/.space/.align) is allowed in "
            ".bss");
      return;
    }
    Offset() += stmt.size;
    statements_.push_back(std::move(stmt));
  }

  void AlignTo(const Token& tok, uint64_t alignment,
               std::optional<uint8_t> fill, std::optional<uint64_t> max_skip) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        alignment > 4096) {
      Error(tok.line, tok.column,
            std::format("alignment {} must be a power of two <= 4096",
                        alignment));
      return;
    }
    section_align_[current_section_] =
        std::max(section_align_[current_section_], alignment);
    const uint64_t padding = (alignment - (Offset() % alignment)) % alignment;
    if (padding == 0 || (max_skip && padding > *max_skip)) return;
    Statement stmt;
    stmt.kind = Statement::Kind::kBytes;
    stmt.line = tok.line;
    stmt.column = tok.column;
    stmt.size = padding;
    if (current_section_ == kText && !fill && padding % 4 == 0 &&
        Offset() % 4 == 0) {
      for (uint64_t k = 0; k < padding / 4; ++k) {
        for (int b = 0; b < 4; ++b) {
          stmt.bytes.push_back(static_cast<uint8_t>(kNopWord >> (8 * b)));
        }
      }
    } else {
      stmt.fill_count = padding;
      stmt.fill_value = fill.value_or(0);
    }
    AddStatement(std::move(stmt));
  }

  // Evaluates a directive argument that must be a pass-1 constant.
  std::optional<int64_t> DirectiveConstant(const Token& tok, Span span) {
    auto expr = ParseExpr(span);
    if (!expr) {
      Error(tok.line, expr.error().column, expr.error().message);
      return std::nullopt;
    }
    auto value = EvalConstantNow(**expr);
    if (!value) {
      Error(tok.line, value.error().column,
            value.error().unresolved
                ? std::format("{} (must be defined before this directive)",
                              value.error().message)
                : value.error().message);
      return std::nullopt;
    }
    return *value;
  }

  void ParseDirective(const Token& tok, Span args_span) {
    const std::string name = Lowercase(tok.text);
    const std::vector<Span> args = SplitOperands(args_span);
    const auto expect_args = [&](size_t min, size_t max) {
      if (args.size() < min || args.size() > max) {
        Error(tok.line, tok.column,
              std::format("'{}' expects {} argument{}", name,
                          min == max ? std::to_string(min)
                                     : std::format("{} to {}", min, max),
                          max == 1 ? "" : "s"));
        return false;
      }
      return true;
    };

    if (name == ".text" || name == ".data" || name == ".rodata" ||
        name == ".bss" || name == ".sdata" || name == ".sbss") {
      SwitchSection(tok, name);
      return;
    }
    if (name == ".section") {
      if (args.empty() || args[0].empty()) {
        Error(tok.line, tok.column, "'.section' expects a section name");
        return;
      }
      // Section names such as ".rodata.str1.1" are a single identifier.
      SwitchSection(Tok(args[0].begin), Tok(args[0].begin).text);
      return;
    }
    if (name == ".globl" || name == ".global") {
      for (const Span& arg : args) {
        if (arg.end - arg.begin != 1 ||
            Tok(arg.begin).kind != TokenKind::kIdentifier) {
          Error(tok.line, Tok(arg.begin).column, "expected a symbol name");
          continue;
        }
        globals_.insert(std::string(Tok(arg.begin).text));
      }
      return;
    }
    if (name == ".equ" || name == ".set" || name == ".equiv") {
      if (!expect_args(2, 2)) return;
      if (args[0].end - args[0].begin != 1 ||
          Tok(args[0].begin).kind != TokenKind::kIdentifier) {
        Error(tok.line, tok.column, "expected a symbol name");
        return;
      }
      if (name == ".equiv" &&
          symbols_.contains(std::string(Tok(args[0].begin).text))) {
        Error(tok.line, tok.column,
              std::format("symbol '{}' is already defined",
                          Tok(args[0].begin).text));
        return;
      }
      DefineEqu(Tok(args[0].begin), args[1]);
      return;
    }
    if (name == ".align" || name == ".p2align" || name == ".balign") {
      if (!expect_args(1, 3)) return;
      auto amount = DirectiveConstant(tok, args[0]);
      if (!amount) return;
      std::optional<uint8_t> fill;
      std::optional<uint64_t> max_skip;
      if (args.size() >= 2 && !args[1].empty()) {
        auto f = DirectiveConstant(tok, args[1]);
        if (!f) return;
        fill = static_cast<uint8_t>(*f);
      }
      if (args.size() == 3) {
        auto m = DirectiveConstant(tok, args[2]);
        if (!m) return;
        max_skip = static_cast<uint64_t>(*m);
      }
      if (name == ".balign") {
        AlignTo(tok, static_cast<uint64_t>(*amount), fill, max_skip);
      } else {
        if (*amount < 0 || *amount > 12) {
          Error(tok.line, tok.column,
                std::format("alignment exponent {} out of range [0, 12]",
                            *amount));
          return;
        }
        AlignTo(tok, uint64_t{1} << *amount, fill, max_skip);
      }
      return;
    }
    if (name == ".zero" || name == ".space" || name == ".skip") {
      if (!expect_args(1, 2)) return;
      auto count = DirectiveConstant(tok, args[0]);
      if (!count) return;
      if (*count < 0 || static_cast<uint64_t>(*count) > kMaxSectionSize) {
        Error(tok.line, tok.column,
              std::format("invalid size {} for '{}'", *count, name));
        return;
      }
      uint8_t fill = 0;
      if (args.size() == 2) {
        auto f = DirectiveConstant(tok, args[1]);
        if (!f) return;
        fill = static_cast<uint8_t>(*f);
      }
      Statement stmt;
      stmt.kind = Statement::Kind::kBytes;
      stmt.line = tok.line;
      stmt.column = tok.column;
      stmt.size = static_cast<uint64_t>(*count);
      stmt.fill_count = stmt.size;
      stmt.fill_value = fill;
      AddStatement(std::move(stmt));
      return;
    }
    if (name == ".ascii" || name == ".asciz" || name == ".string") {
      if (args.empty()) {
        Error(tok.line, tok.column, std::format("'{}' expects a string", name));
        return;
      }
      Statement stmt;
      stmt.kind = Statement::Kind::kBytes;
      stmt.line = tok.line;
      stmt.column = tok.column;
      for (const Span& arg : args) {
        if (arg.end - arg.begin != 1 ||
            Tok(arg.begin).kind != TokenKind::kString) {
          Error(tok.line, Tok(arg.begin).column, "expected a string literal");
          return;
        }
        const std::string& text = Tok(arg.begin).str;
        stmt.bytes.insert(stmt.bytes.end(), text.begin(), text.end());
        if (name != ".ascii") stmt.bytes.push_back(0);
      }
      stmt.size = stmt.bytes.size();
      AddStatement(std::move(stmt));
      return;
    }
    if (const int unit = DataUnit(name); unit > 0) {
      if (args.empty()) {
        Error(tok.line, tok.column,
              std::format("'{}' expects at least one value", name));
        return;
      }
      Statement stmt;
      stmt.kind = Statement::Kind::kData;
      stmt.line = tok.line;
      stmt.column = tok.column;
      stmt.unit = unit;
      for (const Span& arg : args) {
        auto expr = ParseExpr(arg);
        if (!expr) {
          Error(tok.line, expr.error().column, expr.error().message);
          return;
        }
        stmt.values.push_back(*expr);
      }
      stmt.size = static_cast<uint64_t>(unit) * stmt.values.size();
      AddStatement(std::move(stmt));
      return;
    }
    if (name == ".end") {
      stop_ = true;
      return;
    }
    if (name == ".option" || name == ".attribute" || name == ".file" ||
        name == ".ident" || name == ".type" || name == ".size" ||
        name == ".local" || name == ".weak" || name == ".hidden" ||
        name == ".loc" || name == ".addrsig" || name.starts_with(".cfi_")) {
      return;  // Accepted for compatibility with compiler output; no effect.
    }
    Error(tok.line, tok.column, std::format("unknown directive '{}'", name));
  }

  static int DataUnit(std::string_view name) {
    if (name == ".byte") return 1;
    if (name == ".half" || name == ".short" || name == ".2byte") return 2;
    if (name == ".word" || name == ".long" || name == ".4byte" ||
        name == ".int") {
      return 4;
    }
    if (name == ".dword" || name == ".quad" || name == ".8byte") return 8;
    return 0;
  }

  void ParseInstruction(const Token& tok, Span operand_span) {
    const std::string mnemonic = Lowercase(tok.text);
    if (current_section_ != kText) {
      Error(tok.line, tok.column,
            std::format("instruction '{}' outside of .text", mnemonic));
      return;
    }
    std::vector<Operand> operands;
    for (const Span& span : SplitOperands(operand_span)) {
      auto operand = ParseOperand(span);
      if (!operand) {
        Error(tok.line, operand.error().column, operand.error().message);
        return;
      }
      operands.push_back(std::move(*operand));
    }
    if (Offset() % 4 != 0) {
      Warning(tok.line, tok.column,
              "instruction is not 4-byte aligned; padding inserted");
      AlignTo(tok, 4, 0, std::nullopt);
    }
    ExpandContext context;
    context.xlen = options_.xlen;
    context.mnemonic_column = tok.column;
    context.eval_constant = [this](const Expr& expr) -> std::optional<int64_t> {
      auto value = EvalConstantNow(expr);
      return value ? std::optional<int64_t>(*value) : std::nullopt;
    };
    auto expanded = ExpandInstruction(mnemonic, operands, context);
    if (!expanded) {
      Error(tok.line, expanded.error().column, expanded.error().message);
      return;
    }
    Statement stmt;
    stmt.kind = Statement::Kind::kInstructions;
    stmt.line = tok.line;
    stmt.column = tok.column;
    stmt.size = 4 * expanded->size();
    stmt.insts = std::move(*expanded);
    AddStatement(std::move(stmt));
  }

  // ----------------------------------------------------------------- layout
  void Layout() {
    section_base_[kText] = options_.text_base;
    uint64_t cursor = options_.data_base;
    for (const int section : {kData, kRodata, kBss}) {
      const uint64_t align = std::max<uint64_t>(16, section_align_[section]);
      cursor = (cursor + align - 1) & ~(align - 1);
      section_base_[section] = cursor;
      cursor += section_size_[section];
    }
    data_end_ = cursor;
    const uint64_t text_end = options_.text_base + section_size_[kText];
    if (section_size_[kText] > 0 && text_end > options_.data_base &&
        options_.text_base < data_end_) {
      Error(1, 1, ".text overlaps the data sections");
    }
  }

  // ----------------------------------------------------------------- pass 2
  struct ImmError {
    std::string message;
    int column;
  };

  std::expected<int64_t, ImmError> EvalFinal(const ExprPtr& expr,
                                             const Statement& stmt,
                                             uint64_t pc) const {
    if (!expr) return 0;
    const EvalContext ctx{true, stmt.section, pc - section_base_[stmt.section],
                          stmt.seq};
    auto value = Eval(*expr, ctx);
    if (!value) {
      return std::unexpected(
          ImmError{value.error().message, value.error().column});
    }
    return value->v;
  }

  // Splits an offset into the %hi/%lo pair, checking the 32-bit range.
  std::expected<int64_t, std::string> Hi20(int64_t value, bool pcrel) const {
    if (options_.xlen == Xlen::k32 && !pcrel) {
      value = static_cast<int64_t>(static_cast<uint32_t>(value));
      const auto hi = static_cast<uint32_t>((value + 0x800) >> 12) & 0xFFFFF;
      return static_cast<int64_t>(static_cast<int32_t>(hi << 12));
    }
    if (pcrel && options_.xlen == Xlen::k32) {
      value = static_cast<int32_t>(static_cast<uint32_t>(value));
    }
    const int64_t hi = (value + 0x800) >> 12;
    if (hi < -(int64_t{1} << 19) || hi >= (int64_t{1} << 19)) {
      return std::unexpected(std::format(
          "value {:#x} out of range for {}", value,
          pcrel ? "%pcrel_hi (+/-2 GiB)" : "%hi (32-bit addresses)"));
    }
    return hi << 12;
  }

  std::expected<int64_t, ImmError> ResolveImm(const PendingInst& inst,
                                              const Statement& stmt,
                                              uint64_t pc) const {
    const auto fail = [&](std::string message) {
      return std::unexpected(ImmError{std::move(message), inst.column});
    };
    const auto lo_of_pcrel =
        [&](uint64_t auipc_pc) -> std::expected<int64_t, ImmError> {
      const auto it = pcrel_hi_.find(auipc_pc);
      if (it == pcrel_hi_.end()) {
        return fail(std::format("no %pcrel_hi found at {:#x}", auipc_pc));
      }
      return SignExtend12(it->second);
    };

    switch (inst.mode) {
      case ImmMode::kConstant:
        return inst.imm;
      case ImmMode::kValue:
        return EvalFinal(inst.expr, stmt, pc);
      case ImmMode::kUpperValue: {
        auto v = EvalFinal(inst.expr, stmt, pc);
        if (!v) return v;
        if (*v < 0 || *v > 0xFFFFF) {
          return fail(
              std::format("upper immediate {} out of range [0, 0xfffff]", *v));
        }
        return static_cast<int64_t>(
            static_cast<int32_t>(static_cast<uint32_t>(*v) << 12));
      }
      case ImmMode::kPcRel: {
        auto v = EvalFinal(inst.expr, stmt, pc);
        if (!v) return v;
        uint64_t offset = static_cast<uint64_t>(*v) - pc;
        if (options_.xlen == Xlen::k32) {
          return static_cast<int64_t>(
              static_cast<int32_t>(static_cast<uint32_t>(offset)));
        }
        return static_cast<int64_t>(offset);
      }
      case ImmMode::kHi: {
        auto v = EvalFinal(inst.expr, stmt, pc);
        if (!v) return v;
        auto hi = Hi20(*v, false);
        if (!hi) return fail(hi.error());
        return *hi;
      }
      case ImmMode::kLo: {
        auto v = EvalFinal(inst.expr, stmt, pc);
        if (!v) return v;
        return SignExtend12(*v);
      }
      case ImmMode::kPcrelHi: {
        const auto it = pcrel_hi_.find(pc);
        if (it == pcrel_hi_.end()) return fail("internal: missing pcrel_hi");
        auto hi = Hi20(it->second, true);
        if (!hi) return fail(hi.error());
        return *hi;
      }
      case ImmMode::kPcrelLo: {
        auto label = EvalFinal(inst.expr, stmt, pc);
        if (!label) return label;
        return lo_of_pcrel(static_cast<uint64_t>(*label));
      }
      case ImmMode::kPcrelLoPrev:
        return lo_of_pcrel(pc - 4);
    }
    return fail("internal: invalid immediate mode");
  }

  std::optional<ProgramImage> Pass2() {
    // 2a: compute every %pcrel_hi offset so %pcrel_lo can refer to them.
    for (const Statement& stmt : statements_) {
      if (stmt.kind != Statement::Kind::kInstructions) continue;
      uint64_t pc = section_base_[stmt.section] + stmt.offset;
      for (const PendingInst& inst : stmt.insts) {
        if (inst.mode == ImmMode::kPcrelHi) {
          auto target = EvalFinal(inst.expr, stmt, pc);
          if (!target) {
            Error(stmt.line, target.error().column, target.error().message);
          } else {
            pcrel_hi_[pc] =
                static_cast<int64_t>(static_cast<uint64_t>(*target) - pc);
          }
        }
        pc += 4;
      }
    }

    std::array<std::vector<uint8_t>, kNumSections> bytes;
    for (const int s : {kText, kData, kRodata}) {
      bytes[s].assign(section_size_[s], 0);
    }
    ProgramImage image;
    image.xlen = options_.xlen;

    for (const Statement& stmt : statements_) {
      const uint64_t base = section_base_[stmt.section];
      std::vector<uint8_t>& out = bytes[stmt.section];
      switch (stmt.kind) {
        case Statement::Kind::kInstructions: {
          uint64_t pc = base + stmt.offset;
          for (size_t k = 0; k < stmt.insts.size(); ++k, pc += 4) {
            const PendingInst& inst = stmt.insts[k];
            image.source_map.Add(pc, stmt.line);
            auto imm = ResolveImm(inst, stmt, pc);
            if (!imm) {
              Error(stmt.line, imm.error().column, imm.error().message);
              continue;
            }
            auto word = Encode(inst.op, inst.rd, inst.rs1, inst.rs2, *imm,
                               options_.xlen);
            if (!word) {
              Error(stmt.line, inst.column, word.error());
              continue;
            }
            const uint64_t at = stmt.offset + 4 * k;
            for (int b = 0; b < 4; ++b) {
              out[at + static_cast<uint64_t>(b)] =
                  static_cast<uint8_t>(*word >> (8 * b));
            }
          }
          break;
        }
        case Statement::Kind::kData: {
          for (size_t k = 0; k < stmt.values.size(); ++k) {
            const uint64_t at =
                stmt.offset + k * static_cast<uint64_t>(stmt.unit);
            auto value = EvalFinal(stmt.values[k], stmt, base + at);
            if (!value) {
              Error(stmt.line, value.error().column, value.error().message);
              continue;
            }
            if (stmt.unit < 8) {
              const int bits = stmt.unit * 8;
              const int64_t min = -(int64_t{1} << (bits - 1));
              const int64_t max = (int64_t{1} << bits) - 1;
              if (*value < min || *value > max) {
                Error(stmt.line, stmt.values[k]->column,
                      std::format("value {} does not fit in {} byte{}", *value,
                                  stmt.unit, stmt.unit == 1 ? "" : "s"));
                continue;
              }
            }
            if (stmt.section == kBss) continue;
            for (int b = 0; b < stmt.unit; ++b) {
              out[at + static_cast<uint64_t>(b)] = static_cast<uint8_t>(
                  static_cast<uint64_t>(*value) >> (8 * b));
            }
          }
          break;
        }
        case Statement::Kind::kBytes: {
          if (stmt.section == kBss) break;
          std::ranges::copy(stmt.bytes,
                            out.begin() + static_cast<ptrdiff_t>(stmt.offset));
          std::fill_n(out.begin() + static_cast<ptrdiff_t>(stmt.offset +
                                                           stmt.bytes.size()),
                      stmt.fill_count, stmt.fill_value);
          break;
        }
      }
    }

    // Undefined globals.
    for (const std::string& name : globals_) {
      if (!symbols_.contains(name)) {
        Warning(1, 1,
                std::format("'.globl {}' refers to an undefined symbol", name));
      }
    }
    if (HasErrors()) return std::nullopt;

    // Segments.
    for (const int s : {kText, kData, kRodata, kBss}) {
      if (section_size_[s] == 0) continue;
      Segment segment;
      segment.name = std::string(kSectionNames[s]);
      segment.address = section_base_[s];
      segment.mem_size = section_size_[s];
      if (s != kBss) segment.data = std::move(bytes[s]);
      segment.readable = true;
      segment.writable = s == kData || s == kBss;
      segment.executable = s == kText;
      image.segments.push_back(std::move(segment));
    }

    // Symbols (labels only, in definition order before sorting).
    for (const std::string& name : label_order_) {
      const SymbolDef& def = symbols_.at(name);
      image.symbols.push_back(Symbol{name,
                                     section_base_[def.section] + def.offset,
                                     globals_.contains(name)});
    }
    image.FinalizeSymbols();

    if (auto start = image.LookupSymbol("_start")) {
      image.entry_point = *start;
    } else if (auto main = image.LookupSymbol("main")) {
      image.entry_point = *main;
    } else {
      image.entry_point = options_.text_base;
    }
    if (auto gp = image.LookupSymbol("__global_pointer$")) {
      image.initial_gp = *gp;
    } else {
      image.initial_gp = options_.data_base + 0x800;
    }
    image.initial_sp = options_.stack_top;
    image.heap_start =
        std::max(options_.data_base, (data_end_ + 0xFFF) & ~uint64_t{0xFFF});
    if (auto tohost = image.LookupSymbol("tohost")) {
      image.tohost_addr = *tohost;
    }
    return image;
  }

  AssemblerOptions options_;
  std::vector<Diagnostic> diagnostics_;
  std::vector<Token> tokens_;
  std::vector<Statement> statements_;
  std::unordered_map<std::string, SymbolDef> symbols_;
  std::vector<std::string> label_order_;
  std::unordered_map<int64_t, std::vector<LocalDef>> locals_;
  std::set<std::string> globals_;
  std::map<uint64_t, int64_t> pcrel_hi_;
  std::array<uint64_t, kNumSections> section_size_{};
  std::array<uint64_t, kNumSections> section_align_{4, 8, 8, 8};
  std::array<uint64_t, kNumSections> section_base_{};
  uint64_t data_end_ = 0;
  int current_section_ = kText;
  size_t seq_ = 0;
  bool stop_ = false;
};

}  // namespace

AssemblyResult Assemble(std::string_view source,
                        const AssemblerOptions& options) {
  return Assembler(source, options).Run();
}

}  // namespace rvsim
