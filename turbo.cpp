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
#include <cstdlib>   // malloc/free：形状覆盖率缓存
#include <algorithm>
#include <shellapi.h>
#include <commctrl.h>   // 滑块控件（音量）
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <shlobj.h>   // SHGetFolderPathW（取我目录/桌面等路径）
#include <winhttp.h>  // 取实时天气（后台线程、超时短、失败静默）
#include <mmdeviceapi.h>  // WASAPI：抓系统输出算音量（MCI 拿不到 PCM，只能回环）
#include <audioclient.h>
#include <gdiplus.h>  // 解码 Home.jpg / Home.png 当专辑封面（GDI+ 比 OleLoadPicturePath 稳）
#include <wincrypt.h>   // DPAPI：API Key 加密存储
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
// 动画速度（2026-11-05）：动画是"每帧走一步"，所以改速度 = 改总步数。
// 1.0x=原步数；2.0x=步数减半（更快）；0.5x=步数加倍（更慢）。
float g_animSpeed = 1.0f;
static inline int AnimSteps(int base)
{
    int cnt = (int)(base / g_animSpeed + 0.5f);
    if (cnt < 8) cnt = 8;
    if (cnt > 400) cnt = 400;
    return cnt;
}
const int ANIMATION_STEPS = 62;          // 弹出动画步数（62 帧；节拍 FRAME_INTERVAL_MS=16ms，
                                         //  实测含消息循环开销约 19.5ms/帧 → 实际约 1.2s）
const int SLIDEOUT_STEPS  = 31;          // 收回动画步数（31 × 16ms ≈ 500ms）
const int FRAME_INTERVAL_MS = 16;        // 动画帧间隔 16ms ≈ 60fps
// 低配模式：弱机器跑 60fps 掉帧时，注册表 LowPerf=1 降到 30fps。
// 实测这台机器 DrawIsland 约 3ms/帧（60fps 预算 16.7ms），完全够；只有弱机才需要它。
static int gFrameMs = 16;
const int DISPLAY_DURATION_MS = 5000;
// ---- 常驻小岛 ----
const int MINI_WIDTH    = 320;   // 常驻小岛宽度
const int MINI_HEIGHT   = 56;    // 常驻小岛高度

// ===== 迷你岛（1.1.13 加）：贴在屏幕顶边的"刘海"，只显示实时时间 =====
// 顶边与屏幕边齐平（上两角不圆角），下两角圆角 —— 就像手机刘海那样挂在屏幕边上。
// 我要求：小岛只管倒计时，实时时间归迷你岛；迷你岛静止一段时间后降到半透明。
const int NOTCH_WIDTH   = 150;   // 迷你岛宽度
const int NOTCH_HEIGHT  = 48;    // 迷你岛高度

// 迷你岛改成"悬浮"样式（2026-10-13 我要求）：不再贴着屏幕顶边，而是像大岛/小岛那样浮在下面，
// 只留 2 个像素的缝 —— 大改动是"四角都圆角"（原来上两角是直角）。
// 好处是和大岛/小岛之间切换时形状不用变，视觉上顺得多；缝只有 2px，不仔细看还是"贴顶"的感觉。
const int NOTCH_Y       = 2;     // 迷你岛距屏幕顶边的缝隙（我要 1~2px）
const int NOTCH_DIM_MS  = 6000;  // 静止多久才降半透明（原来 10 秒，我嫌一直不变透明，缩到 6 秒）
const int NOTCH_DIM_A   = 115;   // 半透明程度（255=不透明；115 ≈ 45%）
const int MINI_STEPS    = 46;    // 小岛弹出 / 小→大变身 的步数（46 帧，实测约 0.9s）
                                 // 注意：大→小变身不用它，用的是 ANIMATION_STEPS（与展开大岛同速率）
const int HINT_GAP      = 30;    // 小岛下方"长按关闭常驻岛"那行字要占的高度

// ===== 音乐播放器样式（放学《回家》在放的时候，大岛变成播放器）=====
// 我要求：只要一个最主要的"播放/暂停"键，不要歌词、不要进度条；
// 版式 = 左边专辑封面(Home.jpg) + 中间歌名 + 右边播放/暂停键；
// 整体是一个"比普通大岛大一圈的长方形"（长宽都加，主要加长）。
const int PLAYER_W     = 640;    // 播放器岛宽（普通大岛是自适应 424 左右）
const int PLAYER_H     = 124;    // 播放器岛高（普通大岛是 80）
const int PLAYER_BTN_D = 60;     // 播放/暂停键直径
const int PLAYER_PAD_R = 26;     // 按钮距离岛右边
const int PLAYER_PAD_L = 22;     // 封面距离岛左边
const int COVER_S      = 88;     // 专辑封面边长（正方形，居中裁切）
// 2026-11-13 用户报"封面四个角都被挖掉一块"：16 的圆角配 88 的边长 = 18%，
// 视觉上不是"圆角"而是"缺角"。降到 8（9%）就只是轻微磨圆。
const int COVER_RAD    = 8;      // 封面圆角半径
const int PLAYER_LONG  = 180000; // 播放器兜底时长（3 分钟）。正常靠 MM_MCINOTIFY 放完就收，这只是防通知丢失的保险
const int PLAYER_PAUSE_CLOSE_MS = 5000; // 按下暂停后超过 5 秒还没继续播 → 自动收掉播放器

// ===== 麦克风 / 摄像头占用提示（2026-10-13 我要求）=====
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
int bigW = 0;
int cw = 0, ch = 0, cnv = 0;


HWND hMain     = NULL;
// ---- 提醒音 ----
wchar_t nn[MAX_PATH] = {0};   // alin.mp3 的绝对路径（空=没找到）
bool    fuck  = true;             // 是否播放（托盘「提醒音」里可关）
bool q = false;

bool z = false;
int B = 0, scrH = 0;

// ===== 一个岛的全部状态（2026-10-24 结构体化，为了"并排显示两个岛"）=====
// 以前这些是一堆散落的全局变量，一个岛就能把它们全占满；
// 现在每个岛一份：主岛 gA 一份（消息岛/多岛并行已于 2026-11-13 删除）。
struct IslandState
{
    // 位置尺寸
    int islandX, islandY, islandW, islandH;
    // 状态机：0空闲 / 1-4大岛 / 5大↔小变身 / 6-8小岛 / 9小→大 / 10节日 / 11-13迷你岛
    int islandMode, P, animStep;
    float I;
    // 时间：R=下一帧，hideAtMs=定格结束
    LONGLONG R, hideAtMs;
    // 文字交叉渐变（0=大岛字，1=小岛字）
    float vv, vv0, vt;
    int ta, ww;
    // 形状因子（1=吸顶刘海形）
    float sf, sf0, sf1;
    // 变身目标 / 起始（从当前实际尺寸接着长，接缝处不跳变）
    int gw, gh, gy, gJ;
    int ae, an, ao;
    int bigH;
    // 标志
    bool bb, pp, hh, nt;
    int oo;
    // 播放器样式
    bool ply, plyMix;
    float plyA;
    // 消息文字（大岛中间那行字）。搬进结构体，两个岛才不会串
    wchar_t gg[128];
    // 天空背景（日出日落 + 天气）。也要按岛分开，否则消息岛拿不到
    bool     skyAlarm;
    bool     skyClear;
    bool     skyReplay;
    int      skyBr;
    LONGLONG skyT0;
    // 迷你岛的"静止变淡"状态。搬进来之后，AI 岛下面挂着的那个原岛才能自己走，
    // 不然它永远停在快照那一刻的不透明度上。
    LONGLONG na;
    int      nv;
};

IslandState gA;   // 主岛：迷你岛 / 小岛 / 大岛 / 播放器 / AI 岛，都是它


// =====================================================================
// 变量字典 —— 读代码先看这段，比猜名字快
// =====================================================================
//
// 【几何】单位都是像素
//   islandX / islandY   岛左上角的【屏幕】坐标。X 由屏幕宽居中算出来，Y 是离屏幕顶多远。
//   islandW / islandH   岛当前的宽高，动画过程中会变，别当常量用。
//   bigW                大岛的"目标宽度"（按文字量出来，424~560 之间）
//   cw / ch / cnv       画布宽 / 画布高 / 画布在屏幕上的左偏移
//   hMain               主窗口句柄；scrH = 屏幕高
//
// 【状态机】islandMode 的取值（13 个都在用，别删也别乱加）
//   0   空闲，什么都不显示
//   1   大岛进场（弹出动画）
//   2   大岛变形
//   3   大岛稳定 —— 最常住的状态，看到 3 就当"正常显示着"
//   4   大岛收回
//   5   大岛 → 迷你岛 变身
//   6   迷你岛进场
//   7   迷你岛稳定
//   8   迷你岛收回
//   9   迷你岛 → 大岛 变身
//   10  节日彩蛋
//   11  迷你岛（刘海形）进场
//   12  迷你岛（刘海形）稳定
//   13  迷你岛（刘海形）收回
//
// 【时间】都在 NowMs() 的毫秒时间轴上
//   hideAtMs   这次显示的截止时刻，到点开始收回
//   animStep   动画进行到第几帧（0 起），总帧数用 AnimSteps() 算
//   R          下一帧的调度时刻
//
// 【颜色】
//   z          true = 深色主题（亮字）/ false = 浅色主题（暗字）
//   notchA     迷你岛整体透明度（5~100）
//
// =====================================================================

// islandMode 的取值 —— 用常量代替数字，读代码不用再翻字典
enum IslandMode
{
    MODE_IDLE        = 0,    // 空闲，什么都不显示
    MODE_BIG_IN      = 1,    // 大岛进场（弹出动画）
    MODE_BIG_MORPH   = 2,    // 大岛变形
    MODE_BIG_HOLD    = 3,    // 大岛稳定 —— 最常住的状态
    MODE_BIG_OUT     = 4,    // 大岛收回
    MODE_TO_MINI     = 5,    // 大岛 → 迷你岛 变身
    MODE_MINI_IN     = 6,    // 迷你岛进场
    MODE_MINI_HOLD   = 7,    // 迷你岛稳定
    MODE_MINI_OUT    = 8,    // 迷你岛收回
    MODE_TO_BIG      = 9,    // 迷你岛 → 大岛 变身
    MODE_HOLIDAY     = 10,   // 节日彩蛋
    MODE_NOTCH_IN    = 11,   // 迷你岛（刘海形）进场
    MODE_NOTCH_HOLD  = 12,   // 迷你岛（刘海形）稳定
    MODE_NOTCH_OUT   = 13,   // 迷你岛（刘海形）收回
};

int islandX = 0, islandY = 0;
int islandW = 0, islandH = 0;
float I = 0.0f;
int islandMode = MODE_IDLE;
bool L = false;
AlarmType M = ALARM_TYPE_SINGLE;
int N = -1;

// ===== 注册表根路径（2026-11-04）=====
// Turbo 是独立系列，配置键必须和 V1.2 分开：
//   V1.2   → Software\IslandAlarm
//   Turbo  → Software\IslandAlarmTurbo
// 所有读写一律走这个宏，别再硬编码字符串。
#define REGKEY L"Software\\IslandAlarmTurbo"

// ===== 自定义文字颜色（4d）=====
// ⚠️ 坑（2026-11-04 卡了两小时）：这块千万不能定义成普通全局数组。
//    一开始把它插在 islandX/islandY/islandW/islandH/I/islandMode/L/M/N/animStep/P/Q 这组单字母全局变量中间，
//    结果每次画岛颜色都被改掉（两个元素变成 4737279 = 调色板里的红色），
//    表现就是"我没设过颜色，字却是红的"。
//    原因：GlobalsToState / StateToGlobals 那套 RAII 是按这块区域整体搬来搬去的。
//    → 改成**堆分配**，彻底离开那片内存。
struct ColCfg
{
    COLORREF c[4];
    bool     set[4];
    bool     on;        // 总开关：默认关 → 一律用主题金色
    ColCfg() { for (int i = 0; i < 4; i++) { c[i] = 0; set[i] = false; } on = false; }
};
static ColCfg& Col()
{
    static ColCfg* p = new ColCfg();   // 只分配一次，永不释放（进程退出时系统回收）
    return *p;
}
static const COLORREF kPal[16] = {
    RGB(255,255,255), RGB(224,224,224), RGB(168,168,168), RGB( 96, 96, 96),
    RGB(  0,  0,  0), RGB(255, 72, 72), RGB(255,150,  0), RGB(255,225, 60),
    RGB(128,220, 96), RGB(  0,200,128), RGB(  0,200,200), RGB( 64,144,255),
    RGB(128, 96,255), RGB(200, 96,255), RGB(255, 96,176), RGB(160,110, 60)
};
int animStep = 0;
int P = 0;
bool Q = false;
LONGLONG R = 0;   // 下一帧时间（QPC 毫秒）
LONGLONG hideAtMs = 0;     // 定格结束时间
DWORD osBuild = 0;

// GDI 缓存
// 别每帧重新 Create，不然跑一会儿句柄就爆了（血泪）
static HDC      U = NULL;
static HBITMAP  V = NULL;
static BYTE*    X = NULL;
static HDC      Y = NULL;
static HBITMAP  Z = NULL;
static BYTE*    aa = NULL;
static int      cc = 0;               // 画布缓存宽度
static int      cch = 0;              // 画布缓存高度（2026-10-24 加回：之前只判宽度，画布加高后 DIB 不重建，UpdateLayeredWindow 读越界直接崩）
static std::map<int, HFONT> ee[2];

// 三套作息表，运行时切换（注册表 TimeTable：0=初四夏季 11 节 / 1=冬季 9 节 / 2=初四冬季 9 节）
const wchar_t* ffA[] = {          // 初四夏季（含早读 / 午自习）
    L"早读", L"第1时间段", L"第2时间段", L"第3时间段", L"第4时间段",
    L"午自习", L"第5时间段", L"第6时间段", L"第7时间段", L"第8时间段", L"第9时间段"
};
const wchar_t* ffB[] = {          // 冬季
    L"第1时间段", L"第2时间段", L"第3时间段", L"第4时间段", L"第5时间段",
    L"第6时间段", L"第7时间段", L"第8时间段", L"第9时间段"
};
// 初四冬季（2026-11-12 我要求加上早读和午休，从 9 节扩成 11 节）
const wchar_t* ffC[] = {
    L"早读", L"第1时间段", L"第2时间段", L"第3时间段", L"第4时间段",
    L"午自习", L"第5时间段", L"第6时间段", L"第7时间段", L"第8时间段", L"第9时间段"
};
const wchar_t** ff = ffA;     // 当前用的名字表
int  pc = 11;                 // 当前时段数（原 PERIOD_COUNT）
int  tbl = 0;                 // 当前作息表：0=初四夏季 / 1=冬季 / 2=初四冬季
void LoadTimeTable();
void SaveTimeTable();

wchar_t gg[128] = {0};
// P2：大岛"原地换字"用 —— 换之前的旧大字、以及这次交叉渐变的起点
static wchar_t  g_prevBig[128] = {0};
static LONGLONG g_swapT0 = 0;
bool hh = false;
POINT ii = {0,0};
std::vector<AlarmTime> jj;

// ===== 自定义提醒（4b，2026-11-04）=====
// 跟 jj 完全分开：jj 是"上课+下课"成对的作息表，这里是我自己加的单点提醒。
// 两种：每天（daily=true）/ 一次性（触发后 used=true 不再响）
struct OneShot
{
    int  hour, minute;
    wchar_t text[32];
    bool daily;      // true = 每天；false = 只响一次
    bool used;       // 一次性提醒用掉了
};
std::vector<OneShot> g_shots;
wchar_t g_customText[32] = L"时间到！";   // SINGLE 类型弹岛时显示的文字

// ===== 一次性倒计时（4e）=====
// 跟课间倒计时共用小岛，但不受"必须在课间"限制 —— 自己记结束时刻 rr，到点自己收。
bool     g_cdManual = false;        // 正在跑手动倒计时

// 2026-11-13 课表三段流程：下课 →「下课了」→ 课表 → 倒计时
// 存"该弹课表的时刻"（NowMs 口径），0 = 没有待弹。
// 放主循环里检查，不能放 MiniHoldTick —— 下课那会儿还是大岛，MiniHoldTick 不跑。
LONGLONG g_schedPopupAt = 0;
wchar_t  g_cdLabel[24] = L"倒计时";  // 小岛上那行字的前缀
int g_lastShotMin = -1;                   // 同一分钟只弹一次

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
// 变身（islandMode= MODE_TO_MINI 缩小 / islandMode= MODE_TO_BIG 放大）的**目标**：这样六种方向（大↔小、大↔迷你、小↔迷你）共用同一套代码
int       gw = MINI_WIDTH, gh = MINI_HEIGHT, gy = ISLAND_FINAL_Y, gJ = 7;
// 大岛当前"应有的高度"：普通闹钟是 80，播放器是 124。
// 弹出/收回/变身/画布尺寸都按它算，这样播放器才能长成一个更高的长方形。
int       bigH = ISLAND_HEIGHT;
float     vv = 0.0f;               // 文字交叉渐变：0=显示大岛文字，1=显示小岛文字
int       ww = 0;                  // 提示文字("长按关闭常驻岛")的透明度 0~255
bool      xx = false;              // 当前有按下（鼠标/触控）
// 2026-11-13 触控专项：触控不会置位 VK_LBUTTON，PollGesture 之前只认鼠标 → 手指永远"没按下"。
// 这里单独记一份触控的按下状态和坐标，PollGesture 一起用。
bool      g_touchDown = false;
POINT     g_touchPt   = { 0, 0 };
// 2026-11-14：按下那一刻的位置。抬起时挪出去超过 12px 就当拖动，不算点击。
POINT     g_tapDownPt = { 0, 0 };
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
bool      gSelfTest = false;        // 「立即测试」跑着的时候为 true（屏蔽真实闹钟）
// 每节课的上课铃/下课铃开关（N3）。默认全开，保持老版本行为。
bool      bellUp[11] = { true,true,true,true,true,true,true,true,true,true,true };
bool      bellDn[11] = { true,true,true,true,true,true,true,true,true,true,true };
// 每节课两个铃各自的音量（0~100），默认 30 跟全局默认一致
int       bellUpVol[11] = { 30,30,30,30,30,30,30,30,30,30,30 };
int       bellDnVol[11] = { 30,30,30,30,30,30,30,30,30,30,30 };
float     plyA = 255.0f;            // 播放器内容的不透明度（"下课了 → 播放器"过渡时 0→255）
bool      plyMix = false;           // 正在做"下课了 → 播放器"的过渡：ta/plyA 交给 islandMode= MODE_TO_BIG 的进度驱动
LONGLONG  plyPauseAt = 0;           // 按下暂停的时刻（用来判"暂停 5 秒还没播就收掉"）
// 麦克风/摄像头提示
// ===== 天空背景用的全局（绘制部分在 DrawIsland 前面，这里只放状态）=====
LONGLONG  skyT0 = 0;                // 本次"日出 → 现在"动画的起点（NowMs）
// 小岛 → 大岛、且我选了"重播"时：先花这段把当前天色/天气**倒着收回去**，
// 再演日出 → 现在。我要求"能看出来是收回去的，别像直接切"，所以给到 1.1 秒。
const int SKY_CLEAR_MS = 1150;
bool     skyClear = false;            // 这次动画前面有没有"收回段"
// 只有"上课了 / 下课了 / 课间倒计时"这三处才跑天空，其它消息一律不跑：
// 由 TriggerAlarm 按闹钟类型置位，ShowMessageIsland 一律清零。
bool      skyAlarm = false;
int       skyBr = 38;               // 天空亮度 0~100（下面 LoadSkyCfg 会把旧默认 38 迁到 100）
// ---- 临时：帧率统计（诊断用，查完删）----
static int      g_fpsCnt = 0;
static LONGLONG g_fpsT0  = 0;
static double   g_fpsVal = 0.0;
LONGLONG  g_rollUntil = 0;          // 时间滚动的结束时刻（期间开 16ms 定时器强制重绘）
// 消息岛顶部那行小字（空=用默认的"上课了/下课了"）
wchar_t   infoCustom[64] = {0};
bool      schedMode = false;        // 课表展开中 → 文字靠左排

// ===== 课表：九年级 13 班（2026-11-12 真实数据）=====
// kSchedule[星期-1][period]：**行是星期、列是节次**
// 列序（0 基）：0 早读 / 1-4 上午四节 / 5 午自习 / 6-8 下午三节 / 9-10 课后服务
// 周六日没课，整行为空。原表说明：英语/外语按原样保留，未统一；"外语/心理"是原表写法。
static const wchar_t* kSchedule[7][12] = {
    { L"数学", L"英语", L"物理", L"语文", L"道德与法治", L"道法", L"数学", L"体育与健康", L"班会", L"数学", L"英语", L""                                     },   // 周一
    { L"英语", L"化学", L"外语", L"语文", L"校本课程", L"数学", L"外语/心理", L"物理", L"道德与法治", L"语文", L"英语", L""                                   },   // 周二
    { L"语文", L"外语", L"数学", L"化学", L"数学", L"英语", L"物理", L"语文", L"语文", L"化学", L"物理", L""                                           },   // 周三
    { L"英语", L"外语", L"语文", L"校本课程", L"物理", L"语文", L"体育", L"音乐", L"化学", L"数学", L"英语", L""                                         },   // 周四
    { L"语文", L"语文", L"劳动", L"数学", L"体育", L"物理", L"数学", L"道德与法治", L"美术", L"物理", L"语文", L""                                        },   // 周五
    { L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""                                                               },   // 周六
    { L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L"", L""                                                               },   // 周日
};

// 查某个星期、某个时段的科目。
// ⚠️ period 是 **0 基**（AlarmItem.period 从 0 开始，11 节 = 0..10），别当成"第几节"。
// 越界或空串都返回 NULL。
static const wchar_t* SchedSubject(int weekDay, int period)
{
    if (weekDay < 1 || weekDay > 7) return NULL;
    if (period  < 0 || period  > 11) return NULL;
    const wchar_t* t = kSchedule[weekDay - 1][period];
    return (t && t[0]) ? t : NULL;
}

bool      g_hud = false;            // 调试 HUD：岛下方显示状态（注册表 DebugHud=1 打开）
// ===== 考试模式（2026-11-16）=====
// 开考之后只留一条会走时间的迷你岛，别的岛一律不许弹；天空切纯色（关掉实时天气）。
// 退出必须从托盘进设置、再输一次二级密码 —— 学生点不到"关闭"。
bool      g_examOn = false;
wchar_t   g_examPwd[32] = L"426426";   // 二级密码，默认 426426，可在自定义页改
HWND      sExam = NULL;                // 作息表页顶部的开关
HWND      sExamPwdEdit = NULL;         // 自定义页的密码框
float     g_speedDbg = 1.0f;        // HUD 里显示的速度（动画速度功能回滚了，先占位）
// 小岛/迷你岛 → 大岛 时，天空动画要"重新播一遍"还是"直接接着变"（注册表 SkyAnimReplay）
// 默认不重播：我说"大岛毫无征兆地把动画抢过来重播一遍"很别扭。从空屏弹出时无所谓，一定会演。
bool     skyReplay = false;
int      notchA = 45;              // 迷你岛静止后的透明度 %（注册表 NotchAlpha，默认 45）
int      skyEn = 1;                // 天空背景总开关（注册表 SkyOn，0=纯色底板）
// 联网取到的**真实**日出/日落（当天分钟数，-1=没有）。SkyToday() 优先用它们，
// 拿不到才退回本地天文算法 —— 实测本地简化公式能差 7~8 分钟（2026-10-18 核对过）。
int      sunRiseMin = -1, sunSetMin = -1, sunDay = 0;
// 迷你岛时间"背后"的实际亮度：太亮就把字改成深色（只改字，不动背景）

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
    if (islandMode == MODE_IDLE || islandW < 2 || islandH < 2) return false;
    // islandX/islandY 是岛在**屏幕坐标**下的左上角（画布里的位置是 islandX-n，见 DrawIsland 的 ox = islandX - n）。
    // 这里以前写成 ptScreen.x - n 再跟 islandX 比 —— 多减了一个窗口偏移 n，
    // 于是整个可点区域被右移了 n 像素（n 最多能到几百），大岛/小岛根本点不到。
    return (ptScreen.x >= islandX && ptScreen.x < islandX + islandW &&
            ptScreen.y >= islandY && ptScreen.y < islandY + islandH);
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
#define ID_TRAY_SKY        1019   // 天空背景开关（老复选框，已被下面两个单选取代）
#define ID_SKY_MODE_SKY    1062   // 底板：天气模式（日出日落 / 天气）
#define ID_SKY_MODE_SOLID  1063   // 底板：纯色模式
#define ID_TBL_OLD         1040
#define ID_TBL_WINTER      1041   // = 冬季作息
#define ID_TBL_W4          1042   // = 初四冬季作息
#define ID_TRAY_AUDIO_ON   1020
#define ID_PERIOD_BASE 2001
#define ID_TIMER_RESHOW_MENU 99
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
bool NextClassStart(int* hh, int* mm, int* period = NULL);
void ToggleAutoStart();
void TriggerAlarm(AlarmType type);
void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha);
void DrawIsland(BYTE* p, int stride, int width, int height, IslandState& IS, bool clearCanvas = true);
void CompositeGlyph(BYTE* p, const BYTE* mask, int stride, int x0, int y0, int x1, int y1, COLORREF color);
bool EnsureBuffers();
void CleanupCachedResources();
void AnimTick(IslandState& IS);
LONGLONG NowMs();
void ShowMessageIsland(const wchar_t* text, int extraW = 0, const wchar_t* small = NULL, bool sched = false);
void ShowScheduleIsland();
void ShowPlayerIsland();                   // 让大岛以"音乐播放器"样式出现并定格
void PlayerTick();                         // 播放器的低频心跳（只负责那行提示淡出）
void ClosePlayer();                        // 停音乐 + 收回播放器岛
bool PlayerButtonHit(POINT ptScreen);      // 命中播放/暂停键 → 切一下
void BgmToggle();                          // 播放 / 暂停
void BgmStop();                            // 停掉并关掉播放器
void LoadPeriodEnabled();
void AudioPlay(AlarmType type, int vol);
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
// 考试模式（2026-11-16）
void ApplyExamMode(bool on);
static void SaveExamCfg();
static void LoadExamCfg();
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
bool ClassInProgress(LONGLONG* startMs, LONGLONG* endMs);
bool NextBreakTarget(LONGLONG* outTargetMs);
bool PeriodEnabled(int i);
void GestureTick();
void PollGesture();
bool EnsureBuffers();
void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha);
void AnimTick(IslandState& IS);

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
    float maxW = (float)bigW;
    float maxBottom = (float)(ISLAND_FINAL_Y + bigH);
    for (int i = 0; i <= 512; i++)
    {
        float t = i / 512.0f;
        float w = ISLAND_MIN_WIDTH  + (bigW - ISLAND_MIN_WIDTH) * springW(t);
        float h = ISLAND_MIN_HEIGHT + (bigH - ISLAND_MIN_HEIGHT) * springH(t);
        float y = ISLAND_FINAL_Y * springY(t);
        if (w > maxW) maxW = w;
        if (y + h > maxBottom) maxBottom = y + h;
    }
    cw = (int)maxW + 8;
    ch = (int)maxBottom + 8;
    if (cw < bigW) cw = bigW;
    if (ch < ISLAND_FINAL_Y + bigH) ch = ISLAND_FINAL_Y + bigH;
    // 常驻小岛 + 它下方那行提示文字也要完整画出来
    int miniBottom = ISLAND_FINAL_Y + MINI_HEIGHT + HINT_GAP;
    if (ch < miniBottom) ch = miniBottom;
    // 大岛下面那行提示（长按关闭…）也要画得下
    if (ch < ISLAND_FINAL_Y + bigH + HINT_GAP) ch = ISLAND_FINAL_Y + bigH + HINT_GAP;
    cnv = (B - cw) / 2;
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
        if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
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
    if (!hon) return 0;                 // 我把"特殊节日提醒"关掉了 → 当作平常日子

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

// 节日文案：我要求就写「中秋快乐」/「国庆快乐」，不要"明天就是…"那套
const wchar_t* HolidayGreet()
{
    int h = HolidayToday();
    return h == 1 ? L"中秋快乐" : (h == 2 ? L"国庆快乐" : L"");
}
const wchar_t* HolidayMark() { return HolidayToday() == 1 ? L" ●" : (HolidayToday() == 2 ? L" ★" : L""); }

// 岛形覆盖率：一个函数画完"普通圆角矩形"到"吸在屏幕边缘的刘海形"的连续过渡
//   sf=0 → 普通弹窗（四角圆角）；sf=1 → 迷你岛（上边齐平 + 顶边两侧向外拐的小圆弧）
//   中间值给变身动画用，形状随 sf 平滑变形，收尾时不会再"咯噔"一下
// 圆角的"连续曲率"指数：2 = 正圆弧，越大越方，2~3 之间最像 iOS 那种圆角。
// 全文件只在这里改，改完岛形/描边/命中区一起变。
static const float SHAPE_N = 2.0f;

static inline float IslandCoverage(int lx, int ly, int w, int h, int rx, int ry, float sf, float* outDist = NULL)
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
            // 2026-11-14：圆角用"连续曲率"（超椭圆）：(|dx|/r)^SHAPE_N + (|dy|/r)^SHAPE_N <= 1。
            //   SHAPE_N = 2   → 普通圆弧（数学上最圆，但直边接圆弧那一点曲率突变，看着"顶"）
            //   SHAPE_N = 4   → iOS 那种方中带圆，但半径给到 h/2 时两端会拉平
            //   SHAPE_N = 2.6 → 现在的取值：曲率连续、轮廓又还很圆
            // 想更圆往 2 调，想更"方"往 4 调，改这一个常量就行。
            float a = (float)dx / (float)ry, b = (float)dy / (float)ry;
            float f = powf(powf(a, SHAPE_N) + powf(b, SHAPE_N), 1.0f / SHAPE_N);
            if (f <= 1.0f) d = 0.0f;
            else { float e = (f - 1.0f) * (float)ry; if (e > d) d = e; }
        }
        if (topR > 1 && ly < topR && (lx < bx0 + topR || lx >= bx1 - topR))
        {
            int dx = (lx < bx0 + topR) ? (bx0 + topR - lx) : (lx - (bx1 - topR));
            int dy = topR - ly;
            float a = (float)dx / (float)topR, b = (float)dy / (float)topR;
            float f = powf(powf(a, SHAPE_N) + powf(b, SHAPE_N), 1.0f / SHAPE_N);
            if (f <= 1.0f) d = 0.0f;
            else { float e = (f - 1.0f) * (float)topR; if (e > d) d = e; }
        }
        // 羽化别开太大：2.5px 时圆角边缘会被"多段直线"近似出来，看着像八边形。
        // 1.0px 足够抗锯齿，又不会把弧线切碎。
        // 1.0 只覆盖一个像素、且那个像素直接被算成 0 → 等于硬边；
        // 2.5 又会把圆弧切成多段直线（八边形感）。1.8 是这两者之间的平衡点。
        // 2026-11-11 我要 5px 描边。之前一直不敢加宽是因为误以为"八边形"是羽化造成的，
        // 后来发现那是迷你岛被 rx=16 强制小圆角（已删）。羽化本身没问题，直接给 5。
        const float FEATHER = 2.5f;   // 2026-11-14 用户嫌 5px 羽化肉眼可见，收到 2.5
        if (outDist)
        {
            if (d > 0.0f) *outDist = d;                  // 岛外
            else
            {
                int e1 = lx, e2 = w - 1 - lx, e3 = ly, e4 = h - 1 - ly;
                int mn = e1 < e2 ? e1 : e2;
                int mn2 = e3 < e4 ? e3 : e4;
                if (mn2 < mn) mn = mn2;
                *outDist = -(float)mn;                    // 岛内：离最近边几像素（负）
            }
        }
        best = (d <= 0.0f) ? 1.0f : (d >= FEATHER ? 0.0f : 1.0f - d/FEATHER);
    }
    if (best >= 1.0f) return 1.0f;
    return best;
}

// 圆角覆盖率，边界 1.5px 羽化
// 2026-11-14：形状覆盖率缓存。
//   同一个岛形一帧里要被问好几遍（底板 / 描边 / 补 alpha / 星星雨点），每次都要开方。
//   按 (w,h,rx,ry,sf) 缓存成 0..255 的表：定格时形状不变，后面几次全是查表。
//   变身过程中 w/h 每帧都变、缓存会一直重建 —— 那也不过是"把本来就要算的那一遍挪了位置"。
static BYTE* g_covTab = NULL;
static int   g_covW = 0, g_covH = 0, g_covRx = 0, g_covRy = 0;
static float g_covSf = -1.0f;

static void CovTableBuild(int w, int h, int rx, int ry, float sfUse)
{
    if (g_covTab && g_covW == w && g_covH == h && g_covRx == rx && g_covRy == ry && g_covSf == sfUse)
        return;
    if (g_covTab) { free(g_covTab); g_covTab = NULL; }
    g_covW = 0;
    if (w <= 0 || h <= 0 || w > 1600 || h > 800) return;      // 尺寸离谱就不缓存
    g_covTab = (BYTE*)malloc((size_t)w * (size_t)h);
    if (!g_covTab) return;
    for (int y = 0; y < h; y++)
    {
        BYTE* row = g_covTab + (size_t)y * w;
        for (int x = 0; x < w; x++)
            row[x] = (BYTE)(IslandCoverage(x, y, w, h, rx, ry, sfUse) * 255.0f + 0.5f);
    }
    g_covW = w; g_covH = h; g_covRx = rx; g_covRy = ry; g_covSf = sfUse;
}

static inline float ShapeCoverage(int lx, int ly, int w, int h, int rx, int ry, float* outDist = NULL)
{
    // 迷你岛尺寸（150×48 上下）一律四角全圆：sf 这个全局量在多次变身之后会残留，
    // 光靠"结束时分身"收不干净，这里按尺寸兜底，最稳。
    float sfUse = (w <= NOTCH_WIDTH + 12 && h <= NOTCH_HEIGHT + 12) ? 0.0f : sf;
    // 要"离边距离"的两处（描边、补 alpha）没法查表，照旧现算
    if (!outDist)
    {
        CovTableBuild(w, h, rx, ry, sfUse);
        if (g_covTab && lx >= 0 && ly >= 0 && lx < w && ly < h)
            return g_covTab[(size_t)ly * w + lx] * (1.0f / 255.0f);
    }
    return IslandCoverage(lx, ly, w, h, rx, ry, sfUse, outDist);
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
    int W = cw, H = ch;
    // 画布尺寸会随岛宽变化（大岛宽度自适应），尺寸一变就必须重建 DIB ——
    // 否则 UpdateLayeredWindow 会读到缓冲区外面，屏幕上就是一块"随岛变大"的黑块
    if ((U && Y) && cc == W && cch == H) return true;
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
    cc = W; cch = H;
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

// 抗锯齿的**实心椭圆**（整圆也用它）。半椭圆由 halfOnly 控制（只画 y ≤ cy 的上半）。
static void FillEllipseAA(BYTE* p, int stride, int W, int H, int cx, int cy,
                          int rx, int ry, COLORREF c, int a, bool halfOnly)
{
    if (rx < 2 || ry < 2 || a <= 0) return;
    int rr = (rx > ry) ? rx : ry;
    int dy0 = halfOnly ? 0 : -ry;
    for (int dy = dy0; dy <= ry; dy++)
    {
        for (int dx = -rx; dx <= rx; dx++)
        {
            double nx = (double)dx / (double)rx;
            double ny = (double)dy / (double)ry;
            double d = sqrt(nx * nx + ny * ny);
            double px = (d - 1.0) * (double)rr;
            double edge = 1.0 - px / 1.2;
            if (edge <= 0.0) continue;
            if (edge > 1.0) edge = 1.0;
            BlendPx(p, stride, W, H, cx + dx, cy - dy, c, (int)(a * edge));
        }
    }
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
// 封面主色（给小球用）：加载封面时算一次。
// 默认暖金 —— 没封面、或者封面太暗/太灰时用它。
COLORREF covMainColor = RGB(232, 196, 120);
COLORREF covAltColor  = RGB(240, 160, 110);

void CoverFree() { if (covPx) { free(covPx); covPx = NULL; } }

bool CoverLoad(const wchar_t* forcePath = NULL)   // NULL=找 exe 同目录的 Home.jpg
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
    if (forcePath && GetFileAttributesW(forcePath) != INVALID_FILE_ATTRIBUTES)
    {
        Gdiplus::Bitmap* t = Gdiplus::Bitmap::FromFile(forcePath, FALSE);
        if (t && t->GetLastStatus() == Gdiplus::Ok) bm = t;
        else if (t) delete t;
    }
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
            Gdiplus::Graphics bigW(&out);
            bigW.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            if (bigW.DrawImage(bm, Gdiplus::Rect(0, 0, COVER_S, COVER_S),
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

                            // ---- 顺便算封面主色（小球用）----
                            // 只统计"不透明、不太黑、不太白"的像素，免得黑边/白底把颜色拉灰
                            long sr = 0, sg = 0, sb = 0, cnt = 0;
                            for (int yy = 0; yy < COVER_S; yy++)
                                for (int xx = 0; xx < COVER_S; xx++)
                                {
                                    const BYTE* q = covPx + ((size_t)yy * COVER_S + xx) * 4;
                                    if (q[3] < 128) continue;
                                    int lum = (q[2] * 299 + q[1] * 587 + q[0] * 114) / 1000;
                                    if (lum < 28 || lum > 232) continue;
                                    sr += q[2]; sg += q[1]; sb += q[0]; cnt++;
                                }
                            if (cnt > 200)
                            {
                                int r = (int)(sr / cnt), bigW = (int)(sg / cnt), b = (int)(sb / cnt);
                                int mx = r > bigW ? (r > b ? r : b) : (bigW > b ? bigW : b);
                                if (mx >= 36)
                                {
                                    double kk = 205.0 / mx;          // 按比例提亮，色相不变
                                    r = (int)(r * kk); bigW = (int)(bigW * kk); b = (int)(b * kk);
                                    if (r > 255) r = 255;
                                    if (bigW > 255) bigW = 255;
                                    if (b > 255) b = 255;
                                    covMainColor = RGB(r, bigW, b);
                                    // 副色：主色反相再提亮 —— 暖棕会变成干净的蓝，对比够但不像"脏灰"
                                    int rr = 255 - r, gg = 255 - bigW, bb = 255 - b;
                                    int mn2 = rr < gg ? (rr < bb ? rr : bb) : (gg < bb ? gg : bb);
                                    if (mn2 < 120)                  // 太暗就整体抬起来
                                    {
                                        int up = 120 - mn2;
                                        rr += up; gg += up; bb += up;
                                        if (rr > 255) rr = 255;
                                        if (gg > 255) gg = 255;
                                        if (bb > 255) bb = 255;
                                    }
                                    covAltColor = RGB(rr, gg, bb);
                                }
                            }
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
    islandW = (int)(ISLAND_MIN_WIDTH  + (tw - ISLAND_MIN_WIDTH ) * springW(t));
    islandH = (int)(ISLAND_MIN_HEIGHT + (th - ISLAND_MIN_HEIGHT) * springH(t));
    islandY = (int)(ISLAND_FINAL_Y * springY(t));
    islandX = (B - islandW) / 2;
    (*step)++;
}

// 让常驻小岛出现（大岛正在显示时不动 —— 大岛优先，等它走完自动变身）
void ShowMiniIsland()
{
    if (g_examOn) return;           // 考试模式：课间小岛/倒计时一律不许弹
    // ★ 小岛只有"课间倒计时"这一种用途。
    //   我 2026-10-27：以前那种"用小岛样式只显示时间"早就该删了 —— 那个形态已经全交给迷你岛。
    //   不在课间就不进这个形态，直接落回迷你岛，免得走出一条"仅时间的小岛"。
    LONGLONG t0 = 0;
    bool inBreak = BreakInfoActive(&t0);
    // 手动倒计时不看课间，结束时刻 rr 由调用方设好
    if (!inBreak && !g_cdManual) { ShowNotch(); return; }
    if (inBreak && !g_cdManual) rr = t0;      // 手动倒计时的 rr 由调用方设好，别覆盖
    pp = true;
    bb = true;
    hh = false;          // 常驻岛不显示消息，否则 tBig=!hh 会把它画成空壳
    skyT0 = NowMs();     // 课间倒计时也是"要跑动画的三处之一"，从头演一遍
    skyClear = false;    // 小岛出现不搞"收回"那套（没有从大岛缩下来的过程）
    if (islandMode == MODE_TO_MINI || islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD) return;
    if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_BIG_OUT || islandMode == MODE_TO_BIG || islandMode == MODE_HOLIDAY) return;   // 大岛优先：等它走完/收完/变身完再接小岛
    if (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD)              // 迷你岛正挂着 → 直接变身成小岛，别先收再弹
    {
        nt = false; ae = islandW; an = islandH; ao = islandY;
        gw = MINI_WIDTH; gh = MINI_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 7;
        oo = 0; vv0 = 1.0f; vt = 1.0f; sf0 = 1.0f; sf1 = 0.0f;
        islandMode = MODE_TO_BIG;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    animStep = 0; vv = 1.0f;
    sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;    // 小岛也是普通圆角，别把迷你岛的形状因子漏进来
    islandMode = MODE_MINI_IN;
    PrepareBackdropBeforeShow();
    R = NowMs();
}

// 收掉常驻小岛（带收回动画）
void HideMiniIsland()
{
    bb = false;
    if (islandMode == MODE_TO_MINI || islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD)
    {
        af = false; ww = 0; xx = false;      // 提示立刻收掉，别在收回动画里残留
        if (ns)                              // 迷你岛开着 → 直接缩成迷你岛，别先收掉再弹出来
        {
            nt = false; ae = islandW; an = islandH; ao = islandY;
            gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
            // ⚠️ pp 必须清掉：不清的话"距上课倒计时"会一路带到迷你岛上
            //（我报的"带着 距上课还剩 0:0 回到迷你岛，再直接弹回去变成仅时间"）。
            // 清了之后 tMini 立刻为真 → 走"时间层"，迷你岛从变身一开始就显示时间。
            pp = false;
            oo = 0; vv0 = 1.0f; vt = 1.0f; sf0 = sf; sf1 = 0.0f;
            islandMode = MODE_TO_MINI;
        }
        else { islandMode = MODE_MINI_OUT; P = 0; }
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
// ===== 播放器音量能量（2026-10-18 加）=====
// MCI 播放拿不到 PCM 样本，所以用 WASAPI Loopback 抓"系统默认输出的回环"算 RMS。
// 局限：会混进别的应用的声音 —— 波纹只在放《回家》时显示，且用较重的平滑掩盖，可接受。
volatile LONG gAmpRaw = 0;        // 0..1000（RMS ×1000），原子写
volatile LONG gBpm    = 0;        // 估出来的 BPM（0=还没数清楚）
volatile LONG gBeat   = 0;        // 节拍脉冲：检测到一次起音就置 1000，主线程读完清零
volatile LONG gAmpRun = 0;        // 1=线程该干活了

// 从一段 32 位浮点/16 位整数交织样本里算 RMS（只取前两声道，够用）
static float RmsOf(const BYTE* data, UINT32 frames, const WAVEFORMATEX* wf)
{
    if (!data || !frames) return 0.0f;
    int nch = wf->nChannels > 2 ? 2 : wf->nChannels;
    if (nch < 1) return 0.0f;
    bool isFloat = (wf->wBitsPerSample == 32);
    double sum = 0; UINT32 cnt = 0;
    for (UINT32 i = 0; i < frames; i++)
    {
        for (int c = 0; c < ch; c++)
        {
            double v;
            if (isFloat)
            {
                const float* f = (const float*)data;
                v = f[i * wf->nChannels + c];
            }
            else
            {
                const short* q = (const short*)data;
                v = q[i * wf->nChannels + c] / 32768.0;
            }
            sum += v * v; cnt++;
        }
    }
    return cnt ? (float)sqrt(sum / cnt) : 0.0f;
}

DWORD WINAPI AmpThreadProc(LPVOID)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = NULL;
    IMMDevice* dev = NULL;
    IAudioClient* ac = NULL;
    IAudioCaptureClient* cc = NULL;
    WAVEFORMATEX* wf = NULL;
    float smooth = 0.0f;
    while (gAmpRun)
    {
        // 没在放（或暂停）就歇着，省电也让画面能落回去
        if (!bgmPlay || bgmPause)
        {
            InterlockedExchange(&gAmpRaw, 0);
            smooth = 0.0f;
            Sleep(120);
            continue;
        }
        if (!cc)
        {   // 每次重新拿一遍默认设备（我可能切了声卡）
            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), (void**)&en)) || !en)
            { Sleep(2000); continue; }
            if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev)
            { en->Release(); en = NULL; Sleep(2000); continue; }
            if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&ac)) || !ac)
            { dev->Release(); dev = NULL; en->Release(); en = NULL; Sleep(2000); continue; }
            if (FAILED(ac->GetMixFormat(&wf)) || !wf)
            { ac->Release(); ac = NULL; dev->Release(); dev = NULL; en->Release(); en = NULL; Sleep(2000); continue; }
            // 回环：REFERENCE_TIME 10000000 = 1 秒缓冲
            if (FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                      10000000, 0, wf, NULL)) ||
                FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc)) || !cc)
            { CoTaskMemFree(wf); wf = NULL; ac->Release(); ac = NULL;
              dev->Release(); dev = NULL; en->Release(); en = NULL; Sleep(2000); continue; }
            ac->Start();
        }
        Sleep(16);
        UINT32 pkt = 0;
        if (FAILED(cc->GetNextPacketSize(&pkt))) { cc->Release(); cc = NULL; ac->Stop();
            ac->Release(); ac = NULL; dev->Release(); dev = NULL; en->Release(); en = NULL; continue; }
        float rms = 0.0f;
        while (pkt > 0)
        {
            BYTE* data = NULL; UINT32 frames = 0; DWORD flags = 0;
            if (FAILED(cc->GetBuffer(&data, &frames, &flags, NULL, NULL))) break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) rms = RmsOf(data, frames, wf);
            cc->ReleaseBuffer(frames);
            if (FAILED(cc->GetNextPacketSize(&pkt))) break;
        }
        // RMS 一般 0.01~0.25，放大到 0..1 够用；削顶交给绘制端
        float v = rms * 5.5f;
        if (v > 1.0f) v = 1.0f;
        smooth = smooth * 0.7f + v * 0.3f;     // 线程内先粗平滑一次，绘制端再细平滑
        InterlockedExchange(&gAmpRaw, (LONG)(smooth * 1000.0f));

        // ---- 起音检测 → BPM ----
        // 没上 FFT：用"能量包络突然抬起"当一次击打。鼓点够用，柔歌大概率数不出来，
        // 那就一直 gBpm=0，绘制端自然退回纯 RMS 呼吸。
        static float envFast = 0.0f, envSlow = 0.0f;
        static LONGLONG lastOnset = 0;
        static double gaps[8] = {0};
        static int gapN = 0, gapI = 0;
        envFast = envFast * 0.55f + smooth * 0.45f;
        envSlow = envSlow * 0.985f + smooth * 0.015f;
        LONGLONG nowm = NowMs();
        if (envFast > envSlow * 1.55f && envFast > 0.05f && (lastOnset == 0 || nowm - lastOnset > 200))
        {
            if (lastOnset)                       // 跳过第一次（没有前一个间隔）
            {
                gaps[gapI] = (double)(nowm - lastOnset);
                gapI = (gapI + 1) % 8;
                if (gapN < 8) gapN++;
            }
            lastOnset = nowm;
            InterlockedExchange(&gBeat, 1000);   // 给绘制端的节拍脉冲
            if (gapN >= 4)
            {
                double tmp[8];
                for (int i2 = 0; i2 < gapN; i2++) tmp[i2] = gaps[i2];
                for (int a2 = 1; a2 < gapN; a2++)    // 小数组插入排序，求中位数
                {
                    double key = tmp[a2]; int b2 = a2 - 1;
                    while (b2 >= 0 && tmp[b2] > key) { tmp[b2 + 1] = tmp[b2]; b2--; }
                    tmp[b2 + 1] = key;
                }
                double med = tmp[gapN / 2];
                if (med > 200.0)
                {
                    int bpm = (int)(60000.0 / med + 0.5);
                    if (bpm >= 50 && bpm <= 200) InterlockedExchange(&gBpm, bpm);
                }
            }
        }
    }
    if (cc) cc->Release();
    if (ac) { ac->Stop(); ac->Release(); }
    if (wf) CoTaskMemFree(wf);
    if (dev) dev->Release();
    if (en) en->Release();
    CoUninitialize();
    return 0;
}
void StartAmp()
{
    if (InterlockedCompareExchange(&gAmpRun, 1, 0) != 0) return;   // 已在跑
    CreateThread(NULL, 0, AmpThreadProc, NULL, 0, NULL);
}

// ===== 天气（2026-10-16 加）=====
// 用 Open-Meteo 的免费接口取当前天气（不要 key），坐标就是学校那一组。
// 联规则（重要）：**后台线程 + 3~4 秒超时 + 失败静默**。拿不到就按"晴"画，
// 绝不阻塞、绝不影响弹岛 —— 学校网络连不出去也不能让程序变得不可用。
// 结果缓存进注册表（WxKind/WxTime），半小时才刷新一次；失败 5 分钟后再试。
// `WxForce` 是调试/手选用的一次性覆盖值（0=自动 1=晴 2=多云 3=阴 4=雾 5=雨 6=雪）。
int      wxKind = 0;                     // 0=晴 1=多云 2=阴 3=雾 4=雨 5=雪
int      wxForce = 0;                    // 调试覆盖：非 0 就固定用这一种，不联网
const int WX_REFRESH_MS = 30 * 60 * 1000;
LONGLONG wxNextTry = 0;
bool     wxStarted = false;

// WMO 天气代码收敛成画得出来的几种
static int WmoToKind(int code)
{
    if (code <= 1) return 0;                                        // 晴 / 大致晴
    if (code == 2) return 1;                                        // 多云
    if (code == 3) return 2;                                        // 阴
    if (code == 45 || code == 48) return 3;                         // 雾
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return 4;  // 毛毛雨~大雨 / 阵雨
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return 5;    // 雪
    if (code >= 95) return 4;                                       // 雷阵雨
    return 0;
}

// 从 JSON 里抠 "sunrise":"...T06:17" 这种值（实现在后面，先声明）
static bool ParseHM(const std::string& body, const char* key, int* out);

// 取一次天气。成功返回 true。整个函数最多耗几秒，且只在后台线程里跑。
static bool WxFetch(int* outKind)
{
    HINTERNET hS = WinHttpOpen(L"mobile-classbell/1.2-turbo", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;
    WinHttpSetTimeouts(hS, 4000, 4000, 4000, 4000);
    bool ok = false;
    HINTERNET hC = WinHttpConnect(hS, L"api.open-meteo.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hC)
    {
        HINTERNET hR = WinHttpOpenRequest(hC, L"GET",
            // 顺手把当天的日出日落一起取回来（必须带 timezone，否则给的是 UTC）
            L"/v1/forecast?latitude=36.81&longitude=118.06&current=weather_code"
            L"&daily=sunrise,sunset&timezone=Asia%2FShanghai&forecast_days=1",
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hR)
        {
            if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0)
                && WinHttpReceiveResponse(hR, NULL))
            {
                DWORD sc = 0, sz = sizeof(sc);
                WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sz, WINHTTP_NO_HEADER_INDEX);
                if (sc == 200)
                {
                    std::string body;
                    DWORD got = 0; char buf[1024];
                    while (WinHttpReadData(hR, buf, sizeof(buf), &got) && got > 0)
                    {
                        body.append(buf, got);
                        if (body.size() > 8192) break;
                    }
                    // ⚠️ 不能直接 find 第一个 "weather_code": ——
                    // API 的 "current_units" 里也有一个 "weather_code":"wmo code"（字符串），
                    // 先命中它就解析不出数字，整个函数判失败（天气/日出日落全拿不到，踩过）。
                    // 这里逐个往后找，取**后面跟数字**的那个。
                    size_t sp = 0, p = std::string::npos;
                    while ((sp = body.find("\"weather_code\":", sp)) != std::string::npos)
                    {
                        size_t q = sp + 15;
                        while (q < body.size() && (body[q] == ' ' || body[q] == '\t')) q++;
                        if (q < body.size() && body[q] >= '0' && body[q] <= '9') { p = q; break; }
                        sp += 15;
                    }
                    if (p != std::string::npos)
                    {
                        int v = 0, cnv = 0;
                        while (p < body.size() && body[p] >= '0' && body[p] <= '9')
                        { v = v * 10 + (body[p] - '0'); p++; cnv++; }
                        if (cnv > 0) { *outKind = WmoToKind(v); ok = true; }
                    }
                    // 日出日落能拿到就顺手更新缓存（拿不全也不影响天气本身）
                    {
                        int r0 = 0, s0 = 0;
                        // 用 "sunrise":[ 定位（daily 数组里是方括号，daily_units 里是字符串 iso8601，不会撞）
                        if (ParseHM(body, "\"sunrise\":[", &r0) && ParseHM(body, "\"sunset\":[", &s0)
                            && s0 > r0 && s0 - r0 > 240)
                        {
                            sunRiseMin = r0; sunSetMin = s0;
                            SYSTEMTIME st2; GetLocalTime(&st2);
                            sunDay = st2.wYear * 10000 + st2.wMonth * 100 + st2.wDay;
                        }
                    }
                }
            }
            WinHttpCloseHandle(hR);
        }
        WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hS);
    return ok;
}

// 从 JSON 里抠出 "sunrise":["2026-10-18T06:17"] 这种值，返回当天分钟数
static bool ParseHM(const std::string& body, const char* key, int* out)
{
    // 逐个往后找：daily_units 里那个 "sunrise":"iso8601" 不是时间，
    // 只有后面跟着 "2026-10-18T06:17" 的才算（看 'T' 之后是否是两位数字）。
    size_t p = 0, tp = std::string::npos;
    while ((p = body.find(key, p)) != std::string::npos)
    {
        size_t t2 = body.find('T', p);
        if (t2 != std::string::npos && t2 + 6 <= body.size()
            && body[t2+1] >= '0' && body[t2+1] <= '9' && body[t2+2] >= '0' && body[t2+2] <= '9'
            && body[t2+4] >= '0' && body[t2+4] <= '9' && body[t2+5] >= '0' && body[t2+5] <= '9')
        { tp = t2; break; }
        p += strlen(key);
    }
    if (tp == std::string::npos) return false;
    char a = body[tp + 1], b = body[tp + 2], c = body[tp + 4], d = body[tp + 5];
    if (a < '0' || a > '9' || b < '0' || b > '9' || c < '0' || c > '9' || d < '0' || d > '9')
        return false;
    int hh = (a - '0') * 10 + (b - '0'), mm = (c - '0') * 10 + (d - '0');
    if (hh > 23 || mm > 59) return false;
    *out = hh * 60 + mm;
    return true;
}

void SaveWxCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = (DWORD)wxKind;
        RegSetValueEx(hKey, L"WxKind", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        DWORD t = (DWORD)(NowMs() / 1000);
        RegSetValueEx(hKey, L"WxTime", 0, REG_DWORD, (LPBYTE)&t, sizeof(t));
        // 日出日落一起缓存（按天），SkyToday() 优先用这个
        if (sunDay && sunRiseMin >= 0 && sunSetMin > sunRiseMin)
        {
            DWORD r2 = (DWORD)sunRiseMin, s2 = (DWORD)sunSetMin, d2 = (DWORD)sunDay;
            RegSetValueEx(hKey, L"SunRise", 0, REG_DWORD, (LPBYTE)&r2, sizeof(r2));
            RegSetValueEx(hKey, L"SunSet",  0, REG_DWORD, (LPBYTE)&s2, sizeof(s2));
            RegSetValueEx(hKey, L"SunDay",  0, REG_DWORD, (LPBYTE)&d2, sizeof(d2));
        }
        RegCloseKey(hKey);
    }
}

void LoadWxCfg()
{
    HKEY hKey;
    DWORD v = 0, sz = sizeof(v), t = 0;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hKey, L"WxKind", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) wxKind = (int)v;
        sz = sizeof(t);
        if (RegQueryValueEx(hKey, L"WxForce", NULL, NULL, (LPBYTE)&t, &sz) == ERROR_SUCCESS) wxForce = (int)t;
        sz = sizeof(t);
        if (RegQueryValueEx(hKey, L"WxTime", NULL, NULL, (LPBYTE)&t, &sz) == ERROR_SUCCESS)
            wxNextTry = (LONGLONG)t * 1000 + WX_REFRESH_MS;
        RegCloseKey(hKey);
    }
    if (wxKind < 0 || wxKind > 5) wxKind = 0;
    if (wxForce < 0 || wxForce > 6) wxForce = 0;
    if (wxForce > 0) wxKind = wxForce - 1;   // 启动就按强制值画，不用等后台线程那一轮
}

// 后台刷新线程：半小时一次；失败 5 分钟后再来。WxForce 非 0 时完全不联网。
void SaveSkyCfg();   // 实现在后面：取到日出日落后顺手存一下

DWORD WINAPI WxThreadProc(LPVOID)
{
    while (true)
    {
        if (g_examOn) { Sleep(3000); continue; }   // 考试模式：不联网、不取天气
        // ⚠️ **不管有没有 WxForce 都要联网**：WxForce 只是手动指定天气种类，
        // 日出日落照样得从网络拿（实测本地简化公式差 7~8 分钟）。
        // 之前写成 force 就直接 continue，结果 force 一开就再也不联网了（踩过）。
        int k = 0;
        if (WxFetch(&k))
        {
            // 2026-11-14 修：以前这里只写了"force 时不覆盖"，却没有任何地方把 force 应用给 wxKind，
            // 于是 WxForce 从头到尾是个死开关（设了也还是真实天气）。现在真的按强制值画。
            wxKind = wxForce ? (wxForce - 1) : k;
            SaveWxCfg();                 // 天气缓存
            SaveSkyCfg();                // 日出日落缓存在 SkyCfg 里（这条链路实测能写，别只依赖 Wx）
            wxNextTry = NowMs() + WX_REFRESH_MS;
        }
        else wxNextTry = NowMs() + 5 * 60 * 1000;
        while (NowMs() < wxNextTry) Sleep(3000);
    }
    return 0;
}
void StartWxThread()
{
    if (wxStarted) return;
    wxStarted = true;
    CreateThread(NULL, 0, WxThreadProc, NULL, 0, NULL);   // 不 Join：失败也不影响主线程
}

// ===== 天空背景（2026-10-16 加）=====
// 弹出大岛时底板不再是纯色，而是一块"天空"：颜色由**太阳高度角**决定（不是按钟点硬切），
// 弹出时再用一段连续动画把"日出 → 现在"演一遍，太阳从左慢慢走到当前所在的位置。
//
// 太阳位置全部本地算：日期 + 经纬度 → 今天的日出/日落/正午高度、任意时刻的太阳高度。
// **故意不联网**：经纬度本来就是我们自己写死的，联网拿同一个坐标去问，结果只会一样准，
// 却要给一个"自安装 + 写开机启动项"的程序再加一条联网特征 —— 那正是安全软件最爱的组合，
// 之前已经为 Defender 误报专门删过一次全局钩子了，不想再往回加。
const double SKY_LAT   = 36.81;      // 纬度：淄博经济开发区实验学校（铁山学校）
const double SKY_LON   = 118.06;     // 经度
// 「日出 → 现在」演一遍固定 2.5 秒 —— 不管太阳要跨多远（清晨一小段、傍晚一整天），
// 到终点都是这个时长，速度是**前快后慢**。
// （先做过 1 秒，我反馈"太快了，一闪就过去，看着像没有动画"，2026-10-16 改成 2.5 秒）
const int    SKY_ANIM_MS = 2500;
const double SKY_PI    = 3.14159265358979323846;


// 一年里的第几天
static int DoyOf(int y, int mo, int d)
{
    static const int acc[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    if (mo < 1) mo = 1; if (mo > 12) mo = 12;
    int doy = acc[mo-1] + d;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (leap && mo > 2) doy++;
    return doy;
}
// 太阳赤纬（度）：一年在 ±23.44 之间来回
static double SunDecl(int doy) { return 23.44 * sin(2.0 * SKY_PI * (doy - 81) / 365.0); }
// 时差（分钟）：真太阳时和平太阳时的差，不修正的话日出能差一刻钟
static double EqTime(int doy)
{
    double b = 2.0 * SKY_PI * (doy - 81) / 364.0;
    return 9.87 * sin(2*b) - 7.53 * cos(b) - 1.5 * sin(b);
}
// 今天的日出/日落（本地钟点，单位分钟）
struct SkyDay { double rise, set, altm; bool ok; };
static SkyDay SkyToday()
{
    SkyDay d = { 0, 0, 0, false };
    SYSTEMTIME st; GetLocalTime(&st);
    int doy = DoyOf(st.wYear, st.wMonth, st.wDay);
    double decDeg = SunDecl(doy);
    d.altm = 90.0 - fabs(SKY_LAT - decDeg);                 // 正午太阳高度角
    double dec = decDeg * SKY_PI / 180.0;
    // **优先用联网取到的真实时间**（当天有效）；没有就退回本地算法。
    // 实测本地简化公式 2026-10-18 算 06:25/17:20，实际 06:17/17:27 —— 差 7~8 分钟，不能一直靠它。
    int ymd = st.wYear * 10000 + st.wMonth * 100 + st.wDay;
    if (sunDay == ymd && sunRiseMin >= 0 && sunSetMin > sunRiseMin + 240)
    {
        d.rise = sunRiseMin; d.set = sunSetMin; d.ok = true;
        return d;
    }
    double cosW = -tan(SKY_LAT * SKY_PI / 180.0) * tan(dec);
    if (cosW >= 1.0 || cosW <= -1.0) return d;              // 极昼/极夜：按"没有日出日落"处理
    double wDeg = acos(cosW) * 180.0 / SKY_PI;              // 时角
    TIME_ZONE_INFORMATION tz; DWORD r = GetTimeZoneInformation(&tz);
    int bias = (int)tz.Bias + (int)(r == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias);
    double tzHours = -(double)bias / 60.0;                  // 东八区 = +8
    // ⚠️ 时差是**减**不是加：EoT>0 表示"日晷比钟快"，那真太阳正午就比 12:00 更早。
    // 写成加号的话日出日落会整体晚 2×EoT（十月下旬能差近半小时 —— 踩过）。
    double noon = 720.0 - 4.0 * (SKY_LON - 15.0 * tzHours) - EqTime(doy);
    d.rise = noon - wDeg * 4.0;
    d.set  = noon + wDeg * 4.0;
    d.ok   = true;
    return d;
}

// 色标表：按太阳高度角（度）插值出 天顶色 / 地平线色 / 霞光色。
// 档位故意给密一点 —— 晨昏那一段（+4° ~ -18°）颜色变得最快，给少了就是"咔哧一下换色"。
struct SkyKey { double alt; int zr,zg,zb, hr,hg,hb, gr,gg,gb; };
static const SkyKey SKY_KEYS[] = {
    {  45.0,   52,108,196,   138,186,232,   255,236,196 },   // 正午
    {  25.0,   68,128,206,   168,206,238,   255,232,190 },
    {  12.0,   92,146,208,   212,206,196,   255,214,164 },
    {   4.0,   96,124,180,   244,192,140,   255,190,120 },   // 金色时刻
    {  -1.0,   80, 92,148,   246,156, 98,   255,150, 80 },   // 日落
    {  -6.0,   58, 64,116,   206,112, 88,   255,104, 62 },   // 民用蒙影
    { -12.0,   34, 38, 78,   112, 78, 98,   180, 70, 74 },   // 航海蒙影
    { -18.0,   16, 20, 44,    50, 50, 80,    70, 50, 74 },   // 天文蒙影
    { -30.0,    8, 10, 26,    22, 26, 46,    30, 30, 50 },   // 深夜
};
static const int SKY_NKEY = (int)(sizeof(SKY_KEYS)/sizeof(SKY_KEYS[0]));

// ⚠️ 三个出参（天顶/地平线/霞光）**每一次都要写满**。之前"超出首尾档"那条分支只抄了天顶色，
// 地平线色留成栈上的垃圾值 —— 正午（alt≥45°）和深夜（alt≤-30°）两头的天空就花了（踩过）。
static void SkyPalette(double alt, int* zc, int* hc, int* gc)
{
    auto put = [](int* dst, int cr, int cg2, int cb) { dst[0] = cr; dst[1] = cg2; dst[2] = cb; };

    if (alt >= SKY_KEYS[0].alt)
    {
        const SkyKey& k = SKY_KEYS[0];
        put(zc, k.zr, k.zg, k.zb); put(hc, k.hr, k.hg, k.hb); put(gc, k.gr, k.gg, k.gb);
        return;
    }
    if (alt <= SKY_KEYS[SKY_NKEY-1].alt)
    {
        const SkyKey& k = SKY_KEYS[SKY_NKEY-1];
        put(zc, k.zr, k.zg, k.zb); put(hc, k.hr, k.hg, k.hb); put(gc, k.gr, k.gg, k.gb);
        return;
    }
    for (int i = 0; i + 1 < SKY_NKEY; i++)
    {
        const SkyKey& a = SKY_KEYS[i];
        const SkyKey& b = SKY_KEYS[i+1];
        if (alt <= a.alt && alt >= b.alt)
        {
            double t = (a.alt - alt) / (a.alt - b.alt);
            auto lerp = [t](int x, int y) { return (int)(x + (y - x) * t + 0.5); };
            put(zc, lerp(a.zr,b.zr), lerp(a.zg,b.zg), lerp(a.zb,b.zb));
            put(hc, lerp(a.hr,b.hr), lerp(a.hg,b.hg), lerp(a.hb,b.hb));
            put(gc, lerp(a.gr,b.gr), lerp(a.gg,b.gg), lerp(a.gb,b.gb));
            return;
        }
    }
    const SkyKey& k = SKY_KEYS[SKY_NKEY/2];
    put(zc, k.zr, k.zg, k.zb); put(hc, k.hr, k.hg, k.hb); put(gc, k.gr, k.gg, k.gb);
}

// 天空这一帧的状态：全部在 SkyBegin 里一次算好，后面逐像素只做查表和几次加法
struct SkyState
{
    bool  on;
    int   w, h;
    int   rowR[260], rowG[260], rowB[260];   // 每一行（按 0..h-1 采样成 SKY_ROWS 行）的渐变底色
    double sx, sy, sr, sglow;                // 太阳中心 / 半径 / 光晕半径
    int   sunR, sunG, sunB, glowR, glowG, glowB;
    int   moonX, moonY, moonR, moonPhase;    // 月亮（<3 = 不画）
    int   moonA;                             // 月亮亮度 0..255 —— 要渐渐亮起来，不能硬蹦出来
    int   starA;                             // 星星透明度 0..255
    int   nStar;                             // 这一帧画几颗星（按岛面积定：迷你岛别铺满）
    short starX[40], starY[40];              // 星星位置（一帧算一次，逐像素只查表）
    int   cx;                                // 天空带子的中心（= 岛的中心，屏幕 x）
    // 天空是**固定**的一块带子：纵向按屏幕 y 铺 80 像素高（大岛那一档），
    // 横向以岛中心为基准铺 640 宽。岛只是这扇窗，形状变、背景不动。
    double veil;                             // 夜幕"从顶上落下来"的进度：0=还没有 1.4=整片都黑了
    int    wx;                               // 天气：0=晴 1=多云 2=阴 3=雾 4=雨 5=雪
    double grayK;                            // 灰化程度（阴天/雾）
    double fogK;                             // 雾的浓度
    double sunVis;                           // 太阳/月亮的可见度（阴雨雾会被挡住）
    bool   rainy;                            // 这一帧有没有"会动的东西"（雨雪/星星）→ 决定要不要持续重绘
    int    nDrop;                            // 雨/雪粒子数
    short  dropX[48], dropY[48], dropL[48];
    double cloudAmp;                         // 云的浓度（夜里几乎没有云）
    double cloudPhase;
    // ---- 收回段（小岛→大岛、且选了重播时才有）----
    // 夜色从下往上被收走：v>wipe 的地方露出"日出色"，v<wipe 保持夜色。
    int    row2R[260], row2G[260], row2B[260];   // 第二套：日出、无天气
    double blendP;                               // 0=夜色 1=日出色（整块渐变，不逐行）
    double decoA;                                // 日/月/星/雨雪的可见度 1=正常 0=全收走
    bool   noSun;                                // 收回段不画太阳（避免"右边沉一下又从左边升起"）
    double sunA;                                 // 太阳亮度 0~1（正常段开头淡入，避免"啪"地出现）
    double glowA;                                // 光晕的"太阳在地平线以上"系数（落山后归零）
    short  starDly[40], starPer[40];             // 每颗星各自的亮起延迟 / 闪烁周期（随手绘制的"凌乱感"）
};
static SkyState sky;
static LONGLONG starSeedT0 = -1;   // 这批星星是哪一次"弹出"掷的；skyT0 一变就重掷

#define SKY_ROWS 240

// 把"太阳高度角"换算成画面里的观感强度：越高越亮，落到地平线以下就只剩霞光
// 天气强度（1=全在 0=收干净）—— SkyBuild 里要用，实现在后面，先声明
static double SkyWxMul();

static void SkyBuild(SkyState& s, int ox, int oy, int w, int h, double alt, double tau)
{
    s.w = w; s.h = h;
    s.cx = ox + w / 2;                       // 天空带子跟着岛的中心走（岛是居中的，所以带子不动）
    int zc[3], hc[3], gc[3];
    SkyPalette(alt, zc, hc, gc);

    // 冷暖偏置：太阳越低，整块越往霞光色靠
    double warm = 1.0 - std::min(1.0, std::max(0.0, (alt - (-8.0)) / 45.0));   // 0=正午 1=入夜
    (void)warm;

    // ---- 天气对配色的影响 ----
    s.wx    = wxKind;
    double wxk = SkyWxMul();          // 1=天气全在，0=收干净（收回段里会从 1 掉到 0）
    s.grayK = ((wxKind == 0) ? 0.00 : (wxKind == 1) ? 0.05 : (wxKind == 2) ? 0.55
            : (wxKind == 3) ? 0.75 : (wxKind == 4) ? 0.60 : (wxKind == 5) ? 0.45 : 0.0) * wxk;
    s.fogK  = ((wxKind == 3) ? 0.90 : (wxKind == 4) ? 0.20 : (wxKind == 5) ? 0.24 : 0.0) * wxk;
    s.sunVis = 1.0 - ((wxKind == 2) ? 0.75 : (wxKind == 3) ? 0.95 :
                      (wxKind == 4) ? 0.80 : (wxKind == 5) ? 0.70 : (wxKind == 1) ? 0.25 : 0.0) * wxk;

    // 行渐变色算一套：天顶→地平线插值 + 霞光带 + 天气灰化/起雾
    auto rows = [](const int* z, const int* hz, const int* bigW, double gray, double fog,
                   int* oR, int* oG, int* oB)
    {
        for (int i = 0; i < SKY_ROWS; i++)
        {
            double v = (double)i / (SKY_ROWS - 1);    // 0=天顶 1=地平线
            double k = pow(v, 0.72);                  // 地平线那一侧色变快一点，更像真天空
            int R = (int)(z[0] + (hz[0] - z[0]) * k);
            int chanG = (int)(z[1] + (hz[1] - z[1]) * k);
            int B = (int)(z[2] + (hz[2] - z[2]) * k);
            // 地平线附近压一条霞光带
            double gl = exp(-(v - 0.97) * (v - 0.97) * 340.0);
            R = (int)(R + (bigW[0] - R) * gl * 0.55);
            chanG = (int)(chanG + (bigW[1] - chanG) * gl * 0.55);
            B = (int)(B + (bigW[2] - B) * gl * 0.55);
            // 阴/雾：往"同亮度的灰"靠；雾再额外往雾灰提亮（雾其实是发白的，越靠地面越厚）
            if (gray > 0.001)
            {
                double lum = 0.299*R + 0.587*chanG + 0.114*B;
                R = (int)(R + (lum - R) * gray);
                chanG = (int)(chanG + (lum - chanG) * gray);
                B = (int)(B + (lum - B) * gray);
            }
            if (fog > 0.001)
            {
                double f = fog * (0.35 + 0.65 * k);
                R = (int)(R + (168 - R) * f);
                chanG = (int)(chanG + (172 - chanG) * f);
                B = (int)(B + (180 - B) * f);
            }
            oR[i] = R; oG[i] = chanG; oB[i] = B;
        }
    };
    rows(zc, hc, gc, s.grayK, s.fogK, s.rowR, s.rowG, s.rowB);
    (void)warm;

    // ---- 收回段：准备一套"日出、无天气"的行色，然后**整块**渐变过去 ----
    s.blendP = 0.0;
    s.decoA  = 1.0;
    s.sunA   = 1.0;
    s.noSun  = false;
    // 迷你岛（islandMode= MODE_NOTCH_IN/12/13）只显示"现在"这一帧，不参与收回。
    bool canFade = skyClear && !(islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT);
    if (canFade)
    {
        LONGLONG elc = NowMs() - skyT0;
        if (elc < 0) elc = 0;
        if (elc < SKY_CLEAR_MS)
        {
            double pp = (double)elc / (double)SKY_CLEAR_MS;
            double e = pp * pp * (3.0 - 2.0 * pp);   // 两头慢、中间快，看着自然
            s.blendP = e;
            // 星/月/天气淡得比底色快：前 1/3 基本就没了，剩下的时间只做颜色过渡，
            // 这样不会出现"星星被一片片擦掉"那种被接走的感觉。
            s.decoA = pow(1.0 - e, 2.2);
            s.noSun = true;                          // 收回段不画太阳
            int z2[3], h2[3], g2[3];
            SkyPalette(0.0, z2, h2, g2);             // alt=0 就是日出那一刻
            rows(z2, h2, g2, 0.0, 0.0, s.row2R, s.row2G, s.row2B);
            // **整块**按 blendP 混合：不是一行一行揭，是整体渐变（上一版逐行推进被我判成渲染 bug）
            for (int i = 0; i < SKY_ROWS; i++)
            {
                s.rowR[i] = (int)(s.rowR[i] + (s.row2R[i] - s.rowR[i]) * s.blendP);
                s.rowG[i] = (int)(s.rowG[i] + (s.row2G[i] - s.rowG[i]) * s.blendP);
                s.rowB[i] = (int)(s.rowB[i] + (s.row2B[i] - s.rowB[i]) * s.blendP);
            }
        }
        else
        {
            // 收回段刚过、正常段开头这 260ms：星月/太阳从"收干净"平滑淡回来。
            // 不做这个的话，decoA 会从 0 直接跳到 1 —— 日出那会儿月亮条件还成立，
            // 于是月亮会"啪"地冒出来（我报的"抽一下"，也是日月挤在一起的原因之一）。
            double fi = (double)(elc - SKY_CLEAR_MS) / 260.0;
            if (fi < 0) fi = 0; if (fi > 1) fi = 1;
            fi = fi * fi * (3.0 - 2.0 * fi);
            s.decoA = fi;
            s.sunA  = fi;
        }
    }

    // 太阳：横向按"白天进度"从左走到右，纵向按高度角
    double phase = std::min(1.0, std::max(0.0, tau));      // 0=日出 0.5=正午 1=日落
    int padX = (int)(w * 0.10);
    s.sx = ox + padX + phase * (w - 2.0 * padX);           // 绝对坐标（岛内横向顺延）
    double hy = oy + h * 0.99;                             // 地平线在岛的底部
    // ⚠️ 这里**不能把下限夹到 0**：夹住的话太阳会一直贴在底边不走，
    // 我要的是"太阳完全落山之后天才黑"，所以得让它真的沉到地平线以下去（会被岛形裁掉）。
    double up = std::min(1.0, alt / 60.0);                  // 60° 以上就算"正当顶"
    s.sy = hy - up * (h * 0.91);
    // 太阳月亮**尺寸固定**（按大岛那一档算，绝不跟岛缩放 —— 一缩就看不见了）
    s.sr = ISLAND_HEIGHT * 0.095;
    s.sglow = ISLAND_HEIGHT * 0.95;
    double warm2 = std::min(1.0, std::max(0.0, 1.0 - alt / 30.0));
    s.sunR = (int)(255 - 8 * warm2);
    s.sunG = (int)(246 - 96 * warm2);
    s.sunB = (int)(206 - 150 * warm2);
    s.glowR = s.sunR; s.glowG = s.sunG; s.glowB = s.sunB;

    // 夜幕：**太阳没完全落山之前一点黑都不加**，落山之后黑才从最上面往下爬。
    // 这是我明确指出的毛病 —— 原来天还有太阳挂着就整片变黑了。
    // veil 是"黑到第几行"（0=天顶 1=地平线），地平线附近最后才黑，正好是黄昏该有的样子。
    s.veil = std::min(1.4, std::max(0.0, -alt / 14.0));

    // 2026-11-14 修"晚上十点岛里还有一层太阳光辉"：
    //   光晕半径是 76px，太阳沉到地平线以下时它的中心虽然出了岛，光晕还能从岛底透进来一层。
    //   所以光晕单独乘一个"高度角系数"：太阳落到 -3° 以下就彻底没有，+5° 以上才全开。
    //   注意**只压光晕**，太阳本体不压 —— 日出那一刻（alt≈0）本体还得能看见半张脸。
    s.glowA = std::min(1.0, std::max(0.0, (alt + 3.0) / 8.0));

    // 月亮：太阳**完全**落山之后才出场（alt<-2），而且要**渐渐亮起来**不能硬蹦
    //（我原话："月亮毫无征兆的就出来了"）；月相按真实朔望周期算（中秋自然就是满月）
    s.moonR = -1;
    s.moonA = 0;
    // ⚠️ 月亮和太阳要**并行**：从 alt +6° 就开始淡入，太阳还在往地平线下沉的时候
    //    月亮已经在另一边亮起来了（真实天空就是这样；我要求"某个时间段之后并行"）。
    //    原来从 -2° 才起步，于是"太阳先落完、月亮再出场"，是两段动画。
    if (alt < 6.0)
    {
        double ma = (6.0 - alt) / 14.0;                 // +6° → -8° 之间逐渐亮起来
        // 再压一段"弹出来之后才慢慢浮出"：比星星晚一些，别跟星星齐刷刷同步（我要求）
        double me = (double)(NowMs() - skyT0 - 320) / 900.0;
        if (me < 0) me = 0; if (me > 1) me = 1;
        ma *= me * me * (3.0 - 2.0 * me);
        s.moonA = (int)(std::min(1.0, std::max(0.0, ma)) * 255.0);
        SYSTEMTIME st; GetLocalTime(&st);
        int d20 = (int)((st.wYear - 2000) * 365.25) + DoyOf(st.wYear, st.wMonth, st.wDay);
        double syn = fmod((double)d20 - 5.0, 29.530588);
        if (syn < 0) syn += 29.530588;
        s.moonPhase = (int)(syn + 0.5);                    // 0..29，14 左右=满月
        s.moonR = (int)(ISLAND_HEIGHT * 0.115);            // 固定尺寸，不跟岛缩
        // 月亮摆在**左侧**（太阳是"早晨左→傍晚右"，日落时它在右半边；
        // 月亮从东边升起来，摆左边才像"太阳这边沉下去、月亮那边升起来"，两者并不同框打架）。
        // 位置按"当前这个岛"算 —— 所以缩成迷你岛之后它会跟着回到可见范围里。
        // 横向位置取太阳的**镜像**：早上太阳从左升 → 月亮在右边落；
        // 傍晚太阳落到右边 → 月亮在左边升。两个永远错开，不会挤在一处（我要求）。
        s.moonX = 2 * ox + w - s.sx;
        // 迷你岛/小岛太窄：太阳升起时俩挤在一处，观感很怪（我明确要求区分对待）。
        // 大岛保留"日月并行"的观感，小尺寸的就不画月亮了。
        // 小尺寸（小岛/迷你岛）只在**太阳已经落山后**才画月亮：
        // 日出/黄昏时太阳也在，两个挤在一处很怪；夜里只有月亮就正常了。
        if ((islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT) && alt > -1.0) s.moonR = -1;
        s.moonY = oy + (int)(h * 0.24);
    }

    // 星星：太阳一沉下去就开始冒（-4° 起步、-11° 全亮），亮度给足 ——
    // 原来从 -7° 起步、最亮才 210，我反馈"星星不见了"（实测那会儿只有 47/255，基本看不见）
    double sa = (-4.0 - alt) / 7.0;
    double dark = std::min(1.0, std::max(0.0, sa));
    // 天气不好就看不见星星（云一挡就没了）
    double wxClear = (wxKind == 0) ? 1.00 : (wxKind == 1) ? 0.70 : (wxKind == 2) ? 0.25
                   : (wxKind == 3) ? 0.10 : (wxKind == 4) ? 0.20 : (wxKind == 5) ? 0.35 : 1.0;
    wxClear = 1.0 - (1.0 - wxClear) * wxk;      // 天气在散，星星就跟着一点点露出来
    s.starA = (int)(dark * 255.0 * wxClear);
    if (wxKind == 3 || wxKind == 4) s.starA = 0;      // 雾和雨看不见星星，一颗都不留
    // 星星**颗数**按岛面积定：大岛(424×80)约 26 颗、迷你岛(150×48)只有 5 颗 ——
    // 我要求"迷你岛不要整个都铺满星星"；同时"越黑星星越多"（前 45% 先出来，暗透了才全出）。
    int base = (w * h) / 1300;
    if (base < 4) base = 4;
    if (base > 34) base = 34;
    s.nStar = (int)(base * (0.45 + 0.55 * dark) + 0.5);
    // 每次弹出重掷一批：位置、亮起延迟、闪烁周期全都随机（我要的"凌乱美"）。
    // 同一次弹出期间固定不变，所以不会满屏乱跳。
    if (starSeedT0 != skyT0)
    {
        starSeedT0 = skyT0;
        unsigned seed = (unsigned)(skyT0 ^ (skyT0 >> 17) ^ (skyT0 >> 33)) | 1u;
        for (int i = 0; i < 40; i++)
        {
            seed = seed * 1103515245u + 12345u;
            unsigned r1 = (seed >> 8) & 0xFFFF;
            seed = seed * 1103515245u + 12345u;
            unsigned r2 = (seed >> 8) & 0xFFFF;
            seed = seed * 1103515245u + 12345u;
            unsigned r3 = (seed >> 8) & 0xFFFF;
            // 2026-11-14：以前是从画布左上角起算、按 640 宽铺的 —— 大岛只有 422 宽，
            // 于是约三分之一的星星落在岛外（看不见），上面 20px 那截还在岛顶之上。
            // 现在锚在"岛自己"的矩形里，40 颗全在岛上。
            s.starX[i]   = (short)(ox + 3 + (int)(r1 / 65535.0 * (w - 6)));
            s.starY[i]   = (short)(oy + 3 + (int)(r2 / 65535.0 * (h - 6)));
            s.starDly[i] = (short)(120 + (r3 % 900));                              // 0.12~1.02 秒之间各自亮起
            s.starPer[i] = (short)(620 + (r1 % 1080));                             // 各自闪烁周期 620~1700ms
        }
    }

    // 云：白天明显一点，太阳一落就淡掉（夜里天空不该有一团团灰）；再按天气加量
    // 2026-11-11 我报"没多云也有多云动画"：晴天原来给了 0.45 的云量，虽然淡但确实在画。
    // 晴天就该一片云都没有。
    double wxCloud = (wxKind == 0) ? 0.00 : (wxKind == 1) ? 1.00 : (wxKind == 2) ? 1.35
                   : (wxKind == 3) ? 0.70 : (wxKind == 4) ? 1.50 : (wxKind == 5) ? 1.10 : 0.55;
    wxCloud = 1.0 + (wxCloud - 1.0) * wxk;      // 云也跟着天气一起散/一起回来
    s.cloudAmp = std::min(1.0, std::max(0.0, (alt + 3.0) / 22.0)) * 0.55 * wxCloud;
    s.cloudPhase = tau * 9.0;                              // 云跟着动画慢慢飘
    s.rainy = false;                                       // 下面按雨雪/星星的情况置位

    // 雨 / 雪粒子：位置由"序号 + 时间"确定（同一帧稳定、跑起来会缓缓下落）。
    // 速度特意给得很慢 —— 岛定格那几秒是一秒才重绘一次，太快会一跳一跳。
    s.nDrop = (int)(((wxKind == 4) ? 34 : (wxKind == 5) ? 26 : 0) * wxk + 0.5);   // 雨雪越收越稀
    if (s.nDrop)
    {
        double spd = (wxKind == 4) ? 0.030 : 0.012;        // 像素 / 毫秒
        double fall = (double)NowMs() * spd;
        for (int i = 0; i < s.nDrop; i++)
        {
            if (i >= 48) break;
            // 同样锚在岛内：以前按 640 宽 / 84 高铺，岛外的雨滴等于白画
            s.dropX[i] = (short)(ox + 2 + (int)(((i * 3571) % 991) / 991.0 * (w - 4)));
            int y0 = (int)(((i * 6151) % 983) / 983.0 * h);
            s.dropY[i] = (short)(oy + (int)fmod((double)y0 + fall, (double)h));
            s.dropL[i] = (short)((wxKind == 4) ? (11 + (i % 5)) : 2);
        }
    }
    // 有没有"会动的东西"：雨雪粒子、或者夜空里的星星（闪烁）。有就要持续重绘。
    // 云、雨雪、星星任一成立就持续重绘（天空动画那一段由调用方的 SkyAnimating() 单独管）
    s.rainy = (s.nDrop > 0) || (s.starA >= 6) || (s.cloudAmp > 0.02);
}

// 一行一行地取渐变底色（v: 0=天顶 1=地平线）
static inline void SkyRowColor(const SkyState& s, double v, int* R, int* chanG, int* B)
{
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    double fi = v * (SKY_ROWS - 1);
    int i0 = (int)fi; if (i0 >= SKY_ROWS - 1) i0 = SKY_ROWS - 2;
    double f = fi - i0;
    *R = (int)(s.rowR[i0] + (s.rowR[i0+1] - s.rowR[i0]) * f);
    *chanG = (int)(s.rowG[i0] + (s.rowG[i0+1] - s.rowG[i0]) * f);
    *B = (int)(s.rowB[i0] + (s.rowB[i0+1] - s.rowB[i0]) * f);
}

// 云带：横向一条条的淡云。按 x 预生成一行，逐像素只查表 —— 不然每像素两次 sin 太费。
// 2026-11-16：这套是 V1.2 的算法，原样搬回来。之前 11-04 我改成"3 朵云 × 4 个椭圆"，
// 结果横向被摊平、纵向被包络框住，看着就是一张纸。V1.2 这种"明暗交替的带子"反而更像云，
// 而且只要一次查表（比 12 次椭圆判定快得多）。
static double skyCloud[4096];
static void SkyBuildClouds(const SkyState& s)
{
    // 云是固定带子：以岛中心为基准的 640 宽，岛变大变小不影响云的形状
    const int NW = 640;
    for (int i = 0; i < NW; i++)
    {
        double t = i * 0.021 + s.cloudPhase;
        skyCloud[i] = sin(t) * 0.62 + sin(t * 0.47 + 1.7) * 0.38;
    }
}

// 逐像素的天空底色（lx,ly 是岛内坐标）
// lx/ly 现在是**画布绝对坐标**（等于屏幕坐标：画布 x 再加窗口偏移 n 才是屏幕 x，
// 但纵向画布的 y 就是屏幕 y）。天空按这个坐标铺，所以岛怎么变，背景都不动。
static inline void SkyPixel(const SkyState& s, int lx, int ly, int* R, int* chanG, int* B)
{
    // 纵向：大岛那一档（ISLAND_FINAL_Y 起 80 像素高）就是整条渐变；岛比它小就只看其中一段
    double v = (double)(ly - ISLAND_FINAL_Y) / (double)ISLAND_HEIGHT;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    SkyRowColor(s, v, R, chanG, B);            // 收回段的渐变已经在 SkyBuild 里整块混好了

    // 夜幕从顶上往下落：越靠上黑得越透，地平线那一带最后才黑（黄昏就长这样）
    if (s.veil > 0.002)
    {
        double d = (s.veil - v) / 0.24;
        if (d > 1) d = 1; else if (d < 0) d = 0;
        d *= std::min(1.0, s.veil);                     // 刚入夜时只加一点黑，别一步到位
        double k = d * 0.90 * s.decoA;                  // 收回段里夜幕跟着一起退（不留一层黑）

        *R = (int)(*R * (1 - k) + 10 * k);
        *chanG = (int)(*chanG * (1 - k) + 12 * k);
        *B = (int)(*B * (1 - k) + 30 * k);
    }

    // 收回段：太阳整个不画（否则它会先在右边沉一下、又从左边升起，像重播了一遍动画）
    if (s.noSun) return;
    // 太阳光晕 + 本体（只在附近才做开方）
    double dx = lx + 0.5 - s.sx, dy = (ly + 0.5) - s.sy;
    double d2 = dx*dx + dy*dy;
    double gr = s.sglow;
    if (d2 < gr * gr)
    {
        double d = sqrt(d2);
        double f = 1.0 - d / gr;
        f = f * f * f * s.sunVis * s.decoA * s.sunA * s.glowA;   // 阴雨雾压暗 / 收回段淡出 / 开头淡入 / 落山后没有
        *R = (int)(*R + (s.glowR - *R) * f * 0.85);
        *chanG = (int)(*chanG + (s.glowG - *chanG) * f * 0.85);
        *B = (int)(*B + (s.glowB - *B) * f * 0.85);
    }
    if (s.sunVis > 0.15 && d2 < (s.sr + 1.6) * (s.sr + 1.6))
    {
        double d = sqrt(d2);
        double a = (s.sr + 1.0 - d) * s.sunVis * s.decoA * s.sunA;
        if (a > 1.0) a = 1.0;
        if (a > 0)
        {
            *R = (int)(*R + (255 - *R) * a);
            *chanG = (int)(*chanG + (245 - *chanG) * a);
            *B = (int)(*B + (205 - *B) * a);
        }
    }


    // 云：横向一条条的淡云（V1.2 的算法）
    //   ⚠️ c 是有正有负的：正的提亮、负的压暗 —— 明暗交替才有"厚度"，
    //   之前我用"椭圆取 max"只加不减，结果就是一层平涂的白 —— 这就是"像纸"的根。
    if (s.cloudAmp > 0.02)
    {
        // 2.3 → 1.8：包络铺得宽一点，云带不那么"细"
        double env = 1.0 - fabs(((double)(ly - ISLAND_FINAL_Y) / ISLAND_HEIGHT) - 0.46) * 1.8;
        if (env > 0)
        {
            int ci = (int)(lx - (s.cx - 320));
            if (ci < 0) ci = 0;
            if (ci > 639) ci = 639;
            double c = skyCloud[ci];
            // V1.2 是 34，但那时底板压暗没现在狠：34 再乘 0.5 的 dim 只剩 ±9，肉眼看不见。
            // 抬到 60，多云档大约 ±33、雨/阴档 ±50，画面上才有"云"可言。
            int d = (int)(c * env * s.cloudAmp * 60.0);
            *R += d; *chanG += d; *B += (int)(d * 0.85);
        }
    }
}

// 月亮：画一个真实的月牙/满月
static void SkyDrawMoon(const SkyState& s, int lx, int ly, int* R, int* chanG, int* B)
{
    if (s.moonR < 3) return;
    double dx = lx + 0.5 - s.moonX, dy = ly + 0.5 - s.moonY;
    double d = sqrt(dx*dx + dy*dy);
    if (d > s.moonR + 1.2) return;
    // 月相：把 0..29 映射成"影子圆"的横向偏移
    double ph = s.moonPhase / 29.530588;         // 0=朔 0.5=望
    double lit = cos(2.0 * SKY_PI * ph);         //  1=满月 -1=新月
    // 太接近上/下弦时影子圆几乎和月圆重合，月牙会细成一根丝（我："月牙怎么这么细"）。
    // 给照明比例一个下限，保证任何时候都看得出是月牙。
    if (lit < 0.50 && lit > -0.50) lit = (lit < 0) ? -0.50 : 0.50;   // 太厚发白、太薄成丝，0.5 左右刚好
    // 影子圆：中心沿 x 偏移，偏移量由月相决定
    double off = lit * (s.moonR * 2.05);
    double sx2 = s.moonX + off, sy2 = s.moonY;
    double d2 = sqrt((lx + 0.5 - sx2)*(lx + 0.5 - sx2) + (ly + 0.5 - sy2)*(ly + 0.5 - sy2));
    bool inMain = (d <= s.moonR);
    bool inShadow = (d2 <= s.moonR * 1.02);
    bool lit2 = inMain && !inShadow;
    if (!lit2) return;
    double a = 1.0;
    if (d > s.moonR - 1.2) a = (s.moonR + 1.0 - d) / 2.2;
    if (a > 1) a = 1; if (a < 0) a = 0;
    a *= s.moonA / 255.0;                      // 渐渐亮起来（刚过地平线那会儿几乎看不见）
    // 雾几乎完全挡住、阴雨挡掉大半
    a *= (wxKind == 3) ? 0.08 : (0.20 + 0.80 * s.sunVis);
    a *= s.decoA;                              // 收回段里月亮也跟着收走
    *R = (int)(*R + (246 - *R) * a * 0.92);
    *chanG = (int)(*chanG + (244 - *chanG) * a * 0.92);
    *B = (int)(*B + (226 - *B) * a * 0.92);
}

// 小东西（星星 / 雨雪）的叠加层：岛底色填完之后，**按点**画上去。
// 原来是"每个像素都遍历一遍星星和雨雪列表"——26+34 次判断 × 8 万像素，
// 一帧白白多花六七毫秒，把弹出动画都拖慢了（冒烟里"8 秒还没收完"就是这么来的）。
// 换成按点画之后开销是几百次运算，可以忽略。
static void SkyOverlay(BYTE* p, int stride, int width, int height,
                       int ox, int oy, int w, int h, int rx, int ry)
{
    auto put = [&](int lx, int ly, int cr, int cg, int cb, int a)
    {
        if (lx < 0 || ly < 0 || lx >= w || ly >= h || a <= 0) return;
        int sx = ox + lx, sy2 = oy + ly;
        if (sx < 0 || sy2 < 0 || sx >= width || sy2 >= height) return;
        float c = ShapeCoverage(lx, ly, w, h, rx, ry);
        if (c <= 0.0f) return;
        int aa = a * (int)(c * 255.0f + 0.5f) / 255;
        if (aa <= 0) return;
        BYTE* q = p + ((size_t)sy2 * stride + sx) * 4;
        q[0] = (BYTE)((cb * aa + q[0] * (255 - aa)) / 255);
        q[1] = (BYTE)((cg * aa + q[1] * (255 - aa)) / 255);
        q[2] = (BYTE)((cr * aa + q[2] * (255 - aa)) / 255);
        q[3] = (BYTE)(aa + q[3] * (255 - aa) / 255);
    };

    // 星星（夜空才画）：每颗按自己的延迟亮起、按自己的周期闪，互不同步
    if (sky.starA >= 6)
    {
        LONGLONG sel = NowMs() - skyT0;
        if (sel < 0) sel = 0;
        for (int i = 0; i < sky.nStar && i < 40; i++)
        {
            double rise = (sel - sky.starDly[i]) / 260.0;      // 自己那一下"亮起来"
            if (rise <= 0) continue;
            if (rise > 1) rise = 1;
            rise = rise * rise * (3.0 - 2.0 * rise);
            double tw = 0.55 + 0.45 * sin(2.0 * SKY_PI * (double)sel / (double)sky.starPer[i] + i * 1.7);
            int a = (int)(sky.starA * rise * tw);
            if (a <= 2) continue;
            // 2026-11-14：原来只有 1px 芯 + 1px 四向，缩在大岛上就是几个灰点。
            // 现在 2×2 芯 + 四向各延 2px 的弱光晕 —— 看得出是星星，又不会大成月亮。
            int sx0 = sky.starX[i], sy0 = sky.starY[i];
            put(sx0,     sy0,     255, 252, 238, a);
            put(sx0 + 1, sy0,     255, 252, 238, a);
            put(sx0,     sy0 + 1, 255, 252, 238, a);
            put(sx0 + 1, sy0 + 1, 255, 252, 238, a * 2 / 3);
            put(sx0 - 1, sy0,     255, 252, 238, a / 4);
            put(sx0 + 2, sy0,     255, 252, 238, a / 4);
            put(sx0,     sy0 - 1, 255, 252, 238, a / 4);
            put(sx0,     sy0 + 2, 255, 252, 238, a / 4);
        }
    }
    // 雨丝 / 雪花
    for (int i = 0; i < sky.nDrop && i < 48; i++)
    {
        if (sky.wx == 4)
        {
            // 2026-11-14：原来是 1px 宽、5~8px 长的线，大岛上几乎看不见。
            // 现在 3px 宽（中间亮两侧淡）+ 11~15px 长，看上去是"雨丝"而不是长方形色块。
            for (int dy = 0; dy <= sky.dropL[i]; dy++)
            {
                int aa = 150 - dy * 4;              // 尾巴渐隐，不成方块
                if (aa < 40) aa = 40;
                put(sky.dropX[i], sky.dropY[i] + dy, 210, 224, 244, aa);
                put(sky.dropX[i] - 1, sky.dropY[i] + dy, 210, 224, 244, aa / 3);
                put(sky.dropX[i] + 1, sky.dropY[i] + dy, 210, 224, 244, aa / 3);
            }
        }
        else
        {
            put(sky.dropX[i], sky.dropY[i], 245, 248, 255, 190);
            put(sky.dropX[i], sky.dropY[i] + 1, 245, 248, 255, 120);
        }
    }
}
// 天空现在**铺到所有地方**（我 2026-10-16 改的要求：天气、时间、日出日落全都用上）。
// 原来只允许 上课了/下课了/倒计时 三处，后来加了迷你岛，再后来干脆全要 —— 所以这里直接恒真。
bool SkyActive()
{
    if (g_examOn) return false;     // 考试模式：强制纯色钢板，不画天空也不画天气
    if (!skyEn) return false;       // 总开关：关掉 → 所有岛回到原来的纯色底板
    return true;
}
// 此刻的"白天进度"：0=日出 0.5=正午 1=日落；负数=天没亮，>1=已经黑了
static double SkyTauNow()
{
    SkyDay d = SkyToday();
    SYSTEMTIME st; GetLocalTime(&st);
    // 2026-11-14：整体再往前挪 5 分钟（2026-10-27 已经单独把日落挪早了 5 分钟）。
    // 也就是"岛上的天色"比真实时间早 5 分钟：日出、日落都跟着提前，
    // 加上下面那句 setEarly，日落的实际偏移是 10 分钟。
    double now = st.wHour * 60 + st.wMinute + st.wSecond / 60.0 - 5.0;
    if (!d.ok || d.set - d.rise < 30.0) return (now < 720.0) ? -0.20 : 1.20;   // 极昼极夜：给个固定档
#ifdef TAU_TEST
    return TAU_TEST;                      // 只用于性能取证：把"白天进度"钉死在某个值
#endif
    // 我 2026-10-27：日落要比实际早 5 分钟 ——
    // 肉眼已经天黑了，岛上还挂着一点小太阳，看着就是没对上。
    double setEarly = d.set - 5.0;
    if (setEarly <= d.rise + 10.0) setEarly = d.set;    // 极短白天的兜底
    return (now - d.rise) / (setEarly - d.rise);
}
// 这一帧该用的进度：弹出后 2.5 秒内从"日出前一点"连续推到"现在"，之后停在现在
// 演一遍要多久 —— **跟着"要跨多远"走**，不是固定值。
// 清晨那一小段（跨 0.15）2.2 秒就够；入夜那一下要跨一整天（跨 1.25），
// 给到 6 秒多，看起来的"变化速度"才差不多。
// （我反馈："到夜间的时候动画太快了，中午之前就差不多"）
static int SkyAnimDur()
{
    double tNow = SkyTauNow();
    double tStart = std::max(0.0, tNow - 1.35);   // 从日出起演（收回段的结尾正好是日出）
    int dur = (int)((tNow - tStart) / 0.20 * 1000.0);
    if (dur < 2200) dur = 2200;
    if (dur > 6500) dur = 6500;
    return dur;
}
// 这次动画总共要跑多久（收回段 + 正常段），SkyAnimating 用它判断"还没演完"
static int SkyAnimTotal()
{
    return (skyClear ? SKY_CLEAR_MS : 0) + SkyAnimDur();
}
// 天气强度 0~1：收回段 1 → 0（雨雾云散掉），正常段随进度 0 → 1 长回来。
// 只在"要重播"的那两次里起作用，平时恒 1。
static double SkyWxMul()
{
    // 2026-11-04：天气必须**常驻**。
    // 原来这里在"收回段"把天气强度从 1 拉到 0、再在展开段拉回 1，
    // 表现就是我报的"迷你岛变大岛时天空突然放晴一下、雨停了"。
    // 天气（雨雪/云/雾）和日出日落是两回事：日照可以重播，天气不该跟着重置。
    // 所以这里恒定返回 1.0 —— 雨一直下，云一直在，跟岛怎么收怎么展无关。
    return 1.0;
}
static double SkyTauAnim()
{
    double tNow = SkyTauNow();
    LONGLONG el = NowMs() - skyT0;
    if (el < 0) el = 0;
    double tStart0 = std::max(0.0, tNow - 1.35);   // 见 SkyAnimDur：起点定在日出
    // 收回段里 tau **保持"现在"**：夜色本身不动，由 SkyBuild 的 wipe 从下往上把它揭走
    //（倒放整天会在中途穿过正午，先爆亮再变暗，完全不是"收回"的观感，已否决）
    if (skyClear && el < SKY_CLEAR_MS) return tNow;
    if (skyClear) el -= SKY_CLEAR_MS;          // 收回段之后才算正常段的进度
    double t = (double)el / (double)SkyAnimDur();
    if (t >= 1.0) return tNow;
    // ⚠️ 缓动从"前快后慢"改成两头慢、中间快（smoothstep）。
    // 原因：收回段刚结束时画面正好是日出，前快后慢的起步太猛 —— 450ms 就从日出橙
    // 跳到白天蓝，看着像硬切（我报的"闪一下"，这是第二处来源）。
    double e = t * t * (3.0 - 2.0 * t);
    // ⚠️ 起点必须和 SkyAnimDur() 里算的**完全一致**（0=日出）。
    // 这里原来还是 -0.10（日出前一点，很黑）：收回段刚结束、画面已经到日出，
    // 下一帧正常段却从 -0.10 开场 → 屏幕闪一下黑（我报的"已经变成日出又突然闪一下"）。
    double tStart = std::max(0.0, tNow - 1.35);
    return tStart + (tNow - tStart) * e;
}
bool SkyAnimating() { return skyEn && (NowMs() - skyT0) < SkyAnimTotal(); }   // 关了就别再 40ms 空刷

static int gClipRight = 0;   // >0 时把文字合成裁到这个 x（长歌名滚动用）


// ===== 全局 <-> IslandState 的搬运 =====
// DrawIsland 内部有大量和全局同名的局部变量（R/islandH/B 颜色、bb 颜色），
// 机械替换会误伤。所以用"进出口搬运"：进入时把 IS 灌进全局，
// 退出时（RAII 析构，含所有提前 return）把改动收回 IS、再把全局还原。
// 效果等价于参数化：每个岛只在自己那次绘制期间占用全局，互不覆盖。
static void StateToGlobals(const IslandState& st)
{
    islandX = st.islandX; islandY = st.islandY; islandW = st.islandW; islandH = st.islandH;
    islandMode = st.islandMode; P = st.P; animStep = st.animStep; I = st.I;
    R = st.R; hideAtMs = st.hideAtMs;
    vv = st.vv; vv0 = st.vv0; vt = st.vt;
    ta = st.ta; ww = st.ww;
    sf = st.sf; sf0 = st.sf0; sf1 = st.sf1;
    gw = st.gw; gh = st.gh; gy = st.gy; gJ = st.gJ;
    ae = st.ae; an = st.an; ao = st.ao;
    bigH = st.bigH;
    bb = st.bb; pp = st.pp; hh = st.hh; nt = st.nt;
    oo = st.oo;
    ply = st.ply; plyMix = st.plyMix; plyA = st.plyA;
    wcscpy_s(gg, st.gg);
    skyAlarm = st.skyAlarm; skyClear = st.skyClear; skyReplay = st.skyReplay;
    skyBr = st.skyBr; skyT0 = st.skyT0;
    na = st.na; nv = st.nv;
}
static void GlobalsToState(IslandState& st)
{
    st.islandX = islandX; st.islandY = islandY; st.islandW = islandW; st.islandH = islandH;
    st.islandMode = islandMode; st.P = P; st.animStep = animStep; st.I = I;
    st.R = R; st.hideAtMs = hideAtMs;
    st.vv = vv; st.vv0 = vv0; st.vt = vt;
    st.ta = ta; st.ww = ww;
    st.sf = sf; st.sf0 = sf0; st.sf1 = sf1;
    st.gw = gw; st.gh = gh; st.gy = gy; st.gJ = gJ;
    st.ae = ae; st.an = an; st.ao = ao;
    st.bigH = bigH;
    st.bb = bb; st.pp = pp; st.hh = hh; st.nt = nt;
    st.oo = oo;
    st.ply = ply; st.plyMix = plyMix; st.plyA = plyA;
    wcscpy_s(st.gg, gg);
    st.skyAlarm = skyAlarm; st.skyClear = skyClear; st.skyReplay = skyReplay;
    st.skyBr = skyBr; st.skyT0 = skyT0;
    st.na = na; st.nv = nv;
}

// 只载入 + 收回，不还原 —— 给 AnimTick 用。
// AnimTick 这一帧就在改全局，如果析构时再还原成"进入前"，这一帧的改动就丢了，
// 表现是动画卡住 / 岛画成一个点。
struct ScopedLoadOnly
{
    IslandState* mine;
    explicit ScopedLoadOnly(IslandState& is) : mine(&is) { StateToGlobals(is); }
    ~ScopedLoadOnly() { GlobalsToState(*mine); }
};

// 作用域内"这个岛的全局上下文"：构造时载入，析构时收回并还原
struct ScopedIslandState
{
    IslandState  bak;
    IslandState* mine;
    explicit ScopedIslandState(IslandState& is) : mine(&is)
    {
        GlobalsToState(bak);
        StateToGlobals(is);
    }
    ~ScopedIslandState()
    {
        GlobalsToState(*mine);
        StateToGlobals(bak);
    }
};


void DrawIsland(BYTE* p, int stride, int width, int height, IslandState& IS, bool clearCanvas)
{
    ScopedIslandState scoped(IS);
    g_fpsCnt++;                                  // 临时：数帧   // 载入本岛状态；函数结束（含提前 return）自动收回+还原
        // width/height = 画布尺寸；岛形只是画布里会动的一块
    // clearCanvas=false 时不清空 —— 同一个画布要画第二个岛时用（例如 AI 岛 + 消息岛）
    if (clearCanvas)
        for (int y=0; y<height; y++)
            memset(p + (size_t)y*stride*4, 0, (size_t)width*4);

    int ox = islandX - cnv;   // 岛在画布内的位置（画布 = 窗口，固定在 n,0）
    int oy = islandY;
    int w = islandW, h = islandH;
    if (w < 2 || h < 2) return;
    // 2026-11-11 我要"iPhone 灵动岛那种两端半圆"：
    // 半径取高的一半（h/2）就是正圆端；上限再夹到 w/2 防止矮胖时溢出。
    // 原来是 min(28, h/2)/2 → 半径最多 14px，看着只是个普通圆角，不是胶囊。
    // 2026-11-13 修：这里原来叫 rr，把全局的"倒计时目标时刻 rr"遮蔽了。
    // 结果倒计时算成 半径(14) - 系统运行毫秒(81万) = 巨大负数 → 钳到 0 → 每节课都 00:00，而且数字不刷新。
    int rad = std::min(h / 2, w / 2);
    if (rad < 2) rad = 2;
    int rx = rad, ry = rad;
    // 迷你岛一律 16×16 圆角。
    // 之前用尺寸判断（w<=162 && h<=60）—— 但"变身回来"那条路上尺寸是逐帧插值的，
    // 中间可能落在边界外；而且退出变身那一帧的 w/h 未必正好是 150/48。
    // 改成用**状态**判断：只要现在是迷你岛（11/12/13）或者正变向迷你岛（islandMode= MODE_TO_MINI 且 gJ=12），就强制圆角。
    bool notchState = (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT) || (islandMode == MODE_TO_MINI && gJ == 12);
    // 2026-11-11：这里原来强制 rx=ry=16，把迷你岛压成小圆角（八边形感）。
    // 现在统一走上面的胶囊半径（h/2），两端就是标准半圆。
    (void)0;

    // 1. 底板：平时是纯色（节日换节日色）；**大岛弹出时改成"天空"** —— 颜色由太阳高度角决定，
    //    弹出头 3 秒把"日出 → 现在"连续演一遍（2026-10-16 我要求）。
    bool skyOn = SkyActive();
    if (skyOn)
    {
        SkyDay day = SkyToday();
        // 迷你岛只是"要一块背景"，**不演动画**（我只允许 上课了/下课了/倒计时 三处演动画）：
        // 直接取"现在"那一帧，不参与"日出 → 现在"的扫过。
        double tau = (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT) ? SkyTauNow() : SkyTauAnim();
        double alt = day.ok ? (day.altm * sin(SKY_PI * tau)) : 12.0;   // tau<0 / >1 时自然是负的
        SkyBuild(sky, ox, oy, w, h, alt, tau);
        SkyBuildClouds(sky);
        sky.on = true;
    }
    else sky.on = false;

    int br, bg, bb;
    int hol = HolidayToday();
    if (hol == 1)      { if (z) { br = 66; bg = 47; bb = 12; } else { br = 251; bg = 232; bb = 186; } }  // 中秋·浓暖金
    else if (hol == 2) { if (z) { br = 92; bg = 16; bb = 14; } else { br = 255; bg = 226; bb = 220; } }  // 国庆·浓中国红
    else if (z)        { br = 34;  bg = 34;  bb = 38;  }   // 黑灰
    else               { br = 246; bg = 246; bb = 248; }   // 浅白
    // ⚠️ 只扫"岛矩形"这一块（外扩 1px 给抗锯齿留余量）。
    // 以前这里是 for y in 0..height / x in 0..width —— 整块画布逐像素算圆角覆盖率，
    // 640×600 就是 38 万次 ShapeCoverage；而岛本身只占 424×80（3.4 万）。
    // 岛外面的像素算完还是 c<=0 直接 continue，纯属白烧 CPU。
    // 实测：3 条消息岛同时挂着时这个循环吃掉 18ms/帧，改完掉到 2~3ms。
    int gy0 = oy - 1; if (gy0 < 0) gy0 = 0;
    int gy1 = oy + h + 1; if (gy1 > height) gy1 = height;
    int gx0 = ox - 1; if (gx0 < 0) gx0 = 0;
    int gx1 = ox + w + 1; if (gx1 > width) gx1 = width;
    for (int y = gy0; y < gy1; y++)
    {
        int ly = y - oy;
        BYTE* row = p + (size_t)y*stride*4;
        for (int x = gx0; x < gx1; x++)
        {
            float dOut = 0.0f;
            float c = ShapeCoverage(x - ox, ly, w, h, rx, ry, &dOut);
            if (c <= 0.0f) continue;
            int pa = (int)(c * 255.0f + 0.5f);
            // 2026-11-11 修"四边一圈黑线"：
            // 边缘 1.5px 羽化会产生 pa 只有 1~20 的像素，而预乘写成 (B*pa)/255
            // 是整型截断 —— B=44、pa=2 时 88/255 = 0，颜色直接变成纯黑，
            // 一圈几十个这样的像素连起来就是我看到的黑边。
            // 两招：① pa 太小就整个跳过（这种像素肉眼本来也看不见）
            //      ② 剩下的用四舍五入而不是截断
            if (pa < 24) continue;
            int idx = x*4;
            int R, chanG, B;
            // 迷你岛静止后"背景变淡"的标记（只作用在背景上，时间那行字另画、不受影响）
            bool notchFade = (sky.on && nt && nv < 255);
            if (sky.on)
            {
                int lx = x - ox;
                SkyPixel(sky, x, y, &R, &chanG, &B);        // 传画布绝对坐标 → 背景不跟岛缩放
                // 整体压暗 + 下半部分再暗一档：我要的是"有背景感、别抢视野、以黑为底"，
                // 同时保证金字和阴影始终读得清。
                double v = (double)ly / (double)(h > 1 ? h - 1 : 1);
                // 下半部分压一档（给时间那行字让路）；顶部也压一点点 ——
                // 顶上那行小字（"上课了"）颜色本来就淡，天一亮就糊在背景里了（看过截图才发现）。
                double dim = (0.25 + 0.70 * skyBr / 100.0)      // 我可调的"天空亮度"
                                  * (1.0 - 0.34 * std::max(0.0, (v - 0.30) / 0.70))
                                  * (1.0 - 0.26 * std::max(0.0, (0.30 - v) / 0.30));
                // 迷你岛静止后：背景的**颜色和不透明度**一起乘 nv（预乘 alpha 必须同乘），
                // 这才是"透出去"；只乘颜色就是"变暗"（踩过）。
                if (notchFade) dim *= (double)nv / 255.0;
                // 2026-11-12 修：这里原本应该是 G（绿色通道），被批量改名错改成了 islandH（岛高）。
// 后果有两个：① 绿色没被压暗 → 整个岛发绿；② 全局岛高被反复乘 dim → 越变越矮。
R = (int)(R * dim); chanG = (int)(chanG * dim); B = (int)(B * dim);
                // 月亮和星星放在压暗之后 —— 它们本来就该是亮的
                SkyDrawMoon(sky, x, y, &R, &chanG, &B);
                // 雨雪天的边缘质感（2026-11-05）：岛内侧一圈淡雾/霜白。
                // 岛是圆角矩形，这里用"到矩形边的距离"近似（圆角处会略糙，但视觉够用）
                if (wxKind == 4 || wxKind == 5)
                {
                    int dx0 = lx < (w - 1 - lx) ? lx : (w - 1 - lx);
                    int dy0 = ly < (h - 1 - ly) ? ly : (h - 1 - ly);
                    int dd  = dx0 < dy0 ? dx0 : dy0;
                    if (dd < 8 && dd >= 0)
                    {
                        double f = 1.0 - dd / 8.0;      // 0=最靠边 1=雾带内沿
                        double k = f * f * 0.55;        // 越靠边越明显
                        if (wxKind == 4)                // 雨：淡蓝灰水汽
                        {
                            R = (int)(R * (1 - k) + 120 * k);
                            chanG = (int)(chanG * (1 - k) + 140 * k);   // 2026-11-13 修：这里原来是 islandH（岛高），改名污染
                            B = (int)(B * (1 - k) + 165 * k);
                        }
                        else                            // 雪：霜白
                        {
                            R = (int)(R * (1 - k) + 215 * k);
                            chanG = (int)(chanG * (1 - k) + 222 * k);
                            B = (int)(B * (1 - k) + 235 * k);
                        }
                    }
                }
                if (R > 255) R = 255; if (chanG > 255) chanG = 255; if (B > 255) B = 255;
            }
            else { R = br; chanG = bg; B = bb; }   // 2026-11-13 修：又是 islandH 当绿色通道（第三处）

            // 2026-11-13 提亮挪到"预乘之后"了（见 UpdateIslandDisplay）：
            // 在这里提亮没用 —— 紧接着的预乘会再乘一次 alpha，把提亮打回去。
            if (sky.on && notchFade) pa = pa * nv / 255;   // ★ 背景这一层真的变透（字另画，不受影响）
            // 2026-11-11 浅色描边（第 2 版）：只算一份"输出色"，
            // 绝对不动 R/islandH/B 本身 —— 上一次直接改它们，把后面的月亮/混色逻辑一起带崩了。
            int oR = R, oG = chanG, oB = B;
            // 2026-11-11 我要求描边加粗到 5px：用距离判断，不再用覆盖率阈值。
            // dRim = 离边界多远（岛内为 0，但羽化带内 c<1 说明还没完全实心）
            // 只要不是"完全实心"（c<1 即处在 5px 羽化带里）就提亮 —— 这才真的是 5px 宽。
            // 之前写 c<0.995 只覆盖到离边 0.025px，等于没加宽。
            // 2026-11-12 改成"岛内 5px 内描边"：dOut 在岛内是负值（到最近边的距离），
            // -5 <= dOut < 0 正好是贴着边的 5 圈像素，直边和圆角都有。
            if (dOut >= -5.0f)
            {
                // 2026-11-12 我指出"描边像贴了圈胶带、有分裂感" —— 因为 5px 内是同一个亮度，
                // 第 5 到第 6 像素直接突变。改成从边缘往内逐渐衰减：贴边最亮，到 5px 归零。
                double t = (dOut + 5.0) / 5.0;    // 1=最贴边，0=内沿
                t = t * t;                         // 平方一下，衰减更自然
                // 强度压到 35%：之前 100% 是一圈明显的白边，看着更假
                int dr = (int)((R * 0.95 + 20) * t * 0.35);
                int dg = (int)((chanG * 0.95 + 20) * t * 0.35);
                int db = (int)((B * 0.95 + 20) * t * 0.35);
                oR = (R + dr > 255) ? 255 : R + dr;
                oG = (chanG + dg > 255) ? 255 : chanG + dg;
                oB = (B + db > 255) ? 255 : B + db;
            }
            row[idx]  =(BYTE)((oB * pa + 127) / 255);
            row[idx+1]=(BYTE)((oG * pa + 127) / 255);
            row[idx+2]=(BYTE)((oR * pa + 127) / 255);
            row[idx+3]=(BYTE)pa;
        }
    }

    // 小东西（星星 / 雨雪 / 月亮）画完底色再按点补上去
    // AI 岛时把星星/月亮/雨雪也关掉（它们是在底板之后单独叠的，不关就跟深色底打架）
    // 星星月亮画在蒙版之后，所以它们不会被打底的压暗吃掉。
    if (sky.on) SkyOverlay(p, stride, width, height, ox, oy, w, h, rx, ry);

    // 2. 顶部高光 / 底部压暗（纯粹为了好看）—— 天空模式自己有渐深，不用再来一遍
    if (w > 4 && h > 4 && !sky.on)
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
    if (islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD || islandMode == MODE_MINI_OUT)            refH = MINI_HEIGHT;
    else if (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT)    refH = NotchFontRef();   // 迷你岛：用"等效高度"喂字号
    else if (islandMode == MODE_TO_MINI || islandMode == MODE_TO_BIG)
    {
        // 变身：按**目标那一档**插值字号（目标是迷你岛就插到等效高度，别插到小岛的 56 再跳一下）。
        // ⚠️ 原来这里靠 `sf1 > 0.5f` 判断目标是不是迷你岛 —— 2026-10-16 迷你岛改成悬浮之后
        //     `sf` 恒为 0，这个判据永远不成立 → 字号先按小岛缩到 56，到迷你岛再硬跳回等效高度
        //     （我报的"缩过头再毫无征兆地弹回来"）。改成看变身目标 `gJ`，跟形状因子彻底解耦。
        int toH = ISLAND_HEIGHT;
        if (gJ == 12)      toH = NotchFontRef();
        else if (gJ == 7)  toH = MINI_HEIGHT;
        refH = (int)(ISLAND_HEIGHT + (toH - ISLAND_HEIGHT) * vv);
    }
    float fontScale;
    if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_OUT || islandMode == MODE_MINI_IN || islandMode == MODE_MINI_OUT) fontScale = easeOutCubic(I);
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
    // 变身中（islandMode= MODE_TO_MINI/islandMode= MODE_TO_BIG）也要用"大岛排版"：两端才能和相邻状态完全对齐，接缝处不跳
    // （大岛字与小岛字的可见区间互不重叠：vv<0.5 只有大岛字，vv>0.5 只有小岛字）
    bool showInfo=(islandMode==MODE_BIG_IN||islandMode==MODE_BIG_HOLD||islandMode==MODE_BIG_OUT||islandMode==MODE_TO_MINI||islandMode==MODE_TO_BIG||islandMode==MODE_HOLIDAY);
        // 2026-11-13 用户要求 A 方案：所有岛文字统一金色，不再按状态改色。
        // 深色主题用亮金，浅色主题用深金（浅底上亮金看不清）。
        COLORREF textColor = z ? RGB(255, 225, 140) : RGB(170, 120, 30);
    COLORREF shadowColor=z?RGB(0,0,0):RGB(255,255,255);
    if (hol == 1)      textColor = z ? RGB(255,206, 88) : RGB(150,82,  6);   // 中秋·浓金（原来太淡）
    else if (hol == 2) textColor = z ? RGB(255,204, 64) : RGB(186,24, 22);   // 国庆·浓金 / 深中国红

    // 2026-11-13 删掉"按状态换色"（课间冷蓝 / 放学灰）：
    // 就是它让画面从来没金过 —— 只要不在上课中，文字就变蓝或灰。
    // 用户明确要 A 方案：所有岛统一金色，状态靠内容区分，不靠颜色。

    // 自定义文字颜色（4d）：我设了就用他的，盖过主题和节日色
    //   0 = 迷你岛时间 / 1 = 大岛时间 / 2 = 大岛上方小字 / 3 = 课间倒计时
    //   总开关 Col().on 默认关 → 一律用主题金色
    // 2026-11-04 查清了：之前"没设过颜色却是红的"不是代码 bug ——
    //   测试时点过色块写进了注册表，而测试脚本的 regsafe 又把注册表还原了，
    //   加上冒烟留下的 islands.exe 僵尸进程挡住新进程（我一直在看老进程），
    //   三条叠在一起才造成"改了没用"的假象。功能本身一直是好的。
    {
        bool isMini = (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT);
        int ci = isMini ? 0 : 1;
        if (Col().on && Col().set[ci]) textColor = Col().c[ci];
    }

    // 滚动歌名时把合成范围裁到歌名区（0=不裁）
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
        if(gClipRight > 0 && x1 > gClipRight) x1 = gClipRight;
        if(x1<=x0||y1<=y0) return;
        for(int yy=y0;yy<y1;yy++)
            memset(aa+(size_t)yy*stride*4,0,(size_t)width*4);
        DrawText(Y,str,-1,&r,fmt);
        GdiFlush();
        CompositeGlyph(p,(BYTE*)aa,stride,x0,y0,x1,y1,col,alpha);
    };

    // 岛下方那行提示（白字黑描边，浮在桌面上也看得清）—— 常驻小岛与播放器共用同一套
    // heavy=true 时用 8 方向 2px 描边 —— HUD 用，浅色桌面上也读得清
    auto hint=[&](const wchar_t* s, bool heavy = false){
        // heavy（HUD）不跟 ww 走 —— ww 是"长按提示"的透明度，静止时是 0，HUD 会被它挡掉
        int al = heavy ? 255 : ww;
        if (al <= 0 || islandH <= 2) return;
        // 2026-11-13：岛下方那块画布明明有空间，但 UpdateLayeredWindow 之后就是看不见（试过改 l、改坐标都不行）。
        // 实测画进岛内立刻可见 → 说明下方那块被系统裁掉了。HUD 改画在岛内底部，不挡主要内容。
        RECT hr = heavy ? RECT{ox + 12, oy + h - 25, ox + w - 10, oy + h - 5}
                        : RECT{ox, oy + h + 6, ox + w, oy + h + 6 + HINT_GAP - 8};
        if (!heavy)
        {
            int off[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
            for (int k = 0; k < 4; k++)
            {
                RECT r2 = hr;
                r2.left += off[k][0]; r2.right += off[k][0];
                r2.top  += off[k][1]; r2.bottom+= off[k][1];
                stamp(r2, s, hFontHint, RGB(0,0,0), DT_CENTER|DT_TOP|DT_SINGLELINE, al*3/4);
            }
        }
        else
        {
            // 2026-11-14：HUD 原来是 8 方向 2px 黑描边 + 10px 字，全糊成一坨看不清。
            // 改成：一条**只包住文字**的深色底条 + 1px 影 + 13px 白字。
            // 底条宽度按文字实测宽度算 —— 铺满整条会把右边的大时间压掉一截。
            // 迷你岛只有 150 宽，13px 一行 HUD 必然压到时间上 → 干脆不画（要看 HUD 就弹个大岛）。
            if (w < 300 || h < 60) return;
            HFONT hf = GetCachedFont(13, false);
            SIZE ts = { 0, 0 };
            SelectObject(Y, hf);
            GetTextExtentPoint32W(Y, s, (int)wcslen(s), &ts);
            int bw = ts.cx + 18;
            FillBoxPx(p, stride, width, height, ox + 6, oy + h - 27, ox + 6 + bw, oy + h - 5,
                      RGB(0, 0, 0), 130);
            RECT r2 = hr;
            r2.left += 1; r2.right += 1; r2.top += 1; r2.bottom += 1;
            stamp(r2, s, hf, RGB(0,0,0), DT_LEFT|DT_TOP|DT_SINGLELINE, 255);
        }
        stamp(hr, s, heavy ? GetCachedFont(13, false) : hFontHint,
              RGB(255,255,255), (heavy ? DT_LEFT : DT_CENTER)|DT_TOP|DT_SINGLELINE, al);
    };

    RECT shapeRect = {ox, oy, ox+w, oy+h};

    // ===== 播放器样式 =====
    // 放学《回家》在放（或在设置里试听）时，大岛不显示"下课了+时间"，而是变成播放器：
    // 顶上一行状态小字 + 歌名大字 + 右边一个圆形的播放/暂停键。
    // 我明确：只要一个最主要的播放/暂停键，不要歌词、不要进度条。
    // 判据里必须带 `bigH == PLAYER_H`：放学那一下大岛是按**普通高度**弹出来的，
    // 那一刻要正常显示「下课了」+几点，不能抢着画播放器（否则封面会撑出岛外）。
    if (ply && bigH == PLAYER_H && (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_BIG_OUT || islandMode == MODE_TO_BIG) && plyA > 0)
    {
        int pa = (int)plyA;                             // 过渡中 0→255，配合 ta 做交叉淡入淡出
        // 整体按"当前高度 / 播放器应有高度"缩放：弹出(islandMode= MODE_BIG_IN)是 0→1、变大(islandMode= MODE_TO_BIG)是 0.65→1、
        // 收回(islandMode= MODE_BIG_OUT)再缩回去 —— 这样封面和按钮不会在小岛上撑破。
        float k = (float)islandH / (float)bigH;
        if (k > 1.12f) k = 1.12f;
        if (k < 0.05f) k = 0.05f;

        int btnD  = (int)(PLAYER_BTN_D * k);  if (btnD  < 4) btnD  = 4;
        int cs    = (int)(COVER_S      * k);  if (cs    < 4) cs    = 4;
        int padL  = (int)(PLAYER_PAD_L * k);
        int padR  = (int)(PLAYER_PAD_R * k);
        int gap   = (int)(18 * k);
        // 播放键左边留给"两个小球"的宽度（约 2.7 个半径：两球 + 一点留白）
        // 球要占住中间那块空：半径到岛高的 30%（直径约 0.6 岛高），
        // 歌名区再收掉对应宽度，文字不会被压住。
        int ballR0 = (int)(h * 0.19);   // 0.30 太大了，会盖住播放键；0.19 直径约岛高 38%
        if (ballR0 < 6) ballR0 = 6;
        int ballW  = (int)(ballR0 * 3.2f) + (int)(12 * k);
        int ballGap2 = (int)(gap * 2.2f);   // 球和播放键之间留更宽的间隔

        int bx = ox + w - padR - btnD;                  // 按钮左边界（画布坐标）
        int by = oy + (h - btnD) / 2;
        plyBtn.left   = cnv + bx;                         // 命中用屏幕坐标：屏幕 x = 画布 x + n
        plyBtn.top    = by;                             // （y 不用加，窗口顶边就在屏幕顶边）
        plyBtn.right  = cnv + bx + btnD;
        plyBtn.bottom = by + btnD;

        // ① 左：专辑封面（没有 Home.jpg 就不画，右边内容自动往左补）
        if (!covPx) CoverLoad();
        int cx0 = ox + padL, cy0 = oy + (h - cs) / 2;
        if (CoverBlit(p, stride, width, height, cx0, cy0, cs, pa))
            cx0 += cs + gap;
        else
            cx0 = ox + 26;

        // ② 中：歌名 + 状态小字
        RECT tr = { cx0, oy, bx - ballGap2 - ballW, oy + h };
        if (tr.right < tr.left + 20) tr.right = tr.left + 20;
        HFONT fT = GetCachedFont(std::max(10, (int)(44 * k)), true);
        HFONT fS = GetCachedFont(std::max(7,  (int)(19 * k)), false);
        // 优先级：自己的《回家》 > 别的程序在放的音乐 > 空状态
        const wchar_t* song = L"《回家》";
        const wchar_t* sts  = !bgmPlay ? L"未在播放" : (bgmPause ? L"已暂停" : L"正在播放");
        RECT sr = { tr.left, oy + (int)(h * 0.14f), tr.right, oy + (int)(h * 0.46f) };
        RECT gr = { tr.left, oy + (int)(h * 0.46f), tr.right, oy + (int)(h * 0.92f) };
        RECT srh = sr, grh = gr;
        srh.left += 1; srh.top += 1; grh.left += 2; grh.top += 2;
        stamp(srh, sts,  fS, shadowColor, DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
        stamp(sr,  sts,  fS, textColor,   DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
        // 歌名太长就滚动（先停 1.5 秒，再匀速往左滚，滚完留个空隙循环）
        {
            SelectObject(Y, fT);
            SIZE ts = { 0, 0 };
            GetTextExtentPoint32W(Y, song, (int)wcslen(song), &ts);
            int availW = gr.right - gr.left;
            int off = 0;
            if (ts.cx > availW)
            {
                static const wchar_t* lastSong = NULL;
                static LONGLONG songT0 = 0;
                if (lastSong != song) { lastSong = song; songT0 = NowMs(); }
                LONGLONG el = NowMs() - songT0;
                const int HOLD = 1500, SPEED = 34;        // 停 1.5s，每秒 34px
                if (el > HOLD)
                {
                    int span = ts.cx + (int)(40 * k);      // 滚完留 40px 空隙
                    off = (int)(((el - HOLD) * SPEED / 1000) % span);
                }
                gClipRight = gr.right;                     // 超出歌名区的不画
            }
            RECT g2 = gr, g2h = grh;
            g2.left -= off; g2.right -= off;
            g2h.left -= off; g2h.right -= off;
            stamp(g2h, song, fT, shadowColor, DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
            stamp(g2,  song, fT, textColor,   DT_LEFT|DT_VCENTER|DT_SINGLELINE, pa);
            gClipRight = 0;
        }

        // ③ 右：播放 / 暂停圆钮
        int ccx = bx + btnD/2, ccy = oy + h/2;
        FillDisc(p, stride, width, height, ccx, ccy, btnD/2, textColor, 46 * pa / 255);
        int gs = btnD * 42 / 100;
        if (bgmPause) FillPlayTri  (p, stride, width, height, ccx, ccy, gs, textColor, pa);
        else          FillPauseBars(p, stride, width, height, ccx, ccy, gs, textColor, pa);

        // ④ 播放键左边两个"贴在一起的小球"，跟着音乐呼吸（2026-10-18 按我要求从底部挪过来）
        // 能量来自 WASAPI 回环（MCI 拿不到 PCM），见 AmpThreadProc。
        if (pa > 40)
        {
            // 双包络：fast 跟节奏（约 80ms）、slow 记整体音压（1.2s）。
            // 差值 punch = 此刻比基准高多少 —— 音乐起伏大，punch 就大，球的变化也更明显。
            static float sFast[2] = { 0.0f, 0.0f }, sSlow[2] = { 0.0f, 0.0f };
            float raw = (float)InterlockedCompareExchange((volatile LONG*)&gAmpRaw, 0, 0) / 1000.0f;
            // 自适应增益：跟踪一个缓慢回落的峰值，用 raw/peak 归一化。
            // 不然柔和的人声段能量太低，固定的放大倍数会把它们全压成 0 —— 只剩鼓点在跳。
            static float peak = 0.08f;
            if (raw > peak) peak = raw;
            else peak *= 0.998f;                 // 约 6 秒回落到一半
            if (peak < 0.02f) peak = 0.02f;
            raw = raw / peak;
            if (raw > 1.0f) raw = 1.0f;
            if (bgmPause || !bgmPlay) raw = 0.0f;   // 暂停/没播：输入归零，但平滑照跑 → 慢慢收回去
            LONGLONG nowm = NowMs();
            static LONGLONG lastm = 0;
            float dt = lastm ? (float)(nowm - lastm) / 1000.0f : 0.016f;
            if (dt > 0.2f) dt = 0.2f;
            lastm = nowm;
            for (int i = 0; i < 2; i++)
            {
                // 两个球的快包络时间常数不同 → 一个跟得紧、一个懒一点，天然错开
                const float tFast = (i == 0) ? 0.050f : 0.080f;   // 再快一点，柔歌的连音也能跟上
                float kf = 1.0f - expf(-dt / tFast);
                float ks = 1.0f - expf(-dt / 1.2f);
                sFast[i] += (raw - sFast[i]) * kf;
                sSlow[i] += (raw - sSlow[i]) * ks;
            }

            // 节拍脉冲：读到就清零，避免一直亮着
            static float beatPulse = 0.0f;
            beatPulse *= 0.86f;                                  // 约 250ms 衰减完
            if (InterlockedExchange(&gBeat, 0) > 500) beatPulse = 1.0f;
            int bpmNow = (int)InterlockedCompareExchange(&gBpm, 0, 0);

            double tt = nowm / 1000.0;
            // 两个球的组中心：贴在播放键左边
            int gc = bx - ballGap2 - ballW / 2;   // 往中间靠：离播放键远一点，放大时不会压到按钮
            int gy = oy + h / 2;
            // 圆心间距 = 1.55r：有一段是重叠贴住的（这是我要的"贴合"）
            int sep = (int)(ballR0 * 1.55f);
            for (int i = 0; i < 2; i++)
            {
                float f = sFast[i];                       // 此刻音量
                float punch = f - sSlow[i];               // 比基准高出多少 = 节奏起伏
                if (punch < 0.0f) punch = 0.0f;
                // 基础 1.0：音量贡献 0~0.30（人声段也有明显变化），起伏再叠最多约 0.15
                double grow = 1.0 + 0.30 * f + 0.9 * punch;
                // 节拍：数得出 BPM 时，每一拍额外顶一下 —— 这才是"跟着节奏"而不是"跟着音量"
                if (bpmNow) grow += 0.14 * beatPulse;
                if (grow > 1.42) grow = 1.42;             // 封顶，别撑破布局
                int r = (int)(ballR0 * grow);
                if (r < 4) r = 4;
                // 极轻的上下浮动，两个球反相 —— 只是让它活，不抢戏
                int dy = (int)((f * 1.4 + punch * 3.0) * sin(tt * 1.1 + i * 3.14));
                int cxx = gc + (i == 0 ? -sep / 2 : sep / 2);
                // 重叠区被 BlendPx 叠两次自然加深，正好是贴住的那块
                // 亮度也跟着律动走：静音 0.23 → 人声约 0.45 → 高潮能到 0.62
                // （我要的"变大时也更亮一点"，但别亮到发光）
                int al = (int)((58 + 78 * f + 120 * punch) * pa / 255.0);
                if (al > 186) al = 186;   // 双色以后可以再明显一点，不然在深色底上偏灰
                // 颜色：主球用封面主色、副球用它的补色（vivo 原子播放器那种双色）。
                // 后期加歌只要换封面，颜色自动跟着变，不用一张张配。
                COLORREF bc = (i == 0) ? covMainColor : covAltColor;
                FillEllipseAA(p, stride, width, height, cxx, gy + dy, r, r, bc, al, false);
            }
        }

        // 只有"播放器彻底接管了"才提前收工；淡入途中要接着往下画，
        // 让「下课了」那套普通内容（用 ta 控制透明度）跟播放器交叉淡入淡出。
        // islandMode==MODE_BIG_HOLD 表示变身已经结束，播放器该独占了。
        // 只看 plyA>=255 不够稳：plyA 是插值出来的，某些帧会卡在 254.x，
        // 结果大岛文字接着画上去把播放器盖住（自检时抓到的，画面里还有个球飘在岛外）。
        if (plyA >= 250.0f || islandMode == MODE_BIG_HOLD)
        {
            hint(L"长按关闭播放器");
            return;
        }
    }

    // ===== 隐私提示：左边一个自绘图标（麦克风/摄像头），右边写字 =====
    // 我要的版式就是"左图标 + 右文字"；图标和文字一起随岛长大、跟着 ta 一起淡入淡出。
    if (hh && privKind && !ply)
    {
        int pa = (int)(255.0f * std::max(0.0f, 1.0f - vv * 2.0f) * (ta / 255.0f));
        if (pa <= 0) return;
        float k = (float)islandH / (float)ISLAND_HEIGHT;      // 弹出/收回时按岛高整体缩放
        if (k > 1.15f) k = 1.15f;
        if (k < 0.05f) k = 0.05f;
        int nIcon = (privKind == 3) ? 2 : 1;
        int isz  = (int)(PRIV_ICON_W * k); if (isz < 5) isz = 5;
        int padL = (int)(PRIV_PAD_L * k);
        int gap  = (int)(PRIV_GAP * k);
        int igap = (int)(10 * k);
        int ix = ox + padL;
        int iy = oy + (h - isz) / 2;
        for (int kk = 0; kk < nIcon; kk++)
        {
            // kind==1 只麦克风、==2 只摄像头、==3 两个并排（先麦克风后摄像头）
            int one = (privKind == 3) ? (kk == 0 ? 1 : 2) : privKind;
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
        // 迷你岛静止后是**整块一起淡**：背景靠逐像素 alpha 淡，这行字也得跟着淡
        // （不然就是"背景透明的、字却死亮"，不合套）。
        int tA = ta;
        if (nt) tA = ta * nv / 255;
        // 2026-11-12 我要求把"检测背景亮度自动换字色"整套删掉：
        // 它每秒重采一次，背景亮度在 148 附近抖的时候，字的颜色和透明度会来回跳，看着一闪一闪。
        // 现在统一跟背景一起淡，不再看背景脸色。
        stamp(ts, timeStr, hFontLarge, shadowColor, DT_CENTER|DT_VCENTER|DT_SINGLELINE, tA);
        stamp(tr, timeStr, hFontLarge, textColor,   DT_CENTER|DT_VCENTER|DT_SINGLELINE, tA);

        if (showInfo && aBig > 0)     // 顶部那行小标签（上课了/下课了/时间到！）仍按 aBig 淡出
        {
            // 顶行永远显示"事件"本身（上课了/下课了/时间到！）——
            // 节日祝福不在这里插队：那样会把"下课了"顶掉，跟"先显示下课、再出祝福"的两段式打架
            wchar_t infoStr[32];
            if (infoCustom[0]) wcscpy_s(infoStr, infoCustom);
            else AlarmLabel(M, infoStr);
            // 课表模式：靠左 + 左内边距（半圆半径 + 18），别贴着圆弧
            int   padL = schedMode ? (h / 2 + 18) : 0;
            DWORD al   = schedMode ? DT_LEFT : DT_CENTER;
            RECT infoRect={shapeRect.left + padL, oy, shapeRect.right - (padL ? 8 : 0), oy+(int)(h*0.28)};
            RECT infoSh=infoRect; infoSh.left+=1;infoSh.top+=1;infoSh.right+=1;infoSh.bottom+=1;
            stamp(infoSh,infoStr,hFontSmall,shadowColor,al|DT_TOP|DT_SINGLELINE,aBig);
            stamp(infoRect,infoStr,hFontSmall,(Col().on && Col().set[2])?Col().c[2]:textColor,al|DT_TOP|DT_SINGLELINE,aBig);
        }
    }
    else if (aBig > 0)
    {
        // 另一端是消息或倒计时 → 两条不同的文字共用同一行，只能照旧交叉渐变
        RECT textRect;
        int   padL2 = schedMode ? (h / 2 + 18) : 0;
        DWORD al2   = schedMode ? DT_LEFT : DT_CENTER;
        if(showInfo) textRect={shapeRect.left + padL2, oy+(int)(h*0.3), shapeRect.right - (padL2 ? 8 : 0), oy+h};
        else         textRect=shapeRect;
        RECT shRect=textRect; shRect.left+=2;shRect.top+=2;shRect.right+=2;shRect.bottom+=2;
        const wchar_t* bigStr = hh ? gg : timeStr;
        bool rolled = false;
        if (!hh)
        {
            // 时间数字"滚上去"（2026-11-05）：旧数字上移淡出、新数字从下方上来。
            // 之前以为 stamp 会互相擦 —— 其实它清的是临时缓冲 aa，CompositeGlyph 是累加到 p 的，
            // 多次调用只会叠加。所以这里可以放心画两遍。
            static wchar_t prevT[16] = {0}, oldT[16] = {0};
            static LONGLONG t0 = 0;
            if (wcscmp(timeStr, prevT) != 0)
            {
                wcscpy_s(oldT, prevT);
                wcscpy_s(prevT, timeStr);
                t0 = NowMs();
                // 滚动这 300ms 里，迷你岛本来是"静止不重绘"的，最多只能看到 1-2 帧。
                // 开个 16ms 的临时定时器强制重绘，滚动才丝滑。
                g_rollUntil = NowMs() + 300;
                SetTimer(hMain, 79, 16, NULL);
            }
            double pr = (double)(NowMs() - t0) / 280.0;
            if (pr > 1.0) pr = 1.0;
            if (pr < 1.0 && oldT[0])
            {
                rolled = true;
                int up = (int)(pr * 30);          // 旧值走过的距离
                int dn = 30 - up;                 // 新值还差多少到位
                RECT ro = textRect, so = shRect, rn = textRect, sn = shRect;
                ro.top -= up; ro.bottom -= up; so.top -= up; so.bottom -= up;
                rn.top += dn; rn.bottom += dn; sn.top += dn; sn.bottom += dn;
                int aOld = (int)(aBig * (1.0 - pr));
                int aNew = (int)(aBig * pr);
                stamp(so, oldT,     hFontLarge, shadowColor, DT_CENTER|DT_VCENTER|DT_SINGLELINE, aOld);
                stamp(ro, oldT,     hFontLarge, textColor,   DT_CENTER|DT_VCENTER|DT_SINGLELINE, aOld);
                stamp(sn, timeStr,  hFontLarge, shadowColor, DT_CENTER|DT_VCENTER|DT_SINGLELINE, aNew);
                stamp(rn, timeStr,  hFontLarge, textColor,   DT_CENTER|DT_VCENTER|DT_SINGLELINE, aNew);
            }
        }
        // P2：原地换字的交叉渐变（旧字 → 新字，260ms）
        if (!rolled && g_swapT0 && g_prevBig[0])
        {
            double pt2 = (double)(NowMs() - g_swapT0) / 260.0;
            if (pt2 >= 1.0) { g_swapT0 = 0; g_prevBig[0] = 0; }
            else
            {
                rolled = true;
                int aOld = (int)(aBig * (1.0 - pt2));
                int aNew = (int)(aBig * pt2);
                // 旧字按它原来的位置画（大岛的大字一直是居中的）
                RECT oc = shapeRect, os = shapeRect;
                os.left += 2; os.top += 2; os.right += 2; os.bottom += 2;
                stamp(os, g_prevBig, hFontLarge, shadowColor, DT_CENTER|DT_VCENTER|DT_SINGLELINE, aOld);
                stamp(oc, g_prevBig, hFontLarge, textColor,   DT_CENTER|DT_VCENTER|DT_SINGLELINE, aOld);
                stamp(shRect, bigStr, hFontLarge, shadowColor, al2|DT_VCENTER|DT_SINGLELINE, aNew);
                stamp(textRect, bigStr, hFontLarge, textColor, al2|DT_VCENTER|DT_SINGLELINE, aNew);
            }
        }
        if (!rolled)
        {
            stamp(shRect,bigStr,hFontLarge,shadowColor,al2|DT_VCENTER|DT_SINGLELINE,aBig);
            stamp(textRect,bigStr,hFontLarge,textColor,al2|DT_VCENTER|DT_SINGLELINE,aBig);
        }

        // 2026-11-13：课表岛漏了顶行。
        // 原来判据是 `showInfo && !hh` —— 消息岛 hh=true，整个顶行直接跳过；
        // 而且进来也只查 AlarmLabel(M)，消息岛的 M 是 0，查出来是空串。
        // ShowMessageIsland 明明把 small 存进了 infoCustom（"下节课是"），白存了。
        // 所以：课表模式单独放行，并且优先用 infoCustom。
        if(showInfo && (!hh || schedMode)){
            // 顶行永远显示"事件"本身（上课了/下课了/时间到！）——
            // 节日祝福不在这里插队：那样会把"下课了"顶掉，跟"先显示下课、再出祝福"的两段式打架
            wchar_t infoStr[32];
            if (infoCustom[0]) wcscpy_s(infoStr, infoCustom);
            else AlarmLabel(M, infoStr);
            // 课表模式：靠左 + 左内边距（半圆半径 + 18），别贴着圆弧
            int   padL = schedMode ? (h / 2 + 18) : 0;
            DWORD al   = schedMode ? DT_LEFT : DT_CENTER;
            RECT infoRect={shapeRect.left + padL, oy, shapeRect.right - (padL ? 8 : 0), oy+(int)(h*0.28)};
            RECT infoSh=infoRect; infoSh.left+=1;infoSh.top+=1;infoSh.right+=1;infoSh.bottom+=1;
            stamp(infoSh,infoStr,hFontSmall,shadowColor,al|DT_TOP|DT_SINGLELINE,aBig);
            stamp(infoRect,infoStr,hFontSmall,(Col().on && Col().set[2])?Col().c[2]:textColor,al|DT_TOP|DT_SINGLELINE,aBig);
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
            swprintf_s(miniStr, L"%s %02d:%02d", g_cdLabel, sec/60, sec%60);
        }
        else miniStr[0] = 0;   // 没有课间倒计时就不画字（"仅时间小岛"这个老样式已废弃）
        if (miniStr[0])
        {
        // 最后 60 秒进"红区"：时间变红，提醒该收尾了（我明确不要跳动）
        COLORREF cdCol = (Col().on && Col().set[3]) ? Col().c[3] : textColor;
        if (pp && !nt)
        {
            LONGLONG leftMs = rr - NowMs();
            if (leftMs > 0 && leftMs <= 60000) cdCol = RGB(255, 76, 76);
        }
        RECT mr=shapeRect, ms=shapeRect;
        ms.left+=2; ms.top+=2; ms.right+=2; ms.bottom+=2;
        stamp(ms,miniStr,hFontLarge,shadowColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aMini);
        stamp(mr,miniStr,hFontLarge,cdCol,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aMini);
        }
    }

    // 3b2) 迷你岛底部进度线：这节课还剩多少。上课满格，下课前缩到底。
    if (notchState && h >= 28)
    {
        LONGLONG ps = 0, pe = 0;
        if (ClassInProgress(&ps, &pe) && pe > ps)
        {
            double prog = (double)(NowMs() - ps) / (double)(pe - ps);
            if (prog < 0) prog = 0;
            if (prog > 1) prog = 1;
            int padX = 14;
            int barW = (int)((w - padX * 2) * prog + 0.5);
            if (barW < 1) barW = 1;
            int by = oy + h - 5;
            int bx = ox + padX;
            // ⚠️ 2026-11-13 修"上课时间开机必闪退"：
            //   迷你岛是从屏幕**上方滑进来**的，入场头几帧 islandY = -48，而 h 一上来就是 28
            //   （正好等于上面那个 `h >= 28` 的门槛）→ by = -25 → p 往前数 25 行写像素 → 访问违例。
            //   没课的时段不画这条进度线，所以这个坑一直没暴露（旧文档把它归成"定时线程抢跑"）。
            //   顺手把横向也夹住：变身过程中 w 有可能比画布宽，一样会越界。
            if (by >= 0 && by + 1 < height && bx >= 0 && bx < width)
            {
                if (bx + barW > width) barW = width - bx;
                int cr = GetRValue(textColor), cg2 = GetGValue(textColor), cb = GetBValue(textColor);
                for (int yy = 0; yy < 2; yy++)
                {
                    BYTE* row = p + (size_t)(by + yy) * stride * 4 + (size_t)bx * 4;
                    for (int xx = 0; xx < barW; xx++)
                    {
                        row[xx*4+0] = (BYTE)(cb * 0.55);
                        row[xx*4+1] = (BYTE)(cg2 * 0.55);
                        row[xx*4+2] = (BYTE)(cr * 0.55);
                        row[xx*4+3] = (BYTE)150;
                    }
                }
            }
        }
    }

    // 3c) 岛下方那行提示：HUD 开着的时候，这行拿来做调试信息
    extern bool  g_hud;
    extern float g_speedDbg;
    if (g_hud)
    {
        wchar_t hb[128];
        double lf = (hideAtMs > 0) ? (double)(hideAtMs - NowMs()) / 1000.0 : 0.0;
        // 岛只有 150-424 宽，长文案会被裁；用短标签
        swprintf_s(hb, L"M%d  %d×%d  %.1fx", islandMode, w, h, g_speedDbg);
        hint(hb, true);           // HUD 用粗描边，浅桌面上也看得清
    }
    else hint((ply && bigH == PLAYER_H) ? L"长按关闭播放器" : L"长按关闭常驻岛");

    // 临时：每秒结算一次帧率
    {
        LONGLONG n2 = NowMs();
        if (g_fpsT0 == 0) g_fpsT0 = n2;
        if (n2 - g_fpsT0 >= 1000)
        {
            g_fpsVal = g_fpsCnt * 1000.0 / (double)(n2 - g_fpsT0);
            g_fpsCnt = 0;
            g_fpsT0  = n2;
        }
    }
}

// 把画布内容提交给窗口
#ifdef TEST_PERF
// 第 7 项：动画抖动的取证工具。只回答一个问题 —— 一帧的时间到底花在哪。
// 7 个环节：draw(主岛) / below(下面的原岛) / fade(淡出中的AI岛) / bub(消息岛)
//          / alpha(逐像素补alpha) / pos(SetWindowPos) / ulw(UpdateLayeredWindow)
static LARGE_INTEGER pfFreq;
static LARGE_INTEGER pfQ[10];
static double pfAcc[9];
static int    pfFrames = 0;
static bool   pfInit = false;

static void PfFlush(int jj, int ai, int nb)
{
    if (!pfInit) { QueryPerformanceFrequency(&pfFreq); pfInit = true; }
    for (int i = 0; i < 7; i++)
        pfAcc[i] += (double)(pfQ[i + 1].QuadPart - pfQ[i].QuadPart) * 1000.0 / (double)pfFreq.QuadPart;
    ++pfFrames;
    if (pfFrames < 30) return;

    double tot = 0.0;
    for (int i = 0; i < 7; i++) tot += pfAcc[i];
    FILE* f = fopen("islandX:/aab/perf.log", "a");
    if (f)
    {
        fprintf(f, "islandMode=%d ai=%d bub=%d n=%d | draw=%.2f below=%.2f fade=%.2f bubd=%.2f alpha=%.2f pos=%.2f ulw=%.2f | total=%.2f\n",
                jj, ai, nb, pfFrames,
                pfAcc[0] / 30, pfAcc[1] / 30, pfAcc[2] / 30, pfAcc[3] / 30,
                pfAcc[4] / 30, pfAcc[5] / 30, pfAcc[6] / 30, tot / 30);
        fclose(f);
    }
    for (int i = 0; i < 7; i++) pfAcc[i] = 0;
    pfFrames = 0;
}
#define PF_MARK(i) QueryPerformanceCounter(&pfQ[i])
#else
#define PF_MARK(i)
#endif

void UpdateIslandDisplay(HWND hWnd, float scale, BYTE alpha)
{
    (void)scale; (void)alpha;
    if (islandW < 2 || islandH < 2) return;
    // 画布必须装得下当前的岛，否则岛的左右会被裁掉 ——
    // 迷你岛"四角变方"就是被裁了 11px（圆角正好在那条边里）。这里是最后一道保险。
    int needW = islandW + 16;
    if (cw < needW) { cw = needW; cnv = (B - cw) / 2; }

    int needH = islandH + 12;
    if (ch < needH) ch = needH;
    if (!EnsureBuffers()) return;

    PF_MARK(0);
    GlobalsToState(gA);          // 先把当前全局状态收进 gA，再交给 DrawIsland
#ifdef PERFLOG
    LARGE_INTEGER pfT0, pfT1, pfF; QueryPerformanceFrequency(&pfF); QueryPerformanceCounter(&pfT0);
#endif
    DrawIsland(X, cc, cw, ch, gA);
#ifdef PERFLOG
    QueryPerformanceCounter(&pfT1);
    {
        static double acc = 0; static int cnt = 0;
        acc += (double)(pfT1.QuadPart - pfT0.QuadPart) * 1000.0 / (double)pfF.QuadPart;
        if (++cnt >= 150)
        {
            FILE* f = fopen("D:/aab/perf.log", "a");
            if (f) { fprintf(f, "draw avg %.2f ms  mode=%d  %dx%d\n", acc / cnt, islandMode, islandW, islandH); fclose(f); }
            acc = 0; cnt = 0;
        }
    }
#endif
    PF_MARK(1);
    PF_MARK(2);
    PF_MARK(3);
    PF_MARK(4);

    // ===== 统一补 alpha =====
    // GDI（RoundRect / FillRect / TextOut）在 32 位 DIB 上画会把 alpha 字节压低，
    // 而岛用 UpdateLayeredWindow + AC_SRC_ALPHA，alpha 低就是半透明 →
    // litehtml 气泡和输入框底会"透出后面的窗口"。这里统一补回来。
    // 岛外面是纯 (0,0,0,0)，不会被误伤，圆角依旧。
    int iox = islandX - cnv;
    int ioy = islandY;
    int iow = islandW;
    int ioh = islandH;
    // 岛身**自己**那块矩形
    const int isx = iox, isy = ioy, isw = iow, ish = ioh;
    const int isr = (iow < ioh ? iow : ioh) / 2;   // 跟 DrawIsland 里一样：胶囊半径 = min(w,h)/2
    for (int yy = 0; yy < ch; ++yy)
    {
        BYTE* row = X + (size_t)yy * cc * 4;
        bool inY = (yy >= ioy && yy < ioy + ioh);
        for (int xx = 0; xx < cw; ++xx)
        {
            BYTE* qq = row + (size_t)xx * 4;
            // 岛外面一律清空 —— litehtml 的 clip 对 float 元素不生效，
            // 气泡会画到岛左右外面去（截图里灰气泡伸到岛左边）。这里兜底砍掉。
            if (!inY || xx < iox || xx >= iox + iow)
            {
                qq[0] = qq[1] = qq[2] = qq[3] = 0;
                continue;
            }
            if (qq[3] < 250 && (qq[0] | qq[1] | qq[2]))
            {
                // ⚠️ 2026-11-13 修"黑边"真凶就在这一句。
                //   原来是无条件 qq[3] = 255 → 把**抗锯齿的羽化带**也一起按成实心，
                //   而那一圈像素的 RGB 早在画底板时就已经乘过 alpha 了（预乘），
                //   等于"把压暗的颜色以 100% 不透明度端上来"：
                //     · 夜里底板 R=34、覆盖率 0.78 → 显示成 27，贴白桌面 = 一圈发黑；
                //     · 同时抗锯齿没了，圆角是硬台阶（放大看得见格子）。
                //   1.1 是浅金底板，压暗后还有 240+，所以"看不见"，不是"没有"。
                //   现在：岛身内部照旧补到实心（GDI 画过的地方 alpha 会被压成 0），
                //   羽化带按圆角覆盖率给 alpha —— 预乘过的 RGB 只有配上对应 alpha 才是对的。
                int need = 255;
                if (xx >= isx && xx < isx + isw && yy >= isy && yy < isy + ish)
                {
                    float cv = ShapeCoverage(xx - isx, yy - isy, isw, ish, isr, isr);
                    int c255 = (int)(cv * 255.0f + 0.5f);
                    if (c255 < 0) c255 = 0;
                    if (c255 > 255) c255 = 255;
                    need = c255;
                }
                if (need > (int)qq[3]) qq[3] = (BYTE)need;
            }
        }
    }

    PF_MARK(5);
    // 窗口尺寸先跟画布对齐，再交画面：某些环境里"窗口尺寸 ≠ 源画布尺寸"就会带出一条黑边
    static int lj = -1, ll = -1, ln = -1, ls = -1;
    int shift = 0;
    if (cw != lj || ch != ll || cnv != ln || shift != ls)
    {
        SetWindowPos(hWnd, HWND_TOPMOST, cnv, shift, cw, ch, SWP_NOACTIVATE);
        lj = cw; ll = ch; ln = cnv; ls = shift;
    }
    PF_MARK(6);

    // 图层整体保持不透明，**透光交给逐像素 alpha 做**（分层窗口本来就是 AC_SRC_ALPHA）。
    // 这样能做到"背景透、字不透"：
    //   背景像素的 alpha 乘 nv → 真的能看到后面的桌面/网页（不是"画暗一点"）；
    //   时间那行字的 alpha 保持满 → 透出去的同时照样看得清。
    // 踩过的两个坑：① 只压颜色不压 alpha → 只是变暗，一点不透光；
    //             ② 用图层整体不透明度 → 字跟背景一起淡，看不清。
    // ★ 提交前**绝对不要**再乘一次 alpha —— "黑边"这个坑就是它造成的，整整绕了三版。
    //   2026-11-13 查清：DrawIsland 画底板时**已经是预乘**了（搜 `row[idx]  =(BYTE)((oB * pa + 127) / 255)`），
    //   而 BlendPx / CompositeGlyph / SkyOverlay / CoverBlit 全是"吃预乘、吐预乘"的标准 over 混合，
    //   所以整块画布交出去的时候本来就是预乘 BGRA。这里再乘一遍 a/255，等于把边缘颜色平方：
    //     夜里底板 R=34、边缘 alpha 0.4 → 34×0.4×0.4 ≈ 5，贴在白桌面上就是一圈灰黑边。
    //   1.1 版"看着没事"是因为底板是浅金色，平方完还有 240+ —— 不是没有，是看不出来。
    //   下面原来还挂着一套"a<130 提亮 0.35"的补丁，既盖不住（范围太窄），
    //   又在圆角内侧糊出一圈月牙白斑（用户圈过）。预乘去掉了，补丁一起删。

    BYTE ga = (BYTE)255;
    BLENDFUNCTION blend={AC_SRC_OVER,0,ga,AC_SRC_ALPHA};
    POINT ptSrc={0,0};
    SIZE  sz={cw, ch};
    POINT ptDst={cnv, 0};
    UpdateLayeredWindow(hWnd, NULL, &ptDst, &sz, U, &ptSrc, 0, &blend, ULW_ALPHA);
    PF_MARK(7);
#ifdef TEST_PERF
    PfFlush(gA.islandMode, 0, 0);
#endif

}

// 11 个课间时段，写死的。要改直接改这仨数组，别问我为什么不读配置
// ===== 时间段时间覆盖（4b，2026-11-04）=====
// 我可以在「自定义」里改某个时间段的开始/结束时间，不用动源码改数组。
// -1 = 没覆盖，用该作息表的默认值。
int g_ovS[11], g_ovE[11];

// 哪些时间段还在用（2026-11-04）：我可以删掉不需要的段，最少留 1 个。
// 删掉的段只是"不参与"——不进 jj、不在界面显示，名字还在，随时能恢复。
bool g_segOn[11];

// 文字颜色（4d）：0=迷你岛时间 1=大岛时间 2=大岛上方小字 3=倒计时
// 0 = 没设置，用主题默认色

void LoadColCfg()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    DWORD v = 0, sz = sizeof(v);
    for (int i = 0; i < 4; i++)
    {
        wchar_t nm[16];
        swprintf_s(nm, L"Col%d", i);
        sz = sizeof(v);
        if (RegQueryValueExW(k, nm, NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) { Col().c[i] = (COLORREF)v; Col().set[i] = true; }
    }
    {
        DWORD on = 0, so = sizeof(on);
        if (RegQueryValueExW(k, L"ColOn", NULL, NULL, (LPBYTE)&on, &so) == ERROR_SUCCESS) Col().on = (on != 0);
    }
    RegCloseKey(k);
}
void SaveColCfg()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    for (int i = 0; i < 4; i++)
    {
        wchar_t nm[16];
        swprintf_s(nm, L"Col%d", i);
        DWORD v = (DWORD)Col().c[i];
        RegSetValueExW(k, nm, 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
    }
    DWORD on = Col().on ? 1 : 0;
    RegSetValueExW(k, L"ColOn", 0, REG_DWORD, (LPBYTE)&on, sizeof(on));
    RegCloseKey(k);
}

void LoadSegOn()
{
    for (int i = 0; i < 11; i++) g_segOn[i] = true;
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    DWORD m = 0x7FF, sz = sizeof(m);
    if (RegQueryValueExW(k, L"SegOn", NULL, NULL, (LPBYTE)&m, &sz) == ERROR_SUCCESS)
        for (int i = 0; i < 11; i++) g_segOn[i] = (m & (1u << i)) != 0;
    RegCloseKey(k);
}
void SaveSegOn()
{
    DWORD m = 0;
    for (int i = 0; i < 11; i++) if (g_segOn[i]) m |= (1u << i);
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, L"SegOn", 0, REG_DWORD, (LPBYTE)&m, sizeof(m));
    RegCloseKey(k);
}
// 还在用的段数（至少保证 1 个）
int SegCount()
{
    int cnt = 0;
    for (int i = 0; i < pc; i++) if (g_segOn[i]) cnt++;
    return cnt;
}

void LoadTimeOverrides()
{
    for (int i = 0; i < 11; i++) { g_ovS[i] = -1; g_ovE[i] = -1; }
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    short b[22] = { 0 };
    DWORD sz = sizeof(b);
    if (RegQueryValueExW(k, L"TimeOv", NULL, NULL, (LPBYTE)b, &sz) == ERROR_SUCCESS && sz >= sizeof(b))
    {
        for (int i = 0; i < 11; i++) { g_ovS[i] = b[i]; g_ovE[i] = b[11 + i]; }
    }
    RegCloseKey(k);
}
void SaveTimeOverrides()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    short b[22];
    for (int i = 0; i < 11; i++) { b[i] = (short)g_ovS[i]; b[11 + i] = (short)g_ovE[i]; }
    RegSetValueExW(k, L"TimeOv", 0, REG_BINARY, (LPBYTE)b, sizeof(b));
    RegCloseKey(k);
}

// 自定义提醒的存取：注册表里塞成 "HH:MM|文字|daily|used;..." 一行字符串
void LoadShots()
{
    g_shots.clear();
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    static wchar_t buf[2048];
    DWORD sz = sizeof(buf), type = 0;
    if (RegQueryValueExW(k, L"Shots", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS && buf[0])
    {
        wchar_t* ctx = NULL;
        for (wchar_t* p = wcstok_s(buf, L";", &ctx); p; p = wcstok_s(NULL, L";", &ctx))
        {
            int h = 0, m = 0, dy = 1, us = 0;
            wchar_t tx[32] = { 0 };
            wchar_t* f[4] = { p, NULL, NULL, NULL };
            int nf = 1;
            for (wchar_t* q = p; *q && nf < 4; q++)
                if (*q == L'|') { *q = 0; f[nf++] = q + 1; }
            if (nf < 4) continue;
            if (swscanf_s(f[0], L"%d:%d", &h, &m) != 2) continue;
            if (h < 0 || h > 23 || m < 0 || m > 59) continue;
            wcsncpy_s(tx, f[1], _TRUNCATE);
            dy = _wtoi(f[2]);
            us = _wtoi(f[3]);
            OneShot hMain;
            hMain.hour = h; hMain.minute = m; hMain.daily = (dy != 0); hMain.used = (us != 0);
            wcscpy_s(hMain.text, tx[0] ? tx : L"时间到！");
            g_shots.push_back(hMain);
        }
    }
    RegCloseKey(k);
}
void SaveShots()
{
    wchar_t buf[2048] = { 0 };
    for (size_t i = 0; i < g_shots.size(); i++)
    {
        wchar_t one[128];
        swprintf_s(one, L"%02d:%02d|%s|%d|%d;", g_shots[i].hour, g_shots[i].minute,
                   g_shots[i].text, g_shots[i].daily ? 1 : 0, g_shots[i].used ? 1 : 0);
        wcscat_s(buf, one);
    }
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    if (buf[0])
        RegSetValueExW(k, L"Shots", 0, REG_SZ, (LPBYTE)buf, (DWORD)((wcslen(buf) + 1) * sizeof(wchar_t)));
    else
        RegDeleteValueW(k, L"Shots");
    RegCloseKey(k);
}

// Turbo 首次启动：把 V1.2 的配置搬一份过来
// （作息表/铃声/音量/主题这些都要；AI 相关的不要 —— Turbo 没有 AI 岛）
void MigrateFromV12()
{
    HKEY nk = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0,
                        KEY_READ | KEY_SET_VALUE, NULL, &nk, NULL) != ERROR_SUCCESS)
        return;

    // 已经有 TimeTable 说明搬过了（或者我已经在新版里设过），不再动
    DWORD t = 0, ts = sizeof(t);
    if (RegQueryValueExW(nk, L"TimeTable", NULL, NULL, (LPBYTE)&t, &ts) == ERROR_SUCCESS)
    {
        RegCloseKey(nk);
        return;
    }

    HKEY ok = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &ok) != ERROR_SUCCESS)
    {
        RegCloseKey(nk);          // 没装过 V1.2：什么都不用搬
        return;
    }

    static wchar_t nm[256];
    static BYTE buf[8192];
    for (DWORD i = 0; ; i++)
    {
        DWORD nl = 256, dl = sizeof(buf), ty = 0;
        if (RegEnumValueW(ok, i, nm, &nl, NULL, &ty, buf, &dl) != ERROR_SUCCESS) break;
        if (nm[0] == L'A' && nm[1] == L'i') continue;      // AiKey / AiHist* / AiKeyPurged 不搬
        RegSetValueExW(nk, nm, 0, ty, buf, dl);
    }
    RegCloseKey(ok);
    RegCloseKey(nk);
}

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
    // 初四冬季（2026-11-12 加早读 7:10 和午休 12:30，共 11 节）
    //   早读 7:10-7:55 / 第1-4 节同原表 / 午休 12:30-13:15 / 第5-9 节同原表
    static const int hC[]  = { 7, 8, 9,10,11,12,13,14,15,16,17};
    static const int mC[]  = {10, 0, 0,15,10,30,30,30,25,15, 5};
    static const int hCe[] = { 7, 8, 9,11,11,13,14,15,16,16,17};
    static const int mCe[] = {55,45,45, 0,55,15,15,15,10,55,45};
    const int* startH = tbl == 1 ? hB : (tbl == 2 ? hC : hA);
    const int* startM = tbl == 1 ? mB : (tbl == 2 ? mC : mA);
    const int* endH   = tbl == 1 ? hBe : (tbl == 2 ? hCe : hAe);
    const int* endM   = tbl == 1 ? mBe : (tbl == 2 ? mCe : mAe);
    for (int i=0;i<pc;i++)
    {
        if (!g_segOn[i]) continue;          // 被我删掉的段：不生成闹钟
        AlarmTime s,e;
        s.hour=startH[i];s.minute=startM[i];s.second=0;s.type=ALARM_TYPE_CLASS_START;s.enabled=true;s.period=i;
        e.hour=endH[i];e.minute=endM[i];e.second=0;e.type=ALARM_TYPE_CLASS_END;e.enabled=true;e.period=i;
        jj.push_back(s);
        jj.push_back(e);
    }
    // 应用我在「自定义」里改过的时间（-1 = 用默认），改完再排序
    for (int i = 0; i < pc; i++)
    {
        if (!g_segOn[i]) continue;
        for (auto& a : jj)
        {
            if (a.period != i) continue;
            int ov = (a.type == ALARM_TYPE_CLASS_START) ? g_ovS[i] : g_ovE[i];
            if (ov < 0) continue;
            a.hour = ov / 60;
            a.minute = ov % 60;
        }
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
    scrH = GetSystemMetrics(SM_CYSCREEN);
    bigW = B * ISLAND_WIDTH_PERCENT / 100;

    // 画布尺寸：留够过冲余量
    ComputeCanvasSize();   // 算画布，留够果冻过冲的地方

    // 注意：这里不再用 WS_EX_TRANSPARENT（那会让窗口完全不接收输入）。
    // 改成在 WM_NCHITTEST 里只让"岛形"范围内命中 → 其它区域照样穿透，效果一致但能收到长按/触控。
    DWORD exCommon = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

    // 只有一个分层窗口，全部自绘：Win10 / Win11 同一套逻辑，不做任何能力探测
    hMain = CreateWindowEx(WS_EX_LAYERED | exCommon, L"IslandAlarmWindow", L"mobile Turbo",
                            WS_POPUP, 0, 0, ISLAND_MIN_WIDTH, ISLAND_MIN_HEIGHT,
                            NULL, NULL, hInstance, NULL);

    islandX = (B - ISLAND_MIN_WIDTH) / 2;
    islandY = 0;
    islandW = ISLAND_MIN_WIDTH;
    islandH = ISLAND_MIN_HEIGHT;
    // 窗口位置固定不动，只改里面那块可见形状
    SetWindowPos(hMain, HWND_TOPMOST, cnv, 0, cw, ch, SWP_NOACTIVATE);
}

// ===== 卸载：删自启动项 + 删程序目录（跑着的 exe 自己删不掉，交给临时 bat）=====
static void RemoveAutostartEntry()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                     0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        // Turbo 的自启动值叫 IslandAlarmTurbo；删错名字的话卸载完还会被杀软/系统拉起来
        RegDeleteValue(hKey, L"IslandAlarmTurbo");
        RegCloseKey(hKey);
    }
}

void DoUninstall(bool silent)
{
    if (!silent)
    {
        int r = MessageBoxW(NULL,
            L"确定要卸载「mobile Turbo」吗？\n\n"
            L"将删除：\n"
            L"  • 开机自启动项\n"
            L"  • 程序目录（%LOCALAPPDATA%\\IslandAlarmTurbo）\n\n"
            L"你的设置（提醒节次 / 音量 / 主题 / 迷你岛等）会保留在注册表\n"
            L"HKCU\\Software\\IslandAlarmTurbo 中，如需要可手动删除。\n\n"
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
        // ★ 删的是 Turbo 自己的目录。原来这两行抄的主线路径（IslandAlarm），
        //   结果卸载 Turbo 会把同机装的主线 v1.2 整目录删掉，自己反倒留在原地（2026-11-15 修）
        fputs("del /f /q \"%LOCALAPPDATA%\\IslandAlarmTurbo\\islands.exe\" >nul 2>nul\r\n", f);
        fputs("if exist \"%LOCALAPPDATA%\\IslandAlarmTurbo\\islands.exe\" goto wait\r\n", f);
        fputs("rmdir /q \"%LOCALAPPDATA%\\IslandAlarmTurbo\" >nul 2>nul\r\n", f);
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
            L"• 开机自启动项已删除\n"
            L"• 程序文件将在约 1 秒后自动删除\n"
            L"• 设置保留在注册表 HKCU\\Software\\IslandAlarmTurbo",
            L"卸载完成", MB_OK | MB_ICONINFORMATION);
    }
    if (hMain) DestroyWindow(hMain);   // 触发 WM_DESTROY → 退出消息循环
}

// ===== 自安装：双击即装（拷到我目录并启动；不建快捷方式）=====
// ===== 内置资源（2026-10-16 加）=====
// 提醒音、放学音乐、专辑封面三样都编进了 exe（见 buildres/app.rc 里的 RCDATA）。
// 这样"只拷一个 exe"就能用，不用再在旁边带 mp3 / jpg。
// MCI 和 GDI+ 都只能读**磁盘上的文件**，所以启动时把它们释放到安装目录（已存在就不覆盖，
// 我自己换过的那份永远优先）。
// 资源号：256=alin.mp3  257=Home.mp3  258=Home.jpg（要跟 app.rc 保持一致）
static int AudResId(const wchar_t* name)
{
    if (_wcsicmp(name, L"alin.mp3") == 0) return 256;
    if (_wcsicmp(name, L"Home.mp3") == 0) return 257;
    if (_wcsicmp(name, L"Home.jpg") == 0) return 258;
    return 0;
}
static bool DumpResToFile(int id, const wchar_t* dst)
{
    if (id <= 0) return false;
    HRSRC hr = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!hr) return false;
    DWORD sz = SizeofResource(NULL, hr);
    HGLOBAL hg = LoadResource(NULL, hr);
    if (!sz || !hg) return false;
    const void* p = LockResource(hg);
    if (!p) return false;
    // 磁盘上已经有一份、大小也对 → 不重写（省一次 1MB 的写盘，也保住我换过的音频）
    WIN32_FILE_ATTRIBUTE_DATA fa = {0};
    if (GetFileAttributesExW(dst, GetFileExInfoStandard, &fa) &&
        fa.nFileSizeHigh == 0 && fa.nFileSizeLow == sz) return true;
    HANDLE h = CreateFileW(dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(h, p, sz, &wr, NULL) && wr == sz;
    CloseHandle(h);
    return ok;
}

// 读一个 exe 的文件版本（major.minor.build.revision 全取，64 位）
static DWORD64 ExeFileVersion(const wchar_t* path)
{
    DWORD64 ver = 0;
    DWORD sz = GetFileVersionInfoSizeW(path, NULL);
    if (!sz) return 0;
    std::vector<BYTE> buf(sz);
    if (!GetFileVersionInfoW(path, 0, sz, buf.data())) return 0;
    VS_FIXEDFILEINFO* fi = NULL;
    UINT len = 0;
    if (VerQueryValueW(buf.data(), L"\\", (void**)&fi, &len) && fi && len >= sizeof(VS_FIXEDFILEINFO))
        // 2026-11-05 修：只取 MS 的话，1.2.0.5 → 1.2.0.6 这种构建号变化认不出来，
        // 自安装会以为"不比现在新"从而不覆盖。MS 和 LS 一起拼成 64 位。
        ver = ((DWORD64)fi->dwFileVersionMS << 32) | (DWORD64)fi->dwFileVersionLS;
    return ver;
}

// 把正在跑的旧实例干掉。不这么做的话：安装目录的 exe 一直被占用，
// 新版本永远 CopyFile 失败 → 我双击新版却被单实例拦下，看到的还是旧版的岛。
static bool KillRunningInstance()
{
    HWND h = FindWindowW(L"IslandAlarmWindow", NULL);
    if (!h) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid || pid == GetCurrentProcessId()) return true;
    HANDLE hp = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!hp) return false;
    TerminateProcess(hp, 0);
    WaitForSingleObject(hp, 4000);
    CloseHandle(hp);
    return true;
}

// false = 已交接给新实例，本进程该退了
static bool EnsureInstalledOrRun()
{
    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t dir[MAX_PATH] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, dir))) return true;
    // ★ Turbo 用独立安装目录：V1.2 和 Turbo 是两个版本，装同一目录会互相覆盖
    wcscat_s(dir, L"\\IslandAlarmTurbo");
    wchar_t exe[MAX_PATH];
    swprintf_s(exe, L"%ls\\islands.exe", dir);

    bool fromInstalled = (_wcsicmp(self, exe) == 0);                     // 正从安装目录运行
    bool installed     = (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES);
    bool done = installed || fromInstalled;

    if (!done)
    {   // 首次运行 → 装一下
        CreateDirectoryW(dir, NULL);
        if (!CopyFileW(self, exe, FALSE)) return true;                   // 复制失败则按便携版运行
    }
    else if (installed && !fromInstalled)
    {
        // ★ 已装过一版：比版本。只有"手里这份更新"才替换。
        //   老逻辑是"有就跳过"，结果装了 v1.0 之后，以后所有版本都覆盖不上去，
        //   我双击新版 → 被单实例拦住 → 看到的永远是旧版（2026-10-29 踩到）。
        DWORD64 vSelf = ExeFileVersion(self);
        DWORD64 vInst = ExeFileVersion(exe);
        if (vSelf && vSelf > vInst)
        {
            if (KillRunningInstance())          // 先让旧实例退出（文件不占用才复制得动）
            {
                Sleep(250);
                if (CopyFileW(self, exe, FALSE))
                {
                    done = false;               // 覆盖成功 → 走下面的"启动安装目录那份"
                }
            }
        }
    }

    // 音频随行：每次启动都补一次，别只认首次安装。
    // 理由（踩过）：装了以后我才把 Home.mp3 放到 exe 旁边 —— 老逻辑在"已安装"那行就 return 了，
    // 永远搬不进去，放学音乐就一辈子不响。已存在的文件不覆盖，所以我自己换过的音频不会被冲掉。
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
                // 目标已经有了就别动 —— 我自己换过的音频/封面永远优先
                if (GetFileAttributesW(to) != INVALID_FILE_ATTRIBUTES) continue;
                // ① exe 旁边有这份文件 → 用它（这是"我想覆盖内置的"最直白的方式）
                if (GetFileAttributesW(from) != INVALID_FILE_ATTRIBUTES)
                {
                    CopyFileW(from, to, FALSE);
                    continue;
                }
                // ② 旁边没有 → 用编进 exe 的那份（只拷一个 exe 也能出声、也有封面）
                DumpResToFile(AudResId(aud[i]), to);
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

void AnimTick(IslandState& IS)
{
    ScopedLoadOnly scoped(IS);      // 载入本岛状态；结束（含提前 return）收回（不还原）
    if (islandMode == MODE_BIG_IN)
    {
        // 弹出：先胖一圈再弹回来
        float t = (float)animStep / AnimSteps(ANIMATION_STEPS);
        if (t > 1.0f) t = 1.0f;
        I = t;
        islandW  = (int)(ISLAND_MIN_WIDTH  + (bigW - ISLAND_MIN_WIDTH) * springW(t));
        islandH = (int)(ISLAND_MIN_HEIGHT + (bigH - ISLAND_MIN_HEIGHT) * springH(t));
        islandY      = (int)(ISLAND_FINAL_Y * springY(t));
        islandX      = (B - islandW) / 2;
        animStep++;
        if (animStep > AnimSteps(ANIMATION_STEPS))
        {
            animStep = 0;
            islandW  = bigW;
            islandH = bigH;                 // 普通闹钟 80 / 播放器 124
            islandY      = ISLAND_FINAL_Y;
            islandX      = (B - bigW) / 2;
            I = 1.0f;
            islandMode = MODE_BIG_HOLD;
            hideAtMs = NowMs() + DISPLAY_DURATION_MS;
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_MINI_IN)
    {
        // 常驻小岛弹出（与大岛同一套果冻逻辑）
        PopTick(MINI_WIDTH, MINI_HEIGHT, AnimSteps(MINI_STEPS), &animStep);
        if (animStep > AnimSteps(MINI_STEPS))
        {
            animStep = 0;
            islandW = MINI_WIDTH; islandH = MINI_HEIGHT;
            islandY = ISLAND_FINAL_Y; islandX = (B - islandW) / 2; I = 1.0f;
            islandMode = MODE_MINI_HOLD;                       // 进入常驻
            vv = 1.0f;
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_TO_MINI)
    {
        // 缩小变身：从**起始尺寸**（ae/an/ao）过渡到**目标尺寸**（gw/gh/gy），
        // 带果冻（和"展开大岛"同一套弹簧）；步数用 ANIMATION_STEPS 与展开大岛同速率。
        // 目标是"小岛"还是"迷你岛"由此刻的 gw/gh/gy/gJ 决定 —— 大岛→小岛 / 大岛→迷你岛 / 小岛→迷你岛共用这里
        float t = (float)oo / AnimSteps(ANIMATION_STEPS);
        if (t > 1.0f) t = 1.0f;
        islandW = (int)(ae + (gw - ae) * springW(t));
        islandH = (int)(an + (gh - an) * springH(t));
        islandY = (int)(ao + (gy - ao) * springY(t));
        islandX = (B - islandW) / 2;
        vv = vv0 + (vt - vv0) * t;
        sf = sf0 + (sf1 - sf0) * t;
        oo++;
        if (oo > AnimSteps(ANIMATION_STEPS))
        {
            oo = 0;
            islandW = gw; islandH = gh; islandY = gy; islandX = (B - islandW) / 2;
            vv = vt; sf = sf1;
            islandMode = gJ;
            if (islandMode == MODE_NOTCH_HOLD) { nt = true; na = NowMs(); nv = 255; ai = -1; }   // 落到迷你岛
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_TO_BIG)
    {
        // 放大变身：从起始尺寸（ae/an/ao）接到目标尺寸（gw/gh/gy），同样带果冻，文字交叉渐变。
        // 大岛目标时 gJ=3（定格显示大岛）；小岛目标时 gJ=7 —— 小岛→大岛 / 迷你岛→大岛 / 迷你岛→小岛共用这里
        float t = (float)oo / MINI_STEPS;
        if (t > 1.0f) t = 1.0f;
        islandW = (int)(ae + (gw - ae) * springW(t));
        islandH = (int)(an + (gh - an) * springH(t));
        islandY = (int)(ao + (gy - ao) * springY(t));
        islandX = (B - islandW) / 2;
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
            islandW = gw; islandH = gh; islandY = gy; islandX = (B - islandW) / 2;
            vv = vt; sf = sf1;
            if (plyMix) { plyMix = false; ta = 255; plyA = 255.0f; }   // 过渡结束，播放器完全接管
            islandMode = gJ;
            if (islandMode == MODE_BIG_HOLD) hideAtMs = NowMs() + DISPLAY_DURATION_MS;   // 变身完 → 照常定格显示大岛
            if (islandMode == MODE_MINI_HOLD) { vv = 1.0f; }                       // 落到小岛
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_NOTCH_IN)
    {
        // 迷你岛弹出：从屏幕边缘上方"落"下来，宽高一起长，带一点果冻
        float t = (float)animStep / MINI_STEPS;
        if (t > 1.0f) t = 1.0f;
        islandW = (int)(ISLAND_MIN_WIDTH  + (NOTCH_WIDTH  - ISLAND_MIN_WIDTH ) * springW(t));
        islandH = (int)(ISLAND_MIN_HEIGHT + (NOTCH_HEIGHT - ISLAND_MIN_HEIGHT) * springH(t));
        islandY = (int)((NOTCH_Y + NOTCH_HEIGHT) * springY(t)) - NOTCH_HEIGHT;   // 从屏幕上方落到位（离顶 NOTCH_Y）
        islandX = (B - islandW) / 2;
        vv = 1.0f; sf = 0.0f;                                                // 迷你岛也是四角圆角
        animStep++;
        if (animStep > AnimSteps(MINI_STEPS))
        {
            animStep = 0;
            islandW = NOTCH_WIDTH; islandH = NOTCH_HEIGHT; islandY = NOTCH_Y; islandX = (B - islandW) / 2;
            nt = true; nv = 255; na = NowMs(); ai = -1;
            islandMode = MODE_NOTCH_HOLD;
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_NOTCH_OUT)
    {
        // 迷你岛收回：反向走一遍（缩回去 + 滑出屏幕边缘上方）
        float t = 1.0f - (float)P / MINI_STEPS;
        if (t < 0.0f) t = 0.0f;
        islandW = (int)(ISLAND_MIN_WIDTH  + (NOTCH_WIDTH  - ISLAND_MIN_WIDTH ) * springW(t));
        islandH = (int)(ISLAND_MIN_HEIGHT + (NOTCH_HEIGHT - ISLAND_MIN_HEIGHT) * springH(t));
        islandY = (int)((NOTCH_Y + NOTCH_HEIGHT) * springY(t)) - NOTCH_HEIGHT;   // 反向：缩回屏幕上方
        islandX = (B - islandW) / 2;
        sf = 0.0f;
        P++;
        if (P > MINI_STEPS)
        {
            P = 0; nt = false; sf = 0.0f; islandMode = MODE_IDLE;
            {   // 刚收掉迷你岛，若此刻正处课间 → 把倒计时的常驻小岛补上
                LONGLONG t0 = 0;
                if (BreakInfoActive(&t0)) { pp = true; rr = t0; bb = true; animStep = 0; vv = 1.0f; islandMode = MODE_MINI_IN; }
            }
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_HOLIDAY)
    {
        // 特殊日期两段式（**大岛形状一动不动，只有文字在变**）：
        //   ① 先维持"下课了 + 时间" ② 文字渐渐淡出 ③ 换文案 ④ 「国庆节快乐」淡入 ⑤ 停一会儿再收回
        hs++;
        if (hs <= 24)        ta = 255 - 255 * hs / 24;                             // 淡出 ≈0.5s
        else if (hs == 25) { hh = true; wcscpy_s(gg, HolidayGreet()); ta = 0; FitBigWidth(NULL, gg, NULL); }    // 换文案
        else if (hs <= 55)   ta = 255 * (hs - 25) / 30;                            // 淡入 ≈0.6s
        else if (hs <= 190)  ta = 255;                                             // 停留 ≈3s
        else if (ply)      { ta = 255; hh = false; islandMode = MODE_BIG_HOLD; ShowPlayerIsland(); }    // 祝福演完 → 接手播音乐
        else               { ta = 255; hh = false; P = 0; islandMode = MODE_BIG_OUT; }                 // 收回
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_MINI_OUT)
    {
        // 常驻小岛收回（与大岛收回同一套平滑逻辑）
        float t = (float)P / SLIDEOUT_STEPS;
        if (t > 1.0f) t = 1.0f;
        I = 1.0f - t;
        float k = 1.0f - easeInOutCubic(t);
        islandY = (int)(ISLAND_FINAL_Y * k);
        islandW = (int)(MINI_WIDTH  * k);
        islandH = (int)(MINI_HEIGHT * k);
        islandX = (B - islandW) / 2;
        P++;
        if (P > SLIDEOUT_STEPS)
        {
            P = 0;
            islandMode = MODE_IDLE;
            ShowWindow(hMain, SW_HIDE);
            bb = false; pp = false; vv = 0.0f;
            af = false; ww = 0;
            islandW = ISLAND_MIN_WIDTH; islandH = ISLAND_MIN_HEIGHT; islandY = 0; I = 0.0f;
            return;
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else if (islandMode == MODE_BIG_OUT)
    {
        // 收回：平滑收缩上移，不抖
        float t = (float)P / SLIDEOUT_STEPS;
        if (t > 1.0f) t = 1.0f;
        I = 1.0f - t;
        float p = easeInOutCubic(t);            // 起步与收尾速度都为 0 → 全程顺滑，无突兀
        float k = 1.0f - p;
        islandY      = (int)(ISLAND_FINAL_Y * k);
        islandW  = (int)(bigW * k);
        islandH = (int)(bigH * k);
        islandX      = (B - islandW) / 2;
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
                animStep = 0; vv = 1.0f;
                sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;   // 小岛＝普通圆角
                islandMode = MODE_MINI_IN; R = NowMs();
                return;
            }
            if (ns)                       // 没有小岛等着、但迷你岛开着 → 挂成迷你岛
            {
                nt = true; nv = 255; na = NowMs(); ai = -1; animStep = 0;
                hh = false;                           // 别带着消息标志进迷你岛
                vv = 1.0f; vv0 = 1.0f; vt = 1.0f; sf = 0.0f;   // 迷你岛＝四角圆角（悬浮样式）
                islandMode = MODE_NOTCH_IN; R = NowMs();
                return;
            }
            islandMode = MODE_IDLE;
            ShowWindow(hMain, SW_HIDE);
            L = false;
            hh = false;
            islandW  = ISLAND_MIN_WIDTH;
            islandH = ISLAND_MIN_HEIGHT;
            islandY = 0;
            I = 0.0f;
            return;
        }
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
}

// 弹出前：定位 + 显示
void PrepareBackdropBeforeShow()
{
    EnsureBuffers();
    SetWindowPos(hMain, HWND_TOPMOST, cnv, 0, cw, ch, SWP_NOACTIVATE);
    ShowWindow(hMain, SW_SHOWNOACTIVATE);
    RegisterTouchWindow(hMain, 0);   // 触屏：让岛能收到触控（没触屏也无害）
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
        // 我要求"每个课间都显示倒计时，但不必每个课间都响铃"，所以没打钩的时段也照样显示倒计时
        if (minNow >= endMin && minNow < nextMin)
        {
            *outTargetMs = base + (LONGLONG)nextMin*60*1000;
            return true;
        }
    }
    return false;
}

// 当前正在上第几节？给出这节课的开始/结束时刻（毫秒，今天零点起算）。
// 迷你岛底部那条进度线用它 —— BreakInfoActive 只给"下个节点"，算不出"这节上到哪了"。
bool ClassInProgress(LONGLONG* startMs, LONGLONG* endMs)
{
    SYSTEMTIME st; GetLocalTime(&st);
    int nowSec = st.wHour*3600 + st.wMinute*60 + st.wSecond;
    int minNow = st.wHour*60 + st.wMinute;
    LONGLONG base = NowMs() - (LONGLONG)nowSec*1000 - (LONGLONG)st.wMilliseconds;
    for (int i = 0; i < pc; i++)
    {
        int sh, sm, eh, em;
        if (!GetPeriodTime(i, ALARM_TYPE_CLASS_START, &sh, &sm)) continue;
        if (!GetPeriodTime(i, ALARM_TYPE_CLASS_END,   &eh, &em)) continue;
        int s2 = sh*60 + sm, e2 = eh*60 + em;
        if (e2 <= s2) continue;
        if (minNow >= s2 && minNow < e2)
        {
            *startMs = base + (LONGLONG)s2 * 60000;
            *endMs   = base + (LONGLONG)e2 * 60000;
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

    // 手动倒计时：跟课间无关，只看自己那个 rr
    if (g_cdManual)
    {
        if (NowMs() >= rr) { g_cdManual = false; HideMiniIsland(); }
        return;
    }

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
    if (need) UpdateIslandDisplay(hMain, 1.0f, 255);
}

// 全局轮询（钩子不可用时的兜底）：鼠标/被提升为鼠标的触控都能查到
void PollGesture()
{
    static bool prevDown = false;
    POINT pt = { 0, 0 };
    bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (mouseDown) GetCursorPos(&pt); else pt = g_touchPt;
    bool down = mouseDown || g_touchDown;   // 触控按下也算"按下"
    if (down && !prevDown)
    {
        if (IslandHitTest(pt))
        {
            if (!xx) { xx = true; yy = NowMs(); af = true; }
        }
        else
        {
            int cxm = B / 2;
            if (islandMode == MODE_IDLE && pt.y <= 40 && pt.x > cxm - bigW / 2 && pt.x < cxm + bigW / 2)
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

    if (xx && (islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD) && now - yy >= LONGPRESS_MS)
    {
        xx = false; af = false;                     // 长按关闭
        HideMiniIsland();
    }
    if (zz && now - ag >= SWIPE_HOLD_MS)
    {
        POINT pt; GetCursorPos(&pt);
        if (islandMode == MODE_IDLE && pt.y - ah >= SWIPE_DY)
        {
            zz = false;                            // 下滑 → 展开迷你岛（实时时间归它）
            ns = true; SaveNotchCfg();
            ShowNotch();
        }
        else if (now - ag > SWIPE_HOLD_MS + 5000) zz = false;   // 兜底，别一直挂着
    }
    if (!xx && !zz) KillTimer(hMain, 12);                  // 没活儿了就停心跳
}


void StartTimerThread()
{
    kk = CreateEvent(NULL, FALSE, FALSE, NULL);
    ll = CreateWaitableTimer(NULL, TRUE, NULL);
    mm = CreateThread(NULL, 0, TimerThreadProc, NULL, 0, NULL);
}

BOOL GetNextAlarmTime(FILETIME* pNextTime, AlarmType* pType, int* pPeriod)
{
    if (jj.empty()) return FALSE;
    SYSTEMTIME stNowUtc; GetSystemTime(&stNowUtc);
    FILETIME ftNowUtc; SystemTimeToFileTime(&stNowUtc, &ftNowUtc);
    ULONGLONG minDiff = ULLONG_MAX;
    bool found = false;
    AlarmType nextType = ALARM_TYPE_SINGLE;
    int nextPeriod = -1;
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
        if (diff < minDiff) { minDiff=diff; nextFT=ftTarget; nextType=alarm.type; nextPeriod=alarm.period; found=true; }
    }
    if (found) { *pNextTime=nextFT; *pType=nextType; if (pPeriod) *pPeriod=nextPeriod; return TRUE; }
    return FALSE;
}

DWORD WINAPI TimerThreadProc(LPVOID lpParam)
{
    while (true)
    {
        FILETIME ftNext; AlarmType nextType; int nextPeriod = -1;
        if (!GetNextAlarmTime(&ftNext, &nextType, &nextPeriod))
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
        PostMessage(hMain, WM_ALARM_TRIGGER, (WPARAM)nextType, (LPARAM)nextPeriod);
        if (WaitForSingleObject(kk, 2000) == WAIT_OBJECT_0) return 0;
    }
    return 0;
}

// 找同目录的 alin.mp3（安装时会一并拷到我目录）
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
// 《回家》的播放时刻：固定 19:30（2026-10-13 我要求 —— 从"放学那一下"挪出来，放学恢复打铃）
// 音乐岛（《回家》）触发时刻，按作息表分开（2026-11-04 我定的）：
//   初四夏季(0) 19:30 不变 / 冬季(1) 默认不弹 / 初四冬季(2) 提前到 18:30
const int BGM_HOUR = 19;
const int BGM_MIN  = 30;
static int BgmHour()
{
    if (tbl == 2) return 18;
    if (tbl == 1) return -1;      // 不弹
    return 19;
}
static int BgmMinute()
{
    if (tbl == 2) return 30;
    return 30;
}
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
    if (g_examOn) return;                    // 考试模式：放学音乐也不放
    StartAmp();          // 顺便把底部半圆的耳朵打开（MCI 拿不到 PCM，靠 WASAPI 回环）
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
    mciSendStringW(L"play bgm from 0 notify", NULL, 0, hMain);
}

void AudioPlay(AlarmType type, int vol)
{
    if (!fuck) return;
    if (!nn[0]) AudioResolvePath();
    if (!nn[0]) return;
    mciSendStringW(L"close ilsaudio", NULL, 0, NULL);
    wchar_t cmd[MAX_PATH + 64];
    swprintf_s(cmd, L"open \"%ls\" type mpegvideo alias ilsaudio", nn);
    if (mciSendStringW(cmd, NULL, 0, NULL) != 0) return;
    // vol < 0 表示"用全局默认"（试听走这条）；否则用这一节课自己的音量
    if (vol < 0) vol = (type == ALARM_TYPE_CLASS_END) ? FUCK : ab;
    int v = vol * 10;
    if (v < 0) v = 0;
    if (v > 1000) v = 1000;
    swprintf_s(cmd, L"setaudio ilsaudio volume to %d", v);
    mciSendStringW(cmd, NULL, 0, NULL);
    mciSendStringW(L"play ilsaudio from 0", NULL, 0, NULL);
}
void AudioStop() { mciSendStringW(L"close ilsaudio", NULL, 0, NULL); }

// 独立试听：从文件时长的 pct% 处开始放。固定 80% 音量，不读也不改任何铃音配置。
static void AudioPreview(int pct)
{
    if (g_examOn) return;                    // 考试模式：试听同样禁掉
    if (!nn[0]) AudioResolvePath();
    if (!nn[0]) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    mciSendStringW(L"close ilstry", NULL, 0, NULL);
    wchar_t cmd[MAX_PATH + 64];
    swprintf_s(cmd, L"open \"%ls\" type mpegvideo alias ilstry", nn);
    if (mciSendStringW(cmd, NULL, 0, NULL) != 0) return;
    wchar_t buf[64] = { 0 };
    mciSendStringW(L"status ilstry length", buf, 64, NULL);
    int len = _wtoi(buf);
    if (len < 1000) len = 30000;                 // 拿不到时长就按 30 秒算
    int from = len * pct / 100;
    swprintf_s(cmd, L"setaudio ilstry volume to 800");
    mciSendStringW(cmd, NULL, 0, NULL);
    swprintf_s(cmd, L"play ilstry from %d", from);
    mciSendStringW(cmd, NULL, 0, NULL);
}

// 播放别占着 UI 线程：MCI 的 open/play 会同步等解码器就绪（实测卡 ~200ms），
// 这 200ms 正好把"岛弹出 / 变身"的第一帧挡住，看着就是"动画卡了一下才动"。
// 丢到后台线程放 → 画面和声音同时开始。
struct AudioJob { AlarmType type; int vol; };
static DWORD WINAPI TryThreadProc(LPVOID p) { AudioPreview((int)(INT_PTR)p); return 0; }
static void AudioPreviewAsync(int pct)
{
    HANDLE h = CreateThread(NULL, 0, TryThreadProc, (LPVOID)(INT_PTR)pct, 0, NULL);
    if (h) CloseHandle(h);
}

static DWORD WINAPI AudioThreadProc(LPVOID p)
{
    AudioJob* cw = (AudioJob*)p;
    AudioPlay(cw->type, cw->vol);
    delete cw;
    return 0;
}
void AudioPlayAsync(AlarmType type)          // 试听：用全局音量
{
    if (g_examOn) return;                    // 考试模式：一点声音都不许有
    AudioJob* cw = new AudioJob{ type, -1 };
    HANDLE h = CreateThread(NULL, 0, AudioThreadProc, cw, 0, NULL);
    if (h) CloseHandle(h);      // 不 Join：让它自己放完
    else delete cw;
}
// 到点响铃：带这一节课自己的音量
void AudioPlayAsyncVol(AlarmType type, int vol)
{
    if (g_examOn) return;                    // 考试模式：上下课铃也一起禁掉
    AudioJob* cw = new AudioJob{ type, vol };
    HANDLE h = CreateThread(NULL, 0, AudioThreadProc, cw, 0, NULL);
    if (h) CloseHandle(h);
    else delete cw;
}

void LoadAudioCfg()
{
    HKEY hKey; DWORD v = 0, sz = sizeof(v);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
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
        DWORD mu = 0x7FF, md = 0x7FF; sz = sizeof(DWORD);
        if (RegQueryValueEx(hKey, L"BellUpMask", NULL, NULL, (LPBYTE)&mu, &sz) != ERROR_SUCCESS) mu = 0x7FF;
        sz = sizeof(DWORD);
        if (RegQueryValueEx(hKey, L"BellDownMask", NULL, NULL, (LPBYTE)&md, &sz) != ERROR_SUCCESS) md = 0x7FF;
        for (int i = 0; i < 11; i++)
        {
            bellUp[i] = (mu & (1u << i)) != 0;
            bellDn[i] = (md & (1u << i)) != 0;
        }
        // 22 个音量打包成一个 22 字节的二进制值（不加 22 个键）
        BYTE bv[22] = { 0 };
        DWORD bsz = sizeof(bv);
        if (RegQueryValueEx(hKey, L"BellVols", NULL, NULL, bv, &bsz) == ERROR_SUCCESS && bsz >= 22)
        {
            for (int i = 0; i < 11; i++)
            {
                bellUpVol[i] = bv[i];
                bellDnVol[i] = bv[11 + i];
                if (bellUpVol[i] < 0) bellUpVol[i] = 0; if (bellUpVol[i] > 100) bellUpVol[i] = 100;
                if (bellDnVol[i] < 0) bellDnVol[i] = 0; if (bellDnVol[i] > 100) bellDnVol[i] = 100;
            }
        }
        RegCloseKey(hKey);
    }
    AudioResolvePath();
}
void SaveAudioCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
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
        DWORD mu = 0, md = 0;
        for (int i = 0; i < 11; i++)
        {
            if (bellUp[i]) mu |= (1u << i);
            if (bellDn[i]) md |= (1u << i);
        }
        RegSetValueEx(hKey, L"BellUpMask",   0, REG_DWORD, (LPBYTE)&mu, sizeof(mu));
        RegSetValueEx(hKey, L"BellDownMask", 0, REG_DWORD, (LPBYTE)&md, sizeof(md));
        BYTE bv[22] = { 0 };
        for (int i = 0; i < 11; i++) { bv[i] = (BYTE)bellUpVol[i]; bv[11 + i] = (BYTE)bellDnVol[i]; }
        RegSetValueEx(hKey, L"BellVols", 0, REG_BINARY, bv, 22);
        RegCloseKey(hKey);
    }
}

void LoadSkyCfg()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD v = 38, sz = sizeof(v);
        if (RegQueryValueEx(hKey, L"SkyBright", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
        {
            skyBr = (int)v;
            // 旧默认 38 是"为了不抢视野压暗一半"的档位，现在改成以天空为底，一律提到全亮
            if (skyBr <= 38) skyBr = 100;
        }
        // DebugHud 必须在这里读 —— 之前写成在 RegCloseKey 之后读，用的是野句柄，
        // 静默失败，HUD 开关永远读不出来（2026-11-05 另一个 AI 审出来的）。
        DWORD hd = 0, hz = sizeof(hd);
        if (RegQueryValueExW(hKey, L"DebugHud", NULL, NULL, (LPBYTE)&hd, &hz) == ERROR_SUCCESS)
            g_hud = (hd != 0);
        // 动画速度：同样必须在这个块里读（同样的野句柄坑，绝不能再踩一次）
        DWORD av = 100, asz = sizeof(av);
        if (RegQueryValueExW(hKey, L"AnimSpeed", NULL, NULL, (LPBYTE)&av, &asz) == ERROR_SUCCESS)
        {
            int v2 = (int)av;
            if (v2 < 50) v2 = 50;
            if (v2 > 200) v2 = 200;
            g_animSpeed = v2 / 100.0f;
        }
        RegCloseKey(hKey);
    }
    if (skyBr < 0) skyBr = 0;
    if (skyBr > 100) skyBr = 100;
    if (skyBr > 100) skyBr = 100;
    HKEY h2;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &h2) == ERROR_SUCCESS)
    {
        DWORD a = 0, b = 0, sz = sizeof(DWORD);
        if (RegQueryValueEx(h2, L"SkyAnimReplay", NULL, NULL, (LPBYTE)&a, &sz) == ERROR_SUCCESS) skyReplay = (a != 0);
        sz = sizeof(DWORD);
        if (RegQueryValueEx(h2, L"NotchAlpha", NULL, NULL, (LPBYTE)&b, &sz) == ERROR_SUCCESS) notchA = (int)b;
        sz = sizeof(DWORD);
        DWORD c2 = 1;
        if (RegQueryValueEx(h2, L"SkyOn", NULL, NULL, (LPBYTE)&c2, &sz) == ERROR_SUCCESS) skyEn = (c2 != 0);
        sz = sizeof(DWORD); DWORD sr = 0xFFFFFFFF;
        if (RegQueryValueEx(h2, L"SunRise", NULL, NULL, (LPBYTE)&sr, &sz) == ERROR_SUCCESS) sunRiseMin = (int)sr;
        sz = sizeof(DWORD); DWORD ss = 0xFFFFFFFF;
        if (RegQueryValueEx(h2, L"SunSet", NULL, NULL, (LPBYTE)&ss, &sz) == ERROR_SUCCESS) sunSetMin = (int)ss;
        sz = sizeof(DWORD); DWORD sd = 0;
        if (RegQueryValueEx(h2, L"SunDay", NULL, NULL, (LPBYTE)&sd, &sz) == ERROR_SUCCESS) sunDay = (int)sd;
        RegCloseKey(h2);
    }
    if (notchA < 5) notchA = 5;
    if (notchA > 100) notchA = 100;
}
void SaveSkyCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = (DWORD)skyBr;
        RegSetValueEx(hKey, L"SkyBright", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        DWORD r = skyReplay ? 1 : 0;
        RegSetValueEx(hKey, L"SkyAnimReplay", 0, REG_DWORD, (LPBYTE)&r, sizeof(r));
        DWORD na2 = (DWORD)notchA;
        RegSetValueEx(hKey, L"NotchAlpha", 0, REG_DWORD, (LPBYTE)&na2, sizeof(na2));
        DWORD so2 = skyEn ? 1 : 0;
        RegSetValueEx(hKey, L"SkyOn", 0, REG_DWORD, (LPBYTE)&so2, sizeof(so2));
        if (sunDay && sunRiseMin >= 0 && sunSetMin > sunRiseMin)
        {
            DWORD r2 = (DWORD)sunRiseMin, s2 = (DWORD)sunSetMin, d2 = (DWORD)sunDay;
            RegSetValueEx(hKey, L"SunRise", 0, REG_DWORD, (LPBYTE)&r2, sizeof(r2));
            RegSetValueEx(hKey, L"SunSet",  0, REG_DWORD, (LPBYTE)&s2, sizeof(s2));
            RegSetValueEx(hKey, L"SunDay",  0, REG_DWORD, (LPBYTE)&d2, sizeof(d2));
        }
        RegCloseKey(hKey);
    }
}


static void SavePerfCfg()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS)
    {
        DWORD v = (gFrameMs > 20) ? 1 : 0;
        RegSetValueExW(k, L"LowPerf", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(k);
    }
}

static void LoadPerfCfg()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        DWORD v = 0, sz = sizeof(v);
        if (RegQueryValueExW(k, L"LowPerf", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS && v)
            gFrameMs = 33;
        RegCloseKey(k);
    }
}


// 第 i 个时段是否勾选（按 period 字段找；老代码用 i*2 取下标，排序后就对不上了 —— 那是个真 bug）
// 妈了个巴子的，早年这里按下标 i*2 找勾选状态，排序之后全错位了 —— 我还以为自己没勾
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
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
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

// 大岛上方文字（2026-11-04 可自定义，默认还是"上课了/下课了"）
wchar_t g_msgStart[32] = L"上课了";
wchar_t g_msgEnd[32]   = L"下课了";

void LoadMsgCfg()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    wchar_t b[32] = { 0 };
    DWORD sz = sizeof(b), type = 0;
    if (RegQueryValueExW(k, L"MsgStart", NULL, &type, (LPBYTE)b, &sz) == ERROR_SUCCESS && b[0])
        wcscpy_s(g_msgStart, b);
    sz = sizeof(b);
    if (RegQueryValueExW(k, L"MsgEnd", NULL, &type, (LPBYTE)b, &sz) == ERROR_SUCCESS && b[0])
        wcscpy_s(g_msgEnd, b);
    RegCloseKey(k);
}
void SaveMsgCfg()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, L"MsgStart", 0, REG_SZ, (LPBYTE)g_msgStart, (DWORD)((wcslen(g_msgStart) + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"MsgEnd",   0, REG_SZ, (LPBYTE)g_msgEnd,   (DWORD)((wcslen(g_msgEnd)   + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

// 大岛上的"事件标签"文案（画岛和量宽度都用它，避免两处写不一样）
void AlarmLabel(AlarmType t, wchar_t* out)
{
    if (t == ALARM_TYPE_CLASS_START)      wcscpy_s(out, 32, g_msgStart);
    else if (t == ALARM_TYPE_CLASS_END)   wcscpy_s(out, 32, g_msgEnd);
    else                                  wcscpy_s(out, 32, g_customText);
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
    bigW = need;
    ComputeCanvasSize();
}

// 播放器下面那行"长按关闭播放器"只显示一小会儿，之后自己淡掉（一直挂着太吵）
LONGLONG plyHintUntil = 0;

// 让大岛以"音乐播放器"样式出现并常驻。
// 三种来路都要照顾到：
//   ① 大岛正定格（islandMode= MODE_BIG_HOLD）→ 形状不动，内容换成播放器，并把定格时间顶到很久以后
//   ② 小岛/迷你岛在显示（islandMode= MODE_MINI_IN/7/11/12）→ 直接放大变身成大岛
//   ③ 什么都没显示（islandMode= MODE_IDLE）→ 照常从最小尺寸弹出
//   ④ 节日两段式正在演（islandMode= MODE_HOLIDAY）→ 先别抢，等它演完（见 AnimTick 里 islandMode= MODE_HOLIDAY 的收尾）
void ShowPlayerIsland()
{
    if (g_examOn) return;           // 考试模式：播放器岛也不许弹
    bool grow = (bigH != PLAYER_H || bigW != PLAYER_W);            // 现在的尺寸是不是"播放器尺寸"
    ply = true;
    plyA = 255.0f; plyMix = false;                              // 默认完全不透明；要走过渡再单独置
    ww = 255; plyHintUntil = NowMs() + 8000;                    // 露一下"长按关闭播放器"
    if (islandMode == MODE_HOLIDAY) return;                                        // 节日两段式优先级更高（它收尾时会再叫一次）
    CoverLoad();                                                // 每次进场重读一次封面（我可能刚放进来）
    bigH = PLAYER_H;                                            // 播放器比普通大岛高
    bigW = PLAYER_W;                                               // 也比普通大岛长
    ComputeCanvasSize();

    if (islandMode == MODE_BIG_IN || islandMode == MODE_TO_BIG) { hideAtMs = NowMs() + PLAYER_LONG; return; }// 正在弹出/变大，内容自己会变成播放器
    if (islandMode == MODE_BIG_HOLD && !grow)                                        // 已经是大岛且尺寸已对 → 原地换内容
    {
        hideAtMs = NowMs() + PLAYER_LONG;
        UpdateIslandDisplay(hMain, 1.0f, 255);
        return;
    }
    if (islandMode == MODE_IDLE)                                                 // 什么都没显示 → 从最小尺寸弹出
    {
        islandW = ISLAND_MIN_WIDTH; islandH = ISLAND_MIN_HEIGHT; islandY = 0; islandX = (B - islandW) / 2;
        vv0 = 0.0f; vt = 0.0f; vv = 0.0f;
        sf0 = 0.0f; sf1 = 0.0f; sf = 0.0f;
        animStep = 0; I = 0.0f; islandMode = MODE_BIG_IN;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    // 其余情况（大岛定格里但尺寸还是普通大岛 / 小岛 / 迷你岛）→ 走放大变身，长成播放器那个长方形。
    // 同时开"接场过渡"：前 40% 把「下课了」淡出去，后 65% 播放器再淡进来（见 AnimTick 的 islandMode= MODE_TO_BIG）。
    ae = islandW; an = islandH; ao = islandY;                                     // 从当前尺寸接着长，接缝处不跳
    sf0 = sf; sf1 = 0.0f; nt = false;
    gw = bigW; gh = bigH; gy = ISLAND_FINAL_Y; gJ = 3;
    vv0 = 0.0f; vt = 0.0f; oo = 0; islandMode = MODE_TO_BIG;
    // ★ 这条变身路以前漏了 hideAtMs：hideAtMs 还是上一轮的（比如 AI 岛收回时设的 +5 秒），
    //   早过期 → 下一帧就被当"该收了"，islandMode 变 4 把 ply 清掉，播放器刚出来就没了。
    hideAtMs = NowMs() + PLAYER_LONG;
    plyMix = true; plyA = 0.0f; ta = 255;
    R = NowMs();
}

// 收掉播放器：停音乐 + 收回大岛
void ClosePlayer()
{
    BgmStop();
    ply = false;
    bgmPause = false;
    if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD) { P = 0; islandMode = MODE_BIG_OUT; R = NowMs(); }
    else UpdateIslandDisplay(hMain, 1.0f, 255);
}

void PlayerTick()
{
    LONGLONG now = NowMs();
    // 按了暂停后 5 秒还没继续播 → 自动把播放器收掉（我要求）
    if (ply && bgmPause && plyPauseAt && now - plyPauseAt >= PLAYER_PAUSE_CLOSE_MS)
    {
        ClosePlayer();
        return;
    }
    if (now < plyHintUntil)
    {
        if (ww < 255) { ww = 255; UpdateIslandDisplay(hMain, 1.0f, 255); }
        return;
    }
    if (ww > 0)
    {
        ww -= 26; if (ww < 0) ww = 0;
        UpdateIslandDisplay(hMain, 1.0f, 255);
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
    UpdateIslandDisplay(hMain, 1.0f, 255);
    return true;
}

void LoadTimeTable()
{
    HKEY hKey; DWORD v = 1, sz = sizeof(v);
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hKey, L"TimeTable", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) tbl = (int)v;
        RegCloseKey(hKey);
    }
    if (tbl < 0 || tbl > 2) tbl = 1;                       // 认不出就用"冬季"
    pc = (tbl == 0 || tbl == 2) ? 11 : 9;   // 初四冬季也扩成 11（早读 + 午自习）
    ff = (tbl == 0) ? ffA : (tbl == 1 ? ffB : ffC);
}
void SaveTimeTable()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
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
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
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
    wcscpy_s(nid.szTip, L"mobile Turbo");
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

// 今天下一个"上课"时刻（只看 CLASS_START，不看下课/自定义提醒）。
// 点迷你岛时用它做"下节预告"——教室里最常问的就是"下节什么课、几点"。
bool NextClassStart(int* hh, int* mm, int* period)
{
    SYSTEMTIME st; GetLocalTime(&st);
    int curMin = st.wHour * 60 + st.wMinute;
    int best = -1;
    for (const auto& a : jj)
    {
        if (a.type != ALARM_TYPE_CLASS_START || !a.enabled) continue;
        int m = a.hour * 60 + a.minute;
        if (m > curMin && (best < 0 || m < best)) best = m;
    }
    if (best < 0) return false;
    *hh = best / 60; *mm = best % 60;
    if (period)
    {
        int idx = 0;
        for (const auto& a : jj)
        {
            if (a.type != ALARM_TYPE_CLASS_START || !a.enabled) continue;
            if (a.hour * 60 + a.minute == best) { *period = a.period; break; }
        }
        (void)idx;
    }
    return true;
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
HWND sPeriod[11] = {0};           // 时段勾选框（＝到点弹岛）
HWND sBellUp[11] = {0};           // 每节课的上课铃勾选
HWND sBellDn[11] = {0};           // 每节课的下课铃勾选
HWND sBellUpVol[11] = {0};        // 每节课的上课铃音量滑块
HWND sBellDnVol[11] = {0};        // 每节课的下课铃音量滑块
HWND sBellUpPct[11] = {0};        // 上课铃音量百分比
HWND sBellDnPct[11] = {0};        // 下课铃音量百分比
HWND sTryPos = NULL;              // 试听进度条（0~100%）
HWND sTryPct = NULL;              // 试听位置百分比
HWND sMsgStartEdit = NULL;
HWND sMsgEndEdit = NULL;
HWND sPerCombo = NULL, sPerStart = NULL, sPerEnd = NULL;
HWND sShotTime = NULL, sShotText = NULL, sShotKind = NULL, sShotList = NULL;
HWND sColTarget = NULL, sPal[16] = { 0 }, sColOn = NULL;
HWND sHud = NULL;
HWND sAnimSpd = NULL, sAnimSpdPct = NULL;
HWND sCdMin = NULL, sCdLabel = NULL;
HWND sTbl[3] = {0};               // 作息表：0=初四夏季 1=冬季 2=初四冬季
HWND sVol[3] = {0};               // 音量滑块：0=上课铃 1=下课铃 2=《回家》
HWND sVolPct[3] = {0};            // 音量百分比
HWND sBgmOn = NULL;               // 放学播放《回家》
HWND sAudioOn = NULL, sHoliday = NULL, sMini = NULL;
HWND sLowPerf = NULL;                    // 低配模式勾选框
HWND sSkyBr = NULL, sSkyBrPct = NULL;   // 天空亮度滑块 + 百分比
HWND sNotchA = NULL, sNotchAPct = NULL; // 迷你岛静止透明度滑块 + 百分比
HWND sSkyRe = NULL, sSkyGo = NULL;      // 天空动画：重播 / 直接变
HWND sSkyMode[2] = {0};                 // 底板模式：0=天气 1=纯色
HFONT sFont = NULL;
#define ID_NAV_BASE 1100       // 左侧导航按钮：1100..1104
#define ID_BELLUP_BASE 1120    // 每节课的上课铃勾选：1120..1130
#define ID_BELLDN_BASE 1140    // 每节课的下课铃勾选：1140..1150
#define ID_BELLUP_VOL_BASE 1200   // 每节课上课铃音量滑块：1200..1210
#define ID_BELLDN_VOL_BASE 1220   // 每节课下课铃音量滑块：1220..1230
static HWND sStatus = NULL;    // 导航栏底部常驻状态
static std::wstring g_checkMsg;   // 打开设置时自检出来的问题（每行一条，空=全好）
#define ID_SET_EXAM     1240   // 考试模式开关（作息表页最上面）
#define ID_SET_EXAM_PWD 1241   // 自定义页：二级密码输入框
#define ID_SET_EXAM_PWD_SAVE 1242
#define ID_SET_LOWPERF 1085   // 低配模式勾选
#define ID_SET_CLOSE 1090
#define ID_SET_PREV0 1091
#define ID_SET_PREV1 1092
#define ID_SET_TRY   1170   // 独立试听：从进度条选的位置开始放
#define ID_MSG_START_EDIT 1171   // 自定义：上课文字
#define ID_MSG_END_EDIT   1172   // 自定义：下课文字
#define ID_MSG_SAVE       1173   // 保存文字
#define ID_MSG_RESET      1174   // 恢复默认
#define ID_PERIOD_COMBO   1180   // 时间段下拉
#define ID_PERIOD_START   1181   // 该段开始时间
#define ID_PERIOD_END     1182   // 该段结束时间
#define ID_PERIOD_APPLY   1183   // 应用
#define ID_PERIOD_RESET   1184   // 恢复该段默认
#define ID_PERIOD_DEL     1185   // 删除该段
#define ID_PERIOD_ALL     1186   // 恢复所有段
#define ID_SET_HUD        1199   // 调试 HUD 开关
#define ID_ANIM_SPD        1189   // 动画速度滑块（避开已用的）
#define ID_COL_ON         1176   // 自定义颜色总开关（默认关＝强制金色）
#define ID_COL_TARGET     1177   // 文字颜色：改哪一项
#define ID_COL_RESET      1178   // 恢复默认色
#define ID_COL_RESET_ALL  1179   // 四个颜色一起恢复
#define ID_PAL_BASE       1300   // 16 个色块：1300..1315
#define ID_CD_MIN         1196   // 一次性倒计时：分钟
#define ID_CD_LABEL       1197   // 一次性倒计时：文字前缀
#define ID_CD_START       1198   // 立即开始
#define ID_SHOT_TIME      1190   // 自定义提醒：时间
#define ID_SHOT_TEXT      1191   // 自定义提醒：文字
#define ID_SHOT_KIND      1192   // 自定义提醒：每天/一次性
#define ID_SHOT_ADD       1193   // 添加
#define ID_SHOT_LIST      1194   // 列表
#define ID_SHOT_DEL       1195   // 删除选中
#define ID_SET_BGM_ON 1093
#define ID_SET_PREV2 1094
#define ID_SET_SKY_ANIM_RE 1095   // 天空动画：每次重播
#define ID_SET_SKY_ANIM_GO 1096   // 天空动画：直接变过去

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

// ===== GUI 分页（2026-10-27）=====
// 左侧导航 5 项，右侧内容按页显示。控件创建时按 gSetPage 自动登记到对应页，
// 切页就是批量 ShowWindow —— 不动任何业务逻辑，风险最小。
#define SET_PAGES 6
static int                gSetPage = -1;      // -1 = 不属于任何页（导航/状态栏自己）
static int                gCurPage = 0;
static std::vector<HWND>  gPageCtl[SET_PAGES];
static HWND               sNav[SET_PAGES] = { 0 };

// ===== 切页渐变：内容区截图覆盖层渐隐 =====
// 为什么要这么绕：Win32 子控件没有独立透明度（给控件加 WS_EX_LAYERED 会让 STATIC/BUTTON
// 背景变黑块，试过，已回滚）。整窗淡出又像"先消失再出现"，我明确否决。
// 所以改成：切页前把右侧内容区截一张图 → 用一个 layered 覆盖窗口把这张图盖上去 →
// 底下立刻换成新页 → 覆盖层 alpha 从 255 降到 0，旧内容渐隐、新内容逐渐露出来。
// 左侧导航和状态栏完全不动。
static HWND    sOvWnd = NULL;
static HDC     sOvDC = NULL;
static HBITMAP sOvBmp = NULL, sOvOld = NULL;
static int     sOvW = 0, sOvH = 0, sOvX = 0, sOvY = 0, sOvA = 0;
static void*   sOvBits = NULL;      // DIB 像素，截完要把 alpha 刷成 255
static int     sNavW = 190;      // 内容区左边（客户区坐标），布局时同步
// ⚠️ 2026-10-27：截图覆盖层这套先停用 —— overlay 始终不显示（换过窗口类、补过 alpha 都没跑通），
//    不能为了一个渐变把每次切页都拖慢（截图 + 刷 alpha 是实打实的开销）。
//    当前是直接切页。真要做"只有右侧渐变"，正路是把内容区改成自绘控件，见待办 N4。
static bool    sOvOK = false;

static void OvEnsure(int w, int h)
{
    if (!sOvOK || w <= 0 || h <= 0) { sOvOK = false; return; }
    if (sOvWnd && sOvW == w && sOvH == h) return;

    if (sOvWnd)   { DestroyWindow(sOvWnd); sOvWnd = NULL; }
    if (sOvDC)    { DeleteDC(sOvDC);  sOvDC = NULL; }
    if (sOvBmp)   { DeleteObject(sOvBmp); sOvBmp = NULL; }

    // STATIC 是系统控件类，拿来当顶层 popup 不可靠 → 注册一个自己的空窗口类
    static bool regOv = false;
    if (!regOv)
    {
        regOv = true;
        WNDCLASSW wc = { 0 };
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"MobileFadeWnd";
        RegisterClassW(&wc);
    }
    sOvWnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                             L"MobileFadeWnd", L"", WS_POPUP, 0, 0, w, h,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!sOvWnd) { sOvOK = false; return; }

    HDC sc = GetDC(NULL);
    sOvDC = CreateCompatibleDC(sc);
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    sOvBits = NULL;
    sOvBmp = CreateDIBSection(sc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    sOvBits = bits;
    ReleaseDC(NULL, sc);
    if (!sOvBmp) { sOvOK = false; return; }
    sOvOld = (HBITMAP)SelectObject(sOvDC, sOvBmp);
    sOvW = w; sOvH = h;
}

static void OvShow(int alpha)
{
    if (!sOvWnd || !sOvOK) return;
    SIZE sz = { sOvW, sOvH };
    POINT src = { 0, 0 }, dst = { sOvX, sOvY };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 };   // AlphaFormat=0 → 用整体 alpha
    UpdateLayeredWindow(sOvWnd, NULL, &dst, &sz, sOvDC, &src, 0, &bf, ULW_ALPHA);
    ShowWindow(sOvWnd, SW_SHOWNOACTIVATE);
    SetWindowPos(sOvWnd, stw, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

// 设置窗口打开时跑一遍轻量自检：只查"能确定"的，结果挂在左下角。
// 运行时行为（动画、点击）在这里看不到，那些归 check.exe。
static void RunSelfCheck()
{
    g_checkMsg.clear();

    // ① 提醒音
    if (!nn[0]) AudioResolvePath();
    if (!nn[0]) g_checkMsg += L"提醒音文件没找到\n";

    // ③ 注册表
    {
        HKEY hk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hk) != ERROR_SUCCESS)
            g_checkMsg += L"配置文件读不到\n";
        else RegCloseKey(hk);
    }

    // ④ 音量数组长度（长度不对会让所有节次回到默认音量）
    {
        HKEY hk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hk) == ERROR_SUCCESS)
        {
            DWORD sz = 0;
            if (RegQueryValueExW(hk, L"BellVols", NULL, NULL, NULL, &sz) == ERROR_SUCCESS && sz != 22)
                g_checkMsg += L"每节课音量数据异常\n";
            RegCloseKey(hk);
        }
    }

    // ⑤ 铃开关高位残留（换过作息表会有）
    {
        HKEY hk;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hk) == ERROR_SUCCESS)
        {
            DWORD mu = 0, sz = sizeof(mu);
            if (RegQueryValueExW(hk, L"BellUpMask", NULL, NULL, (LPBYTE)&mu, &sz) == ERROR_SUCCESS)
            {
                DWORD valid = (pc >= 11) ? 0x7FF : ((1u << pc) - 1);
                if (mu & ~valid)
                {
                    // 不是错误：多半是之前在节数更多的作息表里全勾过，换到少节数的表后
                    // 高位就空着了。写清楚有几个、说明不影响使用，别让人以为是坏了。
                    int extra = 0;
                    for (int i = pc; i < 11; i++) if (mu & (1u << i)) extra++;
                    wchar_t eb[96];
                    swprintf_s(eb, L"多出 %d 个铃声开关（超出当前 %d 个时间段），不影响使用\n", extra, pc);
                    g_checkMsg += eb;
                }
            }
            RegCloseKey(hk);
        }
    }
}

static void ScrollApply(int off, int offX = 0);   // 定义在后面（滚动那套）
static void ScrollCollect();                        // 同上：重算自定义页的内容范围

static void PageApply(int p)
{
    for (int i = 0; i < SET_PAGES; i++)
        for (HWND h : gPageCtl[i]) if (h) ShowWindow(h, (i == p) ? SW_SHOW : SW_HIDE);
    if (p == 1)
        for (int k = 0; k < 11; k++)
        {
            // 换表后多出来的节次：三列都得藏，不然会剩下孤零零的铃勾选
            if (sPeriod[k]) ShowWindow(sPeriod[k], (k < pc && g_segOn[k]) ? SW_SHOW : SW_HIDE);
            if (sBellUp[k]) ShowWindow(sBellUp[k], (k < pc && g_segOn[k]) ? SW_SHOW : SW_HIDE);
            if (sBellDn[k]) ShowWindow(sBellDn[k], (k < pc && g_segOn[k]) ? SW_SHOW : SW_HIDE);
            if (sBellUpVol[k]) ShowWindow(sBellUpVol[k], (k < pc && g_segOn[k]) ? SW_SHOW : SW_HIDE);
            if (sBellDnVol[k]) ShowWindow(sBellDnVol[k], (k < pc && g_segOn[k]) ? SW_SHOW : SW_HIDE);
            if (sBellUpPct[k]) ShowWindow(sBellUpPct[k], (k < pc && g_segOn[k]) ? SW_SHOW : SW_HIDE);
            if (sBellDnPct[k]) ShowWindow(sBellDnPct[k], (k < pc && g_segOn[k]) ? SW_SHOW : SW_HIDE);
        }
    gCurPage = p;
    if (p == 2)
    {
        // 2026-11-14：以前这里只调 ScrollApply(0)，而它只设 POS、不设 RANGE，
        // 于是自定义页的滚动条范围一直是 0 —— 那颗条根本不出现，
        // 底部那行「一次性倒计时 …开始」永远够不着（用户报的"进度条没了"）。
        ScrollCollect();                 // 重新算内容范围并同步给系统滚动条
        ScrollApply(0);                  // 每次进这页都从顶部开始
    }
    else
    {
        // 2026-11-14 用户要求"滚动条要一直常驻"：
        // 以前这里把范围清零，切到别的页整条就消失了。
        // 现在给一个"滑块占满"的范围 —— 条一直在，只是这一页没超出、拖不动。
        RECT cr = { 0 };
        GetClientRect(stw, &cr);
        int vh = cr.bottom > 2 ? cr.bottom : 640;
        int vw = cr.right  > 2 ? cr.right  : 980;
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0; si.nPos = 0;
        si.nMax = vh; si.nPage = (UINT)vh;
        SetScrollInfo(stw, SB_VERT, &si, TRUE);
        si.nMax = vw; si.nPage = (UINT)vw;
        SetScrollInfo(stw, SB_HORZ, &si, TRUE);
    }
    for (int i = 0; i < SET_PAGES; i++)
        if (sNav[i]) {
            SendMessageW(sNav[i], BM_SETSTATE, (i == p) ? TRUE : FALSE, 0);
            InvalidateRect(sNav[i], NULL, TRUE);    // 自绘导航：选中态变了要重画
        }
}

static void SetSwitchPage(int p)
{
    if (p < 0 || p >= SET_PAGES) return;
    if (p == gCurPage && sOvA == 0)
    {
        for (int i = 0; i < SET_PAGES; i++)
            if (sNav[i]) SendMessageW(sNav[i], BM_SETSTATE, (i == p) ? TRUE : FALSE, 0);
        return;
    }

    // 1) 先把"右侧内容区当前长什么样"截下来
    RECT cr; GetClientRect(stw, &cr);
    int cx = sNavW + 16, cy = 0;
    int cw = cr.right - cx - 16, ch = cr.bottom;
    if (cw < 8 || ch < 8) cw = 0;

    bool shot = false;
    if (cw > 0 && sOvOK)
    {
        POINT pt = { cx, cy };
        ClientToScreen(stw, &pt);
        OvEnsure(cw, ch);
        if (sOvOK)
        {
            UpdateWindow(stw);                       // 保证截到的是最终画面
            HDC sdc = GetDC(stw);
            if (sdc)
            {
                BitBlt(sOvDC, 0, 0, cw, ch, sdc, cx, cy, SRCCOPY);
                ReleaseDC(stw, sdc);
                // ⚠️ BitBlt 拷出来的 alpha 字节是 0，UpdateLayeredWindow 会当成全透明什么都不显示。
                //    必须手动刷成 255（整体透明度交给 SourceConstantAlpha 控制）。
                if (sOvBits)
                {
                    BYTE* bp = (BYTE*)sOvBits;
                    size_t n2 = (size_t)cw * ch;
                    for (size_t q = 0; q < n2; q++) bp[q * 4 + 3] = 255;
                }
                shot = true;
            }
            sOvX = pt.x; sOvY = pt.y;
        }
    }

    // 2) 底下立刻换页
    PageApply(p);

    // 3) 覆盖层渐隐，露出新页
    if (shot)
    {
        sOvA = 235;
        OvShow(sOvA);
        SetTimer(stw, 2, 16, NULL);                  // 约 12 帧 ≈ 0.19 秒
    }
}

static void PageFadeTick()
{
    if (!sOvWnd) { KillTimer(stw, 2); return; }
    sOvA -= 22;
    if (sOvA <= 0)
    {
        sOvA = 0;
        ShowWindow(sOvWnd, SW_HIDE);
        KillTimer(stw, 2);
        return;
    }
    OvShow(sOvA);
}

static HWND SetCtl(const wchar_t* cls, const wchar_t* txt, DWORD style,
                   int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, stw, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (c && cls != L"msctls_trackbar32") SendMessageW(c, WM_SETFONT, (WPARAM)SetUiFont(), TRUE);
    if (c && gSetPage >= 0 && gSetPage < SET_PAGES) gPageCtl[gSetPage].push_back(c);
    return c;
}

// 下拉只列"还在用"的段；每项用 CB_SETITEMDATA 记住真实段号，避免索引错位
static void RefreshPeriodCombo()
{
    if (!sPerCombo) return;
    int curSel = (int)SendMessageW(sPerCombo, CB_GETCURSEL, 0, 0);
    int curSeg = (curSel >= 0) ? (int)SendMessageW(sPerCombo, CB_GETITEMDATA, curSel, 0) : -1;
    SendMessageW(sPerCombo, CB_RESETCONTENT, 0, 0);
    int newSel = -1;
    for (int i = 0; i < pc; i++)
    {
        if (!g_segOn[i]) continue;
        int idx = (int)SendMessageW(sPerCombo, CB_ADDSTRING, 0, (LPARAM)ff[i]);
        SendMessageW(sPerCombo, CB_SETITEMDATA, idx, i);
        if (i == curSeg) newSel = idx;
    }
    if (newSel < 0) newSel = 0;
    SendMessageW(sPerCombo, CB_SETCURSEL, newSel, 0);
}
// 下拉当前选中的真实段号（没有返回 -1）
static int ComboSeg()
{
    if (!sPerCombo) return -1;
    int idx = (int)SendMessageW(sPerCombo, CB_GETCURSEL, 0, 0);
    if (idx < 0) return -1;
    return (int)SendMessageW(sPerCombo, CB_GETITEMDATA, idx, 0);
}

// 把「时间段设置」那两个框填成当前选中段的真实时间
static void FillPeriodTimes()
{
    if (!sPerCombo || !sPerStart || !sPerEnd) return;
    int i = ComboSeg();
    if (i < 0 || i >= pc) return;
    int sh = 0, sm = 0, eh = 0, em = 0;
    GetPeriodTime(i, ALARM_TYPE_CLASS_START, &sh, &sm);
    GetPeriodTime(i, ALARM_TYPE_CLASS_END,   &eh, &em);
    wchar_t b[16];
    swprintf_s(b, L"%02d:%02d", sh, sm); SetDlgItemTextW(stw, ID_PERIOD_START, b);
    swprintf_s(b, L"%02d:%02d", eh, em); SetDlgItemTextW(stw, ID_PERIOD_END,   b);
}

// 把 g_shots 刷进列表控件
static void RefreshShotList()
{
    if (!sShotList) return;
    SendMessageW(sShotList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_shots.size(); i++)
    {
        wchar_t line[96];
        swprintf_s(line, L"%02d:%02d   %s   %s",
                   g_shots[i].hour, g_shots[i].minute,
                   g_shots[i].text,
                   g_shots[i].daily ? L"每天" : (g_shots[i].used ? L"一次性（已响过）" : L"一次性"));
        SendMessageW(sShotList, LB_ADDSTRING, 0, (LPARAM)line);
    }
}

// "08:05" → 485；格式不对返回 false
static bool ParseHM(const wchar_t* s, int* outMin)
{
    int h = -1, m = -1;
    if (!s || swscanf_s(s, L"%d:%d", &h, &m) != 2) return false;
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    *outMin = h * 60 + m;
    return true;
}

// ===== 页内滚动（T6，2026-11-04）=====
// Win32 原生控件不随容器滚动，只能在滚动时把每个控件整体 SetWindowPos 挪位。
// 只对「自定义」页生效（那页内容会越来越长）。
struct ScrollCtl { HWND h; int x0, y0, w, hgt; };
static std::vector<ScrollCtl> g_scCtl;
static int g_scOff  = 0;       // 纵向（0=顶部）
static int g_scOffX = 0;       // 横向（0=最左），暂时不用
static int g_scMax  = 0;       // 纵向最大
static int g_scMaxX = 0;       // 横向最大，暂时不用
static int g_scDragY = -1;     // 触摸/鼠标拖动的起点（-1 = 没在拖）

// 记录「自定义」页所有控件的原始位置（建窗时调一次）
static void ScrollCollect()
{
    g_scCtl.clear();
    g_scOff = 0;
    if (!stw) return;
    RECT cr; GetClientRect(stw, &cr);
    int bottom = 0;
    for (HWND h : gPageCtl[2])
    {
        if (!h) continue;
        RECT r; GetWindowRect(h, &r);
        POINT p = { r.left, r.top };
        ScreenToClient(stw, &p);
        ScrollCtl c;
        c.h = h; c.x0 = p.x; c.y0 = p.y;
        c.w = r.right - r.left; c.hgt = r.bottom - r.top;
        g_scCtl.push_back(c);
        if (p.y + c.hgt > bottom) bottom = p.y + c.hgt;
    }
    // 可视区：底部留 12px 余量
    int viewH = cr.bottom - 12;
    g_scMax = bottom - viewH;
    if (g_scMax < 0) g_scMax = 0;

    int right = 0;
    for (const auto& c : g_scCtl)
        if (c.x0 + c.w > right) right = c.x0 + c.w;
    g_scMaxX = right - (cr.right - 8);
    if (g_scMaxX < 0) g_scMaxX = 0;

    // 同步系统滚动条：范围 = 内容总尺寸，页 = 可视尺寸
    {
        SCROLLINFO si = { sizeof(si) };
        si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin   = 0;
        si.nMax   = right;                 // 横向内容总宽
        si.nPage  = cr.right;              // 可视宽
        si.nPos   = g_scOffX;
        SetScrollInfo(stw, SB_HORZ, &si, TRUE);

        si.nMax   = bottom;                // 纵向内容总高
        si.nPage  = cr.bottom;
        si.nPos   = g_scOff;
        SetScrollInfo(stw, SB_VERT, &si, TRUE);
    }
}

// 把「自定义」页整体上移/下移 off 像素
static void ScrollApply(int off, int offX)
{
    if (off < 0) off = 0;
    if (off > g_scMax) off = g_scMax;
    if (offX < 0) offX = 0;
    if (offX > g_scMaxX) offX = g_scMaxX;
    g_scOff = off; g_scOffX = offX;
    if (stw)
    {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_POS;
        si.nPos  = offX;  SetScrollInfo(stw, SB_HORZ, &si, TRUE);
        si.nPos  = off;   SetScrollInfo(stw, SB_VERT, &si, TRUE);
    }
    // 2026-11-14：滚动时"所有控件先消失再出现"的闪屏，就是这行造成的。
    //   RDW_ALLCHILDREN 会连每个子控件一起擦掉重画，且窗口没 WS_CLIPCHILDREN，
    //   父窗口擦背景时把控件区域也涂了一遍 → 肉眼能看到"唰"一下全空。
    //   现在：一次批量移动（DeferWindowPos，避免逐个重排），然后只让父窗口重画
    //   **没被控件盖住的那几条**（WS_CLIPCHILDREN 自动裁掉控件区域）。
    {
        HDWP dwp = BeginDeferWindowPos((int)g_scCtl.size());
        for (const auto& c : g_scCtl)
        {
            if (dwp) dwp = DeferWindowPos(dwp, c.h, NULL, c.x0 - offX, c.y0 - off, 0, 0,
                                          SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            else SetWindowPos(c.h, NULL, c.x0 - offX, c.y0 - off, 0, 0,
                              SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (dwp) EndDeferWindowPos(dwp);
    }
    RedrawWindow(stw, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

// 触摸/鼠标拖动滚动（2026-11-04）
// 为什么要子类化：Win32 里子控件的鼠标消息**不会冒泡**给父窗口，所以"在页面上拖"
// 只能靠给控件装子类。只装 STATIC —— 它们占的面积最大、本身不处理鼠标，
// 装上去不会干扰按钮点击/滑条拖动/输入框选字。
static POINT g_scDrag0 = { 0, 0 };
static int   g_scDragOff  = 0;
static int   g_scDragOffX = 0;
static bool  g_scDragging = false;

// 2026-11-13 改成"先按住、超过阈值才算拖动"：
//   ① 鼠标按下不再立刻 return 0（否则按钮点不了）
//   ② 加 WM_POINTERDOWN/UPDATE/UP —— 触控拖动走的是这套，只处理 WM_MOUSEMOVE 手指根本拖不动
static POINT g_scDownPt = {0,0};
static bool  g_scPotential = false;

LRESULT CALLBACK ScrollSub(HWND h, UINT m, WPARAM w, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    switch (m)
    {
    case WM_LBUTTONDOWN:
    case WM_POINTERDOWN:
    {
        if (m == WM_LBUTTONDOWN) GetCursorPos(&g_scDrag0);
        else { g_scDrag0.x = GET_X_LPARAM(lp); g_scDrag0.y = GET_Y_LPARAM(lp); ClientToScreen(h, &g_scDrag0); }
        g_scDownPt   = g_scDrag0;
        g_scDragOff  = g_scOff;
        g_scDragOffX = g_scOffX;
        g_scDragging = false;          // 先不拖，等超过阈值
        g_scPotential = true;
        SetCapture(h);
        return (m == WM_LBUTTONDOWN) ? DefSubclassProc(h, m, w, lp) : 0;   // 鼠标往下传，按钮能响应点击
    }
    case WM_MOUSEMOVE:
    case WM_POINTERUPDATE:
    {
        if (g_scPotential || g_scDragging)
        {
            POINT p;
            if (m == WM_MOUSEMOVE) GetCursorPos(&p);
            else { p.x = GET_X_LPARAM(lp); p.y = GET_Y_LPARAM(lp); ClientToScreen(h, &p); }
            int dx = p.x - g_scDownPt.x;
            int dy = p.y - g_scDownPt.y;
            if (!g_scDragging && (dx > 6 || dx < -6 || dy > 6 || dy < -6))
                g_scDragging = true;   // 超过 6px 才认成拖动，避免点击被吞
            if (g_scDragging)
            {
                ScrollApply(g_scDragOff - dy, g_scDragOffX - dx);
                return 0;
            }
        }
        break;
    }
    case WM_LBUTTONUP:
    case WM_POINTERUP:
        g_scPotential = false;
        g_scDragging  = false;
        ReleaseCapture();
        return (m == WM_LBUTTONUP) ? DefSubclassProc(h, m, w, lp) : 0;
    case WM_CAPTURECHANGED:
        g_scPotential = false;
        g_scDragging  = false;
        break;
    }
    return DefSubclassProc(h, m, w, lp);
}

// 给「自定义」页里的 STATIC 全部装上拖动子类（建窗时调一次）
static void InstallScrollDrag()
{
    for (const auto& c : g_scCtl)
    {
        // 所有控件都装：用户手指随便按哪都能拖（靠 6px 阈值区分点击和拖动）
        SetWindowSubclass(c.h, ScrollSub, 1, 0);
    }
}

// 下拉框的项高：默认 16px 手指根本点不中，统一撑到 26
// （触摸键盘不用我们管 —— Windows 自己在 EDIT 获得焦点时会弹）
static void MakeComboTouchFriendly(HWND c)
{
    if (!c) return;
    SendMessageW(c, CB_SETITEMHEIGHT, (WPARAM)-1, 26);   // 收起时的框高
    SendMessageW(c, CB_SETITEMHEIGHT, 0, 26);            // 展开后每一项的高度
}

// 列表（LISTBOX）的触摸拖动滚动（2026-11-04）
// 原来的问题：列表只能拖右边那根十几像素的滚动条，手指根本抓不住。
// 做法：按住拖动 → 按项高换算成行数 → LB_SETTOPINDEX；
//       没拖动（位移 < 6px）→ 当成普通点击，选中手指下面那一项。
LRESULT CALLBACK ListScrollSub(HWND h, UINT m, WPARAM w, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    static POINT base = { 0, 0 };
    static int   top0 = 0;
    static bool  dragging = false, moved = false;

    switch (m)
    {
    case WM_LBUTTONDOWN:
        GetCursorPos(&base);
        top0 = (int)SendMessageW(h, LB_GETTOPINDEX, 0, 0);
        dragging = true;
        moved = false;
        SetCapture(h);
        return 0;                       // 先吞掉，拖动判定完再决定要不要当点击

    case WM_MOUSEMOVE:
        if (dragging)
        {
            POINT p; GetCursorPos(&p);
            int dy = p.y - base.y;
            if (!moved && (dy > 6 || dy < -6)) moved = true;
            if (moved)
            {
                int ih = (int)SendMessageW(h, LB_GETITEMHEIGHT, 0, 0);
                if (ih <= 0) ih = 18;
                // 手往上滑（dy<0）→ 内容往下走 → topindex 变大
                int cnt = -dy / ih;
                SendMessageW(h, LB_SETTOPINDEX, top0 + cnt, 0);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (dragging)
        {
            dragging = false;
            ReleaseCapture();
            if (!moved)
            {
                // 没拖动：手指按在哪一项就选哪一项
                POINT p; GetCursorPos(&p);
                ScreenToClient(h, &p);
                LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, MAKELPARAM(p.x, p.y));
                if (HIWORD(r) == 0)
                    SendMessageW(h, LB_SETCURSEL, LOWORD(r), 0);
            }
        }
        return 0;

    case WM_CAPTURECHANGED:
        dragging = false;
        break;
    }
    return DefSubclassProc(h, m, w, lp);
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
        // 文字/勾选状态照常刷（跟当前在哪一页无关），**只有显隐**受页影响：
        // 不加页判断的话，SetSyncControls 在 PageApply 之后跑会把整列点回可见；
        // 但把文字也一起套进去，切页时就再也不会设置节次名了（2026-11-04 两次都踩到）
        if (i < pc)
        {
            SetWindowTextW(sPeriod[i], ff[i]);
            SendMessageW(sPeriod[i], BM_SETCHECK, PeriodEnabled(i) ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        ShowWindow(sPeriod[i], (gCurPage == 1 && i < pc && g_segOn[i]) ? SW_SHOW : SW_HIDE);
        // 两个铃：跟着总开关走 —— 关掉时只是灰掉，不影响"到点弹岛"那一列
        if (sBellUp[i])
        {
            ShowWindow(sBellUp[i], (gCurPage == 1 && i < pc && g_segOn[i]) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellUp[i], BM_SETCHECK, bellUp[i] ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(sBellUp[i], fuck ? TRUE : FALSE);
        }
        if (sBellDn[i])
        {
            ShowWindow(sBellDn[i], (gCurPage == 1 && i < pc && g_segOn[i]) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellDn[i], BM_SETCHECK, bellDn[i] ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(sBellDn[i], fuck ? TRUE : FALSE);
        }
        if (sBellUpVol[i])
        {
            ShowWindow(sBellUpVol[i], (gCurPage == 1 && i < pc && g_segOn[i]) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellUpVol[i], TBM_SETPOS, TRUE, bellUpVol[i]);
            EnableWindow(sBellUpVol[i], fuck ? TRUE : FALSE);
        }
        if (sBellUpPct[i])
        {
            ShowWindow(sBellUpPct[i], (gCurPage == 1 && i < pc && g_segOn[i]) ? SW_SHOW : SW_HIDE);
            wchar_t pb2[16]; swprintf_s(pb2, L"%d%%", bellUpVol[i]);
            SetWindowTextW(sBellUpPct[i], pb2);
            EnableWindow(sBellUpPct[i], fuck ? TRUE : FALSE);
        }
        if (sBellDnVol[i])
        {
            ShowWindow(sBellDnVol[i], (gCurPage == 1 && i < pc && g_segOn[i]) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellDnVol[i], TBM_SETPOS, TRUE, bellDnVol[i]);
            EnableWindow(sBellDnVol[i], fuck ? TRUE : FALSE);
        }
        if (sBellDnPct[i])
        {
            ShowWindow(sBellDnPct[i], (gCurPage == 1 && i < pc && g_segOn[i]) ? SW_SHOW : SW_HIDE);
            wchar_t pb3[16]; swprintf_s(pb3, L"%d%%", bellDnVol[i]);
            SetWindowTextW(sBellDnPct[i], pb3);
            EnableWindow(sBellDnPct[i], fuck ? TRUE : FALSE);
        }
    }
    if (sAudioOn)  SendMessageW(sAudioOn,  BM_SETCHECK, fuck ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sHoliday)  SendMessageW(sHoliday,  BM_SETCHECK, hon  ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sMini)     SendMessageW(sMini,     BM_SETCHECK, ns   ? BST_CHECKED : BST_UNCHECKED, 0);
    wchar_t b[16];
    if (sVol[0])    SendMessageW(sVol[0], TBM_SETPOS, TRUE, ab);
    if (sVol[1])    SendMessageW(sVol[1], TBM_SETPOS, TRUE, FUCK);
    if (sVol[2])    SendMessageW(sVol[2], TBM_SETPOS, TRUE, bgmV);
    if (sVolPct[0]) { swprintf_s(b, L"%d%%", ab);   SetWindowTextW(sVolPct[0], b); }
    if (sVolPct[1]) { swprintf_s(b, L"%d%%", FUCK); SetWindowTextW(sVolPct[1], b); }
    if (sVolPct[2]) { swprintf_s(b, L"%d%%", bgmV); SetWindowTextW(sVolPct[2], b); }
    if (sBgmOn)     SendMessageW(sBgmOn, BM_SETCHECK, bgmOn ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sLowPerf) SendMessageW(sLowPerf, BM_SETCHECK,
                               (WPARAM)(gFrameMs > 20 ? BST_CHECKED : BST_UNCHECKED), 0);
    if (sSkyBr)     SendMessageW(sSkyBr, TBM_SETPOS, TRUE, skyBr);
    if (sHud)       SendMessageW(sHud, BM_SETCHECK, g_hud ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sAnimSpd)   SendMessageW(sAnimSpd, TBM_SETPOS, TRUE, (LPARAM)(int)(g_animSpeed * 100.0f + 0.5f));
    if (sAnimSpdPct)
    {
        wchar_t b[16];
        swprintf_s(b, L"%.1fx", g_animSpeed);
        SetWindowTextW(sAnimSpdPct, b);
    }
    if (sSkyBrPct)  { swprintf_s(b, L"%d%%", skyBr); SetWindowTextW(sSkyBrPct, b); }
    if (sNotchA)    SendMessageW(sNotchA, TBM_SETPOS, TRUE, notchA);
    if (sNotchAPct) { swprintf_s(b, L"%d%%", notchA); SetWindowTextW(sNotchAPct, b); }
    // 自定义页：把当前文字填进去（每次同步都刷，切页/保存后都能看到最新值）
    if (sColOn) SendMessageW(sColOn, BM_SETCHECK, Col().on ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sMsgStartEdit) SetWindowTextW(sMsgStartEdit, g_msgStart);
    if (sMsgEndEdit)   SetWindowTextW(sMsgEndEdit,   g_msgEnd);
    if (sSkyRe)     SendMessageW(sSkyRe, BM_SETCHECK,  skyReplay ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sSkyGo)     SendMessageW(sSkyGo, BM_SETCHECK, !skyReplay ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sExam)      SendMessageW(sExam, BM_SETCHECK, g_examOn ? BST_CHECKED : BST_UNCHECKED, 0);
#ifdef P0_LOG
    {
        FILE* f = fopen("D:/aab/p0.log", "a");
        if (f) { fprintf(f, "[sync] g_examOn=%d sExam=%p after=%d\n", g_examOn ? 1 : 0, (void*)sExam,
                         sExam ? (int)SendMessageW(sExam, BM_GETCHECK, 0, 0) : -1); fclose(f); }
    }
#endif
    if (sSkyMode[0]) SendMessageW(sSkyMode[0], BM_SETCHECK,  skyEn ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sSkyMode[1]) SendMessageW(sSkyMode[1], BM_SETCHECK, !skyEn ? BST_CHECKED : BST_UNCHECKED, 0);
    // 天空关了，亮度 / 动画那几个控件就没意义了 → 灰掉（看着也明白它们是天空的一部分）
    BOOL se = skyEn ? TRUE : FALSE;
    if (sSkyBr)    EnableWindow(sSkyBr, se);
    if (sSkyBrPct) EnableWindow(sSkyBrPct, se);
    if (sSkyRe)    EnableWindow(sSkyRe, se);
    if (sSkyGo)    EnableWindow(sSkyGo, se);
}

LRESULT CALLBACK SetProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        // 左侧导航：只切页，不动任何业务状态
        if (id >= ID_NAV_BASE && id < ID_NAV_BASE + SET_PAGES)
        {
            SetSwitchPage(id - ID_NAV_BASE);
            return 0;
        }
        if (id == ID_SET_CLOSE) { DestroyWindow(hWnd); return 0; }
        if (id == ID_PERIOD_COMBO && HIWORD(wParam) == CBN_SELCHANGE)
        {
            FillPeriodTimes();
            return 0;
        }
        if (id == ID_PERIOD_APPLY)
        {
            int i = ComboSeg();
            if (i >= 0 && i < pc)
            {
                wchar_t b1[16] = { 0 }, b2[16] = { 0 };
                GetDlgItemTextW(hWnd, ID_PERIOD_START, b1, 16);
                GetDlgItemTextW(hWnd, ID_PERIOD_END,   b2, 16);
                int sm = 0, em = 0;
                if (!ParseHM(b1, &sm) || !ParseHM(b2, &em))
                {
                    MessageBoxW(hWnd, L"时间格式不对。要写成 HH:MM，比如 08:00、14:35。", L"时间段设置", MB_OK | MB_ICONWARNING);
                }
                else if (em <= sm)
                {
                    MessageBoxW(hWnd, L"结束时间必须晚于开始时间。", L"时间段设置", MB_OK | MB_ICONWARNING);
                }
                else
                {
                    g_ovS[i] = sm;
                    g_ovE[i] = em;
                    SaveTimeOverrides();
                    LoadAlarmTimes();          // 立刻重载，铃声/弹岛马上用新时间
                    FillPeriodTimes();
                }
            }
            return 0;
        }
        if (id == ID_PERIOD_RESET)
        {
            int i = ComboSeg();
            if (i >= 0 && i < pc)
            {
                g_ovS[i] = -1;
                g_ovE[i] = -1;
                SaveTimeOverrides();
                LoadAlarmTimes();
                FillPeriodTimes();
            }
            return 0;
        }
        if (id == ID_SHOT_ADD)
        {
            wchar_t bt[16] = { 0 }, bx[32] = { 0 };
            GetDlgItemTextW(hWnd, ID_SHOT_TIME, bt, 16);
            GetDlgItemTextW(hWnd, ID_SHOT_TEXT, bx, 32);
            int m = 0;
            if (!ParseHM(bt, &m))
            {
                MessageBoxW(hWnd, L"时间格式不对。要写成 HH:MM，比如 19:30。", L"自定义提醒", MB_OK | MB_ICONWARNING);
            }
            else
            {
                OneShot hMain;
                hMain.hour = m / 60; hMain.minute = m % 60;
                wcscpy_s(hMain.text, bx[0] ? bx : L"时间到！");
                int k = sShotKind ? (int)SendMessageW(sShotKind, CB_GETCURSEL, 0, 0) : 0;
                hMain.daily = (k != 1);
                hMain.used = false;
                g_shots.push_back(hMain);
                SaveShots();
                RefreshShotList();
                if (sShotText) SetWindowTextW(sShotText, L"");
            }
            return 0;
        }
        if (id == ID_SHOT_DEL)
        {
            int sel = sShotList ? (int)SendMessageW(sShotList, LB_GETCURSEL, 0, 0) : -1;
            if (sel >= 0 && sel < (int)g_shots.size())
            {
                g_shots.erase(g_shots.begin() + sel);
                SaveShots();
                RefreshShotList();
            }
            return 0;
        }
        if (id == ID_CD_START)
        {
            wchar_t b[16] = { 0 }, lb[24] = { 0 };
            GetDlgItemTextW(hWnd, ID_CD_MIN, b, 16);
            GetDlgItemTextW(hWnd, ID_CD_LABEL, lb, 24);
            int m = _wtoi(b);
            if (m <= 0 || m > 600)
            {
                MessageBoxW(hWnd, L"分钟数要在 1~600 之间。", L"一次性倒计时", MB_OK | MB_ICONWARNING);
            }
            else
            {
                if (lb[0]) wcscpy_s(g_cdLabel, lb);
                g_cdManual = true;
                rr = NowMs() + (LONGLONG)m * 60000;
                ShowMiniIsland();       // 手动模式跳过课间检查
            }
            return 0;
        }
        if (id >= ID_PAL_BASE && id < ID_PAL_BASE + 16)
        {
            int t = sColTarget ? (int)SendMessageW(sColTarget, CB_GETCURSEL, 0, 0) : 0;
            if (t >= 0 && t < 4)
            {
                Col().c[t] = kPal[id - ID_PAL_BASE];
                Col().set[t] = true;
                SaveColCfg();
                InvalidateRect(hWnd, NULL, TRUE);
                if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT)
                    UpdateIslandDisplay(hMain, 1.0f, 255);
            }
            return 0;
        }
        if (id == ID_SET_HUD)
        {
            g_hud = (SendMessageW(sHud, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveSkyCfg();
            if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD)
                UpdateIslandDisplay(hMain, 1.0f, 255);     // 立刻重画，HUD 马上出现/消失
            return 0;
        }
        if (id == ID_COL_ON)
        {
            Col().on = !Col().on;
            SaveColCfg();
            SetSyncControls();
            if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT)
                UpdateIslandDisplay(hMain, 1.0f, 255);
            return 0;
        }
        if (id == ID_COL_RESET_ALL)
        {
            for (int i = 0; i < 4; i++) { Col().c[i] = 0; Col().set[i] = false; }
            SaveColCfg();
            InvalidateRect(hWnd, NULL, TRUE);
            if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT)
                UpdateIslandDisplay(hMain, 1.0f, 255);
            return 0;
        }
        if (id == ID_COL_RESET)
        {
            int t = sColTarget ? (int)SendMessageW(sColTarget, CB_GETCURSEL, 0, 0) : 0;
            if (t >= 0 && t < 4)
            {
                Col().c[t] = 0;
                Col().set[t] = false;
                SaveColCfg();
                InvalidateRect(hWnd, NULL, TRUE);
                if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD || islandMode == MODE_NOTCH_OUT)
                    UpdateIslandDisplay(hMain, 1.0f, 255);
            }
            return 0;
        }
        if (id == ID_PERIOD_DEL)
        {
            int i = ComboSeg();
            if (i >= 0 && i < pc)
            {
                if (SegCount() <= 1)
                {
                    MessageBoxW(hWnd, L"至少得留一个时间段。全删了就没有作息了。", L"删除时间段", MB_OK | MB_ICONWARNING);
                }
                else
                {
                    g_segOn[i] = false;
                    SaveSegOn();
                    LoadAlarmTimes();
                    RefreshPeriodCombo();
                    FillPeriodTimes();
                    SetSyncControls();
                }
            }
            return 0;
        }
        if (id == ID_PERIOD_ALL)
        {
            for (int i = 0; i < 11; i++) g_segOn[i] = true;
            SaveSegOn();
            LoadAlarmTimes();
            RefreshPeriodCombo();
            FillPeriodTimes();
            SetSyncControls();
            return 0;
        }
        if (id == ID_SET_EXAM && HIWORD(wParam) == BN_CLICKED)
        {
            bool on = (SendMessageW(sExam, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (on && MessageBoxW(hWnd,
                    L"开启考试模式后：\n\n· 只显示迷你岛时间，其他所有岛都不会弹出\n· 关闭实时天气，底板固定纯色\n· 退出需要从托盘再次进入设置并输入密码\n\n确定开启？",
                    L"考试模式", MB_YESNO | MB_ICONWARNING) != IDYES)
            {
                SendMessageW(sExam, BM_SETCHECK, BST_UNCHECKED, 0);   // 用户反悔，复选框弹回去
                return 0;
            }
            ApplyExamMode(on);
            // P1：开考之后 GUI 立刻收掉（学生不能再在窗口里点来点去）
            if (on && stw) DestroyWindow(stw);   // WM_DESTROY 会把 stw 置空
            return 0;
        }
        if (id == ID_SET_EXAM_PWD_SAVE && HIWORD(wParam) == BN_CLICKED)
        {
            wchar_t buf[32] = { 0 };
            GetDlgItemTextW(hWnd, ID_SET_EXAM_PWD, buf, 32);
            if (wcslen(buf) < 4)
            {
                MessageBoxW(hWnd, L"密码至少 4 位。", L"考试模式", MB_OK | MB_ICONWARNING);
                return 0;
            }
            wcscpy_s(g_examPwd, buf);
            SaveExamCfg();
            SetDlgItemTextW(hWnd, ID_SET_EXAM_PWD, L"");
            MessageBoxW(hWnd, L"密码已保存。", L"考试模式", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == ID_MSG_SAVE)
        {
            wchar_t b1[32] = { 0 }, b2[32] = { 0 };
            GetDlgItemTextW(hWnd, ID_MSG_START_EDIT, b1, 32);
            GetDlgItemTextW(hWnd, ID_MSG_END_EDIT,   b2, 32);
            // 空着就用默认，别让大岛变成一片空白
            if (b1[0]) wcscpy_s(g_msgStart, b1); else wcscpy_s(g_msgStart, L"上课了");
            if (b2[0]) wcscpy_s(g_msgEnd,   b2); else wcscpy_s(g_msgEnd,   L"下课了");
            SaveMsgCfg();
            SetSyncControls();
            return 0;
        }
        if (id == ID_MSG_RESET)
        {
            wcscpy_s(g_msgStart, L"上课了");
            wcscpy_s(g_msgEnd,   L"下课了");
            SaveMsgCfg();
            SetSyncControls();
            return 0;
        }
        if (id == ID_SET_TRY)
        {
            int pct = sTryPos ? (int)SendMessageW(sTryPos, TBM_GETPOS, 0, 0) : 0;
            AudioPreviewAsync(pct);
            return 0;
        }
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
            SendMessageW(hMain, WM_COMMAND, (WPARAM)id, 0);     // 复用主窗口那套（勾选 + 存盘）
            SetSyncControls();
            return 0;
        }
        if (id >= ID_BELLUP_BASE && id < ID_BELLUP_BASE + 11)
        {
            int b = id - ID_BELLUP_BASE;
            bellUp[b] = !bellUp[b];
            SaveAudioCfg(); SetSyncControls();
            return 0;
        }
        if (id >= ID_BELLDN_BASE && id < ID_BELLDN_BASE + 11)
        {
            int b = id - ID_BELLDN_BASE;
            bellDn[b] = !bellDn[b];
            SaveAudioCfg(); SetSyncControls();
            return 0;
        }
        // ⚠️ 这两个必须**单独一段、放在前面**：下面那一大串 case 是"共用同一个转发块"的，
        // 把新的 case 插到它们中间，会让前面的 ID（开机自启动/立即测试…）一起掉进新分支里
        // 直接 return，等于把那些按钮全废掉（踩过）。
        if (id == ID_SET_SKY_ANIM_RE || id == ID_SET_SKY_ANIM_GO)
        {
            skyReplay = (id == ID_SET_SKY_ANIM_RE);
            SaveSkyCfg(); SetSyncControls();
            return 0;
        }
        switch (id)
        {
        case ID_TBL_OLD: case ID_TBL_WINTER: case ID_TBL_W4:
        case ID_TRAY_AUDIO_ON: case ID_TRAY_HOLIDAY:
        case ID_SKY_MODE_SKY: case ID_SKY_MODE_SOLID:   // 底板模式：真正的处理在主窗口那边
        case ID_MINI_SHOW: case ID_MINI_HIDE:
        case ID_TRAY_AUTOSTART: case ID_TRAY_TEST:
        case ID_TRAY_NEXT: case ID_TRAY_STATUS:
            {
                int send = id;
                if (id == ID_MINI_SHOW) send = ns ? ID_MINI_HIDE : ID_MINI_SHOW;   // 勾选框来回切
                SendMessageW(hMain, WM_COMMAND, (WPARAM)send, 0);
                SetSyncControls();
                return 0;
            }
        }
        break;
    }
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT ds = (LPDRAWITEMSTRUCT)lParam;
        int id = (int)ds->CtlID;
        // 2026-11-13 左侧导航自绘：选中=深蓝底 + 白字 + 左侧金条；未选=浅灰底 + 深灰字
        if (id >= ID_NAV_BASE && id < ID_NAV_BASE + SET_PAGES)
        {
            int  idx   = id - ID_NAV_BASE;
            bool sel   = (idx == gCurPage);
            bool down  = (ds->itemState & ODS_SELECTED) != 0;
            bool focus = (ds->itemState & ODS_FOCUS) != 0;

            COLORREF bg = sel ? RGB(58, 110, 200)
                              : (down ? RGB(220, 224, 232) : RGB(244, 245, 248));
            HBRUSH b = CreateSolidBrush(bg);
            FillRect(ds->hDC, &ds->rcItem, b);
            DeleteObject(b);

            if (sel)
            {
                RECT bar = { ds->rcItem.left, ds->rcItem.top + 7,
                             ds->rcItem.left + 3, ds->rcItem.bottom - 7 };
                HBRUSH bb = CreateSolidBrush(RGB(255, 205, 90));
                FillRect(ds->hDC, &bar, bb);
                DeleteObject(bb);
            }

            wchar_t txt[32] = { 0 };
            GetWindowTextW(ds->hwndItem, txt, 32);
            SetBkMode(ds->hDC, TRANSPARENT);
            SetTextColor(ds->hDC, sel ? RGB(255, 255, 255) : RGB(58, 62, 70));
            HFONT f = GetCachedFont(15, sel);
            HGDIOBJ of = SelectObject(ds->hDC, f);
            RECT tr = ds->rcItem;
            tr.left += 18;
            DrawTextW(ds->hDC, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(ds->hDC, of);
            if (focus)
            {
                RECT fr = ds->rcItem;
                InflateRect(&fr, -2, -2);
                DrawFocusRect(ds->hDC, &fr);
            }
            return TRUE;
        }

        if (id >= ID_PAL_BASE && id < ID_PAL_BASE + 16)
        {
            int i = id - ID_PAL_BASE;
            HBRUSH b = CreateSolidBrush(kPal[i]);
            FillRect(ds->hDC, &ds->rcItem, b);
            DeleteObject(b);
            int t = sColTarget ? (int)SendMessageW(sColTarget, CB_GETCURSEL, 0, 0) : 0;
            if (t >= 0 && t < 4 && Col().c[t] == kPal[i])
            {
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(20, 20, 20));
                HPEN op = (HPEN)SelectObject(ds->hDC, pen);
                HBRUSH ob = (HBRUSH)SelectObject(ds->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(ds->hDC, ds->rcItem.left + 1, ds->rcItem.top + 1,
                          ds->rcItem.right - 1, ds->rcItem.bottom - 1);
                SelectObject(ds->hDC, op);
                SelectObject(ds->hDC, ob);
                DeleteObject(pen);
            }
            return TRUE;
        }
        break;
    }

    case WM_MOUSEWHEEL:
    {
        if (gCurPage == 2 && g_scMax > 0)
        {
            int d = GET_WHEEL_DELTA_WPARAM(wParam);
            ScrollApply(g_scOff - d * 70 / WHEEL_DELTA);
        }
        return 0;
    }

    case WM_HSCROLL:
    case WM_VSCROLL:
    {
        // lParam == 0 表示这是"窗口自己的滚动条"，不是滑块/音量条
        if (lParam == 0)
        {
            bool horz = (msg == WM_HSCROLL);
            int  pos  = horz ? g_scOffX : g_scOff;
            int  page = 100;
            switch (LOWORD(wParam))
            {
            case SB_LINELEFT:  pos -= 24; break;
            case SB_LINERIGHT: pos += 24; break;
            case SB_PAGELEFT:  pos -= page; break;
            case SB_PAGERIGHT: pos += page; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = (int)(short)HIWORD(wParam); break;
            }
            if (horz) ScrollApply(g_scOff, pos);
            else      ScrollApply(pos, g_scOffX);
            return 0;
        }
        HWND c = (HWND)lParam;
        if (c == sTryPos)
        {
            int v = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            if (sTryPct) { wchar_t pb[16]; swprintf_s(pb, L"%d%%", v); SetWindowTextW(sTryPct, pb); }
            return 0;
        }
        for (int bi = 0; bi < 11; bi++)
        {
            if (c == sBellUpVol[bi])
            {
                bellUpVol[bi] = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
                if (bellUpVol[bi] < 0) bellUpVol[bi] = 0;
                if (bellUpVol[bi] > 100) bellUpVol[bi] = 100;
                if (sBellUpPct[bi]) { wchar_t pb[16]; swprintf_s(pb, L"%d%%", bellUpVol[bi]); SetWindowTextW(sBellUpPct[bi], pb); }
                SaveAudioCfg();
                return 0;
            }
            if (c == sBellDnVol[bi])
            {
                bellDnVol[bi] = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
                if (bellDnVol[bi] < 0) bellDnVol[bi] = 0;
                if (bellDnVol[bi] > 100) bellDnVol[bi] = 100;
                if (sBellDnPct[bi]) { wchar_t pb[16]; swprintf_s(pb, L"%d%%", bellDnVol[bi]); SetWindowTextW(sBellDnPct[bi], pb); }
                SaveAudioCfg();
                return 0;
            }
        }
        if (c == sNotchA)                      // 迷你岛"静止后透明度"：立刻变给你看
        {
            int v = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
            if (v < 5) v = 5;
            if (v > 100) v = 100;
            notchA = v;
            SaveSkyCfg();
            SetSyncControls();
            nv = 255 * notchA / 100;           // 不等那 10 秒，直接跳到目标值
            na = NowMs() - NOTCH_DIM_MS - 1;
            if (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD) UpdateIslandDisplay(hMain, 1.0f, 255);
            else if (islandMode == MODE_IDLE && ns) { ShowNotch(); nv = 255 * notchA / 100; }
        }
        else if (c == sSkyBr)                  // 天空亮度：**自动弹大岛**给你看着调
        {
            int v = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            skyBr = v;
            SaveSkyCfg();
            SetSyncControls();
            wchar_t pv[64];
            swprintf_s(pv, L"天空亮度 %d%%", skyBr);
            if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD) { wcscpy_s(gg, pv); UpdateIslandDisplay(hMain, 1.0f, 255); }  // 已经在显示 → 只换字
            else if (islandMode == MODE_IDLE || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD) ShowMessageIsland(pv);                   // 没显示 → 弹出来给你看
        }
        else if (c == sAnimSpd)
        {
            int v = (int)SendMessageW(c, TBM_GETPOS, 0, 0);
            if (v < 50) v = 50;
            if (v > 200) v = 200;
            g_animSpeed = v / 100.0f;
            SaveSkyCfg();
            if (sAnimSpdPct)
            {
                wchar_t b[16];
                swprintf_s(b, L"%.1fx", g_animSpeed);
                SetWindowTextW(sAnimSpdPct, b);
            }
        }
        else if (c == sVol[0] || c == sVol[1] || c == sVol[2])
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
    case WM_TIMER:
        if (wParam == 2) { PageFadeTick(); return 0; }
        if (wParam == 79)
        {
            // 时间滚动期间的高频重绘：到点就停，别一直烧 CPU
            if (NowMs() >= g_rollUntil) { KillTimer(hMain, 79); return 0; }
            UpdateIslandDisplay(hMain, 1.0f, 255);
            return 0;
        }
        if (wParam == 1 && sStatus)
        {
            // 状态直接挂在导航栏底部（我：不用再点一下"状态信息"）
            SYSTEMTIME st2; GetLocalTime(&st2);
            int cur = st2.wHour * 60 + st2.wMinute;
            int nxt = -1;
            for (int k = 0; k < pc; k++)
            {
                int hh2 = 0, mm2 = 0;
                if (GetPeriodTime(k, ALARM_TYPE_CLASS_START, &hh2, &mm2))
                {
                    int v = hh2 * 60 + mm2;
                    if (v > cur && (nxt < 0 || v < nxt)) nxt = v;
                }
                if (GetPeriodTime(k, ALARM_TYPE_CLASS_END, &hh2, &mm2))
                {
                    int v = hh2 * 60 + mm2;
                    if (v > cur && (nxt < 0 || v < nxt)) nxt = v;
                }
            }
            wchar_t b2[512];
            if (nxt >= 0)
                swprintf_s(b2, L"运行中\n现在 %02d:%02d  下次 %02d:%02d\n", st2.wHour, st2.wMinute, nxt / 60, nxt % 60);
            else
                swprintf_s(b2, L"运行中\n现在 %02d:%02d\n今天没有更多提醒了\n", st2.wHour, st2.wMinute);
            std::wstring txt = b2;
            if (!g_checkMsg.empty()) txt += g_checkMsg;       // 有问题就堆在下面
            SetWindowTextW(sStatus, txt.c_str());
        }
        return 0;

    case WM_ERASEBKGND:
    {
        // 2026-11-13 换个底色：系统 COLOR_BTNFACE 那层灰像 Win95
        HDC dc = (HDC)wParam;
        RECT r; GetClientRect(hWnd, &r);
        HBRUSH bk = CreateSolidBrush(RGB(247, 248, 250));
        FillRect(dc, &r, bk);
        DeleteObject(bk);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    {
        // 2026-11-13 跟新背景同色：之前返回 COLOR_BTNFACE，每个 Static 都带一块 Win95 灰
        SetBkMode((HDC)wParam, TRANSPARENT);
        static HBRUSH sBg = CreateSolidBrush(RGB(247, 248, 250));
        return (LRESULT)sBg;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 1);
        stw = NULL;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ===== 考试模式：状态 / 落地 / 密码闸门（2026-11-16）=====
static void LoadExamCfg()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        DWORD v = 0, sz = sizeof(v);
        if (RegQueryValueExW(k, L"ExamMode", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) g_examOn = (v != 0);
        wchar_t buf[32] = {0};
        DWORD bs = (DWORD)sizeof(buf);
        if (RegQueryValueExW(k, L"ExamPwd", NULL, NULL, (LPBYTE)buf, &bs) == ERROR_SUCCESS)
            if (buf[0]) wcscpy_s(g_examPwd, buf);
        RegCloseKey(k);
    }
}
static void SaveExamCfg()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS)
    {
        DWORD v = g_examOn ? 1 : 0;
        RegSetValueExW(k, L"ExamMode", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegSetValueExW(k, L"ExamPwd", 0, REG_SZ, (LPBYTE)g_examPwd, (DWORD)((wcslen(g_examPwd) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}

// 把岛强行摆成"迷你岛定格"：不管现在是大岛/小岛/变身中，一律立刻变成迷你岛。
// 考试模式下这是唯一的形态，所以不走动画（学生看到"岛在收"没有意义）。
static void ForceNotchOnly()
{
    nt = true; nv = 255; na = NowMs();
    pp = false; hh = false; af = false;
    islandW = NOTCH_WIDTH; islandH = NOTCH_HEIGHT;
    islandY = NOTCH_Y; islandX = (B - islandW) / 2;
    sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;
    bigH = ISLAND_HEIGHT; bigW = NOTCH_WIDTH;
    islandMode = MODE_NOTCH_HOLD;
    hideAtMs = 0;                       // 不自动收
    ComputeCanvasSize();
    UpdateIslandDisplay(hMain, 1.0f, 255);
}

void ApplyExamMode(bool on)
{
    g_examOn = on;
    SaveExamCfg();
    if (on)
    {
        // 开考那一刻可能有铃声/音乐正在放 —— 立刻停掉，别让它响完
        BgmStop();
        mciSendStringW(L"close ilsaudio", NULL, 0, NULL);
        mciSendStringW(L"close ilstry", NULL, 0, NULL);
        ForceNotchOnly();
    }
    else    UpdateIslandDisplay(hMain, 1.0f, 255);
    if (sExam) SendMessageW(sExam, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    SetSyncControls();
}

// 密码输入框：一个独立的模态小窗（标准控件，和设置窗口一致）
static bool g_pwdDone = false;
static LRESULT CALLBACK PwdProc(HWND h, UINT m, WPARAM wParam, LPARAM lParam)
{
    switch (m)
    {
    case WM_CREATE:
    {
        HINSTANCE hi = GetModuleHandleW(NULL);
        HFONT f = GetCachedFont(10, false);
        HWND st = CreateWindowExW(0, L"STATIC", L"请输入考试模式的二级密码", WS_CHILD | WS_VISIBLE,
                                  18, 14, 340, 22, h, NULL, hi, NULL);
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_NUMBER | ES_AUTOHSCROLL,
                                  18, 44, 340, 28, h, (HMENU)2001, hi, NULL);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                  196, 88, 76, 30, h, (HMENU)IDOK, hi, NULL);
        HWND ca = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
                                  282, 88, 76, 30, h, (HMENU)IDCANCEL, hi, NULL);
        SendMessageW(st, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(ed, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(ok, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(ca, WM_SETFONT, (WPARAM)f, TRUE);
        SendMessageW(ed, EM_SETLIMITTEXT, 15, 0);
        SetFocus(ed);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t buf[32] = {0};
            GetDlgItemTextW(h, 2001, buf, 32);
            if (wcscmp(buf, g_examPwd) == 0) { g_pwdDone = true; DestroyWindow(h); }
            else MessageBoxW(h, L"密码不正确。", L"考试模式", MB_OK | MB_ICONWARNING);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { g_pwdDone = true; DestroyWindow(h); return 0; }
        break;
    case WM_CLOSE: g_pwdDone = true; DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, wParam, lParam);
}
static bool AskExamPassword(HWND owner)
{
    static bool regd = false;
    if (!regd)
    {
        regd = true;
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = PwdProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"MobileExamPwd";
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
    }
    g_pwdDone = false;
    const int W = 392, H = 168;
    RECT wa = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (wa.right - wa.left - W) / 2;
    int y = wa.top + (wa.bottom - wa.top - H) / 2;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MobileExamPwd", L"考试模式 — 需要密码",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H,
                               owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!dlg) return false;
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(GetDlgItem(dlg, 2001));
    MSG msg;
    while (!g_pwdDone && GetMessageW(&msg, NULL, 0, 0))
    {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (IsWindow(dlg)) DestroyWindow(dlg);
    return true;
}

void ShowSettingsWindow(HWND owner)
{
    // 考试模式：进设置先过密码闸门（只有"还没开窗"时才拦，已经开着的直接前置）
    if (!stw && g_examOn && !AskExamPassword(owner)) return;
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
    // 窗口尺寸：**两栏** 860×500。原来是单栏 560×896 —— 又高又窄，
    // 1366×768、4:3 这类屏幕（加上标题栏和非 100% 缩放）底部会被顶出屏幕，按钮点不到。
    // 现在左右分栏：左边放"作息 + 提醒"，右边放"主题 + 迷你岛 + 其他"，高度砍掉一半。
    // 2026-10-27 GUI 改版：一屏两栏改成"左侧导航 + 右侧分页"。
    // 原来 860×500 塞下所有功能，再加东西就没地方了。
    const int CW = 980, CH = 640;
    const int NAV_W = 190;                 // 左侧导航宽度（约 1/5）
    sNavW = NAV_W;                         // 渐变覆盖层要知道内容区从哪开始
    const int CX = NAV_W + 16;             // 右侧内容区左边
    const int CWid = CW - CX - 16;         // 内容区宽度
    RECT r = {0, 0, CW, CH};
    // 2026-11-13 用户提的方案：加系统滚动条。触控拖动系统自己管，不用我们接 WM_POINTER
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_HSCROLL | WS_VSCROLL
             | WS_CLIPCHILDREN;   // 2026-11-14：不裁子控件的话，父窗口擦背景会把所有控件盖掉再重画 —— 滚动时那一下"全没了"就是它
    AdjustWindowRect(&r, st, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    // 居中的同时**夹进工作区**：屏幕矮的时候（比如 768）也要保证整窗可见，
    // 否则标题栏会跑到屏幕外面去。
    RECT wa = {0,0,0,0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (wa.right - wa.left - w) / 2;
    int y = wa.top  + (wa.bottom - wa.top - h) / 2;
    if (y < wa.top) y = wa.top;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (x < wa.left) x = wa.left;
    // 关键：不加 WS_EX_TOOLWINDOW、也不给 owner → 它会像正常程序一样出现在任务栏
    for (int i2 = 0; i2 < SET_PAGES; i2++) gPageCtl[i2].clear();
    gCurPage = 0;
    stw = CreateWindowExW(0, L"MobileSetWnd", L"mobile Turbo 设置", st,
                          x, y, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!stw) return;


    // ===== 左侧导航 + 右侧内容（分页）=====
    static const wchar_t* kNav[SET_PAGES] = { L"作息表", L"时间提醒", L"自定义", L"主题", L"迷你岛", L"更多" };

    gSetPage = -1;   // 导航和状态栏不属于任何页
    SetCtl(L"STATIC", L"mobile 设置", SS_LEFT, 18, 14, 150, 26, -1);
    for (int i2 = 0; i2 < SET_PAGES; i2++)
        sNav[i2] = SetCtl(L"BUTTON", kNav[i2], BS_OWNERDRAW,
                          12, 54 + i2 * 44, NAV_W - 24, 38, ID_NAV_BASE + i2);
    sStatus = SetCtl(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 12, CH - 96, NAV_W - 24, 84, -1);

    // ---------- 页 0：作息表 ----------
    gSetPage = 0;
    // ===== 考试模式（要求"一定放最上面"）=====
    sExam = SetCtl(L"BUTTON", L"考试模式（只保留迷你岛，屏蔽其他所有岛）", BS_AUTOCHECKBOX,
                   CX, 6, 640, 30, ID_SET_EXAM);
    SetCtl(L"STATIC", L"开启后只显示迷你岛时间，其余岛一律不弹；实时天气关闭、底板固定纯色。关闭需从托盘重新进设置并输入密码。",
           SS_LEFT, CX + 4, 38, 760, 20, -1);
    SetCtl(L"STATIC", L"选择一套作息", SS_LEFT, CX, 82, 320, 24, -1);
    sTbl[0] = SetCtl(L"BUTTON", L"初四夏季作息", BS_AUTORADIOBUTTON | WS_GROUP, CX + 8, 126, 320, 26, ID_TBL_OLD);
    sTbl[1] = SetCtl(L"BUTTON", L"冬季作息",     BS_AUTORADIOBUTTON,            CX + 8, 164, 320, 26, ID_TBL_WINTER);
    sTbl[2] = SetCtl(L"BUTTON", L"初四冬季作息", BS_AUTORADIOBUTTON,            CX + 8, 202, 320, 26, ID_TBL_W4);
    SetCtl(L"STATIC", L"换作息后，倒计时和铃声都按新的时间表走。", SS_LEFT, CX, 250, 600, 24, -1);

    // ---------- 页 1：时间提醒 ----------
    gSetPage = 1;
    SetCtl(L"STATIC", L"时间提醒", SS_LEFT, CX, 18, 320, 24, -1);
    sAudioOn = SetCtl(L"BUTTON", L"播放提示音（关掉下面全部灰掉）", BS_AUTOCHECKBOX, CX, 54, 420, 26, ID_TRAY_AUDIO_ON);

    sBgmOn = SetCtl(L"BUTTON", L"放学播放《回家》", BS_AUTOCHECKBOX, CX, 92, 220, 26, ID_SET_BGM_ON);
    SetCtl(L"STATIC", L"音量", SS_LEFT, CX + 234, 96, 40, 20, -1);
    sVol[2]    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, CX + 278, 88, 200, 30, -1);
    sVolPct[2] = SetCtl(L"STATIC", L"0%", SS_LEFT, CX + 484, 96, 44, 20, -1);
    SetCtl(L"BUTTON", L"试听", BS_PUSHBUTTON, CX + 534, 88, 60, 28, ID_SET_PREV2);

    // 独立试听：拖进度条定位 → 点试听从那个位置放。它不读也不写任何铃音配置。
    SetCtl(L"STATIC", L"试听", SS_LEFT, CX, 138, 50, 22, -1);
    sTryPos = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, CX + 54, 132, 470, 30, -1);
    sTryPct = SetCtl(L"STATIC", L"0%", SS_LEFT, CX + 532, 138, 46, 20, -1);
    SetCtl(L"BUTTON", L"试听", BS_PUSHBUTTON, CX + 584, 132, 76, 30, ID_SET_TRY);
    SetCtl(L"STATIC", L"（从选中的位置开始放，跟上面的铃音设置无关）", SS_LEFT, CX + 54, 160, 500, 20, -1);

    // 三列：到点弹岛 / 上课铃 / 下课铃（都按节次独立）
    SetCtl(L"STATIC", L"到点弹岛", SS_LEFT, CX + 30, 202, 90, 20, -1);
    SetCtl(L"STATIC", L"上课铃",   SS_LEFT, CX + 146, 202, 60, 20, -1);
    SetCtl(L"STATIC", L"下课铃",   SS_LEFT, CX + 392, 202, 60, 20, -1);
    SetCtl(L"STATIC", L"滑条＝该节音量", SS_LEFT, CX + 636, 202, 130, 20, -1);
    for (int i2 = 0; i2 < 11; i2++)
    {
        // trackbar 给矮了（<24px）画不出来，行高给到 32
        int ry = 224 + i2 * 32;
        sPeriod[i2]  = SetCtl(L"BUTTON", L"", BS_AUTOCHECKBOX, CX + 8, ry + 3, 130, 26, ID_PERIOD_BASE + i2);
        sBellUp[i2]  = SetCtl(L"BUTTON", L"", BS_AUTOCHECKBOX, CX + 146, ry + 3, 24, 26, ID_BELLUP_BASE + i2);
        sBellUpVol[i2] = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS,
                                CX + 174, ry, 160, 26, ID_BELLUP_VOL_BASE + i2);
        sBellUpPct[i2] = SetCtl(L"STATIC", L"0%", SS_LEFT, CX + 340, ry + 5, 46, 20, -1);
        sBellDn[i2]  = SetCtl(L"BUTTON", L"", BS_AUTOCHECKBOX, CX + 392, ry + 3, 24, 26, ID_BELLDN_BASE + i2);
        sBellDnVol[i2] = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS,
                                CX + 420, ry, 160, 26, ID_BELLDN_VOL_BASE + i2);
        sBellDnPct[i2] = SetCtl(L"STATIC", L"0%", SS_LEFT, CX + 586, ry + 5, 46, 20, -1);
    }

    // ---------- 页 2：自定义 ----------
    gSetPage = 2;
    SetCtl(L"STATIC", L"自定义", SS_LEFT, CX, 18, 320, 24, -1);
    SetCtl(L"STATIC", L"大岛弹出时上面的文字（默认：上课了 / 下课了）", SS_LEFT, CX, 52, 520, 22, -1);

    SetCtl(L"STATIC", L"上课提醒", SS_LEFT, CX, 96, 76, 22, -1);
    sMsgStartEdit = SetCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, CX + 84, 90, 300, 28, ID_MSG_START_EDIT);
    if (sMsgStartEdit) SendMessageW(sMsgStartEdit, EM_SETLIMITTEXT, 30, 0);

    SetCtl(L"STATIC", L"下课提醒", SS_LEFT, CX, 138, 76, 22, -1);
    sMsgEndEdit = SetCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, CX + 84, 132, 300, 28, ID_MSG_END_EDIT);
    if (sMsgEndEdit) SendMessageW(sMsgEndEdit, EM_SETLIMITTEXT, 30, 0);

    SetCtl(L"BUTTON", L"保存文字", BS_PUSHBUTTON, CX + 84, 176, 120, 32, ID_MSG_SAVE);
    SetCtl(L"BUTTON", L"恢复默认", BS_PUSHBUTTON, CX + 216, 176, 120, 32, ID_MSG_RESET);
    SetCtl(L"STATIC", L"（改完点保存，下次弹岛就用新文字）", SS_LEFT, CX + 84, 218, 420, 22, -1);

    // ---- 时间段设置（4b）：选一段，改它的开始/结束时间 ----
    SetCtl(L"STATIC", L"时间段设置", SS_LEFT, CX, 258, 200, 22, -1);
    sPerCombo = SetCtl(L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL,
                       CX, 286, 170, 240, ID_PERIOD_COMBO);
    SetCtl(L"STATIC", L"开始", SS_LEFT, CX + 182, 290, 40, 22, -1);
    sPerStart = SetCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, CX + 224, 286, 76, 26, ID_PERIOD_START);
    SetCtl(L"STATIC", L"结束", SS_LEFT, CX + 310, 290, 40, 22, -1);
    sPerEnd = SetCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, CX + 352, 286, 76, 26, ID_PERIOD_END);
    SetCtl(L"BUTTON", L"应用到该段",   BS_PUSHBUTTON, CX, 322, 106, 30, ID_PERIOD_APPLY);
    SetCtl(L"BUTTON", L"恢复该段默认", BS_PUSHBUTTON, CX + 114, 322, 116, 30, ID_PERIOD_RESET);
    SetCtl(L"BUTTON", L"删除该段",     BS_PUSHBUTTON, CX + 238, 322, 96, 30, ID_PERIOD_DEL);
    SetCtl(L"BUTTON", L"恢复所有段",   BS_PUSHBUTTON, CX + 342, 322, 104, 30, ID_PERIOD_ALL);
    SetCtl(L"STATIC", L"（格式 HH:MM；至少保留一个时间段）", SS_LEFT, CX, 358, 440, 20, -1);

    // ---- 自定义提醒（单点）：不用成对，每天或只响一次 ----
    SetCtl(L"STATIC", L"自定义提醒（单点）", SS_LEFT, CX, 396, 240, 22, -1);
    sShotTime = SetCtl(L"EDIT", L"19:30", WS_BORDER | ES_AUTOHSCROLL, CX, 422, 70, 26, ID_SHOT_TIME);
    sShotText = SetCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, CX + 78, 422, 180, 26, ID_SHOT_TEXT);
    if (sShotText) SendMessageW(sShotText, EM_SETLIMITTEXT, 30, 0);
    sShotKind = SetCtl(L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL,
                       CX + 266, 420, 94, 120, ID_SHOT_KIND);
    if (sShotKind)
    {
        SendMessageW(sShotKind, CB_ADDSTRING, 0, (LPARAM)L"每天");
        SendMessageW(sShotKind, CB_ADDSTRING, 0, (LPARAM)L"只响一次");
        SendMessageW(sShotKind, CB_SETCURSEL, 0, 0);
        MakeComboTouchFriendly(sShotKind);
    }
    SetCtl(L"BUTTON", L"添加", BS_PUSHBUTTON, CX + 368, 420, 72, 28, ID_SHOT_ADD);

    sShotList = SetCtl(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                       CX, 460, 440, 88, ID_SHOT_LIST);
    if (sShotList) SetWindowSubclass(sShotList, ListScrollSub, 2, 0);   // 触摸拖动滚动
    SetCtl(L"BUTTON", L"删除选中", BS_PUSHBUTTON, CX + 448, 460, 100, 30, ID_SHOT_DEL);
    SetCtl(L"STATIC", L"（列表里滚轮可翻）", SS_LEFT, CX + 448, 496, 200, 18, -1);

    // ---- 文字颜色（4d，2026-11-04）：16 色固定调色板 ----
    SetCtl(L"STATIC", L"文字颜色", SS_LEFT, CX, 562, 80, 22, -1);
    sColOn = SetCtl(L"BUTTON", L"启用自定义颜色（关掉＝强制金色）", BS_AUTOCHECKBOX,
                    CX + 84, 560, 380, 24, ID_COL_ON);
    sColTarget = SetCtl(L"COMBOBOX", L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL,
                        CX, 588, 150, 160, ID_COL_TARGET);
    if (sColTarget)
    {
        static const wchar_t* tg[4] = { L"迷你岛时间", L"大岛时间", L"大岛上方小字", L"倒计时" };
        for (int i = 0; i < 4; i++) SendMessageW(sColTarget, CB_ADDSTRING, 0, (LPARAM)tg[i]);
        SendMessageW(sColTarget, CB_SETCURSEL, 0, 0);
        MakeComboTouchFriendly(sColTarget);
    }
    for (int i = 0; i < 16; i++)
    {
        // 色块：owner-draw 按钮，在 WM_DRAWITEM 里刷颜色
        sPal[i] = SetCtl(L"BUTTON", L"", BS_OWNERDRAW | WS_BORDER,
                         CX + 162 + i * 26, 586, 24, 24, ID_PAL_BASE + i);
    }
    SetCtl(L"BUTTON", L"恢复默认色", BS_PUSHBUTTON, CX + 580, 582, 110, 28, ID_COL_RESET);
    SetCtl(L"BUTTON", L"全部恢复", BS_PUSHBUTTON, CX + 696, 582, 84, 28, ID_COL_RESET_ALL);

    // ---- 一次性倒计时（4e）：选分钟数，立刻开始，结束自动收回 ----
    SetCtl(L"STATIC", L"一次性倒计时", SS_LEFT, CX, 622, 110, 20, -1);
    sCdMin = SetCtl(L"EDIT", L"15", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, CX + 112, 618, 56, 26, ID_CD_MIN);
    SetCtl(L"STATIC", L"分钟", SS_LEFT, CX + 172, 622, 40, 20, -1);
    sCdLabel = SetCtl(L"EDIT", L"倒计时", WS_BORDER | ES_AUTOHSCROLL, CX + 214, 618, 110, 26, ID_CD_LABEL);
    if (sCdLabel) SendMessageW(sCdLabel, EM_SETLIMITTEXT, 20, 0);
    SetCtl(L"BUTTON", L"开始", BS_PUSHBUTTON, CX + 334, 616, 80, 30, ID_CD_START);

    // ---- 考试模式密码（2026-11-16）----
    SetCtl(L"STATIC", L"考试模式密码", SS_LEFT, CX, 672, 110, 22, -1);
    sExamPwdEdit = SetCtl(L"EDIT", L"", WS_BORDER | ES_PASSWORD | ES_NUMBER | ES_AUTOHSCROLL,
                          CX + 114, 668, 150, 26, ID_SET_EXAM_PWD);
    if (sExamPwdEdit) SendMessageW(sExamPwdEdit, EM_SETLIMITTEXT, 15, 0);
    SetCtl(L"BUTTON", L"保存密码", BS_PUSHBUTTON, CX + 274, 666, 96, 28, ID_SET_EXAM_PWD_SAVE);
    SetCtl(L"STATIC", L"（默认 426426；改完点保存，下次进设置就用新密码）", SS_LEFT, CX + 380, 672, 380, 20, -1);

    // ---------- 页 3：主题 ----------
    gSetPage = 3;
    SetCtl(L"STATIC", L"主题", SS_LEFT, CX, 18, 320, 24, -1);
    // 2026-11-14：原来这里放的是「浅色 / 深色」，有天空背景之后改成「底板模式」：
    //   天气模式 = 现在这套（底板跟着日出日落/天气走）
    //   纯色模式 = 老深色主题那块底色（深灰），不做任何背景渲染
    sSkyMode[0] = SetCtl(L"BUTTON", L"天气模式", BS_AUTORADIOBUTTON | WS_GROUP, CX, 56, 120, 26, ID_SKY_MODE_SKY);
    sSkyMode[1] = SetCtl(L"BUTTON", L"纯色模式", BS_AUTORADIOBUTTON,            CX + 130, 56, 120, 26, ID_SKY_MODE_SOLID);
    sHoliday    = SetCtl(L"BUTTON", L"特殊节日提醒", BS_AUTOCHECKBOX,       CX, 94, 240, 26, ID_TRAY_HOLIDAY);

    SetCtl(L"STATIC", L"天空亮度", SS_LEFT, CX, 138, 70, 20, -1);
    sSkyBr    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, CX + 78, 130, 240, 30, -1);
    sSkyBrPct = SetCtl(L"STATIC", L"0%", SS_LEFT, CX + 324, 138, 44, 20, -1);

    SetCtl(L"STATIC", L"天空动画", SS_LEFT, CX, 174, 70, 20, -1);
    sSkyRe = SetCtl(L"BUTTON", L"每次重播一遍", BS_AUTORADIOBUTTON | WS_GROUP, CX + 78, 170, 130, 24, ID_SET_SKY_ANIM_RE);
    sSkyGo = SetCtl(L"BUTTON", L"直接接着变",   BS_AUTORADIOBUTTON,            CX + 212, 170, 130, 24, ID_SET_SKY_ANIM_GO);

    SetCtl(L"STATIC", L"动画速度", SS_LEFT, CX, 208, 70, 20, -1);
    sAnimSpd    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, CX + 78, 200, 240, 30, ID_ANIM_SPD);
    sAnimSpdPct = SetCtl(L"STATIC", L"1.0x", SS_LEFT, CX + 324, 208, 44, 20, -1);
    if (sAnimSpd)
    {
        SendMessageW(sAnimSpd, TBM_SETRANGE, TRUE, MAKELPARAM(50, 200));
        SendMessageW(sAnimSpd, TBM_SETPOS,   TRUE, (LPARAM)(int)(g_animSpeed * 100.0f + 0.5f));
    }

    // ---------- 页 4：迷你岛 ----------
    gSetPage = 4;
    SetCtl(L"STATIC", L"迷你岛", SS_LEFT, CX, 18, 320, 24, -1);
    sMini = SetCtl(L"BUTTON", L"显示迷你岛（顶边时间）", BS_AUTOCHECKBOX, CX, 56, 360, 26, ID_MINI_SHOW);
    SetCtl(L"STATIC", L"静止后透明度", SS_LEFT, CX, 100, 90, 20, -1);
    sNotchA    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, CX + 98, 92, 240, 30, -1);
    sNotchAPct = SetCtl(L"STATIC", L"0%", SS_LEFT, CX + 344, 100, 44, 20, -1);

    // ---------- 页 5：更多 ----------
    gSetPage = 5;
    SetCtl(L"STATIC", L"更多", SS_LEFT, CX, 18, 320, 24, -1);
    SetCtl(L"BUTTON", L"立即测试",     BS_PUSHBUTTON, CX, 58, 180, 32, ID_TRAY_TEST);
    SetCtl(L"BUTTON", L"下次提醒时间", BS_PUSHBUTTON, CX + 196, 58, 180, 32, ID_TRAY_NEXT);
    SetCtl(L"BUTTON", L"开机自启动",   BS_PUSHBUTTON, CX, 102, 180, 32, ID_TRAY_AUTOSTART);
    SetCtl(L"BUTTON", L"关闭",         BS_PUSHBUTTON, CX + 196, 102, 180, 32, ID_SET_CLOSE);

    // 调试 HUD：勾上就在岛下方显示 islandMode/尺寸/剩余时间，正常使用时关掉
    sHud = SetCtl(L"BUTTON", L"调试 HUD（岛下方显示状态，仅排查问题时用）", BS_AUTOCHECKBOX, CX, 158, 420, 26, ID_SET_HUD);
    // 2026-11-14：这句本来排在 y=152，和上面 y=158 的「调试 HUD」复选框压在一起
    // （实测重叠 7560 px²，字被盖掉一半）。挪到复选框下面。
    SetCtl(L"STATIC", L"提示：退出整个程序请用托盘图标右键菜单。", SS_LEFT, CX, 192, 500, 24, -1);

    // 滑块的量程（注意：这几行是**循环外**的独立语句，缩进别写成循环体里）
    for (int i = 0; i < 3; i++)
        if (sVol[i]) SendMessageW(sVol[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    if (sTryPos) SendMessageW(sTryPos, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    if (sSkyBr)  SendMessageW(sSkyBr,  TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    if (sNotchA) SendMessageW(sNotchA, TBM_SETRANGE, TRUE, MAKELPARAM(5, 100));
    for (int i2 = 0; i2 < 11; i2++)
    {
        if (sBellUpVol[i2]) SendMessageW(sBellUpVol[i2], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        if (sBellDnVol[i2]) SendMessageW(sBellDnVol[i2], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    }

    // 时间段下拉：把当前作息的名字填进去，默认选第一段
    RefreshPeriodCombo();
    MakeComboTouchFriendly(sPerCombo);
    ScrollCollect();          // 记下自定义页控件的原始位置，供滚动用
    InstallScrollDrag();      // 给 STATIC 装触摸拖动（子类化）

    RunSelfCheck();
    PageApply(0);
    SetSyncControls();
    FillPeriodTimes();
    RefreshShotList();
    SetTimer(stw, 1, 1000, NULL);      // 状态栏每秒刷新
    ShowWindow(stw, SW_SHOW);
    UpdateWindow(stw);
    SetForegroundWindow(stw);
}

void ShowTrayMenu(HWND hWnd)
{
    HMENU hMenu = CreatePopupMenu();
    // 原先这些二级菜单项整个搬进了图形设置窗口（我要求"像应用那样有窗口画面"）
    // 托盘只留"打开设置"和"退出"两项
    AppendMenu(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"设置…");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, ii.x, ii.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

void ShowMessageIsland(const wchar_t* text, int extraW, const wchar_t* small, bool sched)
{
    if (g_examOn) return;           // 考试模式：消息岛一律不许弹
    // 2026-11-13：schedMode 改成由调用方传进来。
    // 原来只有 ShowScheduleIsland 里 `schedMode = true`，**没有任何一处置回 false** ——
    // 注释写着"ShowMessageIsland 里会重置"，其实根本没写。
    // 后果：弹过一次课表岛之后，后面所有消息岛（上课了/下课了/隐私提示）全变成靠左排版。
    schedMode = sched;
    KillTimer(hMain,1); KillTimer(hMain,2); KillTimer(hMain,3); KillTimer(hMain,4);
    // 2026-11-12 我要求：点迷你岛时"像迷你岛弹成大岛"那样平滑长大，不要先收掉再弹出来。
    // 所以这里不再 HideMiniIsland()，而是记下 fromMini，后面走 MODE_TO_BIG。
    bool fromMini = (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD);
    hh = true;
    wcscpy_s(gg, text);
    if (small) wcscpy_s(infoCustom, small); else infoCustom[0] = 0;
    vv = 0.0f; af = false; ww = 0;
    skyAlarm = false;                      // 消息类一律不套天空（我只允许上课/下课/倒计时三处）
    if (extraW <= 0) privKind = 0;         // 普通消息：把左边的图标位让出来
    // ⚠️ 大岛永远是"普通圆角"。sf 是迷你岛的吸顶形状因子（1=上两角收成直角），
    // 迷你岛状态退出后会留着 1 —— 不清掉的话，紧接着弹出来的大岛**上两角是直角**，
    // 看着就是"上半截和迷你岛一样，没有圆角"。
    sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;
    // extraW：左边要放图标时多留出来的宽度（隐私提示用），普通消息传 0
    FitBigWidth(NULL, gg, NULL, extraW);
    L = true;
    // 大岛已经挂在屏幕上了（典型场景：连着点「开机自启动」）→ 只把字换掉，
    // 别再从头弹跳一遍 —— 我明确要求"直接变字体就行了"。
    if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD)
    {
        if (islandMode == MODE_BIG_HOLD) { islandW = bigW; islandX = (B - islandW) / 2; islandY = ISLAND_FINAL_Y; }
        hideAtMs = NowMs() + DISPLAY_DURATION_MS;      // 换字之后重新计 5 秒
        UpdateIslandDisplay(hMain, 1.0f, 255);
        return;
    }
    if (fromMini)
    {
        // 从迷你岛"当前尺寸"接着长，接缝处不跳变
        ae = islandW; an = islandH; ao = islandY;
        gw = bigW; gh = ISLAND_HEIGHT; gy = ISLAND_FINAL_Y; gJ = MODE_BIG_HOLD;
        animStep = 0; P = 0; oo = 0;
        vv0 = 1.0f; vt = 0.0f; vv = 1.0f;
        skyT0 = NowMs(); skyClear = false;
        islandMode = MODE_TO_BIG;
    }
    else
    {
        animStep = 0; P = 0;
        skyT0 = NowMs();                 // 天空动画从这里开始演"日出 → 现在"
        skyClear = false;                // 消息岛是"从无到有"，没有夜色要收
        islandMode = MODE_BIG_IN;
    }

    PrepareBackdropBeforeShow();
    R = NowMs(); // 交主循环 60fps 调度
}

// ===== 课表岛（2026-11-12）=====
// 点迷你岛 / 刚下课 时调用：查"下节课"的科目，用两行 + 靠左的排版弹出来
void ShowScheduleIsland()
{
    int nh = 0, nm = 0, np = -1;
#ifdef P2_TEST
    nh = 21; nm = 5; np = 0;                  // 测试用：假装下一节是 21:05
    bool gotNext = true;
#else
    bool gotNext = NextClassStart(&nh, &nm, &np);
#endif
    if (gotNext)
    {
        SYSTEMTIME st; GetLocalTime(&st);
        int wd = st.wDayOfWeek;             // 0=周日
        if (wd == 0) wd = 7;
        const wchar_t* sub = SchedSubject(wd, np);   // np 0 基，直接查
        wchar_t big[64];
        if (sub) swprintf_s(big, L"%s", sub);
        else if (np >= 0 && np < (int)(sizeof(ffA)/sizeof(ffA[0])))
                 swprintf_s(big, L"%s  %02d:%02d", ffA[np], nh, nm);
        else     swprintf_s(big, L"%02d:%02d", nh, nm);
        // P2：如果「下课了」还挂在屏幕上，就**原地换字**——
        // 底板、尺寸、圆角、天空一律不动，只把大字交叉渐变过去。
        // 宽度不够（课表比"下课了/时间"更宽）才退回 ShowMessageIsland 走放大动画。
        if (!g_examOn && islandMode == MODE_BIG_HOLD && !ply && bigH == ISLAND_HEIGHT && !he)
        {
            // 只**量**宽度，不碰 bigW/画布 —— 之前借 FitBigWidth 会顺手改画布，岛会横跳 4px
            int needW = 0;
            {
                HDC dc = GetDC(NULL);
                HGDIOBJ oldF = SelectObject(dc, GetCachedFont((int)(ISLAND_HEIGHT * 0.62), true));
                SIZE s1 = { 0, 0 }, s2 = { 0, 0 };
                GetTextExtentPoint32W(dc, big, (int)wcslen(big), &s1);
                SelectObject(dc, GetCachedFont((int)(ISLAND_HEIGHT * 0.20), false));
                const wchar_t* lb2 = L"下节课是";
                GetTextExtentPoint32W(dc, lb2, (int)wcslen(lb2), &s2);
                SelectObject(dc, oldF);
                ReleaseDC(NULL, dc);
                needW = (s1.cx > s2.cx ? s1.cx : s2.cx) + 120;
                int lo2 = B * 22 / 100, hi2 = B * 92 / 100;
                if (needW < lo2) needW = lo2;
                if (needW > hi2) needW = hi2;
            }
            if (needW <= islandW + 2)
            {
                if (hh) wcscpy_s(g_prevBig, gg);            // 旧大字是消息
                else                                              // 旧大字是时间
                {
                    SYSTEMTIME st; GetLocalTime(&st);
                    swprintf_s(g_prevBig, L"%02d:%02d", st.wHour, st.wMinute);
                }
                wcscpy_s(gg, big);
                wcscpy_s(infoCustom, L"下节课是");
                schedMode = true;
                hh = true;
                g_swapT0 = NowMs();
                g_rollUntil = NowMs() + 320;                 // 借用时间滚动那条 16ms 重绘定时器
                SetTimer(hMain, 79, 16, NULL);
                hideAtMs = NowMs() + DISPLAY_DURATION_MS;    // 换字后重新给满 5 秒
                UpdateIslandDisplay(hMain, 1.0f, 255);
                return;
            }
        }
        ShowMessageIsland(big, 0, L"下节课是", true);
        // 靠左排版由 ShowMessageIsland 的 sched 参数负责（以前在这里补一句 schedMode=true，
        // 但因为没人置回 false，弹过一次就永远靠左了）
        UpdateIslandDisplay(hMain, 1.0f, 255);
    }
    else
    {
        ShowMessageIsland(L"今天没课了", 0, L"接下来");
    }
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
    if (islandMode == MODE_IDLE || islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD) { privWait = false; ShowPrivacyIsland(want); }
    else { privWait = true; privWant = want; }      // 大岛/变身正忙 → 挂起来
}

// ===== 节日彩蛋弹窗：只挂在"今天最后一节课下课"（放学）那一刻 =====
// 我明确要求：不要一开机就弹，也不要"明天就是…"，就在最后一节课下课后弹一次「中秋快乐」。
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

static void HolidayEggKey(wchar_t* key, size_t sz)
{
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(key, sz, L"HolidayEggShown_%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
}
// 同一天只弹一次（不管当天启动过几次程序）
bool HolidayEggShownToday()
{
    wchar_t key[64] = {0};
    HolidayEggKey(key, 64);
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
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
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = 1;
        RegSetValueEx(hKey, key, 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

void LoadNotchCfg()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, REGKEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD v = 1, sz = sizeof(v);      // 默认 1 = 冬季作息
        if (RegQueryValueEx(hKey, L"NotchShow", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) ns = (v != 0);
        RegCloseKey(hKey);
    }
}
void SaveHolidayCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        DWORD v = hon ? 1 : 0;
        RegSetValueEx(hKey, L"HolidaySkin", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}
void SaveNotchCfg()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, REGKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
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
    if (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD) return;
    if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD || islandMode == MODE_BIG_OUT || islandMode == MODE_TO_MINI || islandMode == MODE_TO_BIG || islandMode == MODE_HOLIDAY) return;   // 大岛/变身优先，不抢
    pp = false;                                // 迷你岛只显示时间，绝不去显示倒计时
    hh = false;                                // 同上：常驻岛不是消息，别把时间吞了
    if (islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD)                      // 小岛（倒计时）正显示 → 直接变身成迷你岛，不先收再弹
    {
        nt = false; ae = islandW; an = islandH; ao = islandY;
        gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
        bigH = ISLAND_HEIGHT; bigW = NOTCH_WIDTH; ComputeCanvasSize();
        oo = 0; vv0 = 1.0f; vt = 1.0f; sf0 = sf; sf1 = 0.0f;   // 两端都是圆角，形状不用变
        islandMode = MODE_TO_MINI;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    nt = true; nv = 255; na = NowMs();
    islandW = NOTCH_WIDTH; islandH = NOTCH_HEIGHT; islandY = NOTCH_Y; islandX = (B - islandW) / 2;
    animStep = 0; vv = 1.0f; ai = -1;
    // 必须把 g / bigH 也拉回迷你岛那一档并重算画布 ——
    // 否则从 AI 岛（g=640）过来时画布还是 656 宽，迷你岛窗口会有 500 多像素的空区（回归过）
    bigH = ISLAND_HEIGHT; bigW = NOTCH_WIDTH; ComputeCanvasSize();
    islandMode = MODE_NOTCH_IN;                                   // 从屏幕边缘上方"落"下来
    PrepareBackdropBeforeShow();
    R = NowMs();
}
void HideNotch()
{
    if (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD) { islandMode = MODE_NOTCH_OUT; P = 0; R = NowMs(); }
}
// 迷你岛常驻期间的心跳：到秒就重绘（分钟跳变），静止够久就把整块降成半透明
// 外部音乐检测：有歌就开始显示播放器岛，停了就收回。
// 只在"没在放《回家》"时管，免得和放学那段抢。
void NotchHoldTick()
{
    hh = false;          // 兜底：迷你岛只显示时间
    int nowSec = (int)(NowMs() / 1000);
    if (nowSec != ai) { ai = nowSec; }
    int dimA = 255 * notchA / 100;                       // 我可调（设置里那条"静止后透明度"）
    // 考试模式：除了时间走字，别的什么都不许变 —— 不做"变淡"
    int target = g_examOn ? 255 : ((NowMs() - na > NOTCH_DIM_MS) ? dimA : 255);
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
    // 原来只在"秒数变了"的时候重画，一旦被压住/丢掉就永远回不来 —— 我报的"开着开着迷你岛没了"就是它。
    // 现在每秒：确认还显示着 → 确认还在最上层（**只在真被盖住时才动手**）→ 重画一帧。
    // ⚠️ 不能每秒无条件 SetWindowPos(HWND_TOPMOST)：那会一直把别的置顶窗口往下压，
    //    实测会把同机的设置窗口都顶不下去（踩过）。只在被盖住时才重新顶。
    if (!IsWindowVisible(hMain)) ShowWindow(hMain, SW_SHOWNOACTIVATE);
    POINT c = { islandX + islandW / 2, islandY + islandH / 2 };              // 岛正中心（屏幕坐标）
    if (WindowFromPoint(c) != hMain)
        SetWindowPos(hMain, HWND_TOPMOST, cnv, 0, cw, ch, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    UpdateIslandDisplay(hMain, 1.0f, 255);
}

// 定时触发唯一入口：两条触发链都走这里，本分钟只放行一次。
// 以前各判各的，谁后到谁就再弹一次 —— 那个"弹一次又弹一次"的 fuck bug 就是它
// 当前这一分钟对应第几节课（按 type 查该节的上课/下课时间）；找不到返回 -1
static int CurBellPeriod(AlarmType type)
{
    SYSTEMTIME st; GetLocalTime(&st);
    int cur = st.wHour * 60 + st.wMinute;
    for (int i = 0; i < pc; i++)
    {
        int hh = 0, mm = 0;
        if (!GetPeriodTime(i, type, &hh, &mm)) continue;
        if (hh * 60 + mm == cur) return i;
    }
    return -1;
}

// 到点这一下到底响不响：总开关 → 该节课的对应铃开关
static int BellVolume(AlarmType type, int period)
{
    if (!fuck) return -1;
    // P3：以前"定位不到这一节"就退回全局音量 —— 那等于绕过勾选框。
    // 现在：优先用闹钟自带的那一节；实在没有才按时间反查；都对不上就**不响**。
    int i = (period >= 0 && period < 11) ? period : CurBellPeriod(type);
#ifdef P0_LOG
    {
        SYSTEMTIME st0; GetLocalTime(&st0);
        FILE* f = fopen("D:/aab/p0.log", "a");
        if (f) { fprintf(f, "[bell] %02d:%02d type=%d period=%d use=%d bellDn=%d bellUp=%d", st0.wHour, st0.wMinute, (int)type, period, i, (i >= 0 && i < 11) ? (bellDn[i] ? 1 : 0) : -1, (i >= 0 && i < 11) ? (bellUp[i] ? 1 : 0) : -1); fputc(10, f); fclose(f); }
    }
#endif
    if (i < 0 || i >= 11) return -1;      // 定位不到就不响（不再回退全局音量）
    bool on = (type == ALARM_TYPE_CLASS_START) ? bellUp[i] : bellDn[i];
    if (!on) return -1;
    return (type == ALARM_TYPE_CLASS_START) ? bellUpVol[i] : bellDnVol[i];
}

void TriggerAlarmByTimer(AlarmType type, int period)
{
    if (gSelfTest) return;      // 自检跑着的时候别让真实闹钟插进来（会把播放器那步顶掉）
    SYSTEMTIME st; GetLocalTime(&st);
    int curMin = st.wHour * 60 + st.wMinute;
    if (N == curMin) return;      // 本分钟已经弹过了，吞掉
    N = curMin;
    // 2026-10-13 我改回去了：放学（今天最后一节课下课）**照常敲下课铃**，
    // 《回家》挪到固定 19:30 单独播（见 case 10 里的 BGM_HOUR/BGM_MIN）。
    int bv = BellVolume(type, period);
    if (bv > 0)
        AudioPlayAsyncVol(type, bv);    // 后台放音：别让 MCI 的启动耗时挡住岛的动画
    // bv<=0：这一节这个铃关掉了（或音量 0），画面照常弹
    TriggerAlarm(type);          // 铃关了也照常弹岛（"到点弹岛"是独立开关）
}

void TriggerAlarmManual(AlarmType type);   // 实现在下面

// ===== 立即测试：完整回归 =====
// 点一次「立即测试」，按顺序把全部可自动化的功能跑一遍（约 30 秒）：
//   迷你岛 收/展 两轮 → 下课了 / 上课了 → 课间小岛倒计时 → 静置看变淡
//   → 再弹大岛 → 开 AI 岛（大岛原样下移）→ 三条消息岛堆叠
//   → 关 AI 岛（淡出 + 原岛滑回）→ 播放器 → 收回
// 用 100ms 的定时器推，tsWait 是"这一拍之后还要等多少拍"。
//
// 测不到的（要外部条件）：节日彩蛋（要改注册表）、麦克风/摄像头占用提示（要真有人占用）、
// 设置窗口、AI 真实对话（要联网 + Key）。
static int tsStep = -1;      // -1 = 没在测试
static int tsWait = 0;
// ⚠️ 别用"拍数"算时间：SetTimer(o, 14, 100) 实际并不保证 100ms 一拍（实测快不少），
//    按拍数推会把后面的步骤提前执行 —— 自检里播放器"刚出来就被下一步关掉"就是这么来的。
//    统一改成绝对时间戳。
static LONGLONG tsNext = 0;

static void TsButton(bool running)
{
    if (!stw) return;
    HWND b = GetDlgItem(stw, ID_TRAY_TEST);
    if (!b) return;
    SetWindowTextW(b, running ? L"测试中…" : L"立即测试");
    EnableWindow(b, running ? FALSE : TRUE);
}

static void SelfTestEnd()
{
    tsStep = -1;
    tsWait = 0;
    gSelfTest = false;
    KillTimer(hMain, 14);
    TsButton(false);
}

void SelfTestStart()
{
    if (tsStep >= 0) return;            // 已经在跑就别重复触发
    tsStep = 0;
    tsWait = 0;
    gSelfTest = true;
    TsButton(true);
    SetTimer(hMain, 14, 100, NULL);
}

void SelfTestTick()
{
    if (tsStep < 0) return;
    if (tsNext != 0 && NowMs() < tsNext) return;
    tsNext = 0;

    switch (tsStep)
    {
    case 0:  HideNotch();                            tsNext = NowMs() + 14 * 100; break;   // 迷你岛：收
    case 1:  ShowNotch();                            tsNext = NowMs() + 14 * 100; break;   // 迷你岛：展
    case 2:  HideNotch();                            tsNext = NowMs() + 14 * 100; break;   // 再收
    case 3:  ShowNotch();                            tsNext = NowMs() + 14 * 100; break;   // 再展
    case 4:  TriggerAlarmManual(ALARM_TYPE_CLASS_END);   tsNext = NowMs() + 24 * 100; break;  // 大岛：下课了
    case 5:  TriggerAlarmManual(ALARM_TYPE_CLASS_START); tsNext = NowMs() + 24 * 100; break;  // 大岛：上课了
    case 6:  ShowMiniIsland();                       tsNext = NowMs() + 20 * 100; break;   // 课间小岛倒计时
    case 7:  tsNext = NowMs() + 60 * 100; tsStep++; return;                                // 静置 6 秒 → 看变淡
    case 8:  TriggerAlarmManual(ALARM_TYPE_CLASS_END);   tsNext = NowMs() + 24 * 100; break;  // 再弹个大岛（下一步要它下移）
    case 9:  case 10: case 11: case 12: case 13:
        tsNext = NowMs() + 2 * 100; break;           // AI 岛/多岛已删，这几步空转
    case 14:
        // 直接定格成播放器，不走 islandMode= MODE_TO_BIG 变身 —— 自检要的是"看得见"，动画路线上变量太多
        ply = true; plyA = 255.0f; plyMix = false; ta = 255; ww = 255;
        plyHintUntil = NowMs() + 8000;
        CoverLoad();
        bigH = PLAYER_H; bigW = PLAYER_W;
        ComputeCanvasSize();
        islandW = PLAYER_W; islandH = PLAYER_H; islandY = ISLAND_FINAL_Y; islandX = (B - islandW) / 2;
        islandMode = MODE_BIG_HOLD; hideAtMs = NowMs() + PLAYER_LONG;
        BgmPlay();
        UpdateIslandDisplay(hMain, 1.0f, 255);
        tsNext = NowMs() + 62 * 100; break;
    case 15: ClosePlayer();                          tsNext = NowMs() + 6 * 100;  break;   // 收回
    default: SelfTestEnd(); return;
    }
    tsStep++;
}

// 手动触发（托盘「立即测试」/ Ctrl+Alt+hideAtMs）：绕开"每分钟只放行一次"，并把 N 还回去
void TriggerAlarmManual(AlarmType type)
{
    int keep = N;
    AudioPlayAsync(type);
    TriggerAlarm(type);
    N = keep;
}

void TriggerAlarm(AlarmType type)
{
    if (g_examOn) return;           // 考试模式：铃声照响（在调用方），但岛不弹
    // 闹钟类一律配天空 —— 包括「立即测试」：它就是"预览真实闹钟长什么样"的入口，
    // 不给的话测试时看到的是纯色底板，和实际弹出来的效果对不上
    //（我 2026-10-16 看冒烟时说"弹到一半动画消失、变成纯黑背景"就是这个）。
    // 不配天空的只有"消息类"（状态信息/下次提醒时间/开机自启动/隐私提示），它们在 ShowMessageIsland 里清掉。
    skyAlarm = true;
    KillTimer(hMain,1); KillTimer(hMain,2); KillTimer(hMain,3); KillTimer(hMain,4);
    animStep=0; P=0;
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

    // 2026-11-13：闹钟弹的是"时间岛"，顶行该显示 AlarmLabel（上课了/下课了），不是别处留下的小标题。
    // infoCustom 是 ShowMessageIsland 的第二个参数（课表岛用它传"下节课是"），
    // 但闹钟这条路以前完全不碰它 —— 于是弹过一次课表岛之后，后面每次「上课了」顶行都还挂着"下节课是"。
    // schedMode 同理（它会带左对齐），一起清。
    infoCustom[0] = 0;
    schedMode = false;

    // 特殊日期（中秋/国庆，含前一天）：正好是放学的时侯，
    // 先照常显示「下课了」+ 时间；等它显示完，再让文字淡出、换上「国庆节快乐」
    // （走 islandMode= MODE_HOLIDAY 两段式，见 AnimTick）—— 所以这里只登记"待会儿要接祝福"，不打断正常下课流程
    if (lastEnd && HolidayToday() && !HolidayEggShownToday())
    {
        MarkHolidayEggShown();
        he = true;
    }
    // 放学不再放《回家》了：音乐改成固定 19:30 播（case 10 里触发），放学这一下只打铃。
    bool wasMini = (islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD);
    sf0 = 0.0f;                                 // 迷你岛也改圆角了，sf 恒 0（不再有"上两角直角"这套）
    sf1 = 0.0f;
    if (islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD) nt = false;        // 迷你岛 → 交给变身接管，形状切回普通圆角
    if (type == ALARM_TYPE_CLASS_END)
    {
        // 下课：倒计时"立刻"开始跑（不管大岛是否还在显示），大岛走完再变身小岛
        LONGLONG tgt = 0;
        if (NextBreakTarget(&tgt)) { pp = true; rr = tgt; bb = true; }
        else                       { pp = false; bb = false; }

        // 2026-11-13 用户要的三段流程：
        //   ① 「下课了」按勾选显示（这段就是当前这段代码，勾选没开根本不会进来）
        //   ② 3 秒后弹课表「下节课是 XXX」—— **每节课都弹，不管这节课勾没勾**
        //   ③ 课表 5 秒后自动收回，回到小岛倒计时
        // 注意第 ② 步不受 g_segOn / PeriodEnabled 影响，只要真下课了就弹。
        g_schedPopupAt = NowMs() + 3000;
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
    // ⚠️ 天空动画的计时起点必须放在这个分叉**之前**：从迷你岛/小岛过来时走的是 islandMode= MODE_TO_BIG 那条路，
    // 只在 islandMode= MODE_BIG_IN 那条分支里设的话，动画永远不启动（天空会定格在"现在"那一帧 —— 踩过）。
    // 从"已经挂在屏幕上的小岛/迷你岛"变成大岛时：按我设置决定是重播一遍动画，
    // 还是接着上一帧直接变过去（不重播的话就不动 skyT0，天空是连续的）。
    // 重播时多走一段"收回"：先把此刻的夜色/天气可见地收回去，再演日出 → 现在。
    if (skyReplay || !wasMini)
    {
        skyT0 = NowMs();
        skyClear = (skyReplay && wasMini);
    }
    else skyClear = false;
    if (wasMini)
    {
        ae = islandW; an = islandH; ao = islandY;              // 从小岛/迷你岛"当前"尺寸接着长，接缝处不跳变
        gw = bigW; gh = ISLAND_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 3;
        oo = 0; vv0 = 1.0f; vt = 0.0f; vv = 1.0f; islandMode = MODE_TO_BIG;
    }
    else
    {
        vv0 = 0.0f; vt = 0.0f; vv = 0.0f;    // 大岛文字从实心开始
        sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;   // 从零弹出的大岛＝普通圆角（别漏成迷你岛的直角）
        islandMode = MODE_BIG_IN;
    }
    {   // 记录本分钟已触发，避免"定时线程"与"每分钟兜底检查"重复弹出两次
        SYSTEMTIME st; GetLocalTime(&st);
        N = st.wHour*60 + st.wMinute;
    }

    PrepareBackdropBeforeShow();
    R = NowMs(); // 交主循环 60fps 调度
}

// 2026-11-14 修"手指点迷你岛没反应"：
//   点击判定原来只写在 WM_LBUTTONUP 里。触控走的是 WM_POINTERUP / WM_TOUCH 的 UP 分支，
//   那两处只把 xx/af 清掉就走人 —— 手指按下去、松开，什么都不会发生（鼠标却正常）。
//   现在抽成一个函数，鼠标 / 指针 / 触摸三条路都调它。
static void TryTapIsland(POINT up)
{
    // 拖过就别当点击（触控拖动、长按滑出去都算）
    // 注意：抬起点必须由调用方传进来。以前想用 GetCursorPos()，但触控的合成消息不会移动光标，
    // 拿到的还是上次鼠标的位置，判定就会误判成"拖过 12px"。
    if (abs((int)up.x - (int)g_tapDownPt.x) > 12 || abs((int)up.y - (int)g_tapDownPt.y) > 12) return;

    // 去重：同一根手指在部分机器上会同时投递 pointer 和合成鼠标消息，
    // 不去重会连弹两次（课表岛刚出来又被第二次点击顶掉）。
    static LONGLONG lastTap = 0;
    LONGLONG now = NowMs();
    if (now - lastTap < 250) return;
    lastTap = now;

    // 只在"常驻迷你岛 / 课间小岛"上算点击；大岛有自己的处理，倒计时岛不响应
    bool want = (islandMode == MODE_NOTCH_HOLD || islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD);
    if (!want) return;

    int nh = 0, nm = 0, np = -1;
    if (NextClassStart(&nh, &nm, &np)) ShowScheduleIsland();
    else ShowMessageIsland(L"今天没课了", 0, L"接下来");
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
        POINT cp; GetCursorPos(&cp);
        g_tapDownPt = cp;

        if (PlayerButtonHit(cp)) return 0;         // 点在播放/暂停键上 → 只切播放状态，别起长按
        if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
        SetCapture(hWnd);
        return 0;
    }
    case WM_LBUTTONUP:
        xx = false; af = false;
        if (GetCapture() == hWnd) ReleaseCapture();
        { POINT up; GetCursorPos(&up); TryTapIsland(up); }   // 鼠标和触控共用同一个点击判定
        return 0;
    case WM_CAPTURECHANGED:
        if (xx) { xx = false; af = false; }
        return 0;

    // 触控（指针模型）：Windows 会把触摸派发为 WM_POINTER
    case WM_POINTERDOWN:
    {   // WM_POINTER 的 GET_X/Y_LPARAM 给的就是屏幕坐标，正好直接拿去命中
        POINT cp = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        g_touchPt = cp;
        g_tapDownPt = cp;
        g_touchDown = true;
        if (PlayerButtonHit(cp)) return 0;
        if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
        return 0;
    }
    case WM_POINTERUPDATE:              // 手指移动：只更新坐标，手势/拖动由 PollGesture 读
        g_touchPt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        g_touchDown = true;
        return 0;
    case WM_POINTERUP:
        g_touchDown = false;
        {
            POINT up = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (xx) { xx = false; af = false; TryTapIsland(up); }
        }
        return 0;
    case WM_POINTERCAPTURECHANGED:      // 被系统收走（不是正常抬起）→ 不算点击
        g_touchDown = false;
        if (xx) { xx = false; af = false; }
        return 0;

    // 触控（WM_TOUCH 模型，RegisterTouchWindow 之后走这条）
    case WM_TOUCH:
    {
        UINT nTouch = LOWORD(wParam);
        TOUCHINPUT ti[8];
        if (nTouch > 0 && nTouch <= 8 && GetTouchInputInfo((HTOUCHINPUT)lParam, nTouch, ti, sizeof(TOUCHINPUT)))
        {
            for (UINT k = 0; k < nTouch; k++)
            {
                // TOUCHINPUT 的坐标单位是 1/100 像素、且是屏幕坐标
                POINT cp = { (LONG)(ti[k].x / 100), (LONG)(ti[k].y / 100) };
                if (ti[k].dwFlags & TOUCHEVENTF_DOWN)
                {
                    g_tapDownPt = cp;
                    g_touchPt = cp; g_touchDown = true;      // 触控按下：记下来给 PollGesture 用
                    if (PlayerButtonHit(cp)) continue;
                    if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
                }
                if (ti[k].dwFlags & TOUCHEVENTF_MOVE)        // ★ 手指移动：这条以前根本没处理，所以拖不动
                {
                    g_touchPt = cp; g_touchDown = true;
                }
                if (ti[k].dwFlags & TOUCHEVENTF_UP)
                {
                    g_touchDown = false;
                    if (xx) { xx = false; af = false; TryTapIsland(cp); }
                }
            }
            CloseTouchInputHandle((HTOUCHINPUT)lParam);
        }
        return 0;
    }

    case WM_ALARM_TRIGGER:
        TriggerAlarmByTimer((AlarmType)wParam, (int)lParam);
        break;

    case WM_HOTKEY:
        if (wParam == HOTKEY_ID_DEBUG)
            TriggerAlarmManual(ALARM_TYPE_SINGLE);      // 快捷键同样是"手动"，也绕开每分钟去重
        else if (wParam == HOTKEY_ID_SETTINGS)
            ShowSettingsWindow(hMain);                      // Ctrl+Alt+hideAtMs：打开设置（以前注册了却没接上）
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
        case ID_TRAY_TEST: SelfTestStart(); break;
        case ID_SET_LOWPERF:
        {
            bool on = false;
            if (sLowPerf) on = (SendMessageW(sLowPerf, BM_GETCHECK, 0, 0) == BST_CHECKED);
            gFrameMs = on ? 33 : 16;
            SavePerfCfg();
            return 0;
        }
        case ID_TRAY_NEXT: ShowNextAlarmTime(); break;
        case ID_TRAY_AUTOSTART: ToggleAutoStart(); break;
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
            if (islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_HOLD) UpdateIslandDisplay(hWnd, 1.0f, 255);
            break;
        }
        case ID_TRAY_HOLIDAY:
            hon = !hon;
            SaveHolidayCfg();
            if (islandMode == MODE_BIG_HOLD || islandMode == MODE_NOTCH_HOLD) UpdateIslandDisplay(hWnd, 1.0f, 255);   // 立刻换色/去掉节日色
            break;
        case ID_SKY_MODE_SKY:                  // 底板：天气模式
        case ID_SKY_MODE_SOLID:                // 底板：纯色模式
            skyEn = (LOWORD(wParam) == ID_SKY_MODE_SKY);
            SaveSkyCfg();
            SetSyncControls();                 // 纯色模式下把亮度/动画那几个控件灰掉
            if (islandMode == MODE_BIG_HOLD || islandMode == MODE_MINI_IN || islandMode == MODE_MINI_HOLD || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD)
                UpdateIslandDisplay(hWnd, 1.0f, 255);
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
                (unsigned long)osBuild,
                B, scrH, Q ? L"已启用" : L"未启用",
                (islandMode == MODE_MINI_HOLD || islandMode == MODE_MINI_IN) ? L"正在显示" : ((islandMode == MODE_TO_MINI || islandMode == MODE_TO_BIG) ? L"变身中" : (bb ? L"待显示" : L"未显示")),
                MINI_WIDTH, MINI_HEIGHT,
                fuck ? L"开" : L"关", ab, FUCK,
                nn[0] ? nn : L"未找到 alin.mp3",
                L"轮询",
                (HolidayToday() == 0) ? L"今天不是节日" : HolidayGreet(),
                cw, ch,
                bigW, ISLAND_HEIGHT);
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
        // 独立音量面板（全自绘那个小窗）2026-10-13 我要求删掉，入口一并撤了。
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
        case 14:
            SelfTestTick();          // 立即测试的推进（100ms）
            break;

        case 22:
        {
            KillTimer(hMain, 22);
            FILE* lf = fopen("islandX:/aab/l.log", "w");
            if (lf) { fprintf(lf, "BEFORE l=%d j=%d cc=%d V=%p X=%p\n", ch, cw, cc, (void*)V, (void*)X); fclose(lf); }
            ch += 200;
            UpdateIslandDisplay(hMain, 1.0f, 255);
            lf = fopen("islandX:/aab/l.log", "a");
            if (lf) { fprintf(lf, "AFTER  l=%d j=%d cc=%d V=%p X=%p\n", ch, cw, cc, (void*)V, (void*)X); fclose(lf); }
            break;
        }

        case 23:
            KillTimer(hMain, 23);
            ShowMessageIsland(L"距上课 03:00");
            break;

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
                    TriggerAlarmByTimer(alarm.type, alarm.period);
                    break;
                }
            }

            // 自定义提醒（单点）：每天 / 一次性
            {
                int curMin2 = st.wHour * 60 + st.wMinute;
                if (g_lastShotMin != curMin2)
                {
                    for (auto& sh : g_shots)
                    {
                        if (!sh.daily && sh.used) continue;
                        if (sh.hour != st.wHour || sh.minute != st.wMinute) continue;
                        wcscpy_s(g_customText, sh.text);
                        // ⚠️ 不能走 TriggerAlarmByTimer：它开头有 `if (N == curMin) return`，
                        //    那是给"作息表闹钟"防同一分钟重复弹的。自定义提醒有自己的
                        //    g_lastShotMin 去重，复用那条会被 N 挡掉、还设不上 g_lastShotMin
                        //    （2026-11-04 卡了一小时）。
                        AudioPlayAsync(ALARM_TYPE_CLASS_START);   // 有声音才叫提醒
                        TriggerAlarm(ALARM_TYPE_SINGLE);
                        if (!sh.daily) { sh.used = true; SaveShots(); }
                        g_lastShotMin = curMin2;
                        break;
                    }
                }
            }

            int nowMin = st.wHour * 60 + st.wMinute;

            SelfTestTick();       // 自测推进（100ms 定时器为主，这里只是兜底）
            // ⓪ 麦克风 / 摄像头被占用提示 —— 2026-10-22 我决定**关闭**。
            // 原因：Windows 那份隐私记录只覆盖 UWP / 走系统相机栈的应用；
            //      希沃这类 Win32 + DirectShow 程序根本不写那个键，所以永远检测不到，
            //      反而在"别的软件正常用摄像头"时给出误导性的提示。与其误导，不如不要。
            //      （函数和图标都留在这儿，将来要做 DirectShow 正向探测时能接回来。）
            (void)0;

            // ① 《回家》固定 19:30 播（我 2026-10-13 从"放学那一下"挪过来的）。
            //    和下课铃完全解耦：放学照常打铃，音乐到点自己响。
            //    nb 每分钟都跟着走，所以第二天同一时刻还会再触发（只比"小时:分"会漏掉第二天）。
            if (nb != nowMin)
            {
                nb = nowMin;
                int bh = BgmHour(), bm = BgmMinute();
                if (bh >= 0 && st.wHour == bh && st.wMinute == bm && bgmOn && !bgmPlay)
                {
                    BgmPlay();
                    if (bgmPlay) ShowPlayerIsland();   // 音乐真起来了才弹播放器岛
                }
            }

            // ② 全局上课倒计时：只要"这会儿在课间"，就该有小岛显示倒计时，**不看这一节有没有打钩**
            //    （打钩只管铃声）。原来只有"打了钩的下课闹钟"才会顺手把小岛叫出来，
            //    于是没打钩的课间彻底没有倒计时 —— 我报的就是这个。
            //    islandMode==MODE_IDLE 是空屏、11/12 是迷你岛挂着，这两种情况都该让位给倒计时小岛；
            //    大岛/变身(j1/3/4/5/9/10)优先，不抢。
            if (islandMode == MODE_IDLE || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_HOLD)
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
            // 真正的清理在 islandMode= MODE_BIG_OUT 收回动画走完那一步
            if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_HOLD) { P = 0; islandMode = MODE_BIG_OUT; }
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
    // 2026-11-11：原来是 "IslandAlarm"，和 V1.2 主线共用同一个值 ——
    // 两边互相覆盖，开 Turbo 自启动会把主线的顶掉（反之亦然）。
    const wchar_t* szValueName = L"IslandAlarmTurbo";
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
    HANDLE hSingle = CreateMutexW(NULL, TRUE, L"IslandAlarm_SingleInstance_turbo");
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
    osBuild = GetOsBuild();
    timeBeginPeriod(1);
    LoadTimeTable();      // 先定作息表（决定时段数与名字），再读勾选和时间
    MigrateFromV12();     // 首次运行搬 V1.2 的配置（AI 相关跳过）
    LoadSegOn();          // 哪些段还在用（我可能删过）
    LoadColCfg();         // 文字颜色
    LoadTimeOverrides();  // 我改过的时间覆盖要在 LoadAlarmTimes 之前读进来
    LoadAlarmTimes();
    LoadShots();          // 自定义提醒（单点）
    LoadPeriodEnabled();
    // 2026-11-14：主题选项已删（有天空背景之后深浅色没意义），统一按"深色"那套配色：
    // 亮金文字 + 深色底板。以前存进注册表的 Theme 值不再读取。
    z = true;
    LoadPerfCfg();
    LoadAudioCfg();
    LoadExamCfg();          // 考试模式：开机就得知道，学生重启也躲不掉
    LoadMsgCfg();
    LoadSkyCfg();
    LoadWxCfg();
    StartWxThread();
    StartAmp();          // 底部小球要的音频能量（只跟自己的《回家》）      // 天气后台刷新：拿不到就按"晴"画，绝不影响主流程
    CreateIslandWindow(hInstance);
    CreateTrayIcon(hMain);
    RegisterHotKey(hMain, HOTKEY_ID_DEBUG, MOD_CONTROL|MOD_ALT, 'A');
    RegisterHotKey(hMain, HOTKEY_ID_SETTINGS, MOD_CONTROL|MOD_ALT, 'S');   // Ctrl+Alt+S = 打开设置
    RegisterHotKey(hMain, HOTKEY_ID_PLAYER, MOD_CONTROL|MOD_ALT, 'P');     // Ctrl+Alt+P = 测试播放器岛
    StartTimerThread();
    SetTimer(hMain, 10, 1000, NULL);
    // 不装全局鼠标钩子了：WH_MOUSE_LL 是安全软件（含 Defender）重点盯的行为之一。
    // 改成常驻心跳轮询 —— 鼠标和触控都能查到，功能一样，启动路径里少一个可疑 API。
    SetTimer(hMain, 12, 120, NULL);
#ifdef TEST_BUBBLE
    SetTimer(hMain, 23, 4000, NULL);  // 测试：4 秒后弹一个大岛（模拟上课倒计时）
#endif


#ifdef TEST_L
    SetTimer(hMain, 22, 4000, NULL);
#endif

    LoadNotchCfg();
    if (ns)                                   // 迷你岛就是这个程序的"常驻形态"：开机就挂上
    {
        LONGLONG t0 = 0;
        if (BreakInfoActive(&t0)) { pp = true; rr = t0; bb = true; ShowMiniIsland(); }  // 课间中 → 直接给倒计时
        else ShowNotch();
    }
    // 节日彩蛋不在这里弹了：改成"今天最后一节课下课后"由 TriggerAlarm 里弹（见 IsLastClassEndOfDay）

    if (q) SetTimer(hMain, 777, 1000, NULL);

    // 主循环：动画期 16ms 一帧，其余时间睡觉
    MSG msg;
    bool running = true;
    while (running)
    {
        DWORD wait = INFINITE;
        if (islandMode == MODE_BIG_IN || islandMode == MODE_BIG_OUT || islandMode == MODE_TO_MINI || islandMode == MODE_MINI_IN || islandMode == MODE_MINI_OUT || islandMode == MODE_TO_BIG || islandMode == MODE_HOLIDAY || islandMode == MODE_NOTCH_IN || islandMode == MODE_NOTCH_OUT)
        {
            LONGLONG now = NowMs();
            if (now >= R)
            {
                GlobalsToState(gA);
                AnimTick(gA);
                R = NowMs() + gFrameMs;
            }
            LONGLONG remain = R - NowMs();
            wait = (remain > 0) ? (DWORD)remain : 0;
        }
        else if (islandMode == MODE_BIG_HOLD)
        {
            LONGLONG now = NowMs();
            if (ply) PlayerTick();           // 播放器：把"长按关闭播放器"那行提示淡出去
            if (now >= hideAtMs)
            {
                if (he)      { he = false; hs = 0; ta = 255; islandMode = MODE_HOLIDAY; }   // 特殊日期 → 接"两段式"，大岛形状不动
                else if (ply) { ShowPlayerIsland(); }                    // 放学音乐在放 → 大岛原地变播放器并常驻
                else if (bb) { hh = false; ae = islandW; an = islandH; ao = islandY; gw = MINI_WIDTH; gh = MINI_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 7;
                               vv0 = vv; vt = 1.0f; oo = 0; sf0 = sf; sf1 = 0.0f; islandMode = MODE_TO_MINI; }   // 有常驻小岛 → 直接变身
                else if (ns) {
                    {
                        hh = false; ae = islandW; an = islandH; ao = islandY; gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
                        vv0 = vv; vt = 1.0f; oo = 0; nt = true; sf0 = sf; sf1 = 0.0f; islandMode = MODE_TO_MINI;
                    }
                }   // 迷你岛开着 → 直接缩成迷你岛
                else         { islandMode = MODE_BIG_OUT; P = 0; }               // 否则照常收回
                R = now;
            }
            LONGLONG remain = hideAtMs - NowMs();
            if (remain < 1) remain = 1;
            if (remain > 60000) remain = 60000;
            wait = (DWORD)remain;
            // 播放器定格时 hideAtMs 被顶到 10 分钟后，不能让循环一口气睡 60 秒 ——
            // 那样下面那行提示就没法淡出了。提示还在淡的时候 60ms 一拍，淡完降到 500ms。
            if (ply)
            {
                DWORD pw = (ww > 0 || bgmPause) ? 60 : 500;   // 暂停时要盯 5 秒的自动关闭
                if (wait > pw) wait = pw;
            }
            // 天空里有"要动的东西"（雨雪在下、星星在闪）→ 岛显示期间就一直重绘，
            // 不能停在某一帧（我明确："下雨在大岛的时候要一直播放，不是到某一帧就停下"）。
            // 播放器岛是长驻（10 分钟）不跟这个 25fps 跑，省 CPU。
            // 2026-11-11：原来 wait=40（25fps），太阳走起来是一跳一跳的，尤其没有云的时候特别明显。
            // 天空动画（日出→现在）只有 3.6 秒，这期间按 60fps 跑，之后自然回落到下面的等待。
            if (!ply && (SkyAnimating() || sky.rainy))
            {
                UpdateIslandDisplay(hMain, 1.0f, 255);
                // 2026-11-11 我定的档位：
                //   天空动画 42fps（24ms）—— 再高没必要
                //   雨雪 25fps、云 25fps（不动，我说云别改）
                //   只有星星闪的时候 20fps（50ms，星星闪得慢，25 是浪费）
                if (SkyAnimating())            wait = 24;
                else if (sky.nDrop > 0)        wait = 40;
                else if (sky.cloudAmp > 0.02)  wait = 40;
                else                           wait = 50;
            }
            // 播放器岛常驻 10 分钟，但底部音波要动 —— 60ms 一帧足够（缓慢律动），别按 25fps 烧 CPU
            else if (ply && bgmPlay && !bgmPause) { UpdateIslandDisplay(hMain, 1.0f, 255); wait = 16; }   // 60fps：40ms 看着像一跳一跳
        }
        else if (islandMode == MODE_MINI_HOLD)
        {
            MiniHoldTick();          // 常驻小岛：倒计时刷新 / 长按判定 / 提示淡入淡出
            wait = 33;
        }
        else if (islandMode == MODE_NOTCH_HOLD)
        {
            islandW = NOTCH_WIDTH; islandH = NOTCH_HEIGHT; islandY = NOTCH_Y; islandX = (B - islandW) / 2; sf = 0.0f;
            NotchHoldTick();         // 迷你岛：分钟跳变重绘 + 静止够久降半透明
            wait = 33;
        }

        // 2026-11-13 课表三段流程的第 ② 步：到点了就弹「下节课是 XXX」。
        // 放在这里而不是 MiniHoldTick —— 下课那会儿还是大岛（MODE_BIG_HOLD），MiniHoldTick 不跑。
        if (g_schedPopupAt)
        {
            LONGLONG left = g_schedPopupAt - NowMs();
            if (left <= 0)
            {
                g_schedPopupAt = 0;
                ShowScheduleIsland();
                wait = 33;
            }
            else if (left < (LONGLONG)wait) wait = (DWORD)left;   // 别一觉睡过头
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
