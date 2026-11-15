# mobile · 闹钟提醒

一个 Windows 桌面小工具：到上下课时间，屏幕顶部弹出一个类似手机「灵动岛」的提醒并响铃。单文件、免安装，双击即自动部署并常驻托盘。

界面用 Win32 API + 分层窗口（Layered Window）全自绘，没有运行时依赖。下载时提示风险为正常现象，此应用绝对安全

## 两条线

| 线 | 源码 | 发布产物 | 依赖 |
| --- | --- | --- | --- |
| 主线 v1.2 | `mobile.cpp` | `mobile-v1.2.exe` | litehtml + gumbo（AI 岛气泡排版） |
| **Turbo** | `turbo.cpp` | `mobile-v1.2-Turbo.exe` | 无，约 449 KB |

Turbo 是从 v1.2 分出来的**减法支线**：砍掉没做完的 AI 岛与多岛并行，修触控点击和夜间天色，顺手把每帧白算的活儿去掉。单文件 10477 → 7844 行，exe 2.1 MB → 449 KB。

两条线**可以同时装在一台机器上**：安装目录（`%LOCALAPPDATA%\IslandAlarmTurbo`）、注册表键（`HKCU\Software\IslandAlarmTurbo`）、单实例标识、开机自启动项全是分开的，互不覆盖。

## 功能

- **三档「岛」**
  - 大岛：上课 / 下课 / 自定义提醒 / 麦克风摄像头占用时弹出，显示事件与时间，约 5 秒收回，宽度随文字自适应
  - 小岛：课间显示「距上课 mm:ss」倒计时，只要处于课间就在
  - 迷你岛：贴屏幕顶边的刘海式时钟，静止 6 秒后自动降到半透明
- **底板模式**：天气模式（底板跟着实时日出日落、云雨雪走）/ 纯色模式（原来那块深灰底）。选纯色时「天空亮度 / 天空动画」自动灰掉
- **每节课独立铃声**：11 个节次，每节的上课铃 / 下课铃分别勾选并拖音量；开关用位掩码存储，22 个音量打包成一个二进制值
- **放学《回家》**：每天 19:30 播放同目录的 `Home.mp3`，大岛变音乐播放器（左封面 / 中歌名 / 右播放-暂停），封面读 `Home.jpg`
- **实时天气**：后台线程走 WinHTTP，超时短、失败静默
- **麦克风 / 摄像头占用提示**：设备被占用时弹大岛，左侧自绘图标 + 右侧文字；数据只读 Windows 自己的隐私设备使用记录
- **自定义提醒**：大岛文字可改；能加「一次性倒计时 / 定时」提醒；时间段可增删改
- **三套作息表**：初四夏季 / 冬季（默认）/ 初四冬季
- **图形设置窗口**：左侧导航 6 页（作息表 / 时间提醒 / 自定义 / 主题 / 迷你岛 / 更多），底部常驻运行状态（运行中 / 现在时间 / 下次提醒），改完即生效并自动保存
- **「立即测试」**：依次演示迷你岛收展、上下课大岛、课间小岛、静置变淡、播放器（约 28 秒，期间屏蔽真实闹钟）
- **节日皮肤**：中秋、国庆（含前一天）自动换配色，可在「主题」页关掉
- **触控**：点击 / 长按 / 顶边下滑都按指针消息处理，手指和鼠标一套逻辑（挪动超过 12px 算拖动，250ms 内只认一次）

### 操作

| 动作 | 效果 |
| --- | --- |
| 长按小岛 / 迷你岛 | 关闭 |
| 从屏幕顶边向下滑 | 展开迷你岛 |
| 点迷你岛 / 小岛 | 弹「下节课是…」之类的信息 |
| `Ctrl + Alt + A` | 立刻弹一次大岛 |
| `Ctrl + Alt + S` | 打开设置 |
| `Ctrl + Alt + P` | 演示播放器岛 |
| 托盘图标右键 | 设置… / 退出 |

## 相比主线删掉了什么

- **AI 岛**：对话界面、litehtml 气泡排版、DPAPI 加密的 API Key，以及首次启动搬配置时的 AI 相关项
- **消息岛 / 多岛并行**：岛下方挂原岛、多条消息堆叠
- **「浅色 / 深色」主题**：有了天空背景之后底板跟着太阳走，这个概念没有落点，被「底板模式」取代
- **litehtml、gumbo 两个静态库**：exe 从 2.1 MB 掉到 449 KB 的主因

## 快速开始

到 [Releases](../../releases) 下载 `mobile.zip`，解压后是：

- `mobile-v1.2-Turbo.exe`：主程序
- `alin.mp3`：上课 / 下课提醒音
- `Home.mp3`：放学《回家》
- `Home.jpg`：播放器封面
- `check.exe`：自检工具，控制台程序，跑完在同目录生成 TXT 报告

放**同一个目录**，双击 exe 即可：自动安装到 `%LOCALAPPDATA%\IslandAlarmTurbo` 并常驻托盘。

首次运行等于全新安装，作息表勾选、音量这些要重新配一次；以后升级只覆盖安装目录里的同名 exe，设置保留。确认新版没问题后，把旧版从托盘退出，再走它自己的卸载。

## 从源码编译

需要 Windows 10 及以上 + MinGW-w64（GCC，支持 C++17），例如 MSYS2。

### Turbo（无第三方依赖）

```bat
windres app.rc -O coff -o app_res.o
g++ -O2 -std=c++17 -mwindows -static -s -o mobile-v1.2-Turbo.exe turbo.cpp app_res.o ^
    -luser32 -lgdi32 -lmsimg32 -lgdiplus -lwinhttp -ladvapi32 -lversion ^
    -lshell32 -lwinmm -lole32 -luuid -lcomctl32
```

### 主线（还要 litehtml + gumbo）

两个库需自行编译，然后按下面的结构放（或改 `LITEHTML_DIR` / `GUMBO_DIR`）：

```
third_party\
├── litehtml\
│   ├── include\litehtml.h
│   └── liblitehtml.a
└── gumbo\
    ├── src\gumbo.h
    └── libgumbo.a
```

```bat
g++ -O2 -std=c++17 -mwindows -static -s -o mobile-v1.2.exe mobile.cpp app_res.o ^
    -I"third_party\litehtml\include" -I"third_party\gumbo\src" ^
    -L"third_party\litehtml" -L"third_party\gumbo" ^
    -luser32 -lgdi32 -lmsimg32 -lgdiplus -lwinhttp -limm32 -ladvapi32 -lcrypt32 -lversion ^
    -lshell32 -lwinmm -lole32 -luuid -lcomctl32 ^
    -llitehtml -lgumbo
```

`build.bat` 默认编 Turbo；`build.bat mobile` 编主线；`build.bat check` 编自检工具。

### 链接库都是干嘛的

| 库 | 用途 |
| --- | --- |
| `-lgdiplus` | 解码 `Home.jpg` 封面 |
| `-lwinhttp` | 实时天气 |
| `-lversion` | 读 exe 自己的版本号（自安装时比较新旧） |
| `-lole32` | WASAPI 回环（抓系统输出算音量） |
| `-limm32` | 主线专用：AI 岛输入框的输入法候选窗定位 |
| `-lcrypt32` | 主线专用：DPAPI 加密 API Key |
| `-llitehtml -lgumbo` | 主线专用：AI 岛气泡排版 |

`app.rc` 里放图标、清单和 VERSIONINFO。**版本号必须跟着发版改** —— 自安装靠它判断「手里这份是不是更新」，没有它新版永远覆盖不了已装好的旧版。清单里声明了 `asInvoker` 和 comctl32 6，别删。

## 作息表

内置三套，在设置窗口切换，选择自动保存到注册表。时间定义在源码 `LoadAlarmTimes()` 中，要改就直接改数组再重新编译。

### 冬季（默认，9 节）

| 节次 | 上课 | 下课 |
| ---: | --- | --- |
| 第1节 | 08:00 | 08:45 |
| 第2节 | 09:00 | 09:45 |
| 第3节 | 10:15 | 11:00 |
| 第4节 | 11:10 | 11:55 |
| 第5节 | 13:30 | 14:15 |
| 第6节 | 14:30 | 15:15 |
| 第7节 | 15:45 | 16:30 |
| 课后服务1 | 16:40 | 17:20 |
| 课后服务2 | 17:30 | 18:10 |

### 初四夏季（11 节，含早读 / 午自习）

早读 07:15-07:55、第1节 08:00-08:45、第2节 09:00-09:45、第3节 10:15-11:00、第4节 11:10-11:55、午自习 12:30-13:15、第5节 14:00-14:45、第6节 15:00-15:45、第7节 15:55-16:40、课后服务1 16:45-17:25、课后服务2 17:35-18:15

### 初四冬季（9 节）

第 1~6 节同冬季；第七节 15:25-16:10、第八节 16:15-16:55、第九节 17:05-17:45

## 自检工具

`check.exe` 是个独立控制台程序，源码在 `check.cpp`，只查「能确定」的东西：系统版本、屏幕尺寸、安装目录与 exe 版本、三个音频文件在不在、注册表值合不合法、自启动项、主程序有没有在跑。运行后同目录生成一份 TXT 报告，失败项会写「可能原因 + 建议」。Turbo 版的注册表键和安装目录都带 `Turbo` 后缀，别拿查主线键的旧工具来查。

```bat
check.exe          直接跑
check.exe --keep   跑完不自动关窗口
```

自己编：`g++ -O2 -std=c++17 -municode -static -s -o check.exe check.cpp -ladvapi32 -lversion -lshell32`（或直接 `build.bat check`）。

运行时行为（动画、点击判定）它看不到，那些归主程序里的「立即测试」。

## 卸载

```bat
mobile-v1.2-Turbo.exe --uninstall
```

会删开机自启动项和程序目录 `%LOCALAPPDATA%\IslandAlarmTurbo`；设置仍留在 `HKCU\Software\IslandAlarmTurbo`，要彻底清就手动删这个键。卸载不影响同机安装的主线 v1.2。

## 更新日志

详见 [CHANGELOG.md](CHANGELOG.md)。

## 许可证

[MIT License](LICENSE)

第三方依赖许可证（仅主线用到）：litehtml 为 BSD 3-Clause，gumbo 为 Apache License 2.0。
