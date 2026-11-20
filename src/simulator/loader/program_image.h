#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simulator/core/types.h"

namespace rvsim {

// A contiguous region of the program loaded into memory. Bytes beyond
// `data.size()` up to `mem_size` are zero-filled (e.g. .bss).
struct Segment {
  std::string name{};
  uint64_t address = 0;
  std::vector<uint8_t> data{};
  uint64_t mem_size = 0;
  bool readable = true;
  bool writable = false;
  bool executable = false;

  uint64_t end() const { return address + mem_size; }
  bool Contains(uint64_t addr) const {
    return addr >= address && addr - address < mem_size;
  }
};

// Bidirectional mapping between instruction addresses and 1-based source
// lines.
class SourceMap {
 public:
  void Add(uint64_t address, int line);
  void Clear();

  std::optional<int> LineForAddress(uint64_t address) const;
  // Lowest address emitted for `line`, if any.
  std::optional<uint64_t> AddressForLine(int line) const;
  // All addresses emitted for `line` (pseudo-instructions expand to many).
  std::vector<uint64_t> AddressesForLine(int line) const;

  bool empty() const { return by_address_.empty(); }
  const std::map<uint64_t, int>& by_address() const { return by_address_; }

 private:
  std::map<uint64_t, int> by_address_;
  std::multimap<int, uint64_t> by_line_;
};

struct Symbol {
  std::string name{};
  uint64_t address = 0;
  bool global = false;
};

enum class Severity : uint8_t { kError, kWarning };

// Assembler / loader diagnostic with a 1-based source position.
struct Diagnostic {
  Severity severity = Severity::kError;
  int line = 0;
  int column = 0;
  std::string message{};
};

// Fully linked, ready-to-load program.
struct ProgramImage {
  Xlen xlen = Xlen::k32;
  uint64_t entry_point = 0;
  std::optional<uint64_t> initial_gp;
  uint64_t initial_sp = 0;
  uint64_t heap_start = 0;
  std::vector<Segment> segments;
  std::vector<Symbol> symbols;  // Sorted by address.
  SourceMap source_map;
  std::optional<uint64_t> tohost_addr;

  const Segment* FindSegment(std::string_view name) const;
  const Segment* SegmentContaining(uint64_t address) const;
  std::optional<uint64_t> LookupSymbol(std::string_view name) const;
  // Symbol with the greatest address <= `address`, if any.
  const Symbol* NearestSymbol(uint64_t address) const;
  // Symbol defined exactly at `address`, if any.
  const Symbol* SymbolAt(uint64_t address) const;

  // Sorts the symbol table; call after populating `symbols`.
  void FinalizeSymbols();
};

}  // namespace rvsim
