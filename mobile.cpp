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
#include <winhttp.h>  // 取实时天气（后台线程、超时短、失败静默）
#include <mmdeviceapi.h>  // WASAPI：抓系统输出算音量（MCI 拿不到 PCM，只能回环）
#include <audioclient.h>
#include <gdiplus.h>  // 解码 Home.jpg / Home.png 当专辑封面（GDI+ 比 OleLoadPicturePath 稳）
#include <litehtml.h>   // AI 岛气泡排版：直接画进岛的位图，跟着变形一起缩放
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
// 用户要求：小岛只管倒计时，实时时间归迷你岛；迷你岛静止一段时间后降到半透明。
const int NOTCH_WIDTH   = 150;   // 迷你岛宽度
const int NOTCH_HEIGHT  = 48;    // 迷你岛高度

// ===== AI 岛（2026-10-21）=====
// 它是一块独立窗口 —— 岛本身是分层窗口，放不了 EDIT 这类子控件。
// 打开时整块岛窗口往下让位（aiShiftY），保证 AI 岛永远在最顶上。
// AI 岛天空蒙版的强度：0=纯天空（白字会糊），1=原来的纯深色
// 编译时可以用 -DAI_SKY_MASK=0.55f 覆盖，方便出几版对比
#ifndef AI_SKY_MASK
#define AI_SKY_MASK 0.72f
#endif

const int AI_W   = 640;          // AI 岛宽
const int AI_H   = 360;          // AI 岛高（两段式：上方气泡区 + 下方输入框）
const int AI_GAP = 8;            // AI 岛和其他岛之间的空隙
// AI 岛不再是独立窗口，而是"岛的一个状态"：J==3 定格里，aiMode=true 时画 AI 内容。
// 这样它和"小岛变大岛"用的是同一个窗口、同一套曲线、同一个 DrawIsland —— 物理上是同一个对象。
bool aiMode = false;
// AI 岛背景：0 = 天空（其他岛），1 = 深色 #26262e。变形过程中线性过渡。
float aiBgMix = 0.0f;
// AI 岛当前高度：输入态只有一条输入框（120），发过问题后长成两段式（360）。
int   aiH = 120;                  // AI 岛当前高度（120/200 输入态，360 对话态）
std::vector<std::wstring> aiHist;   // 历史问句（最多 10 条，最新的在前）
int aiHistHit = -1;                 // 长按落在第几条历史（-1 = 不在历史区）
LONGLONG aiHistDown = 0;            // 按下那一刻
bool aiHistLongDone = false;        // 这次按已经删过了，抬起时别再展开
const int AI_H_EMPTY = 200;         // 输入态（带历史区）的高度
static std::vector<std::wstring> aiQs;   // 本轮问答（问）
static std::vector<std::wstring> aiAs;   // 本轮问答（答）
static void AiHistPush(const std::wstring& q, const std::wstring& a);
static void AiHistSplit(const std::wstring& rec, std::wstring& q, std::wstring& a);
bool  aiCaret = true;
LONGLONG aiLastAct = 0;   // 最后一次交互时间，闲置 30s 自动收回
int aiScroll = 0;          // 气泡区滚动偏移（0=顶部，越大越往上）

// Key 窗"被迷你岛吞掉"的回收（AnimTick 里要用，放顶部）
int      kwClosing = 0;
LONGLONG kwcT0 = 0;
bool KwVisible();
void HideKeyWindowNow();
std::wstring aiInput;            // 输入内容（这轮只做视觉，不接受键盘）
int  aiThink2 = 1;               // 0=off 1=high 2=max
int aiShiftY = 0;                // AI 岛打开时其他岛的下移量
HWND gAiWnd = NULL;              // AI 岛窗口句柄（非空时，岛要排在它下面）

// 迷你岛改成"悬浮"样式（2026-10-13 用户要求）：不再贴着屏幕顶边，而是像大岛/小岛那样浮在下面，
// 只留 2 个像素的缝 —— 大改动是"四角都圆角"（原来上两角是直角）。
// 好处是和大岛/小岛之间切换时形状不用变，视觉上顺得多；缝只有 2px，不仔细看还是"贴顶"的感觉。
const int NOTCH_Y       = 2;     // 迷你岛距屏幕顶边的缝隙（用户要 1~2px）
const int NOTCH_DIM_MS  = 6000;  // 静止多久才降半透明（原来 10 秒，用户嫌一直不变透明，缩到 6 秒）
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
const int PLAYER_LONG  = 180000; // 播放器兜底时长（3 分钟）。正常靠 MM_MCINOTIFY 放完就收，这只是防通知丢失的保险
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

// ===== 一个岛的全部状态（2026-10-24 结构体化，为了"并排显示两个岛"）=====
// 以前这些是一堆散落的全局变量，一个岛就能把它们全占满；
// 现在每个岛一份：主岛 gA 一份，每个消息岛槽（gBubs[i].st）各一份，互不覆盖。
struct IslandState
{
    // 位置尺寸
    int D, E, F, G;
    // 状态机：0空闲 / 1-4大岛 / 5大↔小变身 / 6-8小岛 / 9小→大 / 10节日 / 11-13迷你岛
    int J, P, O;
    float I;
    // 时间：R=下一帧，S=定格结束
    LONGLONG R, S;
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
    // AI 岛
    bool aiMode;
    float aiBgMix;
    int aiH, aiScroll;
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

// ===== AI 岛下面挂着的"原岛"（2026-10-25 第 12 项）=====
// 用户要的不是"再造一个替身岛"，而是：AI 岛从顶上飞出来时，
// 屏幕上原来那个岛**整体平移下去**，样式一点不变（迷你岛还是迷你岛、大岛还是大岛）。
// 所以这里存的是打开 AI 岛那一刻的完整快照，绘制时只改 E，别的字段一律不动。
IslandState gBelow;
bool belowOn = false;
int  belowY = 0, belowTargetY = 0, belowHomeY = 0;
bool belowRestore = false;      // AI 岛收起后，原岛要滑回原来的位置
int  restoreOff = 0;            // 滑回过程中，绘制时临时叠在 y 上的偏移（状态机不知道它的存在）

// AI 岛收起不是"啪一下没了"：留一份画面，往上飘 + 淡出
IslandState gAiOut;
bool  aiFading = false;
float aiFade   = 0.0f;          // 1=完整，0=没了

// 第 10 项：大岛那行字"原地变成输入框"的起点时刻。
// 变身开始记一下时间，绘制时按 0.42 秒把旧字从岛中心滑到输入框位置、缩到 15px、淡出。
LONGLONG aiMorphT0 = 0;
// 消息岛不再是一个全局 gB，而是 gBubs[]（见 2400 行附近），每个槽自己带一份 IslandState

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
static int      cc = 0;               // 画布缓存宽度
static int      cch = 0;              // 画布缓存高度（2026-10-24 加回：之前只判宽度，画布加高后 DIB 不重建，UpdateLayeredWindow 读越界直接崩）
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
    L"第6节课", L"课后服务1", L"课后服务2", L"课后服务2"
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
bool      gSelfTest = false;        // 「立即测试」跑着的时候为 true（屏蔽真实闹钟）
// 每节课的上课铃/下课铃开关（N3）。默认全开，保持老版本行为。
bool      bellUp[11] = { true,true,true,true,true,true,true,true,true,true,true };
bool      bellDn[11] = { true,true,true,true,true,true,true,true,true,true,true };
// 每节课两个铃各自的音量（0~100），默认 30 跟全局默认一致
int       bellUpVol[11] = { 30,30,30,30,30,30,30,30,30,30,30 };
int       bellDnVol[11] = { 30,30,30,30,30,30,30,30,30,30,30 };
float     plyA = 255.0f;            // 播放器内容的不透明度（"下课了 → 播放器"过渡时 0→255）
bool      plyMix = false;           // 正在做"下课了 → 播放器"的过渡：ta/plyA 交给 J=9 的进度驱动
LONGLONG  plyPauseAt = 0;           // 按下暂停的时刻（用来判"暂停 5 秒还没播就收掉"）
// 麦克风/摄像头提示
// ===== 天空背景用的全局（绘制部分在 DrawIsland 前面，这里只放状态）=====
LONGLONG  skyT0 = 0;                // 本次"日出 → 现在"动画的起点（NowMs）
// 小岛 → 大岛、且用户选了"重播"时：先花这段把当前天色/天气**倒着收回去**，
// 再演日出 → 现在。用户要求"能看出来是收回去的，别像直接切"，所以给到 1.1 秒。
const int SKY_CLEAR_MS = 1150;
bool     skyClear = false;            // 这次动画前面有没有"收回段"
// 只有"上课了 / 下课了 / 课间倒计时"这三处才跑天空，其它消息一律不跑：
// 由 TriggerAlarm 按闹钟类型置位，ShowMessageIsland 一律清零。
bool      skyAlarm = false;
int       skyBr = 38;               // 天空亮度 0~100（注册表 SkyBright，默认 38）
// 小岛/迷你岛 → 大岛 时，天空动画要"重新播一遍"还是"直接接着变"（注册表 SkyAnimReplay）
// 默认不重播：用户说"大岛毫无征兆地把动画抢过来重播一遍"很别扭。从空屏弹出时无所谓，一定会演。
bool     skyReplay = false;
int      notchA = 45;              // 迷你岛静止后的透明度 %（注册表 NotchAlpha，默认 45）
int      skyEn = 1;                // 天空背景总开关（注册表 SkyOn，0=纯色底板）
// 联网取到的**真实**日出/日落（当天分钟数，-1=没有）。SkyToday() 优先用它们，
// 拿不到才退回本地天文算法 —— 实测本地简化公式能差 7~8 分钟（2026-10-18 核对过）。
int      sunRiseMin = -1, sunSetMin = -1, sunDay = 0;
// 迷你岛时间"背后"的实际亮度：太亮就把字改成深色（只改字，不动背景）
int       notchLum = 128;

// 采一下迷你岛左边那条（那里没有字）的**实际屏幕颜色** —— 那正是时间背后压着的东西。
// 每秒调一次就够，开销可以忽略。
void SampleNotchLum()
{
    if (F < 40 || G < 12) return;
    int sx = D + 4, sy = E + 4, sw = 12, sh = G - 8;
    if (sx < 0 || sy < 0 || sh < 4) return;
    HDC dc = GetDC(NULL);
    if (!dc) return;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, sw, sh);
    HGDIOBJ old = SelectObject(mem, bm);
    if (BitBlt(mem, 0, 0, sw, sh, dc, sx, sy, SRCCOPY))
    {
        BITMAPINFO bi = {0};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = sw;
        bi.bmiHeader.biHeight = -sh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        static BYTE buf[12 * 96 * 4];
        if (GetDIBits(mem, bm, 0, sh, buf, &bi, DIB_RGB_COLORS))
        {
            long sum = 0;
            for (int i = 0; i < sw * sh; i++)
                sum += (buf[i*4+2] * 299 + buf[i*4+1] * 587 + buf[i*4] * 114) / 1000;
            notchLum = (int)(sum / (sw * sh));
        }
    }
    SelectObject(mem, old); DeleteObject(bm); DeleteDC(mem); ReleaseDC(NULL, dc);
}
void SampleNotchLum();
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
#define ID_TRAY_SKY        1019   // 天空背景开关
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
void DrawIsland(BYTE* p, int stride, int width, int height, IslandState& IS, bool clearCanvas = true);
void CompositeGlyph(BYTE* p, const BYTE* mask, int stride, int x0, int y0, int x1, int y1, COLORREF color);
bool EnsureBuffers();
void CleanupCachedResources();
void AnimTick(IslandState& IS);
LONGLONG NowMs();
void LoadTheme();
void SaveTheme();
void ShowMessageIsland(const wchar_t* text, int extraW = 0);
static bool BubblePromoteToMain();   // AI 岛收起时把消息岛升级成主岛大岛（实现在 ShowMessageIsland 前面）
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
static bool AiLoadKey(std::wstring& out);
static void AiSaveKey(const wchar_t* v);
// 删除已保存的 Key：直接删注册表值，而不是存一个空串
static void AiDeleteKey()
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS)
    {
        RegDeleteValueW(k, L"AiKey");
        RegCloseKey(k);
    }
}
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

// 圆角覆盖率，边界 1.5px 羽化
static inline float ShapeCoverage(int lx, int ly, int w, int h, int rx, int ry)
{
    // 迷你岛尺寸（150×48 上下）一律四角全圆：sf 这个全局量在多次变身之后会残留，
    // 光靠"结束时分身"收不干净，这里按尺寸兜底，最稳。
    float sfUse = (w <= NOTCH_WIDTH + 12 && h <= NOTCH_HEIGHT + 12) ? 0.0f : sf;
    return IslandCoverage(lx, ly, w, h, rx, ry, sfUse);
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
                                int r = (int)(sr / cnt), g = (int)(sg / cnt), b = (int)(sb / cnt);
                                int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
                                if (mx >= 36)
                                {
                                    double kk = 205.0 / mx;          // 按比例提亮，色相不变
                                    r = (int)(r * kk); g = (int)(g * kk); b = (int)(b * kk);
                                    if (r > 255) r = 255;
                                    if (g > 255) g = 255;
                                    if (b > 255) b = 255;
                                    covMainColor = RGB(r, g, b);
                                    // 副色：主色反相再提亮 —— 暖棕会变成干净的蓝，对比够但不像"脏灰"
                                    int rr = 255 - r, gg = 255 - g, bb = 255 - b;
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
    F = (int)(ISLAND_MIN_WIDTH  + (tw - ISLAND_MIN_WIDTH ) * springW(t));
    G = (int)(ISLAND_MIN_HEIGHT + (th - ISLAND_MIN_HEIGHT) * springH(t));
    E = (int)(ISLAND_FINAL_Y * springY(t));
    D = (B - F) / 2;
    (*step)++;
}

// 让常驻小岛出现（大岛正在显示时不动 —— 大岛优先，等它走完自动变身）
void ShowMiniIsland()
{
    if (aiMode) return;          // AI 岛永远在最上面，别的岛不许抢
    // ★ 小岛只有"课间倒计时"这一种用途。
    //   用户 2026-10-27：以前那种"用小岛样式只显示时间"早就该删了 —— 那个形态已经全交给迷你岛。
    //   不在课间就不进这个形态，直接落回迷你岛，免得走出一条"仅时间的小岛"。
    LONGLONG t0 = 0;
    if (!BreakInfoActive(&t0)) { ShowNotch(); return; }
    pp = true; rr = t0;
    bb = true;
    hh = false;          // 常驻岛不显示消息，否则 tBig=!hh 会把它画成空壳
    skyT0 = NowMs();     // 课间倒计时也是"要跑动画的三处之一"，从头演一遍
    skyClear = false;    // 小岛出现不搞"收回"那套（没有从大岛缩下来的过程）
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
            // ⚠️ pp 必须清掉：不清的话"距上课倒计时"会一路带到迷你岛上
            //（用户报的"带着 距上课还剩 0:0 回到迷你岛，再直接弹回去变成仅时间"）。
            // 清了之后 tMini 立刻为真 → 走"时间层"，迷你岛从变身一开始就显示时间。
            pp = false;
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
    int ch = wf->nChannels > 2 ? 2 : wf->nChannels;
    if (ch < 1) return 0.0f;
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
        {   // 每次重新拿一遍默认设备（用户可能切了声卡）
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
    HINTERNET hS = WinHttpOpen(L"mobile-classbell/1.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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
                        int v = 0, n = 0;
                        while (p < body.size() && body[p] >= '0' && body[p] <= '9')
                        { v = v * 10 + (body[p] - '0'); p++; n++; }
                        if (n > 0) { *outKind = WmoToKind(v); ok = true; }
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
    // 只有后面跟着 "2026-10-18T06:17" 的才算（看 T 之后是否是两位数字）。
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
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
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
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
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
}

// 后台刷新线程：半小时一次；失败 5 分钟后再来。WxForce 非 0 时完全不联网。
void SaveSkyCfg();   // 实现在后面：取到日出日落后顺手存一下

DWORD WINAPI WxThreadProc(LPVOID)
{
    while (true)
    {
        // ⚠️ **不管有没有 WxForce 都要联网**：WxForce 只是手动指定天气种类，
        // 日出日落照样得从网络拿（实测本地简化公式差 7~8 分钟）。
        // 之前写成 force 就直接 continue，结果 force 一开就再也不联网了（踩过）。
        int k = 0;
        if (WxFetch(&k))
        {
            if (!wxForce) wxKind = k;    // force 只决定画哪种天气
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
// （先做过 1 秒，用户反馈"太快了，一闪就过去，看着像没有动画"，2026-10-16 改成 2.5 秒）
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
    auto put = [](int* dst, int r, int g, int b) { dst[0] = r; dst[1] = g; dst[2] = b; };

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
    s.grayK = ((wxKind == 0) ? 0.00 : (wxKind == 1) ? 0.22 : (wxKind == 2) ? 0.55
            : (wxKind == 3) ? 0.75 : (wxKind == 4) ? 0.60 : (wxKind == 5) ? 0.45 : 0.0) * wxk;
    s.fogK  = ((wxKind == 3) ? 0.90 : (wxKind == 4) ? 0.20 : (wxKind == 5) ? 0.24 : 0.0) * wxk;
    s.sunVis = 1.0 - ((wxKind == 2) ? 0.75 : (wxKind == 3) ? 0.95 :
                      (wxKind == 4) ? 0.80 : (wxKind == 5) ? 0.70 : (wxKind == 1) ? 0.25 : 0.0) * wxk;

    // 行渐变色算一套：天顶→地平线插值 + 霞光带 + 天气灰化/起雾
    auto rows = [](const int* z, const int* hz, const int* g, double gray, double fog,
                   int* oR, int* oG, int* oB)
    {
        for (int i = 0; i < SKY_ROWS; i++)
        {
            double v = (double)i / (SKY_ROWS - 1);    // 0=天顶 1=地平线
            double k = pow(v, 0.72);                  // 地平线那一侧色变快一点，更像真天空
            int R = (int)(z[0] + (hz[0] - z[0]) * k);
            int G = (int)(z[1] + (hz[1] - z[1]) * k);
            int B = (int)(z[2] + (hz[2] - z[2]) * k);
            // 地平线附近压一条霞光带
            double gl = exp(-(v - 0.97) * (v - 0.97) * 340.0);
            R = (int)(R + (g[0] - R) * gl * 0.55);
            G = (int)(G + (g[1] - G) * gl * 0.55);
            B = (int)(B + (g[2] - B) * gl * 0.55);
            // 阴/雾：往"同亮度的灰"靠；雾再额外往雾灰提亮（雾其实是发白的，越靠地面越厚）
            if (gray > 0.001)
            {
                double lum = 0.299*R + 0.587*G + 0.114*B;
                R = (int)(R + (lum - R) * gray);
                G = (int)(G + (lum - G) * gray);
                B = (int)(B + (lum - B) * gray);
            }
            if (fog > 0.001)
            {
                double f = fog * (0.35 + 0.65 * k);
                R = (int)(R + (168 - R) * f);
                G = (int)(G + (172 - G) * f);
                B = (int)(B + (180 - B) * f);
            }
            oR[i] = R; oG[i] = G; oB[i] = B;
        }
    };
    rows(zc, hc, gc, s.grayK, s.fogK, s.rowR, s.rowG, s.rowB);
    (void)warm;

    // ---- 收回段：准备一套"日出、无天气"的行色，然后**整块**渐变过去 ----
    s.blendP = 0.0;
    s.decoA  = 1.0;
    s.sunA   = 1.0;
    s.noSun  = false;
    // 迷你岛（J=11/12/13）只显示"现在"这一帧，不参与收回。
    bool canFade = skyClear && !(J == 11 || J == 12 || J == 13);
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
            // **整块**按 blendP 混合：不是一行一行揭，是整体渐变（上一版逐行推进被用户判成渲染 bug）
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
            // 于是月亮会"啪"地冒出来（用户报的"抽一下"，也是日月挤在一起的原因之一）。
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
    // 用户要的是"太阳完全落山之后天才黑"，所以得让它真的沉到地平线以下去（会被岛形裁掉）。
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
    // 这是用户明确指出的毛病 —— 原来天还有太阳挂着就整片变黑了。
    // veil 是"黑到第几行"（0=天顶 1=地平线），地平线附近最后才黑，正好是黄昏该有的样子。
    s.veil = std::min(1.4, std::max(0.0, -alt / 14.0));

    // 月亮：太阳**完全**落山之后才出场（alt<-2），而且要**渐渐亮起来**不能硬蹦
    //（用户原话："月亮毫无征兆的就出来了"）；月相按真实朔望周期算（中秋自然就是满月）
    s.moonR = -1;
    s.moonA = 0;
    // ⚠️ 月亮和太阳要**并行**：从 alt +6° 就开始淡入，太阳还在往地平线下沉的时候
    //    月亮已经在另一边亮起来了（真实天空就是这样；用户要求"某个时间段之后并行"）。
    //    原来从 -2° 才起步，于是"太阳先落完、月亮再出场"，是两段动画。
    if (alt < 6.0)
    {
        double ma = (6.0 - alt) / 14.0;                 // +6° → -8° 之间逐渐亮起来
        // 再压一段"弹出来之后才慢慢浮出"：比星星晚一些，别跟星星齐刷刷同步（用户要求）
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
        // 傍晚太阳落到右边 → 月亮在左边升。两个永远错开，不会挤在一处（用户要求）。
        s.moonX = 2 * ox + w - s.sx;
        // 迷你岛/小岛太窄：太阳升起时俩挤在一处，观感很怪（用户明确要求区分对待）。
        // 大岛保留"日月并行"的观感，小尺寸的就不画月亮了。
        // 小尺寸（小岛/迷你岛）只在**太阳已经落山后**才画月亮：
        // 日出/黄昏时太阳也在，两个挤在一处很怪；夜里只有月亮就正常了。
        if ((J == 6 || J == 7 || J == 11 || J == 12 || J == 13) && alt > -1.0) s.moonR = -1;
        s.moonY = oy + (int)(h * 0.24);
    }

    // 星星：太阳一沉下去就开始冒（-4° 起步、-11° 全亮），亮度给足 ——
    // 原来从 -7° 起步、最亮才 210，用户反馈"星星不见了"（实测那会儿只有 47/255，基本看不见）
    double sa = (-4.0 - alt) / 7.0;
    double dark = std::min(1.0, std::max(0.0, sa));
    // 天气不好就看不见星星（云一挡就没了）
    double wxClear = (wxKind == 0) ? 1.00 : (wxKind == 1) ? 0.70 : (wxKind == 2) ? 0.25
                   : (wxKind == 3) ? 0.10 : (wxKind == 4) ? 0.20 : (wxKind == 5) ? 0.35 : 1.0;
    wxClear = 1.0 - (1.0 - wxClear) * wxk;      // 天气在散，星星就跟着一点点露出来
    s.starA = (int)(dark * 255.0 * wxClear);
    if (wxKind == 3 || wxKind == 4) s.starA = 0;      // 雾和雨看不见星星，一颗都不留
    // 星星**颗数**按岛面积定：大岛(424×80)约 26 颗、迷你岛(150×48)只有 5 颗 ——
    // 用户要求"迷你岛不要整个都铺满星星"；同时"越黑星星越多"（前 45% 先出来，暗透了才全出）。
    int base = (w * h) / 1300;
    if (base < 4) base = 4;
    if (base > 34) base = 34;
    s.nStar = (int)(base * (0.45 + 0.55 * dark) + 0.5);
    // 每次弹出重掷一批：位置、亮起延迟、闪烁周期全都随机（用户要的"凌乱美"）。
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
            s.starX[i]   = (short)(s.cx - 320 + (int)(r1 / 65535.0 * 640));       // 横向随机
            s.starY[i]   = (short)((int)(r2 / 65535.0 * (ISLAND_HEIGHT * 1.05)));  // 纵向随机
            s.starDly[i] = (short)(120 + (r3 % 900));                              // 0.12~1.02 秒之间各自亮起
            s.starPer[i] = (short)(620 + (r1 % 1080));                             // 各自闪烁周期 620~1700ms
        }
    }

    // 云：白天明显一点，太阳一落就淡掉（夜里天空不该有一团团灰）；再按天气加量
    double wxCloud = (wxKind == 0) ? 0.45 : (wxKind == 1) ? 1.00 : (wxKind == 2) ? 1.35
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
            s.dropX[i] = (short)(s.cx - 320 + ((i * 3571) % 991) / 991.0 * 640);
            int y0 = (int)(((i * 6151) % 983) / 983.0 * (ISLAND_HEIGHT * 1.05));
            s.dropY[i] = (short)((int)fmod((double)y0 + fall, ISLAND_HEIGHT * 1.05));
            s.dropL[i] = (short)((wxKind == 4) ? (5 + (i % 4)) : 2);
        }
    }
    // 有没有"会动的东西"：雨雪粒子、或者夜空里的星星（闪烁）。有就要持续重绘。
    s.rainy = (s.nDrop > 0) || (s.starA >= 6);
}

// 一行一行地取渐变底色（v: 0=天顶 1=地平线）
static inline void SkyRowColor(const SkyState& s, double v, int* R, int* G, int* B)
{
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    double fi = v * (SKY_ROWS - 1);
    int i0 = (int)fi; if (i0 >= SKY_ROWS - 1) i0 = SKY_ROWS - 2;
    double f = fi - i0;
    *R = (int)(s.rowR[i0] + (s.rowR[i0+1] - s.rowR[i0]) * f);
    *G = (int)(s.rowG[i0] + (s.rowG[i0+1] - s.rowG[i0]) * f);
    *B = (int)(s.rowB[i0] + (s.rowB[i0+1] - s.rowB[i0]) * f);
}

// 云带：横向一条条的淡云。按 x 预生成一行，逐像素只查表 —— 不然每像素两次 sin 太费
static double skyCloud[4096];
static void SkyBuildClouds(const SkyState& s)
{
    // 云也是固定带子：以岛中心为基准的 640 宽，岛变大变小不影响云的形状
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
static inline void SkyPixel(const SkyState& s, int lx, int ly, int* R, int* G, int* B)
{
    // 纵向：大岛那一档（ISLAND_FINAL_Y 起 80 像素高）就是整条渐变；岛比它小就只看其中一段
    double v = (double)(ly - ISLAND_FINAL_Y) / (double)ISLAND_HEIGHT;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    SkyRowColor(s, v, R, G, B);            // 收回段的渐变已经在 SkyBuild 里整块混好了

    // 夜幕从顶上往下落：越靠上黑得越透，地平线那一带最后才黑（黄昏就长这样）
    if (s.veil > 0.002)
    {
        double d = (s.veil - v) / 0.24;
        if (d > 1) d = 1; else if (d < 0) d = 0;
        d *= std::min(1.0, s.veil);                     // 刚入夜时只加一点黑，别一步到位
        double k = d * 0.90 * s.decoA;                  // 收回段里夜幕跟着一起退（不留一层黑）

        *R = (int)(*R * (1 - k) + 10 * k);
        *G = (int)(*G * (1 - k) + 12 * k);
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
        f = f * f * f * s.sunVis * s.decoA * s.sunA;   // 阴雨雾压暗；收回段淡出；正常段开头淡入
        *R = (int)(*R + (s.glowR - *R) * f * 0.85);
        *G = (int)(*G + (s.glowG - *G) * f * 0.85);
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
            *G = (int)(*G + (245 - *G) * a);
            *B = (int)(*B + (205 - *B) * a);
        }
    }


    // 云
    if (s.cloudAmp > 0.02)
    {
        double env = 1.0 - fabs(((double)(ly - ISLAND_FINAL_Y) / ISLAND_HEIGHT) - 0.46) * 2.3;
        if (env > 0)
        {
            int ci = lx - (s.cx - 320); if (ci < 0) ci = 0; if (ci > 639) ci = 639;
            double c = skyCloud[ci];
            int d = (int)(c * env * s.cloudAmp * 34.0);
            *R += d; *G += d; *B += (int)(d * 0.85);
        }
    }
}

// 月亮：画一个真实的月牙/满月
static void SkyDrawMoon(const SkyState& s, int lx, int ly, int* R, int* G, int* B)
{
    if (s.moonR < 3) return;
    double dx = lx + 0.5 - s.moonX, dy = ly + 0.5 - s.moonY;
    double d = sqrt(dx*dx + dy*dy);
    if (d > s.moonR + 1.2) return;
    // 月相：把 0..29 映射成"影子圆"的横向偏移
    double ph = s.moonPhase / 29.530588;         // 0=朔 0.5=望
    double lit = cos(2.0 * SKY_PI * ph);         //  1=满月 -1=新月
    // 太接近上/下弦时影子圆几乎和月圆重合，月牙会细成一根丝（用户："月牙怎么这么细"）。
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
    *G = (int)(*G + (244 - *G) * a * 0.92);
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
            put(sky.starX[i], sky.starY[i], 255, 252, 238, a);
            put(sky.starX[i] - 1, sky.starY[i], 255, 252, 238, a / 3);
            put(sky.starX[i] + 1, sky.starY[i], 255, 252, 238, a / 3);
            put(sky.starX[i], sky.starY[i] - 1, 255, 252, 238, a / 3);
            put(sky.starX[i], sky.starY[i] + 1, 255, 252, 238, a / 3);
        }
    }
    // 雨丝 / 雪花
    for (int i = 0; i < sky.nDrop && i < 48; i++)
    {
        if (sky.wx == 4)
        {
            for (int dy = 0; dy <= sky.dropL[i]; dy++)
                put(sky.dropX[i], sky.dropY[i] + dy, 200, 214, 236, 120);
        }
        else
        {
            put(sky.dropX[i], sky.dropY[i], 245, 248, 255, 190);
            put(sky.dropX[i], sky.dropY[i] + 1, 245, 248, 255, 120);
        }
    }
}
// 天空现在**铺到所有地方**（用户 2026-10-16 改的要求：天气、时间、日出日落全都用上）。
// 原来只允许 上课了/下课了/倒计时 三处，后来加了迷你岛，再后来干脆全要 —— 所以这里直接恒真。
bool SkyActive()
{
    if (!skyEn) return false;       // 总开关：关掉 → 所有岛回到原来的纯色底板
    return true;
}
// 此刻的"白天进度"：0=日出 0.5=正午 1=日落；负数=天没亮，>1=已经黑了
static double SkyTauNow()
{
    SkyDay d = SkyToday();
    SYSTEMTIME st; GetLocalTime(&st);
    double now = st.wHour * 60 + st.wMinute + st.wSecond / 60.0;
    if (!d.ok || d.set - d.rise < 30.0) return (now < 720.0) ? -0.20 : 1.20;   // 极昼极夜：给个固定档
    // 用户 2026-10-27：日落要比实际早 5 分钟 ——
    // 肉眼已经天黑了，岛上还挂着一点小太阳，看着就是没对上。
    double setEarly = d.set - 5.0;
    if (setEarly <= d.rise + 10.0) setEarly = d.set;    // 极短白天的兜底
    return (now - d.rise) / (setEarly - d.rise);
}
// 这一帧该用的进度：弹出后 2.5 秒内从"日出前一点"连续推到"现在"，之后停在现在
// 演一遍要多久 —— **跟着"要跨多远"走**，不是固定值。
// 清晨那一小段（跨 0.15）2.2 秒就够；入夜那一下要跨一整天（跨 1.25），
// 给到 6 秒多，看起来的"变化速度"才差不多。
// （用户反馈："到夜间的时候动画太快了，中午之前就差不多"）
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
    if (!skyClear) return 1.0;
    if (J == 11 || J == 12 || J == 13) return 1.0;   // 迷你岛不参与收回
    LONGLONG el = NowMs() - skyT0;
    if (el < 0) el = 0;
    if (el < SKY_CLEAR_MS)
    {
        double p = (double)el / (double)SKY_CLEAR_MS;
        return 1.0 - (p * p * (3.0 - 2.0 * p));          // 两头慢，中间散得快
    }
    double p2 = (double)(el - SKY_CLEAR_MS) / (double)SkyAnimDur();
    if (p2 >= 1.0) return 1.0;
    return p2 * p2 * (3.0 - 2.0 * p2);
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
    // 跳到白天蓝，看着像硬切（用户报的"闪一下"，这是第二处来源）。
    double e = t * t * (3.0 - 2.0 * t);
    // ⚠️ 起点必须和 SkyAnimDur() 里算的**完全一致**（0=日出）。
    // 这里原来还是 -0.10（日出前一点，很黑）：收回段刚结束、画面已经到日出，
    // 下一帧正常段却从 -0.10 开场 → 屏幕闪一下黑（用户报的"已经变成日出又突然闪一下"）。
    double tStart = std::max(0.0, tNow - 1.35);
    return tStart + (tNow - tStart) * e;
}
bool SkyAnimating() { return skyEn && (NowMs() - skyT0) < SkyAnimTotal(); }   // 关了就别再 40ms 空刷

static int gClipRight = 0;   // >0 时把文字合成裁到这个 x（长歌名滚动用）


// ===== litehtml 容器：把 HTML 画进岛那张位图（所以能跟着变形缩放）=====
static std::wstring AiU8toW(const char* t)
{
    if (!t || !*t) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, t, -1, NULL, 0);
    if (n <= 1) return L"";
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, t, -1, &w[0], n);
    return w;
}

class AiLite : public litehtml::document_container
{
public:
    HDC dc = NULL;
    int vw = AI_W, vh = AI_H;

    litehtml::uint_ptr create_font(const litehtml::font_description& d, const litehtml::document*,
                                   litehtml::font_metrics* fm) override
    {
        std::wstring f = AiU8toW(d.family.c_str());
        if (f.empty()) f = L"Microsoft YaHei";
        LOGFONTW lf = {0};
        lf.lfHeight = -(int)d.size;
        lf.lfWeight = d.weight;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, f.c_str());
        HDC h = dc ? dc : GetDC(NULL);
        HFONT fn = CreateFontIndirectW(&lf);
        HGDIOBJ old = SelectObject(h, fn);
        TEXTMETRICW tm = {0};
        GetTextMetricsW(h, &tm);
        if (fm)
        {
            fm->font_size = d.size;
            fm->height = (float)tm.tmHeight;
            fm->ascent = (float)tm.tmAscent;
            fm->descent = (float)tm.tmDescent;
            fm->x_height = (float)(tm.tmHeight / 2);
            fm->draw_spaces = true;
        }
        SelectObject(h, old);
        if (!dc) ReleaseDC(NULL, h);
        return (litehtml::uint_ptr)fn;
    }
    void delete_font(litehtml::uint_ptr f) override { if (f) DeleteObject((HFONT)f); }

    litehtml::pixel_t text_width(const char* t, litehtml::uint_ptr hFont) override
    {
        HDC h = dc ? dc : GetDC(NULL);
        HGDIOBJ old = hFont ? SelectObject(h, (HFONT)hFont) : NULL;
        std::wstring w = AiU8toW(t);
        SIZE sz = {0};
        GetTextExtentPoint32W(h, w.c_str(), (int)w.size(), &sz);
        if (old) SelectObject(h, old);
        if (!dc) ReleaseDC(NULL, h);
        return (float)sz.cx;
    }

    void draw_text(litehtml::uint_ptr hdc, const char* t, litehtml::uint_ptr hFont,
                   litehtml::web_color c, const litehtml::position& pos) override
    {
        HDC h = (HDC)hdc;
        HGDIOBJ old = hFont ? SelectObject(h, (HFONT)hFont) : NULL;
        int om = SetBkMode(h, TRANSPARENT);
        COLORREF oc = SetTextColor(h, RGB(c.red, c.green, c.blue));
        std::wstring w = AiU8toW(t);
        TextOutW(h, (int)(float)pos.x, (int)(float)pos.y, w.c_str(), (int)w.size());
        SetTextColor(h, oc);
        SetBkMode(h, om);
        if (old) SelectObject(h, old);
    }

    litehtml::pixel_t pt_to_px(float pt) const override { return (float)(pt * 96.0f / 72.0f); }
    litehtml::pixel_t get_default_font_size() const override { return 15; }
    const char* get_default_font_name() const override { return "Microsoft YaHei"; }

    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override {}
    void load_image(const char*, const char*, bool) override {}
    void get_image_size(const char*, const char*, litehtml::size& sz) override { sz.width = 0; sz.height = 0; }
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer&, const std::string&, const std::string&) override {}
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                          const std::shared_ptr<litehtml::document>&) override { return nullptr; }

    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                         const litehtml::web_color& c) override
    {
        HDC h = (HDC)hdc;
        RECT r = { (LONG)(float)layer.border_box.x, (LONG)(float)layer.border_box.y,
                   (LONG)(float)(layer.border_box.x + layer.border_box.width),
                   (LONG)(float)(layer.border_box.y + layer.border_box.height) };
        HBRUSH br = CreateSolidBrush(RGB(c.red, c.green, c.blue));
        int rad = (int)(float)layer.border_radius.top_left_x;
        if (rad > 0)
        {
            HPEN pen = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ ob = SelectObject(h, br), op = SelectObject(h, pen);
            RoundRect(h, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
            SelectObject(h, op); SelectObject(h, ob);
            DeleteObject(pen);
        }
        else FillRect(h, &r, br);
        DeleteObject(br);
    }
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                              const litehtml::background_layer::linear_gradient&) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                              const litehtml::background_layer::radial_gradient&) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&,
                             const litehtml::background_layer::conic_gradient&) override {}
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(std::string&, litehtml::text_transform) override {}
    void import_css(std::string&, const std::string&, std::string&) override {}
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override {}
    void del_clip() override {}
    void get_viewport(litehtml::position& vp) const override
    {
        vp.x = 0; vp.y = 0;
        vp.width = (litehtml::pixel_t)vw;
        vp.height = (litehtml::pixel_t)vh;
    }
    void get_media_features(litehtml::media_features& m) const override
    {
        m.type = litehtml::media_type_screen;
        m.width = (litehtml::pixel_t)vw; m.height = (litehtml::pixel_t)vh;
        m.device_width = (litehtml::pixel_t)vw; m.device_height = (litehtml::pixel_t)vh;
        m.color = 8; m.resolution = 96;
    }
    void get_language(std::string& lang, std::string& culture) const override { lang = "zh"; culture = "CN"; }
};

static AiLite gAiLite;
static litehtml::document::ptr gAiDoc;

// 初始为空 —— 打开时不该有任何气泡（之前塞了示例，看着像"没问就自动答了"）
static const char* kAiHtml =
    "<html><body style='margin:0;padding:0;color:#ffffff;font-size:15px;'>"
    "<div style='clear:both;height:1px;'></div>"
    "</body></html>";


// ===== AI 岛布局：一处算，绘制和命中共用 =====
// 以前绘制和命中各算一套，改一处漏一处，出过好几次"按钮点不中"。
// 现在只有 AiCalc() 一份真值，谁要坐标都来这儿拿。
struct AiLay
{
    int padL, editX, editW, editH;
    int rowY, rowTop, rowBot;
    int pillX, pillW;
    int testX, testD;
    int sendX, sendD;
    int closeX, closeD;
    int histX, histW, histY, histRowH;
};

static AiLay AiCalc(int ox, int oy, int w, int h, float k)
{
    AiLay L;
    int gapB = (int)(8 * k);

    L.padL  = (int)(16 * k);
    L.editX = ox + L.padL;
    L.editW = (int)(356 * k);
    L.editH = (int)(34 * k);

    L.rowY   = oy + h - (int)(52 * k);   // 输入框贴底
    L.rowTop = L.rowY;
    L.rowBot = oy + h;

    L.pillW  = (int)(76 * k);
    L.testD  = (int)(40 * k);
    L.sendD  = (int)(40 * k);
    L.closeD = (int)(28 * k);

    L.pillX  = L.editX + L.editW + gapB;
    L.testX  = L.pillX + L.pillW + gapB;
    L.sendX  = L.testX + L.testD + gapB;
    L.closeX = ox + w - (int)(40 * k);

    L.histX    = ox + (int)(18 * k);
    L.histW    = w - (int)(36 * k);
    L.histY    = oy + (int)(30 * k);
    L.histRowH = (int)(32 * k);
    return L;
}


// ===== 全局 <-> IslandState 的搬运 =====
// DrawIsland 内部有大量和全局同名的局部变量（R/G/B 颜色、bb 颜色），
// 机械替换会误伤。所以用"进出口搬运"：进入时把 IS 灌进全局，
// 退出时（RAII 析构，含所有提前 return）把改动收回 IS、再把全局还原。
// 效果等价于参数化：每个岛只在自己那次绘制期间占用全局，互不覆盖。
static void StateToGlobals(const IslandState& st)
{
    D = st.D; E = st.E; F = st.F; G = st.G;
    J = st.J; P = st.P; O = st.O; I = st.I;
    R = st.R; S = st.S;
    vv = st.vv; vv0 = st.vv0; vt = st.vt;
    ta = st.ta; ww = st.ww;
    sf = st.sf; sf0 = st.sf0; sf1 = st.sf1;
    gw = st.gw; gh = st.gh; gy = st.gy; gJ = st.gJ;
    ae = st.ae; an = st.an; ao = st.ao;
    bigH = st.bigH;
    bb = st.bb; pp = st.pp; hh = st.hh; nt = st.nt;
    oo = st.oo;
    ply = st.ply; plyMix = st.plyMix; plyA = st.plyA;
    aiMode = st.aiMode; aiBgMix = st.aiBgMix; aiH = st.aiH; aiScroll = st.aiScroll;
    wcscpy_s(gg, st.gg);
    skyAlarm = st.skyAlarm; skyClear = st.skyClear; skyReplay = st.skyReplay;
    skyBr = st.skyBr; skyT0 = st.skyT0;
    na = st.na; nv = st.nv;
}
static void GlobalsToState(IslandState& st)
{
    st.D = D; st.E = E; st.F = F; st.G = G;
    st.J = J; st.P = P; st.O = O; st.I = I;
    st.R = R; st.S = S;
    st.vv = vv; st.vv0 = vv0; st.vt = vt;
    st.ta = ta; st.ww = ww;
    st.sf = sf; st.sf0 = sf0; st.sf1 = sf1;
    st.gw = gw; st.gh = gh; st.gy = gy; st.gJ = gJ;
    st.ae = ae; st.an = an; st.ao = ao;
    st.bigH = bigH;
    st.bb = bb; st.pp = pp; st.hh = hh; st.nt = nt;
    st.oo = oo;
    st.ply = ply; st.plyMix = plyMix; st.plyA = plyA;
    st.aiMode = aiMode; st.aiBgMix = aiBgMix; st.aiH = aiH; st.aiScroll = aiScroll;
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


// ============================================================
// 消息岛：AI 岛开着时从它下面冒出来的"小大岛"
// 2026-10-25 改多槽：以前只有 1 个（gB），连发第二条会把第一条挤掉；
// 现在最多 MAX_BUB 个，一个接一个往下排，各走各的动画和 5 秒计时。
// 关键决定（沿用）：不另开窗口。试过独立 layered window + UpdateLayeredWindow，
// 一更新主岛的层就失效（两个 UpdateLayeredWindow 打架），AI 岛直接消失。
// 所以改成：画布加高，所有岛画在同一张画布上，同一次 UpdateLayeredWindow 提交。
// ============================================================
static const int MAX_BUB   = 6;       // 最多堆 6 个（用户说五六个没问题）
static const int BUB_STEPS = 18;      // 18 帧 × 16ms ≈ 0.3s
static const int BUB_HOLD  = 5000;    // 展开后显示 5 秒（和主岛大岛的 DISPLAY_DURATION_MS 一致）
static const int BUB_H     = 80;      // 正常大岛高度
static const int BUB_W     = 424;
static const int BUB_GAP   = 8;       // 岛与岛之间的缝

struct BubSlot
{
    IslandState st;         // 每个岛一份完整状态，DrawIsland 直接吃（以前是全局 gB）
    std::wstring text;
    int F, G, E;            // 宽 / 当前高 / 当前屏幕 y
    int startE, targetE;    // 钻出起点 / 该停的位置
    int step;               // 生长进度 0..BUB_STEPS
    int outStep;            // 收缩进度，0=没在收
    bool showing;
    LONGLONG holdUntil;
};

static BubSlot gBubs[MAX_BUB];
static int gBubN = 0;       // 有效槽数（永远是数组前面连续 N 个）

static void BubInit(BubSlot& b)
{
    b.F = BUB_W; b.G = 0; b.E = 0;
    b.startE = 0; b.targetE = 0;
    b.step = 0; b.outStep = 0;
    b.showing = false;
    b.holdUntil = 0;
    b.text.clear();
}

// 还有岛挂着（生长中 / 停着 / 正在收）
static bool BubAny()
{
    for (int i = 0; i < gBubN; i++) if (gBubs[i].showing) return true;
    return false;
}

// 第 i 个槽"应该"待的顶部 y：AI 岛底 + 8，后面每个再往下 BUB_H + BUB_GAP
// 注意用固定的 BUB_H 算，不用当前 G —— 否则上面那个岛一边收缩，下面的跟着抖
static int BubTargetTop(int i)
{
    // 有原岛挂在下面时，新消息要排在原岛下面，不能压上去
    int top = belowOn ? (belowY + gBelow.G + BUB_GAP) : (gA.E + gA.G + BUB_GAP);
    for (int k = 0; k < i; k++)
        if (gBubs[k].showing) top += BUB_H + BUB_GAP;
    return top;
}

// 硬清：主岛要变别的东西了，这些消息岛没必要留着
static void BubClearAll()
{
    for (int i = 0; i < gBubN; i++) BubInit(gBubs[i]);
    gBubN = 0;
}

// 把已经收完的槽挤掉，保住先后顺序
static void BubCompact()
{
    int w = 0;
    for (int i = 0; i < gBubN; i++)
    {
        if (!gBubs[i].showing) continue;
        if (w != i) gBubs[w] = gBubs[i];
        ++w;
    }
    for (int i = w; i < gBubN; i++) BubInit(gBubs[i]);
    gBubN = w;
}

// 打开 AI 岛之前调用：把此刻屏幕上的岛原样存一份，等会儿挂到 AI 岛下面
static void StartBelowIsland(const IslandState& src)
{
    gBelow = src;
    belowOn = true;
    belowHomeY = src.E;      // 它本来待的 y（AI 岛收起后要滑回这儿）
    belowY = src.E;
    belowTargetY = src.E;
    belowRestore = false;
}

// 把原岛原样画在 AI 岛下面：只改 y，样式 / 尺寸 / 状态全不动。
// 和消息岛一样，画之前要备份头顶以上的像素，否则 DrawIsland 的底板渐变会擦掉 AI 岛。
static void DrawBelowOnCanvas(BYTE* p, int stride, int W, int H)
{
    if (!belowOn || gBelow.G < 4 || gBelow.F < 4) return;
    if (belowY + gBelow.G > H) return;

    IslandState tmp = gBelow;
    tmp.E = belowY;

    // 同上：只画自己矩形，不必备份
    DrawIsland(p, stride, W, H, tmp, false);
}

// 正在淡出的 AI 岛：往上飘一点，透明度交给后面统一乘
static void DrawAiFadeOnCanvas(BYTE* p, int stride, int W, int H)
{
    if (!aiFading || aiFade <= 0.02f) return;
    IslandState tmp = gAiOut;
    // 用户 2026-10-27：回收要"冲出屏幕"，不是原地淡出。
    // 用 k*k 做加速 —— 起步慢一点，然后整块被"吸"到屏幕上方外面去。
    float kk2 = 1.0f - aiFade;
    kk2 = kk2 * kk2;
    tmp.E = gAiOut.E - (int)(kk2 * (gAiOut.G + gAiOut.E + 80));
    if (tmp.F < 4 || tmp.G < 4) return;
    if (tmp.E + tmp.G <= 0 || tmp.E >= H) return;
    DrawIsland(p, stride, W, H, tmp, false);
}

// 直接画进主画布：交给 DrawIsland，和主岛大岛完全一样的样式（天空/大字）
static void DrawBubbleOnCanvas(BYTE* p, int stride, int W, int H)
{
    for (int i = 0; i < gBubN; i++)
    {
        BubSlot& b = gBubs[i];
        if (!b.showing || b.G < 4 || b.F < 4) continue;
        if (b.E + b.G > H) continue;

        // 画布是共用的，而 DrawIsland 的底板渐变会扫整块画布 —— 不备份的话，
        // 画第二个岛就把第一个岛（以及上面的 AI 岛）擦成透明。
        // 所以每画一个岛，先把它"头顶以上"那块存下来，画完原样贴回去。
        // 底板循环现在只扫岛自己的矩形，画不到别处 → 不用再备份/贴回整块上方像素了
        DrawIsland(p, stride, W, H, b.st, false);
    }
}

// 每 16ms 推进所有岛
static void BubbleTick()
{
    // AI 岛往上飘 + 淡出
    if (aiFading)
    {
        aiFade -= 0.075f;                      // 约 13 帧 ≈ 0.21 秒（冲出屏幕要一点时间）                      // 约 13 帧 ≈ 0.21 秒（冲出屏幕要一点时间）
        if (aiFade <= 0.0f) { aiFade = 0.0f; aiFading = false; gAiDoc = nullptr; }
        UpdateIslandDisplay(o, 1.0f, 255);
    }

    // 原岛自己"活着"：迷你岛静止久了要变淡。
    // 注意只推进 nv/na 这两个纯展示状态，**绝不调 NotchHoldTick** ——
    // 那个里面有 SetWindowPos 自愈和 HideMiniIsland，会直接动到主岛。
    if (belowOn && gBelow.J == 12)
    {
        int dimA = 255 * notchA / 100;
        int target = (NowMs() - gBelow.na > NOTCH_DIM_MS) ? dimA : 255;
        if (gBelow.nv != target)
        {
            int d = target - gBelow.nv, step = 10;
            if (d > step) gBelow.nv += step;
            else if (d < -step) gBelow.nv -= step;
            else gBelow.nv = target;
        }
    }

    // 原岛从 AI 岛下面滑回原位：只是绘制偏移，不去碰 gA.E（碰了会被状态机当帧改回去）
    if (belowRestore)
    {
        restoreOff += (0 - restoreOff) / 4;
        if (restoreOff > -2 && restoreOff < 2)
        {
            restoreOff = 0;
            belowRestore = false;
            // 滑动期间状态机可能把 J 停在 1（弹出过渡态），不拨回 3 的话之后就画不出东西
            if (J == 1 && hh) { J = 3; }
        }
        UpdateIslandDisplay(o, 1.0f, 255);
    }

    // 原岛：跟着 AI 岛底部走（每帧 1/3，先快后慢）
    if (belowOn)
    {
        belowTargetY = gA.E + gA.G + BUB_GAP;
        int d = belowTargetY - belowY;
        if (d != 0)
        {
            int st = d / 3;
            if (st == 0) st = (d > 0 ? 1 : -1);
            belowY += st;
        }
        int need = belowY + gBelow.G + 12;
        if (l < need) l = need;
    }

    if (gBubN == 0) return;

    LONGLONG now = NowMs();
    for (int i = 0; i < gBubN; i++)
    {
        BubSlot& b = gBubs[i];
        if (!b.showing) continue;

        int target = BubTargetTop(i);

        if (b.step < BUB_STEPS)
        {
            // 生长：弹簧曲线，和主岛大岛同一条（先冲过头再回落，手感一致）
            ++b.step;
            float t = (float)b.step / BUB_STEPS;
            b.G = (int)(BUB_H * springH(t));
            b.targetE = target;
            b.E = (int)(b.startE + (b.targetE - b.startE) * springY(t));
            if (b.step == BUB_STEPS)
            {
                b.G = BUB_H;
                b.E = b.targetE;
                b.holdUntil = now + BUB_HOLD;
            }
        }
        else if (b.outStep > 0)
        {
            // 收缩：BUB_STEPS 帧收到 0
            ++b.outStep;
            float t = 1.0f - (float)(b.outStep - 1) / (float)BUB_STEPS;
            if (t < 0) t = 0;
            b.G = (int)(BUB_H * t);
            if (t <= 0.01f) { b.showing = false; b.G = 0; }
        }
        else if (now >= b.holdUntil)
        {
            b.outStep = 1;
        }
        else
        {
            // 停住之后还得让位：AI 岛变高（输入态→对话态）、或者前面那个岛收了，
            // 位置都要平滑挪过去（每帧走 1/3，先快后慢）
            b.targetE = target;
            int d = b.targetE - b.E;
            if (d != 0)
            {
                int st = d / 3;
                if (st == 0) st = (d > 0 ? 1 : -1);
                b.E += st;
            }
        }

        b.st.G = b.G;
        b.st.E = b.E;
    }

    BubCompact();

    // 画布必须装得下最下面那个岛
    int need = gA.E + gA.G + BUB_GAP;
    for (int i = 0; i < gBubN; i++)
    {
        int bottom = gBubs[i].E + (gBubs[i].showing ? gBubs[i].G : 0) + 12;
        if (bottom > need) need = bottom;
    }
    if (l < need) l = need;

    UpdateIslandDisplay(o, 1.0f, 255);
}

// 在 AI 岛下面追加一个消息岛
static void ShowBubbleIsland(const wchar_t* text)
{
    // 槽满了：最老的直接赶走（不走收缩，否则新岛得排队等它收完）
    if (gBubN >= MAX_BUB)
    {
        for (int i = 0; i < gBubN - 1; i++) gBubs[i] = gBubs[i + 1];
        --gBubN;
        BubInit(gBubs[gBubN]);
    }

    int idx = gBubN++;
    BubSlot& b = gBubs[idx];
    BubInit(b);

    b.text = text ? text : L"";
    b.F = BUB_W;

    // 落点：AI 岛下面 8px（后面每个再往下 88px）
    int top = BubTargetTop(idx);
    // 第一个从 AI 岛内部靠下 1/3 钻出来；后面几个从上一个岛的底部往上一点钻出来
    b.startE = (idx == 0) ? (gA.E + gA.G - gA.G / 3) : (top - BUB_H / 3);
    b.targetE = top;
    b.E = b.startE;
    b.G = 0;
    b.step = 0;
    b.showing = true;
    b.holdUntil = 0;

    // 样式：整体继承主岛，再改成"定格大岛"的样子
    b.st = gA;

    // 宽度按文字自适应（和主岛大岛一样：量字宽 + 留白，限 424~560）
    {
        HDC hdc = GetDC(NULL);
        HFONT fnt = GetCachedFont((int)(BUB_H * 0.62), true);   // 跟 DrawIsland 的 fsLarge 一致
        HGDIOBJ of = SelectObject(hdc, fnt);
        SIZE sz = { 0 };
        GetTextExtentPoint32W(hdc, text ? text : L"", (int)(text ? wcslen(text) : 0), &sz);
        SelectObject(hdc, of);
        ReleaseDC(NULL, hdc);
        int wNeed = sz.cx + 120;
        if (wNeed < 424) wNeed = 424;
        if (wNeed > 560) wNeed = 560;
        b.F = wNeed;
    }

    b.st.D = (gA.D + gA.F / 2) - b.F / 2;    // 居中于 AI 岛
    b.st.F = b.F;
    b.st.G = 0;                       // 从 0 长起来
    b.st.E = b.startE;
    b.st.J = 3;                       // 定格大岛：DrawIsland 走"大字 + 底板"
    b.st.hh = true;
    b.st.bb = false;
    b.st.nt = false;
    b.st.sf = 0.0f; b.st.sf0 = 0.0f; b.st.sf1 = 0.0f;
    b.st.vv = 0.0f; b.st.vv0 = 0.0f; b.st.vt = 0.0f;
    b.st.ta = 255; b.st.ww = 0;
    b.st.ply = false; b.st.plyMix = false; b.st.plyA = 255.0f;
    b.st.aiMode = false;
    b.st.aiBgMix = 0.0f;         // 关键：gA 是 AI 岛（aiBgMix=1 深色），SkyOverlay 要求 < 0.5 才画天空
    b.st.bigH = BUB_H;
    // 消息岛也跑天空（和主岛大岛一个观感）
    b.st.skyAlarm = true;
    b.st.skyClear = false;
    b.st.skyReplay = true;       // 从"日出"演到"现在"
    b.st.skyBr = skyBr;
    b.st.skyT0 = NowMs();
    b.st.oo = 0;
    wcscpy_s(b.st.gg, text ? text : L"");

    // 立刻把画布加高
    int need = top + BUB_H + 12;
    if (l < need) { l = need; }
    UpdateIslandDisplay(o, 1.0f, 255);
}

void DrawIsland(BYTE* p, int stride, int width, int height, IslandState& IS, bool clearCanvas)
{
    ScopedIslandState scoped(IS);   // 载入本岛状态；函数结束（含提前 return）自动收回+还原
        // width/height = 画布尺寸；岛形只是画布里会动的一块
    // clearCanvas=false 时不清空 —— 同一个画布要画第二个岛时用（例如 AI 岛 + 消息岛）
    if (clearCanvas)
        for (int y=0; y<height; y++)
            memset(p + (size_t)y*stride*4, 0, (size_t)width*4);

    int ox = D - n;   // 岛在画布内的位置（画布 = 窗口，固定在 n,0）
    int oy = E;
    int w = F, h = G;
    if (w < 2 || h < 2) return;
    int rw=std::min(28,w/4), rh=std::min(28,h/2);
    int rx=rw/2, ry=rh/2;
    // 迷你岛一律 16×16 圆角。
    // 之前用尺寸判断（w<=162 && h<=60）—— 但"变身回来"那条路上尺寸是逐帧插值的，
    // 中间可能落在边界外；而且退出变身那一帧的 w/h 未必正好是 150/48。
    // 改成用**状态**判断：只要现在是迷你岛（11/12/13）或者正变向迷你岛（J=5 且 gJ=12），就强制圆角。
    bool notchState = (J == 11 || J == 12 || J == 13) || (J == 5 && gJ == 12);
    if (notchState) { rx = 16; ry = 16; }

    // 1. 底板：平时是纯色（节日换节日色）；**大岛弹出时改成"天空"** —— 颜色由太阳高度角决定，
    //    弹出头 3 秒把"日出 → 现在"连续演一遍（2026-10-16 用户要求）。
    // AI 岛定格里背景必须是深色，兜一层（动画那段万一没跑满）
    if (aiMode && J == 3) aiBgMix = AI_SKY_MASK;
    if (!aiMode && (J == 11 || J == 12 || J == 13)) aiBgMix = 0.0f;

    bool skyOn = SkyActive();
    if (skyOn)
    {
        SkyDay day = SkyToday();
        // 迷你岛只是"要一块背景"，**不演动画**（用户只允许 上课了/下课了/倒计时 三处演动画）：
        // 直接取"现在"那一帧，不参与"日出 → 现在"的扫过。
        double tau = (J == 11 || J == 12 || J == 13) ? SkyTauNow() : SkyTauAnim();
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
            float c = ShapeCoverage(x - ox, ly, w, h, rx, ry);
            if (c <= 0.0f) continue;
            int pa = (int)(c * 255.0f + 0.5f);
            int idx = x*4;
            int R, G, B;
            // 迷你岛静止后"背景变淡"的标记（只作用在背景上，时间那行字另画、不受影响）
            bool notchFade = (sky.on && nt && nv < 255);
            if (sky.on)
            {
                int lx = x - ox;
                SkyPixel(sky, x, y, &R, &G, &B);        // 传画布绝对坐标 → 背景不跟岛缩放
                // 整体压暗 + 下半部分再暗一档：用户要的是"有背景感、别抢视野、以黑为底"，
                // 同时保证金字和阴影始终读得清。
                double v = (double)ly / (double)(h > 1 ? h - 1 : 1);
                // 下半部分压一档（给时间那行字让路）；顶部也压一点点 ——
                // 顶上那行小字（"上课了"）颜色本来就淡，天一亮就糊在背景里了（看过截图才发现）。
                double dim = (0.25 + 0.70 * skyBr / 100.0)      // 用户可调的"天空亮度"
                                  * (1.0 - 0.34 * std::max(0.0, (v - 0.30) / 0.70))
                                  * (1.0 - 0.26 * std::max(0.0, (0.30 - v) / 0.30));
                // 迷你岛静止后：背景的**颜色和不透明度**一起乘 nv（预乘 alpha 必须同乘），
                // 这才是"透出去"；只乘颜色就是"变暗"（踩过）。
                if (notchFade) dim *= (double)nv / 255.0;
                R = (int)(R * dim); G = (int)(G * dim); B = (int)(B * dim);
                // 月亮和星星放在压暗之后 —— 它们本来就该是亮的
                SkyDrawMoon(sky, x, y, &R, &G, &B);
                if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255;
            }
            else { R = br; G = bg; B = bb; }
            // AI 岛：往深色 #26262e 混。aiBgMix 由 AnimTick 驱动，所以变形过程中是渐变的
            if (aiBgMix > 0.001f)
            {
                double m = (double)aiBgMix;
                R = (int)(R * (1.0 - m) + 38 * m);
                G = (int)(G * (1.0 - m) + 38 * m);
                B = (int)(B * (1.0 - m) + 46 * m);
            }
            if (sky.on && notchFade) pa = pa * nv / 255;   // ★ 背景这一层真的变透（字另画，不受影响）
            row[idx]  =(BYTE)((B * pa) / 255);
            row[idx+1]=(BYTE)((G * pa) / 255);
            row[idx+2]=(BYTE)((R * pa) / 255);
            row[idx+3]=(BYTE)pa;
        }
    }

    // 小东西（星星 / 雨雪 / 月亮）画完底色再按点补上去
    // AI 岛时把星星/月亮/雨雪也关掉（它们是在底板之后单独叠的，不关就跟深色底打架）
    // AI 岛现在也吃天空（原来 aiBgMix=1 把这里挡掉了）。星星月亮画在蒙版之后，
    // 所以在 AI 岛的深色底上它们是亮的，不会被压暗。
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
    if (J == 6 || J == 7 || J == 8)            refH = MINI_HEIGHT;
    else if (J == 11 || J == 12 || J == 13)    refH = NotchFontRef();   // 迷你岛：用"等效高度"喂字号
    else if (J == 5 || J == 9)
    {
        // 变身：按**目标那一档**插值字号（目标是迷你岛就插到等效高度，别插到小岛的 56 再跳一下）。
        // ⚠️ 原来这里靠 `sf1 > 0.5f` 判断目标是不是迷你岛 —— 2026-10-16 迷你岛改成悬浮之后
        //     `sf` 恒为 0，这个判据永远不成立 → 字号先按小岛缩到 56，到迷你岛再硬跳回等效高度
        //     （用户报的"缩过头再毫无征兆地弹回来"）。改成看变身目标 `gJ`，跟形状因子彻底解耦。
        int toH = ISLAND_HEIGHT;
        if (gJ == 12)      toH = NotchFontRef();
        else if (gJ == 7)  toH = MINI_HEIGHT;
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
    // ===== AI 岛：和播放器岛同一个窗口、同一套 DrawIsland，只是内容不一样 =====
    // 它和"小岛变大岛"共用 ae/an/ao → gw/gh/gy → gJ 这套变身，所以物理上就是同一个对象在变形。
    if (aiMode && bigH == aiH && (J == 1 || J == 3 || J == 4 || J == 9))
    {
        float k = (float)G / (float)bigH;
        if (k > 1.12f) k = 1.12f;
        if (k < 0.05f) k = 0.05f;


        // 但 AI 岛只有 120 高，装不下气泡，会溢出盖住 ↑ 和 ×。
        // 下一轮把 AI 岛改成"气泡区 + 输入框"两段式（岛要变高）后再启用。
        // === litehtml 气泡层：直接画进岛的位图（U 绑定在 X 上，零拷贝）===
        // 这就是"字跟着变形走"的关键 —— 字是像素，不是另一个窗口。
        {
            gAiLite.dc = U;
            gAiLite.vw = (int)(w);
            int bubbleH = h - (int)(56 * k);
            if (bubbleH < 8) bubbleH = 8;
            gAiLite.vh = bubbleH;
            if (!gAiDoc)
                gAiDoc = litehtml::document::createFromString(kAiHtml, &gAiLite, litehtml::master_css);
            if (gAiDoc)
            {
                gAiDoc->render((litehtml::pixel_t)w);
                int fullH = (int)(float)gAiDoc->height();
                int maxScroll = fullH - bubbleH;
                if (maxScroll < 0) maxScroll = 0;
                if (aiScroll > maxScroll) aiScroll = maxScroll;   // 新消息贴底
                if (aiScroll < 0) aiScroll = 0;
                litehtml::position clip((litehtml::pixel_t)ox, (litehtml::pixel_t)oy,
                                        (litehtml::pixel_t)w, (litehtml::pixel_t)bubbleH);
                gAiDoc->draw((litehtml::uint_ptr)U, (litehtml::pixel_t)ox,
                             (litehtml::pixel_t)(oy - aiScroll), &clip);
            }
            gAiLite.dc = NULL;
        }


        const AiLay L = AiCalc(ox, oy, w, h, k);

        // 两段式：输入框贴底，上方是气泡区
        // ===== 历史区：只在"输入态 + 还没有任何问答"时出现 =====
        if (aiQs.empty() && aiH == AI_H_EMPTY && !aiHist.empty())
        {
            int hx = L.histX;
            int hw = L.histW;
            int rowH = L.histRowH;
            int y0 = L.histY;

            RECT trTitle = { hx, oy + (int)(10 * k), hx + hw, oy + (int)(26 * k) };
            stamp(trTitle, L"历史对话", GetCachedFont(std::max(10, (int)(12 * k)), false),
                  RGB(128, 128, 144), DT_LEFT | DT_VCENTER | DT_SINGLELINE, 255);

            for (int ii = 0; ii < (int)aiHist.size() && ii < 3; ++ii)
            {
                int ry = y0 + ii * rowH;
                RECT rr = { hx, ry, hx + hw, ry + rowH - (int)(5 * k) };
                HBRUSH bb = CreateSolidBrush(RGB(46, 46, 56));
                HPEN pp = CreatePen(PS_NULL, 0, 0);
                HGDIOBJ ob2 = SelectObject(U, bb), op2 = SelectObject(U, pp);
                RoundRect(U, rr.left, rr.top, rr.right, rr.bottom,
                          (int)(10 * k), (int)(10 * k));
                SelectObject(U, op2); SelectObject(U, ob2);
                DeleteObject(bb); DeleteObject(pp);

                std::wstring qh, ah;
                AiHistSplit(aiHist[ii], qh, ah);
                std::wstring t = qh;
                if (t.size() > 24) t = t.substr(0, 24) + L"…";
                RECT tr = { rr.left + (int)(12 * k), rr.top, rr.right - (int)(8 * k), rr.bottom };
                stamp(tr, t.c_str(), GetCachedFont(std::max(10, (int)(14 * k)), false),
                      RGB(226, 226, 236), DT_LEFT | DT_VCENTER | DT_SINGLELINE, 255);
            }
        }


        int rowY = L.rowY;
        int x0 = L.editX;
        int editW = L.editW, editH = L.editH;
        int pillW = L.pillW, testD = L.testD, sendD = L.sendD, closeD = L.closeD;
        int gapB = (int)(8 * k);

        // （第 10 项）这里原来有个"前 35% 画 HH:MM 居中淡出，然后 return 掉"的过渡，
        // 问题有两个：① 字是原地淡出，没有往输入框走；② 它 return 之后输入框还没画，
        // 所以肉眼看到的是"字没了 → 输入框出现"两件事。已经改成在分支末尾统一做渐变，
        // 这里不再提前 return，让输入框和滑动的旧字能同时存在。

        COLORREF panel = z ? RGB(40, 40, 48) : RGB(255, 255, 255);
        COLORREF line  = z ? RGB(60, 60, 70) : RGB(214, 214, 224);
        COLORREF txt   = z ? RGB(236, 236, 242) : RGB(36, 36, 46);
        COLORREF sub   = z ? RGB(140, 140, 152) : RGB(132, 132, 142);

        // ① 输入框
        FillRoundBoxPx(p, stride, width, height,
                       x0, rowY, x0 + editW, rowY + editH, 12.0 * k, -1.0, line, 255);
        FillRoundBoxPx(p, stride, width, height,
                       x0 + 1, rowY + 1, x0 + editW - 1, rowY + editH - 1,
                       11.0 * k, -1.0, panel, 255);

        // 占位文字 / 已输入内容
        HFONT fIn = GetCachedFont(std::max(9, (int)(15 * k)), false);
        RECT trIn = { x0 + (int)(12 * k), rowY, x0 + editW - (int)(12 * k), rowY + editH };
        const wchar_t* shown = aiInput.empty() ? L"输入你的问题…" : aiInput.c_str();
        stamp(trIn, shown, fIn, aiInput.empty() ? sub : txt,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX, 255);

        // ② 「思考」胶囊
        int bx = L.pillX;
        int by = rowY;
        FillRoundBoxPx(p, stride, width, height,
                       bx, by, bx + pillW, by + editH, editH * 0.5, -1.0, line, 255);
        FillRoundBoxPx(p, stride, width, height,
                       bx + 1, by + 1, bx + pillW - 1, by + editH - 1,
                       editH * 0.5 - 1, -1.0, panel, 255);
        const wchar_t* thinkT = aiThink2 == 0 ? L"思考 off" : (aiThink2 == 1 ? L"思考 high" : L"思考 max");
        RECT trT = { bx, by, bx + pillW, by + editH };
        stamp(trT, thinkT, fIn, txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 255);

        // ③ 圆形 ↑（空=白底深箭头 / 有字=蓝底白箭头）
        // 「测」：本地测试，点一次加一问一答
        int tx = bx + pillW + gapB + testD / 2;
        int sx = bx + pillW + gapB + testD + gapB + sendD / 2;
        int sy = rowY + editH / 2;

        FillDisc(p, stride, width, height, tx, sy, testD / 2, RGB(58, 58, 68), 255);
        {
            RECT trTest = { tx - testD / 2, sy - editH / 2, tx + testD / 2 + 1, sy + editH / 2 + 1 };
            stamp(trTest, L"测", GetCachedFont(std::max(12, (int)(17 * k)), false),
                  RGB(226, 226, 238), DT_CENTER | DT_VCENTER | DT_SINGLELINE, 255);
        }
        bool hasTxt = !aiInput.empty();
        COLORREF sc = hasTxt ? RGB(52, 120, 246) : (z ? RGB(240, 240, 246) : RGB(250, 250, 252));
        FillDisc(p, stride, width, height, sx, sy, sendD / 2, sc, 255);
        COLORREF ac = hasTxt ? RGB(255, 255, 255) : RGB(42, 42, 52);
        // 直接用字符 ↑（之前用短方块拼，拼成了漏斗状，放弃）
        RECT trArrow = { sx - sendD / 2, sy - editH / 2, sx + sendD / 2 + 1, sy + editH / 2 + 1 };
        stamp(trArrow, L"↑", GetCachedFont(std::max(14, (int)(26 * k)), true), ac,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE, 255);

        // ③.5 光标：贴着已输入文字后面闪
        if (aiCaret)
        {
            HFONT fC = GetCachedFont(std::max(9, (int)(15 * k)), false);
            SIZE sw = {0};
            if (!aiInput.empty())
            {
                HDC hh = U;
                HGDIOBJ oldF = SelectObject(hh, fC);
                GetTextExtentPoint32W(hh, aiInput.c_str(), (int)aiInput.size(), &sw);
                SelectObject(hh, oldF);
            }
            int cx = x0 + (int)(12 * k) + sw.cx + (int)(1 * k);
            int cy0 = rowY + (int)(9 * k);
            int cy1 = rowY + editH - (int)(9 * k);
            int cw = (int)(1.6f * k); if (cw < 1) cw = 1;
            FillBoxPx(p, stride, width, height, cx, cy0, cx + cw, cy1,
                      hasTxt ? RGB(230, 230, 240) : RGB(150, 150, 164), 255);
        }

        // ④ 右上角 ×
        RECT trX = { ox + w - closeD - (int)(12 * k), rowY,
                     ox + w - (int)(12 * k), rowY + editH };
        stamp(trX, L"×", GetCachedFont(std::max(10, (int)(20 * k)), false), sub,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE, 255);

        // ===== 第 10 项：旧的大岛文字 → 输入框，原地交叉渐变 =====
        // 用户的话："时间字原地变成输入框"。
        // 以前 aiMode 一开就直接不画旧字了，视觉上那行字是"啪"一下没的，
        // 然后输入框在下面早就摆好了 —— 看着像两件不相干的东西。
        // 现在变身这 0.42 秒里，把旧字按位置、字号、颜色一起插值：
        //   大岛中心 → 输入框中心（跟着往下滑）
        //   49px 金字 → 15px 白字（缩进去）
        //   alpha 255 → 0（最后彻底交给输入框）
        // 只在"原来是大岛消息"（hh）时做，从迷你岛点开不会莫名闪一行旧字。
        if (J == 1 || J == 9)
        {
            float tm = (float)(NowMs() - aiMorphT0) / 420.0f;
            if (tm < 0.999f)
            {
                if (tm < 0.0f) tm = 0.0f;

                // 旧字来源：原来是大岛消息就用那行字；从迷你岛点开的就用当前时间
                const wchar_t* oldStr = NULL;
                wchar_t tb[16] = { 0 };
                int fs0 = (int)(an * 0.62f);
                if (hh && gg[0]) oldStr = gg;
                else
                {
                    SYSTEMTIME st; GetLocalTime(&st);
                    swprintf(tb, L"%02d:%02d", st.wHour, st.wMinute);
                    oldStr = tb;
                    fs0 = 28;                                  // 迷你岛时间那档字号
                }

                // 位置：岛中心 → 输入框中心（跟着岛一起往下走，不会飘出去）
                // 输入框是贴底的：rowY = oy + h - 52k，高 34k → 中心在 h - 35k
                int eMid = h - (int)(35 * k);
                int y0   = (int)(h * 0.5f);
                int tyy  = oy + y0 + (int)((eMid - y0) * tm);
                int fs1  = std::max(9, (int)(15 * k));         // 输入框字号
                int fs2  = fs0 + (int)((fs1 - fs0) * tm);
                int a2   = (int)(255.0f * (1.0f - tm));
                // 颜色也一起走：金黄 → 白（AI 岛上是白字）
                int cg2  = 214 + (int)((255 - 214) * tm);
                int cb2  = 102 + (int)((255 - 102) * tm);
                if (fs2 > 4 && a2 > 2)
                {
                    RECT rr = { ox, tyy - fs2, ox + w, tyy + fs2 };
                    stamp(rr, oldStr, GetCachedFont(fs2, true),
                          RGB(255, cg2, cb2), DT_CENTER | DT_VCENTER | DT_SINGLELINE, a2);
                }
            }
        }
        return;
    }

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
        // 播放键左边留给"两个小球"的宽度（约 2.7 个半径：两球 + 一点留白）
        // 球要占住中间那块空：半径到岛高的 30%（直径约 0.6 岛高），
        // 歌名区再收掉对应宽度，文字不会被压住。
        int ballR0 = (int)(h * 0.19);   // 0.30 太大了，会盖住播放键；0.19 直径约岛高 38%
        if (ballR0 < 6) ballR0 = 6;
        int ballW  = (int)(ballR0 * 3.2f) + (int)(12 * k);
        int ballGap2 = (int)(gap * 2.2f);   // 球和播放键之间留更宽的间隔

        int bx = ox + w - padR - btnD;                  // 按钮左边界（画布坐标）
        int by = oy + (h - btnD) / 2;
        plyBtn.left   = n + bx;                         // 命中用屏幕坐标：屏幕 x = 画布 x + n
        plyBtn.top    = by;                             // （y 不用加，窗口顶边就在屏幕顶边）
        plyBtn.right  = n + bx + btnD;
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

        // ④ 播放键左边两个"贴在一起的小球"，跟着音乐呼吸（2026-10-18 按用户要求从底部挪过来）
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
            // 圆心间距 = 1.55r：有一段是重叠贴住的（这是用户要的"贴合"）
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
                // （用户要的"变大时也更亮一点"，但别亮到发光）
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
        // J==3 表示变身已经结束，播放器该独占了。
        // 只看 plyA>=255 不够稳：plyA 是插值出来的，某些帧会卡在 254.x，
        // 结果大岛文字接着画上去把播放器盖住（自检时抓到的，画面里还有个球飘在岛外）。
        if (plyA >= 250.0f || J == 3)
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
        // 迷你岛静止后是**整块一起淡**：背景靠逐像素 alpha 淡，这行字也得跟着淡
        // （不然就是"背景透明的、字却死亮"，不合套）。
        int tA = ta;
        if (nt) tA = ta * nv / 255;
        // 唯一的例外：背后那片**太亮**（压着浅色网页）时，淡了的字会糊进去看不清 ——
        // 这时才把字单独拎出来：换成深色 + 满不透明。只动这一个字，背景不受影响。
        COLORREF fgC = textColor, shC = shadowColor;
        if (nt && notchLum > 148)
        {
            fgC = RGB(22, 24, 32);
            shC = RGB(236, 240, 248);
            tA = ta;
        }
        stamp(ts, timeStr, hFontLarge, shC, DT_CENTER|DT_VCENTER|DT_SINGLELINE, tA);
        stamp(tr, timeStr, hFontLarge, fgC, DT_CENTER|DT_VCENTER|DT_SINGLELINE, tA);

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
        else miniStr[0] = 0;   // 没有课间倒计时就不画字（"仅时间小岛"这个老样式已废弃）
        if (miniStr[0])
        {
        RECT mr=shapeRect, ms=shapeRect;
        ms.left+=2; ms.top+=2; ms.right+=2; ms.bottom+=2;
        stamp(ms,miniStr,hFontLarge,shadowColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aMini);
        stamp(mr,miniStr,hFontLarge,textColor,DT_CENTER|DT_VCENTER|DT_SINGLELINE,aMini);
        }
    }

    // 3c) 岛下方那行提示（播放器/淡入途中说"关闭播放器"，其余说"关闭常驻岛"）
    hint((ply && bigH == PLAYER_H) ? L"长按关闭播放器" : L"长按关闭常驻岛");
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
    FILE* f = fopen("D:/aab/perf.log", "a");
    if (f)
    {
        fprintf(f, "J=%d ai=%d bub=%d n=%d | draw=%.2f below=%.2f fade=%.2f bubd=%.2f alpha=%.2f pos=%.2f ulw=%.2f | total=%.2f\n",
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
    if (F < 2 || G < 2) return;
    // 画布必须装得下当前的岛，否则岛的左右会被裁掉 ——
    // 迷你岛"四角变方"就是被裁了 11px（圆角正好在那条边里）。这里是最后一道保险。
    // ⚠️ 多岛时代必须把"下面挂的原岛"和"正在淡出的 AI 岛"一起算进去：
    //    大岛 424 宽退出时画布只有 443，AI 岛 640 宽直接被裁成一块残片（踩过）。
    int needW = F + 16;
    if (belowOn && gBelow.F + 16 > needW) needW = gBelow.F + 16;
    if (aiFading && gAiOut.F + 16 > needW) needW = gAiOut.F + 16;
    if (j < needW) { j = needW; n = (B - j) / 2; }

    int needH = G + 12;
    if (belowOn && belowY + gBelow.G + 12 > needH) needH = belowY + gBelow.G + 12;
    if (aiFading && gAiOut.E + gAiOut.G + 12 > needH) needH = gAiOut.E + gAiOut.G + 12;
    if (l < needH) l = needH;
    if (!EnsureBuffers()) return;

    PF_MARK(0);
    GlobalsToState(gA);          // 先把当前全局状态收进 gA，再交给 DrawIsland
    if (belowRestore)
    {
        // 原岛正从 AI 岛下面滑回来：**直接拿快照画**（它是 J=3 的定格态）。
        // 不能画 gA —— 这时候 gA 还卡在 J=1 的过渡态里，画出来是空的（踩过）。
        IslandState tmp = gBelow;
        tmp.E = gBelow.E + restoreOff;
        DrawIsland(X, cc, j, l, tmp);          // 第一个画，clearCanvas 默认 true
    }
    else
    {
        DrawIsland(X, cc, j, l, gA);
    }
    PF_MARK(1);
    if (belowOn) DrawBelowOnCanvas(X, cc, j, l);   // 原岛原样挂在 AI 岛下面
    PF_MARK(2);
    if (aiFading) DrawAiFadeOnCanvas(X, cc, j, l);// 正在收起来的 AI 岛（往上飘 + 淡出）
    PF_MARK(3);
    if (aiMode) DrawBubbleOnCanvas(X, cc, j, l);   // 消息岛画在同一张画布上
    PF_MARK(4);

    // ===== 统一补 alpha =====
    // GDI（RoundRect / FillRect / TextOut）在 32 位 DIB 上画会把 alpha 字节压低，
    // 而岛用 UpdateLayeredWindow + AC_SRC_ALPHA，alpha 低就是半透明 →
    // litehtml 气泡和输入框底会"透出后面的窗口"。这里统一补回来。
    // 岛外面是纯 (0,0,0,0)，不会被误伤，圆角依旧。
    int iox = D - n;
    int ioy = E;
    if (belowRestore) ioy += restoreOff;        // 岛正在滑回去，补 alpha 的框得跟上
    int iow = F;
    int ioh = G;
    if (aiMode)
    {
        for (int bi = 0; bi < gBubN; bi++)
        {
            if (!gBubs[bi].showing || gBubs[bi].G <= 0) continue;
            int bubBottom = gBubs[bi].E + gBubs[bi].G;
            if (bubBottom - ioy > ioh) ioh = bubBottom - ioy;
        }
    }
    // ⚠️ 这一段是按"岛矩形"把外面全清成透明。所以下面挂着的原岛、
    //    以及正在淡出的 AI 岛，都必须算进这个框里，否则会被当成"岛外"直接擦掉。
    if (belowOn)
    {
        int bb2 = belowY + gBelow.G;
        if (bb2 - ioy > ioh) ioh = bb2 - ioy;
    }
    if (aiFading)
    {
        int ax = gAiOut.D - n;
        int aw = gAiOut.F;
        if (ax < iox) { iow += iox - ax; iox = ax; }
        if (aw > iow) iow = aw;
        if (gAiOut.E < ioy) { ioh += ioy - gAiOut.E; ioy = gAiOut.E; }
        int ab = gAiOut.E + gAiOut.G;
        if (ab - ioy > ioh) ioh = ab - ioy;
    }
    for (int yy = 0; yy < l; ++yy)
    {
        BYTE* row = X + (size_t)yy * cc * 4;
        bool inY = (yy >= ioy && yy < ioy + ioh);
        for (int xx = 0; xx < j; ++xx)
        {
            BYTE* qq = row + (size_t)xx * 4;
            // 岛外面一律清空 —— litehtml 的 clip 对 float 元素不生效，
            // 气泡会画到岛左右外面去（截图里灰气泡伸到岛左边）。这里兜底砍掉。
            if (!inY || xx < iox || xx >= iox + iow)
            {
                qq[0] = qq[1] = qq[2] = qq[3] = 0;
                continue;
            }
            if (qq[3] < 250 && (qq[0] | qq[1] | qq[2])) qq[3] = 255;
        }
    }

    PF_MARK(5);
    // 淡出：只对 AI 岛那块乘法，原岛和消息岛不受影响
    if (aiFading)
    {
        int ax0 = gAiOut.D - n, ay0 = gAiOut.E;
        int ax1 = ax0 + gAiOut.F, ay1 = ay0 + gAiOut.G;
        if (ax0 < 0) ax0 = 0;
        if (ay0 < 0) ay0 = 0;
        if (ax1 > j) ax1 = j;
        if (ay1 > l) ay1 = l;
        for (int yy = ay0; yy < ay1; yy++)
        {
            BYTE* row = X + (size_t)yy * cc * 4;
            for (int xx = ax0; xx < ax1; xx++)
            {
                BYTE* q = row + (size_t)xx * 4;
                // 前 70% 完全实心（靠位移出屏），最后 30% 才淡 —— 否则又变成"原地渐变"
                float fa2 = (aiFade > 0.30f) ? 1.0f : (aiFade / 0.30f);
                q[3] = (BYTE)(q[3] * fa2);
            }
        }
    }


    // 窗口尺寸先跟画布对齐，再交画面：某些环境里"窗口尺寸 ≠ 源画布尺寸"就会带出一条黑边
    static int lj = -1, ll = -1, ln = -1, ls = -1;
    int shift = aiShiftY;                       // AI 岛打开时，其他岛整体往下让
    if (j != lj || l != ll || n != ln || shift != ls)
    {
        // Z 序：AI 岛开着的时候，岛要插在它下面（否则迷你岛每秒抢最前，会把 AI 岛压下去）
        SetWindowPos(hWnd, gAiWnd ? gAiWnd : HWND_TOPMOST, n, shift, j, l, SWP_NOACTIVATE);
        lj = j; ll = l; ln = n; ls = shift;
    }
    PF_MARK(6);

    // 图层整体保持不透明，**透光交给逐像素 alpha 做**（分层窗口本来就是 AC_SRC_ALPHA）。
    // 这样能做到"背景透、字不透"：
    //   背景像素的 alpha 乘 nv → 真的能看到后面的桌面/网页（不是"画暗一点"）；
    //   时间那行字的 alpha 保持满 → 透出去的同时照样看得清。
    // 踩过的两个坑：① 只压颜色不压 alpha → 只是变暗，一点不透光；
    //             ② 用图层整体不透明度 → 字跟背景一起淡，看不清。
    BYTE ga = (BYTE)255;
    BLENDFUNCTION blend={AC_SRC_OVER,0,ga,AC_SRC_ALPHA};
    POINT ptSrc={0,0};
    SIZE  sz={j, l};
    POINT ptDst={n, 0};
    UpdateLayeredWindow(hWnd, NULL, &ptDst, &sz, U, &ptSrc, 0, &blend, ULW_ALPHA);
    PF_MARK(7);
#ifdef TEST_PERF
    PfFlush(gA.J, aiMode ? 1 : 0, gBubN);
#endif

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
// ===== 内置资源（2026-10-16 加）=====
// 提醒音、放学音乐、专辑封面三样都编进了 exe（见 buildres/app.rc 里的 RCDATA）。
// 这样"只拷一个 exe"就能用，不用再在旁边带 mp3 / jpg。
// MCI 和 GDI+ 都只能读**磁盘上的文件**，所以启动时把它们释放到安装目录（已存在就不覆盖，
// 用户自己换过的那份永远优先）。
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
    // 磁盘上已经有一份、大小也对 → 不重写（省一次 1MB 的写盘，也保住用户换过的音频）
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
                // 目标已经有了就别动 —— 用户自己换过的音频/封面永远优先
                if (GetFileAttributesW(to) != INVALID_FILE_ATTRIBUTES) continue;
                // ① exe 旁边有这份文件 → 用它（这是"用户想覆盖内置的"最直白的方式）
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
        // AI 岛长高不要回弹 —— 用户说"弹跳很别扭"，用 easeOutCubic 单调到位
        E = (int)(ao + (gy - ao) * (aiMode ? (float)(1.0 - pow(1.0 - t, 3.0)) : springY(t)));
        D = (B - F) / 2;
        vv = vv0 + (vt - vv0) * t;
        sf = sf0 + (sf1 - sf0) * t;
        // 收回时（J=5）：如果正在离开 AI 岛，背景从深色淡回天空
        if (!aiMode && aiBgMix > 0.001f) aiBgMix = AI_SKY_MASK * (1.0f - t);
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
        // J=9 是"变大"，如果目标是大岛 AI 模式 → 背景从天空淡到深色；J=5 反过来
        if (aiMode) aiBgMix = t * AI_SKY_MASK;
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
            if (aiMode) aiBgMix = AI_SKY_MASK; else aiBgMix = 0.0f;
            if (plyMix) { plyMix = false; ta = 255; plyA = 255.0f; }   // 过渡结束，播放器完全接管
            J = gJ;
            // AI 岛缩回迷你岛完成（J=5 那条路）→ 轮到 Key 窗"被吞"
            if (gJ == 12 && KwVisible()) { kwClosing = 1; kwcT0 = NowMs(); }
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
            if (!aiMode && bigH != ISLAND_HEIGHT) { bigH = ISLAND_HEIGHT; ComputeCanvasSize(); }
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
    mciSendStringW(L"play bgm from 0 notify", NULL, 0, o);
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
    AudioJob* j = (AudioJob*)p;
    AudioPlay(j->type, j->vol);
    delete j;
    return 0;
}
void AudioPlayAsync(AlarmType type)          // 试听：用全局音量
{
    AudioJob* j = new AudioJob{ type, -1 };
    HANDLE h = CreateThread(NULL, 0, AudioThreadProc, j, 0, NULL);
    if (h) CloseHandle(h);      // 不 Join：让它自己放完
    else delete j;
}
// 到点响铃：带这一节课自己的音量
void AudioPlayAsyncVol(AlarmType type, int vol)
{
    AudioJob* j = new AudioJob{ type, vol };
    HANDLE h = CreateThread(NULL, 0, AudioThreadProc, j, 0, NULL);
    if (h) CloseHandle(h);
    else delete j;
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
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD v = 38, sz = sizeof(v);
        if (RegQueryValueEx(hKey, L"SkyBright", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS) skyBr = (int)v;
        RegCloseKey(hKey);
    }
    if (skyBr < 0) skyBr = 0;
    if (skyBr > 100) skyBr = 100;
    HKEY h2;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &h2) == ERROR_SUCCESS)
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
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
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

static void SavePerfCfg()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0,
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
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &k) == ERROR_SUCCESS)
    {
        DWORD v = 0, sz = sizeof(v);
        if (RegQueryValueExW(k, L"LowPerf", NULL, NULL, (LPBYTE)&v, &sz) == ERROR_SUCCESS && v)
            gFrameMs = 33;
        RegCloseKey(k);
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
// 打开 AI 岛：从"当前形状"（迷你岛/小岛/大岛都行）顺着长成 AI 岛。
// 和 ShowPlayerIsland 一模一样的路子 —— J=9 变身，落点 gJ=3，只是内容改成 AI 的。
static bool AiHasKey();
static bool AiLoadKey(std::wstring& out);
void ShowKeyWindow();

// ===== AI 气泡数据 + 发送 =====
static int aiPending = -1;                // 正在等第几条的回答（-1 = 空闲）
static std::wstring aiAnswerSlot;         // 线程写完，主线程取走

static std::string aiWtoU8(const std::wstring& w)
{
    if (w.empty()) return "";
    int n2 = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string t(n2, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &t[0], n2, NULL, NULL);
    return t;
}
// JSON 里的字符串转义（和 HTML 的 aiEsc 不是一回事）
static std::string aiJsonEsc(const std::wstring& w)
{
    std::string u = aiWtoU8(w), o;
    for (size_t i = 0; i < u.size(); ++i)
    {
        unsigned char c = (unsigned char)u[i];
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == 0x0A) o += "\\n";
        else if (c == 0x0D) o += "\\r";
        else if (c == 0x09) o += "\\t";
        else o += (char)c;
    }
    return o;
}

// 从响应 JSON 里抠 choices[0].message.content
static std::string aiExtract(const std::string& body)
{
    size_t p = body.find("\"content\"");
    if (p == std::string::npos)
    {
        // 出错时把 message 字段拿出来（比如 key 无效）
        size_t e = body.find("\"message\"");
        if (e != std::string::npos)
        {
            size_t q1 = body.find('"', e + 9);
            if (q1 != std::string::npos)
            {
                std::string o2;
                for (size_t i = q1 + 1; i < body.size(); ++i)
                {
                    if (body[i] == '\\' && i + 1 < body.size()) { o2 += body[++i]; continue; }
                    if (body[i] == '"') break;
                    o2 += body[i];
                }
                if (!o2.empty()) return "（出错）" + o2;
            }
        }
        return "";
    }
    size_t q1 = body.find('"', p + 9);
    if (q1 == std::string::npos) return "";
    std::string out;
    for (size_t i = q1 + 1; i < body.size(); ++i)
    {
        char c = body[i];
        if (c == '\\' && i + 1 < body.size())
        {
            char nx = body[++i];
            if (nx == 'n') out += (char)0x0A;
            else if (nx == 'r') out += (char)0x0D;
            else if (nx == 't') out += (char)0x09;
            else if (nx == 'u')
            {
                // \uXXXX → 先转 UTF-8 太麻烦，直接丢掉（中文一般是原样 UTF-8，不会走这里）
                if (i + 4 < body.size()) i += 4;
            }
            else out += nx;
            continue;
        }
        if (c == '"') break;
        out += c;
    }
    return out;
}

// WinHTTP 同步请求（在线程里跑，不占 UI）
static std::string aiHttp(const std::wstring& key, const std::wstring& q)
{
    std::string out;
    std::string body = "{\"model\":\"deepseek-chat\",\"stream\":false,\"messages\":["
                       "{\"role\":\"user\",\"content\":\"" + aiJsonEsc(q) + "\"}]}";

    HINTERNET ses = WinHttpOpen(L"mobile/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return "（网络初始化失败）";
    WinHttpSetTimeouts(ses, 5000, 5000, 20000, 30000);

    HINTERNET con = WinHttpConnect(ses, L"api.deepseek.com", 443, 0);
    if (!con) { WinHttpCloseHandle(ses); return "（连不上 api.deepseek.com）"; }

    HINTERNET req = WinHttpOpenRequest(con, L"POST", L"/chat/completions", NULL,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req)
    {
        WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return "（建立请求失败）";
    }

    std::wstring hdr = L"Authorization: Bearer " + key +
                       L"\r\nContent-Type: application/json";
    BOOL ok = WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L,
                                 (LPVOID)body.c_str(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(req, NULL);
    if (ok)
    {
        DWORD avail = 0;
        do
        {
            avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail)) break;
            if (avail == 0) break;
            std::string buf(avail, 0);
            DWORD rd = 0;
            if (!WinHttpReadData(req, &buf[0], avail, &rd)) break;
            out.append(buf.data(), rd);
        } while (avail > 0);
    }
    else
    {
        DWORD ec = GetLastError();
        wchar_t msg[128];
        swprintf(msg, L"（请求失败 %lu）", ec);
        out = aiWtoU8(msg);
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return out;
}

DWORD WINAPI aiThread(LPVOID pv)
{
    int idx = (int)(intptr_t)pv;
    std::wstring key, q;
    if (idx >= 0 && idx < (int)aiQs.size())
    {
        q = aiQs[idx];
        AiLoadKey(key);          // DPAPI 解密（旧的明文也认）
    }

    std::string resp = key.empty() ? std::string() : aiHttp(key, q);
    std::string ans = key.empty() ? "（未接入 API：先填 Key）" : aiExtract(resp);

    std::wstring wans;
    if (!ans.empty())
    {
        int m = MultiByteToWideChar(CP_UTF8, 0, ans.c_str(), -1, NULL, 0);
        if (m > 1)
        {
            wans.assign(m - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, ans.c_str(), -1, &wans[0], m);
        }
    }
    if (wans.empty()) wans = L"（没拿到内容）";

    // 结果写到"结果槽"，主线程收到消息后再并入 aiAs（避免直接改 vector 的竞争）
    aiAnswerSlot = wans;
    PostMessage(o, WM_APP + 7, (WPARAM)idx, 0);
    return 0;
}

static std::string aiEsc(const std::wstring& w)
{
    std::string u = aiWtoU8(w), o;
    for (size_t i = 0; i < u.size(); ++i)
    {
        char c = u[i];
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}
static std::string aiBuildHtml()
{
    std::string h = "<html><body style='margin:0;padding:0;color:#ffffff;font-size:15px;'>";
    for (size_t i = 0; i < aiQs.size(); ++i)
    {
        h += "<div style='float:right;clear:both;background:#2c4a7c;border-radius:11px;"
             "padding:7px 12px;margin:0 0 9px 0;'>" + aiEsc(aiQs[i]) + "</div>";
        if (i < aiAs.size() && !aiAs[i].empty())
            h += "<div style='float:left;clear:both;background:#3a3a45;border-radius:11px;"
                 "padding:7px 12px;margin:0 0 8px 0;'>" + aiEsc(aiAs[i]) + "</div>";
        else if (aiPending == (int)i)
            h += "<div style='float:left;clear:both;color:#8a8a98;padding:2px 4px;margin:0 0 8px 0;'>"
                 "思考中…</div>";
        else
            h += "<div style='float:left;clear:both;color:#8a8a98;padding:2px 4px;margin:0 0 8px 0;'>"
                 "（未回答）</div>";
    }
    h += "<div style='clear:both;height:1px;'></div></body></html>";
    return h;
}
static void aiRebuild()
{
    std::string h = aiBuildHtml();
    gAiDoc = litehtml::document::createFromString(h.c_str(), &gAiLite, litehtml::master_css);
    if (gAiDoc) gAiDoc->render((litehtml::pixel_t)AI_W);
}

// 发送：问句进气泡 → 如果还在输入态，长成两段式
static void aiSend(HWND hWnd)
{
    aiLastAct = NowMs();
    AiHistPush(aiInput, L"（还没回）");
    aiQs.push_back(aiInput);
    aiAs.push_back(L"");
    aiScroll = 999999;          // 新消息 → 贴底（绘制时 clamp）
    aiInput.clear();
    aiPending = (int)aiQs.size() - 1;
    aiRebuild();

    // 正常提问走真 API（没填 Key 的话 aiThread 会直接回一句"未接入 API"，不会发请求）
    // 「测」按钮才是纯本地、不碰网络。
    {
        HANDLE th = CreateThread(NULL, 0, aiThread, (LPVOID)(intptr_t)aiPending, 0, NULL);
        if (th) CloseHandle(th);
    }

    if (aiH != AI_H)                      // 输入态 → 对话态：长高
    {
        aiH = AI_H;
        bigH = aiH;
        ComputeCanvasSize();
        ae = F; an = G; ao = E;
        sf0 = sf; sf1 = 0.0f; nt = false;
        gw = AI_W; gh = aiH; gy = ISLAND_FINAL_Y; gJ = 3;
        vv0 = vv; vt = 1.0f; oo = 0; J = 9;
        R = NowMs();
    }
    else
    {
        UpdateIslandDisplay(hWnd, 1.0f, 255);
    }
}
void HideKeyWindow();

void ShowAiIsland()
{
    if (aiMode && J == 3) return;                 // 已经在 AI 岛里了
    // ★ 先把此刻屏幕上这个岛整块存下来：AI 岛一会儿从顶上飞出来，它要原样滑到下面去
    {
        IslandState cur;
        GlobalsToState(cur);
        // 原岛正滑回半路又被点开：快照要按它**眼前**的位置来，不然会跳一下
        if (belowRestore) { cur.E += restoreOff; belowRestore = false; restoreOff = 0; }
        StartBelowIsland(cur);
    }
    aiMode = true;
    aiMorphT0 = NowMs();   // 第 10 项：旧字→输入框的交叉渐变从这里开始计时
    gAiDoc = nullptr;      // 每次打开都重建，别把上次的气泡带过来
    // 别在这里同步建 Key 窗：创建窗口 + SetFocus 全压在变身第一帧上，会卡住动画开局。
    // 推迟 120ms 再弹，第一帧就轻了。
    if (!AiHasKey()) SetTimer(o, 16, 120, NULL);
    aiLastAct = NowMs();          // 重置闲置计时
    aiH = aiHist.empty() ? 120 : AI_H_EMPTY;   // 有历史就高一点，腾出历史区
#ifdef TEST_BUBBLE
    SetTimer(o, 21, 2500, NULL);
#endif
#ifdef TEST_MULTI
    SetTimer(o, 31, 3600, NULL);   // 测试多岛：AI 岛打开 3.6 秒后再补两条
#endif
    bigH = aiH;
    g = AI_W;
    ComputeCanvasSize();

    if (J == 1 || J == 9) { return; }             // 正在弹/变，内容自己会变成 AI 岛
    // 不是迷你岛（正在跑倒计时小岛 / 大岛消息等）：AI 岛从屏幕最上方掉下来，原岛被接管
    if (J != 12 && J != 11 && J != 13)
    {
        F = AI_W; G = aiH; E = -aiH - 8; D = (B - F) / 2;
        bigH = aiH; g = AI_W;
        ComputeCanvasSize();
        vv0 = 0.0f; vt = 1.0f; vv = 0.0f;
        sf0 = 0.0f; sf1 = 0.0f; sf = 0.0f;
        nt = false; O = 0; I = 0.0f;
        aiShiftY = 0;
        J = 1;                                     // 大岛弹出那套：从上方掉下来（springY 自带果冻过冲）
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    if (J == 0)
    {
        F = ISLAND_MIN_WIDTH; G = ISLAND_MIN_HEIGHT; E = 0; D = (B - F) / 2;
        vv0 = 0.0f; vt = 0.0f; vv = 0.0f;
        sf0 = 0.0f; sf1 = 0.0f; sf = 0.0f;
        O = 0; I = 0.0f; J = 1;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    // 关键：从**当前实际尺寸**（迷你岛 150×48 @y=2）接着长，接缝处完全不跳
    ae = F; an = G; ao = E;
    sf0 = sf; sf1 = 0.0f; nt = false;
    gw = AI_W; gh = aiH; gy = ISLAND_FINAL_Y; gJ = 3;
    vv0 = 0.0f; vt = 1.0f; oo = 0; J = 9;
    R = NowMs();
}

// 收起 AI 岛：顺着缩回迷你岛（同一套变身，落点 gJ=12）
void CloseAiIsland()
{
    if (!aiMode) return;
    aiMode = false;
    // 关掉就把本轮问答清空（历史已经存在 aiHist 里），下次打开回到输入态
    aiQs.clear(); aiAs.clear(); aiScroll = 0; aiHistHit = -1;
    // 文档先留着：AI 岛还要拿它做淡出动画，等淡完再清（见 BubbleTick）
    HideKeyWindowNow();   // 先直接收掉（"被吞"动画还没通，别挂着）
    kwClosing = 0;

    // ★ 有"原岛"挂在下面：把它原样还给主岛，然后让它自己滑回原位。
    //   AI 岛这一层直接撤掉（它的位置本来就在顶上，撤掉不突兀）。
    if (belowOn)
    {
        // AI 岛留一份画面出来做淡出，别直接抹掉
        gAiOut = gA;
        aiFading = true;
        aiFade = 1.0f;

        belowOn = false;
        BubClearAll();               // 下面挂的消息岛也一起收，别留着
        gA = gBelow;
        StateToGlobals(gA);          // 状态机从现在起按原岛的身份跑
        // ★ 大岛回来要重新计时：S 存的还是快照那一刻（弹出+5 秒），早就过期了，
        //   不重置的话它会刚滑回来就立刻缩回去（自检时抓到的）。
        if (J == 1 || J == 3) S = NowMs() + DISPLAY_DURATION_MS;
        restoreOff = belowY - gA.E;  // 当前还挂在下面，先记下差多少
        belowRestore = true;
        bigH = gA.bigH;
        g = gA.F;
        ComputeCanvasSize();
        UpdateIslandDisplay(o, 1.0f, 255);
        return;
    }

    // AI 岛下面还挂着消息 → 升级成主岛大岛继续显示，别让消息跟着 AI 岛一起消失
    if (BubblePromoteToMain()) return;
    // Key 窗不在这里收：等 AI 岛缩完，由 J=5 结束那一下触发"被迷你岛吞掉"的动画
    bigH = ISLAND_HEIGHT;
    g = ISLAND_MIN_WIDTH;
    ComputeCanvasSize();          // ← 必须重算：不然画布还是 640×120，窗口尺寸和命中区全错位
    ae = F; an = G; ao = E;
    // nt=true 才是"悬浮迷你岛（四角圆角）"；写 false 会变成贴顶直角 —— 之前就是这个 bug
    sf0 = sf; sf1 = 0.0f; nt = true;
    gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
    vv0 = vv; vt = 1.0f; oo = 0; J = 5;
    R = NowMs();
}

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
    // ★ 这条变身路以前漏了 S：S 还是上一轮的（比如 AI 岛收回时设的 +5 秒），
    //   早过期 → 下一帧就被当"该收了"，J 变 4 把 ply 清掉，播放器刚出来就没了。
    S = NowMs() + PLAYER_LONG;
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
HWND sPeriod[11] = {0};           // 时段勾选框（＝到点弹岛）
HWND sBellUp[11] = {0};           // 每节课的上课铃勾选
HWND sBellDn[11] = {0};           // 每节课的下课铃勾选
HWND sBellUpVol[11] = {0};        // 每节课的上课铃音量滑块
HWND sBellDnVol[11] = {0};        // 每节课的下课铃音量滑块
HWND sBellUpPct[11] = {0};        // 上课铃音量百分比
HWND sBellDnPct[11] = {0};        // 下课铃音量百分比
HWND sAiKeyState = NULL;          // AI 页的状态文字
HWND sTryPos = NULL;              // 试听进度条（0~100%）
HWND sTryPct = NULL;              // 试听位置百分比
HWND sTbl[3] = {0};               // 作息表：0=初四夏季 1=冬季 2=初四冬季
HWND sVol[3] = {0};               // 音量滑块：0=上课铃 1=下课铃 2=《回家》
HWND sVolPct[3] = {0};            // 音量百分比
HWND sBgmOn = NULL;               // 放学播放《回家》
HWND sTheme[2] = {0};             // 主题：0=浅色 1=深色
HWND sAudioOn = NULL, sHoliday = NULL, sMini = NULL;
HWND sLowPerf = NULL;                    // 低配模式勾选框
HWND sSkyBr = NULL, sSkyBrPct = NULL;   // 天空亮度滑块 + 百分比
HWND sNotchA = NULL, sNotchAPct = NULL; // 迷你岛静止透明度滑块 + 百分比
HWND sSkyRe = NULL, sSkyGo = NULL;      // 天空动画：重播 / 直接变
HWND sSkyOn = NULL;                     // 天空背景总开关
HFONT sFont = NULL;
#define ID_NAV_BASE 1100       // 左侧导航按钮：1100..1104
#define ID_BELLUP_BASE 1120    // 每节课的上课铃勾选：1120..1130
#define ID_BELLDN_BASE 1140    // 每节课的下课铃勾选：1140..1150
#define ID_BELLUP_VOL_BASE 1200   // 每节课上课铃音量滑块：1200..1210
#define ID_BELLDN_VOL_BASE 1220   // 每节课下课铃音量滑块：1220..1230
#define ID_AI_KEY_EDIT  1160   // API Key 输入框
#define ID_AI_KEY_PASTE 1161   // 从剪贴板粘贴
#define ID_AI_KEY_SAVE  1162   // 保存
#define ID_AI_KEY_DEL   1163   // 删除已保存的 Key

static HWND sStatus = NULL;    // 导航栏底部常驻状态
#define ID_SET_LOWPERF 1085   // 低配模式勾选
#define ID_SET_CLOSE 1090
#define ID_SET_PREV0 1091
#define ID_SET_PREV1 1092
#define ID_SET_TRY   1170   // 独立试听：从进度条选的位置开始放
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
// 背景变黑块，试过，已回滚）。整窗淡出又像"先消失再出现"，用户明确否决。
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

static void UpdateAiKeyState()
{
    if (!sAiKeyState) return;
    std::wstring k;
    if (AiLoadKey(k))
        SetWindowTextW(sAiKeyState, L"状态：已设置（为了安全，这里不显示 Key 本身）");
    else
        SetWindowTextW(sAiKeyState, L"状态：未设置 —— 没有 Key 的话 AI 岛问不了问题");
}

static void PageApply(int p)
{
    for (int i = 0; i < SET_PAGES; i++)
        for (HWND h : gPageCtl[i]) if (h) ShowWindow(h, (i == p) ? SW_SHOW : SW_HIDE);
    if (p == 1)
        for (int k = 0; k < 11; k++)
        {
            // 换表后多出来的节次：三列都得藏，不然会剩下孤零零的铃勾选
            if (sPeriod[k]) ShowWindow(sPeriod[k], (k < pc) ? SW_SHOW : SW_HIDE);
            if (sBellUp[k]) ShowWindow(sBellUp[k], (k < pc) ? SW_SHOW : SW_HIDE);
            if (sBellDn[k]) ShowWindow(sBellDn[k], (k < pc) ? SW_SHOW : SW_HIDE);
            if (sBellUpVol[k]) ShowWindow(sBellUpVol[k], (k < pc) ? SW_SHOW : SW_HIDE);
            if (sBellDnVol[k]) ShowWindow(sBellDnVol[k], (k < pc) ? SW_SHOW : SW_HIDE);
            if (sBellUpPct[k]) ShowWindow(sBellUpPct[k], (k < pc) ? SW_SHOW : SW_HIDE);
            if (sBellDnPct[k]) ShowWindow(sBellDnPct[k], (k < pc) ? SW_SHOW : SW_HIDE);
        }
    gCurPage = p;
    if (p == 2) UpdateAiKeyState();
    for (int i = 0; i < SET_PAGES; i++)
        if (sNav[i]) SendMessageW(sNav[i], BM_SETSTATE, (i == p) ? TRUE : FALSE, 0);
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
        // 两个铃：跟着总开关走 —— 关掉时只是灰掉，不影响"到点弹岛"那一列
        if (sBellUp[i])
        {
            ShowWindow(sBellUp[i], (i < pc) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellUp[i], BM_SETCHECK, bellUp[i] ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(sBellUp[i], fuck ? TRUE : FALSE);
        }
        if (sBellDn[i])
        {
            ShowWindow(sBellDn[i], (i < pc) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellDn[i], BM_SETCHECK, bellDn[i] ? BST_CHECKED : BST_UNCHECKED, 0);
            EnableWindow(sBellDn[i], fuck ? TRUE : FALSE);
        }
        if (sBellUpVol[i])
        {
            ShowWindow(sBellUpVol[i], (i < pc) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellUpVol[i], TBM_SETPOS, TRUE, bellUpVol[i]);
            EnableWindow(sBellUpVol[i], fuck ? TRUE : FALSE);
        }
        if (sBellUpPct[i])
        {
            ShowWindow(sBellUpPct[i], (i < pc) ? SW_SHOW : SW_HIDE);
            wchar_t pb2[16]; swprintf_s(pb2, L"%d%%", bellUpVol[i]);
            SetWindowTextW(sBellUpPct[i], pb2);
            EnableWindow(sBellUpPct[i], fuck ? TRUE : FALSE);
        }
        if (sBellDnVol[i])
        {
            ShowWindow(sBellDnVol[i], (i < pc) ? SW_SHOW : SW_HIDE);
            SendMessageW(sBellDnVol[i], TBM_SETPOS, TRUE, bellDnVol[i]);
            EnableWindow(sBellDnVol[i], fuck ? TRUE : FALSE);
        }
        if (sBellDnPct[i])
        {
            ShowWindow(sBellDnPct[i], (i < pc) ? SW_SHOW : SW_HIDE);
            wchar_t pb3[16]; swprintf_s(pb3, L"%d%%", bellDnVol[i]);
            SetWindowTextW(sBellDnPct[i], pb3);
            EnableWindow(sBellDnPct[i], fuck ? TRUE : FALSE);
        }
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
    if (sLowPerf) SendMessageW(sLowPerf, BM_SETCHECK,
                               (WPARAM)(gFrameMs > 20 ? BST_CHECKED : BST_UNCHECKED), 0);
    if (sSkyBr)     SendMessageW(sSkyBr, TBM_SETPOS, TRUE, skyBr);
    if (sSkyBrPct)  { swprintf_s(b, L"%d%%", skyBr); SetWindowTextW(sSkyBrPct, b); }
    if (sNotchA)    SendMessageW(sNotchA, TBM_SETPOS, TRUE, notchA);
    if (sNotchAPct) { swprintf_s(b, L"%d%%", notchA); SetWindowTextW(sNotchAPct, b); }
    if (sSkyRe)     SendMessageW(sSkyRe, BM_SETCHECK,  skyReplay ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sSkyGo)     SendMessageW(sSkyGo, BM_SETCHECK, !skyReplay ? BST_CHECKED : BST_UNCHECKED, 0);
    if (sSkyOn)     SendMessageW(sSkyOn, BM_SETCHECK, skyEn ? BST_CHECKED : BST_UNCHECKED, 0);
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
            SendMessageW(o, WM_COMMAND, (WPARAM)id, 0);     // 复用主窗口那套（勾选 + 存盘）
            SetSyncControls();
            return 0;
        }
        if (id == ID_AI_KEY_PASTE)
        {
            if (OpenClipboard(hWnd))
            {
                HANDLE hd = GetClipboardData(CF_UNICODETEXT);
                if (hd)
                {
                    const wchar_t* cp = (const wchar_t*)GlobalLock(hd);
                    if (cp)
                    {
                        std::wstring vv(cp);
                        // 顺手把首尾空白/换行清掉（从网页复制常带这些）
                        while (!vv.empty() && (vv.back() == L' ' || vv.back() == L'\r' || vv.back() == L'\n' || vv.back() == L'\t')) vv.pop_back();
                        size_t b = vv.find_first_not_of(L" \r\n\t");
                        if (b == std::wstring::npos) vv.clear(); else vv = vv.substr(b);
                        SetDlgItemTextW(hWnd, ID_AI_KEY_EDIT, vv.c_str());
                        GlobalUnlock(hd);
                    }
                }
                CloseClipboard();
            }
            return 0;
        }
        if (id == ID_AI_KEY_SAVE)
        {
            wchar_t buf[512] = { 0 };
            GetDlgItemTextW(hWnd, ID_AI_KEY_EDIT, buf, 512);
            std::wstring vv(buf);
            while (!vv.empty() && (vv.back() == L' ' || vv.back() == L'\r' || vv.back() == L'\n' || vv.back() == L'\t')) vv.pop_back();
            size_t b = vv.find_first_not_of(L" \r\n\t");
            if (b == std::wstring::npos) vv.clear(); else vv = vv.substr(b);
            if (!vv.empty())
            {
                AiSaveKey(vv.c_str());
                SetDlgItemTextW(hWnd, ID_AI_KEY_EDIT, L"");   // 存完就清掉，界面上不留明文
                UpdateAiKeyState();
                MessageBoxW(hWnd, L"已保存。Key 已加密存到本机，下次打开不会显示出来。", L"AI 岛", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
        if (id == ID_AI_KEY_DEL)
        {
            if (MessageBoxW(hWnd, L"确定删除已保存的 API Key？删了之后 AI 岛就问不了问题了。",
                            L"删除 API Key", MB_YESNO | MB_ICONWARNING) == IDYES)
            {
                AiDeleteKey();
                SetDlgItemTextW(hWnd, ID_AI_KEY_EDIT, L"");
                UpdateAiKeyState();
            }
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
        case ID_TRAY_AUDIO_ON: case ID_TRAY_HOLIDAY: case ID_TRAY_SKY:
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
            if (J == 11 || J == 12) UpdateIslandDisplay(o, 1.0f, 255);
            else if (J == 0 && ns) { ShowNotch(); nv = 255 * notchA / 100; }
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
            if (J == 1 || J == 3) { wcscpy_s(gg, pv); UpdateIslandDisplay(o, 1.0f, 255); }  // 已经在显示 → 只换字
            else if (J == 0 || J == 11 || J == 12) ShowMessageIsland(pv);                   // 没显示 → 弹出来给你看
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
        if (wParam == 1 && sStatus)
        {
            // 状态直接挂在导航栏底部（用户：不用再点一下"状态信息"）
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
            wchar_t b2[192];
            if (nxt >= 0)
                swprintf_s(b2, L"运行中\n现在 %02d:%02d\n下次提醒 %02d:%02d", st2.wHour, st2.wMinute, nxt / 60, nxt % 60);
            else
                swprintf_s(b2, L"运行中\n现在 %02d:%02d\n今天没有更多提醒了", st2.wHour, st2.wMinute);
            SetWindowTextW(sStatus, b2);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
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

// 出现动画用的缓动：先快、后慢、最后轻轻过冲一点再回来。
// 不用 springY —— 那条曲线是给"62 帧的岛变身"调的，前 20% 就冲到位，看着像没动画。
static float AiEase(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    // 苹果 iMessage 的手感 = **匀速放大 → 到顶时多冲一点 → 轻轻回落**。
    // 之前用的 easeOutBack 前 30% 就跑完 90%，看着像"瞬间变大然后卡住"（用户骂的就是这个）。
    // 现在：78% 的时间匀速冲到 1.06，剩下 22% 从 1.06 回落到 1.00。
    const float P = 0.78f, OV = 1.06f;
    if (t < P) return (t / P) * OV;
    float u = (t - P) / (1.0f - P);
    return OV - (OV - 1.0f) * u;
}

// ===== AI 岛（2026-10-21 v2，纯 GDI 绘制）=====
// 用 GDI 而不是 GDI+：GDI+ 在这个窗口里出现过"整段绘制静默失败"，
// 而 GDI 在任何机器上都能画出来。代价是圆角有一点锯齿，但换来的是"一定能看见"。
static HWND aiWnd = NULL, aiEdit = NULL, aiMenu = NULL;
static int  aiThink = 1;        // 0=off 1=high 2=max
static bool aiShown = false;
static int  aiW = AI_W;
static int  aiY = ISLAND_FINAL_Y;   // AI 岛的 y：起点用迷你岛的 NOTCH_Y，终点 ISLAND_FINAL_Y
static float aiCtl = 0.0f;      // 控件透明度 0→1（形状定型后才淡入）
static float ctlE = 0, ctlT = 0, ctlS = 0, ctlX = 0;   // 四个控件各自的淡入进度
static int   ctlFrm = 0;        // 淡入开始后经过的帧数（30ms/帧）
static int   aiClosing = 0;     // 1 = 正在回收（反向缩回迷你岛）
static LONGLONG aiT0 = 0;       // 本轮动画的起始时间（真实时间戳，掉帧也不影响速度）
static LONGLONG kwT0 = 0;
// Key 窗"从 AI 岛底部中间 1/3 冒出来"：起始只有 AI 岛宽的 1/3、高 20，然后长大
static int kwFromW = 0, kwFromH = 20, kwFromY = 0;

// 果冻曲线：冲过头再缩回来（easeOutBack，c1=1.5 → 过冲约 8%）
static float JellyEase(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float c1 = 1.5f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
static int   keyShown = 0;      // Key 窗本次只弹一次
static LONGLONG aiIdleAt = 0;   // 最后一次操作时间（8 秒没动静就自动收）
static int  aiTargetH = AI_H;
static float aiGrow = 1.0f;
static int  sendMix = 0;        // 0=白 255=蓝
static std::wstring aiA;        // 答案
static bool aiBusy = false;
static int  menuFor = -1;       // 0=思考 1=模型（弹窗当前给谁用）

static const wchar_t* AiThinkTxt()
{
    return aiThink == 0 ? L"思考 off" : (aiThink == 1 ? L"思考 high" : L"思考 max");
}

// ---- 配色：跟主题 z ----
static COLORREF AiBg()    { return z ? RGB(26, 26, 32)    : RGB(248, 248, 252); }
static COLORREF AiPanel() { return z ? RGB(40, 40, 48)    : RGB(255, 255, 255); }
static COLORREF AiText()  { return z ? RGB(238, 238, 244) : RGB(35, 35, 45); }
static COLORREF AiSub()   { return z ? RGB(150, 150, 162) : RGB(120, 120, 132); }
static COLORREF AiLine()  { return z ? RGB(58, 58, 68)    : RGB(218, 218, 226); }

struct AiRect { int x, y, w, h; };
// 一行排下来：输入框 / 思考 / ↑ / ×。× 必须和 ↑ 拉开，否则点不着
static AiRect AiREdit()  { return { 16, 18, 420, 34 }; }
static AiRect AiRThink() { return { 444, 18, 76, 34 }; }
static AiRect AiRSend()  { return { 528, 14, 42, 42 }; }
static AiRect AiRClose() { return { 588, 18, 28, 28 }; }
static int   AiAnsTop()  { return 66; }
static int   AiAnsH()    { return aiH - AiAnsTop() - 14; }

// 把颜色按 a（0~1）混向背景色，模拟淡入
static COLORREF AiMix(COLORREF c, float a)
{
    if (a >= 0.995f) return c;
    if (a <= 0.005f) return AiBg();
    COLORREF b = AiBg();
    int r = GetRValue(b) + (int)((GetRValue(c) - GetRValue(b)) * a);
    int g = GetGValue(b) + (int)((GetGValue(c) - GetGValue(b)) * a);
    int bl = GetBValue(b) + (int)((GetBValue(c) - GetBValue(b)) * a);
    return RGB(r, g, bl);
}

static void AiRelayout();
static void AiRecalc();
static bool AiHasKey();      // 实现在下面（Key 窗那段）
void ShowKeyWindow();
void HideKeyWindow();
void HideKeyWindowNow();
bool KwVisible();

// ---- 小工具：画圆角矩形 ----
static void AiRoundRect(HDC dc, int x, int y, int w, int h, int r, COLORREF fill, COLORREF line)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pn = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pn);
    RoundRect(dc, x, y, x + w + 1, y + h + 1, r, r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pn);
}

static void AiTextOut(HDC dc, const wchar_t* t, RECT r, COLORREF c, HFONT f, UINT fmt)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    HGDIOBJ of = SelectObject(dc, f);
    DrawTextW(dc, t, -1, &r, fmt);
    SelectObject(dc, of);
}

// 量答案需要多高（GDI 的 DrawText 带 DT_CALCRECT，和绘制完全一致）
static int AiMeasureH(const std::wstring& t, int w)
{
    if (t.empty() || w < 40) return 0;
    HDC dc = GetDC(NULL);
    HFONT f = GetCachedFont(17, false);
    HGDIOBJ of = SelectObject(dc, f);
    RECT r = { 0, 0, w, 4000 };
    DrawTextW(dc, t.c_str(), -1, &r, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    return r.bottom - r.top;
}

static void AiRecalc()
{
    int maxH = GetSystemMetrics(SM_CYSCREEN) / 3;   // 用户要求：高不超过屏幕 1/3
    if (maxH < AI_H) maxH = AI_H;
    int need = AI_H;
    if (!aiA.empty())
    {
        int ah = AiMeasureH(aiA, aiW - 32);
        need = AiAnsTop() + ah + 14;
    }
    if (need > maxH) need = maxH;
    aiTargetH = need;
}

// 真正收掉（不做动画）
void HideAiWindowNow()
{
    aiShown = false;
    aiClosing = 0;
    aiShiftY = 0;
    if (aiWnd) KillTimer(aiWnd, 7301);
    if (aiMenu) ShowWindow(aiMenu, SW_HIDE);
    HideKeyWindow();
    if (aiWnd) ShowWindow(aiWnd, SW_HIDE);
    if (o) ShowWindow(o, SW_SHOW);
    if (ns) ShowNotch();
}

// 用户点了 × / Esc：先反向缩回迷你岛的尺寸，再真正收掉
void HideAiWindow()
{
    if (!aiWnd || !aiShown || aiClosing) { HideAiWindowNow(); return; }
    // 主窗口先露出来垫在下面。
    // ⚠️ 不能用 ShowNotch()：那会让迷你岛重新播放一次"从上掉下来"的弹出动画，
    // 看起来就是"迷你岛先弹出来、AI 岛再收"，完全不是变形。
    // 这里直接把它摆到常驻位（J=12），不播任何动画 —— 被 AI 岛挡住，看不见。
    if (o)
    {
        F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = NOTCH_Y; D = (B - F) / 2;
        pp = false; hh = false; nt = true; nv = 255; na = NowMs();
        O = 0; vv = 1.0f; ai = -1;
        J = 12;
        ShowWindow(o, SW_SHOW);
        R = NowMs();
    }
    aiClosing = 1;                  // 交给 WM_TIMER 7301 逐步缩小
    aiT0 = NowMs();                 // 回收也走 0.8 秒（同一条曲线倒放）
    if (KwVisible()) kwT0 = NowMs();   // Key 窗同步往回滑
    SetTimer(aiWnd, 7301, 16, NULL);
}

// ---------- 选择弹窗（思考 / 模型共用）----------
static LRESULT CALLBACK AiMenuProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC dc0 = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(dc0);
        HBITMAP bm = CreateCompatibleBitmap(dc0, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(dc, bm);
        AiRoundRect(dc, 0, 0, rc.right - 1, rc.bottom - 1, 16, AiPanel(), AiLine());
        HFONT f = GetCachedFont(15, false);
        const wchar_t* items[3] = { L"off", L"high", L"max" };
        int n = 3;
        int ih = 34;
        for (int i = 0; i < n; i++)
        {
            int y = 6 + i * ih;
            bool sel = (aiThink == i);
            if (sel) AiRoundRect(dc, 6, y, rc.right - 12, ih - 4, 10, z ? RGB(58, 58, 70) : RGB(232, 236, 246), AiLine());
            RECT tr = { 18, y, rc.right - 8, y + ih - 4 };
            AiTextOut(dc, items[i], tr, sel ? AiText() : AiSub(), f, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        BitBlt(dc0, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        int i = (GET_Y_LPARAM(l) - 6) / 34;
        if (i >= 0 && i < 3) aiThink = i;
        ShowWindow(h, SW_HIDE);
        if (aiWnd) { InvalidateRect(aiWnd, NULL, FALSE); SetFocus(aiEdit); }
        return 0;
    }
    case WM_KILLFOCUS:
        ShowWindow(h, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void AiPopupMenu()
{
    static bool reg = false;
    if (!reg)
    {
        reg = true;
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = AiMenuProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"MobileAiMenu";
        RegisterClassExW(&wc);
    }
    int n = 3;
    int mw = 120, mh = 6 * 2 + n * 34 + 6;
    if (!aiMenu)
        aiMenu = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                 L"MobileAiMenu", L"", WS_POPUP,
                                 0, 0, mw, mh, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!aiMenu) return;
    HRGN rg = CreateRoundRectRgn(0, 0, mw + 1, mh + 1, 16, 16);
    SetWindowRgn(aiMenu, rg, TRUE);

    // 位置：贴在对应按钮下方
    RECT wr; GetWindowRect(aiWnd, &wr);
    AiRect b = AiRThink();
    SetWindowPos(aiMenu, HWND_TOPMOST, wr.left + b.x, wr.top + b.y + b.h + 4, mw, mh,
                 SWP_SHOWWINDOW);
    InvalidateRect(aiMenu, NULL, TRUE);
    SetForegroundWindow(aiMenu);
    SetFocus(aiMenu);
}

// ---------- AI 岛本体 ----------
static LRESULT CALLBACK AiProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc0 = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(dc0);
        HBITMAP bm = CreateCompatibleBitmap(dc0, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(dc, bm);

        // 底
        HBRUSH bg = CreateSolidBrush(AiBg());
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        HFONT fb = GetCachedFont(15, false);
        HFONT ft = GetCachedFont(17, false);

        // 形状还在放大的时候，把迷你岛那行**时间**继续画出来（字号跟着宽度放大），
        // 这样看起来就是"那行时间自己变大"，而不是"一块黑盖上来"。
        if (aiCtl < 0.02f)
        {
            if (aiW > 4)
            {
                SYSTEMTIME st; GetLocalTime(&st);
                wchar_t tb[16];
                swprintf(tb, L"%02d:%02d", st.wHour, st.wMinute);
                int fs = (int)(28.0 * aiW / (double)NOTCH_WIDTH);   // 迷你岛 150 宽时是 28px
                if (fs < 10) fs = 10;
                if (fs > 120) fs = 120;
                HFONT ft2 = GetCachedFont(fs, true);
                RECT tr2 = { 0, 0, aiW, aiH };
                // 后 15% 把时间淡掉（不然会"啪"一下消失，换成输入框太突兀）
                float ta2 = (aiGrow < 0.85f) ? 1.0f : (1.0f - (aiGrow - 0.85f) / 0.15f);
                if (ta2 < 0.0f) ta2 = 0.0f;
                COLORREF tc2 = z ? RGB(255, 225, 140) : RGB(35, 35, 45);
                tc2 = AiMix(tc2, ta2);
                AiTextOut(dc, tb, tr2, tc2, ft2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            BitBlt(dc0, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, ob);
            DeleteObject(bm);
            DeleteDC(dc);
            EndPaint(h, &ps);
            return 0;
        }

        // 输入框（EDIT 本体盖在上面，这里画底和边）
        AiRect e = AiREdit();
        if (ctlE > 0.01f)
            AiRoundRect(dc, e.x, e.y, e.w, e.h, 12, AiMix(AiPanel(), ctlE), AiMix(AiLine(), ctlE));

        // 两个胶囊
        auto pill = [&](AiRect a, const wchar_t* txt, float al)
        {
            if (al <= 0.01f) return;
            AiRoundRect(dc, a.x, a.y, a.w, a.h, a.h, AiMix(AiPanel(), al), AiMix(AiLine(), al));
            RECT tr = { a.x, a.y, a.x + a.w, a.y + a.h };
            AiTextOut(dc, txt, tr, AiMix(AiText(), al), fb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        };
        pill(AiRThink(), AiThinkTxt(), ctlT);

        // 圆形「问」：空=白底深箭头，有字=蓝底白箭头，sendMix 做过渡
        AiRect s2 = AiRSend();
        int cx = s2.x + s2.w / 2, cy = s2.y + s2.h / 2;
        {
            // 兜底：输入框里有字就一定是"蓝"，定时器哪怕没跑也不会卡在白底
            int t = sendMix;
            if (aiEdit && GetWindowTextLengthW(aiEdit) > 0) t = 255;
            else if (aiEdit) t = 0;
            auto mx = [&](int a, int b) { return (BYTE)(a + (b - a) * t / 255); };
            COLORREF fill = AiMix(RGB(mx(250, 52), mx(250, 120), mx(250, 246)), ctlS);
            HBRUSH br = CreateSolidBrush(fill);
            HPEN pn = CreatePen(PS_SOLID, 1, AiLine());
            HGDIOBJ ob2 = SelectObject(dc, br), op2 = SelectObject(dc, pn);
            Ellipse(dc, s2.x, s2.y, s2.x + s2.w + 1, s2.y + s2.h + 1);
            SelectObject(dc, ob2); SelectObject(dc, op2);
            DeleteObject(br); DeleteObject(pn);

            // 白底要用深色箭头（之前用了 AiText()，深色主题下是浅色 → 白底上看不见）
            COLORREF ac = AiMix((t > 127) ? RGB(255, 255, 255) : RGB(42, 42, 52), ctlS);
            HPEN ap = CreatePen(PS_SOLID, 2, ac);
            HGDIOBJ op3 = SelectObject(dc, ap);
            MoveToEx(dc, cx, cy + 8, NULL); LineTo(dc, cx, cy - 7);
            MoveToEx(dc, cx - 6, cy - 1, NULL); LineTo(dc, cx, cy - 7);
            MoveToEx(dc, cx + 6, cy - 1, NULL); LineTo(dc, cx, cy - 7);
            SelectObject(dc, op3);
            DeleteObject(ap);
        }

        // 右上角的 ×（一直显示：空白时、等待时、出答案后都在）
        {
            AiRect c = AiRClose();
            int cx0 = c.x + c.w / 2, cy0 = c.y + c.h / 2, d = 5;
            HPEN xp = CreatePen(PS_SOLID, 2, AiMix(AiSub(), ctlX));
            HGDIOBJ ox = SelectObject(dc, xp);
            MoveToEx(dc, cx0 - d, cy0 - d, NULL); LineTo(dc, cx0 + d + 1, cy0 + d + 1);
            MoveToEx(dc, cx0 + d, cy0 - d, NULL); LineTo(dc, cx0 - d, cy0 + d + 1);
            SelectObject(dc, ox);
            DeleteObject(xp);
        }

        // 答案区
        if (!aiA.empty() && AiAnsH() > 12)
        {
            HPEN lp = CreatePen(PS_SOLID, 1, AiLine());
            HGDIOBJ op = SelectObject(dc, lp);
            MoveToEx(dc, 16, AiAnsTop() - 8, NULL);
            LineTo(dc, aiW - 16, AiAnsTop() - 8);
            SelectObject(dc, op);
            DeleteObject(lp);

            RECT ar = { 16, AiAnsTop(), aiW - 16, AiAnsTop() + AiAnsH() };
            AiTextOut(dc, aiA.c_str(), ar, AiText(), ft, DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        }
        else if (aiBusy)
        {
            RECT br2 = { 16, 70, aiW - 16, 104 };
            AiTextOut(dc, L"思考中…", br2, AiSub(), fb, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }

        BitBlt(dc0, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bm);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)w;
        SetBkColor(dc, AiPanel());
        SetTextColor(dc, AiText());
        static HBRUSH hb = NULL;
        if (hb) DeleteObject(hb);
        hb = CreateSolidBrush(AiPanel());
        return (LRESULT)hb;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        AiRect a;
        a = AiRThink();
        if (pt.x >= a.x && pt.x < a.x + a.w && pt.y >= a.y && pt.y < a.y + a.h)
        { menuFor = 0; AiPopupMenu(); return 0; }
        a = AiRSend();
        {
            int dx = pt.x - (a.x + a.w / 2), dy = pt.y - (a.y + a.h / 2);
            if (dx * dx + dy * dy <= (a.w / 2) * (a.w / 2))
            {
                if (aiEdit && GetWindowTextLengthW(aiEdit) > 0) { /* 下一轮接 API */ }
                return 0;
            }
        }
        a = AiRClose();
        if (pt.x >= a.x - 4 && pt.x < a.x + a.w + 4 && pt.y >= a.y - 4 && pt.y < a.y + a.h + 4)
        { HideAiWindow(); return 0; }

        SetFocus(aiEdit);
        return 0;
    }

    case WM_TIMER:
        if (w == 7311)
        {
            // 不再等岛"收回去"：主窗口已经被藏起来，这里直接开始长开
            KillTimer(h, 7311);

            aiGrow = 0.0f;
            aiCtl = 0.0f;
            ctlE = ctlT = ctlS = ctlX = 0.0f;
            ctlFrm = 0;
            aiClosing = 0;
            keyShown = 0;
            // 起点必须**完全等于迷你岛**（150×48、y=2），否则放大时会有"跳一下"的错位
            aiW = NOTCH_WIDTH;
            aiH = NOTCH_HEIGHT;
            aiY = NOTCH_Y;
            if (aiEdit) ShowWindow(aiEdit, SW_HIDE);   // 内容先藏着，形状定型后再露
            // 关键：主窗口直接藏起来，而不是让它"收回去"。
            // 这样视觉上就是迷你岛原地长成 AI 岛，也不会有两个窗口叠着闪的问题。
            if (o) ShowWindow(o, SW_HIDE);
            AiRelayout();
            ShowWindow(h, SW_SHOW);
            aiT0 = NowMs();                // 动画起点（0.8 秒从这里开始算）
            SetTimer(h, 7301, 16, NULL);   // 动画/淡入/回收全靠它
            aiIdleAt = NowMs();
            SetTimer(h, 7301, 16, NULL);      // 现在才开始逐帧长高
            SetForegroundWindow(h);
            if (aiEdit) SetFocus(aiEdit);
            return 0;
        }
        if (w == 7301)
        {
            bool dirty = false;
            if (aiClosing)
            {
                // ---- 回收：反着缩回迷你岛的尺寸 ----
                double pr = 1.0 - (double)(NowMs() - aiT0) / 800.0;   // 0.8 秒倒放
                if (pr <= 0.0)
                {
                    aiGrow = 0.0f;
                    HideAiWindowNow();
                    return 0;
                }
                aiGrow = (float)pr;
                float e = AiEase(aiGrow);
                aiW = NOTCH_WIDTH  + (int)((AI_W - NOTCH_WIDTH) * e);
                aiH = NOTCH_HEIGHT + (int)((aiTargetH - NOTCH_HEIGHT) * e);
                aiY = NOTCH_Y + (int)((ISLAND_FINAL_Y - NOTCH_Y) * e);
                // 内容先消失，只剩下形状
                float fade = (aiGrow > 0.55f) ? (aiGrow - 0.55f) / 0.45f : 0.0f;
                ctlE = ctlT = ctlS = ctlX = fade;
                // 后 70% 把整块窗淡出，露出下面的迷你岛
                double ap = (aiGrow - 0.30) / 0.70;
                if (ap < 0.0) ap = 0.0;
                if (ap > 1.0) ap = 1.0;
                SetLayeredWindowAttributes(h, 0, (BYTE)(255 * ap), LWA_ALPHA);
                AiRelayout();
                dirty = true;
            }
            else if (aiGrow < 1.0f)
            {
                // 小岛变大岛那种：宽和高**同时**插值，视觉上整个形状"撑开"
                // 用真实时间算进度：定时器偶尔迟到也不影响节奏（原来按帧累加，掉帧就变慢）
                double pr = (double)(NowMs() - aiT0) / 800.0;   // 0.8 秒
                if (pr > 1.0) pr = 1.0;
                aiGrow = (float)pr;
                float e = AiEase(aiGrow);
                aiW = NOTCH_WIDTH  + (int)((AI_W - NOTCH_WIDTH) * e);
                aiH = NOTCH_HEIGHT + (int)((aiTargetH - NOTCH_HEIGHT) * e);
                aiY = NOTCH_Y + (int)((ISLAND_FINAL_Y - NOTCH_Y) * e);
                // 前 60% 就把不透明度推到 255：形状还小的时候已经"变成"AI 岛了，
                // 后半段纯粹是长大，看起来最像"迷你岛自己变大"
                double ap = pr / 0.60;
                if (ap > 1.0) ap = 1.0;
                SetLayeredWindowAttributes(h, 0, (BYTE)(255 * ap), LWA_ALPHA);
                // ⚠️ 这里以前写的是 ap>=1.0（也就是 48% 的时间就隐藏主窗口）——
                // 那时 AI 岛才 300 宽，迷你岛一藏，看到的就是"迷你岛没了、一个黑块在长"。
                // 必须等 AI 岛**完全盖满**（pr 到 1）再藏，中间迷你岛一直在下面垫着。
                if (pr >= 0.999 && o && IsWindowVisible(o)) ShowWindow(o, SW_HIDE);
                AiRelayout();
                dirty = true;
            }
            else
            {
                // 形状定型 → 四个控件按顺序错峰淡入：文本框 0 / 思考 250 / ↑ 500 / × 750
                if (aiCtl < 1.0f) { aiCtl = 1.0f; ctlFrm = 0; }
                ctlFrm++;
                int ms = (int)(NowMs() - aiT0 - 800);  // 形状定型后才开始算
                if (ms < 0) ms = 0;
                auto st = [&](int delay) -> float
                {
                    float v = (ms - delay) / 220.0f;  // 单个淡入 220ms，节奏更紧凑
                    return v <= 0.0f ? 0.0f : (v >= 1.0f ? 1.0f : v);
                };
                ctlE = st(0); ctlT = st(150); ctlS = st(300); ctlX = st(450);
                if (ctlE >= 1.0f && aiEdit && !IsWindowVisible(aiEdit)) ShowWindow(aiEdit, SW_SHOW);
                // AI 岛完全弹完、控件也出齐了，才轮到 Key 窗
                if (ctlX >= 1.0f && !keyShown)
                {
                    keyShown = 1;
                    // 2026-10-28 v1.2：首次弹 Key 窗的功能删了 —— 输入入口统一到「设置 → AI 岛」。
                    // 没配 Key 就给一句提示，用户自己去设置里填。
                    if (!AiHasKey())
                        ShowBubbleIsland(L"还没配 API Key：设置 → AI 岛 里填一下");
                }
                dirty = true;
            }
            int want = 0;
            if (aiEdit) want = (GetWindowTextLengthW(aiEdit) > 0) ? 255 : 0;
            if (sendMix != want)
            {
                int d = want - sendMix;
                int st = d / 3; if (st == 0) st = (d > 0 ? 1 : -1);
                sendMix += st;
                if (abs(want - sendMix) <= 3) sendMix = want;
                dirty = true;
            }
            if (dirty) InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { HideAiWindow(); return 0; }
        break;
    case WM_CLOSE:
        HideAiWindow();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void AiRelayout()
{
    if (!aiWnd) return;
    int x = (B - aiW) / 2;
    if (x < 0) x = 0;
    // y 和大岛一致（ISLAND_FINAL_Y）。注意这里不带 SWP_SHOWWINDOW —— 显示时机由 7311 控制
    SetWindowPos(aiWnd, HWND_TOPMOST, x, aiY, aiW, aiH, SWP_NOACTIVATE);
    // SetWindowRgn 是这轮卡顿的元凶：每帧重建 + 重算裁剪区域，60fps 下很吃 CPU。
    // 圆角半径固定，尺寸变化 3px 以内肉眼看不出，所以每 3 帧才更新一次。
    static int rgnW = -1, rgnH = -1, rgnSkip = 0;
    if (++rgnSkip >= 3 || rgnW < 0 || abs(aiW - rgnW) > 6 || abs(aiH - rgnH) > 6)
    {
        rgnSkip = 0;
        rgnW = aiW; rgnH = aiH;
        HRGN rg = CreateRoundRectRgn(0, 0, aiW + 1, aiH + 1, 26, 26);
        SetWindowRgn(aiWnd, rg, FALSE);   // FALSE：不立刻擦背景，避免逐帧闪
    }
    if (aiEdit)
    {
        AiRect e = AiREdit();
        SetWindowPos(aiEdit, NULL, e.x + 10, e.y + 3, e.w - 20, e.h - 6, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// ---------- API Key 窗（岛样式，从 AI 岛底部往下长出来）----------
static HWND kwWnd = NULL, kwEdit = NULL;
static int  kwH = 0, kwTargetH = 132;
static float kwGrow = 1.0f;

// 历史记录读写（注册表 AiHist0..AiHist9，最新的在 0）
static void AiHistSave();
static void AiHistLoad()
{
    aiHist.clear();
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return;
    for (int i = 0; i < 10; ++i)
    {
        wchar_t nm[16];
        swprintf(nm, L"AiHist%d", i);
        wchar_t buf[512] = { 0 };
        DWORD sz = sizeof(buf) - 2;
        if (RegQueryValueExW(k, nm, NULL, NULL, (LPBYTE)buf, &sz) == ERROR_SUCCESS && buf[0])
            aiHist.push_back(buf);
    }
    RegCloseKey(k);
}
static void AiHistSave()
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0,
                        KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    for (int i = 0; i < 10; ++i)
    {
        wchar_t nm[16];
        swprintf(nm, L"AiHist%d", i);
        if (i < (int)aiHist.size())
            RegSetValueExW(k, nm, 0, REG_SZ, (const BYTE*)aiHist[i].c_str(),
                           (DWORD)((aiHist[i].size() + 1) * sizeof(wchar_t)));
        else
            RegDeleteValueW(k, nm);
    }
    RegCloseKey(k);
}
// 加一条历史（去重 + 最多 10 条 + 最新在前）。存的是「问<US>答」
// 用 \x01 当分隔符，因为 Windows 注册表字符串里不能放 \0
static void AiHistPush(const std::wstring& q, const std::wstring& a)
{
    if (q.empty()) return;
    std::wstring rec = q + L'\x01' + a;
    for (size_t i = 0; i < aiHist.size(); ++i)
        if (aiHist[i] == rec) { aiHist.erase(aiHist.begin() + i); break; }
    aiHist.insert(aiHist.begin(), rec);
    while (aiHist.size() > 10) aiHist.pop_back();
    AiHistSave();
}
// 拆分「问<US>答」
static void AiHistSplit(const std::wstring& rec, std::wstring& q, std::wstring& a)
{
    size_t p2 = rec.find(L'\x01');
    if (p2 == std::wstring::npos) { q = rec; a.clear(); return; }
    q = rec.substr(0, p2);
    a = rec.substr(p2 + 1);
}

static bool AiLoadKey(std::wstring& out);   // 实现在下面

static bool AiHasKey()
{
    std::wstring tmp;
    return AiLoadKey(tmp);
}

// 读 Key：优先 DPAPI 解密，遇到旧的明文（REG_SZ）也认
static bool AiLoadKey(std::wstring& out)
{
    out.clear();
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;

    DWORD type = 0, sz = 0;
    if (RegQueryValueExW(k, L"AiKey", NULL, &type, NULL, &sz) != ERROR_SUCCESS || sz == 0)
    {
        RegCloseKey(k);
        return false;
    }
    std::vector<BYTE> buf(sz + 2, 0);
    if (RegQueryValueExW(k, L"AiKey", NULL, &type, buf.data(), &sz) != ERROR_SUCCESS)
    {
        RegCloseKey(k);
        return false;
    }
    RegCloseKey(k);

    if (type == REG_BINARY && sz > 8)
    {
        DATA_BLOB in = { sz, buf.data() };
        DATA_BLOB outb = { 0, NULL };
        if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &outb))
            return false;
        out.assign((const wchar_t*)outb.pbData);
        LocalFree(outb.pbData);
    }
    else
    {
        out.assign((const wchar_t*)buf.data());
    }
    return !out.empty();
}

// 存 Key：DPAPI 加密后写 REG_BINARY（同一用户 + 同一台机器才能解）
static void AiSaveKey(const wchar_t* v)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\IslandAlarm", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;

    DATA_BLOB in = { (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)), (BYTE*)v };
    DATA_BLOB outb = { 0, NULL };
    if (CryptProtectData(&in, L"mobile-ai-key", NULL, NULL, NULL,
                         CRYPTPROTECT_UI_FORBIDDEN, &outb))
    {
        RegSetValueExW(k, L"AiKey", 0, REG_BINARY, outb.pbData, outb.cbData);
        LocalFree(outb.pbData);
    }
    RegCloseKey(k);
}

static AiRect KwREdit()  { return { 16, 56, 496, 36 }; }   // 宽 640：和 AI 岛同宽
static AiRect KwRSave()  { return { 528, 56, 96, 36 }; }

static LRESULT CALLBACK KwProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc0 = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(dc0);
        HBITMAP bm = CreateCompatibleBitmap(dc0, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(dc, bm);

        HBRUSH bg = CreateSolidBrush(AiBg());
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        HFONT f1 = GetCachedFont(16, true), f2 = GetCachedFont(14, false);
        RECT tr = { 16, 16, rc.right - 16, 44 };
        AiTextOut(dc, L"首次使用：填入 DeepSeek API Key（存在本机，之后不再询问）",
                  tr, AiText(), f1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        AiRect e = KwREdit();
        AiRoundRect(dc, e.x, e.y, e.w, e.h, 12, AiPanel(), AiLine());

        AiRect b = KwRSave();
        AiRoundRect(dc, b.x, b.y, b.w, b.h, b.h / 2, RGB(52, 120, 246), RGB(52, 120, 246));
        RECT br = { b.x, b.y, b.x + b.w, b.y + b.h };
        AiTextOut(dc, L"保存", br, RGB(255, 255, 255), f1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(dc0, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, ob);
        DeleteObject(bm);
        DeleteDC(dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)w;
        SetBkColor(dc, AiPanel());
        SetTextColor(dc, AiText());
        static HBRUSH hb = NULL;
        if (hb) DeleteObject(hb);
        hb = CreateSolidBrush(AiPanel());
        return (LRESULT)hb;
    }

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        AiRect b = KwRSave();
        if (pt.x >= b.x && pt.x < b.x + b.w && pt.y >= b.y && pt.y < b.y + b.h)
        {
            wchar_t buf[512] = { 0 };
            if (kwEdit) GetWindowTextW(kwEdit, buf, 511);
            wchar_t* a = buf;
            while (*a == L' ') a++;
            size_t L = wcslen(a);
            while (L > 0 && (a[L-1] == L' ' || a[L-1] == L'\r' || a[L-1] == L'\n')) a[--L] = 0;
            if (*a) AiSaveKey(a);
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        if (kwEdit) SetFocus(kwEdit);
        return 0;
    }

    case WM_TIMER:
        if (w == 7312)
        {
            bool dirty = false;
            // ===== Key 窗"被迷你岛吞掉"：AI 岛缩完之后，自己缩小 + 往迷你岛位置移 + 淡出 =====
            if (kwClosing && kwWnd)
            {
                LONGLONG el2 = NowMs() - kwcT0;
                if (el2 < 760) return 0;                    // 先等 AI 岛缩完
                double t2 = (double)(el2 - 760) / 620.0;
                if (t2 >= 1.0) { kwClosing = 0; HideKeyWindowNow(); return 0; }
                int w2 = (int)(AI_W + (NOTCH_WIDTH - AI_W) * t2);
                int h2 = (int)(kwTargetH + (NOTCH_HEIGHT - kwTargetH) * t2);
                int x2 = (B - w2) / 2;
                int fromY2 = ISLAND_FINAL_Y + aiH + AI_GAP, toY2 = NOTCH_Y;
                int y2 = (int)(fromY2 + (toY2 - fromY2) * t2);
                SetWindowPos(kwWnd, HWND_TOPMOST, x2, y2, w2, h2, SWP_NOACTIVATE);
                HRGN rg2 = CreateRoundRectRgn(0, 0, w2 + 1, h2 + 1,
                                              (int)(26 * (1.0 - t2)) + 8, (int)(26 * (1.0 - t2)) + 8);
                SetWindowRgn(kwWnd, rg2, FALSE);
                SetLayeredWindowAttributes(kwWnd, 0, (BYTE)(255 * (1.0 - t2)), LWA_ALPHA);
                return 0;
            }
            if (aiClosing && kwWnd && IsWindowVisible(kwWnd))
            {
                // AI 岛在收，Key 窗也跟着滑回 AI 岛底下，而不是"啪"一下没了
                double pr = (double)(NowMs() - kwT0) / 800.0;
                float kp = 1.0f - (float)pr;
                SetLayeredWindowAttributes(kwWnd, 0, (BYTE)(255 * (kp > 0 ? kp : 0)), LWA_ALPHA);
                if (kp <= 0.0f) { ShowWindow(kwWnd, SW_HIDE); }
                else
                {
                    int fromY = ISLAND_FINAL_Y + AI_H - kwTargetH;
                    int toY   = ISLAND_FINAL_Y + AI_H + AI_GAP;
                    int y2 = fromY + (int)((toY - fromY) * AiEase(kp));
                    int x2 = (B - AI_W) / 2;
                    if (x2 < 0) x2 = 0;
                    SetWindowPos(kwWnd, HWND_TOPMOST, x2, y2, AI_W, kwTargetH, SWP_NOACTIVATE);
                }
                return 0;
            }
            if (kwGrow < 1.0f)
            {
                // 从 AI 岛底部中间 1/3 冒出来 → 长大到 640×132，末尾带果冻过冲
                double pr = (double)(NowMs() - kwT0) / 620.0;
                if (pr > 1.0) pr = 1.0;
                kwGrow = (float)pr;
                float je = JellyEase(kwGrow);
                int w2 = kwFromW + (int)((AI_W - kwFromW) * je);
                int h2 = kwFromH + (int)((kwTargetH - kwFromH) * je);
                int y2 = kwFromY + (int)(AI_GAP * je);       // 往下让 8px，落到最终位置
                if (w2 < 20) w2 = 20;
                if (h2 < 12) h2 = 12;
                int x2 = (B - w2) / 2;
                if (x2 < 0) x2 = 0;
                SetWindowPos(h, HWND_TOPMOST, x2, y2, w2, h2, SWP_NOACTIVATE);
                HRGN rg = CreateRoundRectRgn(0, 0, w2 + 1, h2 + 1, 22, 22);
                SetWindowRgn(h, rg, FALSE);
                // 前 45% 渐显到 255（形状还小就已经"是"那块窗口了）
                double ap = pr / 0.45;
                if (ap > 1.0) ap = 1.0;
                SetLayeredWindowAttributes(h, 0, (BYTE)(255 * ap), LWA_ALPHA);
                dirty = true;
            }
            if (dirty) InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { ShowWindow(h, SW_HIDE); return 0; }
        break;

    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

bool KwVisible()
{
    return kwWnd && IsWindowVisible(kwWnd) != 0;
}

static int kwAlpha = 255;

// 真正的渐隐：逐帧降 alpha，到 0 才隐藏（之前直接 ShowWindow(SW_HIDE)，就是"啪"一下没了）
void HideKeyWindow()
{
    if (!kwWnd || !IsWindowVisible(kwWnd)) return;
    kwAlpha = 255;
    SetTimer(o, 15, 16, NULL);
}

void HideKeyWindowNow()
{
    if (kwWnd) ShowWindow(kwWnd, SW_HIDE);
    kwAlpha = 255;
    KillTimer(o, 15);
}

void ShowKeyWindow()
{
    static bool reg = false;
    if (!reg)
    {
        reg = true;
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = KwProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"MobileKeyWnd";
        RegisterClassExW(&wc);
    }
    if (!kwWnd)
    {
        kwWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                L"MobileKeyWnd", L"API Key", WS_POPUP | WS_CLIPCHILDREN,
                                0, 0, AI_W, 8, NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (!kwWnd) return;
        HRGN rg = CreateRoundRectRgn(0, 0, AI_W + 1, 9, 26, 26);
        SetWindowRgn(kwWnd, rg, TRUE);
        AiRect e = KwREdit();
        kwEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 e.x + 10, e.y + 3, e.w - 20, e.h - 6, kwWnd,
                                 (HMENU)(UINT_PTR)9200, GetModuleHandleW(NULL), NULL);
        if (kwEdit) SendMessageW(kwEdit, WM_SETFONT, (WPARAM)GetCachedFont(14, false), TRUE);
        SetTimer(kwWnd, 7312, 16, NULL);
    }

    // 起始位置：居中（用起始宽算）
    int x = (B - kwFromW) / 2;
    if (x < 0) x = 0;
    // 起点：AI 岛宽度 1/3、高 20，贴在 AI 岛底边中点 —— 从这里"冒出来"
    kwFromW = AI_W / 3;
    kwFromH = 20;
    kwFromY = ISLAND_FINAL_Y + aiH;
    int y = kwFromY;
    kwGrow = 0.0f;
    kwT0 = NowMs();
    // region 跟着起始尺寸走（原来固定成 640×132，会盖住小窗口的实际范围）
    HRGN rg0 = CreateRoundRectRgn(0, 0, kwFromW + 1, kwFromH + 1, 22, 22);
    SetWindowRgn(kwWnd, rg0, FALSE);
    SetWindowPos(kwWnd, HWND_TOPMOST, x, y, kwFromW, kwFromH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetLayeredWindowAttributes(kwWnd, 0, 0, LWA_ALPHA);   // 从全透明开始
    InvalidateRect(kwWnd, NULL, TRUE);
    if (kwEdit) SetFocus(kwEdit);
}

void ShowAiWindow()
{
    static bool reg = false;
    if (!reg)
    {
        reg = true;
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = AiProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"MobileAiWnd";
        RegisterClassExW(&wc);
    }
    if (!aiWnd)
    {
        // WS_CLIPCHILDREN 必须加：否则双缓冲 BitBlt 会把 EDIT 一起盖掉（整块贴图越过了子控件）
        aiWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                L"MobileAiWnd", L"AI", WS_POPUP | WS_CLIPCHILDREN,
                                0, 0, aiW, aiH, NULL, NULL,
                                GetModuleHandleW(NULL), NULL);
        if (!aiWnd) return;
        gAiWnd = aiWnd;
        HRGN rgn = CreateRoundRectRgn(0, 0, aiW + 1, aiH + 1, 26, 26);
        SetWindowRgn(aiWnd, rgn, TRUE);

        AiRect e = AiREdit();
        aiEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 e.x + 10, e.y + 3, e.w - 20, e.h - 6, aiWnd,
                                 (HMENU)(UINT_PTR)9100, GetModuleHandleW(NULL), NULL);
        if (aiEdit) SendMessageW(aiEdit, WM_SETFONT, (WPARAM)GetCachedFont(15, false), TRUE);
        // 7301 不在这里启动：要等迷你岛收完、窗口真正开始显示时再启动（见 7311）
    }

    AiRecalc();
    aiH = 48;
    aiGrow = 0.0f;
    aiShiftY = ISLAND_FINAL_Y + AI_H + AI_GAP;    // 其他岛要让过 AI 岛 + 它头上的空隙
    aiShown = true;
    HideNotch();                                    // 迷你岛先收（它自己有收回动画）
    // 隐藏着等：7311 会盯着 J，岛一空就显示并开始长高
    ShowWindow(aiWnd, SW_HIDE);
    SetTimer(aiWnd, 7311, 50, NULL);
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
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
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
    stw = CreateWindowExW(0, L"MobileSetWnd", L"mobile 设置", st,
                          x, y, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!stw) return;


    // ===== 左侧导航 + 右侧内容（分页）=====
    static const wchar_t* kNav[SET_PAGES] = { L"作息表", L"时间提醒", L"AI 岛", L"主题", L"迷你岛", L"更多" };

    gSetPage = -1;   // 导航和状态栏不属于任何页
    SetCtl(L"STATIC", L"mobile 设置", SS_LEFT, 18, 14, 150, 26, -1);
    for (int i2 = 0; i2 < SET_PAGES; i2++)
        sNav[i2] = SetCtl(L"BUTTON", kNav[i2], BS_PUSHBUTTON,
                          12, 54 + i2 * 44, NAV_W - 24, 38, ID_NAV_BASE + i2);
    sStatus = SetCtl(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 12, CH - 96, NAV_W - 24, 84, -1);

    // ---------- 页 0：作息表 ----------
    gSetPage = 0;
    SetCtl(L"STATIC", L"选择一套作息", SS_LEFT, CX, 18, 320, 24, -1);
    sTbl[0] = SetCtl(L"BUTTON", L"初四夏季作息", BS_AUTORADIOBUTTON | WS_GROUP, CX + 8, 62, 320, 26, ID_TBL_OLD);
    sTbl[1] = SetCtl(L"BUTTON", L"冬季作息",     BS_AUTORADIOBUTTON,            CX + 8, 100, 320, 26, ID_TBL_WINTER);
    sTbl[2] = SetCtl(L"BUTTON", L"初四冬季作息", BS_AUTORADIOBUTTON,            CX + 8, 138, 320, 26, ID_TBL_W4);
    SetCtl(L"STATIC", L"换作息后，倒计时和铃声都按新的时间表走。", SS_LEFT, CX, 186, 600, 24, -1);

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

    // ---------- 页 2：AI 岛 ----------
    gSetPage = 2;
    SetCtl(L"STATIC", L"AI 岛", SS_LEFT, CX, 18, 320, 24, -1);
    SetCtl(L"STATIC", L"界面上不回显已保存的 Key —— 想换就直接粘一个新的。", SS_LEFT, CX, 56, 700, 22, -1);

    SetCtl(L"STATIC", L"API key", SS_LEFT, CX, 118, 80, 24, -1);
    {
        HWND he = SetCtl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, CX + 88, 112, 420, 28, ID_AI_KEY_EDIT);
        SendMessageW(he, EM_SETLIMITTEXT, 512, 0);
    }
    SetCtl(L"BUTTON", L"粘贴", BS_PUSHBUTTON, CX + 520, 112, 76, 28, ID_AI_KEY_PASTE);
    SetCtl(L"BUTTON", L"保存", BS_PUSHBUTTON, CX + 88, 156, 100, 32, ID_AI_KEY_SAVE);
    SetCtl(L"BUTTON", L"删除已保存的 Key", BS_PUSHBUTTON, CX + 200, 156, 200, 32, ID_AI_KEY_DEL);
    sAiKeyState = SetCtl(L"STATIC", L"", SS_LEFT, CX, 206, 700, 24, -1);

    // ---------- 页 3：主题 ----------
    gSetPage = 3;
    SetCtl(L"STATIC", L"主题", SS_LEFT, CX, 18, 320, 24, -1);
    sTheme[0] = SetCtl(L"BUTTON", L"浅色", BS_AUTORADIOBUTTON | WS_GROUP, CX, 56, 100, 26, ID_TRAY_THEME_LIGHT);
    sTheme[1] = SetCtl(L"BUTTON", L"深色", BS_AUTORADIOBUTTON,            CX + 110, 56, 100, 26, ID_TRAY_THEME_DARK);
    sHoliday  = SetCtl(L"BUTTON", L"特殊节日提醒", BS_AUTOCHECKBOX,       CX, 94, 240, 26, ID_TRAY_HOLIDAY);

    SetCtl(L"STATIC", L"天空亮度", SS_LEFT, CX, 138, 70, 20, -1);
    sSkyBr    = SetCtl(L"msctls_trackbar32", L"", TBS_HORZ | TBS_NOTICKS, CX + 78, 130, 240, 30, -1);
    sSkyBrPct = SetCtl(L"STATIC", L"0%", SS_LEFT, CX + 324, 138, 44, 20, -1);

    SetCtl(L"STATIC", L"天空动画", SS_LEFT, CX, 174, 70, 20, -1);
    sSkyRe = SetCtl(L"BUTTON", L"每次重播一遍", BS_AUTORADIOBUTTON | WS_GROUP, CX + 78, 170, 130, 24, ID_SET_SKY_ANIM_RE);
    sSkyGo = SetCtl(L"BUTTON", L"直接接着变",   BS_AUTORADIOBUTTON,            CX + 212, 170, 130, 24, ID_SET_SKY_ANIM_GO);

    sSkyOn = SetCtl(L"BUTTON", L"天空背景（关闭＝纯色底板）", BS_AUTOCHECKBOX, CX, 208, 360, 26, ID_TRAY_SKY);

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
    SetCtl(L"STATIC", L"提示：退出整个程序请用托盘图标右键菜单。", SS_LEFT, CX, 152, 500, 24, -1);

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

    PageApply(0);
    SetSyncControls();
    SetTimer(stw, 1, 1000, NULL);      // 状态栏每秒刷新
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

// AI 岛要收的时候调用：把最下面那条消息"升级"成主岛大岛，接着显示满 5 秒。
// 返回 true = 状态机已经被接管（J=5 变身成大岛），调用方别再走"缩回迷你岛"那套。
// 为什么不能留着消息岛自己飘：AI 岛一收，它下面就没有锚点了，
// 会变成一个孤儿挂在迷你岛下面，5 秒后才自己缩掉（这就是以前的 bug）。
static bool BubblePromoteToMain()
{
    if (gBubN == 0) return false;

    // 取最新的那条（最靠下 = 最后进来的），其余的丢掉
    std::wstring keep;
    for (int i = gBubN - 1; i >= 0; i--)
    {
        if (!gBubs[i].showing) continue;
        keep = gBubs[i].text;
        break;
    }
    BubClearAll();
    if (keep.empty()) return false;

    KillTimer(o, 1); KillTimer(o, 2); KillTimer(o, 3); KillTimer(o, 4);

    hh = true;
    wcscpy_s(gg, keep.c_str());
    vv = 0.0f; af = false; ww = 0;      // 大岛字是 vv=0
    skyAlarm = false;                    // 消息类一律不套天空（和 ShowMessageIsland 保持一致）
    sf = 0.0f; sf0 = 0.0f; sf1 = 0.0f;   // 大岛要四角圆角，别留着迷你岛的吸顶直角
    FitBigWidth(NULL, gg, NULL, 0);
    L = true;

    bigH = ISLAND_HEIGHT;
    ComputeCanvasSize();

    // 从 AI 岛当前尺寸（640×360）直接缩成大岛，走通用的缩小变身（J=5 → gJ=3）
    ae = F; an = G; ao = E;
    gw = g; gh = ISLAND_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 3;
    vv0 = 0.0f; vt = 0.0f; oo = 0;
    nt = false;
    J = 5;
    S = NowMs() + DISPLAY_DURATION_MS;
    R = NowMs();
    return true;
}

void ShowMessageIsland(const wchar_t* text, int extraW)
{
    if (aiMode)                  // AI 岛在最上面：消息岛从它下方冒出来，并排显示
    {
        ShowBubbleIsland(text);
        return;
    }
    BubClearAll();          // 主岛要变回普通大岛：AI 岛下面的消息岛残留全清掉，别两套叠着
    KillTimer(o,1); KillTimer(o,2); KillTimer(o,3); KillTimer(o,4);
    if (J == 11 || J == 12) HideMiniIsland();   // 迷你岛挂在顶部，会盖住弹出来的大岛
    hh = true;
    wcscpy_s(gg, text);
    vv = 0.0f; af = false; ww = 0;
    skyAlarm = false;                      // 消息类一律不套天空（用户只允许上课/下课/倒计时三处）
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
    skyT0 = NowMs();                     // 天空动画从这里开始演"日出 → 现在"
    skyClear = false;                    // 消息岛是"从无到有"，没有夜色要收
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
        bigH = ISLAND_HEIGHT; g = NOTCH_WIDTH; ComputeCanvasSize();
        oo = 0; vv0 = 1.0f; vt = 1.0f; sf0 = sf; sf1 = 0.0f;   // 两端都是圆角，形状不用变
        J = 5;
        PrepareBackdropBeforeShow();
        R = NowMs();
        return;
    }
    nt = true; nv = 255; na = NowMs();
    F = NOTCH_WIDTH; G = NOTCH_HEIGHT; E = NOTCH_Y; D = (B - F) / 2;
    O = 0; vv = 1.0f; ai = -1;
    // 必须把 g / bigH 也拉回迷你岛那一档并重算画布 ——
    // 否则从 AI 岛（g=640）过来时画布还是 656 宽，迷你岛窗口会有 500 多像素的空区（回归过）
    bigH = ISLAND_HEIGHT; g = NOTCH_WIDTH; ComputeCanvasSize();
    J = 11;                                   // 从屏幕边缘上方"落"下来
    PrepareBackdropBeforeShow();
    R = NowMs();
}
void HideNotch()
{
    if (J == 11 || J == 12) { J = 13; P = 0; R = NowMs(); }
}
// 迷你岛常驻期间的心跳：到秒就重绘（分钟跳变），静止够久就把整块降成半透明
// 外部音乐检测：有歌就开始显示播放器岛，停了就收回。
// 只在"没在放《回家》"时管，免得和放学那段抢。
void NotchHoldTick()
{
    hh = false;          // 兜底：迷你岛只显示时间
    int nowSec = (int)(NowMs() / 1000);
    if (nowSec != ai) { ai = nowSec; SampleNotchLum(); }   // 顺带看一眼背景亮不亮
    int dimA = 255 * notchA / 100;                       // 用户可调（设置里那条"静止后透明度"）
    int target = (NowMs() - na > NOTCH_DIM_MS) ? dimA : 255;
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
static int BellVolume(AlarmType type)
{
    if (!fuck) return -1;
    int i = CurBellPeriod(type);
    if (i < 0 || i >= 11) return (type == ALARM_TYPE_CLASS_END) ? FUCK : ab;   // 定位不到用全局
    bool on = (type == ALARM_TYPE_CLASS_START) ? bellUp[i] : bellDn[i];
    if (!on) return -1;
    return (type == ALARM_TYPE_CLASS_START) ? bellUpVol[i] : bellDnVol[i];
}

void TriggerAlarmByTimer(AlarmType type)
{
    if (gSelfTest) return;      // 自检跑着的时候别让真实闹钟插进来（会把播放器那步顶掉）
    SYSTEMTIME st; GetLocalTime(&st);
    int curMin = st.wHour * 60 + st.wMinute;
    if (N == curMin) return;      // 本分钟已经弹过了，吞掉
    N = curMin;
    // 2026-10-13 用户改回去了：放学（今天最后一节课下课）**照常敲下课铃**，
    // 《回家》挪到固定 19:30 单独播（见 case 10 里的 BGM_HOUR/BGM_MIN）。
    int bv = BellVolume(type);
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
    KillTimer(o, 14);
    TsButton(false);
}

void SelfTestStart()
{
    if (tsStep >= 0) return;            // 已经在跑就别重复触发
    tsStep = 0;
    tsWait = 0;
    gSelfTest = true;
    TsButton(true);
    SetTimer(o, 14, 100, NULL);
}

void SelfTestTick()
{
    if (tsStep < 0) return;
    if (tsNext != 0 && NowMs() < tsNext) return;
    tsNext = 0;

    // 自检期间 AI 岛别自己收（它定格 5 秒就 S 到期），不然测不到淡出那一步
    if (tsStep >= 9 && tsStep <= 13 && aiMode) S = NowMs() + 10000;

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
    case 9:  ShowAiIsland();                         tsNext = NowMs() + 25 * 100; break;   // AI 岛：大岛原样下移
    case 10: ShowBubbleIsland(L"测试 · 第 1 条消息"); tsNext = NowMs() + 10 * 100; break;   // 消息岛 1
    case 11: ShowBubbleIsland(L"测试 · 第 2 条消息"); tsNext = NowMs() + 10 * 100; break;   // 消息岛 2
    case 12: ShowBubbleIsland(L"测试 · 第 3 条消息"); tsNext = NowMs() + 22 * 100; break;   // 三条堆叠，停 2 秒
    case 13: CloseAiIsland();                        tsNext = NowMs() + 25 * 100; break;   // 淡出 + 原岛滑回
    case 14:
        // 直接定格成播放器，不走 J=9 变身 —— 自检要的是"看得见"，动画路线上变量太多
        // ⚠️ 先把"原岛滑回中"这个状态清干净：belowRestore=1 时
        // UpdateIslandDisplay 画的是 gBelow（原大岛），gA（播放器）根本轮不到画。
        belowRestore = false; restoreOff = 0; belowOn = false;
        ply = true; plyA = 255.0f; plyMix = false; ta = 255; ww = 255;
        plyHintUntil = NowMs() + 8000;
        CoverLoad();
        bigH = PLAYER_H; g = PLAYER_W;
        ComputeCanvasSize();
        F = PLAYER_W; G = PLAYER_H; E = ISLAND_FINAL_Y; D = (B - F) / 2;
        J = 3; S = NowMs() + PLAYER_LONG;
        BgmPlay();
        UpdateIslandDisplay(o, 1.0f, 255);
        tsNext = NowMs() + 62 * 100; break;
    case 15: ClosePlayer();                          tsNext = NowMs() + 6 * 100;  break;   // 收回
    default: SelfTestEnd(); return;
    }
    tsStep++;
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
    if (aiMode) return;          // AI 岛开着时不要让闹钟把画面顶掉（声音已经在 AudioPlayAsync 里播了）
    // 闹钟类一律配天空 —— 包括「立即测试」：它就是"预览真实闹钟长什么样"的入口，
    // 不给的话测试时看到的是纯色底板，和实际弹出来的效果对不上
    //（用户 2026-10-16 看冒烟时说"弹到一半动画消失、变成纯黑背景"就是这个）。
    // 不配天空的只有"消息类"（状态信息/下次提醒时间/开机自启动/隐私提示），它们在 ShowMessageIsland 里清掉。
    skyAlarm = true;
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
    // ⚠️ 天空动画的计时起点必须放在这个分叉**之前**：从迷你岛/小岛过来时走的是 J=9 那条路，
    // 只在 J=1 那条分支里设的话，动画永远不启动（天空会定格在"现在"那一帧 —— 踩过）。
    // 从"已经挂在屏幕上的小岛/迷你岛"变成大岛时：按用户设置决定是重播一遍动画，
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
    // ===== API 线程回来了 =====
    case WM_MOUSEWHEEL:
    {
        if (aiMode && J == 3)
        {
            int d = GET_WHEEL_DELTA_WPARAM(wParam);
            aiScroll -= d * 48 / WHEEL_DELTA;
            if (aiScroll < 0) aiScroll = 0;
            aiLastAct = NowMs();
            UpdateIslandDisplay(hWnd, 1.0f, 255);
        }
        return 0;
    }

    case WM_APP + 7:
    {
        int idx = (int)wParam;
        if (idx >= 0 && idx < (int)aiAs.size()) aiAs[idx] = aiAnswerSlot;
        // 答案回来了，把历史里那条的答补上（AiHistPush 会去重提到最前）
        if (idx >= 0 && idx < (int)aiQs.size()) AiHistPush(aiQs[idx], aiAnswerSlot);
        aiPending = -1;
        aiLastAct = NowMs();
        aiRebuild();
        UpdateIslandDisplay(hWnd, 1.0f, 255);
        return 0;
    }

    // ===== AI 岛输入：普通字符 / 输入法上屏 =====
    case WM_CHAR:
    {
        if (!aiMode || J != 3) break;
        aiLastAct = NowMs();          // 打字也算活动，别打着打着收回去
        wchar_t ch = (wchar_t)wParam;
        if (ch == 8)                                  // 退格
        {
            if (!aiInput.empty()) aiInput.pop_back();
        }
        else if (ch >= 32 && ch != 127)               // 可见字符
        {
            if (aiInput.size() < 400) aiInput.push_back(ch);
        }
        else break;
        UpdateIslandDisplay(hWnd, 1.0f, 255);
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (!aiMode || J != 3) break;
        if (wParam == VK_BACK && !aiInput.empty())
        {
            aiInput.pop_back();
            UpdateIslandDisplay(hWnd, 1.0f, 255);
            return 0;
        }
        if (wParam == VK_ESCAPE) { CloseAiIsland(); return 0; }
        break;
    }

    // ===== 输入法：把候选框定位到输入框下方 =====
    case WM_IME_STARTCOMPOSITION:
    {
        if (aiMode && J == 3)
        {
            HIMC imc = ImmGetContext(hWnd);
            if (imc)
            {
                COMPOSITIONFORM cf = { 0 };
                cf.dwStyle = CFS_FORCE_POSITION;
                cf.ptCurrentPos.x = D + 40;
                cf.ptCurrentPos.y = E + 60;
                ImmSetCompositionWindow(imc, &cf);
                ImmReleaseContext(hWnd, imc);
            }
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        POINT cp; GetCursorPos(&cp);

        // 点大岛（上课倒计时/上课了/下课了）→ 唤出 AI 岛，大岛让到它下面。
        // 用户定过：不再用"点大岛关闭"，关岛交给自动收回。
        if (!aiMode && !ply && (J == 1 || J == 3) && F > 4 && G > 4)
        {
            if (cp.x >= D && cp.x < D + F && cp.y >= E && cp.y < E + G)
            {
                // 不再复制一个"消息岛"替身：ShowAiIsland 会把当前这个岛原样存成 gBelow，
                // 由它自己滑到 AI 岛下面（样式一点不变）
                ShowAiIsland();
                return 0;
            }
        }

        // AI 岛：先抢焦点，不然 WM_CHAR / IME 都收不到
        if (aiMode && J == 3)
        {
            SetForegroundWindow(hWnd);
            SetFocus(hWnd);
        }
        // AI 岛底部那一排：输入框 / 思考 / ↑ / ×
        // ⚠️ 它们贴在岛**底部**，不是顶部 —— 之前按顶部算，所以全点不中。
        if (aiMode && J == 3)
        {
            float kk = (float)G / (float)(aiH > 0 ? aiH : AI_H);
            const AiLay H2 = AiCalc(D, E, F, G, kk);
            int rowTop = H2.rowTop;
            int rowBot = H2.rowBot;
            int pillX  = H2.pillX;
            int pillW  = H2.pillW;
            int testX  = H2.testX;
            int testD  = H2.testD;
            int sendX  = H2.sendX;
            int sendD  = H2.sendD;
            int closeX = H2.closeX;

            // × 放宽一点：右边缘往里 60px、上下留 8px 容差（太小了很难点中）
            if (cp.x >= closeX - 20 && cp.x <= D + F + 4 &&
                cp.y >= rowTop - 8 && cp.y <= rowBot + 8)
            {
                CloseAiIsland();
                return 0;
            }
            if (cp.x >= pillX && cp.x < pillX + pillW && cp.y >= rowTop && cp.y <= rowBot)
            {
                aiThink2 = (aiThink2 + 1) % 3;      // off → high → max
                aiLastAct = NowMs();
                UpdateIslandDisplay(hWnd, 1.0f, 255);
                return 0;
            }
            // 历史行：点一下把那条问句填进输入框（自己再点 ↑ 发出去）
            if (aiQs.empty() && aiH == AI_H_EMPTY && !aiHist.empty())
            {
                int hx0 = H2.histX;
                int hw0 = H2.histW;
                int hy0 = H2.histY;
                int rh0 = H2.histRowH;
                if (cp.x >= hx0 && cp.x <= hx0 + hw0)
                {
                    for (int ii = 0; ii < (int)aiHist.size() && ii < 3; ++ii)
                    {
                        int ry = hy0 + ii * rh0;
                        if (cp.y >= ry && cp.y <= ry + rh0 - (int)(5 * kk))
                        {
                            // 先别急着展开 —— 短按=展开，长按 520ms=删除。
                            // 判断交给 WM_TIMER(18) 和 WM_LBUTTONUP。
                            aiHistHit = ii;
                            aiHistDown = NowMs();
                            SetTimer(hWnd, 18, 520, NULL);
                            return 0;
                        }
                        if (false)
                        {
                            // 点一条历史 = 把那一轮对话整段恢复出来
                            std::wstring qh, ah;
                            AiHistSplit(aiHist[ii], qh, ah);
                            aiQs.clear(); aiAs.clear();
                            aiQs.push_back(qh);
                            aiAs.push_back(ah.empty() ? L"（未回答）" : ah);
                            aiScroll = 999999;
                            aiLastAct = NowMs();
                            aiRebuild();
                            if (aiH != AI_H)
                            {
                                aiH = AI_H;
                                bigH = aiH;
                                ComputeCanvasSize();
                                ae = F; an = G; ao = E;
                                sf0 = sf; sf1 = 0.0f; nt = false;
                                gw = AI_W; gh = aiH; gy = ISLAND_FINAL_Y; gJ = 3;
                                vv0 = vv; vt = 1.0f; oo = 0; J = 9;
                                R = NowMs();
                            }
                            else
                            {
                                EnsureBuffers();
                                UpdateIslandDisplay(hWnd, 1.0f, 255);
                            }
                            return 0;
                        }
                    }
                }
            }

            // 测：每点一次加一问一答
            if (cp.x >= testX && cp.x < testX + testD && cp.y >= rowTop && cp.y <= rowBot)
            {
                wchar_t tname[32];
                swprintf(tname, L"测试 #%d", aiHist.size() + 1);
                AiHistPush(tname, L"本地测试未调API");
                aiQs.push_back(tname);
                aiAs.push_back(L"本地测试未调API");
                aiScroll = 999999;
                aiLastAct = NowMs();
                aiRebuild();
                // 输入态的话要长成两段式，不然气泡区只有 64px 高，放不下
                if (aiH != AI_H)
                {
                    aiH = AI_H;
                    bigH = aiH;
                    ComputeCanvasSize();
                    ae = F; an = G; ao = E;
                    sf0 = sf; sf1 = 0.0f; nt = false;
                    gw = AI_W; gh = aiH; gy = ISLAND_FINAL_Y; gJ = 3;
                    vv0 = vv; vt = 1.0f; oo = 0; J = 9;
                    R = NowMs();
                }
                else
                {
                    EnsureBuffers();
                    UpdateIslandDisplay(hWnd, 1.0f, 255);
                }
                return 0;
            }
            if (cp.x >= sendX && cp.x < sendX + sendD && cp.y >= rowTop && cp.y <= rowBot)
            {
                if (!aiInput.empty())
                {
                    aiSend(hWnd);                    // 推进气泡 + 必要时长高
                }
                return 0;
            }
        }
        if (PlayerButtonHit(cp)) return 0;         // 点在播放/暂停键上 → 只切播放状态，别起长按
        if (!xx) { xx = true; yy = NowMs(); af = true; SetTimer(hWnd, 12, 100, NULL); }
        SetCapture(hWnd);
        return 0;
    }
    case WM_LBUTTONUP:
        xx = false; af = false;
        if (aiHistHit >= 0)                   // 松手在历史行上
        {
            KillTimer(hWnd, 18);
            if (!aiHistLongDone && aiHistHit < (int)aiHist.size())
            {
                // 短按 → 展开那一轮对话
                std::wstring qh, ah;
                AiHistSplit(aiHist[aiHistHit], qh, ah);
                aiQs.clear(); aiAs.clear();
                aiQs.push_back(qh);
                aiAs.push_back(ah.empty() ? L"（未回答）" : ah);
                aiScroll = 999999;
                aiLastAct = NowMs();
                aiRebuild();
                if (aiH != AI_H)
                {
                    aiH = AI_H;
                    bigH = aiH;
                    ComputeCanvasSize();
                    ae = F; an = G; ao = E;
                    sf0 = sf; sf1 = 0.0f; nt = false;
                    gw = AI_W; gh = aiH; gy = ISLAND_FINAL_Y; gJ = 3;
                    vv0 = vv; vt = 1.0f; oo = 0; J = 9;
                    R = NowMs();
                }
                else
                {
                    EnsureBuffers();
                    UpdateIslandDisplay(hWnd, 1.0f, 255);
                }
            }
            aiHistHit = -1;
            aiHistLongDone = false;
        }
        if (GetCapture() == hWnd) ReleaseCapture();
        // 短按（<400ms）→ 打开 AI 岛。
        // 迷你岛 J=12、课间小岛 J=6/7 都算；大岛在 WM_LBUTTONDOWN 里已经单独处理了。
        // 小岛原来短按是**什么都不做**（只有长按关闭），所以这里加不影响已有操作。
        if ((J == 12 || J == 6 || J == 7) && !aiMode && NowMs() - yy < 400) ShowAiIsland();
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
        case ID_TRAY_SKY:                      // 天空总开关：翻完当场就换底板
            skyEn = !skyEn;
            SaveSkyCfg();
            if (J == 3 || J == 6 || J == 7 || J == 11 || J == 12)
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
        case 14:
            SelfTestTick();          // 立即测试的推进（100ms）
            break;

        case 22:
        {
            KillTimer(o, 22);
            FILE* lf = fopen("D:/aab/l.log", "w");
            if (lf) { fprintf(lf, "BEFORE l=%d j=%d cc=%d V=%p X=%p\n", l, j, cc, (void*)V, (void*)X); fclose(lf); }
            l += 200;
            UpdateIslandDisplay(o, 1.0f, 255);
            lf = fopen("D:/aab/l.log", "a");
            if (lf) { fprintf(lf, "AFTER  l=%d j=%d cc=%d V=%p X=%p\n", l, j, cc, (void*)V, (void*)X); fclose(lf); }
            break;
        }

        case 23:
            KillTimer(o, 23);
            ShowMessageIsland(L"距上课 03:00");
            break;

        case 21:
            KillTimer(o, 21);
            {
                FILE* f = fopen("D:/aab/bub.log", "a");
                if (f) { fprintf(f, "CASE21 hit, aiMode=%d J=%d aiH=%d E=%d G=%d l=%d\n", aiMode?1:0, J, aiH, E, G, l); fclose(f); }
            }
            if (gBubN == 0) ShowBubbleIsland(L"测试消息：下课还有 5 分钟");   // 别覆盖真实消息
            break;

        case 31:                     // 测试多岛堆叠：再补两条（只在 -DTEST_MULTI）
            KillTimer(o, 31);
            ShowBubbleIsland(L"第二条：有人正在使用麦克风");
            ShowBubbleIsland(L"第三条：测试第三条消息");
            break;

        case 20:                     // 消息岛动画（16ms）
            BubbleTick();
            break;

        case 18:                     // 历史行长按检测（520ms）
            KillTimer(o, 18);
            // 不看 GetKeyState —— 模拟点击/某些环境下它读不到。
            // 只要 520ms 到了、抬手还没来（aiHistHit 还在），就当长按。
            if (aiHistHit >= 0 && aiHistHit < (int)aiHist.size())
            {
                aiHist.erase(aiHist.begin() + aiHistHit);
                AiHistSave();
                aiHistHit = -1;
                aiHistLongDone = true;          // 抬起时别再展开
                if (aiHist.empty()) aiH = 120;  // 最后一条删了，收回小输入态
                UpdateIslandDisplay(o, 1.0f, 255);
            }
            break;

        case 17:                     // AI 岛输入光标闪烁
            if (aiMode && J == 3) { aiCaret = !aiCaret; UpdateIslandDisplay(o, 1.0f, 255); }
            break;

        case 16:                     // v1.2：Key 窗已删，这里只把定时器收掉
            KillTimer(o, 16);
            break;

        case 15:                     // Key 窗渐隐（16ms 一帧）
            kwAlpha -= 26;
            if (kwAlpha <= 0) { HideKeyWindowNow(); }
            else if (kwWnd) SetLayeredWindowAttributes(kwWnd, 0, (BYTE)kwAlpha, LWA_ALPHA);
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
                    TriggerAlarmByTimer(alarm.type);
                    break;
                }
            }

            int nowMin = st.wHour * 60 + st.wMinute;

            SelfTestTick();       // 自测推进（100ms 定时器为主，这里只是兜底）
            // ⓪ 麦克风 / 摄像头被占用提示 —— 2026-10-22 用户决定**关闭**。
            // 原因：Windows 那份隐私记录只覆盖 UWP / 走系统相机栈的应用；
            //      希沃这类 Win32 + DirectShow 程序根本不写那个键，所以永远检测不到，
            //      反而在"别的软件正常用摄像头"时给出误导性的提示。与其误导，不如不要。
            //      （函数和图标都留在这儿，将来要做 DirectShow 正向探测时能接回来。）
            (void)0;

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
    timeBeginPeriod(1);          // 让 SetTimer 精度从 ~15.6ms 提到 1ms，60fps 才跑得满
    LoadTheme();
    LoadPerfCfg();
    LoadAudioCfg();
    LoadSkyCfg();
    LoadWxCfg();
    StartWxThread();
    StartAmp();          // 底部小球要的音频能量（只跟自己的《回家》）      // 天气后台刷新：拿不到就按"晴"画，绝不影响主流程
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
    SetTimer(o, 17, 530, NULL);   // AI 岛输入框光标闪烁
    AiHistLoad();                 // 读历史问句
#ifdef TEST_BUBBLE
    SetTimer(o, 23, 4000, NULL);  // 测试：4 秒后弹一个大岛（模拟上课倒计时）
#endif
    SetTimer(o, 20, 16, NULL);    // 消息岛动画


#ifdef TEST_L
    SetTimer(o, 22, 4000, NULL);
#endif

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
                GlobalsToState(gA);
                AnimTick(gA);
                R = NowMs() + gFrameMs;
            }
            LONGLONG remain = R - NowMs();
            wait = (remain > 0) ? (DWORD)remain : 0;
        }
        else if (J == 3)
        {
            LONGLONG now = NowMs();
            if (ply) PlayerTick();           // 播放器：把"长按关闭播放器"那行提示淡出去
            if (aiMode)
            {
                S = now + 60000;                              // 别让通用超时收它
                // 收的条件：真的闲着 + 输入框里没东西。
                // 只要 aiInput 有内容（字打到一半、或者还没发），就永远不收。
                // 30 秒也太急，放宽到 120 秒。
                if (aiInput.empty() && NowMs() - aiLastAct > 120000)
                    CloseAiIsland();
            }
            if (now >= S)
            {
                if (he)      { he = false; hs = 0; ta = 255; J = 10; }   // 特殊日期 → 接"两段式"，大岛形状不动
                else if (ply) { ShowPlayerIsland(); }                    // 放学音乐在放 → 大岛原地变播放器并常驻
                else if (bb) { hh = false; ae = F; an = G; ao = E; gw = MINI_WIDTH; gh = MINI_HEIGHT; gy = ISLAND_FINAL_Y; gJ = 7;
                               vv0 = vv; vt = 1.0f; oo = 0; sf0 = sf; sf1 = 0.0f; J = 5; }   // 有常驻小岛 → 直接变身
                else if (ns) {
                    // 自然回收也要清 AI 状态，否则 aiMode 残留 → 之后点迷你岛永远进不来（踩过）
                    bool aiWas = aiMode;
                    if (aiMode) { aiMode = false; HideKeyWindow(); }
                    // AI 岛下面还有消息 → 升级成主岛大岛继续显示满 5 秒，不缩迷你岛
                    if (!BubblePromoteToMain())
                    {
                        if (aiWas) { bigH = ISLAND_HEIGHT; g = NOTCH_WIDTH; ComputeCanvasSize(); }
                        hh = false; ae = F; an = G; ao = E; gw = NOTCH_WIDTH; gh = NOTCH_HEIGHT; gy = NOTCH_Y; gJ = 12;
                        vv0 = vv; vt = 1.0f; oo = 0; nt = true; sf0 = sf; sf1 = 0.0f; J = 5;
                    }
                }   // 迷你岛开着 → 直接缩成迷你岛
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
            // 天空里有"要动的东西"（雨雪在下、星星在闪）→ 岛显示期间就一直重绘，
            // 不能停在某一帧（用户明确："下雨在大岛的时候要一直播放，不是到某一帧就停下"）。
            // 播放器岛是长驻（10 分钟）不跟这个 25fps 跑，省 CPU。
            if (!ply && (SkyAnimating() || sky.rainy)) { UpdateIslandDisplay(o, 1.0f, 255); wait = 40; }
            // 播放器岛常驻 10 分钟，但底部音波要动 —— 60ms 一帧足够（缓慢律动），别按 25fps 烧 CPU
            else if (ply && bgmPlay && !bgmPause) { UpdateIslandDisplay(o, 1.0f, 255); wait = 16; }   // 60fps：40ms 看着像一跳一跳
        }
        else if (J == 7)
        {
            MiniHoldTick();          // 常驻小岛：倒计时刷新 / 长按判定 / 提示淡入淡出
            wait = 33;
        }
        else if (J == 12)
        {
        if (aiMode) { aiMode = false; bigH = ISLAND_HEIGHT; g = NOTCH_WIDTH; ComputeCanvasSize(); BubClearAll(); }
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
