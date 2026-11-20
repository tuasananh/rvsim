#include "ui/riscv_language.h"

#include <string_view>

#include "simulator/core/instruction.h"
#include "simulator/core/registers.h"
#include "ui/design_tokens.h"

namespace rvsim::ui {
namespace {

using Iterator = TextEditor::Iterator;

constexpr std::string_view kPseudoInstructions[] = {
    "nop",    "li",     "la",   "lla",  "mv",   "not",      "neg",  "negw",
    "sext.w", "zext.b", "seqz", "snez", "sltz", "sgtz",     "beqz", "bnez",
    "blez",   "bgez",   "bltz", "bgtz", "bgt",  "ble",      "bgtu", "bleu",
    "j",      "jr",     "ret",  "call", "tail", "fence.tso"};

bool IsIdentStart(ImWchar c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         c == '.' || c == '$';
}

bool IsIdentChar(ImWchar c) {
  return IsIdentStart(c) || (c >= '0' && c <= '9');
}

bool IsDigit(ImWchar c) { return c >= '0' && c <= '9'; }

Iterator GetIdentifier(Iterator start, Iterator end) {
  Iterator i = start;
  if (i < end && IsIdentStart(*i)) {
    ++i;
    while (i < end && IsIdentChar(*i)) ++i;
  }
  return i;
}

Iterator GetNumber(Iterator start, Iterator end) {
  Iterator i = start;
  if (!(i < end) || !IsDigit(*i)) return start;
  // Digits plus hex/binary/octal prefixes and local label suffixes (1f/1b).
  while (i < end && IsIdentChar(*i) && *i != '.') ++i;
  return i;
}

// Directives (".text"), relocations ("%hi"), and label definitions ("x:").
Iterator Tokenize(Iterator start, Iterator end, TextEditor::Color& color) {
  if (!(start < end)) return start;
  if (*start == '%') {
    Iterator i = start;
    ++i;
    Iterator ident_end = GetIdentifier(i, end);
    if (ident_end != i) {
      color = TextEditor::Color::preprocessor;
      return ident_end;
    }
    return start;
  }
  if (!IsIdentStart(*start) && !IsDigit(*start)) return start;
  Iterator ident_end =
      IsDigit(*start) ? GetNumber(start, end) : GetIdentifier(start, end);
  if (ident_end < end && *ident_end == ':') {
    color = TextEditor::Color::declaration;
    ++ident_end;
    return ident_end;
  }
  if (*start == '.' && ident_end != start) {
    Iterator next = start;
    ++next;
    if (next != ident_end) {
      color = TextEditor::Color::preprocessor;
      return ident_end;
    }
  }
  return start;
}

bool IsPunctuation(ImWchar c) {
  switch (c) {
    case ',':
    case '(':
    case ')':
    case '+':
    case '-':
    case '*':
    case '/':
    case '%':
    case '<':
    case '>':
    case '&':
    case '|':
    case '^':
    case '~':
    case '=':
    case ':':
    case ';':
      return true;
    default:
      return false;
  }
}

}  // namespace

const TextEditor::Language* RiscvLanguage() {
  static const TextEditor::Language language = [] {
    TextEditor::Language lang;
    lang.name = "RISC-V Assembly";
    lang.caseSensitive = true;
    lang.singleLineComment = "#";
    lang.singleLineCommentAlt = "//";
    lang.commentStart = "/*";
    lang.commentEnd = "*/";
    lang.hasSingleQuotedStrings = true;
    lang.hasDoubleQuotedStrings = true;
    lang.stringEscape = '\\';
    for (size_t i = 1; i < static_cast<size_t>(Opcode::kCount); ++i) {
      lang.keywords.insert(
          std::string(GetOpcodeInfo(static_cast<Opcode>(i)).mnemonic));
    }
    for (std::string_view pseudo : kPseudoInstructions) {
      lang.keywords.insert(std::string(pseudo));
    }
    for (const RegisterInfo& info : kGprInfo) {
      lang.identifiers.insert(std::string(info.numeric_name));
      lang.identifiers.insert(std::string(info.abi_name));
    }
    lang.identifiers.insert("fp");
    lang.isPunctuation = IsPunctuation;
    lang.getIdentifier = GetIdentifier;
    lang.getNumber = GetNumber;
    lang.customTokenizer = Tokenize;
    return lang;
  }();
  return &language;
}

TextEditor::Palette RiscvEditorPalette() {
  using Color = TextEditor::Color;
  const Palette& c = Colors();
  TextEditor::Palette palette = TextEditor::GetDarkPalette();
  const auto set = [&](Color slot, ImU32 value) {
    palette[static_cast<size_t>(slot)] = value;
  };
  set(Color::text, U32(c.text_primary));
  set(Color::keyword, IM_COL32(0x6C, 0xB6, 0xFF, 0xFF));  // mnemonics
  set(Color::declaration, U32(c.symbol));                 // labels
  set(Color::number, IM_COL32(0xB5, 0xCE, 0xA8, 0xFF));
  set(Color::string, IM_COL32(0xE6, 0xA0, 0x7C, 0xFF));
  set(Color::punctuation, U32(c.text_secondary));
  set(Color::preprocessor, IM_COL32(0xD2, 0x9B, 0xEB, 0xFF));  // directives
  set(Color::identifier, U32(c.text_primary));
  set(Color::knownIdentifier, IM_COL32(0xF2, 0xC9, 0x7D, 0xFF));  // registers
  set(Color::comment, IM_COL32(0x7F, 0x8A, 0x9C, 0xFF));
  set(Color::background, U32(c.surface_base));
  set(Color::cursor, U32(c.text_primary));
  set(Color::selection, IM_COL32(0x61, 0xA9, 0xFF, 0x55));
  set(Color::lineNumber, U32(c.text_disabled));
  set(Color::currentLineNumber, U32(c.text_primary));
  return palette;
}

}  // namespace rvsim::ui
