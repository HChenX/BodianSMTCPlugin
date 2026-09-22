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

DLL 有两种装法：从 Releases 下载预编译产物手动替换（免构建），或自行构建后用安装脚本部署。

### 方式一：手动替换（免构建）

从 [Releases](https://github.com/HChenX/BodianSMTCPlugin/releases) 下载 `media_key_detector_windows_plugin.dll`。下文将客户端安装目录记为 `<安装目录>`，其下应存在 `bodian_pc.exe`。

**安装**

1. 完全退出波点音乐，并在任务管理器中确认无 `bodian_pc.exe` 残留
2. 将 `<安装目录>\media_key_detector_windows_plugin.dll` 重命名为 `media_key_detector_windows_plugin_orig.dll`，完成备份
3. 将下载的 DLL 复制到 `<安装目录>\media_key_detector_windows_plugin.dll`

**还原**

1. 完全退出波点音乐
2. 删除 `<安装目录>\media_key_detector_windows_plugin.dll`
3. 将 `<安装目录>\media_key_detector_windows_plugin_orig.dll` 改回 `media_key_detector_windows_plugin.dll`

> 备份只需做一次。若目录下已存在 `_orig.dll`，说明此前已备份，跳过第 2 步，切勿重复重命名 —— 否则会用插件的 DLL 覆盖掉真正的原版备份。

校验下载文件完整性（哈希值见对应 Release 说明）：

```bash
certutil -hashfile media_key_detector_windows_plugin.dll SHA256
```

### 方式二：安装脚本（需先自行构建）

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

- **初始化时机**：`DllMain` 不做实质工作 —— 此时进程持有 Loader Lock，加载 DLL 或安装挂钩会死锁。真正的初始化推迟到窗口就绪后：以 800ms 间隔的定时器轮询本进程窗口，匹配类名 `BODIAN_FLUTTER_WIN32_WINDOW` 后，在该窗口消息循环所在线程调用 `GetForWindow` 挂载 SMTC。
- **曲目信息**：只读打开客户端 SQLite 库 `%LOCALAPPDATA%\cn.wenyu.bodian\bodian_pc\database\songDB.db`，读取 `hist_song` 表中 `ord` 最大的记录，查询周期 600ms。
- **播放进度与状态**：MinHook 挂钩 `libmpv-2.dll` 的 `mpv_create` 以取得 `mpv_handle`，每 200ms 经 `mpv_get_property` 读取 `time-pos` / `duration` / `pause`；不可读时回退至 WASAPI 音频会话状态判断。
- **封面**：缓存在 `%APPDATA%\cn.wenyu.bodian\bodian_pc\artwork_cache\`，文件名为封面 URL 的 MD5 加 `.img` 后缀，内容为 webp；由 WIC 转为 JPEG 后提交给 SMTC。
- **控制回传**：以 `SendInput` 注入标准媒体键实现，不使用 Flutter MethodChannel。

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

插件日志写入波点音乐安装目录下的 `smtc_plugin.log`，同时经 `OutputDebugStringW` 输出，可用 DebugView 实时查看。排查时先读该文件，其中的关键节点：

- `SMTC successfully initialized` —— 未出现表示 SMTC 未挂载，继续查同文件中的 `GetForWindow failed with HRESULT`
- `Hook_mpv_create captured handle` —— 未出现表示 mpv 挂钩未生效，播放进度将不更新
- `ConvertWebpToJpeg failed` —— 封面转换失败
- `MpvManager::Seek to ... err=` —— 进度拖动失败

若客户端版本更新后整体失效，通常是窗口类名、数据库路径或 `libmpv-2.dll` 的导出符号发生了变化。

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
