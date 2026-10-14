// mobile · 教室上下课提醒 · 单文件 Win32（分层窗口全自绘）
// 编译: g++ -O2 -std=c++17 -mwindows -static -s -o mobile.exe mobile.cpp app_res.o \
//       -luser32 -lgdi32 -lmsimg32 -ladvapi32 -lshell32 -lwinmm -lole32 -luuid -lcomctl32

#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <shellapi.h>
#include <commctrl.h>   // 滑块控件（音量）
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <shlobj.h>   // SHGetFolderPathW（取用户目录/桌面等路径）
#include <gdiplus.h>  // 解码 Home.jpg / Home.png 当专辑封面（GDI+ 比 OleLoadPicturePath 稳）
#pragma comment(lib, "gdiplus.lib")

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comctl32.lib")

const int ISLAND_WIDTH_PERCENT = 50;
const int ISLAND_HEIGHT = 80;
const int ISLAND_MIN_WIDTH = 120;  // 弹出起始小岛宽度
const int ISLAND_MIN_HEIGHT = 28;  // 弹出起始小岛高度
const int ISLAND_FINAL_Y = 20;     // 定格时的 y 坐标
const int ANIMATION_STEPS = 62;          // 弹出动画步数（62 帧；节拍 FRAME_INTERVAL_MS=16ms，
                                         //  实测含消息循环开销约 19.5ms/帧 → 实际约 1.2s）
const int SLIDEOUT_STEPS  = 31;          // 收回动画步数（31 × 16ms ≈ 500ms）
const int FRAME_INTERVAL_MS = 16;        // 动画帧间隔 16ms ≈ 60fps
const int DISPLAY_DURATION_MS = 5000;
// ---- 常驻小岛 ----
const int MINI_WIDTH    = 320;   // 常驻小岛宽度
const int MINI_HEIGHT   = 56;    // 常驻小岛高度

// ===== 迷你岛（1.1.13 加）：贴在屏幕顶边的"刘海"，只显示实时时间 =====
// 顶边与屏幕边齐平（上两角不圆角），下两角圆角 —— 就像手机刘海那样挂在屏幕边上。
// 用户要求：小岛只管倒计时，实时时间归迷你岛；迷你岛静止一段时间后降到半透明。
const int NOTCH_WIDTH   = 150;   // 迷你岛宽度
const int NOTCH_HEIGHT  = 48;    // 迷你岛高度
// 迷你岛改成"悬浮"样式（2026-10-13 用户要求）：不再贴着屏幕顶边，而是像大岛/小岛那样浮在下面，
// 只留 2 个像素的缝 —— 大改动是"四角都圆角"（原来上两角是直角）。
// 好处是和大岛/小岛之间切换时形状不用变，视觉上顺得多；缝只有 2px，不仔细看还是"贴顶"的感觉。
const int NOTCH_Y       = 2;     // 迷你岛距屏幕顶边的缝隙（用户要 1~2px）
const int NOTCH_DIM_MS  = 10000; // 静止多久才降半透明
const int NOTCH_DIM_A   = 115;   // 半透明程度（255=不透明；115 ≈ 45%）
const int MINI_STEPS    = 46;    // 小岛弹出 / 小→大变身 的步数（46 帧，实测约 0.9s）
                                 // 注意：大→小变身不用它，用的是 ANIMATION_STEPS（与展开大岛同速率）
const int HINT_GAP      = 30;    // 小岛下方"长按关闭常驻岛"那行字要占的高度

// ===== 音乐播放器样式（放学《回家》在放的时候，大岛变成播放器）=====
// 用户要求：只要一个最主要的"播放/暂停"键，不要歌词、不要进度条；
// 版式 = 左边专辑封面(Home.jpg) + 中间歌名 + 右边播放/暂停键；
// 整体是一个"比普通大岛大一圈的长方形"（长宽都加，主要加长）。
const int PLAYER_W     = 640;    // 播放器岛宽（普通大岛是自适应 424 左右）
const int PLAYER_H     = 124;    // 播放器岛高（普通大岛是 80）
const int PLAYER_BTN_D = 60;     // 播放/暂停键直径
const int PLAYER_PAD_R = 26;     // 按钮距离岛右边
const int PLAYER_PAD_L = 22;     // 封面距离岛左边
const int COVER_S      = 88;     // 专辑封面边长（正方形，居中裁切）
const int COVER_RAD    = 16;     // 封面圆角半径
const int PLAYER_LONG  = 600000; // 播放器定格时长（10 分钟，够放完一首；放完由 MM_MCINOTIFY 收掉）
const int PLAYER_PAUSE_CLOSE_MS = 5000; // 按下暂停后超过 5 秒还没继续播 → 自动收掉播放器

// ===== 麦克风 / 摄像头占用提示（2026-10-13 用户要求）=====
// 侦测到麦克风或摄像头被占用 → 弹大岛提示："[图标] 麦克风被使用中"。
// 图标是自绘的（左上角那个位置），岛宽跟着文字自适应，外面再给图标留出位置。
const int PRIV_ICON_W = 34;      // 图标边长（按大岛高度缩放）
const int PRIV_PAD_L  = 20;      // 图标距岛左边
const int PRIV_GAP    = 14;      // 图标与文字之间的间距
const int LONGPRESS_MS  = 800;   // 长按多久算长按
const int SWIPE_HOLD_MS = 3000;  // 顶部下滑后停留多久展开常驻岛
const int SWIPE_DY      = 60;    // 下滑多少像素才算"下滑"

enum AlarmType {
    ALARM_TYPE_CLASS_START = 0,
    ALARM_TYPE_CLASS_END = 1,
    ALARM_TYPE_SINGLE = 2
};

struct AlarmTime {
    int hour, minute, second;
    AlarmType type;
    bool enabled;
    int period;      // 属于第几个时段(0..pc-1)：排序后靠它跟菜单勾选对齐，别再用 i*2 猜
};

// 画布尺寸：按动画过冲范围自动推导
int g = 0;
int j = 0, l = 0, n = 0;


HWND o     = NULL;
// ---- 提醒音 ----
wchar_t nn[MAX_PATH] = {0};   // alin.mp3 的绝对路径（空=没找到）
bool    fuck  = true;             // 是否播放（托盘「提醒音」里可关）
bool q = false;

bool z = false;
int B = 0, C = 0;
int D = 0, E = 0;
int F = 0, G = 0;
float I = 0.0f;
int J = 0;
bool L = false;
AlarmType M = ALARM_TYPE_SINGLE;
int N = -1;
int O = 0;
int P = 0;
bool Q = false;
LONGLONG R = 0;   // 下一帧时间（QPC 毫秒）
LONGLONG S = 0;     // 定格结束时间
DWORD T = 0;

// GDI 缓存
// 别每帧重新 Create，不然跑一会儿句柄就爆了（血泪）
static HDC      U = NULL;
static HBITMAP  V = NULL;
static BYTE*    X = NULL;
static HDC      Y = NULL;
static HBITMAP  Z = NULL;
static BYTE*    aa = NULL;
static int      cc = 0;               // 画布缓存宽度（dd 原先存高度但从没被读过，已删）
static std::map<int, HFONT> ee[2];

// 三套作息表，运行时切换（注册表 TimeTable：0=初四夏季 11 节 / 1=冬季 9 节 / 2=初四冬季 9 节）
const wchar_t* ffA[] = {          // 初四夏季（含早读 / 午自习）
    L"早读", L"第1节课", L"第2节课", L"第3节课", L"第4节课",
    L"午自习", L"第5节课", L"第6节课", L"第7节课", L"课后服务1", L"课后服务2"
};
const wchar_t* ffB[] = {          // 冬季
    L"第1节课", L"第2节课", L"第3节课", L"第4节课", L"第5节课",
    L"第6节课", L"第7节课", L"课后服务1", L"课后服务2"
};
const wchar_t* ffC[] = {          // 初四冬季（第7/8/9 节与冬季不同，时间另算）
    L"第1节课", L"第2节课", L"第3节课", L"第4节课", L"第5节课",
    L"第6节课", L"第七节", L"第八节", L"第九节"
};
const wchar_t** ff = ffA;     // 当前用的名字表
int  pc = 11;                 // 当前时段数（原 PERIOD_COUNT）
int  tbl = 0;                 // 当前作息表：0=初四夏季 / 1=冬季 / 2=初四冬季
void LoadTimeTable();
void SaveTimeTable();

wchar_t gg[128] = {0};
bool hh = false;
POINT ii = {0,0};
std::vector<AlarmTime> jj;

HANDLE ll = NULL;
HANDLE kk = NULL;
HANDLE mm = NULL;

// ---- 常驻小岛 / 交互 / 双音量（短名照旧，含义看注释）----
bool      bb = false;              // 常驻小岛"应该显示"（目标状态，有课间倒计时或手动展开时为真）
int       oo = 0;                  // 小岛/变身动画已走步数
bool      pp = false;              // 小岛当前显示的是倒计时(true)还是时间(false)
LONGLONG  rr = 0;                  // 倒计时目标时刻（毫秒）
LONGLONG  ja = 0;                  // 课间刚结束的时刻：给"上课"闹钟留的衔接窗口（0=不在窗口内）
bool      he = false;              // 特殊日期：这一轮"下课"显示完之后要接一句「国庆节快乐」
int       hs = 0;                  // 上面那段的阶段步数
int       ta = 255;                // 文字总透明度倍率（255=正常）—— 只给"两段式"淡出淡入用
bool      nt = false;              // 当前是不是"迷你岛"（贴顶边的刘海）→ 决定用哪种形状画
bool      ns = true;               // 迷你岛是否启用（托盘二级菜单可开关，存注册表）
bool      hon = true;              // 是否开启"特殊节日提醒"（节日皮肤 + 放学问候），存注册表 HolidaySkin
int       nv = 255;                // 迷你岛当前的全局不透明度（降到半透明时 ≈115）
LONGLONG  na = 0;                  // 迷你岛"变活跃"的时刻，用来算多久后降半透明
// 变身（J=5 缩小 / J=9 放大）的**目标**：这样六种方向（大↔小、大↔迷你、小↔迷你）共用同一套代码
int       gw = MINI_WIDTH, gh = MINI_HEIGHT, gy = ISLAND_FINAL_Y, gJ = 7;
// 大岛当前"应有的高度"：普通闹钟是 80，播放器是 124。
// 弹出/收回/变身/画布尺寸都按它算，这样播放器才能长成一个更高的长方形。
int       bigH = ISLAND_HEIGHT;
float     vv = 0.0f;               // 文字交叉渐变：0=显示大岛文字，1=显示小岛文字
int       ww = 0;                  // 提示文字("长按关闭常驻岛")的透明度 0~255
bool      xx = false;              // 当前有按下（鼠标/触控）
LONGLONG  yy = 0;                  // 本次按下的起始时刻
bool      zz = false;              // 检测到"从顶部往下滑"的手势
LONGLONG  ag = 0;                  // 手势起始时刻
int       ah = 0;                  // 手势起始 y
bool      af = false;              // 提示文字是否该显示
int       ab = 30;                 // 上课铃音量 0~100
int       FUCK = 30;               // 下课铃音量 0~100（fuck=要不要响，FUCK=下课铃多响）
int       ai = -1;                  // 上次重绘用的"秒"（避免无谓重画）
int       ae = 0, an = 0, ao = 0;   // 变身的**起始**尺寸/位置（从当前实际尺寸接着长，接缝处不跳变）
float     vv0 = 0.0f, vt = 1.0f;    // 变身的起始/目标文字权重（大岛字=0，小岛字=1）
// ---- 音乐播放器 / 放学《回家》----
// 这几个状态必须声明在 DrawIsland 之前（岛要按 bgmPause 画三角还是双竖条）
bool      ply = false;              // 大岛是不是"播放器样式"（在放/暂停中；收回动画走完才清）
float     plyA = 255.0f;            // 播放器内容的不透明度（"下课了 → 播放器"过渡时 0→255）
bool      plyMix = false;           // 正在做"下课了 → 播放器"的过渡：ta/plyA 交给 J=9 的进度驱动
LONGLONG  plyPauseAt = 0;           // 按下暂停的时刻（用来判"暂停 5 秒还没播就收掉"）
// 麦克风/摄像头提示
int       privKind = 0;             // 当前这条提示是哪种：0=不是隐私提示 1=麦克风 2=摄像头 3=两个都在
int       privWant = 0;             // 挂着待弹的那条（岛正忙时先存着）
bool      privWait = false;         // 有挂着的没弹出去
bool      bgmOn = true;             // 是否播放《回家》（GUI 可关）
int       bgmV  = 15;               // 《回家》音量 0~100，默认 15%
bool      bgmPlay = false;          // 当前是否在播（MCI 已打开）
bool      bgmPause = false;         // 暂停中（曲子还在，只是没往前走）
RECT      plyBtn = {0,0,0,0};       // 播放/暂停键的屏幕矩形（命中测试用，每帧更新）
BYTE*     covPx = NULL;             // 专辑封面解码后的预乘 BGRA（COVER_S×COVER_S），NULL=没有封面
float     sf = 0.0f;                // 形状因子：0=普通圆角弹窗，1=吸顶刘海形
float     sf0 = 0.0f, sf1 = 0.0f;   // 变身过程中的形状起止

// 只有"岛形"那一块让窗口接收输入，其它地方一律穿透 —— 否则顶部 100px 会挡住下面窗口的点击
bool IslandHitTest(POINT ptScreen)
{
    if (J == 0 || F < 2 || G < 2) return false;
    // D/E 是岛在**屏幕坐标**下的左上角（画布里的位置是 D-n，见 DrawIsland 的 ox = D - n）。
    // 这里以前写成 ptScreen.x - n 再跟 D 比 —— 多减了一个窗口偏移 n，
    // 于是整个可点区域被右移了 n 像素（n 最多能到几百），大岛/小岛根本点不到。
    return (ptScreen.x >= D && ptScreen.x < D + F &&
            ptScreen.y >= E && ptScreen.y < E + G);
}

#define WM_ALARM_TRIGGER (WM_USER + 1)
#define WM_TRAYICON (WM_USER + 2)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TEST 1002
#define ID_TRAY_NEXT 1003
#define ID_TRAY_AUTOSTART 1004
#define ID_MINI_SHOW       1030
#define ID_MINI_HIDE       1031
#define ID_NOTCH_TOGGLE    1032
#define ID_TRAY_HOLIDAY    1017
#define ID_TRAY_SETTINGS   1018
#define ID_TBL_OLD         1040
#define ID_TBL_WINTER      1041   // = 冬季作息
#define ID_TBL_W4          1042   // = 初四冬季作息
#define ID_TRAY_AUDIO_ON   1020
#define ID_PERIOD_BASE 2001
#define ID_TIMER_RESHOW_MENU 99
#define ID_TRAY_THEME_LIGHT 1010
#define ID_TRAY_THEME_DARK 1011
#define ID_TRAY_STATUS 1013
#define ID_TRAY_TRANSPARENCY 1014
#define ID_TRAY_UNINSTALL 1016
#define HOTKEY_ID_DEBUG 1
#define HOTKEY_ID_SETTINGS 2
#define HOTKEY_ID_PLAYER 3

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CreateIslandWindow(HINSTANCE hInstance);
void StartTimerThread();
DWORD WINAPI TimerThreadProc(LPVOID lpParam);
void LoadAlarmTimes();
void CreateTrayIcon(HWND hWnd);
void RemoveTrayIcon(HWND hWnd);
void ShowNextAlarmTime();
void ToggleAutoStart();
void TriggerAlarm(AlarmType type);
void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha);
void DrawIsland(BYTE* p, int stride, int width, int height);
void CompositeGlyph(BYTE* p, const BYTE* mask, int stride, int x0, int y0, int x1, int y1, COLORREF color);
bool EnsureBuffers();
void CleanupCachedResources();
void AnimTick();
LONGLONG NowMs();
void LoadTheme();
void SaveTheme();
void ShowMessageIsland(const wchar_t* text, int extraW = 0);
void ShowPlayerIsland();                   // 让大岛以"音乐播放器"样式出现并定格
void PlayerTick();                         // 播放器的低频心跳（只负责那行提示淡出）
void ClosePlayer();                        // 停音乐 + 收回播放器岛
bool PlayerButtonHit(POINT ptScreen);      // 命中播放/暂停键 → 切一下
void BgmToggle();                          // 播放 / 暂停
void BgmStop();                            // 停掉并关掉播放器
void LoadPeriodEnabled();
void AudioPlay(AlarmType type);
HICON AppIcon(bool small = false);
void AudioPlayAsync(AlarmType type);       // 后台线程播放，不占 UI 线程
void AudioStop();
void AudioResolvePath();
void LoadAudioCfg();
void SaveAudioCfg();
void SavePeriodEnabled();
void LoadNotchCfg();
void SaveNotchCfg();
void SaveHolidayCfg();
void ShowSettingsWindow(HWND owner);
void ShowTrayMenu(HWND hWnd);
void AlarmLabel(AlarmType t, wchar_t* out);
void FitBigWidth(const wchar_t* label, const wchar_t* big, const wchar_t* extra, int extraPx = 0);
void EnableDpiAwareness();
DWORD GetOsBuild();

LONGLONG NowMs();
void ComputeCanvasSize();
void PrepareBackdropBeforeShow();
void PopTick(int tw, int th, int maxStep, int* step);
void ShowMiniIsland();
void HideMiniIsland();
void MiniHoldTick();
void ShowNotch();
void HideNotch();
void NotchHoldTick();
bool GetPeriodTime(int period, AlarmType type, int* hh, int* mm);
bool BreakInfoActive(LONGLONG* outTargetMs);
bool NextBreakTarget(LONGLONG* outTargetMs);
bool PeriodEnabled(int i);
void GestureTick();
void PollGesture();
bool EnsureBuffers();
void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha);
void AnimTick();

// ===== 缓动 =====
float easeOutCubic(float t)
{
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// 两端速度为 0 的平滑缓动（收回用）
float easeInOutCubic(float t)
{
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - (u * u * u) / 2.0f;
}

// 欠阻尼弹簧：冲过目标再回弹。宽/高/位移三条参数故意不同步 → 才像果冻在颤
float springCurve(float t, float a, float b)
{
    return 1.0f - expf(-a*t) * (cosf(b*t) + (a/b)*sinf(b*t));
}
float springW(float t) { return springCurve(t, 7.5f, 12.0f); }  // 宽度：过冲约 14%
float springH(float t) { return springCurve(t, 6.5f, 13.0f); }  // 高度：过冲约 16%（比宽度多一点 → 更有弹性）
float springY(float t) { return springCurve(t, 4.5f, 11.5f); }  // 竖向位移：过冲约 28%

// 扫描整段曲线取最大范围：过冲时不被裁
void ComputeCanvasSize()
{
    float maxW = (float)g;
    float maxBottom = (float)(ISLAND_FINAL_Y + bigH);
    for (int i = 0; i <= 512; i++)
    {
        float t = i / 512.0f;
        float w = ISLAND_MIN_WIDTH  + (g - ISLAND_MIN_WIDTH) * springW(t);
        float h = ISLAND_MIN_HEIGHT + (bigH - ISLAND_MIN_HEIGHT) * springH(t);
        float y = ISLAND_FINAL_Y * springY(t);
        if (w > maxW) maxW = w;
        if (y + h > maxBottom) maxBottom = y + h;
    }
    j = (int)maxW + 8;
    l = (int)maxBottom + 8;
    if (j < g) j = g;
    if (l < ISLAND_FINAL_Y + bigH) l = ISLAND_FINAL_Y + bigH;
    // 常驻小岛 + 它下方那行提示文字也要完整画出来
    int miniBottom = ISLAND_FINAL_Y + MINI_HEIGHT + HINT_GAP;
    if (l < miniBottom) l = miniBottom;
    // 大岛下面那行提示（长按关闭…）也要画得下
    if (l < ISLAND_FINAL_Y + bigH + HINT_GAP) l = ISLAND_FINAL_Y + bigH + HINT_GAP;
    if (j > B) j = B;
    n = (B - j) / 2;
}

// 毫秒时钟
LONGLONG NowMs()
{
    LARGE_INTEGER f, c;
    if (!QueryPerformanceFrequency(&f) || f.QuadPart == 0) return GetTickCount64();
    QueryPerformanceCounter(&c);
    return (LONGLONG)(c.QuadPart * 1000LL / f.QuadPart);
}

// 节日皮肤：中秋（按年查表）/ 国庆（固定 10/1~10/7）；HolidayForce=1/2 强制预览，HolidaySkin=0 关掉
struct HolidayDate { int y, m, d; };
static const HolidayDate g_midautumn[] = {
    {2026, 9,25}, {2027, 9,15}, {2028,10, 3}, {2029, 9,22}, {2030, 9,12},
    {2031,10, 1}, {2032, 9,19}, {2033, 9, 8}, {2034, 9,27}, {2035, 9,16},
};

static bool IsMidAutumn(int y, int m, int d)
{
    for (const auto& hd : g_midautumn)
        if (y == hd.y && m == hd.m && d == hd.d) return true;
    return false;
}
static int DaysInMonth(int y, int m)
{
    static const int dm[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return (m >= 1 && m <= 12) ? dm[m] : 30;
}

// 返回 0=无 / 1=中秋 / 2=国庆。范围含"前一天"——学校当天不上学，只有前一天有人看
int HolidayToday()
{
    static int force = -1;              // -1=还没读过；0=没强制；1/2=强制；9=强制关闭
    static int forceDate = 0;           // yyyymmdd：按指定日期假装"今天"（预览/测试用）
    if (force < 0)
    {
        force = 0;
        HKEY hKey;
        if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            DWORD v = 0, sz = sizeof(v);
            if (RegQueryValueEx(hKey, L"HolidayForce", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) force = (int)v;
            v = 0; sz = sizeof(v);
            if (RegQueryValueEx(hKey, L"HolidayForceDate", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) forceDate = (int)v;
            v = 1; sz = sizeof(v);
            if (RegQueryValueEx(hKey, L"HolidaySkin", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) hon = (v != 0);
            RegCloseKey(hKey);
        }
        if (force != 1 && force != 2 && force != 9) force = 0;
        if (forceDate < 19000101 || forceDate > 29991231) forceDate = 0;
    }
    if (force == 9) return 0;           // 9 = 强制关闭
    if (force) return force;            // 强制预览优先（测试/预览用）
    if (!hon) return 0;                 // 用户把"特殊节日提醒"关掉了 → 当作平常日子

    int y, m, d;
    if (forceDate) { y = forceDate / 10000; m = (forceDate / 100) % 100; d = forceDate % 100; }
    else { SYSTEMTIME st; GetLocalTime(&st); y = st.wYear; m = st.wMonth; d = st.wDay; }

    static int lastDay = -1, cached = 0;    // 按"日期"缓存，跨零点也会自动更新
    int day = y * 10000 + m * 100 + d;
    if (day != lastDay)
    {
        lastDay = day;
        cached = 0;
        if (IsMidAutumn(y, m, d))                                cached = 1;
        else if (m == 10 && d >= 1 && d <= 7)                    cached = 2;   // 国庆假期
        else                                                     // 否则看"明天"是不是节日
        {
            int ny = y, nm = m, nd = d + 1;
            if (nd > DaysInMonth(ny, nm)) { nd = 1; if (++nm > 12) { nm = 1; ++ny; } }
            if (IsMidAutumn(ny, nm, nd))                         cached = 1;
            else if (nm == 10 && nd == 1)                        cached = 2;
        }
    }
    return cached;
}

// 节日文案：用户要求就写「中秋快乐」/「国庆快乐」，不要"明天就是…"那套
const wchar_t* HolidayGreet()
{
    int h = HolidayToday();
    return h == 1 ? L"中秋快乐" : (h == 2 ? L"国庆快乐" : L"");
}
const wchar_t* HolidayMark() { return HolidayToday() == 1 ? L" ●" : (HolidayToday() == 2 ? L" ★" : L""); }

// 岛形覆盖率：一个函数画完"普通圆角矩形"到"吸在屏幕边缘的刘海形"的连续过渡
//   sf=0 → 普通弹窗（四角圆角）；sf=1 → 迷你岛（上边齐平 + 顶边两侧向外拐的小圆弧）
//   中间值给变身动画用，形状随 sf 平滑变形，收尾时不会再"咯噔"一下
static inline float IslandCoverage(int lx, int ly, int w, int h, int rx, int ry, float sf)
{
    if (w <= 0 || h <= 0) return 0.0f;
    int bx0 = 0, bx1 = w;                          // 主体就是整宽（外拐圆弧已去掉）
    int topR = (int)(rx * (1.0f - sf) + 0.5f);     // 上两角：普通圆角 → 迷你岛时收成直角
    float best;
    {
        float d = 0.0f;
        if (lx < bx0) d = (float)(bx0 - lx);
        else if (lx >= bx1) d = (float)(lx - bx1 + 1);
        if (ly < 0) d = (float)(-ly);
        else if (ly >= h) { float t = (float)(ly - h + 1); if (t > d) d = t; }
        if (ry > 1 && ly >= h - ry && (lx < bx0 + ry || lx >= bx1 - ry))
        {
            int dx = (lx < bx0 + ry) ? (bx0 + ry - lx) : (lx - (bx1 - ry));
            int dy = ly - (h - ry);
            float v = (float)dx*dx/(float)(ry*ry) + (float)dy*dy/(float)(ry*ry);
            if (v <= 1.0f) d = 0.0f;
            else { float e = (sqrtf(v) - 1.0f) * (float)ry; if (e > d) d = e; }
        }
        if (topR > 1 && ly < topR && (lx < bx0 + topR || lx >= bx1 - topR))
        {
            int dx = (lx < bx0 + topR) ? (bx0 + topR - lx) : (lx - (bx1 - topR));
            int dy = topR - ly;
            float v = (float)dx*dx/(float)(topR*topR) + (float)dy*dy/(float)(topR*topR);
            if (v <= 1.0f) d = 0.0f;
            else { float e = (sqrtf(v) - 1.0f) * (float)topR; if (e > d) d = e; }
        }
        best = (d <= 0.0f) ? 1.0f : (d >= 1.5f ? 0.0f : 1.0f - d/1.5f);
    }
    if (best >= 1.0f) return 1.0f;
    return best;
}

// 刘海形覆盖率：**顶边齐平**（上两角不圆角）、只有下两角圆角 —— 迷你岛专用
static inline float NotchCoverage(int lx, int ly, int w, int h, int r)
{
    if (w <= 0 || h <= 0) return 0.0f;
    float d = 0.0f;
    if (lx < 0) d = (float)(-lx);
    else if (lx >= w) d = (float)(lx - w + 1);
    if (ly < 0) { float t = (float)(-ly); if (t > d) d = t; }
    else if (ly >= h) { float t = (float)(ly - h + 1); if (t > d) d = t; }
    // 只有下边两个角参与圆角
    if (r > 1 && ly >= h - r && ((lx < r) || (lx >= w - r)))
    {
        int dx = (lx < r) ? (r - lx) : (lx - (w - r));
        int dy = ly - (h - r);
        float v = (float)dx*dx/(float)(r*r) + (float)dy*dy/(float)(r*r);
        if (v <= 1.0f) d = 0.0f;
        else { float e = (sqrtf(v) - 1.0f) * (float)r; if (e > d) d = e; }
    }
    if (d <= 0.0f) return 1.0f;
    if (d >= 1.5f) return 0.0f;
    return 1.0f - d / 1.5f;
}

// 圆角覆盖率，边界 1.5px 羽化
static inline float ShapeCoverage(int lx, int ly, int w, int h, int rx, int ry)
{
    return IslandCoverage(lx, ly, w, h, rx, ry, sf);
    if (w <= 0 || h <= 0) return 0.0f;
    float d = 0.0f;
    if (lx < 0) d = (float)(-lx);
    else if (lx >= w) d = (float)(lx - w + 1);
    if (ly < 0) { float t = (float)(-ly); if (t > d) d = t; }
    else if (ly >= h) { float t = (float)(ly - h + 1); if (t > d) d = t; }
    if (rx > 1 && ry > 1 && ((lx < rx) || (lx >= w - rx)) && ((ly < ry) || (ly >= h - ry)))
    {
        int dx = (lx < rx) ? (rx - lx) : (lx - (w - rx));
        int dy = (ly < ry) ? (ry - ly) : (ly - (h - ry));
        float v = (float)dx*dx/(float)(rx*rx) + (float)dy*dy/(float)(ry*ry);
        if (v <= 1.0f) d = 0.0f;
        else { float e = (sqrtf(v) - 1.0f) * ((rx + ry) * 0.5f); if (e > d) d = e; }
    }
    if (d <= 0.0f) return 1.0f;
    if (d >= 1.5f) return 0.0f;
    return 1.0f - d / 1.5f;
}

// 缩放屏下不感知 DPI 会坐标错位
void EnableDpiAwareness()
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) return;
    typedef BOOL (WINAPI* pfnSPDAC)(HANDLE);
    pfnSPDAC pAC = (pfnSPDAC)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
    if (pAC)
    {
        SetLastError(0);
        if (pAC((HANDLE)-4)) { Q = true; return; }
        // 清单已声明时再调用会失败 → 当已启用
        if (GetLastError() == ERROR_ACCESS_DENIED) { Q = true; return; }
        SetLastError(0);
        if (pAC((HANDLE)-2)) { Q = true; return; }
        if (GetLastError() == ERROR_ACCESS_DENIED) { Q = true; return; }
    }
    typedef BOOL (WINAPI* pfnSPDA)(void);
    pfnSPDA pA = (pfnSPDA)GetProcAddress(hUser, "SetProcessDPIAware");
    if (pA && pA()) Q = true;
    else if (GetLastError() == ERROR_ACCESS_DENIED) Q = true;
}

DWORD GetOsBuild()
{
    typedef LONG (WINAPI* pfnRtlGetVersion)(OSVERSIONINFOEXW*);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll)
    {
        pfnRtlGetVersion p = (pfnRtlGetVersion)GetProcAddress(hNtdll, "RtlGetVersion");
        if (p)
        {
            OSVERSIONINFOEXW os = {0};
            os.dwOSVersionInfoSize = sizeof(os);
            if (p(&os) == 0) return os.dwBuildNumber;
        }
    }
    return 0;
}

bool EnsureBuffers()
{
    int W = j, H = l;
    // 画布尺寸会随岛宽变化（大岛宽度自适应），尺寸一变就必须重建 DIB ——
    // 否则 UpdateLayeredWindow 会读到缓冲区外面，屏幕上就是一块"随岛变大"的黑块
    if ((U && Y) && cc == W) return true;
    if (V) { DeleteObject(V); V = NULL; X = NULL; }
    if (Z) { DeleteObject(Z); Z = NULL; aa = NULL; }
    if (U) { DeleteDC(U); U = NULL; }
    if (Y) { DeleteDC(Y); Y = NULL; }
    if (W < 2 || H < 2) return false;
    HDC hdcScreen = GetDC(NULL);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W; bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    V  = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, (void**)&X, NULL, 0);
    Z = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, (void**)&aa, NULL, 0);
    U  = CreateCompatibleDC(hdcScreen);
    Y = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);
    if (!V || !Z) return false;
    if (!U || !Y) return false;
    SelectObject(U, V);
    SelectObject(Y, Z);
    SetBkMode(Y, TRANSPARENT);
    SetTextColor(Y, RGB(255,255,255));
    cc = W;
    return true;
}

void CleanupCachedResources()
{
    if (U)  { DeleteDC(U);   U = NULL; }
    if (V)  { DeleteObject(V); V = NULL; }
    if (Y) { DeleteDC(Y);  Y = NULL; }
    if (Z) { DeleteObject(Z); Z = NULL; }

    X = aa = NULL;
    for (int i = 0; i < 2; i++)
    {
        for (auto& kv : ee[i]) DeleteObject(kv.second);
        ee[i].clear();
    }
}

static HFONT GetCachedFont(int size, bool bold)
{
    std::map<int, HFONT>& m = ee[bold ? 1 : 0];
    auto it = m.find(size);
    if (it != m.end()) return it->second;
    HFONT f = CreateFont(size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    m[size] = f;
    return f;
}

// 蒙版按亮度当 alpha 合成（BGRA 预乘）
void CompositeGlyph(BYTE* p, const BYTE* mask, int stride, int x0, int y0, int x1, int y1, COLORREF color, int alpha)
{
    int sr=GetRValue(color), sg=GetGValue(color), sb=GetBValue(color);
    for (int y=y0; y<y1; y++)
    {
        BYTE* dp = p + ((size_t)y*stride + x0)*4;
        const BYTE* mp = mask + ((size_t)y*stride + x0)*4;
        for (int x=x0; x<x1; x++, dp+=4, mp+=4)
        {
            int sa=mp[2];
            if (sa<=0) continue;
            sa = sa*alpha/255;              // 整体透明度（渐变用）
            if (sa<=0) continue;
            int inv=255-sa, dA=dp[3];
            dp[2]=(BYTE)((sr*sa + dp[2]*inv)/255);
            dp[1]=(BYTE)((sg*sa + dp[1]*inv)/255);
            dp[0]=(BYTE)((sb*sa + dp[0]*inv)/255);
            dp[3]=(BYTE)(sa + dA*inv/255);
        }
    }
}

// ===== 播放器按钮用的形状填充（同样按预乘 BGRA 混合）=====
// 岛上原来只有文字，没有画过"面"。播放键要一个圆底 + 三角/双竖条，所以补三个填充原语。
static inline void BlendPx(BYTE* p, int stride, int W, int H, int x, int y, COLORREF c, int a)
{
    if (a <= 0 || x < 0 || y < 0 || x >= W || y >= H) return;
    BYTE* dp = p + ((size_t)y*stride + x)*4;
    int sr=GetRValue(c), sg=GetGValue(c), sb=GetBValue(c);
    int inv = 255 - a, dA = dp[3];
    dp[0]=(BYTE)((sb*a + dp[0]*inv)/255);
    dp[1]=(BYTE)((sg*a + dp[1]*inv)/255);
    dp[2]=(BYTE)((sr*a + dp[2]*inv)/255);
    dp[3]=(BYTE)(a + dA*inv/255);
}

static void FillDisc(BYTE* p, int stride, int W, int H, int cx, int cy, int r, COLORREF c, int a)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                BlendPx(p, stride, W, H, cx+dx, cy+dy, c, a);
}

// 右向三角（播放）。size = 高；宽约 0.82 倍，视觉重心稍微往右挪一点才看着居中
static void FillPlayTri(BYTE* p, int stride, int W, int H, int cx, int cy, int size, COLORREF c, int a)
{
    int half = size / 2;
    if (half < 1) half = 1;
    int left = cx - size * 36 / 100;
    for (int i = -half; i <= half; i++)
    {
        int wv = (int)((double)size * 0.82 * (1.0 - (double)(i < 0 ? -i : i) / (double)(half + 1)));
        for (int x = 0; x < wv; x++)
            BlendPx(p, stride, W, H, left + x, cy + i, c, a);
    }
}

// 两条竖条（暂停）
static void FillPauseBars(BYTE* p, int stride, int W, int H, int cx, int cy, int size, COLORREF c, int a)
{
    int bw = size / 4;
    if (bw < 2) bw = 2;
    int gap = size / 4;
    int x0 = cx - (bw + gap / 2);
    for (int k = 0; k < 2; k++)
        for (int y = -size/2; y <= size/2; y++)
            for (int x = 0; x < bw; x++)
                BlendPx(p, stride, W, H, x0 + k*(bw+gap) + x, cy + y, c, a);
}

// ===== 专辑封面（Home.jpg）=====
// 用 OLE 的图片解码（不引 GDI+）：解码一次缩成 COVER_S×COVER_S 的预乘 BGRA 缓存起来，
// 之后每帧只是复制一段内存 —— 动画期间反复解码 JPEG 会明显掉帧。
void CoverFree() { if (covPx) { free(covPx); covPx = NULL; } }

bool CoverLoad()
{
    CoverFree();
    wchar_t exe[MAX_PATH] = {0};
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return false;
    wchar_t* sl = wcsrchr(exe, L'\\');
    if (!sl) return false;
    sl[1] = 0;                                   // 只找 exe 同目录（和 Home.mp3 一个规矩）
    const wchar_t* cand[] = { L"Home.jpg", L"Home.jpeg", L"Home.png", L"home.jpg", L"封面.jpg" };
    Gdiplus::Bitmap* bm = NULL;
    wchar_t p[MAX_PATH] = {0};
    for (int i = 0; i < 5 && !bm; i++)
    {
        swprintf_s(p, L"%ls%ls", exe, cand[i]);
        if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
        Gdiplus::Bitmap* t = Gdiplus::Bitmap::FromFile(p, FALSE);
        if (t && t->GetLastStatus() == Gdiplus::Ok) bm = t;
        else if (t) delete t;
    }
    if (!bm) return false;

    bool ok = false;
    UINT sw = bm->GetWidth(), sh = bm->GetHeight();
    if (sw && sh)
    {
        // 居中裁成正方形再缩放：封面多半不是正方形，直接拉伸会变形
        UINT side = (sw < sh) ? sw : sh;
        UINT sx = (sw - side) / 2;
        UINT sy = (sh - side) / 2;
        Gdiplus::Bitmap out(COVER_S, COVER_S, PixelFormat32bppPARGB);   // 预乘 ARGB，正好是我们要的 BGRA
        if (out.GetLastStatus() == Gdiplus::Ok)
        {
            Gdiplus::Graphics g(&out);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            if (g.DrawImage(bm, Gdiplus::Rect(0, 0, COVER_S, COVER_S),
                            (INT)sx, (INT)sy, (INT)side, (INT)side, Gdiplus::UnitPixel) == Gdiplus::Ok)
            {
                Gdiplus::Rect rc(0, 0, COVER_S, COVER_S);
                Gdiplus::BitmapData bd;
                if (out.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Gdiplus::Ok)
                {
                    covPx = (BYTE*)malloc((size_t)COVER_S * COVER_S * 4);
                    if (covPx)
                    {
                        for (int y = 0; y < COVER_S; y++)
                            memcpy(covPx + (size_t)y * COVER_S * 4,
                                   (BYTE*)bd.Scan0 + (size_t)y * bd.Stride, (size_t)COVER_S * 4);
                        ok = true;
                    }
                    out.UnlockBits(&bd);
                }
            }
        }
    }
    delete bm;
    if (!ok) CoverFree();
    return ok;
}

// 把封面画到岛上（缩放到 s×s，四角圆角，和岛的风格一致）
bool CoverBlit(BYTE* p, int stride, int W, int H, int x0, int y0, int s, int mul = 255)
{
    if (!covPx || s < 4 || mul <= 0) return false;
    int r = COVER_RAD * s / COVER_S;
    if (r > s / 2) r = s / 2;
    for (int y = 0; y < s; y++)
    {
        int py = y0 + y;
        if (py < 0 || py >= H) continue;
        int sy = y * COVER_S / s; if (sy >= COVER_S) sy = COVER_S - 1;
        for (int x = 0; x < s; x++)
        {
            int px = x0 + x;
            if (px < 0 || px >= W) continue;
            int cov = 255;
            if (r > 0)
            {
                int dx = 0, dy = 0;
                if      (x < r     && y < r    ) { dx = r - x;      dy = r - y; }
                else if (x >= s - r && y < r    ) { dx = x - (s-r) + 1; dy = r - y; }
                else if (x < r     && y >= s - r) { dx = r - x;      dy = y - (s-r) + 1; }
                else if (x >= s - r && y >= s - r) { dx = x - (s-r) + 1; dy = y - (s-r) + 1; }
                if (dx || dy)
                {
                    double d = sqrt((double)dx*dx + (double)dy*dy);
                    cov = (int)(255.0 * (r - d) / r);
                    if (cov < 0) cov = 0;
                    if (cov > 255) cov = 255;
                    if (cov == 0) continue;
                }
            }
            int sx = x * COVER_S / s; if (sx >= COVER_S) sx = COVER_S - 1;
            const BYTE* sp = covPx + ((size_t)sy * COVER_S + sx) * 4;
            BYTE* dp = p + ((size_t)py * stride + px) * 4;
            if (mul < 255) cov = cov * mul / 255;      // 过渡时整体淡入
            int inv = 255 - cov;
            dp[0] = (BYTE)((sp[0]*cov + dp[0]*inv) / 255);
            dp[1] = (BYTE)((sp[1]*cov + dp[1]*inv) / 255);
            dp[2] = (BYTE)((sp[2]*cov + dp[2]*inv) / 255);
            dp[3] = (BYTE)((255*cov   + dp[3]*inv) / 255);
        }
    }
    return true;
}

// ===== 隐私提示用的小图标（全自绘，和岛一样逐像素混合）=====
// 全部按"设计尺寸 34×34"来写，实际乘 s/34 —— 这样弹出动画里图标会跟着岛一起长大。
static void FillBoxPx(BYTE* p, int stride, int W, int H, int x0, int y0, int x1, int y1, COLORREF c, int a)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            BlendPx(p, stride, W, H, x, y, c, a);
}

// 圆角矩形的"有符号距离"：<0 在里面、=0 在边上、>0 在外面。
// 有了它，"实心圆角矩形"和"圆角描边"就能共用一套代码。
static double RoundRectSD(double px, double py, double x0, double y0, double x1, double y1, double r)
{
    double dx = std::max(std::max(x0 + r - px, px - (x1 - r)), 0.0);
    double dy = std::max(std::max(y0 + r - py, py - (y1 - r)), 0.0);
    return sqrt(dx*dx + dy*dy) - r;
}
// th < 0 → 实心；th >= 0 → 只画厚度 th 的描边
static void FillRoundBoxPx(BYTE* p, int stride, int W, int H,
                           double x0, double y0, double x1, double y1, double r, double th,
                           COLORREF c, int a)
{
    for (int y = (int)floor(y0) - 1; y <= (int)ceil(y1) + 1; y++)
        for (int x = (int)floor(x0) - 1; x <= (int)ceil(x1) + 1; x++)
        {
            double d = RoundRectSD(x + 0.5, y + 0.5, x0, y0, x1, y1, r);
            if (d > 0.0) continue;
            if (th >= 0.0 && d < -th) continue;
            BlendPx(p, stride, W, H, x, y, c, a);
        }
}
// 圆环（rIn=0 就是实心圆）；halfBottom=1 只画下半圈 —— 话筒那个托架就是它
static void FillArcPx(BYTE* p, int stride, int W, int H,
                      double cx, double cy, double rOut, double rIn, int halfBottom,
                      COLORREF c, int a)
{
    for (int y = (int)(cy - rOut) - 1; y <= (int)(cy + rOut) + 1; y++)
        for (int x = (int)(cx - rOut) - 1; x <= (int)(cx + rOut) + 1; x++)
        {
            double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            double d2 = dx*dx + dy*dy;
            if (d2 > rOut*rOut || d2 < rIn*rIn) continue;
            if (halfBottom && dy < 0) continue;
            BlendPx(p, stride, W, H, x, y, c, a);
        }
}

// 麦克风：竖着的话筒头 + 下半圈托架 + 立柱 + 底座
static void DrawMicIcon(BYTE* p, int stride, int W, int H, int x0, int y0, int s, COLORREF c, int a)
{
    double u = (double)s / 34.0;
    if (u <= 0.0) return;
    auto MX = [&](double v) { return x0 + v * u; };
    auto MY = [&](double v) { return y0 + v * u; };
    FillRoundBoxPx(p, stride, W, H, MX(12.5), MY(3),   MX(21.5), MY(21),  4.5*u, -1.0, c, a);   // 话筒头
    FillArcPx(p, stride, W, H, MX(17), MY(20.5), 10.4*u, 8.1*u, 1, c, a);                       // 托架
    FillBoxPx(p, stride, W, H, (int)MX(15.9), (int)MY(30.0), (int)MX(18.1), (int)MY(31.8), c, a);  // 立柱
    FillBoxPx(p, stride, W, H, (int)MX(11.5), (int)MY(31.6), (int)MX(22.5), (int)MY(33.6), c, a);  // 底座
}

// 摄像头：顶部取景凸起 + 机身描边 + 镜头环
static void DrawCamIcon(BYTE* p, int stride, int W, int H, int x0, int y0, int s, COLORREF c, int a)
{
    double u = (double)s / 34.0;
    if (u <= 0.0) return;
    auto CX = [&](double v) { return x0 + v * u; };
    auto CY = [&](double v) { return y0 + v * u; };
    FillRoundBoxPx(p, stride, W, H, CX(9.5), CY(4),  CX(22.5), CY(12),  2.2*u, -1.0, c, a);     // 取景凸起
    FillRoundBoxPx(p, stride, W, H, CX(2),   CY(10), CX(32),   CY(29),  4.2*u, 2.2*u, c, a);    // 机身描边
    FillArcPx(p, stride, W, H, CX(17), CY(19.5), 6.6*u, 4.3*u, 0, c, a);                        // 镜头环
}

// 弹出（大岛/小岛共用）：从最小尺寸长到目标尺寸，果冻过冲
void PopTick(int tw, int th, int maxStep, int* step)
{
    float t = (float)(*step) / maxStep;
    if (t > 1.0f) t = 1.0f;
    I = t;
    F = (int)(ISLAND_MIN_WIDTH  + (tw - ISLAND_MIN_WIDTH ) * springW(t));
    G = (int)(ISLAND_MIN_HEIGHT + (th - ISLAND_MIN_HEIGHT) * springH(t));
    E = (int)(ISLAND_FINAL_Y * springY(t));
    D = (B - F) / 2;
    (*step)++;
}

// 让常驻小岛出现（大岛正在显示时不动 —— 大岛优先，等它走完自动变身）
void ShowMiniIsland()
{
    bb = true;
    hh = false;          // 常驻岛不显示消息，否则 tBig=!hh 会把它画成空壳
    if (J == 5 || J == 6 || J == 7) return;
    if (J == 1 || J == 3 || J == 4 || J == 9 || J == 10) return;   // 大岛优先：等它走完/收完/变身完再接小岛
    if (J == 11 || J == 12)              // 迷你岛正挂着 → 直接变身成小岛，别先收再弹
    {
        nt = false; ae = F; an = G; ao = E;
        gw = MINI_WIDTH; gh = MINI_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 7;
        oo = 0; vv0 = 1.0f; vt = 1.0f; sf0 = 1.0f; sf1 = 0.0f;
        J = 9;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    O = 0; vv = 1.0f;
    sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;    // 小岛也是普通圆角，别把迷你岛的形状因子漏进来
    J = 6;
    PrepareBackdropBeforeShow();
    R = NowMs();
}

// 收掉常驻小岛（带收回动画）
void HideMiniIsland()
{
    bb = false;
    if (J == 5 || J == 6 || J == 7)
    {
        af = false; ww = 0; xx = false;      // 提示立刻收掉，别在收回动画里残留
        if (ns)                              // 迷你岛开着 → 直接缩成迷你岛，别先收掉再弹出来
        {
            nt = false; ae = F; an = G; ao = E;
            gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
            oo = 0; vv0 = 1.0f; vt = 1.0f; sf0 = sf; sf1 = 0.0f;
            J = 5;
        }
        else { J = 8; P = 0; }
        R = NowMs();
    }
}

// 迷你岛（刘海）的字号。
// 共用公式是 fs = refH*0.62，迷你岛 refH=48 只出 29px 的字 —— 摆在 150×48 的刘海里太秀气。
// 所以这里给它一个"等效高度"：先按高度取饱满，再量一次实际宽度往回缩，
// 保证"22:02"加上节日记号也绝不会顶出刘海（这就是适配）。
int NotchFontRef()
{
    int fs = (int)(NOTCH_HEIGHT * 0.95f);        // 高度上取饱满（原来共用 0.62 太保守）
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t t[32];
    swprintf_s(t, L"%02d:%02d", st.wHour, st.wMinute);
    const wchar_t* mk = HolidayMark();           // 中秋 ● / 国庆 ★ 也算进宽度里
    if (mk[0]) wcscat_s(t, mk);
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, GetCachedFont(fs, true));
    SIZE s = {0,0};
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &s);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    int lim = NOTCH_WIDTH - 24;                  // 左右各留 12 的呼吸空间
    if (s.cx > lim && s.cx > 0) fs = fs * lim / s.cx;
    if (fs < 16) fs = 16;
    int r = (int)(fs / 0.62f + 0.5f);            // 反推等效高度，喂给共用公式
    return r;
}

// 画一帧
void DrawIsland(BYTE* p, int stride, int width, int height)
{
        // width/height = 画布尺寸；岛形只是画布里会动的一块
    for (int y=0; y<height; y++)
        memset(p + (size_t)y*stride*4, 0, (size_t)width*4);

    int ox = D - n;   // 岛在画布内的位置（画布 = 窗口，固定在 n,0）
    int oy = E;
    int w = F, h = G;
    if (w < 2 || h < 2) return;
    int rw=std::min(28,w/4), rh=std::min(28,h/2);
    int rx=rw/2, ry=rh/2;

    // 1. 底板：节日当天换节日色（与时间无关），平时深色=黑灰 / 浅色=浅白
    int br, bg, bb;
    int hol = HolidayToday();
    if (hol == 1)      { if (z) { br = 66; bg = 47; bb = 12; } else { br = 251; bg = 232; bb = 186; } }  // 中秋·浓暖金
    else if (hol == 2) { if (z) { br = 92; bg = 16; bb = 14; } else { br = 255; bg = 226; bb = 220; } }  // 国庆·浓中国红
    else if (z)        { br = 34;  bg = 34;  bb = 38;  }   // 黑灰
    else               { br = 246; bg = 246; bb = 248; }   // 浅白
    for (int y=0; y<height; y++)
    {
        int ly = y - oy;
        BYTE* row = p + (size_t)y*stride*4;
        for (int x=0; x<width; x++)
        {
            float c = ShapeCoverage(x - ox, ly, w, h, rx, ry);
            if (c <= 0.0f) continue;
            int pa = (int)(c * 255.0f + 0.5f);
            int idx = x*4;
            row[idx]  =(BYTE)((bb * pa) / 255);
            row[idx+1]=(BYTE)((bg * pa) / 255);
            row[idx+2]=(BYTE)((br * pa) / 255);
            row[idx+3]=(BYTE)pa;
        }
    }

    // 2. 顶部高光 / 底部压暗（纯粹为了好看）
    if (w > 4 && h > 4)
    {
        int topHi   = z ? 18 : 26;   // 顶部微高光
        int botDark = z ? 26 : 10;   // 底部微压暗
        int thirdH=h/3; if(thirdH<1)thirdH=1;
        for (int y=0; y<height; y++)
        {
            int ly = y - oy;
            if (ly < 0 || ly >= h) continue;
            int aW=0,aB=0;
            if (ly<thirdH) aW=(int)((1.0f-(float)ly/thirdH)*topHi);
            if (ly>(int)(h*0.7f)) aB=(int)(((float)(ly-h*0.7f)/(h*0.3f))*botDark);
            BYTE* row=p+(size_t)y*stride*4;
            for (int x=0; x<width; x++)
            {
                float c = ShapeCoverage(x-ox, ly, w, h, rx, ry);
                if (c <= 0.0f) continue;
                int aBlack=aB; if(aBlack>255)aBlack=255;
                int A=aW+aBlack; if(A>255)A=255;
                A = (int)(A * c);
                if (A<=0) continue;
                int col=(255*aW/A);
                int pc=(col*A)/255;
                int idx=x*4, invA=255-A;
                row[idx]  =(BYTE)(pc + row[idx]  *invA/255);
                row[idx+1]=(BYTE)(pc + row[idx+1]*invA/255);
                row[idx+2]=(BYTE)(pc + row[idx+2]*invA/255);
                row[idx+3]=(BYTE)(A  + row[idx+3]*invA/255);
            }
        }
    }

    // 3. 文字：大岛文字 ↔ 小岛文字 交叉渐变（vv：0=大岛，1=小岛）
    int refH = ISLAND_HEIGHT;
    if (J == 6 || J == 7 || J == 8)            refH = MINI_HEIGHT;
    else if (J == 11 || J == 12 || J == 13)    refH = NotchFontRef();   // 迷你岛：用"等效高度"喂字号
    else if (J == 5 || J == 9)
    {
        // 变身：按"目标那一档"插值字号（目标是迷你岛就插到等效高度，别插到小岛的 56 再跳一下）
        int toH = (sf1 > 0.5f) ? NotchFontRef() : MINI_HEIGHT;
        refH = (int)(ISLAND_HEIGHT + (toH - ISLAND_HEIGHT) * vv);
    }
    float fontScale;
    if (J == 1 || J == 4 || J == 6 || J == 8) fontScale = easeOutCubic(I);
    else                                      fontScale = 1.0f;
    int fsLarge=(int)(12 + ((int)(refH*0.62) - 12) * fontScale);
    int fsSmall=(int)(8  + ((int)(refH*0.20) - 8 ) * fontScale);
    if (fsLarge < 12) fsLarge = 12;
    if (fsSmall < 8)  fsSmall = 8;
    HFONT hFontLarge=GetCachedFont(fsLarge,true);
    HFONT hFontSmall=GetCachedFont(fsSmall,false);
    HFONT hFontHint =GetCachedFont(14,false);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t timeStr[32]; swprintf_s(timeStr,L"%02d:%02d",st.wHour,st.wMinute);
    {   // 节日彩蛋：时间后面缀个小记号（中秋 ● ／ 国庆 ★），文字本身仍然是时间
        const wchar_t* mk = HolidayMark();
        if (mk[0]) wcscat_s(timeStr, mk);
    }
    // 变身中（J=5/J=9）也要用"大岛排版"：两端才能和相邻状态完全对齐，接缝处不跳
    // （大岛字与小岛字的可见区间互不重叠：vv<0.5 只有大岛字，vv>0.5 只有小岛字）
    bool showInfo=(J==1||J==3||J==4||J==5||J==9||J==10);
    COLORREF textColor=z?RGB(255,225,140):RGB(35,35,45);
    COLORREF shadowColor=z?RGB(0,0,0):RGB(255,255,255);
    if (hol == 1)      textColor = z ? RGB(255,206, 88) : RGB(150,82,  6);   // 中秋·浓金（原来太淡）
    else if (hol == 2) textColor = z ? RGB(255,204, 64) : RGB(186,24, 22);   // 国庆·浓金 / 深中国红

    auto stamp=[&](RECT r,const wchar_t* str,HFONT f,COLORREF col,int fmt,int alpha){
        if (alpha <= 0) return;
        SelectObject(Y,f);
        SIZE ts={0,0};
        GetTextExtentPoint32W(Y,str,(int)wcslen(str),&ts);
        int x0=r.left, y0=r.top;
        if (fmt & DT_CENTER)  x0 = r.left + ((r.right-r.left) - ts.cx)/2;
        if (fmt & DT_VCENTER) y0 = r.top  + ((r.bottom-r.top) - ts.cy)/2;
        int x1=x0+ts.cx, y1=y0+ts.cy;
        x0-=4; y0-=4; x1+=4; y1+=4;
        if(x0<0)x0=0; if(y0<0)y0=0;
        if(x1>width)x1=width; if(y1>height)y1=height;
        if(x1<=x0||y1<=y0) return;
        for(int yy=y0;yy<y1;yy++)
            memset(aa+(size_t)yy*stride*4,0,(size_t)width*4);
        DrawText(Y,str,-1,&r,fmt);
        GdiFlush();
        CompositeGlyph(p,(BYTE*)aa,stride,x0,y0,x1,y1,col,alpha);
    };

    // 岛下方那行提示（白字黑描边，浮在桌面上也看得清）—— 常驻小岛与播放器共用同一套
    auto hint=[&](const wchar_t* s){
        if (ww <= 0 || G <= 2) return;
        RECT hr = {ox, oy + h + 6, ox + w, oy + h + 6 + HINT_GAP - 8};
        int off[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (int k = 0; k < 4; k++)
        {
            RECT r2 = hr;
            r2.left += off[k][0]; r2.right += off[k][0];
            r2.top  += off[k][1]; r2.bottom+= off[k][1];
            stamp(r2, s, hFontHint, RGB(0,0,0), DT_CENTER|DT_TOP|DT_SINGLELINE, ww*3/4);
        }
        stamp(hr, s, hFontHint, RGB(255,255,255), DT_CENTER|DT_TOP|DT_SINGLELINE, ww);
    };

    RECT shapeRect = {ox, oy, ox+w, oy+h};

    // ===== 播放器样式 =====
    // 放学《回家》在放（或在设置里试听）时，大岛不显示"下课了+时间"，而是变成播放器：
    // 顶上一行状态小字 + 歌名大字 + 右边一个圆形的播放/暂停键。
    // 用户明确：只要一个最主要的播放/暂停键，不要歌词、不要进度条。
    // 判据里必须带 `bigH == PLAYER_H`：放学那一下大岛是按**普通高度**弹出来的，
    // 那一刻要正常显示「下课了」+几点，不能抢着画播放器（否则封面会撑出岛外）。
    if (ply && bigH == PLAYER_H && (J == 1 || J == 3 || J == 4 || J == 9) && plyA > 0)
    {
        int pa = (int)plyA;                             // 过渡中 0→255，配合 ta 做交叉淡入淡出
        // 整体按"当前高度 / 播放器应有高度"缩放：弹出(J=1)是 0→1、变大(J=9)是 0.65→1、
        // 收回(J=4)再缩回去 —— 这样封面和按钮不会在小岛上撑破。
        float k = (float)G / (float)bigH;
        if (k > 1.12f) k = 1.12f;
        if (k < 0.05f) k = 0.05f;

        int btnD  = (int)(PLAYER_BTN_D * k);  if (btnD  < 4) btnD  = 4;
        int cs    = (int)(COVER_S      * k);  if (cs    < 4) cs    = 4;
        int padL  = (int)(PLAYER_PAD_L * k);
        int padR  = (int)(PLAYER_PAD_R * k);
        int gap   = (int)(18 * k);

        int bx = ox + w - padR - btnD;                  // 按钮左边界（画布坐标）
        int by = oy + (h - btnD) / 2;
        plyBtn.left   = n + bx;                         // 命中用屏幕坐标：屏幕 x = 画布 x + n
        plyBtn.top    = by;                             // （y 不用加，窗口顶边就在屏幕顶边）
        plyBtn.right  = n + bx + btnD;
        plyBtn.bottom = by + btnD;

        // ① 左：专辑封面（没有 Home.jpg 就不画，右边内容自动往左补）
        int cx0 = ox + padL, cy0 = oy + (h - cs) / 2;
        if (CoverBlit(p, stride, width, height, cx0, cy0, cs, pa))
            cx0 += cs + gap;
        else
            cx0 = ox + 26;

        // ② 中：歌名 + 状态小字
        RECT tr = { cx0, oy, bx - gap, oy + h };
        if (tr.right < tr.left + 20) tr.right = tr.left + 20;
        HFONT fT = GetCachedFont(std::max(10, (int)(44 * k)), true);
        HFONT fS = GetCachedFont(std::max(7,  (int)(19 * k)), false);
        const wchar_t* song = L"《回家》";
        const wchar_t* sts  = !bgmPlay ? L"未在播放" : (bgmPause ? L"已暂停" : L"正在播放");
        RECT sr = { tr.left, oy + (int)(h * 0.14f), tr.right, oy + (int)(h * 0.46f) };
        RECT gr = { tr.left, oy + (int)(h * 0.46f), tr.right, oy + (int)(h * 0.92f) };
        RECT srh = sr, grh = gr;
        srh.left += 1; srh.top += 1; grh.left += 2; grh.top += 2;
        stamp(srh, sts,  fS, shadowColor, DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
        stamp(sr,  sts,  fS, textColor,   DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
        stamp(grh, song, fT, shadowColor, DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
        stamp(gr,  song, fT, textColor,   DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);

        // ③ 右：播放 / 暂停圆钮
        int ccx = bx + btnD/2, ccy = oy + h/2;
        FillDisc(p, stride, width, height, ccx, ccy, btnD/2, textColor, 46 * pa / 255);
        int gs = btnD * 42 / 100;
        if (bgmPause) FillPlayTri  (p, stride, width, height, ccx, ccy, gs, textColor, pa);
        else          FillPauseBars(p, stride, width, height, ccx, ccy, gs, textColor, pa);

        // 只有"播放器彻底接管了"才提前收工；淡入途中要接着往下画，
        // 让「下课了」那套普通内容（用 ta 控制透明度）跟播放器交叉淡入淡出。
        if (plyA >= 255.0f)
        {
            hint(L"长按关闭播放器");
            return;
        }
    }

    // ===== 隐私提示：左边一个自绘图标（麦克风/摄像头），右边写字 =====
    // 用户要的版式就是"左图标 + 右文字"；图标和文字一起随岛长大、跟着 ta 一起淡入淡出。
    if (hh && privKind && !ply)
    {
        int pa = (int)(255.0f * std::max(0.0f, 1.0f - vv * 2.0f) * (ta / 255.0f));
        if (pa <= 0) return;
        float k = (float)G / (float)ISLAND_HEIGHT;      // 弹出/收回时按岛高整体缩放
        if (k > 1.15f) k = 1.15f;
        if (k < 0.05f) k = 0.05f;
        int nIcon = (privKind == 3) ? 2 : 1;
        int isz  = (int)(PRIV_ICON_W * k); if (isz < 5) isz = 5;
        int padL = (int)(PRIV_PAD_L * k);
        int gap  = (int)(PRIV_GAP * k);
        int igap = (int)(10 * k);
        int ix = ox + padL;
        int iy = oy + (h - isz) / 2;
        for (int n = 0; n < nIcon; n++)
        {
            // kind==1 只麦克风、==2 只摄像头、==3 两个并排（先麦克风后摄像头）
            int one = (privKind == 3) ? (n == 0 ? 1 : 2) : privKind;
            if (one == 1) DrawMicIcon(p, stride, width, height, ix, iy, isz, textColor, pa);
            else          DrawCamIcon(p, stride, width, height, ix, iy, isz, textColor, pa);
            ix += isz + igap;
        }
        ix += gap - igap;
        HFONT fP = GetCachedFont(std::max(12, (int)(46 * k)), true);
        RECT tr = { ix, oy, ox + w - 18, oy + h };
        if (tr.right < tr.left + 20) tr.right = tr.left + 20;
        RECT sh = tr; sh.top += 2; sh.left += 2;
        stamp(sh, gg, fP, shadowColor, DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
        stamp(tr, gg, fP, textColor,   DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
        return;
    }

    // 两端都是时间时只挪位置换字号，不能淡出淡入（会看着闪）→ 全程不透明、排版随 vv 插值
    bool tBig  = !hh;
    bool tMini = nt ? true : !pp;   // 迷你岛永远显示时间；小岛只在课间显示倒计时

    // 3a) 大岛文字（变身时逐渐淡出）。ta 是"两段式"的总透明度倍率，平时恒 255、不影响任何既有动画
    int aBig = (int)(255.0f * std::max(0.0f, 1.0f - vv*2.0f) * (ta / 255.0f));
    if (tBig && tMini)
    {
        // 时间层：透明度恒定，位置从"大岛排版"(下方 70% 区) 连续挪到"小岛排版"(整块居中)
        RECT tr = { shapeRect.left, oy + (int)((float)h * 0.3f * (1.0f - vv)),
                    shapeRect.right, oy + h };
        RECT ts = tr; ts.left += 2; ts.top += 2; ts.right += 2; ts.bottom += 2;
        stamp(ts, timeStr, hFontLarge, shadowColor, DT_CENTER|DT_VCENTER|DT_SINGLELINE, ta);
        stamp(tr, timeStr, hFontLarge, textColor,   DT_CENTER|DT_VCENTER|DT_SINGLELINE, ta);

        if (showInfo && aBig > 0)     // 顶部那行小标签（上课了/下课了/时间到！）仍按 aBig 淡出
        {
            // 顶行永远显示"事件"本身（上课了/下课了/时间到！）——
            // 节日祝福不在这里插队：那样会把"下课了"顶掉，跟"先显示下课、再出祝福"的两段式打架
            wchar_t infoStr[32];
            AlarmLabel(M, infoStr);
            RECT infoRect={shapeRect.left, oy, shapeRect.right, oy+(int)(h*0.28)};
            RECT infoSh=infoRect; infoSh.left+=1;infoSh.top+=1;infoSh.right+=1;infoSh.bottom+=1;
            stamp(infoSh,infoStr,hFontSmall,shadowColor,DT_CENTER|DT_TOP|DT_SINGLELINE,aBig);
            stamp(infoRect,infoStr,hFontSmall,textColor,DT_CENTER|DT_TOP|DT_SINGLELINE,aBig);
        }
    }
    else if (aBig > 0)
    {
        // 另一端是消息或倒计时 → 两条不同的文字共用同一行，只能照旧交叉渐变
        RECT textRect;
        if(showInfo) textRect={shapeRect.left, oy+(int)(h*0.3), shapeRect.right, oy+h};
        else         textRect=shapeRect;
        RECT shRect=textRect; shRect.left+=2;shRect.top+=2;shRect.right+=2;shRect.bottom+=2;
        const wchar_t* bigStr = hh ? gg : timeStr;
        stamp(shRect,bigStr,hFontLarge,shadowColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aBig);
        stamp(textRect,bigStr,hFontLarge,textColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aBig);

        if(showInfo && !hh){
            // 顶行永远显示"事件"本身（上课了/下课了/时间到！）——
            // 节日祝福不在这里插队：那样会把"下课了"顶掉，跟"先显示下课、再出祝福"的两段式打架
            wchar_t infoStr[32];
            AlarmLabel(M, infoStr);
            RECT infoRect={shapeRect.left, oy, shapeRect.right, oy+(int)(h*0.28)};
            RECT infoSh=infoRect; infoSh.left+=1;infoSh.top+=1;infoSh.right+=1;infoSh.bottom+=1;
            stamp(infoSh,infoStr,hFontSmall,shadowColor,DT_CENTER|DT_TOP|DT_SINGLELINE,aBig);
            stamp(infoRect,infoStr,hFontSmall,textColor,DT_CENTER|DT_TOP|DT_SINGLELINE,aBig);
        }
    }

    // 3b) 小岛文字（倒计时 / 当前时间），变身时逐渐淡入
    //     两端都是时间时上面已经画过了（不透明），这里就别重画，否则会叠出重影
    int aMini = (int)(255.0f * std::max(0.0f, vv*2.0f - 1.0f) * (ta / 255.0f));
    if (!(tBig && tMini) && aMini > 0)
    {
        wchar_t miniStr[48];
        if (pp && !nt)
        {
            LONGLONG left = rr - NowMs();
            int sec = (int)(left / 1000);
            if (sec < 0) sec = 0;
            swprintf_s(miniStr, L"距上课 %02d:%02d", sec/60, sec%60);
        }
        else wcscpy_s(miniStr, timeStr);
        RECT mr=shapeRect, ms=shapeRect;
        ms.left+=2; ms.top+=2; ms.right+=2; ms.bottom+=2;
        stamp(ms,miniStr,hFontLarge,shadowColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aMini);
        stamp(mr,miniStr,hFontLarge,textColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aMini);
    }

    // 3c) 岛下方那行提示（播放器/淡入途中说"关闭播放器"，其余说"关闭常驻岛"）
    hint((ply && bigH == PLAYER_H) ? L"长按关闭播放器" : L"长按关闭常驻岛");
}

// 把画布内容提交给窗口
void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha)
{
    (void)scale; (void)alpha;
    if (F < 2 || G < 2) return;
    if (!EnsureBuffers()) return;

    DrawIsland(X, cc, j, l);

    // 窗口尺寸先跟画布对齐，再交画面：某些环境里"窗口尺寸 ≠ 源画布尺寸"就会带出一条黑边
    static int lj = -1, ll = -1, ln = -1;
    if (j != lj || l != ll || n != ln)
    {
        SetWindowPos(hWnd, HWND_TOPMOST, n, 0, j, l, SWP_NOACTIVATE);
        lj = j; ll = l; ln = n;
    }

    BYTE ga = nt ? (BYTE)nv : (BYTE)255;      // 迷你岛降到半透明；其它岛恒不透明
    BLENDFUNCTION blend={AC_SRC_OVER,0,ga,AC_SRC_ALPHA};
    POINT ptSrc={0,0};
    SIZE  sz={j, l};
    POINT ptDst={n, 0};
    UpdateLayeredWindow(hWnd, NULL, &ptDst, &sz, U, &ptSrc, 0, &blend, ULW_ALPHA);
}

// 11 个课间时段，写死的。要改直接改这仨数组，别问我为什么不读配置
void LoadAlarmTimes()
{
    jj.clear();
    // 旧版（11 节，含早读 / 午自习）
    static const int hA[]  = { 7, 8, 9,10,11,12,14,15,15,16,17};
    static const int mA[]  = {15, 0, 0,15,10,30, 0, 0,55,45,35};
    static const int hAe[] = { 7, 8, 9,11,11,13,14,15,16,17,18};
    static const int mAe[] = {55,45,45, 0,55,15,45,45,40,25,15};
    // 冬季（9 节，第1节 8:00 起，午休不排时段）
    static const int hB[]  = { 8, 9,10,11,13,14,15,16,17};
    static const int mB[]  = { 0, 0,15,10,30,30,45,40,30};
    static const int hBe[] = { 8, 9,11,11,14,15,16,17,18};
    static const int mBe[] = {45,45, 0,55,15,15,30,20,10};
    // 初四冬季（9 节；第7/8/9 节 = 15:25-16:10 / 16:15-16:55 / 17:05-17:45）
    static const int hC[]  = { 8, 9,10,11,13,14,15,16,17};
    static const int mC[]  = { 0, 0,15,10,30,30,25,15, 5};
    static const int hCe[] = { 8, 9,11,11,14,15,16,16,17};
    static const int mCe[] = {45,45, 0,55,15,15,10,55,45};
    const int* startH = tbl == 1 ? hB : (tbl == 2 ? hC : hA);
    const int* startM = tbl == 1 ? mB : (tbl == 2 ? mC : mA);
    const int* endH   = tbl == 1 ? hBe : (tbl == 2 ? hCe : hAe);
    const int* endM   = tbl == 1 ? mBe : (tbl == 2 ? mCe : mAe);
    for (int i=0;i<pc;i++)
    {
        AlarmTime s,e;
        s.hour=startH[i];s.minute=startM[i];s.second=0;s.type=ALARM_TYPE_CLASS_START;s.enabled=true;s.period=i;
        e.hour=endH[i];e.minute=endM[i];e.second=0;e.type=ALARM_TYPE_CLASS_END;e.enabled=true;e.period=i;
        jj.push_back(s);
        jj.push_back(e);
    }
    std::sort(jj.begin(), jj.end(),
        [](const AlarmTime& a, const AlarmTime& b){ return a.hour*60+a.minute < b.hour*60+b.minute; });
}

void CreateIslandWindow(HINSTANCE hInstance)
{
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"IslandAlarmWindow";
    wc.hbrBackground = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    B = GetSystemMetrics(SM_CXSCREEN);
    C = GetSystemMetrics(SM_CYSCREEN);
    g = B * ISLAND_WIDTH_PERCENT / 100;

    // 画布尺寸：留够过冲余量
    ComputeCanvasSize();   // 算画布，留够果冻过冲的地方

    // 注意：这里不再用 WS_EX_TRANSPARENT（那会让窗口完全不接收输入）。
    // 改成在 WM_NCHITTEST 里只让"岛形"范围内命中 → 其它区域照样穿透，效果一致但能收到长按/触控。
    DWORD exCommon = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    // 只有一个分层窗口，全部自绘：Win10 / Win11 同一套逻辑，不做任何能力探测
    o = CreateWindowEx(WS_EX_LAYERED | exCommon, L"IslandAlarmWindow", L"mobile",
                            WS_POPUP, 0, 0, ISLAND_MIN_WIDTH, ISLAND_MIN_HEIGHT,
                            NULL, NULL, hInstance, NULL);

    D = (B - ISLAND_MIN_WIDTH) / 2;
    E = 0;
    F = ISLAND_MIN_WIDTH;
    G = ISLAND_MIN_HEIGHT;
    // 窗口位置固定不动，只改里面那块可见形状
    SetWindowPos(o, HWND_TOPMOST, n, 0, j, l, SWP_NOACTIVATE);
}

// ===== 卸载：删自启动项 + 删程序目录（跑着的 exe 自己删不掉，交给临时 bat）=====
static void RemoveAutostartEntry()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                     0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValue(hKey, L"IslandAlarm");
        RegCloseKey(hKey);
    }
}

void DoUninstall(bool silent)
{
    if (!silent)
    {
        int r = MessageBoxW(NULL,
            L"确定要卸载「mobile 上下课提醒」吗？\n\n"
            L"将删除：\n"
            L"  • 开机自启动项\n"
            L"  • 程序目录（%LOCALAPPDATA%\\IslandAlarm）\n\n"
            L"你的设置（提醒节次 / 音量 / 主题 / 迷你岛等）会保留在注册表\n"
            L"HKCU\\Software\\IslandAlarm 中，如需要可手动删除。\n\n"
            L"卸载后程序立即退出。",
            L"卸载 mobile", MB_YESNO | MB_ICONWARNING);
        if (r != IDYES) return;
    }

    RemoveAutostartEntry();

    // 写个临时 bat，等本进程退出后再删目录
    wchar_t tmpDir[MAX_PATH] = {0}, bat[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmpDir);
    swprintf_s(bat, L"%lsislands_uninstall.bat", tmpDir);
    FILE* f = _wfopen(bat, L"w");
    if (f)
    {
        fputs("@echo off\r\n", f);
        fputs("ping -n 3 127.0.0.1 >nul\r\n", f);                 // 等本进程退出
        fputs("taskkill /f /im islands.exe >nul 2>nul\r\n", f);   // 有其他实例在跑先结束
        fputs(":retry\r\n", f);
        fputs("del /f /q \"%LOCALAPPDATA%\\IslandAlarm\\islands.exe\" >nul 2>nul\r\n", f);
        fputs("if exist \"%LOCALAPPDATA%\\IslandAlarm\\islands.exe\" goto wait\r\n", f);
        fputs("rmdir /q \"%LOCALAPPDATA%\\IslandAlarm\" >nul 2>nul\r\n", f);
        fputs("del /f /q \"%~f0\" >nul 2>nul\r\n", f);
        fputs("exit\r\n", f);
        fputs(":wait\r\n", f);
        fputs("ping -n 2 127.0.0.1 >nul\r\n", f);
        fputs("goto retry\r\n", f);
        fclose(f);
        ShellExecuteW(NULL, L"open", bat, NULL, NULL, SW_HIDE);          // 隐藏窗口执行
    }

    if (!silent)
    {
        MessageBoxW(NULL,
            L"已卸载。\n\n"
            L"• 桌面快捷方式与开机自启动项已删除\n"
            L"• 程序文件将在约 1 秒后自动删除\n"
            L"• 设置保留在注册表 HKCU\\Software\\IslandAlarm",
            L"卸载完成", MB_OK | MB_ICONINFORMATION);
    }
    if (o) DestroyWindow(o);   // 触发 WM_DESTROY → 退出消息循环
}

// ===== 自安装：双击即装（拷到用户目录并启动；不建快捷方式）=====
// false = 已交接给新实例，本进程该退了
static bool EnsureInstalledOrRun()
{
    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t dir[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir))) return true;
    wcscat_s(dir, L"\\IslandAlarm");
    wchar_t exe[MAX_PATH];
    swprintf_s(exe, L"%ls\\islands.exe", dir);

    bool done = (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES)      // 已安装过 → 正常运行
             || (_wcsicmp(self, exe) == 0);                              // 正从安装目录运行

    if (!done)
    {   // 首次运行 → 装一下
        CreateDirectoryW(dir, NULL);
        if (!CopyFileW(self, exe, FALSE)) return true;                   // 复制失败则按便携版运行
    }

    // 音频随行：每次启动都补一次，别只认首次安装。
    // 理由（踩过）：装了以后用户才把 Home.mp3 放到 exe 旁边 —— 老逻辑在"已安装"那行就 return 了，
    // 永远搬不进去，放学音乐就一辈子不响。已存在的文件不覆盖，所以用户自己换过的音频不会被冲掉。
    // 注意要搬 alin.mp3（提醒音）和 Home.mp3（放学《回家》）两类，别只搬一个。
    const wchar_t* aud[8] = { L"alin.mp3", L"Home.mp3", L"Home.wav", L"home.mp3", L"回家.mp3",
                              L"Home.jpg", L"Home.png", L"home.jpg" };
    {
        wchar_t base[MAX_PATH] = {0};
        wcscpy_s(base, self);
        wchar_t* sl = wcsrchr(base, L'\\');
        if (sl)
        {
            *(sl + 1) = 0;      // 截到最后一个反斜杠后：base 就是"原 exe 所在目录"（末尾带分隔符）
            // 注意：注释千万不能用反斜杠结尾 —— 那是"续行符"，会把下一行代码吞进注释（这里栽过）
            // ⚠️ 这个上限必须跟上面 aud[] 的元素个数一致。原来写的是 5，而数组有 8 个，
            // 结果 Home.jpg / Home.png / home.jpg **永远搬不进去** —— 自安装之后播放器就没封面了
            // （测试时是直接往安装目录里拷的封面，所以没暴露）。
            for (int i = 0; i < 8; i++)
            {
                wchar_t from[MAX_PATH] = {0};
                wchar_t to[MAX_PATH] = {0};
                swprintf_s(from, L"%ls%ls", base, aud[i]);
                swprintf_s(to, L"%ls\\%ls", dir, aud[i]);
                if (_wcsicmp(from, to) == 0) continue;                    // 本来就在安装目录里跑
                if (GetFileAttributesW(from) != INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesW(to) == INVALID_FILE_ATTRIBUTES)
                    CopyFileW(from, to, FALSE);
            }
        }
    }

    if (done) return true;

    wchar_t cmd[MAX_PATH + 4];
    swprintf_s(cmd, L"\"%ls\"", exe);
    STARTUPINFOW si = {0}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi);
    if (ok) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }

    // 静默装完，不弹确认框
    return false;
}

void AnimTick()
{
    if (J == 1)
    {
        // 弹出：先胖一圈再弹回来
        float t = (float)O / ANIMATION_STEPS;
        if (t > 1.0f) t = 1.0f;
        I = t;
        F  = (int)(ISLAND_MIN_WIDTH  + (g - ISLAND_MIN_WIDTH) * springW(t));
        G = (int)(ISLAND_MIN_HEIGHT + (bigH - ISLAND_MIN_HEIGHT) * springH(t));
        E      = (int)(ISLAND_FINAL_Y * springY(t));
        D      = (B - F) / 2;
        O++;
        if (O > ANIMATION_STEPS)
        {
            O = 0;
            F  = g;
            G = bigH;                 // 普通闹钟 80 / 播放器 124
            E      = ISLAND_FINAL_Y;
            D      = (B - g) / 2;
            I = 1.0f;
            J = 3;
            S = NowMs() + DISPLAY_DURATION_MS;
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 6)
    {
        // 常驻小岛弹出（与大岛同一套果冻逻辑）
        PopTick(MINI_WIDTH, MINI_HEIGHT, MINI_STEPS, &O);
        if (O > MINI_STEPS)
        {
            O = 0;
            F = MINI_WIDTH; G = MINI_HEIGHT;
            E = ISLAND_FINAL_Y; D = (B - F) / 2; I = 1.0f;
            J = 7;                       // 进入常驻
            vv = 1.0f;
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 5)
    {
        // 缩小变身：从**起始尺寸**（ae/an/ao）过渡到**目标尺寸**（gw/gh/gy），
        // 带果冻（和"展开大岛"同一套弹簧）；步数用 ANIMATION_STEPS 与展开大岛同速率。
        // 目标是"小岛"还是"迷你岛"由此刻的 gw/gh/gy/gJ 决定 —— 大岛→小岛 / 大岛→迷你岛 / 小岛→迷你岛共用这里
        float t = (float)oo / ANIMATION_STEPS;
        if (t > 1.0f) t = 1.0f;
        F = (int)(ae + (gw - ae) * springW(t));
        G = (int)(an + (gh - an) * springH(t));
        E = (int)(ao + (gy - ao) * springY(t));
        D = (B - F) / 2;
        vv = vv0 + (vt - vv0) * t;
        sf = sf0 + (sf1 - sf0) * t;
        oo++;
        if (oo > ANIMATION_STEPS)
        {
            oo = 0;
            F = gw; G = gh; E = gy; D = (B - F) / 2;
            vv = vt; sf = sf1;
            J = gJ;
            if (J == 12) { nt = true; na = NowMs(); nv = 255; ai = -1; }   // 落到迷你岛
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 9)
    {
        // 放大变身：从起始尺寸（ae/an/ao）接到目标尺寸（gw/gh/gy），同样带果冻，文字交叉渐变。
        // 大岛目标时 gJ=3（定格显示大岛）；小岛目标时 gJ=7 —— 小岛→大岛 / 迷你岛→大岛 / 迷你岛→小岛共用这里
        float t = (float)oo / MINI_STEPS;
        if (t > 1.0f) t = 1.0f;
        F = (int)(ae + (gw - ae) * springW(t));
        G = (int)(an + (gh - an) * springH(t));
        E = (int)(ao + (gy - ao) * springY(t));
        D = (B - F) / 2;
        vv = vv0 + (vt - vv0) * t;
        sf = sf0 + (sf1 - sf0) * t;
        if (plyMix)
        {
            // 「下课了」→ 播放器：前 40% 旧字先淡出，后 65% 播放器再淡入（直接对切会显得糊）。
            // 两段重叠一点，看起来才像"换了个版面"而不是"闪了一下"。
            float outT = t / 0.40f;             if (outT > 1.0f) outT = 1.0f;
            float inT  = (t - 0.35f) / 0.65f;   if (inT < 0.0f) inT = 0.0f; if (inT > 1.0f) inT = 1.0f;
            ta   = (int)(255.0f * (1.0f - outT));
            plyA = 255.0f * inT;
        }
        oo++;
        if (oo > MINI_STEPS)
        {
            oo = 0;
            F = gw; G = gh; E = gy; D = (B - F) / 2;
            vv = vt; sf = sf1;
            if (plyMix) { plyMix = false; ta = 255; plyA = 255.0f; }   // 过渡结束，播放器完全接管
            J = gJ;
            if (J == 3) S = NowMs() + DISPLAY_DURATION_MS;   // 变身完 → 照常定格显示大岛
            if (J == 7) { vv = 1.0f; }                       // 落到小岛
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 11)
    {
        // 迷你岛弹出：从屏幕边缘上方"落"下来，宽高一起长，带一点果冻
        float t = (float)O / MINI_STEPS;
        if (t > 1.0f) t = 1.0f;
        F = (int)(ISLAND_MIN_WIDTH  + (NOTCH_WIDTH  - ISLAND_MIN_WIDTH ) * springW(t));
        G = (int)(ISLAND_MIN_HEIGHT + (NOTCH_HEIGHT - ISLAND_MIN_HEIGHT) * springH(t));
        E = (int)((NOTCH_Y + NOTCH_HEIGHT) * springY(t)) - NOTCH_HEIGHT;   // 从屏幕上方落到位（离顶 NOTCH_Y）
        D = (B - F) / 2;
        vv = 1.0f; sf = 0.0f;                                                // 迷你岛也是四角圆角
        O++;
        if (O > MINI_STEPS)
        {
            O = 0;
            F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = NOTCH_Y; D = (B - F) / 2;
            nt = true; nv = 255; na = NowMs(); ai = -1;
            J = 12;
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 13)
    {
        // 迷你岛收回：反向走一遍（缩回去 + 滑出屏幕边缘上方）
        float t = 1.0f - (float)P / MINI_STEPS;
        if (t < 0.0f) t = 0.0f;
        F = (int)(ISLAND_MIN_WIDTH  + (NOTCH_WIDTH  - ISLAND_MIN_WIDTH ) * springW(t));
        G = (int)(ISLAND_MIN_HEIGHT + (NOTCH_HEIGHT - ISLAND_MIN_HEIGHT) * springH(t));
        E = (int)((NOTCH_Y + NOTCH_HEIGHT) * springY(t)) - NOTCH_HEIGHT;   // 反向：缩回屏幕上方
        D = (B - F) / 2;
        sf = 0.0f;
        P++;
        if (P > MINI_STEPS)
        {
            P = 0; nt = false; sf = 0.0f; J = 0;
            {   // 刚收掉迷你岛，若此刻正处课间 → 把倒计时的常驻小岛补上
                LONGLONG t0 = 0;
                if (BreakInfoActive(&t0)) { pp = true; rr = t0; bb = true; O = 0; vv = 1.0f; J = 6; }
            }
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 10)
    {
        // 特殊日期两段式（**大岛形状一动不动，只有文字在变**）：
        //   ① 先维持"下课了 + 时间" ② 文字渐渐淡出 ③ 换文案 ④ 「国庆节快乐」淡入 ⑤ 停一会儿再收回
        hs++;
        if (hs <= 24)        ta = 255 - 255 * hs / 24;                             // 淡出 ≈0.5s
        else if (hs == 25) { hh = true; wcscpy_s(gg, HolidayGreet()); ta = 0; FitBigWidth(NULL, gg, NULL); }    // 换文案
        else if (hs <= 55)   ta = 255 * (hs - 25) / 30;                            // 淡入 ≈0.6s
        else if (hs <= 190)  ta = 255;                                             // 停留 ≈3s
        else if (ply)      { ta = 255; hh = false; J = 3; ShowPlayerIsland(); }    // 祝福演完 → 接手播音乐
        else               { ta = 255; hh = false; P = 0; J = 4; }                 // 收回
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 8)
    {
        // 常驻小岛收回（与大岛收回同一套平滑逻辑）
        float t = (float)P / SLIDEOUT_STEPS;
        if (t > 1.0f) t = 1.0f;
        I = 1.0f - t;
        float k = 1.0f - easeInOutCubic(t);
        E = (int)(ISLAND_FINAL_Y * k);
        F = (int)(MINI_WIDTH  * k);
        G = (int)(MINI_HEIGHT * k);
        D = (B - F) / 2;
        P++;
        if (P > SLIDEOUT_STEPS)
        {
            P = 0;
            J = 0;
            ShowWindow(o, SW_HIDE);
            bb = false; pp = false; vv = 0.0f;
            af = false; ww = 0;
            F = ISLAND_MIN_WIDTH; G = ISLAND_MIN_HEIGHT; E = 0; I = 0.0f;
            return;
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
    else if (J == 4)
    {
        // 收回：平滑收缩上移，不抖
        float t = (float)P / SLIDEOUT_STEPS;
        if (t > 1.0f) t = 1.0f;
        I = 1.0f - t;
        float p = easeInOutCubic(t);            // 起步与收尾速度都为 0 → 全程顺滑，无突兀
        float k = 1.0f - p;
        E      = (int)(ISLAND_FINAL_Y * k);
        F  = (int)(g * k);
        G = (int)(bigH * k);
        D      = (B - F) / 2;
        P++;
        if (P > SLIDEOUT_STEPS)
        {
            P = 0;
            // 播放器岛在收回这段要一直保持"播放器内容"（不然会闪一下普通闹钟的字），到这儿才真正结束
            if (ply) { ply = false; ww = 0; af = false; }
            plyA = 255.0f; plyMix = false; ta = 255;     // 过渡状态一并清干净
            if (bigH != ISLAND_HEIGHT) { bigH = ISLAND_HEIGHT; ComputeCanvasSize(); }
            if (bb)                       // 收回结束时小岛还在等 → 直接接着弹小岛
            {
                hh = false;                           // 别带着消息标志进小岛（会画成空壳）
                O = 0; vv = 1.0f;
                sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;   // 小岛＝普通圆角
                J = 6; R = NowMs();
                return;
            }
            if (ns)                       // 没有小岛等着、但迷你岛开着 → 挂成迷你岛
            {
                nt = true; nv = 255; na = NowMs(); ai = -1; O = 0;
                hh = false;                           // 别带着消息标志进迷你岛
                vv = 1.0f; vv0 = 1.0f; vt = 1.0f; sf = 0.0f;   // 迷你岛＝四角圆角（悬浮样式）
                J = 11; R = NowMs();
                return;
            }
            J = 0;
            ShowWindow(o, SW_HIDE);
            L = false;
            hh = false;
            F  = ISLAND_MIN_WIDTH;
            G = ISLAND_MIN_HEIGHT;
            E = 0;
            I = 0.0f;
            return;
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }
}

// 弹出前：定位 + 显示
void PrepareBackdropBeforeShow()
{
    EnsureBuffers();
    SetWindowPos(o, HWND_TOPMOST, n, 0, j, l, SWP_NOACTIVATE);
    ShowWindow(o, SW_SHOWNOACTIVATE);
    RegisterTouchWindow(o, 0);   // 触屏：让岛能收到触控（没触屏也无害）
}

// 取某个时段的开始/结束时间点（按 period 号 + 类型查表）
bool GetPeriodTime(int period, AlarmType type, int* hh, int* mm)
{
    for (const auto& a : jj)
        if (a.period == period && a.type == type) { *hh = a.hour; *mm = a.minute; return true; }
    return false;
}

// 当前是否处在"课间"，是则给出倒计时目标（下一节课开始时刻，NowMs 口径）
// 课间长度因时段而不同（有的 15 分钟，有的别的），这里直接用"下一节的开始 - 本节的下课"算，不写死
bool BreakInfoActive(LONGLONG* outTargetMs)
{
    SYSTEMTIME st; GetLocalTime(&st);
    int nowSec = st.wHour*3600 + st.wMinute*60 + st.wSecond;
    int minNow = st.wHour*60 + st.wMinute;
    LONGLONG base = NowMs() - (LONGLONG)nowSec*1000 - (LONGLONG)st.wMilliseconds;   // 今天零点
    for (int i = 0; i + 1 < pc; i++)
    {
        int eh, em, nh, nm;
        if (!GetPeriodTime(i, ALARM_TYPE_CLASS_END,   &eh, &em)) continue;
        if (!GetPeriodTime(i + 1, ALARM_TYPE_CLASS_START, &nh, &nm)) continue;
        int endMin = eh*60 + em, nextMin = nh*60 + nm;
        if (nextMin <= endMin) continue;                       // 跨天/异常，忽略
        if (nextMin - endMin > 45) continue;                   // 长课间（午休）不显示倒计时
        // 注意：这里**故意不看 PeriodEnabled** —— 倒计时（显示）与铃声（打钩）是两码事：
        // 用户要求"每个课间都显示倒计时，但不必每个课间都响铃"，所以没打钩的时段也照样显示倒计时
        if (minNow >= endMin && minNow < nextMin)
        {
            *outTargetMs = base + (LONGLONG)nextMin*60*1000;
            return true;
        }
    }
    return false;
}

// 刚下课的这一节，它的课间到几点结束？（下课瞬间调用，用来立刻起倒计时）
bool NextBreakTarget(LONGLONG* outTargetMs)
{
    SYSTEMTIME st; GetLocalTime(&st);
    int nowSec = st.wHour*3600 + st.wMinute*60 + st.wSecond;
    int minNow = st.wHour*60 + st.wMinute;
    LONGLONG base = NowMs() - (LONGLONG)nowSec*1000 - (LONGLONG)st.wMilliseconds;
    for (int i = 0; i + 1 < pc; i++)
    {
        int eh, em, nh, nm;
        if (!GetPeriodTime(i, ALARM_TYPE_CLASS_END,   &eh, &em)) continue;
        if (!GetPeriodTime(i + 1, ALARM_TYPE_CLASS_START, &nh, &nm)) continue;
        if (nh*60 + nm - (eh*60 + em) > 45) continue;          // 长课间（午休）不算
        if (eh*60 + em == minNow && nh*60 + nm > minNow)
        {
            *outTargetMs = base + (LONGLONG)(nh*60 + nm)*60*1000;
            return true;
        }
    }
    return false;
}

// 常驻小岛心跳（约 33ms）：刷倒计时 / 课间结束就收 / 提示淡入淡出 / 到秒才重绘
void MiniHoldTick()
{
    hh = false;          // 兜底：常驻岛期间一律按"不是消息"画
    LONGLONG tgt = 0;
    bool brk = BreakInfoActive(&tgt);
    if (brk)
    {
        if (!pp || rr != tgt) { pp = true; rr = tgt; ai = -1; }
    }
    else                               // 课间结束了
    {
        // 课间结束：留一小段窗口等"上课"闹钟，好让它直接变身，而不是先缩回再弹一次

        if (ja == 0) ja = NowMs();
        if (NowMs() - ja > 1600) { ja = 0; HideMiniIsland(); return; }
    }
    if (brk) ja = 0;                   // 还在课间 / 课间又开始了 → 清掉衔接窗口

    int nowSec = (int)(NowMs() / 1000);
    bool need = false;
    if (nowSec != ai) { ai = nowSec; need = true; }

    // 提示文字淡入淡出
    int target = af ? 255 : 0;
    if (ww != target)
    {
        int step = 42;
        if (ww < target) { ww += step; if (ww > target) ww = target; }
        else             { ww -= step; if (ww < target) ww = target; }
        need = true;
    }
    if (need) UpdateIslandDisplay(o, 1.0f, 255);
}

// 全局轮询（钩子不可用时的兜底）：鼠标/被提升为鼠标的触控都能查到
void PollGesture()
{
    static bool prevDown = false;
    POINT pt; GetCursorPos(&pt);
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (down && !prevDown)
    {
        if (IslandHitTest(pt))
        {
            if (!xx) { xx = true; yy = NowMs(); af = true; }
        }
        else
        {
            int cxm = B / 2;
            if (J == 0 && pt.y <= 40 && pt.x > cxm - g / 2 && pt.x < cxm + g / 2)
            { zz = true; ag = NowMs(); ah = pt.y; }
        }
    }
    else if (!down && prevDown)
    {
        if (xx) { xx = false; af = false; }
        if (zz) zz = false;
    }
    prevDown = down;
}

// 交互心跳（约 100ms）：长按判满 → 关掉常驻小岛；下滑后停留够 3 秒 → 展开常驻小岛
void GestureTick()
{
    PollGesture();
    LONGLONG now = NowMs();

    if (xx && (J == 6 || J == 7) && now - yy >= LONGPRESS_MS)
    {
        xx = false; af = false;                     // 长按关闭
        HideMiniIsland();
    }
    // 播放器岛：长按 → 停掉《回家》并收回（用户只要求有播放/暂停键，所以长按是它的退出方式，
    // 和"长按关闭常驻岛"是同一套手势，不额外加按钮）
    if (xx && ply && (J == 1 || J == 3) && now - yy >= LONGPRESS_MS)
    {
        xx = false; af = false;
        ClosePlayer();
    }
    if (zz && now - ag >= SWIPE_HOLD_MS)
    {
        POINT pt; GetCursorPos(&pt);
        if (J == 0 && pt.y - ah >= SWIPE_DY)
        {
            zz = false;                            // 下滑 → 展开迷你岛（实时时间归它）
            ns = true; SaveNotchCfg();
            ShowNotch();
        }
        else if (now - ag > SWIPE_HOLD_MS + 5000) zz = false;   // 兜底，别一直挂着
    }
    if (!xx && !zz) KillTimer(o, 12);                  // 没活儿了就停心跳
}


void StartTimerThread()
{
    kk = CreateEvent(NULL, FALSE, FALSE, NULL);
    ll = CreateWaitableTimer(NULL, TRUE, NULL);
    mm = CreateThread(NULL, 0, TimerThreadProc, NULL, 0, NULL);
}

BOOL GetNextAlarmTime(FILETIME* pNextTime, AlarmType* pType)
{
    if (jj.empty()) return FALSE;
    SYSTEMTIME stNowUtc; GetSystemTime(&stNowUtc);
    FILETIME ftNowUtc; SystemTimeToFileTime(&stNowUtc, &ftNowUtc);
    ULONGLONG minDiff = ULLONG_MAX;
    bool found = false;
    AlarmType nextType = ALARM_TYPE_SINGLE;
    FILETIME nextFT;
    for (const auto& alarm : jj)
    {
        if (!alarm.enabled) continue;
        SYSTEMTIME stLocal; GetLocalTime(&stLocal);
        SYSTEMTIME target = stLocal;
        target.wHour=alarm.hour;target.wMinute=alarm.minute;target.wSecond=alarm.second;target.wMilliseconds=0;
        SYSTEMTIME targetUtc;
        TzSpecificLocalTimeToSystemTime(NULL, &target, &targetUtc);
        FILETIME ftTarget; SystemTimeToFileTime(&targetUtc, &ftTarget);
        ULARGE_INTEGER uliCurrent, uliTarget;
        uliCurrent.LowPart=ftNowUtc.dwLowDateTime;uliCurrent.HighPart=ftNowUtc.dwHighDateTime;
        uliTarget.LowPart=ftTarget.dwLowDateTime;uliTarget.HighPart=ftTarget.dwHighDateTime;
        if (uliTarget.QuadPart <= uliCurrent.QuadPart)
        {
            ULONGLONG oneDay = 10000000ULL*60*60*24;
            uliTarget.QuadPart += oneDay;
            ftTarget.dwLowDateTime=(DWORD)uliTarget.LowPart;ftTarget.dwHighDateTime=(DWORD)uliTarget.HighPart;
        }
        ULONGLONG diff = uliTarget.QuadPart - uliCurrent.QuadPart;
        if (diff < minDiff) { minDiff=diff; nextFT=ftTarget; nextType=alarm.type; found=true; }
    }
    if (found) { *pNextTime=nextFT; *pType=nextType; return TRUE; }
    return FALSE;
}

DWORD WINAPI TimerThreadProc(LPVOID lpParam)
{
    while (true)
    {
        FILETIME ftNext; AlarmType nextType;
        if (!GetNextAlarmTime(&ftNext, &nextType))
        {
            if (WaitForSingleObject(kk, 60000) == WAIT_OBJECT_0) return 0;
            continue;
        }
        FILETIME ftNow; GetSystemTimeAsFileTime(&ftNow);
        ULARGE_INTEGER uliNow, uliNext;
        uliNow.LowPart=ftNow.dwLowDateTime;uliNow.HighPart=ftNow.dwHighDateTime;
        uliNext.LowPart=ftNext.dwLowDateTime;uliNext.HighPart=ftNext.dwHighDateTime;
        LONGLONG diffMs = (uliNext.QuadPart - uliNow.QuadPart) / 10000;
        if (diffMs < 0) diffMs = 0;
        while (diffMs > 0)
        {
            DWORD waitMs = (diffMs > 30000) ? 30000 : (DWORD)diffMs;
            if (WaitForSingleObject(kk, waitMs) == WAIT_OBJECT_0) return 0;
            GetSystemTimeAsFileTime(&ftNow);
            uliNow.LowPart=ftNow.dwLowDateTime;uliNow.HighPart=ftNow.dwHighDateTime;
            diffMs = (uliNext.QuadPart - uliNow.QuadPart) / 10000;
            if (diffMs < 0) diffMs = 0;
        }
        PostMessage(o, WM_ALARM_TRIGGER, (WPARAM)nextType, 0);
        if (WaitForSingleObject(kk, 2000) == WAIT_OBJECT_0) return 0;
    }
    return 0;
}

// 找同目录的 alin.mp3（安装时会一并拷到用户目录）
void AudioResolvePath()
{
    nn[0] = 0;
    wchar_t exe[MAX_PATH] = {0};
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash) return;
    slash[1] = 0;
    swprintf_s(nn, L"%lsalin.mp3", exe);
    if (GetFileAttributesW(nn) == INVALID_FILE_ATTRIBUTES) nn[0] = 0;
}

// MCI 播 mp3：系统自带解码器，支持音量 0~1000
// 播 mp3。找不到文件就闭嘴，绝不弹框打扰（上课呢）
// 程序自己的图标（资源里那个 1 ICON）：托盘 / 任务栏 / 标题栏 都用它
HICON AppIcon(bool small)
{
    static HICON big = NULL, sml = NULL;
    HINSTANCE hi = GetModuleHandleW(NULL);
    if (small)
    {
        if (!sml) sml = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        if (!sml) sml = LoadIcon(NULL, IDI_APPLICATION);
        return sml;
    }
    if (!big) big = LoadIconW(hi, MAKEINTRESOURCEW(1));
    if (!big) big = LoadIcon(NULL, IDI_APPLICATION);
    return big;
}

// ===== 放学 BGM《回家》：同目录找 Home.mp3 / Home.wav（没有就安静地不播）=====
// 状态量都声明在文件上方（DrawIsland 要用），这里只有路径和几个动作函数
wchar_t bgmPath[MAX_PATH] = {0};
// 《回家》的播放时刻：固定 19:30（2026-10-13 用户要求 —— 从"放学那一下"挪出来，放学恢复打铃）
const int BGM_HOUR = 19;
const int BGM_MIN  = 30;
int  nb = -1;                      // 上次处理过的分钟；每分钟跟着走，用于"到点只放一次"

void HomeResolvePath()
{
    bgmPath[0] = 0;
    wchar_t exe[MAX_PATH] = {0};
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (!slash) return;
    slash[1] = 0;
    const wchar_t* cand[] = { L"Home.mp3", L"Home.wav", L"home.mp3", L"回家.mp3" };
    for (int i = 0; i < 4; i++)
    {
        swprintf_s(bgmPath, L"%ls%ls", exe, cand[i]);
        if (GetFileAttributesW(bgmPath) != INVALID_FILE_ATTRIBUTES) return;
    }
    bgmPath[0] = 0;
}
void BgmStop()
{
    if (!bgmPlay) return;
    mciSendStringW(L"close bgm", NULL, 0, NULL);      // 会顺带发一条 MCI_NOTIFY_ABORTED，不是"放完了"
    bgmPlay = false;
    bgmPause = false;
}
void BgmPause()
{
    if (!bgmPlay || bgmPause) return;
    if (mciSendStringW(L"pause bgm", NULL, 0, NULL) == 0) { bgmPause = true; plyPauseAt = NowMs(); }
}
void BgmResume()
{
    if (!bgmPlay || !bgmPause) return;
    if (mciSendStringW(L"resume bgm", NULL, 0, NULL) == 0) { bgmPause = false; plyPauseAt = 0; }
}
void BgmToggle() { if (bgmPause) BgmResume(); else BgmPause(); }

void BgmPlay()
{
    if (!bgmOn) return;
    if (!bgmPath[0]) HomeResolvePath();
    if (!bgmPath[0]) return;
    BgmStop();
    wchar_t cmd[MAX_PATH + 64];
    swprintf_s(cmd, L"open \"%ls\" type mpegvideo alias bgm", bgmPath);
    if (mciSendStringW(cmd, NULL, 0, NULL) != 0) return;
    int v = bgmV * 10; if (v < 0) v = 0; if (v > 1000) v = 1000;
    swprintf_s(cmd, L"setaudio bgm volume to %d", v);
    mciSendStringW(cmd, NULL, 0, NULL);
    bgmPlay = true;
    bgmPause = false;
    // 带 notify：曲子自然放完时系统会给主窗口发 MM_MCINOTIFY，我们据此自动收掉播放器岛。
    // 不这么做就只能轮询 status，而 MCI 的状态查询在某些驱动上会同步阻塞 UI 线程。
    mciSendStringW(L"play bgm from 0 notify", NULL, 0, o);
}

void AudioPlay(AlarmType type)
{
    if (!fuck) return;
    if (!nn[0]) AudioResolvePath();
    if (!nn[0]) return;
    mciSendStringW(L"close ilsaudio", NULL, 0, NULL);
    wchar_t cmd[MAX_PATH + 64];
    swprintf_s(cmd, L"open \"%ls\" type mpegvideo alias ilsaudio", nn);
    if (mciSendStringW(cmd, NULL, 0, NULL) != 0) return;
    int vol = (type == ALARM_TYPE_CLASS_END) ? FUCK : ab;    // 下课铃 / 上课铃 各自音量
    int v = vol * 10;
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    swprintf_s(cmd, L"setaudio ilsaudio volume to %d", v);
    mciSendStringW(cmd, NULL, 0, NULL);
    mciSendStringW(L"play ilsaudio from 0", NULL, 0, NULL);
}
void AudioStop() { mciSendStringW(L"close ilsaudio", NULL, 0, NULL); }

// 播放别占着 UI 线程：MCI 的 open/play 会同步等解码器就绪（实测卡 ~200ms），
// 这 200ms 正好把"岛弹出 / 变身"的第一帧挡住，看着就是"动画卡了一下才动"。
// 丢到后台线程放 → 画面和声音同时开始。
static DWORD WINAPI AudioThreadProc(LPVOID p)
{
    AudioPlay((AlarmType)(INT_PTR)p);
    return 0;
}
void AudioPlayAsync(AlarmType type)
{
    HANDLE h = CreateThread(NULL, 0, AudioThreadProc, (LPVOID)(INT_PTR)type, 0, NULL);
    if (h) CloseHandle(h);      // 不 Join：让它自己放完
}

void LoadAudioCfg()
{
    HKEY hKey; DWORD v = 0, sz = sizeof(v);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hKey, L"AudioOn", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
            fuck = (v != 0);
        v = 30; sz = sizeof(v);
        if (RegQueryValueEx(hKey, L"AudioVol", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
        { ab = FUCK = (int)v; }                     // 老版本的单音量 → 迁移成两路
        v = (DWORD)ab; sz = sizeof(v);
        if (RegQueryValueEx(hKey, L"AudioVolStart", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) ab = (int)v;
        v = (DWORD)FUCK; sz = sizeof(v);
        if (RegQueryValueEx(hKey, L"AudioVolEnd", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) FUCK = (int)v;
        v = 1; sz = sizeof(v);
        if (RegQueryValueEx(hKey, L"HomeOn", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) bgmOn = (v != 0);
        v = 15; sz = sizeof(v);
        if (RegQueryValueEx(hKey, L"HomeVol", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) bgmV = (int)v;
        if (bgmV < 0) bgmV = 0; if (bgmV > 100) bgmV = 100;
        if (ab < 0) ab = 0; if (ab > 100) ab = 100;
        if (FUCK < 0) FUCK = 0; if (FUCK > 100) FUCK = 100;
        RegCloseKey(hKey);
    }
    AudioResolvePath();
}
void SaveAudioCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = fuck ? 1 : 0;
        RegSetValueEx(hKey, L"AudioOn", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        v = (DWORD)ab;
        RegSetValueEx(hKey, L"AudioVolStart", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        v = (DWORD)FUCK;
        RegSetValueEx(hKey, L"AudioVolEnd", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        v = bgmOn ? 1 : 0;
        RegSetValueEx(hKey, L"HomeOn", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        v = (DWORD)bgmV;
        RegSetValueEx(hKey, L"HomeVol", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

void LoadTheme()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwData = 0, dwSize = sizeof(dwData);
        if (RegQueryValueEx(hKey, L"Theme", NULL, NULL, (LPBYTE)&dwData, &dwSize) == ERROR_SUCCESS)
            z = (dwData != 0);
        RegCloseKey(hKey);
    }
}

void SaveTheme()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD dwData = z ? 1 : 0;
        RegSetValueEx(hKey, L"Theme", 0, REG_DWORD, (LPBYTE)&dwData, sizeof(dwData));
        RegCloseKey(hKey);
    }
}

// 第 i 个时段是否勾选（按 period 字段找；老代码用 i*2 取下标，排序后就对不上了 —— 那是个真 bug）
// 妈了个巴子的，早年这里按下标 i*2 找勾选状态，排序之后全错位了 —— 用户还以为自己没勾
bool PeriodEnabled(int i)
{
    for (const auto& a : jj) if (a.period == i) return a.enabled;
    return false;
}
void SetPeriodEnabled(int i, bool en)
{
    for (auto& a : jj) if (a.period == i) a.enabled = en;
}

void LoadPeriodEnabled()
{
    HKEY hKey;
    bool found = false;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwData = 0, dwSize = sizeof(dwData);
        if (RegQueryValueEx(hKey, L"PeriodEnabled", NULL, NULL, (LPBYTE)&dwData, &dwSize) == ERROR_SUCCESS)
        {
            found = true;
            for (int i = 0; i < pc; i++)
                SetPeriodEnabled(i, (dwData & (1 << i)) != 0);
        }
        RegCloseKey(hKey);
    }
    if (!found)
    {
        DWORD defaultMask = 0;
        for (int i = pc - 4; i < pc; i++) if (i >= 0) defaultMask |= (1 << i);   // 最后 4 节
        for (int i = 0; i < pc; i++)
            SetPeriodEnabled(i, (defaultMask & (1 << i)) != 0);
        SavePeriodEnabled();
    }
}

// 大岛上的"事件标签"文案（画岛和量宽度都用它，避免两处写不一样）
void AlarmLabel(AlarmType t, wchar_t* out)
{
    if (t == ALARM_TYPE_CLASS_START)      wcscpy_s(out, 32, L"上课了");
    else if (t == ALARM_TYPE_CLASS_END)   wcscpy_s(out, 32, L"下课了");
    else                                  wcscpy_s(out, 32, L"时间到！");
}

// 让大岛宽度贴合文字：量出标签与主文字里更宽的那个，再加上留白
// （原来固定屏宽 50%，写"下课了"时两侧空一大片；现在字占多少岛就多宽，有上下限兜底）
void FitBigWidth(const wchar_t* label, const wchar_t* big, const wchar_t* extra, int extraPx)
{
    int fsLarge = (int)(12 + ((int)(ISLAND_HEIGHT*0.62) - 12));
    int fsSmall = (int)(8  + ((int)(ISLAND_HEIGHT*0.20) - 8 ));
    if (fsLarge < 12) fsLarge = 12;
    if (fsSmall < 8)  fsSmall = 8;
    HDC dc = GetDC(NULL);
    SIZE s1 = {0,0}, s2 = {0,0}, s3 = {0,0};
    HGDIOBJ old = SelectObject(dc, GetCachedFont(fsLarge, true));
    if (big && big[0])   GetTextExtentPoint32W(dc, big,   (int)wcslen(big),   &s1);
    if (extra && extra[0]) GetTextExtentPoint32W(dc, extra, (int)wcslen(extra), &s3);
    SelectObject(dc, GetCachedFont(fsSmall, false));
    if (label && label[0]) GetTextExtentPoint32W(dc, label, (int)wcslen(label), &s2);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    int need = s1.cx;
    if (s3.cx > need) need = s3.cx;
    if (s2.cx > need) need = s2.cx;
    need += 120 + extraPx;                           // 左右各留 60 的呼吸空间（extraPx 给播放键等托底）
    int lo = B * 22 / 100, hi = B * 92 / 100;        // 最短/最长（别太窄也别顶到屏幕边）
    if (need < lo) need = lo;
    if (need > hi) need = hi;
    g = need;
    ComputeCanvasSize();
}

// 播放器下面那行"长按关闭播放器"只显示一小会儿，之后自己淡掉（一直挂着太吵）
LONGLONG plyHintUntil = 0;

// 让大岛以"音乐播放器"样式出现并常驻。
// 三种来路都要照顾到：
//   ① 大岛正定格（J=3）→ 形状不动，内容换成播放器，并把定格时间顶到很久以后
//   ② 小岛/迷你岛在显示（J=6/7/11/12）→ 直接放大变身成大岛
//   ③ 什么都没显示（J=0）→ 照常从最小尺寸弹出
//   ④ 节日两段式正在演（J=10）→ 先别抢，等它演完（见 AnimTick 里 J=10 的收尾）
void ShowPlayerIsland()
{
    bool grow = (bigH != PLAYER_H || g != PLAYER_W);            // 现在的尺寸是不是"播放器尺寸"
    ply = true;
    plyA = 255.0f; plyMix = false;                              // 默认完全不透明；要走过渡再单独置
    ww = 255; plyHintUntil = NowMs() + 8000;                    // 露一下"长按关闭播放器"
    if (J == 10) return;                                        // 节日两段式优先级更高（它收尾时会再叫一次）
    CoverLoad();                                                // 每次进场重读一次封面（用户可能刚放进来）
    bigH = PLAYER_H;                                            // 播放器比普通大岛高
    g = PLAYER_W;                                               // 也比普通大岛长
    ComputeCanvasSize();

    if (J == 1 || J == 9) { S = NowMs() + PLAYER_LONG; return; }// 正在弹出/变大，内容自己会变成播放器
    if (J == 3 && !grow)                                        // 已经是大岛且尺寸已对 → 原地换内容
    {
        S = NowMs() + PLAYER_LONG;
        UpdateIslandDisplay(o, 1.0f, 255);
        return;
    }
    if (J == 0)                                                 // 什么都没显示 → 从最小尺寸弹出
    {
        F = ISLAND_MIN_WIDTH; G = ISLAND_MIN_HEIGHT; E = 0; D = (B - F) / 2;
        vv0 = 0.0f; vt = 0.0f; vv = 0.0f;
        sf0 = 0.0f; sf1 = 0.0f; sf = 0.0f;
        O = 0; I = 0.0f; J = 1;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    // 其余情况（大岛定格里但尺寸还是普通大岛 / 小岛 / 迷你岛）→ 走放大变身，长成播放器那个长方形。
    // 同时开"接场过渡"：前 40% 把「下课了」淡出去，后 65% 播放器再淡进来（见 AnimTick 的 J=9）。
    ae = F; an = G; ao = E;                                     // 从当前尺寸接着长，接缝处不跳
    sf0 = sf; sf1 = 0.0f; nt = false;
    gw = g; gh = bigH; gy = ISLAND_FINAL_Y; gJ = 3;
    vv0 = 0.0f; vt = 0.0f; oo = 0; J = 9;
    plyMix = true; plyA = 0.0f; ta = 255;
    R = NowMs();
}

// 收掉播放器：停音乐 + 收回大岛
void ClosePlayer()
{
    BgmStop();
    ply = false;
    bgmPause = false;
    if (J == 1 || J == 3) { P = 0; J = 4; R = NowMs(); }
    else UpdateIslandDisplay(o, 1.0f, 255);
}

void PlayerTick()
{
    LONGLONG now = NowMs();
    // 按了暂停后 5 秒还没继续播 → 自动把播放器收掉（用户要求）
    if (ply && bgmPause && plyPauseAt && now - plyPauseAt >= PLAYER_PAUSE_CLOSE_MS)
    {
        ClosePlayer();
        return;
    }
    if (now < plyHintUntil)
    {
        if (ww < 255) { ww = 255; UpdateIslandDisplay(o, 1.0f, 255); }
        return;
    }
    if (ww > 0)
    {
        ww -= 26; if (ww < 0) ww = 0;
        UpdateIslandDisplay(o, 1.0f, 255);
    }
}

// 点在播放/暂停键上？是就切一下，并返回 true（顺便别让这一下还去当"长按"）
bool PlayerButtonHit(POINT ptScreen)
{
    // 必须"确实画着播放器"才算数：放学那一下大岛是按普通高度弹的（bigH 还没换成播放器高度），
    // 那时按钮根本没画，plyBtn 里是上一轮的残留坐标，认了就会莫名其妙把音乐暂停。
    if (!ply || bigH != PLAYER_H || plyA < 128.0f) return false;
    if (plyBtn.right <= plyBtn.left) return false;
    if (ptScreen.x < plyBtn.left || ptScreen.x >= plyBtn.right ||
        ptScreen.y < plyBtn.top  || ptScreen.y >= plyBtn.bottom) return false;
    BgmToggle();
    plyHintUntil = NowMs() + 4000;      // 点一下把提示再叫出来几秒
    ww = 255;
    UpdateIslandDisplay(o, 1.0f, 255);
    return true;
}

void LoadTimeTable()
{
    HKEY hKey; DWORD v = 1, sz = sizeof(v);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hKey, L"TimeTable", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) tbl = (int)v;
        RegCloseKey(hKey);
    }
    if (tbl < 0 || tbl > 2) tbl = 1;                       // 认不出就用"冬季"
    pc = (tbl == 0) ? 11 : 9;
    ff = (tbl == 0) ? ffA : (tbl == 1 ? ffB : ffC);
}
void SaveTimeTable()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = (DWORD)tbl;
        RegSetValueEx(hKey, L"TimeTable", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

void SavePeriodEnabled()
{
    DWORD dwData = 0;
    for (int i = 0; i < pc; i++)
        if (PeriodEnabled(i)) dwData |= (1 << i);
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueEx(hKey, L"PeriodEnabled", 0, REG_DWORD, (LPBYTE)&dwData, sizeof(dwData));
        RegCloseKey(hKey);
    }
}

void CreateTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = AppIcon();
    wcscpy_s(nid.szTip, L"mobile 上下课提醒");
    Shell_NotifyIcon(NIM_ADD, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
}

void RemoveTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hWnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void ShowNextAlarmTime()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    int curMin = st.wHour * 60 + st.wMinute;
    int nextMin = -1;
    for (const auto& alarm : jj)
    {
        if (!alarm.enabled) continue;              // 没打钩的不算
        int alm = alarm.hour * 60 + alarm.minute;
        if (alm > curMin) { nextMin = alm; break; }
    }
    if (nextMin < 0)
    {
        for (const auto& alarm : jj)
            if (alarm.enabled) { nextMin = alarm.hour * 60 + alarm.minute; break; }
    }
    if (nextMin >= 0)
    {
        wchar_t msg[64];
        swprintf_s(msg, L"下次提醒时间为 %02d:%02d", nextMin / 60, nextMin % 60);
        ShowMessageIsland(msg);
    }
    else
    {
        ShowMessageIsland(L"暂无提醒时间");
    }
}

// ===== 设置窗口：真·系统窗口（标准控件，会出现在任务栏里）=====
HWND stw = NULL;                  // 设置窗口
HWND sPeriod[11] = {0};           // 时段勾选框
HWND sTbl[3] = {0};               // 作息表：0=初四夏季 1=冬季 2=初四冬季
HWND sVol[3] = {0};               // 音量滑块：0=上课铃 1=下课铃 2=《回家》
HWND sVolPct[3] = {0};            // 音量百分比
HWND sBgmOn = NULL;               // 放学播放《回家》
HWND sTheme[2] = {0};             // 主题：0=浅色 1=深色
HWND sAudioOn = NULL, sHoliday = NULL, sMini = NULL;
HFONT sFont = NULL;
#define ID_SET_CLOSE 1090
#define ID_SET_PREV0 1091
#define ID_SET_PREV1 1092
#define ID_SET_BGM_ON 1093
#define ID_SET_PREV2 1094

static HFONT SetUiFont()
{
    if (sFont) return sFont;
    NONCLIENTMETRICSW ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        sFont = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!sFont) sFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    return sFont;
}

static HWND SetCtl(const wchar_t* cls, const wchar_t* txt, DWORD style,
                   int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, stw, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (c && cls != L"msctls_trackbar32") SendMessageW(c, WM_SETFONT, (WPARAM)SetUiFont(), TRUE);
    return c;
}

// 把控件状态从程序里同步过来（打开时、以及每次改动之后都调一次）
void SetSyncControls()
{
    if (!stw) return;
    for (int i = 0; i < 3; i++)
        if (sTbl[i]) SendMessageW(sTbl[i], BM_SETCHECK, (tbl == i) ? BST_CHECKED : BST_UNCHECKED, 0);
    for (int i = 0; i < 11; i++)
    {
        if (!sPeriod[i]) continue;
        if (i < pc)
        {
            ShowWindow(sPeriod[i], SW_SHOW);
            SetWindowTextW(sPeriod[i], ff[i]);
            SendMessageW(sPeriod[i], BM_SETCHECK, PeriodEnabled(i) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else ShowWindow(sPeriod[i], SW_HIDE);       // 换表后多出来的勾选框藏起来
    }
    if (sAudioOn)  SendMessageW(sAudioOn,  BM_SETCHECK, fuck ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sHoliday)  SendMessageW(sHoliday,  BM_SETCHECK, hon  ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sMini)     SendMessageW(sMini,     BM_SETCHECK, ns   ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sTheme[0]) SendMessageW(sTheme[0], BM_SETCHECK, !z   ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sTheme[1]) SendMessageW(sTheme[1], BM_SETCHECK,  z   ? BST_CHECKED : BST_UNCHECKED, 0);
    wchar_t b[16];
    if (sVol[0])    SendMessageW(sVol[0], TBM_SETPOS, TRUE, ab);
    if (sVol[1])    SendMessageW(sVol[1], TBM_SETPOS, TRUE, FUCK);
    if (sVol[2])    SendMessageW(sVol[2], TBM_SETPOS, TRUE, bgmV);
    if (sVolPct[0]) { swprintf_s(b, L"%d%%", ab);   SetWindowTextW(sVolPct[0], b); }
    if (sVolPct[1]) { swprintf_s(b, L"%d%%", FUCK); SetWindowTextW(sVolPct[1], b); }
    if (sVolPct[2]) { swprintf_s(b, L"%d%%", bgmV); SetWindowTextW(sVolPct[2], b); }
    if (sBgmOn)     SendMessageW(sBgmOn, BM_SETCHECK, bgmOn ? BST_CHECKED : BST_UNCHECKED, 0);
}

LRESULT CALLBACK SetProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == ID_SET_CLOSE) { DestroyWindow(hWnd); return 0; }
        if (id == ID_SET_PREV0) { AudioPlayAsync(ALARM_TYPE_CLASS_START); return 0; }
        if (id == ID_SET_PREV1) { AudioPlayAsync(ALARM_TYPE_CLASS_END);   return 0; }
        if (id == ID_SET_PREV2) { BgmPlay(); if (bgmPlay) ShowPlayerIsland(); return 0; }   // 试听顺便预览播放器样式
        if (id == ID_SET_BGM_ON)
        {
            bgmOn = !bgmOn;
            if (!bgmOn) BgmStop();
            SaveAudioCfg(); SetSyncControls();
            return 0;
        }
        if (id >= ID_PERIOD_BASE && id < ID_PERIOD_BASE + 11)
        {
            SendMessageW(o, WM_COMMAND, (WPARAM)id, 0);     // 复用主窗口那套（勾选 + 存盘）
            SetSyncControls();
            return 0;
        }
        switch (id)
        {
        case ID_TBL_OLD: case ID_TBL_WINTER: case ID_TBL_W4:
        case ID_TRAY_AUDIO_ON: case ID_TRAY_HOLIDAY:
        case ID_TRAY_THEME_LIGHT: case ID_TRAY_THEME_DARK:
        case ID_MINI_SHOW: case ID_MINI_HIDE:
        case ID_TRAY_AUTOSTART: case ID_TRAY_TEST:
        case ID_TRAY_NEXT: case ID_TRAY_STATUS:
            {
                int send = id;
                if (id == ID_MINI_SHOW) send = ns ? ID_MINI_HIDE : ID_MINI_SHOW;   // 勾选框来回切
                SendMessageW(o, WM_COMMAND, (WPARAM)send, 0);
                SetSyncControls();
                return 0;
            }
        }
        break;
    }
    case WM_HSCROLL:
    {
        HWND c = (HWND)lParam;
        if (c == sVol[0] || c == sVol[1] || c == sVol[2])
        {
            int v = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            if (c == sVol[0]) ab = v; else if (c == sVol[1]) FUCK = v; else bgmV = v;
            SaveAudioCfg();
            SetSyncControls();
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        stw = NULL;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowSettingsWindow(HWND owner)
{
    (void)owner;
    if (stw) { ShowWindow(stw, SW_RESTORE); SetForegroundWindow(stw); return; }
    static bool reg = false;
    if (!reg)
    {
        reg = true;
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = SetProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"MobileSetWnd";
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = AppIcon();
                RegisterClassW(&wc);
    }
    const int CW = 560, CH = 776;
    RECT r = {0, 0, CW, CH};
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, st, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2 - 30;
    // 关键：不加 WS_EX_TOOLWINDOW、也不给 owner → 它会像正常程序一样出现在任务栏
    stw = CreateWindowExW(0, L"MobileSetWnd", L"mobile 设置", st,
                          x, y, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!stw) return;

    // ---- 作息表 ----
    SetCtl(L"BUTTON", L"作息表", BS_GROUPBOX, 12, 8, 536, 76, -1);
    sTbl[0] = SetCtl(L"BUTTON", L"初四夏季作息", BS_AUTORADIOBUTTON | WS_GROUP, 28, 34, 150, 24, ID_TBL_OLD);
    sTbl[1] = SetCtl(L"BUTTON", L"冬季作息",     BS_AUTORADIOBUTTON,            190, 34, 140, 24, ID_TBL_WINTER);
    sTbl[2] = SetCtl(L"BUTTON", L"初四冬季作息", BS_AUTORADIOBUTTON,            344, 34, 160, 24, ID_TBL_W4);

    // ---- 时间提醒（建满 11 个，按当前作息表显示/隐藏）----
    SetCtl(L"BUTTON", L"时间提醒（打钩＝到点响铃）", BS_GROUPBOX, 12, 92, 536, 186, -1);
    for (int i = 0; i < 11; i++)
    {
        int col = i % 2, row = i / 2;
        sPeriod[i] = SetCtl(L"BUTTON", L"", BS_AUTOCHECKBOX,
                            28 + col * 258, 116 + row * 26, 250, 24, ID_PERIOD_BASE + i);
    }

    // ---- 提醒音 ----
    SetCtl(L"BUTTON", L"提醒音 / 放学音乐", BS_GROUPBOX, 12, 286, 536, 192, -1);
    sAudioOn = SetCtl(L"BUTTON", L"播放提醒音", BS_AUTOCHECKBOX, 28, 310, 220, 24, ID_TRAY_AUDIO_ON);
    SetCtl(L"STATIC", L"上课铃", SS_LEFT, 28, 346, 84, 20, -1);
    sVol[0]    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, 118, 338, 268, 30, -1);
    sVolPct[0] = SetCtl(L"STATIC", L"0%", SS_LEFT, 394, 346, 52, 20, -1);
    SetCtl(L"BUTTON", L"试听", BS_PUSHBUTTON, 452, 339, 62, 28, ID_SET_PREV0);
    SetCtl(L"STATIC", L"下课铃", SS_LEFT, 28, 380, 84, 20, -1);
    sVol[1]    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, 118, 372, 268, 30, -1);
    sVolPct[1] = SetCtl(L"STATIC", L"0%", SS_LEFT, 394, 380, 52, 20, -1);
    SetCtl(L"BUTTON", L"试听", BS_PUSHBUTTON, 452, 373, 62, 28, ID_SET_PREV1);
    sBgmOn = SetCtl(L"BUTTON", L"放学播放《回家》（同目录 Home.mp3）", BS_AUTOCHECKBOX, 28, 412, 320, 24, ID_SET_BGM_ON);
    SetCtl(L"STATIC", L"《回家》音量", SS_LEFT, 28, 448, 84, 20, -1);
    sVol[2]    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, 118, 440, 268, 30, -1);
    sVolPct[2] = SetCtl(L"STATIC", L"0%", SS_LEFT, 394, 448, 52, 20, -1);
    SetCtl(L"BUTTON", L"试听", BS_PUSHBUTTON, 452, 441, 62, 28, ID_SET_PREV2);

    // ---- 主题 ----
    SetCtl(L"BUTTON", L"主题", BS_GROUPBOX, 12, 486, 536, 70, -1);
    sTheme[0] = SetCtl(L"BUTTON", L"浅色", BS_AUTORADIOBUTTON | WS_GROUP, 28, 508, 90, 24, ID_TRAY_THEME_LIGHT);
    sTheme[1] = SetCtl(L"BUTTON", L"深色", BS_AUTORADIOBUTTON,            124, 508, 90, 24, ID_TRAY_THEME_DARK);
    sHoliday  = SetCtl(L"BUTTON", L"特殊节日提醒", BS_AUTOCHECKBOX,       300, 508, 230, 24, ID_TRAY_HOLIDAY);

    // ---- 迷你岛 ----
    SetCtl(L"BUTTON", L"迷你岛", BS_GROUPBOX, 12, 564, 536, 56, -1);
    sMini = SetCtl(L"BUTTON", L"显示迷你岛（顶边时间）", BS_AUTOCHECKBOX, 28, 586, 300, 24, ID_MINI_SHOW);

    // ---- 其他 ----
    SetCtl(L"BUTTON", L"其他", BS_GROUPBOX, 12, 628, 536, 76, -1);
    SetCtl(L"BUTTON", L"立即测试",      BS_PUSHBUTTON, 28,  650, 160, 28, ID_TRAY_TEST);
    SetCtl(L"BUTTON", L"下次提醒时间",  BS_PUSHBUTTON, 196, 650, 160, 28, ID_TRAY_NEXT);
    SetCtl(L"BUTTON", L"状态信息",      BS_PUSHBUTTON, 364, 650, 160, 28, ID_TRAY_STATUS);
    SetCtl(L"BUTTON", L"开机自启动",    BS_PUSHBUTTON, 28,  680, 160, 28, ID_TRAY_AUTOSTART);
    SetCtl(L"BUTTON", L"退出程序",      BS_PUSHBUTTON, 364, 680, 160, 28, ID_TRAY_EXIT);
    SetCtl(L"BUTTON", L"关闭",          BS_PUSHBUTTON, 436, 724, 100, 30, ID_SET_CLOSE);

    // 滑块的量程
    for (int i = 0; i < 3; i++)
        if (sVol[i]) SendMessageW(sVol[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));

    SetSyncControls();
    ShowWindow(stw, SW_SHOW);
    UpdateWindow(stw);
    SetForegroundWindow(stw);
}

void ShowTrayMenu(HWND hWnd)
{
    HMENU hMenu = CreatePopupMenu();
    // 原先这些二级菜单项整个搬进了图形设置窗口（用户要求"像应用那样有窗口画面"）
    // 托盘只留"打开设置"和"退出"两项
    AppendMenu(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"设置…");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, ii.x, ii.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

void ShowMessageIsland(const wchar_t* text, int extraW)
{
    KillTimer(o,1); KillTimer(o,2); KillTimer(o,3); KillTimer(o,4);
    hh = true;
    wcscpy_s(gg, text);
    vv = 0.0f; af = false; ww = 0;
    if (extraW <= 0) privKind = 0;         // 普通消息：把左边的图标位让出来
    // ⚠️ 大岛永远是"普通圆角"。sf 是迷你岛的吸顶形状因子（1=上两角收成直角），
    // 迷你岛状态退出后会留着 1 —— 不清掉的话，紧接着弹出来的大岛**上两角是直角**，
    // 看着就是"上半截和迷你岛一样，没有圆角"。
    sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;
    // extraW：左边要放图标时多留出来的宽度（隐私提示用），普通消息传 0
    FitBigWidth(NULL, gg, NULL, extraW);
    L = true;
    // 大岛已经挂在屏幕上了（典型场景：连着点「开机自启动」）→ 只把字换掉，
    // 别再从头弹跳一遍 —— 用户明确要求"直接变字体就行了"。
    if (J == 1 || J == 3)
    {
        if (J == 3) { F = g; D = (B - F) / 2; E = ISLAND_FINAL_Y; }
        S = NowMs() + DISPLAY_DURATION_MS;      // 换字之后重新计 5 秒
        UpdateIslandDisplay(o, 1.0f, 255);
        return;
    }
    O=0; P=0;
    J=1;

    PrepareBackdropBeforeShow();
    R = NowMs(); // 交主循环 60fps 调度
}

// ===== 麦克风 / 摄像头被占用 → 弹岛提示（2026-10-13 加）=====
// 数据源是 Windows 自己那份"隐私设备使用记录"（就是系统托盘里"摄像头正在使用"
// 那个小图标的同一份数据）：
//   HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore
//        \<microphone|webcam>\<NonPackaged|Packaged>\<应用>
//            LastUsedTimeStart / LastUsedTimeStop   （FILETIME）
// 判据：start != 0 且 stop == 0 → 正在占用。
// ⚠️ 这个键是系统管的，**只读，绝不往里写**。
static ULONGLONG ReadTimeVal(HKEY k, const wchar_t* name)
{
    BYTE buf[16] = {0};
    DWORD sz = sizeof(buf), type = 0;
    if (RegQueryValueExW(k, name, NULL, &type, buf, &sz) != ERROR_SUCCESS) return 0;
    if (type == REG_BINARY && sz >= 8) { ULONGLONG v = 0; memcpy(&v, buf, 8); return v; }
    if (type == REG_DWORD  && sz >= 4) { DWORD v = 0;    memcpy(&v, buf, 4); return v; }
    return 0;
}

bool PrivacyDeviceInUse(const wchar_t* dev)
{
    const wchar_t* CATS[2] = { L"NonPackaged", L"Packaged" };
    FILETIME ftNow; GetSystemTimeAsFileTime(&ftNow);
    ULONGLONG now = 0; memcpy(&now, &ftNow, 8);
    const ULONGLONG FRESH = 12ULL * 3600ULL * 10000000ULL;      // 只认 12 小时内的起点
    for (int c = 0; c < 2; c++)
    {
        wchar_t path[512];
        swprintf_s(path, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager"
                         L"\\ConsentStore\\%ls\\%ls", dev, CATS[c]);
        HKEY hk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hk) != ERROR_SUCCESS) continue;
        DWORD sub = 0;
        RegQueryInfoKeyW(hk, NULL, NULL, NULL, &sub, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        bool hit = false;
        for (DWORD i = 0; i < sub && !hit; i++)
        {
            wchar_t name[512]; DWORD cch = 512;
            if (RegEnumKeyExW(hk, i, name, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) continue;
            HKEY sk;
            if (RegOpenKeyExW(hk, name, 0, KEY_READ, &sk) != ERROR_SUCCESS) continue;
            ULONGLONG st = ReadTimeVal(sk, L"LastUsedTimeStart");
            ULONGLONG sp = ReadTimeVal(sk, L"LastUsedTimeStop");
            RegCloseKey(sk);
            // start 非 0 且 stop 是 0 → 正在用。
            // 再加一条"起点得够近"：程序崩掉会把 stop 永远留在 0，
            // 这种历史残留不该开机就报警。
            if (st != 0 && sp == 0 && st <= now && now - st <= FRESH) hit = true;
        }
        RegCloseKey(hk);
        if (hit) return true;
    }
    return false;
}

// 弹出这条提示。privKind 必须先设好 —— ShowMessageIsland 按它决定左边要多留多宽。
void ShowPrivacyIsland(int kind)
{
    privKind = kind;
    const wchar_t* txt = (kind == 3) ? L"麦克风、摄像头被使用中"
                       : (kind == 2) ? L"摄像头被使用中"
                                     : L"麦克风被使用中";
    int extra = PRIV_PAD_L + PRIV_ICON_W + PRIV_GAP;
    if (kind == 3) extra += PRIV_ICON_W + 10;         // 两个图标并排，再多留一个
    ShowMessageIsland(txt, extra);
}

// 每秒看一眼：谁"从不使用变成正在使用"就弹一次；岛正忙就先挂起来，等空出来再弹。
void PrivacyTick()
{
    static int  lastMic = 0, lastCam = 0;
    static bool inited = false;
    int mic = PrivacyDeviceInUse(L"microphone") ? 1 : 0;
    int cam = PrivacyDeviceInUse(L"webcam")     ? 1 : 0;
    if (!inited) { inited = true; lastMic = mic; lastCam = cam; return; }   // 开机第一次只记状态
    int want = (mic && cam) ? 3 : (mic ? 1 : (cam ? 2 : 0));
    bool changed = (mic != lastMic) || (cam != lastCam);
    lastMic = mic; lastCam = cam;
    if (!want) return;                              // 都松开了：岛上那条自己会收回，不用管
    if (!changed && want == privKind) return;       // 没变化也没换设备 → 别重复弹
    if (J == 0 || J == 3 || J == 11 || J == 12) { privWait = false; ShowPrivacyIsland(want); }
    else { privWait = true; privWant = want; }      // 大岛/变身正忙 → 挂起来
}

// ===== 节日彩蛋弹窗：只挂在"今天最后一节课下课"（放学）那一刻 =====
// 用户明确要求：不要一开机就弹，也不要"明天就是…"，就在最后一节课下课后弹一次「中秋快乐」。
// 判断"这节课是不是最后一场下课"：今天所有打钩的时段里，再没有比它更晚的下课了。
bool IsLastClassEndOfDay(int curMin)
{
    for (int i = 0; i < pc; i++)
    {
        if (!PeriodEnabled(i)) continue;                 // 没打钩的时段不算
        int h = 0, m = 0;
        if (!GetPeriodTime(i, ALARM_TYPE_CLASS_END, &h, &m)) continue;
        if (h * 60 + m > curMin) return false;           // 后面还有更晚的下课 → 这不是最后一节
    }
    return true;
}

static void HolidayEggKey(wchar_t* key, size_t n)
{
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(key, n, L"HolidayEggShown_%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
}
// 同一天只弹一次（不管当天启动过几次程序）
bool HolidayEggShownToday()
{
    wchar_t key[64] = {0};
    HolidayEggKey(key, 64);
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    DWORD v = 0, sz = sizeof(v);
    bool shown = (RegQueryValueEx(hKey, key, NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return shown;
}
void MarkHolidayEggShown()
{
    wchar_t key[64] = {0};
    HolidayEggKey(key, 64);
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = 1;
        RegSetValueEx(hKey, key, 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

void LoadNotchCfg()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD v = 1, sz = sizeof(v);      // 默认 1 = 冬季作息
        if (RegQueryValueEx(hKey, L"NotchShow", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) ns = (v != 0);
        RegCloseKey(hKey);
    }
}
void SaveHolidayCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = hon ? 1 : 0;
        RegSetValueEx(hKey, L"HolidaySkin", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}
void SaveNotchCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = ns ? 1 : 0;
        RegSetValueEx(hKey, L"NotchShow", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

// ===== 迷你岛（贴屏幕顶边的"刘海"小时钟，1.1.13 加）=====
// 它接手了以前"常驻小岛显示时间"的活；小岛现在只管课间倒计时。
// 触控环境（希沃白板）也走同一套：长按 = 收起，托盘二级菜单里可开关。
void ShowNotch()
{
    if (!ns) return;
    if (J == 11 || J == 12) return;
    if (J == 1 || J == 3 || J == 4 || J == 5 || J == 9 || J == 10) return;   // 大岛/变身优先，不抢
    pp = false;                                // 迷你岛只显示时间，绝不去显示倒计时
    hh = false;                                // 同上：常驻岛不是消息，别把时间吞了
    if (J == 6 || J == 7)                      // 小岛（倒计时）正显示 → 直接变身成迷你岛，不先收再弹
    {
        nt = false; ae = F; an = G; ao = E;
        gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
        oo = 0; vv0 = 1.0f; vt = 1.0f; sf0 = sf; sf1 = 0.0f;   // 两端都是圆角，形状不用变
        J = 5;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    nt = true; nv = 255; na = NowMs();
    F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = NOTCH_Y; D = (B - F) / 2;
    O = 0; vv = 1.0f; ai = -1;
    J = 11;                                   // 从屏幕边缘上方"落"下来
    PrepareBackdropBeforeShow();
    R = NowMs();
}
void HideNotch()
{
    if (J == 11 || J == 12) { J = 13; P = 0; R = NowMs(); }
}
// 迷你岛常驻期间的心跳：到秒就重绘（分钟跳变），静止够久就把整块降成半透明
void NotchHoldTick()
{
    hh = false;          // 兜底：迷你岛只显示时间
    int nowSec = (int)(NowMs() / 1000);
    if (nowSec != ai) ai = nowSec;
    int target = (NowMs() - na > NOTCH_DIM_MS) ? NOTCH_DIM_A : 255;
    if (nv != target)
    {
        int d = target - nv, step = 10;
        if (d > step) nv += step;
        else if (d < -step) nv -= step;
        else nv = target;
    }
    // ===== 自愈（2026-10-13 加）=====
    // 迷你岛是这个程序的"常驻形态"，一挂就是几个小时。它是置顶分层窗口，时间长了有三种翻车法：
    //   ① 后面又冒出别的置顶窗口（全屏视频、白板的置顶工具条、任务管理器"置于顶层"）把它压住；
    //   ② 系统省电/切分辨率/远程桌面上来之后，窗口内容被丢掉；
    //   ③ 窗口本身被谁 ShowWindow(SW_HIDE) 了。
    // 原来只在"秒数变了"的时候重画，一旦被压住/丢掉就永远回不来 —— 用户报的"开着开着迷你岛没了"就是它。
    // 现在每秒：确认还显示着 → 确认还在最上层（**只在真被盖住时才动手**）→ 重画一帧。
    // ⚠️ 不能每秒无条件 SetWindowPos(HWND_TOPMOST)：那会一直把别的置顶窗口往下压，
    //    实测会把同机的设置窗口都顶不下去（踩过）。只在被盖住时才重新顶。
    if (!IsWindowVisible(o)) ShowWindow(o, SW_SHOWNOACTIVATE);
    POINT c = { D + F / 2, E + G / 2 };              // 岛正中心（屏幕坐标）
    if (WindowFromPoint(c) != o)
        SetWindowPos(o, HWND_TOPMOST, n, 0, j, l, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    UpdateIslandDisplay(o, 1.0f, 255);
}

// 定时触发唯一入口：两条触发链都走这里，本分钟只放行一次。
// 以前各判各的，谁后到谁就再弹一次 —— 那个"弹一次又弹一次"的 fuck bug 就是它
void TriggerAlarmByTimer(AlarmType type)
{
    SYSTEMTIME st; GetLocalTime(&st);
    int curMin = st.wHour * 60 + st.wMinute;
    if (N == curMin) return;      // 本分钟已经弹过了，吞掉
    N = curMin;
    // 2026-10-13 用户改回去了：放学（今天最后一节课下课）**照常敲下课铃**，
    // 《回家》挪到固定 19:30 单独播（见 case 10 里的 BGM_HOUR/BGM_MIN）。
    AudioPlayAsync(type);        // 后台放音：别让 MCI 的启动耗时挡住岛的动画
    TriggerAlarm(type);
}

// 手动触发（托盘「立即测试」/ Ctrl+Alt+S）：绕开"每分钟只放行一次"，并把 N 还回去
void TriggerAlarmManual(AlarmType type)
{
    int keep = N;
    AudioPlayAsync(type);
    TriggerAlarm(type);
    N = keep;
}

void TriggerAlarm(AlarmType type)
{
    KillTimer(o,1); KillTimer(o,2); KillTimer(o,3); KillTimer(o,4);
    O=0; P=0;
    af = false; ww = 0;                     // 收起"长按关闭"提示
    ja = 0; he = false; hs = 0; ta = 255;   // 清掉上一轮的衔接窗口与"两段式"状态

    // 这一轮是不是"今天最后一节课下课"（放学）。下面两处都要用，先算一次
    bool lastEnd = false;
    if (type == ALARM_TYPE_CLASS_END)
    {
        SYSTEMTIME st; GetLocalTime(&st);
        lastEnd = IsLastClassEndOfDay(st.wHour * 60 + st.wMinute);
    }
    // 播放器还开着（《回家》在放）又来了别的提醒 → 先把它收掉，别跟这一轮的大岛抢画面
    if (ply && !lastEnd) { BgmStop(); ply = false; bgmPause = false; plyPauseAt = 0; }
    // 普通闹钟一律回到标准高度；播放器那个更高的长方形由 ShowPlayerIsland 单独设
    bigH = ISLAND_HEIGHT;

    // 特殊日期（中秋/国庆，含前一天）：正好是放学的时侯，
    // 先照常显示「下课了」+ 时间；等它显示完，再让文字淡出、换上「国庆节快乐」
    // （走 J=10 两段式，见 AnimTick）—— 所以这里只登记"待会儿要接祝福"，不打断正常下课流程
    if (lastEnd && HolidayToday() && !HolidayEggShownToday())
    {
        MarkHolidayEggShown();
        he = true;
    }
    // 放学不再放《回家》了：音乐改成固定 19:30 播（case 10 里触发），放学这一下只打铃。
    bool wasMini = (J == 6 || J == 7 || J == 11 || J == 12);
    sf0 = 0.0f;                                 // 迷你岛也改圆角了，sf 恒 0（不再有"上两角直角"这套）
    sf1 = 0.0f;
    if (J == 11 || J == 12) nt = false;        // 迷你岛 → 交给变身接管，形状切回普通圆角
    if (type == ALARM_TYPE_CLASS_END)
    {
        // 下课：倒计时"立刻"开始跑（不管大岛是否还在显示），大岛走完再变身小岛
        LONGLONG tgt = 0;
        if (NextBreakTarget(&tgt)) { pp = true; rr = tgt; bb = true; }
        else                       { pp = false; bb = false; }
    }
    else if (type == ALARM_TYPE_CLASS_START)
    {
        // 上课：课间结束，常驻小岛要收掉 —— 若此刻正显示，交给下面的变身统一接管，避免先缩回再弹出
        pp = false; bb = false;
    }
    ai = -1;
    hh = false;                              // 真闹钟显示时间，不显示上一条消息
    M=type; L=true;
    {   // 大岛宽度随文字自适应：量"事件标签"和"时间"里更宽的那个（节日多量一个后缀的宽度）
        wchar_t lb[32] = {0}, tm[24] = {0};
        AlarmLabel(type, lb);
        SYSTEMTIME st; GetLocalTime(&st);
        swprintf_s(tm, L"%02d:%02d", st.wHour, st.wMinute);
        FitBigWidth(lb, tm, HolidayToday() ? L"00●" : NULL);
    }
    if (wasMini)
    {
        ae = F; an = G; ao = E;              // 从小岛/迷你岛"当前"尺寸接着长，接缝处不跳变
        gw = g; gh = ISLAND_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 3;
        oo = 0; vv0 = 1.0f; vt = 0.0f; vv = 1.0f; J = 9;
    }
    else
    {
        vv0 = 0.0f; vt = 0.0f; vv = 0.0f;    // 大岛文字从实心开始
        sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;   // 从零弹出的大岛＝普通圆角（别漏成迷你岛的直角）
        J = 1;
    }
    {   // 记录本分钟已触发，避免"定时线程"与"每分钟兜底检查"重复弹出两次
        SYSTEMTIME st; GetLocalTime(&st);
        N = st.wHour*60 + st.wMinute;
    }

    PrepareBackdropBeforeShow();
    R = NowMs(); // 交主循环 60fps 调度
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };   // 屏幕坐标
        return IslandHitTest(pt) ? HTCLIENT : HTTRANSPARENT;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;

    // 鼠标按在岛上：显示提示 + 开始长按计时
    case WM_LBUTTONDOWN:
    {
        POINT cp; GetCursorPos(&cp);               // 这条消息的 lParam 是客户区坐标，命中要用屏幕坐标
        if (PlayerButtonHit(cp)) return 0;         // 点在播放/暂停键上 → 只切播放状态，别起长按
        if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
        SetCapture(hWnd);
        return 0;
    }
    case WM_LBUTTONUP:
        xx = false; af = false;
        if (GetCapture() == hWnd) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (xx) { xx = false; af = false; }
        return 0;

    // 触控（指针模型）：Windows 会把触摸派发为 WM_POINTER
    case WM_POINTERDOWN:
    {   // WM_POINTER 的 GET_X/Y_LPARAM 给的就是屏幕坐标，正好直接拿去命中
        POINT cp = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PlayerButtonHit(cp)) return 0;
        if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
        return 0;
    }
    case WM_POINTERUP:
    case WM_POINTERCAPTURECHANGED:
        if (xx) { xx = false; af = false; }
        return 0;

    // 触控（WM_TOUCH 模型，RegisterTouchWindow 之后走这条）
    case WM_TOUCH:
    {
        UINT n = LOWORD(wParam);
        TOUCHINPUT ti[8];
        if (n > 0 && n <= 8 && GetTouchInputInfo((HTOUCHINPUT)lParam, n, ti, sizeof(TOUCHINPUT)))
        {
            for (UINT k = 0; k < n; k++)
            {
                if (ti[k].dwFlags & TOUCHEVENTF_DOWN)
                {
                    // TOUCHINPUT 的坐标单位是 1/100 像素、且是屏幕坐标
                    POINT cp = { (LONG)(ti[k].x / 100), (LONG)(ti[k].y / 100) };
                    if (PlayerButtonHit(cp)) continue;
                    if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
                }
                if (ti[k].dwFlags & TOUCHEVENTF_UP)
                    if (xx) { xx = false; af = false; }
            }
            CloseTouchInputHandle((HTOUCHINPUT)lParam);
        }
        return 0;
    }

    case WM_ALARM_TRIGGER:
        TriggerAlarmByTimer((AlarmType)wParam);
        break;

    case WM_HOTKEY:
        if (wParam == HOTKEY_ID_DEBUG)
            TriggerAlarmManual(ALARM_TYPE_SINGLE);      // 快捷键同样是"手动"，也绕开每分钟去重
        else if (wParam == HOTKEY_ID_SETTINGS)
            ShowSettingsWindow(o);                      // Ctrl+Alt+S：打开设置（以前注册了却没接上）
        else if (wParam == HOTKEY_ID_PLAYER)            // Ctrl+Alt+P：测试键 —— 直接弹出播放器样式的大岛
        {
            BgmPlay();
            ShowPlayerIsland();
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            GetCursorPos(&ii);
            ShowTrayMenu(hWnd);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_TEST: TriggerAlarmManual(ALARM_TYPE_SINGLE); break;
        case ID_TRAY_NEXT: ShowNextAlarmTime(); break;
        case ID_TRAY_AUTOSTART: ToggleAutoStart(); break;
        case ID_TRAY_THEME_LIGHT:
        case ID_TRAY_THEME_DARK:
            z = (LOWORD(wParam)==ID_TRAY_THEME_DARK);
            SaveTheme();
            if (J == 3) UpdateIslandDisplay(hWnd, 1.0f, 255);
            break;
        case ID_TRAY_SETTINGS:
            ShowSettingsWindow(hWnd);
            break;
        case ID_TBL_OLD:
        case ID_TBL_WINTER:
        case ID_TBL_W4:
        {
            int w = LOWORD(wParam);
            tbl = (w == ID_TBL_OLD) ? 0 : (w == ID_TBL_WINTER ? 1 : 2);
            pc = (tbl == 0) ? 11 : 9;
            ff = (tbl == 0) ? ffA : (tbl == 1 ? ffB : ffC);
            SaveTimeTable();
            LoadAlarmTimes();          // 换表 → 重新载入时间
            LoadPeriodEnabled();       // 勾选位跟着新表重新对应
            if (J == 3 || J == 12) UpdateIslandDisplay(hWnd, 1.0f, 255);
            break;
        }
        case ID_TRAY_HOLIDAY:
            hon = !hon;
            SaveHolidayCfg();
            if (J == 3 || J == 12) UpdateIslandDisplay(hWnd, 1.0f, 255);   // 立刻换色/去掉节日色
            break;
        case ID_TRAY_STATUS:
        {
            wchar_t buf[512];
            swprintf_s(buf,
                L"运行状态\n\n"
                L"系统版本：Windows build %lu\n"
                L"屏幕：%d × %d（DPI 感知：%ls）\n"
                L"底板：纯色（深色=黑灰 / 浅色=浅白，不做任何背景渲染）\n"
                L"圆角：分层窗口逐像素 alpha（形状覆盖率 + 1.5px 羽化）\n"
                L"动画：大岛弹出 与 大→小变身 同速率（62 帧，实测约 1.2s）/ 小岛 46 帧约 0.9s / 收回约 0.6s\n"
                L"常驻小岛：%ls（宽 %d 高 %d）\n"
                L"提醒音：%ls（上课铃 %d%% / 下课铃 %d%%）\n"
                L"音频文件：%ls\n"
                L"下滑手势：%ls\n"
                L"节日彩蛋：%ls\n"
                L"画布：%d × %d（已含过冲余量）\n"
                L"岛尺寸：%d × %d",
                (unsigned long)T,
                B, C, Q ? L"已启用" : L"未启用",
                (J == 7 || J == 6) ? L"正在显示" : ((J == 5 || J == 9) ? L"变身中" : (bb ? L"待显示" : L"未显示")),
                MINI_WIDTH, MINI_HEIGHT,
                fuck ? L"开" : L"关", ab, FUCK,
                nn[0] ? nn : L"未找到 alin.mp3",
                L"轮询",
                (HolidayToday() == 0) ? L"今天不是节日" : HolidayGreet(),
                j, l,
                g, ISLAND_HEIGHT);
            MessageBox(hWnd, buf, L"mobile 状态信息", MB_OK | MB_ICONINFORMATION);
        }
        break;
        case ID_MINI_SHOW:                     // 展开迷你岛（实时时间从此归它）
            ns = true; SaveNotchCfg();
            ShowNotch();
            break;

        case ID_MINI_HIDE:                     // 收起迷你岛（关掉，别让它马上又自己回来）
            ns = false; SaveNotchCfg();
            HideNotch();
            break;
        case ID_TRAY_AUDIO_ON:
            fuck = !fuck;
            SaveAudioCfg();
            if (!fuck) AudioStop();
            break;
        // 独立音量面板（全自绘那个小窗）2026-10-13 用户要求删掉，入口一并撤了。
        case ID_TRAY_UNINSTALL: DoUninstall(false); break;
        case ID_TRAY_EXIT: DestroyWindow(hWnd); break;
        default:
        {
            int periodIdx = LOWORD(wParam) - ID_PERIOD_BASE;
            if (periodIdx >= 0 && periodIdx < pc)
            {
                SetPeriodEnabled(periodIdx, !PeriodEnabled(periodIdx));
                SavePeriodEnabled();
                SetTimer(hWnd, ID_TIMER_RESHOW_MENU, 20, NULL);
            }
        }
        break;
        }
        break;

    case WM_TIMER:
        switch (wParam)
        {
        case 777:
            KillTimer(hWnd,777); TriggerAlarm(ALARM_TYPE_CLASS_START); break;

        // 动画由主循环 60fps 调度

        case 10:
        {
            SYSTEMTIME st; GetLocalTime(&st);
            for (const auto& alarm : jj)
            {
                if (!alarm.enabled) continue;       // 没打钩的时段一律不响、不弹
                if (alarm.hour==st.wHour && alarm.minute==st.wMinute)
                {
                    TriggerAlarmByTimer(alarm.type);
                    break;
                }
            }

            int nowMin = st.wHour * 60 + st.wMinute;

            // ⓪ 麦克风 / 摄像头被占用 → 弹岛提示（每秒看一眼，只在"刚被占用"那一下弹）
            PrivacyTick();
            if (privWait && (J == 0 || J == 3 || J == 11 || J == 12))
            {
                privWait = false;
                ShowPrivacyIsland(privWant);
            }

            // ① 《回家》固定 19:30 播（用户 2026-10-13 从"放学那一下"挪过来的）。
            //    和下课铃完全解耦：放学照常打铃，音乐到点自己响。
            //    nb 每分钟都跟着走，所以第二天同一时刻还会再触发（只比"小时:分"会漏掉第二天）。
            if (nb != nowMin)
            {
                nb = nowMin;
                if (st.wHour == BGM_HOUR && st.wMinute == BGM_MIN && bgmOn && !bgmPlay)
                {
                    BgmPlay();
                    if (bgmPlay) ShowPlayerIsland();   // 音乐真起来了才弹播放器岛
                }
            }

            // ② 全局上课倒计时：只要"这会儿在课间"，就该有小岛显示倒计时，**不看这一节有没有打钩**
            //    （打钩只管铃声）。原来只有"打了钩的下课闹钟"才会顺手把小岛叫出来，
            //    于是没打钩的课间彻底没有倒计时 —— 用户报的就是这个。
            //    J==0 是空屏、11/12 是迷你岛挂着，这两种情况都该让位给倒计时小岛；
            //    大岛/变身(j1/3/4/5/9/10)优先，不抢。
            if (J == 0 || J == 11 || J == 12)
            {
                LONGLONG t0 = 0;
                if (BreakInfoActive(&t0))
                {
                    if (!bb) { pp = true; rr = t0; bb = true; ShowMiniIsland(); }
                    else if (pp && rr != t0) { rr = t0; ai = -1; }
                }
            }
        }
        break;

        case 12:
            GestureTick();
            break;

        case ID_TIMER_RESHOW_MENU:
            KillTimer(hWnd, ID_TIMER_RESHOW_MENU);
            ShowTrayMenu(hWnd);
            break;
        }
        break;

    // 《回家》放完（MCI 的 notify 回调）：自动把播放器岛收掉。
    // 只有 SUCCESSFUL 才算"自然放完" —— 我们自己 close 时来的是 ABORTED，不能当成放完处理。
    case MM_MCINOTIFY:
        if (wParam == MCI_NOTIFY_SUCCESSFUL && ply)
        {
            BgmStop();
            bgmPause = false; plyPauseAt = 0;
            // ply 先留着 —— 收回这段还要保持"播放器内容"，不然会闪一下普通闹钟的字；
            // 真正的清理在 J=4 收回动画走完那一步
            if (J == 1 || J == 3) { P = 0; J = 4; }
            R = NowMs();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd,10);
        RemoveTrayIcon(hWnd);
        UnregisterHotKey(hWnd,HOTKEY_ID_DEBUG);
        UnregisterHotKey(hWnd,HOTKEY_ID_SETTINGS);
        UnregisterHotKey(hWnd,HOTKEY_ID_PLAYER);
        CoverFree();                       // 封面缓存
        CleanupCachedResources();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void ToggleAutoStart()
{
    const wchar_t* szValueName = L"IslandAlarm";
    const wchar_t* szSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    wchar_t szPath[MAX_PATH];
    GetModuleFileName(NULL, szPath, MAX_PATH);
    bool bEnabled = false;
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, szSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t szData[MAX_PATH*2]; DWORD dwSize=sizeof(szData);
        if (RegQueryValueEx(hKey, szValueName, NULL, NULL, (LPBYTE)szData, &dwSize)==ERROR_SUCCESS) bEnabled=true;
        RegCloseKey(hKey);
    }
    if (bEnabled)
    {
        if (RegOpenKeyEx(HKEY_CURRENT_USER, szSubKey, 0, KEY_SET_VALUE, &hKey)==ERROR_SUCCESS)
        { RegDeleteValue(hKey, szValueName); RegCloseKey(hKey); }
        ShowMessageIsland(L"开机自启动已关闭");     // 开/关都回一句，不然点了没有任何反馈
    }
    else
    {
        wchar_t szCommand[MAX_PATH*2+4];
        swprintf_s(szCommand, L"\"%ls\"", szPath);
        if (RegOpenKeyEx(HKEY_CURRENT_USER, szSubKey, 0, KEY_SET_VALUE, &hKey)==ERROR_SUCCESS)
        { RegSetValueEx(hKey, szValueName, 0, REG_SZ, (LPBYTE)szCommand, (DWORD)((wcslen(szCommand)+1)*sizeof(wchar_t))); RegCloseKey(hKey); }
        ShowMessageIsland(L"开机自启动已开启");
    }
}

void Cleanup()
{
    if (kk) SetEvent(kk);
    if (mm) WaitForSingleObject(mm, 5000);
    if (ll) CloseHandle(ll);
    if (kk) CloseHandle(kk);
    if (mm) CloseHandle(mm);
    timeEndPeriod(1);
}



int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    q = (lpCmdLine && strstr(lpCmdLine, "--test"));

    // 静默卸载：islands.exe --uninstall
    if (lpCmdLine && strstr(lpCmdLine, "--uninstall"))
    {
            DoUninstall(true);
            return 0;
    }

    // 首次双击 → 自安装然后自退
    if (!EnsureInstalledOrRun()) return 0;

    // 单实例，免得冒出两个托盘图标
    HANDLE hSingle = CreateMutexW(NULL, TRUE, L"IslandAlarm_SingleInstance_v1");
    if (hSingle && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hSingle);
        MessageBoxW(NULL, L"mobile 已经在运行中。\n\n请看任务栏右下角的托盘图标（右键 →「设置…」）。",
                    L"mobile", MB_OK | MB_ICONINFORMATION);
            return 0;
    }

    EnableDpiAwareness(); // 必须在建窗口前：缩放屏下防坐标错位
    {   // 专辑封面走 GDI+ 解码，进程内起一次即可（退出时系统会回收，不必手工 Shutdown）
        ULONG_PTR tok = 0;
        Gdiplus::GdiplusStartupInput gin;
        Gdiplus::GdiplusStartup(&tok, &gin, NULL);
    }
    T = GetOsBuild();
    timeBeginPeriod(1);
    LoadTimeTable();      // 先定作息表（决定时段数与名字），再读勾选和时间
    LoadAlarmTimes();
    LoadPeriodEnabled();
    LoadTheme();
    LoadAudioCfg();
    CreateIslandWindow(hInstance);
    CreateTrayIcon(o);
    RegisterHotKey(o, HOTKEY_ID_DEBUG, MOD_CONTROL|MOD_ALT, 'A');
    RegisterHotKey(o, HOTKEY_ID_SETTINGS, MOD_CONTROL|MOD_ALT, 'S');   // Ctrl+Alt+S = 打开设置
    RegisterHotKey(o, HOTKEY_ID_PLAYER, MOD_CONTROL|MOD_ALT, 'P');     // Ctrl+Alt+P = 测试播放器岛
    StartTimerThread();
    SetTimer(o, 10, 1000, NULL);
    // 不装全局鼠标钩子了：WH_MOUSE_LL 是安全软件（含 Defender）重点盯的行为之一。
    // 改成常驻心跳轮询 —— 鼠标和触控都能查到，功能一样，启动路径里少一个可疑 API。
    SetTimer(o, 12, 120, NULL);

    LoadNotchCfg();
    if (ns)                                   // 迷你岛就是这个程序的"常驻形态"：开机就挂上
    {
        LONGLONG t0 = 0;
        if (BreakInfoActive(&t0)) { pp = true; rr = t0; bb = true; ShowMiniIsland(); }  // 课间中 → 直接给倒计时
        else ShowNotch();
    }
    // 节日彩蛋不在这里弹了：改成"今天最后一节课下课后"由 TriggerAlarm 里弹（见 IsLastClassEndOfDay）

    if (q) SetTimer(o, 777, 1000, NULL);

    // 主循环：动画期 16ms 一帧，其余时间睡觉
    MSG msg;
    bool running = true;
    while (running)
    {
        DWORD wait = INFINITE;
        if (J == 1 || J == 4 || J == 5 || J == 6 || J == 8 || J == 9 || J == 10 || J == 11 || J == 13)
        {
            LONGLONG now = NowMs();
            if (now >= R)
            {
                AnimTick();
                R = NowMs() + FRAME_INTERVAL_MS;
            }
            LONGLONG remain = R - NowMs();
            wait = (remain > 0) ? (DWORD)remain : 0;
        }
        else if (J == 3)
        {
            LONGLONG now = NowMs();
            if (ply) PlayerTick();           // 播放器：把"长按关闭播放器"那行提示淡出去
            if (now >= S)
            {
                if (he)      { he = false; hs = 0; ta = 255; J = 10; }   // 特殊日期 → 接"两段式"，大岛形状不动
                else if (ply) { ShowPlayerIsland(); }                    // 放学音乐在放 → 大岛原地变播放器并常驻
                else if (bb) { hh = false; ae = F; an = G; ao = E; gw = MINI_WIDTH; gh = MINI_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 7;
                               vv0 = vv; vt = 1.0f; oo = 0; sf0 = sf; sf1 = 0.0f; J = 5; }   // 有常驻小岛 → 直接变身
                else if (ns) { hh = false; ae = F; an = G; ao = E; gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
                               vv0 = vv; vt = 1.0f; oo = 0; nt = false; sf0 = sf; sf1 = 0.0f; J = 5; }   // 迷你岛开着 → 直接缩成迷你岛
                else         { J = 4; P = 0; }               // 否则照常收回
                R = now;
            }
            LONGLONG remain = S - NowMs();
            if (remain < 1) remain = 1;
            if (remain > 60000) remain = 60000;
            wait = (DWORD)remain;
            // 播放器定格时 S 被顶到 10 分钟后，不能让循环一口气睡 60 秒 ——
            // 那样下面那行提示就没法淡出了。提示还在淡的时候 60ms 一拍，淡完降到 500ms。
            if (ply)
            {
                DWORD pw = (ww > 0 || bgmPause) ? 60 : 500;   // 暂停时要盯 5 秒的自动关闭
                if (wait > pw) wait = pw;
            }
        }
        else if (J == 7)
        {
            MiniHoldTick();          // 常驻小岛：倒计时刷新 / 长按判定 / 提示淡入淡出
            wait = 33;
        }
        else if (J == 12)
        {
            F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = NOTCH_Y; D = (B - F) / 2; sf = 0.0f;
            NotchHoldTick();         // 迷你岛：分钟跳变重绘 + 静止够久降半透明
            wait = 33;
        }

        DWORD rr = MsgWaitForMultipleObjects(0, NULL, FALSE, wait, QS_ALLINPUT);
        if (rr == WAIT_TIMEOUT) continue;

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    Cleanup();
    return (int)msg.wParam;
}
