#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "simulator/env/environment.h"

namespace rvsim {

// In-memory ConsoleIo: captures output into strings and serves input from a
// queue fed by PushInput(). Not thread-safe; the owner synchronizes access.
class BufferedConsole final : public ConsoleIo {
 public:
  void WriteStdout(std::string_view text) override;
  void WriteStderr(std::string_view text) override;
  std::optional<std::string> ReadLine() override;
  std::optional<int> ReadChar() override;

  // Appends raw input bytes (typically a line ending in '\n').
  void PushInput(std::string_view text) { input_.append(text); }
  // Marks end-of-file: pending reads return EOF instead of waiting.
  void CloseInput() { input_closed_ = true; }

  const std::string& stdout_text() const { return stdout_; }
  const std::string& stderr_text() const { return stderr_; }
  // Output interleaved in the order it was written.
  const std::string& combined_text() const { return combined_; }

  // Appends text to the combined transcript only (e.g. echoed stdin).
  void AppendTranscript(std::string_view text) { combined_.append(text); }
  // Keeps at most the last `max_bytes` of each output buffer.
  void TrimOutput(size_t max_bytes);
  bool has_pending_input() const { return !input_.empty(); }

  void ClearOutput();
  void Reset();

 private:
  std::string stdout_;
  std::string stderr_;
  std::string combined_;
  std::string input_;
  bool input_closed_ = false;
};

}  // namespace rvsim
