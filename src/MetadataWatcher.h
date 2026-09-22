#pragma once
#include "Common.h"
#include <atomic>
#include <thread>

namespace BodianSMTC {

class MetadataWatcher {
public:
    static MetadataWatcher& Instance();

    void Start();
    void Stop();

private:
    MetadataWatcher() = default;
    ~MetadataWatcher();

    MetadataWatcher(const MetadataWatcher&) = delete;
    MetadataWatcher& operator=(const MetadataWatcher&) = delete;

    void WatchLoop();
    bool ReadLatestSong(TrackInfo& outTrack, int64_t& outOrd);
    bool CheckAudioPlaying();

    std::atomic<bool> m_running{ false };
    std::thread m_workerThread;
    int64_t m_lastOrd = -1;
    bool m_lastPlayingState = false;
    std::wstring m_dbPath;
    std::wstring m_artworkDir;

    int64_t m_lastReportedPosMs = 0;
    std::chrono::steady_clock::time_point m_lastReportedTime{};
    std::chrono::steady_clock::time_point m_lastSyncTime{};
};

} // namespace BodianSMTC
