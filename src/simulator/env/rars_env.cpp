#include "simulator/env/rars_env.h"

#include <charconv>
#include <format>
#include <string>

#include "simulator/core/registers.h"

namespace rvsim {
namespace {

std::string_view Trim(std::string_view text) {
  constexpr std::string_view kWhitespace = " \t\r\n";
  const size_t begin = text.find_first_not_of(kWhitespace);
  if (begin == std::string_view::npos) {
    return {};
  }
  const size_t end = text.find_last_not_of(kWhitespace);
  return text.substr(begin, end - begin + 1);
}

std::optional<int64_t> ParseInt(std::string_view text) {
  text = Trim(text);
  int base = 10;
  bool negative = false;
  if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    text.remove_prefix(2);
  }
  uint64_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (ec != std::errc{} || ptr != text.data() + text.size() || text.empty()) {
    return std::nullopt;
  }
  return negative ? static_cast<int64_t>(0 - value)
                  : static_cast<int64_t>(value);
}

}  // namespace

void RarsEnvironment::Reset(uint64_t heap_start) {
  program_break_ = heap_start;
}

EcallResult RarsEnvironment::HandleEcall(CpuState& state, MemoryBus& bus) {
  const auto service = static_cast<uint32_t>(state.ReadReg(reg::kA7));
  const uint64_t a0 = state.ReadReg(reg::kA0);
  const bool rv32 = state.xlen == Xlen::k32;

  switch (service) {
    case kPrintInt:
      console_.WriteStdout(std::to_string(state.ReadSigned(reg::kA0)));
      return {};
    case kPrintUnsigned:
      console_.WriteStdout(std::to_string(a0));
      return {};
    case kPrintHex:
      console_.WriteStdout(rv32 ? std::format("{:#010x}", a0)
                                : std::format("{:#018x}", a0));
      return {};
    case kPrintBin:
      console_.WriteStdout(rv32 ? std::format("{:032b}", a0)
                                : std::format("{:064b}", a0));
      return {};
    case kPrintChar:
      console_.WriteStdout(std::string(1, static_cast<char>(a0 & 0xFF)));
      return {};
    case kPrintString: {
      std::string text;
      for (uint64_t i = 0; i < kMaxStringLength; ++i) {
        auto byte = bus.Read8((a0 + i) & state.XlenMask());
        if (!byte) {
          return {EcallAction::kFault, 0,
                  std::format("PrintString: {} at {:#x}",
                              ToString(byte.error().cause), byte.error().tval)};
        }
        if (*byte == 0) {
          break;
        }
        text.push_back(static_cast<char>(*byte));
      }
      console_.WriteStdout(text);
      return {};
    }
    case kReadInt: {
      auto line = console_.ReadLine();
      if (!line) {
        return {EcallAction::kWaitForInput, 0, {}};
      }
      const auto value = ParseInt(*line);
      if (!value) {
        console_.WriteStderr(std::format(
            "ReadInt: '{}' is not an integer; using 0\n", Trim(*line)));
      }
      state.WriteReg(reg::kA0, static_cast<uint64_t>(value.value_or(0)));
      return {};
    }
    case kReadString: {
      const int64_t max_length = state.ReadSigned(reg::kA1);
      if (max_length < 1) {
        return {};
      }
      auto line = console_.ReadLine();
      if (!line) {
        return {EcallAction::kWaitForInput, 0, {}};
      }
      const size_t count =
          std::min<size_t>(line->size(), static_cast<size_t>(max_length - 1));
      for (size_t i = 0; i <= count; ++i) {
        const uint8_t byte = i < count ? static_cast<uint8_t>((*line)[i]) : 0;
        auto stored = bus.Write8((a0 + i) & state.XlenMask(), byte);
        if (!stored) {
          return {
              EcallAction::kFault, 0,
              std::format("ReadString: {} at {:#x}",
                          ToString(stored.error().cause), stored.error().tval)};
        }
      }
      return {};
    }
    case kReadChar: {
      auto c = console_.ReadChar();
      if (!c) {
        return {EcallAction::kWaitForInput, 0, {}};
      }
      state.WriteReg(reg::kA0, static_cast<uint64_t>(static_cast<int64_t>(*c)));
      return {};
    }
    case kSbrk: {
      const int64_t increment = state.ReadSigned(reg::kA0);
      const uint64_t old_break = (program_break_ + 7) & ~uint64_t{7};
      const uint64_t new_break = old_break + static_cast<uint64_t>(increment);
      if (increment < 0 || (new_break & ~state.XlenMask()) != 0) {
        return {EcallAction::kFault, 0,
                std::format("Sbrk: invalid increment {}", increment)};
      }
      program_break_ = new_break;
      state.WriteReg(reg::kA0, old_break);
      return {};
    }
    case kExit:
      return {EcallAction::kExit, 0, {}};
    case kExit2:
      return {EcallAction::kExit, state.ReadSigned(reg::kA0), {}};
    default:
      return {EcallAction::kFault, 0,
              std::format("unsupported ecall service a7={}", service)};
  }
}

}  // namespace rvsim
