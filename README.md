# BodianSMTCPlugin · 波点音乐 SMTC 插件

给波点音乐 PC 端（`bodian_pc.exe`）补上 Windows 系统媒体控制中心（SMTC）的插件。

装好之后，音量键弹出的媒体卡片、锁屏界面、以及 Windows 的媒体键路由，都能正确显示波点音乐正在播放的歌曲名、歌手、封面和播放进度，进度条也支持拖动跳转，卡片上的播放/暂停/上一首/下一首按钮可直接控制客户端。

> 本项目只针对 Windows 桌面版波点音乐，仅支持 **x64**。

---

## 背景与思路

波点音乐 PC 端是 Flutter 应用。它自带的播放器插件 `media_key_detector_windows_plugin.dll` 只做了全局媒体键监听，没有接入 Windows SMTC —— 所以系统媒体卡片里看不到波点音乐，播放时也无法用键盘的媒体键（部分场景下）正确路由。

本项目没有选择注入进程或修改客户端文件，而是用了最直接的一种做法：**代理替换那个插件 DLL**。

```
bodian_pc.exe
    └── 加载 media_key_detector_windows_plugin.dll   ← 本插件顶替了这个位置
            └── 转发调用 media_key_detector_windows_plugin_orig.dll（原版，改名保留）
```

插件导出与原版**同名**的 `MediaKeyDetectorWindowsRegisterWithRegistrar`，被 Flutter 加载时会先转发给原版 DLL，保证客户端原有的媒体键功能完全不受影响，然后再执行自己的 SMTC 逻辑。两者是配合关系，不是替代关系。

具体实现分三条链路，详见下方[工作原理](#工作原理)。

---

## 功能特性

- 系统媒体卡片（音量键 / Win+G / 锁屏）显示歌曲标题、歌手、专辑、封面
- 实时播放进度与总时长，进度条支持拖动跳转
- 播放 / 暂停 / 上一首 / 下一首 / 停止按钮可用
- webp 封面自动转 JPEG 后交给 SMTC（SMTC 不认 webp）
- 播放状态通过 WASAPI 音频会话兜底判断，mpv 属性读取失败时不会失效
- 注册 `AppUserModelId` 到系统，媒体卡片显示"波点音乐"而非进程名
- 原版 DLL 自动备份/还原，卸载脚本一键回滚

---

## 环境要求

| 依赖 | 要求 |
|---|---|
| 操作系统 | Windows 10 1809 及以上 / Windows 11，**x64** |
| 编译器 | Visual Studio Build Tools 2022（MSVC，需支持 C++20） |
| Windows SDK | 需包含 **cppwinrt / WinRT** 头文件（`winrt/Windows.Media.h` 等） |
| CMake | ≥ 3.20 |
| 构建系统 | Ninja |
| 脚本运行 | PowerShell 5.1+ |

SMTC 部分依赖 Windows SDK 提供的 `systemmediatransportcontrolsinterop.h`，请确保 SDK 版本足够新。

---

## 构建方法

### 1. 让构建脚本找到工具链

`build.ps1` 里不含任何路径，环境交给 [init_env.ps1](init_env.ps1) 自动探测，顺序如下（命中即止）：

1. `-MsvcRoot` 参数
2. 仓库根目录下的 `toolchain.local.ps1`（已被 `.gitignore` 排除，专门用来放本机路径）
3. 环境变量 `BODIAN_MSVC_ROOT`
4. `vswhere.exe` 定位已安装的 Visual Studio / Build Tools，导入 `vcvars64.bat`
5. 常见标准安装路径

也就是说，**装了标准 Visual Studio 2022 或 Build Tools（勾选「使用 C++ 的桌面开发」）就什么都不用配，直接编译**。

如果用的是便携式工具链（没装 VS、自然也没有 vswhere），任选一种方式指路：

```powershell
# 方式一：命令行传参
.\build.ps1 -MsvcRoot "E:\toolchains\msvc" -CMakeBin "E:\toolchains\cmake\bin"

# 方式二：写一份 toolchain.local.ps1 放仓库根目录，之后直接跑 build.ps1
$env:BODIAN_MSVC_ROOT = "E:\toolchains\msvc"
$env:BODIAN_CMAKE_BIN = "E:\toolchains\cmake\bin"
```

便携式工具链的目录约定如下。MSVC 与 SDK 的版本号由脚本自动挑最新的，不用你填：

```
<工具链根目录>\
├── VC\Tools\MSVC\<版本>\{bin\Hostx64\x64, include, lib\x64}
└── Windows Kits\10\{Include,Lib,bin}\<版本>\
```

`-CMakeBin` / `BODIAN_CMAKE_BIN` 指向同时含 `cmake.exe` 与 `ninja.exe` 的目录；两者已在系统 PATH 中时可省略。

探测失败时脚本会抛出带修复指引的错误，而不是让编译在稍后报出难懂的「找不到 cl.exe」。

### 2. 编译

```bash
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

脚本会依次完成：加载工具链环境 → CMake 配置（Ninja，Release）→ 编译。产物：

```
build\media_key_detector_windows_plugin.dll
```

脚本末尾会打印 DLL 路径和大小，看到 `SUCCESS!` 即编译成功。

---

## 安装

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1
```

`-TargetDir` 可以省略。省略时脚本按下面的顺序自动定位安装目录（逻辑在 [scripts/common.ps1](scripts/common.ps1)）：

1. 传入的 `-TargetDir`
2. 注册表卸载项中 `DisplayName` 匹配「波点 / bodian」的条目 —— 先取 `InstallLocation`，为空则从 `DisplayIcon` 反推
3. 正在运行的 `bodian_pc.exe` 进程所在目录

三条都走不通时（例如客户端是便携安装、当前又没运行），显式传参即可：

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1 -TargetDir "E:\apps\bodian"
```

无论走哪条路，脚本都会校验该目录下确实存在 `bodian_pc.exe`，免得误判到别处去动错文件。

[install.ps1](scripts/install.ps1) 做的事情：

1. 检测 `bodian_pc.exe` 是否在运行，在跑就先尝试正常关闭，不配合则强制结束（DLL 被占用会导致替换失败）
2. 把原版 `media_key_detector_windows_plugin.dll` 改名为 `media_key_detector_windows_plugin_orig.dll` 备份（**只备份一次**，已存在备份则保留原备份，不会覆盖）
3. 把刚编译出的 DLL 复制到原位置

> 备份文件是卸载的关键，不要手动删除 `_orig.dll`。

装完直接启动波点音乐即可。

## 卸载

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

目录探测方式与安装完全一致，同样支持用 `-TargetDir` 显式指定。脚本会删掉插件 DLL，再把 `_orig.dll` 改回原名。

注意：为了解除 DLL 占用，脚本会直接强制结束正在运行的 `bodian_pc.exe`，不做二次确认。

---

## 验证是否生效

用仓库里的诊断脚本查看当前系统注册了哪些媒体会话：

```bash
powershell -ExecutionPolicy Bypass -File .\scripts\check_smtc.ps1
```

波点音乐正在播放时，输出里应出现：

```
SourceAppId: Tencent.BodianMusic.PC
Title:       <当前歌曲名>
Artist:      <歌手>
Status:      Playing
Position:    00:01:23.4560000
EndTime:     00:04:05.6780000
```

只要 `SourceAppId` 是 `Tencent.BodianMusic.PC`，就说明 SMTC 已经挂上了。如果同时按音量键弹出的卡片能看到封面，说明封面转换链路也是通的。

---

## 工作原理

### 一、DLL 代理与初始化时机

[src/main.cpp](src/main.cpp) 负责代理入口。

- `DllMain` 里**不做任何实质性初始化**。原因是此时进程持有 Loader Lock，调用 `LoadLibraryW` / MinHook 会直接死锁 —— 这不是理论风险，是踩过的坑，代码里有注释标明。
- 真正的初始化在 `MediaKeyDetectorWindowsRegisterWithRegistrar` 里：先 `LoadOriginalPlugin()` 加载 `_orig.dll` 并转发调用，再 `SetTimer` 投一个 800ms 间隔的定时器。
- 定时器回调枚举本进程所有顶层窗口，找类名为 `BODIAN_FLUTTER_WIN32_WINDOW` 的窗口（兜底 `FLUTTER_RUNNER_WIN32_WINDOW`）。找到后才调用 `SmtcManager::InitializeOnUIThread`，最多重试 30 次。

为什么要绕这一圈？因为 `GetForWindow` 需要真实且已就绪的窗口句柄，且必须在窗口消息循环所在线程上调用 —— `SetTimer(NULL, ...)` 的回调正好跑在该线程的消息循环里。

### 二、SMTC 挂载

[src/SmtcManager.cpp](src/SmtcManager.cpp)：

1. `RegisterAppUserModel()` 在 `HKCU\Software\Classes\AppUserModelId\Tencent.BodianMusic.PC` 下写入 `DisplayName` / `IconUri`，并在开始菜单创建带 `PKEY_AppUserModel_ID` 的快捷方式。没有这一步，媒体卡片上显示的是进程名而不是"波点音乐"。
2. `SetWindowAppId()` 通过 `SHGetPropertyStoreForWindow` 把 `AppUserModelId` 直接写到窗口属性上。
3. `ISystemMediaTransportControlsInterop::GetForWindow()` 拿到该窗口的 SMTC 实例（先试根窗口，失败再试子窗口）。
4. 开启播放/暂停/上一首/下一首/停止按钮，注册 `ButtonPressed` 和 `PlaybackPositionChangeRequested` 两个回调。
5. `DisplayUpdater` 填元数据 + 时间轴，最后 `Update()` 提交。

**按钮回传**：`ButtonPressed` 回调里没有走 Flutter 的 MethodChannel（拿不到），而是用 `SendInput` 注入标准媒体键（`VK_MEDIA_PLAY_PAUSE` / `VK_MEDIA_NEXT_TRACK` 等），交给客户端**自己的**媒体键监听逻辑处理。这正是插件必须保留原版 DLL 转发的原因。

**进度条拖动**：`PlaybackPositionChangeRequested` 回调拿到目标毫秒数，调用 `MpvManager::Seek()` 写 mpv 的 `time-pos` 属性。

### 三、播放数据来源（两路）

[src/MetadataWatcher.cpp](src/MetadataWatcher.cpp) 起了一个常驻工作线程，主循环 200ms 一轮。

**① 实时进度与状态 —— 来自 mpv**

[src/MpvManager.cpp](src/MpvManager.cpp) 用 **MinHook** 挂钩 `libmpv-2.dll` 的导出函数：

- `mpv_create` / `mpv_destroy` / `mpv_terminate_destroy` —— 在播放器实例创建时抓住 `mpv_handle` 指针并保存
- 挂钩 `media_kit_native_event_loop.dll` 的 `MediaKitEventLoopHandlerRegister` / `Dispose` 作为兜底（`libmpv-2.dll` 在插件注册时可能还没加载）

拿到 handle 后，每轮循环通过 `mpv_get_property` 读 `time-pos`、`duration`、`pause`。

状态判断的优先级是：**能读到 mpv 的 `pause` 就用它**；读不到则退回 `CheckAudioPlaying()` —— 用 WASAPI（`IMMDeviceEnumerator` → `IAudioSessionManager2`）遍历音频会话，看本进程会话是否处于 `AudioSessionStateActive`。

**② 歌曲信息 —— 来自客户端自己的数据库**

直接只读打开客户端的 SQLite 库：

```
%LOCALAPPDATA%\cn.wenyu.bodian\bodian_pc\database\songDB.db
```

取 `hist_song` 表中 `ord` 最大的一条记录（即最近播过的一首），解析 `json` 字段里的 `name` / `artist` / `album` / `albumPic` / `duration`。`sqlite3.dll` 是运行时 `LoadLibrary` 动态解析的，不参与编译链接。每 600ms（3 轮循环）查一次，`ord` 变化才认为是换歌。

**进度同步策略**：每轮把 mpv 报的 `time-pos` 与"上次上报位置 + 经过的时间"比对：

- 偏差 > 400ms → 判定为用户拖动了进度条，立即同步
- 否则每满 1 秒做一次常规同步

这样正常播放时不会高频刷 SMTC，拖动时又能立刻响应。

### 四、封面转换

客户端把封面缓存在：

```
%APPDATA%\cn.wenyu.bodian\bodian_pc\artwork_cache\
```

文件名是封面 URL 的 **MD5 + `.img` 后缀**（内容实际是 webp）。SMTC 不认 webp，所以 [SmtcManager.cpp](src/SmtcManager.cpp) 里的 `ConvertWebpToJpeg()` 用 **WIC** 解码 webp → 编码 JPEG，输出到 `%TEMP%\bodian_current_cover.jpg`，再用 `RandomAccessStreamReference::CreateFromFile` 喂给 `DisplayUpdater.Thumbnail()`。

如果按 MD5 找不到对应缓存文件，会退化为"取 `artwork_cache` 目录下最后修改的那个 `.img`"。

---

## 项目结构

```
BodianSMTCPlugin/
├── CMakeLists.txt                  # 构建定义 (C++20, x64, SHARED)
├── exports.def                     # 导出 MediaKeyDetectorWindowsRegisterWithRegistrar
├── init_env.ps1                    # 工具链自动探测与环境注入（无硬编码路径）
├── build.ps1                       # 一键构建，向 init_env.ps1 透传工具链参数
├── toolchain.local.ps1             # [本机生成，已被 gitignore] 存放本机工具链路径
├── src/
│   ├── Common.h                    # 公共头、TrackInfo、日志工具
│   ├── main.cpp                    # DLL 代理入口、窗口查找、初始化调度
│   ├── SmtcManager.{h,cpp}         # SMTC 挂载、元数据/时间轴更新、WIC 封面转换
│   ├── MpvManager.{h,cpp}          # MinHook 挂钩 libmpv / media_kit，读写播放属性
│   ├── MetadataWatcher.{h,cpp}     # 200ms 工作线程：mpv 进度 + SQLite 歌曲信息
│   ├── MediaController.{h,cpp}     # SendInput 注入媒体键
│   └── minhook/                    # 内置的 MinHook 源码 (BSD-2-Clause)
└── scripts/
    ├── common.ps1                  # 安装目录探测（注册表 / 运行中进程），被下面两个脚本共用
    ├── install.ps1                 # 备份原版 DLL 并部署插件
    ├── uninstall.ps1               # 还原原版 DLL
    ├── check_smtc.ps1              # 列出当前系统媒体会话，用于验证
    └── check_crash.ps1             # 查最近 10 分钟的应用崩溃事件
```

> 改 `.ps1` 时注意：这些脚本含中文，**必须保存为带 BOM 的 UTF-8**。Windows PowerShell 5.1 在没有 BOM 时会按系统 ANSI 代码页（简中环境即 GBK）读取，中文注释会被解析成乱码并导致语法错误。VS Code 右下角编码选 `UTF-8 with BOM` 即可。

---

## 排错

插件的日志写在**波点音乐安装目录**下的 `smtc_plugin.log`（同时通过 `OutputDebugStringW` 输出，可用 DebugView 实时观察）。出问题先看这个文件，日志覆盖了初始化各个阶段和每次换歌。

| 现象 | 排查方向 |
|---|---|
| 媒体卡片完全不出现 | 日志里搜 `SMTC successfully initialized`。没有则看 `GetForWindow failed with HRESULT`，多半是窗口没找到或窗口类名变了 |
| 卡片出现但显示进程名 | `RegisterAppUserModel` 失败，检查 `HKCU\Software\Classes\AppUserModelId\Tencent.BodianMusic.PC` 是否写入成功、开始菜单快捷方式是否创建 |
| 有歌名但进度条不动 | mpv 挂钩没生效。日志里搜 `Hook_mpv_create captured handle`；搜不到说明 `libmpv-2.dll` 加载时机太晚或导出符号变了 |
| 进度条会动但不能拖动 | `PlaybackPositionChangeRequested` 未触发，或 `MpvManager::Seek` 返回负值（日志里有 `MpvManager::Seek to ... err=`） |
| 卡片没有封面 | 搜 `ConvertWebpToJpeg failed`。也可以确认 `%TEMP%\bodian_current_cover.jpg` 是否生成 |
| 客户端启动崩溃 | 跑 `scripts\check_crash.ps1` 看事件查看器里的崩溃记录；确认 `_orig.dll` 存在且完整 |
| 编译报找不到 `cl.exe` | `init_env.ps1` 的路径没改成你自己的 |
| 安装报 DLL 被占用 | 手动退出波点音乐后重跑安装脚本 |

---

## 已知限制

- **仅 x64**，代码里用的是 `hde64` 反汇编引擎，不支持 32 位。
- **与客户端版本强绑定**。以下几处任一发生变化都可能失效：窗口类名 `BODIAN_FLUTTER_WIN32_WINDOW`、数据库路径、`artwork_cache` 目录结构、`libmpv-2.dll` / `media_kit_native_event_loop.dll` 的导出符号名。
- 挂钩生效依赖目标模块**已经被加载**。如果插件注册时 `libmpv-2.dll` 还没进内存，会失去该挂钩点，退化为依赖 MediaKit 事件循环 DLL 的挂钩路径。
- 歌曲信息依赖客户端本地数据库的 JSON 结构，客户端改了字段名就会读不到。
- 未做多播放器实例 / 多窗口的处理，`m_activeHandle` 只保留最后一个创建的 mpv 实例。
- 卸载前必须先退出波点音乐，否则 DLL 被占用无法还原。

---

## 第三方组件

| 组件 | 许可 | 说明 |
|---|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | API Hook 库，源码内置于 `src/minhook/`，版权归原作者 Tsuda Kageyu |

其余代码为原创。

---

## 免责声明

本项目为**学习与研究性质**的逆向工程实践，仅用于探讨 Windows SMTC 接口与 Flutter 桌面应用的集成方式。

- 项目通过替换客户端插件 DLL 的方式工作，请自行评估风险；**因使用本插件导致的客户端异常、数据丢失或其他任何后果，由使用者自行承担**。
- 请勿将本项目用于商业用途或任何违反波点音乐用户协议的场景。
- 版权归原有权利方所有的内容（客户端程序、`libmpv-2.dll`、`media_kit` 等）均不在本项目范围内，本项目不分发这些文件。
- 若权利方认为本项目侵犯其权益，请联系删除。
