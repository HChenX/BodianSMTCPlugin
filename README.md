# BodianSMTCPlugin · 波点音乐 SMTC 插件

为波点音乐 Windows 桌面客户端（`bodian_pc.exe`）接入 Windows 系统媒体控制中心（SMTC）的插件。

安装后，系统媒体卡片（音量键 / Win+G / 锁屏）可显示当前曲目的标题、歌手、专辑与封面，播放进度实时同步并支持拖动跳转，卡片上的播放、暂停、上一首、下一首、停止按钮可直接控制客户端。

> 仅支持 Windows 桌面版波点音乐，仅 x64。

## 实现方式

波点音乐 PC 端为 Flutter 应用，其自带的 `media_key_detector_windows_plugin.dll` 只实现全局媒体键监听，未接入 Windows SMTC。本插件以代理方式接管该 DLL 的加载位置：

```
bodian_pc.exe
    └── media_key_detector_windows_plugin.dll               ← 本插件
            └── media_key_detector_windows_plugin_orig.dll   （原版，改名保留）
```

插件导出与原版同名的 `MediaKeyDetectorWindowsRegisterWithRegistrar`，被 Flutter 加载时先转发调用给原版 DLL，再执行自身的 SMTC 逻辑，客户端原有的媒体键功能不受影响。

## 功能

- 媒体卡片显示曲目标题、歌手、专辑、封面
- 播放进度与总时长实时同步，支持拖动跳转
- 播放 / 暂停 / 上一首 / 下一首 / 停止控制
- 封面自动由 webp 转为 JPEG（SMTC 不支持 webp）
- mpv 属性不可读时，播放状态回退至 WASAPI 音频会话判断
- 注册 `AppUserModelId`，媒体卡片显示应用名而非进程名
- 安装时自动备份原版 DLL，卸载可完整还原

## 环境要求

| 依赖 | 要求 |
|---|---|
| 操作系统 | Windows 10 1809 及以上 / Windows 11，x64 |
| 编译器 | MSVC，需支持 C++20 |
| Windows SDK | 需包含 cppwinrt / WinRT 头文件与 `systemmediatransportcontrolsinterop.h` |
| CMake | ≥ 3.20 |
| 构建系统 | Ninja |
| 脚本运行 | PowerShell 5.1+ |

## 构建

### 工具链探测

`build.ps1` 不含硬编码路径，由 [init_env.ps1](init_env.ps1) 按以下顺序定位工具链（命中即止）：

1. `-MsvcRoot` 参数
2. 仓库根目录的 `toolchain.local.ps1`（被 `.gitignore` 排除，用于存放本机路径）
3. 环境变量 `BODIAN_MSVC_ROOT`
4. `vswhere.exe` 定位 Visual Studio / Build Tools 并导入 `vcvars64.bat`
5. 常见标准安装路径

已安装标准 Visual Studio 2022 或 Build Tools（含「使用 C++ 的桌面开发」工作负载）时无需额外配置。使用便携式工具链时，以参数或环境变量指定：

```powershell
# 命令行传参
.\build.ps1 -MsvcRoot "E:\toolchains\msvc" -CMakeBin "E:\toolchains\cmake\bin"

# 或写入仓库根目录的 toolchain.local.ps1，之后直接运行 build.ps1
$env:BODIAN_MSVC_ROOT = "E:\toolchains\msvc"
$env:BODIAN_CMAKE_BIN = "E:\toolchains\cmake\bin"
```

便携式工具链的目录约定如下，MSVC 与 SDK 版本号由脚本自动选取最新：

```
<工具链根目录>\
├── VC\Tools\MSVC\<版本>\{bin\Hostx64\x64, include, lib\x64}
└── Windows Kits\10\{Include,Lib,bin}\<版本>\
```

`-CMakeBin` / `BODIAN_CMAKE_BIN` 指向同时包含 `cmake.exe` 与 `ninja.exe` 的目录；两者已在系统 PATH 中时可省略。探测失败时脚本抛出含修复指引的错误。

### 编译

```bash
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

流程为：加载工具链环境 → CMake 配置（Ninja / Release）→ 编译。产物：

```
build\media_key_detector_windows_plugin.dll
```

## 安装

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1
```

`-TargetDir` 可省略。省略时脚本按以下顺序定位安装目录（逻辑见 [scripts/common.ps1](scripts/common.ps1)）：

1. 传入的 `-TargetDir`
2. 注册表卸载项中 `DisplayName` 匹配「波点 / bodian」的条目：先取 `InstallLocation`，为空则从 `DisplayIcon` 反推
3. 正在运行的 `bodian_pc.exe` 进程所在目录

均未命中时显式指定：

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1 -TargetDir "E:\apps\bodian"
```

无论走哪条路径，脚本都会校验目标目录下存在 `bodian_pc.exe`。

安装脚本执行的操作：

1. 检测 `bodian_pc.exe`，先尝试正常关闭，未退出则强制结束（DLL 被占用时无法替换）
2. 将原版 `media_key_detector_windows_plugin.dll` 重命名为 `media_key_detector_windows_plugin_orig.dll`，仅备份一次，已存在时不覆盖
3. 复制编译产物到原位置

> `_orig.dll` 是卸载的依据，请勿删除。

## 卸载

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

目录定位方式与安装一致，同样支持 `-TargetDir`。脚本删除插件 DLL，并将 `_orig.dll` 改回原名。为解除 DLL 占用，脚本会强制结束正在运行的 `bodian_pc.exe`，不做二次确认。

## 验证

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\check_smtc.ps1
```

该脚本列出系统当前的媒体会话。波点音乐播放时应包含以下内容：

```
SourceAppId: Tencent.BodianMusic.PC
Title:       <曲目名>
Artist:      <歌手>
Status:      Playing
Position:    00:01:23.4560000
EndTime:     00:04:05.6780000
```

`SourceAppId` 为 `Tencent.BodianMusic.PC` 表示 SMTC 已挂载成功。

## 工作原理

### 1. DLL 代理与初始化时机

[src/main.cpp](src/main.cpp) 为代理入口。

- `DllMain` 不进行实质初始化：此时进程持有 Loader Lock，调用 `LoadLibraryW` 或 MinHook 会导致死锁。
- 实际初始化位于 `MediaKeyDetectorWindowsRegisterWithRegistrar`：先由 `LoadOriginalPlugin()` 加载 `_orig.dll` 并转发调用，再以 `SetTimer` 投递 800ms 间隔的定时器。
- 定时器回调枚举本进程顶层窗口，匹配类名 `BODIAN_FLUTTER_WIN32_WINDOW`（回退 `FLUTTER_RUNNER_WIN32_WINDOW`），命中后调用 `SmtcManager::InitializeOnUIThread`，最多重试 30 次。

`GetForWindow` 要求句柄对应的窗口已创建，且必须在窗口消息循环所在线程调用；`SetTimer(NULL, ...)` 的回调运行于该线程的消息循环，故以此方式延迟初始化。

### 2. SMTC 挂载

[src/SmtcManager.cpp](src/SmtcManager.cpp)：

1. `RegisterAppUserModel()` 在 `HKCU\Software\Classes\AppUserModelId\Tencent.BodianMusic.PC` 写入 `DisplayName` / `IconUri`，并创建带 `PKEY_AppUserModel_ID` 的开始菜单快捷方式。缺少该步骤时媒体卡片显示进程名。
2. `SetWindowAppId()` 经 `SHGetPropertyStoreForWindow` 将 `AppUserModelId` 写入窗口属性。
3. `ISystemMediaTransportControlsInterop::GetForWindow()` 获取窗口的 SMTC 实例，先试根窗口，失败再试子窗口。
4. 启用播放 / 暂停 / 上一首 / 下一首 / 停止，注册 `ButtonPressed` 与 `PlaybackPositionChangeRequested` 回调。
5. 通过 `DisplayUpdater` 提交元数据与时间轴。

**控制回传**：`ButtonPressed` 不使用 Flutter MethodChannel，而以 `SendInput` 注入标准媒体键（`VK_MEDIA_PLAY_PAUSE`、`VK_MEDIA_NEXT_TRACK` 等），交由客户端自身的媒体键监听逻辑处理。

**进度跳转**：`PlaybackPositionChangeRequested` 取得目标毫秒数后调用 `MpvManager::Seek()`，写入 mpv 的 `time-pos` 属性。

### 3. 数据来源

[src/MetadataWatcher.cpp](src/MetadataWatcher.cpp) 维护一个常驻工作线程，主循环周期 200ms。

**进度与播放状态（mpv）**

[src/MpvManager.cpp](src/MpvManager.cpp) 使用 MinHook 挂钩以下导出函数：

- `libmpv-2.dll` 的 `mpv_create` / `mpv_destroy` / `mpv_terminate_destroy`，在播放器实例创建时保存 `mpv_handle`
- `media_kit_native_event_loop.dll` 的 `MediaKitEventLoopHandlerRegister` / `Dispose`，作为 `libmpv-2.dll` 尚未加载时的回退路径

取得句柄后，每轮循环通过 `mpv_get_property` 读取 `time-pos`、`duration`、`pause`。状态判断优先采用 mpv 的 `pause`；不可读时回退至 `CheckAudioPlaying()`：经 WASAPI（`IMMDeviceEnumerator` → `IAudioSessionManager2`）遍历音频会话，判断本进程会话是否为 `AudioSessionStateActive`。

**曲目信息（客户端数据库）**

以只读方式打开客户端的 SQLite 库：

```
%LOCALAPPDATA%\cn.wenyu.bodian\bodian_pc\database\songDB.db
```

读取 `hist_song` 表中 `ord` 最大的记录，解析 `json` 字段的 `name` / `artist` / `album` / `albumPic` / `duration`。`sqlite3.dll` 于运行时 `LoadLibrary` 动态解析，不参与链接。查询周期 600ms（每 3 轮循环一次），`ord` 变化时判定为切歌。

**进度同步策略**：每轮将 mpv 的 `time-pos` 与「上次上报位置 + 已过时间」比较。

- 偏差 > 400ms：判定为进度跳转，立即同步
- 否则每满 1 秒同步一次

### 4. 封面转换

封面缓存在：

```
%APPDATA%\cn.wenyu.bodian\bodian_pc\artwork_cache\
```

文件名为封面 URL 的 MD5 加 `.img` 后缀，内容为 webp。SMTC 不支持 webp，故 [SmtcManager.cpp](src/SmtcManager.cpp) 的 `ConvertWebpToJpeg()` 使用 WIC 解码 webp 并编码为 JPEG，输出至 `%TEMP%\bodian_current_cover.jpg`，再经 `RandomAccessStreamReference::CreateFromFile` 提交给 `DisplayUpdater.Thumbnail()`。

若按 MD5 未命中缓存文件，回退为取该目录下最后修改的 `.img`。

## 项目结构

```
BodianSMTCPlugin/
├── CMakeLists.txt                  # 构建定义 (C++20, x64, SHARED)
├── exports.def                     # 导出 MediaKeyDetectorWindowsRegisterWithRegistrar
├── init_env.ps1                    # 工具链自动探测与环境注入
├── build.ps1                       # 一键构建，向 init_env.ps1 透传参数
├── toolchain.local.ps1             # [本机生成，已被 gitignore] 存放本机工具链路径
├── src/
│   ├── Common.h                    # 公共头、TrackInfo、日志工具
│   ├── main.cpp                    # DLL 代理入口、窗口查找、初始化调度
│   ├── SmtcManager.{h,cpp}         # SMTC 挂载、元数据与时间轴更新、WIC 封面转换
│   ├── MpvManager.{h,cpp}          # MinHook 挂钩 libmpv / media_kit，读写播放属性
│   ├── MetadataWatcher.{h,cpp}     # 200ms 工作线程：mpv 进度与 SQLite 曲目信息
│   ├── MediaController.{h,cpp}     # SendInput 注入媒体键
│   └── minhook/                    # 内置的 MinHook 源码 (BSD-2-Clause)
└── scripts/
    ├── common.ps1                  # 安装目录探测（注册表 / 运行中进程）
    ├── install.ps1                 # 备份原版 DLL 并部署插件
    ├── uninstall.ps1               # 还原原版 DLL
    ├── check_smtc.ps1              # 列出当前系统媒体会话
    └── check_crash.ps1             # 查询最近 10 分钟的应用崩溃事件
```

> 修改 `.ps1` 时须注意编码：脚本含中文，必须保存为带 BOM 的 UTF-8。PowerShell 5.1 在缺少 BOM 时按系统 ANSI 代码页读取，中文将解析为乱码并导致语法错误。

## 排错

插件日志写入波点音乐安装目录下的 `smtc_plugin.log`，同时经 `OutputDebugStringW` 输出，可用 DebugView 实时查看。日志覆盖初始化各阶段与每次切歌。

| 现象 | 排查方向 |
|---|---|
| 媒体卡片完全不出现 | 日志中检索 `SMTC successfully initialized`。若不存在，查看 `GetForWindow failed with HRESULT`，通常为窗口未找到或窗口类名变更 |
| 卡片出现但显示进程名 | `RegisterAppUserModel` 失败。检查 `HKCU\Software\Classes\AppUserModelId\Tencent.BodianMusic.PC` 是否写入成功、开始菜单快捷方式是否创建 |
| 有歌名但进度条不动 | mpv 挂钩未生效。检索 `Hook_mpv_create captured handle`；无此日志说明 `libmpv-2.dll` 加载时机过晚或导出符号变更 |
| 进度条会动但无法拖动 | `PlaybackPositionChangeRequested` 未触发，或 `MpvManager::Seek` 返回负值（日志含 `MpvManager::Seek to ... err=`） |
| 卡片没有封面 | 检索 `ConvertWebpToJpeg failed`，并确认 `%TEMP%\bodian_current_cover.jpg` 是否生成 |
| 客户端启动崩溃 | 运行 `scripts\check_crash.ps1` 查看事件查看器记录；确认 `_orig.dll` 存在且完整 |
| 构建报工具链探测失败 | 检查 `-MsvcRoot` / `toolchain.local.ps1` / `BODIAN_MSVC_ROOT` 是否指向正确的工具链根目录，或确认已安装 Visual Studio 的 C++ 工作负载 |
| 安装报 DLL 被占用 | 手动退出波点音乐后重新执行安装脚本 |

## 已知限制

- **仅 x64**。实现使用 `hde64` 反汇编引擎，不支持 32 位。
- **与客户端版本强绑定**。以下任一变更均可能导致失效：窗口类名 `BODIAN_FLUTTER_WIN32_WINDOW`、数据库路径、`artwork_cache` 目录结构、`libmpv-2.dll` / `media_kit_native_event_loop.dll` 的导出符号名。
- 挂钩生效依赖目标模块**已被加载**。若插件注册时 `libmpv-2.dll` 尚未进内存，该挂钩点将丢失，退化为依赖 MediaKit 事件循环 DLL 的路径。
- 曲目信息依赖客户端数据库的 JSON 结构，字段名变更后无法读取。
- 未处理多播放器实例 / 多窗口场景，`m_activeHandle` 仅保留最后创建的 mpv 实例。

## 第三方组件

| 组件 | 许可 | 说明 |
|---|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | API Hook 库，源码内置于 `src/minhook/`，版权归原作者 Tsuda Kageyu |

其余代码为原创。

## 免责声明

本项目为学习与研究性质的逆向工程实践，用于探讨 Windows SMTC 接口与 Flutter 桌面应用的集成方式。

- 项目通过替换客户端插件 DLL 的方式工作，因使用本插件导致的客户端异常、数据丢失或其他后果，由使用者自行承担。
- 请勿将本项目用于商业用途或任何违反波点音乐用户协议的场景。
- 客户端程序、`libmpv-2.dll`、`media_kit` 等版权归原权利方所有，不在本项目范围内，本项目不分发这些文件。
- 若权利方认为本项目侵犯其权益，请联系删除。
