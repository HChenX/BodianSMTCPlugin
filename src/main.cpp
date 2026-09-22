#include "Common.h"
#include "SmtcManager.h"
#include "MetadataWatcher.h"
#include "MpvManager.h"

typedef void* FlutterDesktopPluginRegistrarRef;
typedef void* FlutterDesktopViewRef;
typedef void (*pfn_OriginalRegister)(FlutterDesktopPluginRegistrarRef);
typedef FlutterDesktopViewRef (*pfn_FlutterDesktopPluginRegistrarGetView)(FlutterDesktopPluginRegistrarRef);
typedef HWND (*pfn_FlutterDesktopViewGetHWND)(FlutterDesktopViewRef);

static HMODULE g_hOrigDll = nullptr;
static pfn_OriginalRegister g_pfnOrigRegister = nullptr;

static HMODULE GetCurrentModuleHandle() {
    HMODULE hMod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetCurrentModuleHandle, &hMod);
    return hMod;
}

static bool LoadOriginalPlugin() {
    if (g_pfnOrigRegister) return true;

    wchar_t modPath[MAX_PATH] = {};
    GetModuleFileNameW(GetCurrentModuleHandle(), modPath, MAX_PATH);
    PathRemoveFileSpecW(modPath);

    std::wstring origPath = std::wstring(modPath) + L"\\media_key_detector_windows_plugin_orig.dll";
    g_hOrigDll = LoadLibraryW(origPath.c_str());
    if (!g_hOrigDll) {
        BodianSMTC::Log(L"Could not load orig dll: " + origPath);
        return false;
    }

    g_pfnOrigRegister = (pfn_OriginalRegister)GetProcAddress(g_hOrigDll, "MediaKeyDetectorWindowsRegisterWithRegistrar");
    if (!g_pfnOrigRegister) {
        BodianSMTC::Log(L"Could not find MediaKeyDetectorWindowsRegisterWithRegistrar in orig dll");
        return false;
    }

    BodianSMTC::Log(L"Successfully loaded original plugin DLL");
    return true;
}

static HWND FindBodianMainWindow() {
    DWORD currentPid = GetCurrentProcessId();
    HWND hwndFound = nullptr;

    // 1. Enumerate top-level windows for this process
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        auto pTarget = (HWND*)lParam;

        if (pid == GetCurrentProcessId()) {
            wchar_t cls[256] = {};
            GetClassNameW(hwnd, cls, 256);
            if (_wcsicmp(cls, L"BODIAN_FLUTTER_WIN32_WINDOW") == 0) {
                *pTarget = hwnd;
                return FALSE; // Found exact match
            }
            if (_wcsicmp(cls, L"FLUTTER_RUNNER_WIN32_WINDOW") == 0) {
                *pTarget = hwnd;
                return FALSE;
            }
        }
        return TRUE;
    }, (LPARAM)&hwndFound);

    if (hwndFound) {
        BodianSMTC::Log(L"Found Bodian main window: 0x" + std::to_wstring((uintptr_t)hwndFound));
        return hwndFound;
    }

    // 2. Direct FindWindow check
    HWND direct = FindWindowW(L"BODIAN_FLUTTER_WIN32_WINDOW", NULL);
    if (direct) {
        DWORD pid = 0;
        GetWindowThreadProcessId(direct, &pid);
        if (pid == currentPid) {
            BodianSMTC::Log(L"Found via FindWindow BODIAN_FLUTTER_WIN32_WINDOW");
            return direct;
        }
    }

    return nullptr;
}

static UINT_PTR g_timerId = 0;
static int g_retries = 0;

static VOID CALLBACK SmtcTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    g_retries++;
    HWND hwnd = FindBodianMainWindow();
    if (hwnd) {
        KillTimer(NULL, id);
        g_timerId = 0;

        BodianSMTC::Log(L"Timer matched Bodian window on thread " + std::to_wstring(GetCurrentThreadId()));
        if (BodianSMTC::SmtcManager::Instance().InitializeOnUIThread(hwnd)) {
            BodianSMTC::MetadataWatcher::Instance().Start();
        }
    } else if (g_retries > 30) {
        KillTimer(NULL, id);
        g_timerId = 0;
        BodianSMTC::Log(L"Timer timed out looking for BODIAN_FLUTTER_WIN32_WINDOW");
    }
}

extern "C" __declspec(dllexport) void MediaKeyDetectorWindowsRegisterWithRegistrar(FlutterDesktopPluginRegistrarRef registrar) {
    BodianSMTC::Log(L"MediaKeyDetectorWindowsRegisterWithRegistrar called on thread " + std::to_wstring(GetCurrentThreadId()));

    // Ensure MPV hooks are active
    BodianSMTC::MpvManager::Instance().InitializeHooks();

    // 1. Forward to original plugin first
    if (LoadOriginalPlugin() && g_pfnOrigRegister) {
        g_pfnOrigRegister(registrar);
    }

    // 2. Set timer on the UI message loop to initialize when window is fully ready
    g_retries = 0;
    g_timerId = SetTimer(NULL, 0, 800, SmtcTimerProc);
    BodianSMTC::Log(L"Scheduled UI timer for SMTC init");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            SetCurrentProcessExplicitAppUserModelID(L"Tencent.BodianMusic.PC");
            BodianSMTC::Log(L"BodianSMTCPlugin DLL_PROCESS_ATTACH with AppUserModelID");
            // NOTE: Do NOT call InitializeHooks() here - DllMain runs under Loader Lock,
            // calling LoadLibraryW/MinHook will deadlock. Hooks init in RegisterWithRegistrar.
            break;
        case DLL_PROCESS_DETACH:
            BodianSMTC::Log(L"BodianSMTCPlugin DLL_PROCESS_DETACH");
            if (g_timerId) {
                KillTimer(NULL, g_timerId);
                g_timerId = 0;
            }
            BodianSMTC::MetadataWatcher::Instance().Stop();
            BodianSMTC::SmtcManager::Instance().Shutdown();
            BodianSMTC::MpvManager::Instance().ShutdownHooks();
            if (g_hOrigDll) {
                FreeLibrary(g_hOrigDll);
                g_hOrigDll = nullptr;
            }
            break;
    }
    return TRUE;
}
