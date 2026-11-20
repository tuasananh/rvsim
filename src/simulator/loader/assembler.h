#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "simulator/core/types.h"
#include "simulator/loader/program_image.h"

namespace rvsim {

struct AssemblerOptions {
  Xlen xlen = Xlen::k32;
  uint64_t text_base = 0x00400000;
  uint64_t data_base = 0x10000000;  // .data, then .rodata, then .bss
  uint64_t stack_top = 0x7FFFFFF0;
};

struct AssemblyResult {
  // Present only when assembly produced no errors.
  std::optional<ProgramImage> image;
  // All errors and warnings, ordered by source position.
  std::vector<Diagnostic> diagnostics;

  bool ok() const { return image.has_value(); }
  size_t error_count() const;
};

// Assembles a single GNU as-compatible RISC-V source file.
//
// Supported: .text/.data/.rodata/.bss/.section, .globl/.global, .equ/.set and
// `sym = expr`, .align/.p2align/.balign, .byte/.half/.short/.word/.long/
// .dword/.quad (+ .2byte/.4byte/.8byte), .ascii/.asciz/.string,
// .zero/.space/.skip; labels, numeric local labels (1: / 1f / 1b), "."; C-like
// constant expressions; %hi/%lo/%pcrel_hi/%pcrel_lo; all RV32I/RV64I
// instructions and the standard base pseudo-instructions.
//
// Integer literals used as branch/jump targets are byte offsets; expressions
// that reference symbols are absolute target addresses.
AssemblyResult Assemble(std::string_view source,
                        const AssemblerOptions& options = {});

}  // namespace rvsim
