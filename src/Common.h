#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>

// WinRT Headers
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Storage.Streams.h>
#include <systemmediatransportcontrolsinterop.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "windowsapp.lib")

namespace BodianSMTC {

struct TrackInfo {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring coverPath;
    int64_t durationMs = 0;
    int64_t positionMs = 0;
    bool isPlaying = false;
};

inline void Log(const std::wstring& msg) {
    OutputDebugStringW((L"[BodianSMTC] " + msg + L"\n").c_str());
    try {
        static std::wstring logPath;
        if (logPath.empty()) {
            wchar_t modPath[MAX_PATH];
            GetModuleFileNameW(nullptr, modPath, MAX_PATH);
            PathRemoveFileSpecW(modPath);
            logPath = std::wstring(modPath) + L"\\smtc_plugin.log";
        }
        std::ofstream ofs(logPath, std::ios::app | std::ios::binary);
        if (ofs.is_open()) {
            int len = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                std::string u8(len - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, &u8[0], len, nullptr, nullptr);
                ofs << "[BodianSMTC] " << u8 << "\r\n";
            }
        }
    } catch (...) {}
}

inline void LogA(const std::string& msg) {
    int len = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
    if (len > 0) {
        std::wstring wmsg(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, &wmsg[0], len);
        Log(wmsg);
    }
}

} // namespace BodianSMTC
