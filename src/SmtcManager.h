#pragma once
#include "Common.h"

namespace BodianSMTC {

class SmtcManager {
public:
    static SmtcManager& Instance();

    // Call on the window's UI thread
    bool InitializeOnUIThread(HWND hwnd);
    void Shutdown();

    void UpdateTrack(const TrackInfo& track);
    void UpdatePlaybackState(bool isPlaying);
    void UpdatePosition(int64_t positionMs, int64_t durationMs = -1);

    bool IsInitialized() const { return m_initialized; }
    HWND GetHwnd() const { return m_hwnd; }

private:
    SmtcManager() = default;
    ~SmtcManager();

    SmtcManager(const SmtcManager&) = delete;
    SmtcManager& operator=(const SmtcManager&) = delete;

    HWND m_hwnd = nullptr;
    bool m_initialized = false;
    std::mutex m_mutex;
    TrackInfo m_currentTrack;

    winrt::Windows::Media::SystemMediaTransportControls m_smtc{ nullptr };
    winrt::event_token m_buttonToken{};
    winrt::event_token m_positionChangeToken{};
};

} // namespace BodianSMTC
