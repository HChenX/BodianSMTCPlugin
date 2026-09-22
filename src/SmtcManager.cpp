#include "SmtcManager.h"
#include "MediaController.h"
#include "MpvManager.h"

namespace BodianSMTC {

static const wchar_t* const kAppUserModelId = L"Tencent.BodianMusic.PC";
static const wchar_t* const kAppDisplayName = L"波点音乐";

static void RegisterAppUserModel() {
    // 1. Register in HKCU Classes\AppUserModelId
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\AppUserModelId\\Tencent.BodianMusic.PC", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, (const BYTE*)kAppDisplayName, (DWORD)(wcslen(kAppDisplayName) + 1) * sizeof(wchar_t));
        
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring iconUri = std::wstring(exePath) + L",0";
        RegSetValueExW(hKey, L"IconUri", 0, REG_SZ, (const BYTE*)iconUri.c_str(), (DWORD)(iconUri.length() + 1) * sizeof(wchar_t));

        DWORD show = 1;
        RegSetValueExW(hKey, L"ShowInSettings", 0, REG_DWORD, (const BYTE*)&show, sizeof(show));
        RegCloseKey(hKey);
        Log(L"Registered AppUserModelId in HKCU");
    }

    // 2. Create Start Menu shortcut with System.AppUserModel.ID
    wchar_t programsPath[MAX_PATH] = {};
    if (SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, programsPath) == S_OK) {
        std::wstring linkPath = std::wstring(programsPath) + L"\\波点音乐.lnk";
        IShellLinkW* psl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl)))) {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            wchar_t dirPath[MAX_PATH] = {};
            wcscpy_s(dirPath, exePath);
            PathRemoveFileSpecW(dirPath);

            psl->SetPath(exePath);
            psl->SetWorkingDirectory(dirPath);
            psl->SetIconLocation(exePath, 0);

            IPropertyStore* pps = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&pps)))) {
                PROPVARIANT pv;
                if (SUCCEEDED(InitPropVariantFromString(kAppUserModelId, &pv))) {
                    pps->SetValue(PKEY_AppUserModel_ID, pv);
                    PropVariantClear(&pv);
                    pps->Commit();
                }
                pps->Release();
            }

            IPersistFile* ppf = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf)))) {
                ppf->Save(linkPath.c_str(), TRUE);
                ppf->Release();
                Log(L"Created/Updated Start Menu shortcut with AppUserModelID: " + linkPath);
            }
            psl->Release();
        }
    }
}

static void SetWindowAppId(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;
    IPropertyStore* pStore = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&pStore)))) {
        PROPVARIANT pv;
        if (SUCCEEDED(InitPropVariantFromString(kAppUserModelId, &pv))) {
            pStore->SetValue(PKEY_AppUserModel_ID, pv);
            PropVariantClear(&pv);
            pStore->Commit();
            Log(L"Successfully applied PKEY_AppUserModel_ID to window HWND");
        }
        pStore->Release();
    }
}

static bool ConvertWebpToJpeg(const std::wstring& srcWebp, const std::wstring& dstJpeg) {
    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr)) return false;

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(srcWebp.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr)) {
        pFactory->Release();
        return false;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        pDecoder->Release();
        pFactory->Release();
        return false;
    }

    IWICStream* pStream = nullptr;
    hr = pFactory->CreateStream(&pStream);
    if (FAILED(hr)) {
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }

    hr = pStream->InitializeFromFilename(dstJpeg.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        pStream->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }

    IWICBitmapEncoder* pEncoder = nullptr;
    hr = pFactory->CreateEncoder(GUID_ContainerFormatJpeg, NULL, &pEncoder);
    if (FAILED(hr)) {
        pStream->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }

    hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        pEncoder->Release();
        pStream->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }

    IWICBitmapFrameEncode* pFrameEncode = nullptr;
    hr = pEncoder->CreateNewFrame(&pFrameEncode, NULL);
    if (FAILED(hr)) {
        pEncoder->Release();
        pStream->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }

    hr = pFrameEncode->Initialize(NULL);
    if (FAILED(hr)) {
        pFrameEncode->Release();
        pEncoder->Release();
        pStream->Release();
        pFrame->Release();
        pDecoder->Release();
        pFactory->Release();
        return false;
    }

    hr = pFrameEncode->WriteSource(pFrame, NULL);
    if (SUCCEEDED(hr)) {
        hr = pFrameEncode->Commit();
        if (SUCCEEDED(hr)) {
            pEncoder->Commit();
        }
    }

    pFrameEncode->Release();
    pEncoder->Release();
    pStream->Release();
    pFrame->Release();
    pDecoder->Release();
    pFactory->Release();

    return SUCCEEDED(hr);
}

SmtcManager& SmtcManager::Instance() {
    static SmtcManager s_instance;
    return s_instance;
}

SmtcManager::~SmtcManager() {
    Shutdown();
}

bool SmtcManager::InitializeOnUIThread(HWND hwnd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) return true;
    if (!hwnd || !IsWindow(hwnd)) {
        Log(L"InitializeOnUIThread failed: invalid HWND");
        return false;
    }

    HWND rootHwnd = GetAncestor(hwnd, GA_ROOT);
    if (!rootHwnd) rootHwnd = hwnd;

    DWORD winPid = 0;
    DWORD winTid = GetWindowThreadProcessId(rootHwnd, &winPid);
    DWORD curTid = GetCurrentThreadId();

    std::wstringstream ss;
    ss << L"Init SMTC: hwnd=0x" << std::hex << (uintptr_t)hwnd
       << L", rootHwnd=0x" << (uintptr_t)rootHwnd
       << L", winTid=" << std::dec << winTid
       << L", curTid=" << curTid;
    Log(ss.str());

    // Register AppUserModelId in Shell & attach to HWND
    RegisterAppUserModel();
    SetWindowAppId(rootHwnd);
    if (hwnd != rootHwnd) {
        SetWindowAppId(hwnd);
    }

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (...) {}

    try {
        auto interop = winrt::get_activation_factory<winrt::Windows::Media::SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
        if (!interop) {
            Log(L"Failed to get ISystemMediaTransportControlsInterop activation factory");
            return false;
        }

        m_hwnd = rootHwnd;
        HRESULT hr = interop->GetForWindow(m_hwnd, winrt::guid_of<winrt::Windows::Media::SystemMediaTransportControls>(), winrt::put_abi(m_smtc));
        
        if (FAILED(hr) && hwnd != rootHwnd) {
            ss.str(L"");
            ss << L"GetForWindow on rootHwnd failed (0x" << std::hex << hr << L"), trying child hwnd...";
            Log(ss.str());
            m_hwnd = hwnd;
            hr = interop->GetForWindow(m_hwnd, winrt::guid_of<winrt::Windows::Media::SystemMediaTransportControls>(), winrt::put_abi(m_smtc));
        }

        if (FAILED(hr) || !m_smtc) {
            ss.str(L"");
            ss << L"GetForWindow failed with HRESULT: 0x" << std::hex << hr;
            Log(ss.str());
            return false;
        }

        m_smtc.IsPlayEnabled(true);
        m_smtc.IsPauseEnabled(true);
        m_smtc.IsNextEnabled(true);
        m_smtc.IsPreviousEnabled(true);
        m_smtc.IsStopEnabled(true);
        m_smtc.IsEnabled(true);

        m_buttonToken = m_smtc.ButtonPressed([this](auto const&, winrt::Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs const& args) {
            try {
                switch (args.Button()) {
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Play:
                        Log(L"SMTC ButtonPressed: Play");
                        MediaController::Play();
                        break;
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Pause:
                        Log(L"SMTC ButtonPressed: Pause");
                        MediaController::Pause();
                        break;
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Next:
                        Log(L"SMTC ButtonPressed: Next");
                        MediaController::Next();
                        break;
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Previous:
                        Log(L"SMTC ButtonPressed: Previous");
                        MediaController::Previous();
                        break;
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Stop:
                        Log(L"SMTC ButtonPressed: Stop");
                        MediaController::Stop();
                        break;
                    default:
                        break;
                }
            } catch (...) {
                Log(L"Exception in ButtonPressed handler");
            }
        });

        m_positionChangeToken = m_smtc.PlaybackPositionChangeRequested([this](auto const&, winrt::Windows::Media::PlaybackPositionChangeRequestedEventArgs const& args) {
            try {
                auto reqPos = args.RequestedPlaybackPosition();
                int64_t targetMs = std::chrono::duration_cast<std::chrono::milliseconds>(reqPos).count();
                Log(L"SMTC PlaybackPositionChangeRequested: " + std::to_wstring(targetMs) + L" ms");
                MpvManager::Instance().Seek(targetMs / 1000.0);
                UpdatePosition(targetMs);
            } catch (...) {
                Log(L"Exception in PlaybackPositionChangeRequested handler");
            }
        });

        // Set initial playback state
        m_smtc.PlaybackStatus(winrt::Windows::Media::MediaPlaybackStatus::Closed);

        m_initialized = true;
        Log(L"SMTC successfully initialized!");
        return true;
    } catch (const winrt::hresult_error& ex) {
        Log(L"WinRT exception during SMTC init: " + std::wstring(ex.message()));
        return false;
    } catch (...) {
        Log(L"Unknown exception during SMTC init");
        return false;
    }
}

void SmtcManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) return;

    try {
        if (m_smtc) {
            if (m_buttonToken.value != 0) {
                m_smtc.ButtonPressed(m_buttonToken);
                m_buttonToken = {};
            }
            if (m_positionChangeToken.value != 0) {
                m_smtc.PlaybackPositionChangeRequested(m_positionChangeToken);
                m_positionChangeToken = {};
            }
            m_smtc.IsEnabled(false);
            m_smtc.PlaybackStatus(winrt::Windows::Media::MediaPlaybackStatus::Closed);
            m_smtc = nullptr;
        }
    } catch (...) {}

    m_initialized = false;
    m_hwnd = nullptr;
    Log(L"SMTC Shutdown");
}

void SmtcManager::UpdateTrack(const TrackInfo& track) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_smtc) return;

    m_currentTrack = track;
    try {
        auto updater = m_smtc.DisplayUpdater();
        updater.Type(winrt::Windows::Media::MediaPlaybackType::Music);

        auto musicProps = updater.MusicProperties();
        musicProps.Title(track.title.empty() ? kAppDisplayName : track.title);
        musicProps.Artist(track.artist.empty() ? L"未知歌手" : track.artist);
        if (!track.album.empty()) {
            musicProps.AlbumTitle(track.album);
        }

        // Convert and set cover image as standard JPEG
        if (!track.coverPath.empty() && PathFileExistsW(track.coverPath.c_str())) {
            try {
                wchar_t tempDir[MAX_PATH];
                GetTempPathW(MAX_PATH, tempDir);
                std::wstring tempCover = std::wstring(tempDir) + L"bodian_current_cover.jpg";

                if (ConvertWebpToJpeg(track.coverPath, tempCover)) {
                    auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(tempCover).get();
                    updater.Thumbnail(winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromFile(file));
                    Log(L"Thumbnail successfully converted to JPEG and applied: " + tempCover);
                } else {
                    Log(L"ConvertWebpToJpeg failed for: " + track.coverPath);
                }
            } catch (const winrt::hresult_error& ex) {
                Log(L"Thumbnail WinRT error: " + std::wstring(ex.message()));
            } catch (...) {
                Log(L"Thumbnail unknown error");
            }
        }

        updater.Update();

        m_smtc.PlaybackStatus(track.isPlaying ?
            winrt::Windows::Media::MediaPlaybackStatus::Playing :
            winrt::Windows::Media::MediaPlaybackStatus::Paused);

        if (track.durationMs > 0) {
            winrt::Windows::Media::SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(winrt::Windows::Foundation::TimeSpan{ 0 });
            timeline.EndTime(std::chrono::milliseconds(track.durationMs));
            timeline.MinSeekTime(winrt::Windows::Foundation::TimeSpan{ 0 });
            timeline.MaxSeekTime(std::chrono::milliseconds(track.durationMs));
            timeline.Position(std::chrono::milliseconds(track.positionMs));
            m_smtc.UpdateTimelineProperties(timeline);
        }

        Log(L"SMTC DisplayUpdater: " + track.title + L" - " + track.artist);
    } catch (const winrt::hresult_error& ex) {
        Log(L"Error updating track: " + std::wstring(ex.message()));
    } catch (...) {
        Log(L"Unknown error updating track");
    }
}

void SmtcManager::UpdatePlaybackState(bool isPlaying) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_smtc) return;

    m_currentTrack.isPlaying = isPlaying;
    try {
        m_smtc.PlaybackStatus(isPlaying ?
            winrt::Windows::Media::MediaPlaybackStatus::Playing :
            winrt::Windows::Media::MediaPlaybackStatus::Paused);
    } catch (...) {}
}

void SmtcManager::UpdatePosition(int64_t positionMs, int64_t durationMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !m_smtc) return;

    if (durationMs > 0) m_currentTrack.durationMs = durationMs;
    m_currentTrack.positionMs = positionMs;

    try {
        if (m_currentTrack.durationMs > 0) {
            winrt::Windows::Media::SystemMediaTransportControlsTimelineProperties timeline;
            timeline.StartTime(winrt::Windows::Foundation::TimeSpan{ 0 });
            timeline.EndTime(std::chrono::milliseconds(m_currentTrack.durationMs));
            timeline.MinSeekTime(winrt::Windows::Foundation::TimeSpan{ 0 });
            timeline.MaxSeekTime(std::chrono::milliseconds(m_currentTrack.durationMs));
            timeline.Position(std::chrono::milliseconds(m_currentTrack.positionMs));
            m_smtc.UpdateTimelineProperties(timeline);
        }
    } catch (...) {}
}

} // namespace BodianSMTC
