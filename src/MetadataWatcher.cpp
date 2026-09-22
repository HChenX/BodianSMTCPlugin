#include "MetadataWatcher.h"
#include "SmtcManager.h"
#include "MpvManager.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wincrypt.h>
#include <shlobj.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace BodianSMTC {

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

typedef int (*pfn_sqlite3_open_v2)(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
typedef int (*pfn_sqlite3_prepare_v2)(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
typedef int (*pfn_sqlite3_step)(sqlite3_stmt*);
typedef const unsigned char *(*pfn_sqlite3_column_text)(sqlite3_stmt*, int iCol);
typedef __int64 (*pfn_sqlite3_column_int64)(sqlite3_stmt*, int iCol);
typedef int (*pfn_sqlite3_finalize)(sqlite3_stmt *pStmt);
typedef int (*pfn_sqlite3_close)(sqlite3*);

static pfn_sqlite3_open_v2 g_sqlite3_open_v2 = nullptr;
static pfn_sqlite3_prepare_v2 g_sqlite3_prepare_v2 = nullptr;
static pfn_sqlite3_step g_sqlite3_step = nullptr;
static pfn_sqlite3_column_text g_sqlite3_column_text = nullptr;
static pfn_sqlite3_column_int64 g_sqlite3_column_int64 = nullptr;
static pfn_sqlite3_finalize g_sqlite3_finalize = nullptr;
static pfn_sqlite3_close g_sqlite3_close = nullptr;

static bool LoadSqlite() {
    if (g_sqlite3_open_v2) return true;
    HMODULE hSql = GetModuleHandleW(L"sqlite3.dll");
    if (!hSql) {
        hSql = LoadLibraryW(L"sqlite3.dll");
    }
    if (!hSql) {
        Log(L"Failed to load sqlite3.dll");
        return false;
    }
    g_sqlite3_open_v2 = (pfn_sqlite3_open_v2)GetProcAddress(hSql, "sqlite3_open_v2");
    g_sqlite3_prepare_v2 = (pfn_sqlite3_prepare_v2)GetProcAddress(hSql, "sqlite3_prepare_v2");
    g_sqlite3_step = (pfn_sqlite3_step)GetProcAddress(hSql, "sqlite3_step");
    g_sqlite3_column_text = (pfn_sqlite3_column_text)GetProcAddress(hSql, "sqlite3_column_text");
    g_sqlite3_column_int64 = (pfn_sqlite3_column_int64)GetProcAddress(hSql, "sqlite3_column_int64");
    g_sqlite3_finalize = (pfn_sqlite3_finalize)GetProcAddress(hSql, "sqlite3_finalize");
    g_sqlite3_close = (pfn_sqlite3_close)GetProcAddress(hSql, "sqlite3_close");

    return (g_sqlite3_open_v2 && g_sqlite3_prepare_v2 && g_sqlite3_step && g_sqlite3_column_text && g_sqlite3_close);
}

static std::string ComputeMD5(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (const BYTE*)input.c_str(), (DWORD)input.length(), 0)) {
                BYTE hash[16];
                DWORD cbHash = 16;
                if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &cbHash, 0)) {
                    char hex[33] = {};
                    for (int i = 0; i < 16; i++) {
                        sprintf_s(hex + i * 2, 3, "%02x", hash[i]);
                    }
                    result = hex;
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (count <= 0) return L"";
    std::wstring wstr(count - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], count);
    return wstr;
}

// Simple JSON string value extractor
static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.length();
    size_t end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// Simple JSON int value extractor
static int64_t ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return 0;
    pos += pattern.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = pos;
    while (end < json.length() && (isdigit(json[end]) || json[end] == '-')) end++;
    if (end == pos) return 0;
    return _atoi64(json.substr(pos, end - pos).c_str());
}

MetadataWatcher& MetadataWatcher::Instance() {
    static MetadataWatcher s_instance;
    return s_instance;
}

MetadataWatcher::~MetadataWatcher() {
    Stop();
}

void MetadataWatcher::Start() {
    if (m_running) return;

    // Resolve directories
    wchar_t localAppData[MAX_PATH] = {};
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData) == S_OK) {
        m_dbPath = std::wstring(localAppData) + L"\\cn.wenyu.bodian\\bodian_pc\\database\\songDB.db";
    }

    wchar_t roamingAppData[MAX_PATH] = {};
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, roamingAppData) == S_OK) {
        m_artworkDir = std::wstring(roamingAppData) + L"\\cn.wenyu.bodian\\bodian_pc\\artwork_cache";
    }

    m_running = true;
    m_workerThread = std::thread(&MetadataWatcher::WatchLoop, this);
    Log(L"MetadataWatcher started. DB: " + m_dbPath);
}

void MetadataWatcher::Stop() {
    if (!m_running) return;
    m_running = false;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    Log(L"MetadataWatcher stopped");
}

bool MetadataWatcher::ReadLatestSong(TrackInfo& outTrack, int64_t& outOrd) {
    if (!LoadSqlite()) return false;
    if (m_dbPath.empty() || !PathFileExistsW(m_dbPath.c_str())) return false;

    // Convert to UTF-8
    char dbPathA[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, m_dbPath.c_str(), -1, dbPathA, sizeof(dbPathA), NULL, NULL);

    sqlite3* db = nullptr;
    // Open read-only
    int rc = g_sqlite3_open_v2(dbPathA, &db, 0x00000001 /* SQLITE_OPEN_READONLY */, nullptr);
    if (rc != 0 || !db) {
        if (db) g_sqlite3_close(db);
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT ord, json FROM hist_song ORDER BY ord DESC LIMIT 1;";
    rc = g_sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != 0 || !stmt) {
        g_sqlite3_close(db);
        return false;
    }

    bool found = false;
    if (g_sqlite3_step(stmt) == 100 /* SQLITE_ROW */) {
        outOrd = g_sqlite3_column_int64(stmt, 0);
        const char* jsonText = (const char*)g_sqlite3_column_text(stmt, 1);
        if (jsonText) {
            std::string jsonStr = jsonText;
            std::string name = ExtractJsonString(jsonStr, "name");
            std::string artist = ExtractJsonString(jsonStr, "artist");
            std::string album = ExtractJsonString(jsonStr, "album");
            std::string albumPic = ExtractJsonString(jsonStr, "albumPic");
            int64_t duration = ExtractJsonInt(jsonStr, "duration");

            outTrack.title = Utf8ToWide(name);
            outTrack.artist = Utf8ToWide(artist);
            outTrack.album = Utf8ToWide(album);
            outTrack.durationMs = duration * 1000;

            // Find cover image
            if (!albumPic.empty()) {
                std::string md5 = ComputeMD5(albumPic);
                std::wstring coverFile = m_artworkDir + L"\\" + Utf8ToWide(md5) + L".img";
                if (PathFileExistsW(coverFile.c_str())) {
                    outTrack.coverPath = coverFile;
                }
            }

            // Fallback: search most recent .img file in artworkDir
            if (outTrack.coverPath.empty() && !m_artworkDir.empty()) {
                WIN32_FIND_DATAW fd;
                std::wstring searchPattern = m_artworkDir + L"\\*.img";
                HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
                FILETIME latestFt = {};
                std::wstring bestFile;
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            if (CompareFileTime(&fd.ftLastWriteTime, &latestFt) > 0) {
                                latestFt = fd.ftLastWriteTime;
                                bestFile = m_artworkDir + L"\\" + fd.cFileName;
                            }
                        }
                    } while (FindNextFileW(hFind, &fd));
                    FindClose(hFind);
                }
                if (!bestFile.empty()) {
                    outTrack.coverPath = bestFile;
                }
            }

            found = true;
        }
    }

    g_sqlite3_finalize(stmt);
    g_sqlite3_close(db);
    return found;
}

bool MetadataWatcher::CheckAudioPlaying() {
    HRESULT hr = S_OK;
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioSessionManager2* pSessionManager = nullptr;
    IAudioSessionEnumerator* pSessionEnum = nullptr;
    bool isPlaying = false;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (SUCCEEDED(hr) && pEnumerator) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
        if (SUCCEEDED(hr) && pDevice) {
            hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
            if (SUCCEEDED(hr) && pSessionManager) {
                hr = pSessionManager->GetSessionEnumerator(&pSessionEnum);
                if (SUCCEEDED(hr) && pSessionEnum) {
                    int count = 0;
                    pSessionEnum->GetCount(&count);
                    DWORD currentPid = GetCurrentProcessId();

                    for (int i = 0; i < count; i++) {
                        IAudioSessionControl* pControl = nullptr;
                        IAudioSessionControl2* pControl2 = nullptr;
                        if (SUCCEEDED(pSessionEnum->GetSession(i, &pControl)) && pControl) {
                            if (SUCCEEDED(pControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pControl2)) && pControl2) {
                                DWORD pid = 0;
                                if (SUCCEEDED(pControl2->GetProcessId(&pid)) && pid == currentPid) {
                                    AudioSessionState state;
                                    if (SUCCEEDED(pControl2->GetState(&state))) {
                                        if (state == AudioSessionStateActive) {
                                            isPlaying = true;
                                        }
                                    }
                                }
                                pControl2->Release();
                            }
                            pControl->Release();
                        }
                        if (isPlaying) break;
                    }
                    pSessionEnum->Release();
                }
                pSessionManager->Release();
            }
            pDevice->Release();
        }
        pEnumerator->Release();
    }

    return isPlaying;
}

void MetadataWatcher::WatchLoop() {
    Log(L"MetadataWatcher loop entering (with real-time MPV timeline tracking)");
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    int dbCheckCounter = 0;

    while (m_running) {
        // 1. Check real-time playback state and position from MPV
        double posSec = 0.0;
        bool hasPos = MpvManager::Instance().GetPosition(posSec);
        double durSec = 0.0;
        bool hasDur = MpvManager::Instance().GetDuration(durSec);
        bool isMpvPaused = false;
        bool hasPause = MpvManager::Instance().IsPaused(isMpvPaused);

        bool isPlaying = false;
        if (hasPos) {
            if (hasPause) {
                isPlaying = !isMpvPaused;
            } else {
                isPlaying = CheckAudioPlaying();
            }
        } else {
            isPlaying = CheckAudioPlaying();
        }

        auto now = std::chrono::steady_clock::now();

        if (hasPos) {
            int64_t posMs = static_cast<int64_t>(posSec * 1000.0);
            int64_t durMs = hasDur ? static_cast<int64_t>(durSec * 1000.0) : -1;

            bool stateChanged = (isPlaying != m_lastPlayingState);
            if (stateChanged) {
                m_lastPlayingState = isPlaying;
                Log(L"Playback state changed: " + std::wstring(isPlaying ? L"Playing" : L"Paused") + L" at " + std::to_wstring(posMs) + L" ms");
                SmtcManager::Instance().UpdatePlaybackState(isPlaying);
                SmtcManager::Instance().UpdatePosition(posMs, durMs);
                m_lastReportedPosMs = posMs;
                m_lastReportedTime = now;
                m_lastSyncTime = now;
            } else if (isPlaying) {
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastReportedTime).count();
                int64_t expectedPosMs = m_lastReportedPosMs + elapsedMs;
                int64_t drift = std::abs(posMs - expectedPosMs);

                auto sinceLastSyncMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSyncTime).count();

                // Seek detected (drift > 400ms) or periodic 1-second sync
                if (drift > 400) {
                    Log(L"Seek detected: pos=" + std::to_wstring(posMs) + L"ms, expected=" + std::to_wstring(expectedPosMs) + L"ms, drift=" + std::to_wstring(drift) + L"ms");
                    SmtcManager::Instance().UpdatePosition(posMs, durMs);
                    m_lastReportedPosMs = posMs;
                    m_lastReportedTime = now;
                    m_lastSyncTime = now;
                } else if (sinceLastSyncMs >= 1000) {
                    SmtcManager::Instance().UpdatePosition(posMs, durMs);
                    m_lastReportedPosMs = posMs;
                    m_lastReportedTime = now;
                    m_lastSyncTime = now;
                }
            }
        } else {
            bool stateChanged = (isPlaying != m_lastPlayingState);
            if (stateChanged) {
                m_lastPlayingState = isPlaying;
                Log(L"Playback state changed (no pos): " + std::wstring(isPlaying ? L"Playing" : L"Paused"));
                SmtcManager::Instance().UpdatePlaybackState(isPlaying);
            }
        }

        // 2. Periodically check SQLite database for song changes (every 600ms = 3 ticks)
        if (++dbCheckCounter >= 3) {
            dbCheckCounter = 0;
            TrackInfo track;
            int64_t ord = -1;
            if (ReadLatestSong(track, ord)) {
                if (ord != m_lastOrd) {
                    m_lastOrd = ord;
                    track.isPlaying = isPlaying;
                    if (hasPos) track.positionMs = static_cast<int64_t>(posSec * 1000.0);
                    if (hasDur) track.durationMs = static_cast<int64_t>(durSec * 1000.0);

                    SmtcManager::Instance().UpdateTrack(track);
                    m_lastReportedPosMs = track.positionMs;
                    m_lastReportedTime = now;
                    m_lastSyncTime = now;
                    Log(L"New song detected: " + track.title + L" - " + track.artist);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    CoUninitialize();
    Log(L"MetadataWatcher loop exiting");
}

} // namespace BodianSMTC
