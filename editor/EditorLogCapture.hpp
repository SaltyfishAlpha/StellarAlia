#pragma once

#include <spdlog/sinks/base_sink.h>
#include <mutex>
#include <vector>
#include <string>
#include <memory>

namespace StellarAlia::Editor {

struct LogEntry {
    spdlog::level::level_enum level;
    std::string               timeStr;   // "HH:MM:SS"
    std::string               message;   // raw user payload
};

// Custom spdlog sink: collects entries into m_pending under base_sink's mutex.
// Main thread calls Drain() each frame to swap out pending entries.
class EditorLogSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    std::vector<LogEntry> Drain();

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}

private:
    std::vector<LogEntry> m_pending;
    static constexpr size_t kMaxPending = 2000;
};

// RAII wrapper: injects the sink into spdlog's default logger on construction,
// removes it on destruction.
class EditorLogCapture {
public:
    EditorLogCapture();
    ~EditorLogCapture();

    [[nodiscard]] std::shared_ptr<EditorLogSink> GetSink() const { return m_sink; }

private:
    std::shared_ptr<EditorLogSink> m_sink;
};

} // namespace StellarAlia::Editor
