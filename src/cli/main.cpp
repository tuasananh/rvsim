// rvsim-cli: headless RISC-V assembler and simulator runner.
//
//   rvsim-cli run <file.s> [--arch rv32i|rv64i] [--env rars]
//                          [--max-steps N] [--dump-regs] [--trace]
//   rvsim-cli assemble <file.s> [--arch rv32i|rv64i] [--disasm]
//
// Exit status: the program's exit code for `run`; 1 on a runtime fault or
// step limit; 2 on usage, I/O, or assembly errors.

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "simulator/core/decoder.h"
#include "simulator/core/disassembler.h"
#include "simulator/core/registers.h"
#include "simulator/env/environment.h"
#include "simulator/loader/assembler.h"
#include "simulator/runtime/machine.h"
#include "simulator/simulator.h"

namespace {

using namespace rvsim;  // NOLINT(build/namespaces)

constexpr int kExitFault = 1;
constexpr int kExitUsage = 2;
constexpr uint64_t kDefaultMaxSteps = 1'000'000'000;

// Console backed by the process's stdin/stdout/stderr.
class StdioConsole final : public ConsoleIo {
 public:
  void WriteStdout(std::string_view text) override {
    std::fwrite(text.data(), 1, text.size(), stdout);
  }
  void WriteStderr(std::string_view text) override {
    std::fflush(stdout);
    std::fwrite(text.data(), 1, text.size(), stderr);
  }
  std::optional<std::string> ReadLine() override {
    std::fflush(stdout);
    std::string line;
    int c = 0;
    while ((c = std::fgetc(stdin)) != EOF) {
      line.push_back(static_cast<char>(c));
      if (c == '\n') break;
    }
    return line;  // Empty string signals end-of-file.
  }
  std::optional<int> ReadChar() override {
    std::fflush(stdout);
    const int c = std::fgetc(stdin);
    return c == EOF ? -1 : c;
  }
};

struct Options {
  std::string command;
  std::string path;
  Xlen xlen = Xlen::k32;
  uint64_t max_steps = kDefaultMaxSteps;
  bool dump_regs = false;
  bool trace = false;
  bool disasm = false;
};

void PrintUsage(std::FILE* out) {
  std::println(out,
               "rvsim-cli {}\n"
               "usage:\n"
               "  rvsim-cli run <file.s> [--arch rv32i|rv64i] [--env rars]\n"
               "                         [--max-steps N] [--dump-regs] "
               "[--trace]\n"
               "  rvsim-cli assemble <file.s> [--arch rv32i|rv64i] "
               "[--disasm]",
               CoreVersion());
}

std::optional<Xlen> ParseArch(std::string_view arch) {
  if (arch == "rv32" || arch == "rv32i") return Xlen::k32;
  if (arch == "rv64" || arch == "rv64i") return Xlen::k64;
  return std::nullopt;
}

std::optional<Options> ParseArgs(int argc, char** argv) {
  if (argc < 2) return std::nullopt;
  Options options;
  options.command = argv[1];
  if (options.command == "--help" || options.command == "-h") {
    PrintUsage(stdout);
    std::exit(0);
  }
  if (options.command != "run" && options.command != "assemble") {
    std::println(stderr, "rvsim-cli: unknown command '{}'", options.command);
    return std::nullopt;
  }
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const auto next = [&]() -> std::optional<std::string_view> {
      if (i + 1 >= argc) {
        std::println(stderr, "rvsim-cli: '{}' expects a value", arg);
        return std::nullopt;
      }
      return std::string_view(argv[++i]);
    };
    if (arg == "--arch") {
      auto value = next();
      if (!value) return std::nullopt;
      auto xlen = ParseArch(*value);
      if (!xlen) {
        std::println(stderr, "rvsim-cli: unsupported --arch '{}'", *value);
        return std::nullopt;
      }
      options.xlen = *xlen;
    } else if (arg == "--env") {
      auto value = next();
      if (!value) return std::nullopt;
      if (*value != "rars") {
        std::println(stderr, "rvsim-cli: unsupported --env '{}'", *value);
        return std::nullopt;
      }
    } else if (arg == "--max-steps") {
      auto value = next();
      if (!value) return std::nullopt;
      const auto [ptr, ec] = std::from_chars(
          value->data(), value->data() + value->size(), options.max_steps);
      if (ec != std::errc{} || ptr != value->data() + value->size()) {
        std::println(stderr, "rvsim-cli: invalid --max-steps '{}'", *value);
        return std::nullopt;
      }
    } else if (arg == "--dump-regs") {
      options.dump_regs = true;
    } else if (arg == "--trace") {
      options.trace = true;
    } else if (arg == "--disasm") {
      options.disasm = true;
    } else if (!arg.empty() && arg.front() == '-') {
      std::println(stderr, "rvsim-cli: unknown option '{}'", arg);
      return std::nullopt;
    } else if (options.path.empty()) {
      options.path = arg;
    } else {
      std::println(stderr, "rvsim-cli: unexpected argument '{}'", arg);
      return std::nullopt;
    }
  }
  if (options.path.empty()) {
    std::println(stderr, "rvsim-cli: missing input file");
    return std::nullopt;
  }
  return options;
}

std::optional<std::string> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string_view SourceLine(std::string_view source, int line) {
  size_t start = 0;
  for (int current = 1; current < line; ++current) {
    start = source.find('\n', start);
    if (start == std::string_view::npos) return {};
    ++start;
  }
  const size_t end = source.find('\n', start);
  std::string_view text = source.substr(start, end == std::string_view::npos
                                                   ? std::string_view::npos
                                                   : end - start);
  if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
  return text;
}

void PrintDiagnostics(const Options& options, std::string_view source,
                      const AssemblyResult& result) {
  for (const Diagnostic& d : result.diagnostics) {
    std::println(stderr, "{}:{}:{}: {}: {}", options.path, d.line, d.column,
                 d.severity == Severity::kError ? "error" : "warning",
                 d.message);
    const std::string_view text = SourceLine(source, d.line);
    if (!text.empty()) {
      std::string caret;
      for (int i = 1; i < d.column && static_cast<size_t>(i - 1) < text.size();
           ++i) {
        caret.push_back(text[static_cast<size_t>(i - 1)] == '\t' ? '\t' : ' ');
      }
      std::println(stderr, "  {}\n  {}^", text, caret);
    }
  }
}

void PrintDisassembly(const ProgramImage& image, std::string_view source) {
  for (const Segment& segment : image.segments) {
    if (!segment.executable) continue;
    std::println("Disassembly of section {}:", segment.name);
    for (size_t offset = 0; offset + 4 <= segment.data.size(); offset += 4) {
      const uint64_t pc = segment.address + offset;
      if (const Symbol* symbol = image.SymbolAt(pc)) {
        std::println("\n{:016x} <{}>:", pc, symbol->name);
      }
      uint32_t word = 0;
      for (int b = 0; b < 4; ++b) {
        word |=
            static_cast<uint32_t>(segment.data[offset + static_cast<size_t>(b)])
            << (8 * b);
      }
      const std::string text = Disassemble(Decode(word, image.xlen), pc);
      const auto line = image.source_map.LineForAddress(pc);
      if (line) {
        std::println("  {:8x}:  {:08x}  {:<32} # {}: {}", pc, word, text, *line,
                     SourceLine(source, *line));
      } else {
        std::println("  {:8x}:  {:08x}  {}", pc, word, text);
      }
    }
  }
}

void DumpRegisters(const Machine& machine) {
  const CpuState& state = machine.state();
  const int width = state.xlen == Xlen::k32 ? 8 : 16;
  std::println(stderr, "pc   = 0x{:0{}x}   instret = {}", state.pc, width,
               state.instret);
  for (int i = 0; i < kNumGprs; ++i) {
    const auto idx = static_cast<uint8_t>(i);
    std::print(stderr, "{:>4} {:<4} = 0x{:0{}x} {:>21}", GprName(idx, false),
               GprName(idx, true), state.ReadReg(idx), width,
               state.ReadSigned(idx));
    std::print(stderr, "{}", (i % 2 == 1) ? "\n" : "   ");
  }
}

int RunProgram(const Options& options, ProgramImage image) {
  StdioConsole console;
  Machine machine(MachineConfig{.xlen = options.xlen}, console);
  if (auto loaded = machine.Load(std::move(image)); !loaded) {
    std::println(stderr, "rvsim-cli: {}", loaded.error().message);
    return kExitFault;
  }

  uint64_t steps = 0;
  int status = 0;
  while (true) {
    if (steps >= options.max_steps) {
      std::fflush(stdout);
      std::println(stderr, "rvsim-cli: step limit of {} reached at pc={:#x}",
                   options.max_steps, machine.state().pc);
      status = kExitFault;
      break;
    }
    if (options.trace) {
      const uint64_t pc = machine.state().pc;
      auto word = machine.bus().FetchInstruction32(pc);
      if (word) {
        std::fflush(stdout);
        std::println(stderr, "[{:>8}] {:#010x}: {:08x}  {}",
                     machine.state().instret, pc, *word,
                     Disassemble(Decode(*word, options.xlen), pc));
      }
    }
    const RunResult result =
        machine.Run(options.trace ? 1 : options.max_steps - steps);
    steps += result.steps;
    if (result.status == StepStatus::kExited) {
      status = static_cast<int>(machine.exit_code() & 0xFF);
      break;
    }
    if (result.status == StepStatus::kFaulted) {
      std::fflush(stdout);
      std::println(stderr, "rvsim-cli: fault: {}", machine.fault_message());
      status = kExitFault;
      break;
    }
    // kEbreak: continue; kRetired: step budget exhausted or trace step.
  }
  std::fflush(stdout);
  if (options.dump_regs) {
    DumpRegisters(machine);
  }
  return status;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> options = ParseArgs(argc, argv);
  if (!options) {
    PrintUsage(stderr);
    return kExitUsage;
  }
  const std::optional<std::string> source = ReadFile(options->path);
  if (!source) {
    std::println(stderr, "rvsim-cli: cannot read '{}'", options->path);
    return kExitUsage;
  }
  AssemblyResult result =
      Assemble(*source, AssemblerOptions{.xlen = options->xlen});
  PrintDiagnostics(*options, *source, result);
  if (!result.ok()) {
    std::println(stderr, "rvsim-cli: {} error(s)", result.error_count());
    return kExitUsage;
  }
  if (options->command == "assemble") {
    if (options->disasm) {
      PrintDisassembly(*result.image, *source);
    } else {
      for (const Segment& segment : result.image->segments) {
        std::println("{:<8} {:#010x}  {} bytes", segment.name, segment.address,
                     segment.mem_size);
      }
      std::println("entry    {:#010x}", result.image->entry_point);
    }
    return 0;
  }
  return RunProgram(*options, std::move(*result.image));
}
