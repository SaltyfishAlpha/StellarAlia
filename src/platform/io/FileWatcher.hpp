#pragma once

#include <filesystem>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

namespace StellarAlia::Platform {

// Watches a directory tree for file changes and reports them to the main thread
// via PollChanges(). Safe to call Watch/Stop repeatedly.
//
// Windows: uses ReadDirectoryChangesW on a background thread.
// Non-Windows: stub (no-op Watch, empty PollChanges).
class FileWatcher {
public:
    FileWatcher() = default;
    ~FileWatcher() { Stop(); }

    FileWatcher(const FileWatcher&)            = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // Start watching dir recursively. Stops any previous watch session first.
    void Watch(const std::filesystem::path& dir);

    // Stop the background thread. Blocks until the thread exits.
    void Stop();

    // Drain all pending changed paths into out. Call once per frame from main thread.
    void PollChanges(std::vector<std::filesystem::path>& out);

private:
    std::thread m_thread;
    std::mutex  m_mutex;
    std::vector<std::filesystem::path> m_pending;
    std::atomic<bool> m_running{false};
    void* m_stopEvent = nullptr; // HANDLE on Windows
};

} // namespace StellarAlia::Platform
