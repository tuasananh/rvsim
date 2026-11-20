#include "simulator/env/buffered_console.h"

namespace rvsim {

void BufferedConsole::WriteStdout(std::string_view text) {
  stdout_.append(text);
  combined_.append(text);
}

void BufferedConsole::WriteStderr(std::string_view text) {
  stderr_.append(text);
  combined_.append(text);
}

std::optional<std::string> BufferedConsole::ReadLine() {
  const size_t newline = input_.find('\n');
  if (newline == std::string::npos) {
    if (!input_closed_) {
      return std::nullopt;
    }
    std::string rest = std::move(input_);
    input_.clear();
    return rest;
  }
  std::string line = input_.substr(0, newline + 1);
  input_.erase(0, newline + 1);
  return line;
}

std::optional<int> BufferedConsole::ReadChar() {
  if (input_.empty()) {
    if (input_closed_) {
      return -1;
    }
    return std::nullopt;
  }
  const auto c = static_cast<unsigned char>(input_.front());
  input_.erase(0, 1);
  return static_cast<int>(c);
}

void BufferedConsole::TrimOutput(size_t max_bytes) {
  for (std::string* buffer : {&stdout_, &stderr_, &combined_}) {
    if (buffer->size() > max_bytes) {
      buffer->erase(0, buffer->size() - max_bytes);
    }
  }
}

void BufferedConsole::ClearOutput() {
  stdout_.clear();
  stderr_.clear();
  combined_.clear();
}

void BufferedConsole::Reset() {
  ClearOutput();
  input_.clear();
  input_closed_ = false;
}

}  // namespace rvsim
