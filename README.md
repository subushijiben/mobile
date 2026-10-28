# mobile · 教室上下课提醒

一个 Windows 桌面小工具：到上下课时间，屏幕顶部会弹出一个类似手机「灵动岛」的提醒并响铃。单文件、免安装，双击即可自动部署并常驻托盘。

界面使用 Win32 API + 分层窗口（Layered Window）全自绘；AI 岛的气泡排版借助第三方 HTML 渲染库 [litehtml](https://github.com/litehtml/litehtml)（底层用 [gumbo](https://github.com/google/gumbo-parser) 解析 HTML）。

## 功能特性

- **多档「岛」**
  - 大岛：上课 / 下课 / 自定义提醒时弹出，显示事件与时间，约 5 秒后自动收回；宽度随文字自适应
  - 小岛：课间出现，显示「距上课 mm:ss」倒计时；**只要处于课间就显示**（打钩只控制铃声，倒计时全局生效）
  - 迷你岛：贴在屏幕顶边的刘海式小时钟，只显示实时时间；静止 10 秒后自动降到约半透明，悬浮样式、四角全圆角
  - 消息岛：多条消息以气泡形式并排（最多 6 条）
- **AI 岛**：点岛唤出的对话界面，带天空背景（日出 / 日落随时间变化，星星月亮），气泡用 litehtml 排版；支持连续对话，消息岛从下方冒出时 AI 岛不退出。API Key 用 **DPAPI 加密**存本机（绑定当前用户和机器，注册表被拷走也解不开），界面不回显
- **每节课独立铃声**：每节课的上课铃 / 下课铃可分别勾选并调音量；开关用位掩码存储，22 个音量打包成一个二进制值
- **放学《回家》**：每天固定 19:30 自动播放同目录的 `Home.mp3`，大岛变成音乐播放器（左封面 / 中歌名 / 右播放-暂停键），封面读同目录的 `Home.jpg`
- **实时天气**：后台线程通过 WinHTTP 拉取，超时短、失败静默
- **麦克风 / 摄像头占用提示**：设备被占用时弹出大岛，左侧自绘图标 + 右侧文字。数据来自 Windows 系统隐私设备使用记录，只读
- **三套作息表**：初四夏季 / 冬季（默认）/ 初四冬季，在设置窗口里随时切换
- **图形设置窗口**：左侧导航分页（作息表 / 时间提醒 / AI 岛 / 主题 / 迷你岛 / 更多），导航栏底部常驻运行状态（运行中 / 现在时间 / 下次提醒），改动即时生效并自动保存
- **11 步全功能自检**：设置里「立即测试」会依次演示收展、上下课、课间小岛、静置变淡、AI 岛、消息岛、播放器等全部形态（约 37 秒，期间屏蔽真实闹钟）
- **特殊节日皮肤**：中秋、国庆（含节前一天）自动切换节日配色，可在设置中关闭
- **操作方式**
  - 长按小岛 / 迷你岛：关闭
  - 从屏幕顶边向下滑：展开迷你岛（支持触控屏）
  - `Ctrl + Alt + A`：立刻弹一次大岛（测试用）
  - `Ctrl + Alt + S`：打开设置窗口
- 设置保存在注册表 `HKCU\Software\IslandAlarm`

## 快速开始

到 [Releases](../../releases) 下载最新版本的完整包（`moblie.zip`），解压后包含：

- `mobile-v1.2.exe`：主程序
- `alin.mp3`：上课 / 下课提醒音
- `Home.mp3`：放学《回家》
- `Home.jpg`：音乐播放器封面（可选，没有就不显示封面）
- `smtc_probe.exe`：附带的辅助工具

把这些文件放在**同一个目录**，双击 `mobile-v1.2.exe` 即可：程序会自动完成部署、把数据文件搬进安装目录并常驻托盘。使用 AI 岛前，在「设置 → AI 岛」里填入你的 API Key。

## 从源码编译

### 环境要求

- Windows 10 及以上
- [MinGW-w64](https://www.mingw-w64.org/) 工具链（GCC，支持 C++17），例如 MSYS2、Dev-C++ 自带的 g++
- 第三方库 **litehtml** 与 **gumbo**（需自行编译，得到 `liblitehtml.a`、`libgumbo.a` 及头文件）

### 准备依赖

下载并编译两个第三方库：

```
git clone https://github.com/litehtml/litehtml.git
git clone https://github.com/google/gumbo-parser.git
```

按各自说明编译后，在仓库根目录建 `third_party` 目录，放成如下结构：

```
third_party\
├── litehtml\
│   ├── include\litehtml.h
│   └── liblitehtml.a
└── gumbo\
    ├── src\gumbo.h
    └── libgumbo.a
```

### 编译命令

```bat
g++ -O2 -std=c++17 -mwindows -static -s -o mobile.exe mobile.cpp ^
    -I"third_party\litehtml\include" -I"third_party\gumbo\src" ^
    -L"third_party\litehtml" -L"third_party\gumbo" ^
    -luser32 -lgdi32 -lmsimg32 -lgdiplus -lwinhttp -limm32 -ladvapi32 -lcrypt32 ^
    -lshell32 -lwinmm -lole32 -luuid -lcomctl32 ^
    -llitehtml -lgumbo
```

也可以直接运行仓库中的 `build.bat`（依赖路径可用环境变量 `LITEHTML_DIR`、`GUMBO_DIR` 覆盖）。

各库用途：`-lgdiplus` 解码封面图片；`-lwinhttp` 取实时天气与 AI 请求；`-limm32` 控制输入法候选窗；`-lcrypt32` 提供 DPAPI 加密；`-llitehtml -lgumbo` 渲染 AI 岛气泡。

> 原项目编译命令中的 `app_res.o` 仅含可选的版本信息资源，程序本身不依赖自定义资源（托盘图标使用系统默认图标），不链接它不影响功能。

## 作息表

内置三套作息表，在设置窗口切换，选择自动保存到注册表。时间定义在源码 `LoadAlarmTimes()` 中，需要修改时直接编辑对应数组后重新编译。

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

## 卸载

在命令行中运行：

```bat
mobile-v1.2.exe --uninstall
```

会删除桌面快捷方式与开机自启动项；设置仍保留在注册表 `HKCU\Software\IslandAlarm` 中，如需彻底清除可手动删除该键。

## 更新日志

详见 [CHANGELOG.md](CHANGELOG.md)。

## 许可证

[MIT License](LICENSE)

第三方依赖许可证：litehtml 为 BSD 3-Clause，gumbo 为 Apache License 2.0。
