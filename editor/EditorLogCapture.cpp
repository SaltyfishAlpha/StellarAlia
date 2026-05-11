#include "EditorLogCapture.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace StellarAlia::Editor {

std::vector<LogEntry> EditorLogSink::Drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(m_pending, {});
}

void EditorLogSink::sink_it_(const spdlog::details::log_msg& msg) {
    if (m_pending.size() >= kMaxPending)
        return;

    LogEntry e;
    e.level      = msg.level;
    e.message    = std::string(msg.payload.begin(), msg.payload.end());
    e.loggerName = std::string(msg.logger_name.begin(), msg.logger_name.end());

    // Format wall-clock time as HH:MM:SS
    auto secs  = std::chrono::time_point_cast<std::chrono::seconds>(msg.time);
    auto tt    = static_cast<time_t>(secs.time_since_epoch().count());
    struct tm  tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &tt);
#else
    localtime_r(&tt, &tm_buf);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    e.timeStr = buf;

    m_pending.push_back(std::move(e));
}

EditorLogCapture::EditorLogCapture() {
    m_sink = std::make_shared<EditorLogSink>();
    // Plain pattern — no ANSI colour codes (ImGui handles colouring).
    m_sink->set_pattern("%v");
    spdlog::default_logger()->sinks().push_back(m_sink);
}

EditorLogCapture::~EditorLogCapture() {
    auto& sinks = spdlog::default_logger()->sinks();
    auto  it    = std::find(sinks.begin(), sinks.end(),
                            std::static_pointer_cast<spdlog::sinks::sink>(m_sink));
    if (it != sinks.end())
        sinks.erase(it);
}

} // namespace StellarAlia::Editor
