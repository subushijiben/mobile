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
const int NOTCH_WIDTH   = 150;   // 迷你岛宽度（只包住时间那点字，别留太多空）
const int NOTCH_HEIGHT  = 48;    // 迷你岛高度（从屏幕顶边往下量）
const int NOTCH_RADIUS  = 16;    // 下两角圆角半径
const int NOTCH_DIM_MS  = 10000; // 静止多久才降半透明
const int NOTCH_DIM_A   = 115;   // 半透明程度（255=不透明；115 ≈ 45%）
const int MINI_STEPS    = 46;    // 小岛弹出 / 小→大变身 的步数（46 帧，实测约 0.9s）
                                 // 注意：大→小变身不用它，用的是 ANIMATION_STEPS（与展开大岛同速率）
const int HINT_GAP      = 30;    // 小岛下方"长按关闭常驻岛"那行字要占的高度
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
    int period;      // 属于第几个时段(0..PERIOD_COUNT-1)：排序后靠它跟菜单勾选对齐，别再用 i*2 猜
};

// 画布尺寸：按动画过冲范围自动推导
int g = 0;
int j = 0, l = 0, n = 0;


HWND o     = NULL;
// ---- 提醒音 ----
wchar_t nn[MAX_PATH] = {0};   // alin.mp3 的绝对路径（空=没找到）
bool    fuck  = true;             // 是否播放（托盘「提醒音」里可关）
HWND    qq  = NULL;             // 音量设置小窗
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

// 时段名称（11个）
const wchar_t* ff[] = {
    L"早读", L"第1节课", L"第2节课", L"第3节课", L"第4节课",
    L"午自习", L"第5节课", L"第6节课", L"第7节课", L"课后服务1", L"课后服务2"
};
const int PERIOD_COUNT = 11;

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
HHOOK     ad = NULL;               // 低级鼠标钩子（用于全局检测下滑手势）
int       aj = -1;                  // 音量面板正在拖的滑块（0=上课 1=下课 -1=没在拖）
int       ai = -1;                  // 上次重绘用的"秒"（避免无谓重画）
int       ae = 0, an = 0, ao = 0;   // 变身的**起始**尺寸/位置（从当前实际尺寸接着长，接缝处不跳变）
float     vv0 = 0.0f, vt = 1.0f;    // 变身的起始/目标文字权重（大岛字=0，小岛字=1）
bool      am = false;              // 钩子是否安装成功

// 只有"岛形"那一块让窗口接收输入，其它地方一律穿透 —— 否则顶部 100px 会挡住下面窗口的点击
bool IslandHitTest(POINT ptScreen)
{
    if (J == 0 || F < 2 || G < 2) return false;
    int lx = ptScreen.x - n;
    int ly = ptScreen.y;
    return (lx >= D && lx < D + F && ly >= E && ly < E + G);
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
#define ID_TRAY_AUDIO_ON   1020
#define ID_TRAY_AUDIO_VOL  1021
#define ID_TRAY_UPDATENOTES 1008
#define ID_PERIOD_BASE 2001
#define ID_TIMER_RESHOW_MENU 99
#define ID_TRAY_THEME_LIGHT 1010
#define ID_TRAY_THEME_DARK 1011
#define ID_TRAY_STATUS 1013
#define ID_TRAY_TRANSPARENCY 1014
#define ID_TRAY_UNINSTALL 1016
#define HOTKEY_ID_DEBUG 1
#define HOTKEY_ID_SETTINGS 2

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
void ShowUpdateNotes();
void ShowUpdateNotesIfFirstRun();
void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha);
void DrawIsland(BYTE* p, int stride, int width, int height);
void CompositeGlyph(BYTE* p, const BYTE* mask, int stride, int x0, int y0, int x1, int y1, COLORREF color);
bool EnsureBuffers();
void CleanupCachedResources();
void AnimTick();
LONGLONG NowMs();
void LoadTheme();
void SaveTheme();
void ShowMessageIsland(const wchar_t* text);
void LoadPeriodEnabled();
void AudioPlay(AlarmType type);
void AudioPlayAsync(AlarmType type);       // 后台线程播放，不占 UI 线程
void AudioStop();
void AudioResolvePath();
void LoadAudioCfg();
void SaveAudioCfg();
void ShowVolumeWindow(HWND owner);
void SavePeriodEnabled();
void LoadNotchCfg();
void SaveNotchCfg();
void SaveHolidayCfg();
void ShowSettingsWindow(HWND owner);
void ShowTrayMenu(HWND hWnd);
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
LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam);
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
    float maxBottom = (float)(ISLAND_FINAL_Y + ISLAND_HEIGHT);
    for (int i = 0; i <= 512; i++)
    {
        float t = i / 512.0f;
        float w = ISLAND_MIN_WIDTH  + (g - ISLAND_MIN_WIDTH) * springW(t);
        float h = ISLAND_MIN_HEIGHT + (ISLAND_HEIGHT  - ISLAND_MIN_HEIGHT) * springH(t);
        float y = ISLAND_FINAL_Y * springY(t);
        if (w > maxW) maxW = w;
        if (y + h > maxBottom) maxBottom = y + h;
    }
    j = (int)maxW + 8;
    l = (int)maxBottom + 8;
    if (j < g) j = g;
    if (l < ISLAND_FINAL_Y + ISLAND_HEIGHT) l = ISLAND_FINAL_Y + ISLAND_HEIGHT;
    // 常驻小岛 + 它下方那行提示文字也要完整画出来
    int miniBottom = ISLAND_FINAL_Y + MINI_HEIGHT + HINT_GAP;
    if (l < miniBottom) l = miniBottom;
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
    // 迷你岛（贴屏幕顶边的刘海）：上两角不圆角、只圆下两角 → 直接换一套形状
    if (nt) return NotchCoverage(lx, ly, w, h, NOTCH_RADIUS);
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
    if (U && Y) return true;
    int W = j, H = l;
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
    if (J == 5 || J == 6 || J == 7) return;
    if (J == 1 || J == 3 || J == 4 || J == 9 || J == 10) return;   // 大岛优先：等它走完/收完/变身完再接小岛
    if (J == 11 || J == 12)              // 迷你岛正挂着 → 直接变身成小岛，别先收再弹
    {
        nt = false; ae = F; an = G; ao = E;
        gw = MINI_WIDTH; gh = MINI_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 7;
        oo = 0; vv0 = 1.0f; vt = 1.0f;
        J = 9;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    O = 0; vv = 1.0f; 
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
            gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = 0; gJ = 12;
            oo = 0; vv0 = 1.0f; vt = 1.0f;
            J = 5;
        }
        else { J = 8; P = 0; }
        R = NowMs();
    }
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
    if (J == 6 || J == 7 || J == 8) refH = MINI_HEIGHT;
    else if (J == 5 || J == 9)      refH = (int)(ISLAND_HEIGHT + (MINI_HEIGHT - ISLAND_HEIGHT) * vv);
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

    RECT shapeRect = {ox, oy, ox+w, oy+h};

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
            switch(M){
            case ALARM_TYPE_CLASS_START: wcscpy_s(infoStr,L"上课了");break;
            case ALARM_TYPE_CLASS_END: wcscpy_s(infoStr,L"下课了");break;
            default: wcscpy_s(infoStr,L"时间到！");break;
            }
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
            switch(M){
            case ALARM_TYPE_CLASS_START: wcscpy_s(infoStr,L"上课了");break;
            case ALARM_TYPE_CLASS_END: wcscpy_s(infoStr,L"下课了");break;
            default: wcscpy_s(infoStr,L"时间到！");break;
            }
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

    // 3c) 小岛下方那行提示：不加背景，白字黑描边（浮在桌面上也看得清）
    if (ww > 0 && G > 2)
    {
        RECT hr = {ox, oy + h + 6, ox + w, oy + h + 6 + HINT_GAP - 8};
        const wchar_t* hintStr = L"长按关闭常驻岛";
        int off[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for (int k = 0; k < 4; k++)
        {
            RECT r2 = hr;
            r2.left += off[k][0]; r2.right += off[k][0];
            r2.top  += off[k][1]; r2.bottom+= off[k][1];
            stamp(r2, hintStr, hFontHint, RGB(0,0,0), DT_CENTER|DT_TOP|DT_SINGLELINE, ww*3/4);
        }
        stamp(hr, hintStr, hFontHint, RGB(255,255,255), DT_CENTER|DT_TOP|DT_SINGLELINE, ww);
    }
}

// 把画布内容提交给窗口
void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha)
{
    (void)scale; (void)alpha;
    if (F < 2 || G < 2) return;
    if (!EnsureBuffers()) return;

    DrawIsland(X, cc, j, l);

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
    int startH[] = {7,8,9,10,11,12,14,15,15,16,17};
    int startM[] = {15,0,0,15,10,30,0,0,55,45,35};
    int endH[]   = {7,8,9,11,11,13,14,15,16,17,18};
    int endM[]   = {55,45,45,0,55,15,45,45,40,25,15};
    for (int i=0;i<11;i++)
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

    if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES) return true; // 已安装过 → 正常运行
    if (_wcsicmp(self, exe) == 0) return true;                          // 正从安装目录运行

    // 首次运行 → 装一下
    CreateDirectoryW(dir, NULL);
    if (!CopyFileW(self, exe, FALSE)) return true;                      // 复制失败则按便携版运行
    {   // 提醒音也跟着搬过去（用户把 alin.mp3 放在原 exe 旁边即可）
        wchar_t from[MAX_PATH] = {0}, to[MAX_PATH] = {0};
        wcscpy_s(from, self);
        wchar_t* sl = wcsrchr(from, L'\\');
        if (sl)
        {
            *(sl + 1) = 0;
            wcscat_s(from, L"alin.mp3");
            swprintf_s(to, L"%ls\\alin.mp3", dir);
            if (GetFileAttributesW(from) != INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesW(to) == INVALID_FILE_ATTRIBUTES)
                CopyFileW(from, to, FALSE);
        }
    }

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
        G = (int)(ISLAND_MIN_HEIGHT + (ISLAND_HEIGHT  - ISLAND_MIN_HEIGHT) * springH(t));
        E      = (int)(ISLAND_FINAL_Y * springY(t));
        D      = (B - F) / 2;
        O++;
        if (O > ANIMATION_STEPS)
        {
            O = 0;
            F  = g;
            G = ISLAND_HEIGHT;
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
        oo++;
        if (oo > ANIMATION_STEPS)
        {
            oo = 0;
            F = gw; G = gh; E = gy; D = (B - F) / 2;
            vv = vt;
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
        oo++;
        if (oo > MINI_STEPS)
        {
            oo = 0;
            F = gw; G = gh; E = gy; D = (B - F) / 2;
            vv = vt;
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
        E = (int)(-NOTCH_HEIGHT * (1.0f - springY(t)));
        D = (B - F) / 2;
        vv = 1.0f;
        O++;
        if (O > MINI_STEPS)
        {
            O = 0;
            F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = 0; D = (B - F) / 2;
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
        E = (int)(-NOTCH_HEIGHT * (1.0f - springY(t)));
        D = (B - F) / 2;
        P++;
        if (P > MINI_STEPS)
        {
            P = 0; nt = false; J = 0;
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
        else if (hs == 25) { hh = true; wcscpy_s(gg, HolidayGreet()); ta = 0; }    // 换文案
        else if (hs <= 55)   ta = 255 * (hs - 25) / 30;                            // 淡入 ≈0.6s
        else if (hs <= 190)  ta = 255;                                             // 停留 ≈3s
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
        G = (int)(ISLAND_HEIGHT  * k);
        D      = (B - F) / 2;
        P++;
        if (P > SLIDEOUT_STEPS)
        {
            P = 0;
            if (bb)                       // 收回结束时小岛还在等 → 直接接着弹小岛
            {
                O = 0; vv = 1.0f; 
                J = 6; R = NowMs();
                return;
            }
            if (ns)                       // 没有小岛等着、但迷你岛开着 → 挂成迷你岛
            {
                nt = true; nv = 255; na = NowMs(); ai = -1; O = 0;
                vv = 1.0f; vv0 = 1.0f; vt = 1.0f;
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
    for (int i = 0; i + 1 < PERIOD_COUNT; i++)
    {
        int eh, em, nh, nm;
        if (!GetPeriodTime(i, ALARM_TYPE_CLASS_END,   &eh, &em)) continue;
        if (!GetPeriodTime(i + 1, ALARM_TYPE_CLASS_START, &nh, &nm)) continue;
        int endMin = eh*60 + em, nextMin = nh*60 + nm;
        if (nextMin <= endMin) continue;                       // 跨天/异常，忽略
        if (i == 4) continue;                                  // 中午那段（第4节下课→午自习）不显示倒计时，用户要求
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
    for (int i = 0; i + 1 < PERIOD_COUNT; i++)
    {
        int eh, em, nh, nm;
        if (!GetPeriodTime(i, ALARM_TYPE_CLASS_END,   &eh, &em)) continue;
        if (!GetPeriodTime(i + 1, ALARM_TYPE_CLASS_START, &nh, &nm)) continue;
        if (i == 4) continue;                                  // 中午那段同样不算
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
    if (!am) PollGesture();
    LONGLONG now = NowMs();

    if (xx && (J == 6 || J == 7) && now - yy >= LONGPRESS_MS)
    {
        xx = false; af = false;                     // 长按关闭
        HideMiniIsland();
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
    if (!(xx || zz || !am)) KillTimer(o, 12);          // 没活儿了就停心跳
}

// 低级鼠标钩子：全局捕获按下（不拦截事件），用于"顶部下滑"手势
LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION)
    {
        MSLLHOOKSTRUCT* m = (MSLLHOOKSTRUCT*)lParam;
        POINT pt = { (int)m->pt.x, (int)m->pt.y };
        if (wParam == WM_LBUTTONDOWN)
        {
            if (IslandHitTest(pt))
            {
                if (!xx) { xx = true; yy = NowMs(); af = true; }
                SetTimer(o, 12, 100, NULL);
            }
            else
            {
                int cxm = B / 2;
                if (J == 0 && pt.y <= 40 && pt.x > cxm - g / 2 && pt.x < cxm + g / 2)
                { zz = true; ag = NowMs(); ah = pt.y; SetTimer(o, 12, 100, NULL); }
            }
        }
        else if (wParam == WM_LBUTTONUP)
        {
            if (xx) { xx = false; af = false; }
            if (zz) zz = false;
        }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
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
        RegCloseKey(hKey);
    }
}

// ===== 音量调节面板（全自绘：圆角轨道 + 圆钮，跟随深浅主题）=====
const int VSL_X0 = 24;      // 轨道左端
const int VSL_X1 = 384;     // 轨道右端
const int VSL_W  = 480;     // 客户区宽
const int VSL_H  = 220;     // 客户区高
int VolSliderY(int which) { return which == 0 ? 108 : 178; }
RECT VolPreviewRect(int which)
{
    int y = VolSliderY(which);
    RECT r = {VSL_X1 + 16, y - 15, VSL_X1 + 60, y + 15};
    return r;
}

void VolDraw(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hWnd, &ps);
    RECT rc; GetClientRect(hWnd, &rc);

    int cBg    = z ? RGB(28,28,32)   : RGB(247,247,250);
    int cTitle = z ? RGB(242,242,247): RGB(26,26,34);
    int cLabel = z ? RGB(196,196,206): RGB(86,86,98);
    int cTrack = z ? RGB(60,60,68)   : RGB(226,226,234);
    int cFill  = z ? RGB(255,201,74) : RGB(226,166,40);
    int cBtnB  = z ? RGB(48,48,56)   : RGB(238,238,244);
    int cBtnT  = z ? RGB(230,230,238): RGB(40,40,50);

    HBRUSH bg = CreateSolidBrush(cBg);
    FillRect(dc, &rc, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    HFONT fTitle = GetCachedFont(20, true);
    HFONT fLabel = GetCachedFont(14, false);
    HFONT fValue = GetCachedFont(15, true);

    SelectObject(dc, fTitle); SetTextColor(dc, cTitle);
    TextOutW(dc, 24, 20, L"音量调节", 4);

    for (int i = 0; i < 2; i++)
    {
        int vol = (i == 0) ? ab : FUCK;
        int y   = VolSliderY(i);
        const wchar_t* name = (i == 0) ? L"上课铃音量" : L"下课铃音量";

        SelectObject(dc, fLabel); SetTextColor(dc, cLabel);
        TextOutW(dc, VSL_X0, y - 42, name, (int)wcslen(name));

        wchar_t vbuf[16]; swprintf_s(vbuf, L"%d%%", vol);
        SelectObject(dc, fValue); SetTextColor(dc, cTitle);
        SIZE vs; GetTextExtentPoint32W(dc, vbuf, (int)wcslen(vbuf), &vs);
        TextOutW(dc, VSL_X1 - vs.cx, y - 43, vbuf, (int)wcslen(vbuf));

        // 轨道
        HBRUSH tb = CreateSolidBrush(cTrack);
        HBRUSH ob = (HBRUSH)SelectObject(dc, tb);
        RoundRect(dc, VSL_X0, y-3, VSL_X1, y+3, 6, 6);
        SelectObject(dc, ob); DeleteObject(tb);

        // 已填充
        int tx = VSL_X0 + (VSL_X1 - VSL_X0) * vol / 100;
        if (tx > VSL_X0)
        {
            HBRUSH fb = CreateSolidBrush(cFill);
            ob = (HBRUSH)SelectObject(dc, fb);
            RoundRect(dc, VSL_X0, y-3, tx, y+3, 6, 6);
            SelectObject(dc, ob); DeleteObject(fb);
        }

        // 圆钮
        int r = 9;
        HBRUSH nb = CreateSolidBrush(RGB(255,255,255));
        HPEN   np = CreatePen(PS_SOLID, 1, z ? RGB(96,96,106) : RGB(198,198,208));
        ob = (HBRUSH)SelectObject(dc, nb);
        SelectObject(dc, np);
        Ellipse(dc, tx-r, y-r, tx+r, y+r);
        SelectObject(dc, ob); SelectObject(dc, np);
        DeleteObject(nb); DeleteObject(np);

        // 试听按钮
        RECT br = VolPreviewRect(i);
        HBRUSH bb = CreateSolidBrush(cBtnB);
        ob = (HBRUSH)SelectObject(dc, bb);
        RoundRect(dc, br.left, br.top, br.right, br.bottom, 12, 12);
        SelectObject(dc, ob); DeleteObject(bb);
        SelectObject(dc, fLabel); SetTextColor(dc, cBtnT);
        SIZE bs; GetTextExtentPoint32W(dc, L"试听", 2, &bs);
        TextOutW(dc, br.left + ((br.right-br.left)-bs.cx)/2,
                     br.top  + ((br.bottom-br.top)-bs.cy)/2, L"试听", 2);
    }

    HFONT fNote = GetCachedFont(12, false);
    SelectObject(dc, fNote); SetTextColor(dc, cLabel);
    TextOutW(dc, 24, VSL_H - 28, L"拖动滑块调节；点「试听」按当前音量放一次", 21);
    EndPaint(hWnd, &ps);
}

// 客户区坐标 → 命中哪个滑块 / 哪个试听按钮（返回 0=上课 1=下课 -1=没有）
int VolHitSlider(POINT p)
{
    for (int i = 0; i < 2; i++)
    {
        int y = VolSliderY(i);
        if (p.x >= VSL_X0 - 16 && p.x <= VSL_X1 + 16 && p.y >= y - 20 && p.y <= y + 20) return i;
    }
    return -1;
}
int VolHitPreview(POINT p)
{
    for (int i = 0; i < 2; i++)
    {
        RECT r = VolPreviewRect(i);
        if (p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom) return i;
    }
    return -1;
}
void VolSetFromX(int which, int x)
{
    int v = (x - VSL_X0) * 100 / (VSL_X1 - VSL_X0);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (which == 0) ab = v; else FUCK = v;
    SaveAudioCfg();
    InvalidateRect(qq, NULL, FALSE);
}

LRESULT CALLBACK VolProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:      VolDraw(hWnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:
    {
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int pv = VolHitPreview(p);
        if (pv >= 0) { AudioPlayAsync(pv == 0 ? ALARM_TYPE_CLASS_START : ALARM_TYPE_CLASS_END); return 0; }
        int sl = VolHitSlider(p);
        if (sl >= 0)
        {
            aj = sl;
            SetCapture(hWnd);
            VolSetFromX(sl, p.x);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (aj >= 0 && (GetKeyState(VK_LBUTTON) & 0x8000))
        {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            VolSetFromX(aj, p.x);
        }
        return 0;
    case WM_LBUTTONUP:
        if (aj >= 0) { aj = -1; if (GetCapture() == hWnd) ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
        aj = -1;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        qq = NULL; aj = -1;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowVolumeWindow(HWND owner)
{
    if (qq) { SetForegroundWindow(qq); return; }   // 已经开着就拉到前面
    static bool reg = false;
    if (!reg)
    {
        reg = true;
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = VolProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"IslandVolWnd";
        wc.hbrBackground = NULL;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
    }
    AudioResolvePath();
    RECT r = {0, 0, VSL_W, VSL_H};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    POINT pt; GetCursorPos(&pt);
    qq = CreateWindowExW(WS_EX_TOOLWINDOW, L"IslandVolWnd", L"音量调节",
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                         pt.x - w / 2, pt.y - h - 24, w, h,
                         owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!qq) return;
    ShowWindow(qq, SW_SHOW);
    UpdateWindow(qq);
    SetForegroundWindow(qq);
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
            for (int i = 0; i < PERIOD_COUNT; i++)
                SetPeriodEnabled(i, (dwData & (1 << i)) != 0);
        }
        RegCloseKey(hKey);
    }
    if (!found)
    {
        DWORD defaultMask = (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10);
        for (int i = 0; i < PERIOD_COUNT; i++)
            SetPeriodEnabled(i, (defaultMask & (1 << i)) != 0);
        SavePeriodEnabled();
    }
}

void SavePeriodEnabled()
{
    DWORD dwData = 0;
    for (int i = 0; i < PERIOD_COUNT; i++)
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
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
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

// ===== 设置窗口：把原先托盘的二级菜单整个搬成图形界面 =====
// 全自绘（跟音量面板同一套画法），控件都按触控留足尺寸；点一下立刻生效并存盘
const int SET_W = 560;
const int SET_H = 750;
HWND stw = NULL;            // 设置窗口
int  sadj = -1;             // 正在拖的设置滑块（0=上课铃 1=下课铃 -1=没在拖）

// 一条条的控件（按钮 / 勾选）：坐标显式写死，改位置一眼就能找到
struct SetCtl { int id; int x1, y1, x2, y2; const wchar_t* text; };
static const SetCtl g_set[] = {
    { ID_TRAY_AUDIO_ON,     28, 336, 268, 368, L"播放提醒音" },
    { ID_TRAY_THEME_LIGHT,  28, 480, 148, 512, L"浅色" },
    { ID_TRAY_THEME_DARK,  154, 480, 274, 512, L"深色" },
    { ID_TRAY_HOLIDAY,     330, 480, 530, 512, L"特殊节日提醒" },
    { ID_MINI_SHOW,         28, 554, 288, 586, L"显示迷你岛（顶边时间）" },
    { ID_TRAY_AUDIO_VOL,   330, 554, 530, 586, L"精细调节音量…" },
    { ID_TRAY_TEST,         28, 624, 178, 658, L"立即测试" },
    { ID_TRAY_NEXT,        190, 624, 340, 658, L"下次提醒时间" },
    { ID_TRAY_STATUS,      352, 624, 502, 658, L"状态信息" },
    { ID_TRAY_AUTOSTART,    28, 668, 178, 702, L"开机自启动" },
    { ID_TRAY_UPDATENOTES, 190, 668, 340, 702, L"更新速递" },
    { ID_TRAY_EXIT,        352, 668, 502, 702, L"退出程序" },
};
// 时段勾选区：2 列 × 6 行
RECT SetPeriodRect(int i)
{
    int col = i % 2, row = i / 2;
    int x = 28 + col * 262, y = 96 + row * 36;
    RECT r = { x, y, x + 246, y + 30 };
    return r;
}
int  SetSliderY(int i)      { return i == 0 ? 386 : 444; }
RECT SetTrackRect(int i)    { RECT r = { 28, SetSliderY(i) - 4, 368, SetSliderY(i) + 4 }; return r; }
RECT SetPreviewRect(int i)  { int y = SetSliderY(i); RECT r = { 386, y - 16, 448, y + 16 }; return r; }

// 勾选状态（哪些控件是"开关"）
bool SetChecked(int id)
{
    if (id == ID_TRAY_AUDIO_ON)    return fuck;
    if (id == ID_TRAY_HOLIDAY)     return hon;
    if (id == ID_TRAY_THEME_LIGHT) return !z;
    if (id == ID_TRAY_THEME_DARK)  return z;
    if (id == ID_MINI_SHOW)        return ns;
    if (id >= ID_PERIOD_BASE)      return PeriodEnabled(id - ID_PERIOD_BASE);
    return false;
}

void SetSetSlider(int which, int x)
{
    // 用和音量面板同一套变量：ab=上课铃、FUCK=下课铃
    RECT t = SetTrackRect(which);
    int v = (x - t.left) * 100 / (t.right - t.left);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (which == 0) ab = v; else FUCK = v;
    SaveAudioCfg();
    InvalidateRect(stw, NULL, FALSE);
}

void SetDraw(HWND hWnd)
{
    PAINTSTRUCT ps; HDC dc = BeginPaint(hWnd, &ps);
    RECT rc; GetClientRect(hWnd, &rc);

    int cBg    = z ? RGB(28,28,32)   : RGB(247,247,250);
    int cTitle = z ? RGB(242,242,247): RGB(26,26,34);
    int cLabel = z ? RGB(176,176,188): RGB(104,104,118);
    int cBox   = z ? RGB(58,58,68)   : RGB(224,224,232);
    int cTrack = z ? RGB(60,60,68)   : RGB(226,226,234);
    int cFill  = z ? RGB(255,201,74) : RGB(226,166,40);
    int cBtnB  = z ? RGB(48,48,56)   : RGB(238,238,244);
    int cBtnT  = z ? RGB(230,230,238): RGB(40,40,50);

    HBRUSH bg = CreateSolidBrush(cBg);
    FillRect(dc, &rc, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    HFONT fTitle = GetCachedFont(22, true);
    HFONT fSec   = GetCachedFont(14, true);
    HFONT fItem  = GetCachedFont(14, false);
    HFONT fSmall = GetCachedFont(12, false);

    SelectObject(dc, fTitle); SetTextColor(dc, cTitle);
    TextOutW(dc, 28, 18, L"mobile 设置", (int)wcslen(L"mobile 设置"));
    SelectObject(dc, fSmall); SetTextColor(dc, cLabel);
    TextOutW(dc, 28, 50, L"改动立即生效，设置会自动保存", (int)wcslen(L"改动立即生效，设置会自动保存"));

    // ---- 区块标题 ----
    SelectObject(dc, fSec); SetTextColor(dc, cTitle);
    TextOutW(dc, 28,  70, L"时间提醒（打钩＝响铃）", (int)wcslen(L"时间提醒（打钩＝响铃）"));
    TextOutW(dc, 28, 312, L"提醒音", (int)wcslen(L"提醒音"));
    TextOutW(dc, 28, 456, L"主题", (int)wcslen(L"主题"));
    TextOutW(dc, 28, 530, L"迷你岛", (int)wcslen(L"迷你岛"));
    TextOutW(dc, 28, 600, L"其他", (int)wcslen(L"其他"));

    // ---- 时段勾选（11 个）----
    for (int i = 0; i < PERIOD_COUNT; i++)
    {
        RECT r = SetPeriodRect(i);
        int by = r.top + 5;
        HBRUSH bb = CreateSolidBrush(PeriodEnabled(i) ? cFill : cBox);
        HBRUSH ob = (HBRUSH)SelectObject(dc, bb);
        RoundRect(dc, r.left, by, r.left + 20, by + 20, 6, 6);
        SelectObject(dc, ob); DeleteObject(bb);
        if (PeriodEnabled(i))
        {
            HPEN pk = CreatePen(PS_SOLID, 3, z ? RGB(28,28,32) : RGB(255,255,255));
            HPEN op = (HPEN)SelectObject(dc, pk);
            MoveToEx(dc, r.left + 5, by + 11, NULL);
            LineTo(dc, r.left + 9, by + 15);
            LineTo(dc, r.left + 16, by + 5);
            SelectObject(dc, op); DeleteObject(pk);
        }
        SelectObject(dc, fItem); SetTextColor(dc, cTitle);
        TextOutW(dc, r.left + 28, r.top + 5, ff[i], (int)wcslen(ff[i]));
    }

    // ---- 两个滑块 + 试听 ----
    for (int i = 0; i < 2; i++)
    {
        int vol = (i == 0) ? ab : FUCK, y = SetSliderY(i);
        RECT t = SetTrackRect(i);
        const wchar_t* nm = (i == 0) ? L"上课铃" : L"下课铃";
        SelectObject(dc, fItem); SetTextColor(dc, cLabel);
        TextOutW(dc, 28, y - 30, nm, (int)wcslen(nm));

        wchar_t vb[16]; swprintf_s(vb, L"%d%%", vol);
        SelectObject(dc, fItem); SetTextColor(dc, cTitle);
        SIZE vs; GetTextExtentPoint32W(dc, vb, (int)wcslen(vb), &vs);
        TextOutW(dc, 455 - vs.cx, y - 30, vb, (int)wcslen(vb));

        HBRUSH tb = CreateSolidBrush(cTrack);
        HBRUSH ob = (HBRUSH)SelectObject(dc, tb);
        RoundRect(dc, t.left, t.top, t.right, t.bottom, 8, 8);
        SelectObject(dc, ob); DeleteObject(tb);
        int tx = t.left + (t.right - t.left) * vol / 100;
        if (tx > t.left)
        {
            HBRUSH fb = CreateSolidBrush(cFill);
            ob = (HBRUSH)SelectObject(dc, fb);
            RoundRect(dc, t.left, t.top, tx, t.bottom, 8, 8);
            SelectObject(dc, ob); DeleteObject(fb);
        }
        HBRUSH nb = CreateSolidBrush(RGB(255,255,255));
        HPEN   np = CreatePen(PS_SOLID, 1, z ? RGB(96,96,106) : RGB(198,198,208));
        ob = (HBRUSH)SelectObject(dc, nb); SelectObject(dc, np);
        Ellipse(dc, tx - 10, y - 10, tx + 10, y + 10);
        SelectObject(dc, ob); SelectObject(dc, np);
        DeleteObject(nb); DeleteObject(np);

        RECT pr = SetPreviewRect(i);
        HBRUSH pb = CreateSolidBrush(cBtnB);
        ob = (HBRUSH)SelectObject(dc, pb);
        RoundRect(dc, pr.left, pr.top, pr.right, pr.bottom, 14, 14);
        SelectObject(dc, ob); DeleteObject(pb);
        SelectObject(dc, fItem); SetTextColor(dc, cBtnT);
        SIZE bs; GetTextExtentPoint32W(dc, L"试听", 2, &bs);
        TextOutW(dc, pr.left + ((pr.right - pr.left) - bs.cx) / 2,
                     pr.top + ((pr.bottom - pr.top) - bs.cy) / 2, L"试听", 2);
    }

    // ---- 按钮 / 勾选 ----
    for (const auto& c : g_set)
    {
        bool isChk = (c.id == ID_TRAY_AUDIO_ON || c.id == ID_TRAY_HOLIDAY ||
                      c.id == ID_TRAY_THEME_LIGHT || c.id == ID_TRAY_THEME_DARK || c.id == ID_MINI_SHOW);
        if (isChk)
        {
            bool on = SetChecked(c.id);
            HBRUSH bb = CreateSolidBrush(on ? cFill : cBox);
            HBRUSH ob = (HBRUSH)SelectObject(dc, bb);
            RoundRect(dc, c.x1, c.y1, c.x1 + 22, c.y1 + 22, 6, 6);
            SelectObject(dc, ob); DeleteObject(bb);
            if (on)
            {
                HPEN pk = CreatePen(PS_SOLID, 3, z ? RGB(28,28,32) : RGB(255,255,255));
                HPEN op = (HPEN)SelectObject(dc, pk);
                MoveToEx(dc, c.x1 + 5, c.y1 + 12, NULL);
                LineTo(dc, c.x1 + 9, c.y1 + 16);
                LineTo(dc, c.x1 + 17, c.y1 + 6);
                SelectObject(dc, op); DeleteObject(pk);
            }
            SelectObject(dc, fItem); SetTextColor(dc, cTitle);
            TextOutW(dc, c.x1 + 30, c.y1 + 2, c.text, (int)wcslen(c.text));
        }
        else
        {
            HBRUSH bb = CreateSolidBrush(cBtnB);
            HBRUSH ob = (HBRUSH)SelectObject(dc, bb);
            RoundRect(dc, c.x1, c.y1, c.x2, c.y2, 12, 12);
            SelectObject(dc, ob); DeleteObject(bb);
            SelectObject(dc, fItem); SetTextColor(dc, cBtnT);
            SIZE bs; GetTextExtentPoint32W(dc, c.text, (int)wcslen(c.text), &bs);
            TextOutW(dc, c.x1 + ((c.x2 - c.x1) - bs.cx) / 2,
                         c.y1 + ((c.y2 - c.y1) - bs.cy) / 2, c.text, (int)wcslen(c.text));
        }
    }
    EndPaint(hWnd, &ps);
}

// 点在哪：>=1000 时段、>=9900 试听、>=9910 滑块、其余就是控件 id
int SetHit(POINT p)
{
    for (int i = 0; i < PERIOD_COUNT; i++)
    {
        RECT r = SetPeriodRect(i);
        if (PtInRect(&r, p)) return ID_PERIOD_BASE + i;
    }
    for (const auto& c : g_set)
    {
        RECT r = { c.x1, c.y1, c.x2, c.y2 };
        if (PtInRect(&r, p)) return c.id;
    }
    for (int i = 0; i < 2; i++)
    {
        RECT r = SetPreviewRect(i);
        if (PtInRect(&r, p)) return 9900 + i;
    }
    for (int i = 0; i < 2; i++)
    {
        RECT t = SetTrackRect(i);
        RECT r = { t.left - 14, t.top - 14, t.right + 14, t.bottom + 14 };
        if (PtInRect(&r, p)) return 9910 + i;
    }
    return -1;
}

LRESULT CALLBACK SetProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:      SetDraw(hWnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN:
    {
        POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int id = SetHit(p);
        if (id >= 9910)      { sadj = id - 9910; SetCapture(hWnd); SetSetSlider(sadj, p.x); }
        else if (id >= 9900)   AudioPlayAsync(id == 9900 ? ALARM_TYPE_CLASS_START : ALARM_TYPE_CLASS_END);
        else if (id >= 0)
        {
            int send = id;
            // "显示迷你岛"这一条是开关：按当前状态决定发"展开"还是"收起"
            if (id == ID_MINI_SHOW) send = ns ? ID_MINI_HIDE : ID_MINI_SHOW;
            SendMessage(o, WM_COMMAND, (WPARAM)send, 0);     // 复用主窗口那套处理，不重写逻辑
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (sadj >= 0 && (GetKeyState(VK_LBUTTON) & 0x8000))
        {
            POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetSetSlider(sadj, p.x);
        }
        return 0;
    case WM_LBUTTONUP:
        if (sadj >= 0) { sadj = -1; if (GetCapture() == hWnd) ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
        sadj = -1;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        stw = NULL; sadj = -1;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void ShowSettingsWindow(HWND owner)
{
    if (stw) { SetForegroundWindow(stw); return; }     // 已经开着就拉到前面
    static bool reg = false;
    if (!reg)
    {
        reg = true;
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = SetProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"MobileSetWnd";
        wc.hbrBackground = NULL;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
    }
    RECT r = {0, 0, SET_W, SET_H};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    stw = CreateWindowExW(WS_EX_TOOLWINDOW, L"MobileSetWnd", L"mobile 设置",
                          WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                          x, 60, w, h, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!stw) return;
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

void ShowMessageIsland(const wchar_t* text)
{
    KillTimer(o,1); KillTimer(o,2); KillTimer(o,3); KillTimer(o,4);
    O=0; P=0;
    hh = true;
    wcscpy_s(gg, text);
    vv = 0.0f; af = false; ww = 0;
    L=true; J=1;

    PrepareBackdropBeforeShow();
    R = NowMs(); // 交主循环 60fps 调度
}

// ===== 节日彩蛋弹窗：只挂在"今天最后一节课下课"（放学）那一刻 =====
// 用户明确要求：不要一开机就弹，也不要"明天就是…"，就在最后一节课下课后弹一次「中秋快乐」。
// 判断"这节课是不是最后一场下课"：今天所有打钩的时段里，再没有比它更晚的下课了。
bool IsLastClassEndOfDay(int curMin)
{
    for (int i = 0; i < PERIOD_COUNT; i++)
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
        DWORD v = 1, sz = sizeof(v);
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
    if (J == 6 || J == 7)                      // 小岛（倒计时）正显示 → 直接变身成迷你岛，不先收再弹
    {
        nt = false; ae = F; an = G; ao = E;
        gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = 0; gJ = 12;
        oo = 0; vv0 = 1.0f; vt = 1.0f;
        J = 5;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    nt = true; nv = 255; na = NowMs();
    F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = 0; D = (B - F) / 2;
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
    bool need = false;
    int nowSec = (int)(NowMs() / 1000);
    if (nowSec != ai) { ai = nowSec; need = true; }
    int target = (NowMs() - na > NOTCH_DIM_MS) ? NOTCH_DIM_A : 255;
    if (nv != target)
    {
        int d = target - nv, step = 10;
        if (d > step) nv += step;
        else if (d < -step) nv -= step;
        else nv = target;
        need = true;
    }
    if (need) UpdateIslandDisplay(o, 1.0f, 255);
}

// 定时触发唯一入口：两条触发链都走这里，本分钟只放行一次。
// 以前各判各的，谁后到谁就再弹一次 —— 那个"弹一次又弹一次"的 fuck bug 就是它
void TriggerAlarmByTimer(AlarmType type)
{
    SYSTEMTIME st; GetLocalTime(&st);
    int curMin = st.wHour * 60 + st.wMinute;
    if (N == curMin) return;      // 本分钟已经弹过了，吞掉
    N = curMin;
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
    // 特殊日期（中秋/国庆，含前一天）：正好是"今天最后一节课下课"（放学）时，
    // 先照常显示「下课了」+ 时间；等它显示完，再让文字淡出、换上「国庆节快乐」
    // （走 J=10 两段式，见 AnimTick）—— 所以这里只登记"待会儿要接祝福"，不打断正常下课流程
    if (type == ALARM_TYPE_CLASS_END && HolidayToday() && !HolidayEggShownToday())
    {
        SYSTEMTIME st; GetLocalTime(&st);
        if (IsLastClassEndOfDay(st.wHour * 60 + st.wMinute))
        {
            MarkHolidayEggShown();
            he = true;
        }
    }
    // 常驻小岛正显示时，大岛要"直接变身放大"，而不是从最小尺寸重新弹一次（免得闪一下）
    bool wasMini = (J == 6 || J == 7 || J == 11 || J == 12);
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
    if (wasMini)
    {
        ae = F; an = G; ao = E;              // 从小岛/迷你岛"当前"尺寸接着长，接缝处不跳变
        gw = g; gh = ISLAND_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 3;
        oo = 0; vv0 = 1.0f; vt = 0.0f; vv = 1.0f; J = 9;
    }
    else
    {
        vv0 = 0.0f; vt = 0.0f; vv = 0.0f;    // 大岛文字从实心开始
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
        if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
        SetCapture(hWnd);
        return 0;
    case WM_LBUTTONUP:
        xx = false; af = false;
        if (GetCapture() == hWnd) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (xx) { xx = false; af = false; }
        return 0;

    // 触控（指针模型）：Windows 会把触摸派发为 WM_POINTER
    case WM_POINTERDOWN:
        if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
        return 0;
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
                am ? L"已装钩子" : L"轮询模式",
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
        case ID_TRAY_AUDIO_VOL:
            ShowVolumeWindow(hWnd);
            break;
        case ID_TRAY_UPDATENOTES: ShowUpdateNotes(); break;
        case ID_TRAY_UNINSTALL: DoUninstall(false); break;
        case ID_TRAY_EXIT: DestroyWindow(hWnd); break;
        default:
        {
            int periodIdx = LOWORD(wParam) - ID_PERIOD_BASE;
            if (periodIdx >= 0 && periodIdx < PERIOD_COUNT)
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

    case WM_DESTROY:
        KillTimer(hWnd,10);
        RemoveTrayIcon(hWnd);
        UnregisterHotKey(hWnd,HOTKEY_ID_DEBUG);
        UnregisterHotKey(hWnd,HOTKEY_ID_SETTINGS);
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
        ShowMessageIsland(L"开机自启动已关闭");
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
    if (ad) { UnhookWindowsHookEx(ad); ad = NULL; }
    if (kk) SetEvent(kk);
    if (mm) WaitForSingleObject(mm, 5000);
    if (ll) CloseHandle(ll);
    if (kk) CloseHandle(kk);
    if (mm) CloseHandle(mm);
    timeEndPeriod(1);
}

void ShowUpdateNotes()
{
    const wchar_t* msg =
        L"mobile v1.0\n\n"
        L"【这是什么】\n"
        L"• 教室用的上下课提醒：到点弹一个「岛」，同时按设定的音量响铃\n"
        L"• 单文件、免安装：双击就自己装好，并常驻在托盘里\n\n"
        L"【三档岛】\n"
        L"• 大岛：上课 / 下课 / 自定义提醒时弹出，显示事件和时间，约 5 秒后收回\n"
        L"• 小岛：课间出现，显示「距上课 mm:ss」倒计时；每个课间都会出现\n"
        L"• 迷你岛：贴在屏幕顶边的刘海小时钟，只显示实时时间；\n"
        L"  静止 10 秒后自动降到约一半透明，平时不挡桌面\n\n"
        L"【提醒音】\n"
        L"• 上课铃、下课铃可分别调音量，都能先试听\n"
        L"• 只有「打了钩」的时段才会响铃；没打钩的时段只显示倒计时、不响铃\n\n"
        L"【特殊节日】\n"
        L"• 中秋、国庆（含节日前一天）自动换节日配色，时间后面缀 ● / ★\n"
        L"• 当天最后一节课下课后：先显示「下课了」+ 时间，再渐渐换成「国庆节快乐」\n"
        L"• 不想要节日皮肤：设置里把「特殊节日提醒」取消打钩即可\n\n"
        L"【设置】\n"
        L"• 托盘图标上右键 →「设置…」：时段勾选、提醒音、主题、迷你岛、开机自启动都在里面\n"
        L"• 改动立即生效并自动保存，不用重启\n\n"
        L"【操作】\n"
        L"• 长按小岛 / 迷你岛：把它关掉\n"
        L"• 从屏幕顶边往下滑：展开迷你岛（触控屏同样支持）\n"
        L"• Ctrl+Alt+A：立刻弹一次大岛（测试用）\n"
        L"• Ctrl+Alt+S：打开设置窗口\n\n"
        L"【卸载】\n"
        L"• 命令行运行 mobile-v1.0.exe --uninstall";

    MessageBox(NULL, msg, L"mobile 更新速递", MB_OK | MB_ICONINFORMATION);
}

void ShowUpdateNotesIfFirstRun()
{
    const wchar_t* szSubKey = L"Software\\IslandAlarm";
    const wchar_t* szValueName = L"UpdateNotesShown_m1.0";

    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, szSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD dwData = 0, dwSize = sizeof(dwData);
        if (RegQueryValueEx(hKey, szValueName, NULL, NULL, (LPBYTE)&dwData, &dwSize) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return;
        }
        RegCloseKey(hKey);
    }

    ShowUpdateNotes();

    if (RegCreateKeyEx(HKEY_CURRENT_USER, szSubKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD dwData = 1;
        RegSetValueEx(hKey, szValueName, 0, REG_DWORD, (LPBYTE)&dwData, sizeof(dwData));
        RegCloseKey(hKey);
    }
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
    T = GetOsBuild();
    timeBeginPeriod(1);
    LoadAlarmTimes();
    LoadPeriodEnabled();
    LoadTheme();
    LoadAudioCfg();
    CreateIslandWindow(hInstance);
    CreateTrayIcon(o);
    RegisterHotKey(o, HOTKEY_ID_DEBUG, MOD_CONTROL|MOD_ALT, 'A');
    RegisterHotKey(o, HOTKEY_ID_SETTINGS, MOD_CONTROL|MOD_ALT, 'S');   // Ctrl+Alt+S = 打开设置
    StartTimerThread();
    SetTimer(o, 10, 1000, NULL);
    ad = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(NULL), 0);
    am = (ad != NULL);
    if (!am) SetTimer(o, 12, 120, NULL);     // 钩子装不上 → 常驻心跳跑轮询兜底

    ShowUpdateNotesIfFirstRun();
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
            if (now >= S)
            {
                if (he)      { he = false; hs = 0; ta = 255; J = 10; }   // 特殊日期 → 接"两段式"，大岛形状不动
                else if (bb) { ae = F; an = G; ao = E; gw = MINI_WIDTH; gh = MINI_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 7;
                               vv0 = vv; vt = 1.0f; oo = 0; J = 5; }               // 有常驻小岛 → 直接变身
                else if (ns) { ae = F; an = G; ao = E; gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = 0; gJ = 12;
                               vv0 = vv; vt = 1.0f; oo = 0; nt = false; J = 5; }   // 迷你岛开着 → 直接缩成迷你岛
                else         { J = 4; P = 0; }               // 否则照常收回
                R = now;
            }
            LONGLONG remain = S - NowMs();
            if (remain < 1) remain = 1;
            if (remain > 60000) remain = 60000;
            wait = (DWORD)remain;
        }
        else if (J == 7)
        {
            MiniHoldTick();          // 常驻小岛：倒计时刷新 / 长按判定 / 提示淡入淡出
            wait = 33;
        }
        else if (J == 12)
        {
            F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = 0; D = (B - F) / 2;
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
