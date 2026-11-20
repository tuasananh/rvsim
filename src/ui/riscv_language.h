#pragma once

#include "TextEditor.h"

namespace rvsim::ui {

// Syntax definition for GNU as-style RISC-V assembly: mnemonics (keywords),
// registers (known identifiers), directives and %relocations (preprocessor),
// labels (declarations), numbers, strings, and #, //, /* */ comments.
const TextEditor::Language* RiscvLanguage();

// Editor palette derived from the rvsim design tokens.
TextEditor::Palette RiscvEditorPalette();

}  // namespace rvsim::ui
