// ============================================================================
//  mobile 自检工具（独立 exe，控制台）
//  用法：check.exe            —— 直接跑，控制台打印 + 同目录生成 TXT 报告
//        check.exe --keep     —— 跑完不自动关窗口（方便看）
//
//  设计原则：
//   · 只做"能确定"的检测：文件在不在、注册表值合法不合法、版本对不对。
//   · 运行时行为（动画/AI 岛）独立 exe 看不到，不瞎判 —— 那些归主程序内的自检。
//   · 每个检查点输出 Yes/No + 一行依据，失败的在 TXT 里给"可能原因 + 建议"。
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <ctime>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")

static const wchar_t* REGPATH = L"Software\\IslandAlarmTurbo";   // Turbo 独立系列，键分开
static FILE* g_rep = NULL;              // TXT 报告
static int   g_no = 0, g_total = 0;
static std::wstring g_reportPath;

struct Fail { std::wstring name, why, advice; };
static std::vector<Fail> g_fails;

static void NowStr(wchar_t* out, int n)
{
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf_s(out, n, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
}

// 统一输出：MinGW 的 wprintf 在 UTF-8 控制台会乱码，转成 UTF-8 字节用 printf 更稳
static void Out(const wchar_t* fmt, ...)
{
    wchar_t wbuf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(wbuf, _countof(wbuf), _TRUNCATE, fmt, ap);
    va_end(ap);
    char u8[6144];
    int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, u8, sizeof(u8), NULL, NULL);
    if (n > 1) fwrite(u8, 1, (size_t)(n - 1), stdout);
    fflush(stdout);
}


// 报告文件同样转 UTF-8 写，不用 fwprintf（MinGW 下会把中文写成单字节垃圾）
static void RepOut(const wchar_t* fmt, ...)
{
    if (!g_rep) return;
    wchar_t wbuf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(wbuf, _countof(wbuf), _TRUNCATE, fmt, ap);
    va_end(ap);
    char u8[6144];
    int n = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, u8, sizeof(u8), NULL, NULL);
    if (n > 1) fwrite(u8, 1, (size_t)(n - 1), g_rep);
}
// 一条检查：名字 / 结果 / 依据 / 失败原因 / 建议
static void Check(const wchar_t* name, bool ok, const wchar_t* detail,
                  const wchar_t* why = L"", const wchar_t* advice = L"")
{
    ++g_total;
    wchar_t ts[16]; NowStr(ts, 16);
    Out(L"[%s] #%02d %-34s %s", ts, g_total, name, ok ? L"Yes" : L"No");
    if (detail && detail[0]) Out(L"  (%s)", detail);
    Out(L"\n");
    if (g_rep)
        RepOut(L"[%s] #%02d %-34s %s%s%s%s\n", ts, g_total, name,
                 ok ? L"Yes" : L"No",
                 (detail && detail[0]) ? L"  (" : L"",
                 (detail && detail[0]) ? detail : L"",
                 (detail && detail[0]) ? L")" : L"");
    if (!ok)
    {
        ++g_no;
        g_fails.push_back({ name, why ? why : L"未知", advice ? advice : L"请检查该项配置" });
    }
    fflush(stdout);
}

static bool RegDword(HKEY k, const wchar_t* name, DWORD* out)
{
    DWORD v = 0, sz = sizeof(v), type = 0;
    if (RegQueryValueExW(k, name, NULL, &type, (LPBYTE)&v, &sz) != ERROR_SUCCESS) return false;
    *out = v;
    return true;
}

static bool RegHas(HKEY k, const wchar_t* name)
{
    DWORD sz = 0;
    return RegQueryValueExW(k, name, NULL, NULL, NULL, &sz) == ERROR_SUCCESS;
}

static DWORD FileVer(const wchar_t* path)
{
    DWORD v = 0, sz = GetFileVersionInfoSizeW(path, NULL);
    if (!sz) return 0;
    std::vector<BYTE> b(sz);
    if (!GetFileVersionInfoW(path, 0, sz, b.data())) return 0;
    VS_FIXEDFILEINFO* fi = NULL; UINT len = 0;
    if (VerQueryValueW(b.data(), L"\\", (void**)&fi, &len) && fi && len >= sizeof(VS_FIXEDFILEINFO))
        v = fi->dwFileVersionMS;
    return v;
}

static bool IsDir(const wchar_t* p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static bool IsFile(const wchar_t* p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

static long long FileSize(const wchar_t* p)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fa)) return -1;
    return ((long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}

// 找到音频：优先安装目录，其次 exe 目录
static std::wstring FindMedia(const wchar_t* exeDir, const wchar_t* instDir, const wchar_t* name)
{
    std::wstring a = std::wstring(instDir) + L"\\" + name;
    if (IsFile(a.c_str())) return a;
    std::wstring b = std::wstring(exeDir) + L"\\" + name;
    if (IsFile(b.c_str())) return b;
    return L"";
}

int wmain(int argc, wchar_t** argv)
{
    SetConsoleOutputCP(65001);
    bool keep = false;
    for (int i = 1; i < argc; i++) if (wcscmp(argv[i], L"--keep") == 0) keep = true;

    // ---- 先确定路径 ----
    wchar_t self[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, self, MAX_PATH);
    std::wstring sSelf = self;
    std::wstring exeDir = sSelf.substr(0, sSelf.find_last_of(L'\\'));
    wchar_t lad[MAX_PATH] = { 0 };
    SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, lad);
    std::wstring instDir = std::wstring(lad) + L"\\IslandAlarmTurbo";

    // ---- 报告文件 ----
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t rn[MAX_PATH];
    swprintf_s(rn, L"%ls\\check-report-%04d%02d%02d-%02d%02d%02d.txt",
               exeDir.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_reportPath = rn;
    _wfopen_s(&g_rep, rn, L"wb");

    wchar_t t0[16]; NowStr(t0, 16);
    Out(L"=====================================================================\n");
    Out(L" mobile 自检工具   %s\n", t0);
    Out(L" exe 目录 : %s\n", exeDir.c_str());
    Out(L" 安装目录 : %s\n", instDir.c_str());
    Out(L" 检测点   : 共 26 项\n");
    Out(L"=====================================================================\n");
    if (g_rep)
        RepOut(L"mobile 自检报告\n开始时间：%04d-%02d-%02d %s\nexe 目录：%s\n安装目录：%s\n\n",
                 st.wYear, st.wMonth, st.wDay, t0, exeDir.c_str(), instDir.c_str());

    // ================= 一、环境 =================
    {
        OSVERSIONINFOEXW os = { 0 };
        os.dwOSVersionInfoSize = sizeof(os);
        typedef LONG(WINAPI* pRtl)(OSVERSIONINFOEXW*);
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (nt) { auto p = (pRtl)GetProcAddress(nt, "RtlGetVersion"); if (p) p(&os); }
        wchar_t d[64];
        swprintf_s(d, L"%lu.%lu.%lu", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
        Check(L"Windows 版本 ≥ 10", os.dwMajorVersion >= 10, d,
              L"系统低于 Windows 10，部分 API（layered 子窗口等）不可用", L"升级到 Win10/11 再跑");
    }
    {
        BOOL wow64 = FALSE;
        IsWow64Process(GetCurrentProcess(), &wow64);
        SYSTEM_INFO si; GetNativeSystemInfo(&si);
        bool is64 = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64);
        Check(L"系统是 64 位", is64, is64 ? L"x64" : L"x86",
              L"32 位系统跑不了这个 64 位 exe", L"换 64 位系统，或编 32 位版");
        Check(L"当前进程是 64 位", !wow64, wow64 ? L"WOW64" : L"原生 64", L"", L"");
    }
    {
        wchar_t d[64];
        swprintf_s(d, L"%d×%d", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        int h = GetSystemMetrics(SM_CYSCREEN);
        Check(L"屏幕高度 ≥ 768", h >= 768, d,
              L"屏幕太矮，大岛可能贴到屏幕外", L"调低缩放比例或换分辨率");
    }
    {
        RECT wa = { 0 };
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        wchar_t d[64];
        swprintf_s(d, L"工作区 %d px", wa.bottom - wa.top);
        Check(L"工作区高度 ≥ 700", (wa.bottom - wa.top) >= 700, d,
              L"任务栏占太多，岛底下可能露不全", L"隐藏任务栏或调小缩放");
    }
    {
        wchar_t d[64];
        swprintf_s(d, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        bool ok = (st.wYear >= 2020 && st.wYear <= 2100);
        Check(L"系统日期合理", ok, d,
              L"日期不对会导致作息判定错乱（比如永远不打铃）", L"校准系统时间");
    }

    // ================= 二、安装 / 文件 =================
    Check(L"exe 所在目录存在", IsDir(exeDir.c_str()), exeDir.c_str(), L"", L"");

    {
        DWORD attr = GetFileAttributesW(exeDir.c_str());
        bool writable = (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_READONLY);
        Check(L"exe 目录可写", writable, writable ? L"可写" : L"只读或不存在",
              L"U 盘写保护 / 目录只读，日志和报告写不进去", L"去掉写保护，或换目录运行");
    }
    Check(L"安装目录存在", IsDir(instDir.c_str()), instDir.c_str(),
          L"没自安装过（可能一直是便携模式运行）", L"双击一次 exe 会自动安装");

    {
        std::wstring ie = instDir + L"\\islands.exe";
        bool ok = IsFile(ie.c_str());
        wchar_t d[64] = L"";
        if (ok)
        {
            DWORD vi = FileVer(ie.c_str());
            swprintf_s(d, L"v%lu.%lu", (vi >> 16) & 0xFFFF, vi & 0xFFFF);
        }
        Check(L"安装目录里有 islands.exe", ok, ok ? d : L"不存在",
              L"没安装，或装到别处了", L"双击 exe 重新安装");
    }
    {
        // 查的是**主程序**的版本，不是本工具自己的（本工具没编资源，永远是 0.0）
        std::wstring mainExe = instDir + L"\\islands.exe";
        if (!IsFile(mainExe.c_str()))
        {
            WIN32_FIND_DATAW fd;
            std::wstring pat = exeDir + L"\\mobile-*.exe";
            HANDLE hf = FindFirstFileW(pat.c_str(), &fd);
            if (hf != INVALID_HANDLE_VALUE) { mainExe = exeDir + L"\\" + fd.cFileName; FindClose(hf); }
        }
        DWORD vs = IsFile(mainExe.c_str()) ? FileVer(mainExe.c_str()) : 0;
        wchar_t d[64];
        swprintf_s(d, L"v%lu.%lu", (vs >> 16) & 0xFFFF, vs & 0xFFFF);
        Check(L"主程序带版本号", vs != 0, d,
              L"资源里的 VERSIONINFO 没生效，升级判断会失效（新版本覆盖不了旧版）",
              L"重新用 windres 编 app.rc");
    }

    // 音频三件
    {
        std::wstring a = FindMedia(exeDir.c_str(), instDir.c_str(), L"alin.mp3");
        long long sz = a.empty() ? -1 : FileSize(a.c_str());
        wchar_t d[256];
        swprintf_s(d, sz > 0 ? L"%lld KB  %ls" : L"没找到", sz > 0 ? sz / 1024 : 0, a.c_str());
        Check(L"提醒音 alin.mp3", sz > 0, d,
              L"没有提醒音 → 上下课只有岛没有声", L"把 alin.mp3 放到 exe 同目录，再双击一次");
    }
    {
        std::wstring a = FindMedia(exeDir.c_str(), instDir.c_str(), L"Home.mp3");
        long long sz = a.empty() ? -1 : FileSize(a.c_str());
        wchar_t d[256];
        swprintf_s(d, sz > 0 ? L"%lld KB  %ls" : L"没找到", sz > 0 ? sz / 1024 : 0, a.c_str());
        Check(L"放学音乐 Home.mp3", sz > 0, d,
              L"没有它，《回家》和播放器岛都不会有声音", L"把 Home.mp3 放到 exe 同目录");
    }
    {
        std::wstring a = FindMedia(exeDir.c_str(), instDir.c_str(), L"Home.jpg");
        long long sz = a.empty() ? -1 : FileSize(a.c_str());
        wchar_t d[256];
        swprintf_s(d, sz > 0 ? L"%lld KB" : L"没找到", sz > 0 ? sz / 1024 : 0);
        Check(L"播放器封面 Home.jpg", sz > 0, d,
              L"没有封面，播放器岛会显示占位图", L"把 Home.jpg 放到 exe 同目录");
    }

    // ================= 三、注册表配置 =================
    HKEY k = NULL;
    bool keyOk = (RegOpenKeyExW(HKEY_CURRENT_USER, REGPATH, 0, KEY_READ, &k) == ERROR_SUCCESS);
    Check(L"注册表配置键可读", keyOk, keyOk ? REGPATH : L"打不开",
          L"读不到配置 → 全部按默认值跑（铃声/作息可能不是你设的）",
          L"运行一次主程序会自动创建；还不行就查权限");

    if (keyOk)
    {
        DWORD v = 0;
        bool has = RegDword(k, L"TimeTable", &v);
        Check(L"作息表 TimeTable 合法", has && v <= 2,
              has ? (v == 0 ? L"初四夏季(11节)" : v == 1 ? L"冬季(9节)" : L"初四冬季(11节)") : L"没有该值",
              L"值不在 0/1/2 里，会按 0 处理，节次可能对不上", L"在设置里重新选一次作息表");

        DWORD pc = 11;
        if (has && v != 0) pc = 9;

        // 铃声开关
        DWORD mu = 0, md = 0;
        bool hu = RegDword(k, L"BellUpMask", &mu);
        bool hd = RegDword(k, L"BellDownMask", &md);
        Check(L"上课铃开关掩码", hu && (mu & ~0x7FF) == 0,
              hu ? L"存在" : L"没存过（默认全开）", L"", L"");
        Check(L"下课铃开关掩码", hd && (md & ~0x7FF) == 0,
              hd ? L"存在" : L"没存过（默认全开）", L"", L"");

        // 音量数组
        DWORD bsz = 0;
        LONG r = RegQueryValueExW(k, L"BellVols", NULL, NULL, NULL, &bsz);
        Check(L"每节课音量数组长度", r == ERROR_SUCCESS && bsz == 22,
              r == ERROR_SUCCESS ? (bsz == 22 ? L"22 字节" : L"长度不对") : L"没存过（默认 30%）",
              L"长度不是 22，读的时候会整段忽略 → 所有节次回到默认音量",
              L"在设置里随便拖一下某个音量滑条，会重写这个值");

        // 关键配置项
        struct { const wchar_t* n; const wchar_t* label; } keys[] = {
            { L"AudioOn", L"提示音总开关" },
            { L"AudioVolEnd", L"下课铃音量（全局兜底）" },
            { L"HomeVol", L"《回家》音量" },
            { L"PeriodEnabled", L"到点弹岛勾选" },
        };
        for (auto& it : keys)
        {
            DWORD t = 0;
            Check(it.label, RegDword(k, it.n, &t), RegDword(k, it.n, &t) ? L"存在" : L"没存过",
                  L"", L"");
        }

        // AI Key 这项删了：Turbo 没有 AI 岛，一直报"没配置"只会误导

        // 节次与掩码一致性（只看有没有超出 pc 的位被置上）
        if (hu && pc < 11)
        {
            DWORD over = mu >> pc;
            Check(L"铃开关没有超出节数的位", over == 0,
                  over ? L"有超出当前作息节的位被置上" : L"正常",
                  L"换过作息表，旧的高位可能残留（不影响当前显示，但值不干净）",
                  L"在设置里取消再勾一次即可清掉");
        }

        RegCloseKey(k);
    }

    // ================= 四、自启动 =================
    {
        HKEY rk = NULL;
        bool ok = false;
        std::wstring val;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                          0, KEY_READ, &rk) == ERROR_SUCCESS)
        {
            wchar_t buf[MAX_PATH * 2] = { 0 };
            DWORD sz = sizeof(buf), type = 0;
            if (RegQueryValueExW(rk, L"mobile", NULL, &type, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            {
                ok = true; val = buf;
            }
            RegCloseKey(rk);
        }
        Check(L"开机自启动项", ok, ok ? val.c_str() : L"没设置",
              L"不开机自启的话，重启后得手动双击", L"设置 → 更多 → 开机自启动");
    }

    // ================= 五、运行时 =================
    {
        HWND h = FindWindowW(L"IslandAlarmWindow", NULL);
        Check(L"主程序正在运行", h != NULL, h ? L"已运行" : L"没在跑",
              L"没在跑就收不到铃声", L"双击 mobile-v1.2.exe");
    }

    // ================= 收尾 =================
    wchar_t t1[16]; NowStr(t1, 16);
    Out(L"=====================================================================\n");
    Out(L" 结束 %s   共 %d 项，通过 %d，失败 %d\n", t1, g_total, g_total - g_no, g_no);
    if (g_no == 0) Out(L" 结论：全部通过\n");
    else           Out(L" 结论：有 %d 项没过，见下面的原因和建议\n", g_no);
    Out(L" 报告：%s\n", g_reportPath.c_str());
    Out(L"=====================================================================\n");

    if (g_rep)
    {
        RepOut(L"\n结束时间：%s\n共 %d 项，通过 %d，失败 %d\n\n", t1, g_total, g_total - g_no, g_no);
        if (g_no == 0)
        {
            RepOut(L"结论：全部通过。\n");
        }
        else
        {
            RepOut(L"================= 失败项明细 =================\n");
            for (size_t i = 0; i < g_fails.size(); i++)
            {
                RepOut(L"\n[%lu] %s\n", i + 1, g_fails[i].name.c_str());
                RepOut(L"    可能原因：%s\n", g_fails[i].why.c_str());
                RepOut(L"    建议    ：%s\n", g_fails[i].advice.c_str());
            }
        }
        fclose(g_rep);
    }

    if (!keep)
    {
        Out(L"\n8 秒后自动关闭本窗口…\n");
        Sleep(8000);
    }
    else
    {
        Out(L"\n--keep 模式：20 秒后关闭\n");
        Sleep(20000);
    }
    return 0;
}
