#include "platform/io/FileWatcher.hpp"
#include "core/logs/Log.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#endif

namespace StellarAlia::Platform {

void FileWatcher::Watch(const std::filesystem::path& dir) {
    Stop();
    if (dir.empty() || !std::filesystem::exists(dir)) return;

#ifdef _WIN32
    HANDLE stopEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_stopEvent = static_cast<void*>(stopEv);
    m_running   = true;

    m_thread = std::thread([this, dir, stopEv]() {
        HANDLE hDir = CreateFileW(
            dir.wstring().c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (hDir == INVALID_HANDLE_VALUE) {
            SA_LOG_ERROR("[FileWatcher] Failed to open '{}' (error {})",
                         dir.string(), GetLastError());
            m_running = false;
            return;
        }

        alignas(DWORD) char buf[8192];
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        HANDLE waitHandles[2] = { ov.hEvent, stopEv };

        while (m_running.load()) {
            ResetEvent(ov.hEvent);
            DWORD bytes = 0;
            ReadDirectoryChangesW(
                hDir, buf, sizeof(buf),
                TRUE, // watch subtree
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                &bytes, &ov, nullptr);

            DWORD res = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (res == WAIT_OBJECT_0) {
                if (!GetOverlappedResult(hDir, &ov, &bytes, FALSE) || bytes == 0)
                    continue;

                auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buf);
                std::lock_guard<std::mutex> lk(m_mutex);
                while (true) {
                    std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
                    m_pending.push_back(dir / name);
                    if (!info->NextEntryOffset) break;
                    info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                        reinterpret_cast<char*>(info) + info->NextEntryOffset);
                }
            } else {
                CancelIo(hDir);
                break;
            }
        }

        CloseHandle(ov.hEvent);
        CloseHandle(hDir);
    });
#endif
}

void FileWatcher::Stop() {
    m_running = false;
#ifdef _WIN32
    if (m_stopEvent) {
        SetEvent(static_cast<HANDLE>(m_stopEvent));
    }
#endif
    if (m_thread.joinable()) m_thread.join();
#ifdef _WIN32
    if (m_stopEvent) {
        CloseHandle(static_cast<HANDLE>(m_stopEvent));
        m_stopEvent = nullptr;
    }
#endif
    std::lock_guard<std::mutex> lk(m_mutex);
    m_pending.clear();
}

void FileWatcher::PollChanges(std::vector<std::filesystem::path>& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    out.insert(out.end(), m_pending.begin(), m_pending.end());
    m_pending.clear();
}

} // namespace StellarAlia::Platform
