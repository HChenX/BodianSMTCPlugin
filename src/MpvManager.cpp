#include "MpvManager.h"
#include "minhook/MinHook.h"
#include <iomanip>
#include <sstream>

namespace BodianSMTC {

typedef mpv_handle* (*pfn_mpv_create)(void);
typedef void (*pfn_mpv_destroy)(mpv_handle*);
typedef void (*pfn_mpv_terminate_destroy)(mpv_handle*);
typedef int (*pfn_mpv_get_property)(mpv_handle*, const char*, mpv_format, void*);
typedef int (*pfn_mpv_set_property)(mpv_handle*, const char*, mpv_format, void*);
typedef int (*pfn_mpv_command)(mpv_handle*, const char**);
typedef int (*pfn_mpv_command_string)(mpv_handle*, const char*);

typedef void (*pfn_MediaKitRegister)(int64_t handle, void* post_c_object, int64_t send_port);
typedef void (*pfn_MediaKitDispose)(int64_t handle);

static pfn_mpv_create g_orig_mpv_create = nullptr;
static pfn_mpv_destroy g_orig_mpv_destroy = nullptr;
static pfn_mpv_terminate_destroy g_orig_mpv_terminate_destroy = nullptr;
static pfn_mpv_get_property g_pfn_mpv_get_property = nullptr;
static pfn_mpv_set_property g_pfn_mpv_set_property = nullptr;
static pfn_mpv_command g_pfn_mpv_command = nullptr;
static pfn_mpv_command_string g_pfn_mpv_command_string = nullptr;

static pfn_MediaKitRegister g_orig_MediaKitRegister = nullptr;
static pfn_MediaKitDispose g_orig_MediaKitDispose = nullptr;

static mpv_handle* Hook_mpv_create() {
    mpv_handle* handle = nullptr;
    if (g_orig_mpv_create) {
        handle = g_orig_mpv_create();
    }
    if (handle) {
        std::wstringstream ss;
        ss << L"Hook_mpv_create captured handle: 0x" << std::hex << (uintptr_t)handle;
        Log(ss.str());
        MpvManager::Instance().OnMpvCreated(handle);
    }
    return handle;
}

static void Hook_mpv_destroy(mpv_handle* handle) {
    std::wstringstream ss;
    ss << L"Hook_mpv_destroy handle: 0x" << std::hex << (uintptr_t)handle;
    Log(ss.str());
    MpvManager::Instance().OnMpvDestroyed(handle);
    if (g_orig_mpv_destroy) {
        g_orig_mpv_destroy(handle);
    }
}

static void Hook_mpv_terminate_destroy(mpv_handle* handle) {
    std::wstringstream ss;
    ss << L"Hook_mpv_terminate_destroy handle: 0x" << std::hex << (uintptr_t)handle;
    Log(ss.str());
    MpvManager::Instance().OnMpvDestroyed(handle);
    if (g_orig_mpv_terminate_destroy) {
        g_orig_mpv_terminate_destroy(handle);
    }
}

static void Hook_MediaKitRegister(int64_t handle, void* post_c_object, int64_t send_port) {
    std::wstringstream ss;
    ss << L"Hook_MediaKitRegister captured handle: 0x" << std::hex << handle;
    Log(ss.str());
    MpvManager::Instance().OnMpvCreated(reinterpret_cast<mpv_handle*>(handle));
    if (g_orig_MediaKitRegister) {
        g_orig_MediaKitRegister(handle, post_c_object, send_port);
    }
}

static void Hook_MediaKitDispose(int64_t handle) {
    std::wstringstream ss;
    ss << L"Hook_MediaKitDispose handle: 0x" << std::hex << handle;
    Log(ss.str());
    MpvManager::Instance().OnMpvDestroyed(reinterpret_cast<mpv_handle*>(handle));
    if (g_orig_MediaKitDispose) {
        g_orig_MediaKitDispose(handle);
    }
}

MpvManager& MpvManager::Instance() {
    static MpvManager s_instance;
    return s_instance;
}

MpvManager::~MpvManager() {
    ShutdownHooks();
}

bool MpvManager::InitializeHooks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hooksInstalled) return true;

    if (MH_Initialize() != MH_OK) {
        Log(L"MH_Initialize failed");
        return false;
    }

    // 1. Hook libmpv-2.dll (already loaded by Flutter/media_kit)
    HMODULE hMpv = GetModuleHandleW(L"libmpv-2.dll");

    if (hMpv) {
        g_pfn_mpv_get_property = (pfn_mpv_get_property)GetProcAddress(hMpv, "mpv_get_property");
        g_pfn_mpv_set_property = (pfn_mpv_set_property)GetProcAddress(hMpv, "mpv_set_property");
        g_pfn_mpv_command = (pfn_mpv_command)GetProcAddress(hMpv, "mpv_command");
        g_pfn_mpv_command_string = (pfn_mpv_command_string)GetProcAddress(hMpv, "mpv_command_string");

        void* pMpvCreate = (void*)GetProcAddress(hMpv, "mpv_create");
        void* pMpvDestroy = (void*)GetProcAddress(hMpv, "mpv_destroy");
        void* pMpvTermDestroy = (void*)GetProcAddress(hMpv, "mpv_terminate_destroy");

        if (pMpvCreate) {
            MH_CreateHook(pMpvCreate, (LPVOID)&Hook_mpv_create, (LPVOID*)&g_orig_mpv_create);
        }
        if (pMpvDestroy) {
            MH_CreateHook(pMpvDestroy, (LPVOID)&Hook_mpv_destroy, (LPVOID*)&g_orig_mpv_destroy);
        }
        if (pMpvTermDestroy) {
            MH_CreateHook(pMpvTermDestroy, (LPVOID)&Hook_mpv_terminate_destroy, (LPVOID*)&g_orig_mpv_terminate_destroy);
        }
        Log(L"libmpv-2.dll exports resolved and hooks created");
    } else {
        Log(L"libmpv-2.dll not loaded yet - hooks will rely on MediaKit event loop");
    }

    // 2. Hook media_kit_native_event_loop.dll (already loaded by Flutter)
    HMODULE hLoop = GetModuleHandleW(L"media_kit_native_event_loop.dll");

    if (hLoop) {
        void* pRegister = (void*)GetProcAddress(hLoop, "MediaKitEventLoopHandlerRegister");
        void* pDispose = (void*)GetProcAddress(hLoop, "MediaKitEventLoopHandlerDispose");

        if (pRegister) {
            MH_CreateHook(pRegister, (LPVOID)&Hook_MediaKitRegister, (LPVOID*)&g_orig_MediaKitRegister);
        }
        if (pDispose) {
            MH_CreateHook(pDispose, (LPVOID)&Hook_MediaKitDispose, (LPVOID*)&g_orig_MediaKitDispose);
        }
        Log(L"media_kit_native_event_loop.dll exports resolved and hooks created");
    } else {
        Log(L"media_kit_native_event_loop.dll not found yet");
    }

    MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    if (status == MH_OK) {
        m_hooksInstalled = true;
        Log(L"All MPV and MediaKit hooks enabled successfully via MinHook");
        return true;
    } else {
        std::wstringstream ss;
        ss << L"MH_EnableHook returned status " << status;
        Log(ss.str());
        return false;
    }
}

void MpvManager::ShutdownHooks() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hooksInstalled) return;

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    m_hooksInstalled = false;
    m_activeHandle.store(nullptr);
    Log(L"MpvManager hooks disabled and uninitialized");
}

void MpvManager::OnMpvCreated(mpv_handle* handle) {
    if (handle) {
        m_activeHandle.store(handle);
    }
}

void MpvManager::OnMpvDestroyed(mpv_handle* handle) {
    mpv_handle* cur = m_activeHandle.load();
    if (cur == handle) {
        m_activeHandle.store(nullptr);
    }
}

mpv_handle* MpvManager::GetActiveHandle() {
    return m_activeHandle.load();
}

bool MpvManager::GetPosition(double& outPosSec) {
    mpv_handle* h = m_activeHandle.load();
    if (!h || !g_pfn_mpv_get_property) return false;

    double pos = 0.0;
    int err = g_pfn_mpv_get_property(h, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    if (err >= 0) {
        outPosSec = pos;
        return true;
    }
    return false;
}

bool MpvManager::GetDuration(double& outDurSec) {
    mpv_handle* h = m_activeHandle.load();
    if (!h || !g_pfn_mpv_get_property) return false;

    double dur = 0.0;
    int err = g_pfn_mpv_get_property(h, "duration", MPV_FORMAT_DOUBLE, &dur);
    if (err >= 0 && dur > 0.0) {
        outDurSec = dur;
        return true;
    }
    return false;
}

bool MpvManager::IsPaused(bool& outPaused) {
    mpv_handle* h = m_activeHandle.load();
    if (!h || !g_pfn_mpv_get_property) return false;

    int flag = 0;
    int err = g_pfn_mpv_get_property(h, "pause", MPV_FORMAT_FLAG, &flag);
    if (err >= 0) {
        outPaused = (flag != 0);
        return true;
    }
    return false;
}

bool MpvManager::Seek(double posSec) {
    mpv_handle* h = m_activeHandle.load();
    if (!h || !g_pfn_mpv_set_property) return false;

    int err = g_pfn_mpv_set_property(h, "time-pos", MPV_FORMAT_DOUBLE, &posSec);
    std::wstringstream ss;
    ss << L"MpvManager::Seek to " << posSec << L"s, err=" << err;
    Log(ss.str());
    return (err >= 0);
}

} // namespace BodianSMTC
