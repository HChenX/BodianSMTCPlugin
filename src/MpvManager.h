#pragma once
#include "Common.h"
#include <atomic>

namespace BodianSMTC {

typedef struct mpv_handle mpv_handle;

typedef enum mpv_format {
    MPV_FORMAT_NONE             = 0,
    MPV_FORMAT_STRING           = 1,
    MPV_FORMAT_OSD_STRING       = 2,
    MPV_FORMAT_FLAG             = 3,
    MPV_FORMAT_INT64            = 4,
    MPV_FORMAT_DOUBLE           = 5,
    MPV_FORMAT_NODE             = 6,
    MPV_FORMAT_NODE_ARRAY       = 7,
    MPV_FORMAT_NODE_MAP         = 8,
    MPV_FORMAT_BYTE_ARRAY       = 9
} mpv_format;

class MpvManager {
public:
    static MpvManager& Instance();

    bool InitializeHooks();
    void ShutdownHooks();

    void OnMpvCreated(mpv_handle* handle);
    void OnMpvDestroyed(mpv_handle* handle);

    mpv_handle* GetActiveHandle();

    bool GetPosition(double& outPosSec);
    bool GetDuration(double& outDurSec);
    bool IsPaused(bool& outPaused);
    bool Seek(double posSec);

private:
    MpvManager() = default;
    ~MpvManager();

    MpvManager(const MpvManager&) = delete;
    MpvManager& operator=(const MpvManager&) = delete;

    std::atomic<mpv_handle*> m_activeHandle{ nullptr };
    std::mutex m_mutex;
    bool m_hooksInstalled = false;
};

} // namespace BodianSMTC
