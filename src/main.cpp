// NovaBar — premium, lightweight animated taskbar for Windows 10 / 11
// Win32 + Direct2D + DirectWrite + DirectComposition. No runtime dependencies.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <shellscalingapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wincodec.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <iphlpapi.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <shcore.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include <algorithm>

// ───────────────────────────── helpers ─────────────────────────────
template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
static inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline D2D1_COLOR_F Col(float r, float g, float b, float a = 1) { D2D1_COLOR_F c = {r, g, b, a}; return c; }
static inline D2D1_COLOR_F Hex(uint32_t rgb, float a = 1) { return Col(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a); }
static inline D2D1_RECT_F RF(float l, float t, float r, float b) { D2D1_RECT_F x = {l, t, r, b}; return x; }
static inline D2D1_POINT_2F PT(float x, float y) { D2D1_POINT_2F p = {x, y}; return p; }
static inline bool InR(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
static D2D1_MATRIX_3X2_F MIdent() { D2D1_MATRIX_3X2_F m; m._11 = 1; m._12 = 0; m._21 = 0; m._22 = 1; m._31 = 0; m._32 = 0; return m; }
static D2D1_MATRIX_3X2_F MMul(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {
    D2D1_MATRIX_3X2_F m;
    m._11 = a._11 * b._11 + a._12 * b._21; m._12 = a._11 * b._12 + a._12 * b._22;
    m._21 = a._21 * b._11 + a._22 * b._21; m._22 = a._21 * b._12 + a._22 * b._22;
    m._31 = a._31 * b._11 + a._32 * b._21 + b._31; m._32 = a._31 * b._12 + a._32 * b._22 + b._32;
    return m;
}
static D2D1_MATRIX_3X2_F MTrans(float x, float y) { D2D1_MATRIX_3X2_F m = MIdent(); m._31 = x; m._32 = y; return m; }
static D2D1_MATRIX_3X2_F MRotScale(float deg, float s, float cx, float cy) {
    float r = deg * 3.14159265f / 180.f, c = cosf(r) * s, n = sinf(r) * s;
    D2D1_MATRIX_3X2_F m; m._11 = c; m._12 = n; m._21 = -n; m._22 = c; m._31 = 0; m._32 = 0;
    return MMul(MMul(MTrans(-cx, -cy), m), MTrans(cx, cy));
}

// Spring physics — every animation in the bar is driven by these.
struct Spring {
    float v = 0, t = 0, vel = 0;
    void snap(float x) { v = t = x; vel = 0; }
    bool step(float dt, float k, float d, float eps) {
        if (v == t && vel == 0) return false;
        const float h = 1.f / 240.f;
        while (dt > 0) { float s = dt < h ? dt : h; float a = k * (t - v) - d * vel; vel += a * s; v += vel * s; dt -= s; }
        if (fabsf(t - v) < eps && fabsf(vel) < eps * 8) { v = t; vel = 0; return false; }
        return true;
    }
    bool ui(float dt) { return step(dt, 320.f, 36.f, 0.002f); }     // hover / fades
    bool pop(float dt) { return step(dt, 210.f, 19.f, 0.002f); }    // bouncy appear
    bool lay(float dt) { return step(dt, 240.f, 27.f, 0.04f); }     // layout widths (px)
    bool slide(float dt) { return step(dt, 300.f, 25.f, 0.04f); }   // indicator slide
};

// ───────────────────────────── GUIDs (defined locally to avoid lib gaps) ─────────────────────────────
static const GUID kCLSID_MMDevEnum = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID kIID_IMMDevEnum = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID kIID_IEndpointVol = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};
static const GUID kIID_IImageFactory = {0xBCC18B79, 0xBA16, 0x442F, {0x80, 0xC4, 0x8A, 0x59, 0xC3, 0x0C, 0x46, 0x3B}};
static const GUID kIID_IPropStore = {0x886D8EEB, 0x8CF2, 0x4446, {0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99}};
static const PROPERTYKEY kPKEY_AUMID = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

// ───────────────────────────── config ─────────────────────────────
struct Config {
    bool top = false; int height = 48; int margin = 8;
    bool solid = false; float tint = 0.35f;
    uint32_t accent = 0x7AA2FF, accent2 = 0xB983FF;
    bool center = true, stats = true, title = true, hideTaskbar = true, islands = true;
    bool media = true, previews = true, trash = true, lang = true;
} g_cfg;
struct Pin { std::wstring key, target; };
static std::vector<Pin> g_pins;
static std::wstring g_iniPath, g_exePath;

static std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
static std::wstring IniGet(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048]; GetPrivateProfileStringW(sec, key, def, buf, 2048, g_iniPath.c_str()); return buf;
}
static int IniInt(const wchar_t* sec, const wchar_t* key, int def) { return (int)GetPrivateProfileIntW(sec, key, def, g_iniPath.c_str()); }
static uint32_t ParseHex(const std::wstring& s, uint32_t def) {
    std::wstring t = s; if (!t.empty() && t[0] == L'#') t = t.substr(1);
    if (t.size() != 6) return def; wchar_t* e = nullptr; unsigned long v = wcstoul(t.c_str(), &e, 16); return (e && *e == 0) ? (uint32_t)v : def;
}
static void WriteDefaultIni() {
    if (GetFileAttributesW(g_iniPath.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    const wchar_t* txt =
        L"; NovaBar ayarları. Değiştirdikten sonra bara sağ tıkla > \"Ayarları yeniden yükle\".\r\n"
        L"[Bar]\r\n"
        L"; bottom (alt) veya top (üst)\r\n"
        L"Position=bottom\r\n"
        L"; Bar yüksekliği (DIP). 40-56 arası iyi görünür.\r\n"
        L"Height=48\r\n"
        L"; Kenar boşluğu. 0 = kenara yapışık tam genişlik bar, 6-12 = yüzen ada görünümü\r\n"
        L"Margin=8\r\n"
        L"; islands (üç ayrı yüzen ada) veya bar (tek parça)\r\n"
        L"Style=islands\r\n"
        L"; acrylic (duvar kağıdından buzlu cam) veya solid (opak koyu)\r\n"
        L"Backdrop=acrylic\r\n"
        L"; Akrilik üstündeki koyu ton, 0-100\r\n"
        L"Tint=35\r\n"
        L"; Vurgu gradyanı\r\n"
        L"Accent=#7AA2FF\r\n"
        L"Accent2=#B983FF\r\n"
        L"CenterApps=1\r\n"
        L"ShowStats=1\r\n"
        L"ShowActiveTitle=1\r\n"
        L"HideWindowsTaskbar=1\r\n"
        L"ShowMedia=1\r\n"
        L"WindowPreviews=1\r\n"
        L"ShowTrash=1\r\n"
        L"ShowKeyboardLayout=1\r\n"
        L"\r\n[Pins]\r\nCount=1\r\n0=c:\\windows\\explorer.exe|explorer.exe\r\n";
    HANDLE f = CreateFileW(g_iniPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WORD bom = 0xFEFF; WriteFile(f, &bom, 2, &w, nullptr);
    WriteFile(f, txt, (DWORD)(wcslen(txt) * sizeof(wchar_t)), &w, nullptr); CloseHandle(f);
}
static void LoadConfig() {
    WriteDefaultIni();
    g_cfg.top = Lower(IniGet(L"Bar", L"Position", L"bottom")) == L"top";
    g_cfg.height = std::max(32, std::min(72, IniInt(L"Bar", L"Height", 48)));
    g_cfg.margin = std::max(0, std::min(40, IniInt(L"Bar", L"Margin", 8)));
    g_cfg.solid = Lower(IniGet(L"Bar", L"Backdrop", L"acrylic")) == L"solid";
    g_cfg.tint = std::max(0, std::min(100, IniInt(L"Bar", L"Tint", 35))) / 100.f;
    g_cfg.accent = ParseHex(IniGet(L"Bar", L"Accent", L"#7AA2FF"), 0x7AA2FF);
    g_cfg.accent2 = ParseHex(IniGet(L"Bar", L"Accent2", L"#B983FF"), 0xB983FF);
    g_cfg.center = IniInt(L"Bar", L"CenterApps", 1) != 0;
    g_cfg.stats = IniInt(L"Bar", L"ShowStats", 1) != 0;
    g_cfg.title = IniInt(L"Bar", L"ShowActiveTitle", 1) != 0;
    g_cfg.hideTaskbar = IniInt(L"Bar", L"HideWindowsTaskbar", 1) != 0;
    g_cfg.islands = Lower(IniGet(L"Bar", L"Style", L"islands")) != L"bar";
    g_cfg.media = IniInt(L"Bar", L"ShowMedia", 1) != 0;
    g_cfg.previews = IniInt(L"Bar", L"WindowPreviews", 1) != 0;
    g_cfg.trash = IniInt(L"Bar", L"ShowTrash", 1) != 0;
    g_cfg.lang = IniInt(L"Bar", L"ShowKeyboardLayout", 1) != 0;
    g_pins.clear();
    int n = IniInt(L"Pins", L"Count", 0);
    for (int i = 0; i < n && i < 64; i++) {
        std::wstring v = IniGet(L"Pins", std::to_wstring(i).c_str(), L"");
        size_t p = v.find(L'|'); if (p == std::wstring::npos || p == 0) continue;
        g_pins.push_back({Lower(v.substr(0, p)), v.substr(p + 1)});
    }
}
static void SavePins() {
    WritePrivateProfileStringW(L"Pins", nullptr, nullptr, g_iniPath.c_str());
    WritePrivateProfileStringW(L"Pins", L"Count", std::to_wstring(g_pins.size()).c_str(), g_iniPath.c_str());
    for (size_t i = 0; i < g_pins.size(); i++)
        WritePrivateProfileStringW(L"Pins", std::to_wstring(i).c_str(), (g_pins[i].key + L"|" + g_pins[i].target).c_str(), g_iniPath.c_str());
}

// ───────────────────────────── state ─────────────────────────────
static HWND g_hwnd = nullptr;
static float g_scale = 1.f;
static int g_pxW = 0, g_pxH = 0;

static UINT WM_TASKBARCREATED = 0;
#define WM_APPBAR (WM_APP + 1)
#define WM_REFRESH (WM_APP + 2)
enum { TMR_POLL = 1, TMR_REFRESH = 2, TMR_VOLHIDE = 3, TMR_REHIDE = 4, TMR_FAST = 5, TMR_POPUP = 6, TMR_POPHIDE = 7 };

static ID2D1Factory1* g_d2d = nullptr;
static IDWriteFactory* g_dw = nullptr;
static IWICImagingFactory* g_wic = nullptr;
static ID3D11Device* g_d3d = nullptr;
static IDXGISwapChain1* g_swap = nullptr;
static ID2D1Device* g_d2dDev = nullptr;
static ID2D1DeviceContext* g_dc = nullptr;
static ID2D1Bitmap1* g_target = nullptr;
static IDCompositionDevice* g_dcomp = nullptr;
static IDCompositionTarget* g_dtarget = nullptr;
static IDCompositionVisual* g_visual = nullptr;
static int g_gen = 0;
static ID2D1DeviceContext* g_curDC = nullptr; // DC for HQ bitmap drawing (may be null)

static ID2D1SolidColorBrush* g_br = nullptr;
static ID2D1LinearGradientBrush* g_acc = nullptr;
static ID2D1LinearGradientBrush* g_sheen = nullptr; static ID2D1LinearGradientBrush* g_rim = nullptr;
static ID2D1RadialGradientBrush *g_spot = nullptr, *g_shadow = nullptr, *g_edge = nullptr, *g_accGlow = nullptr;
static IDWriteTextFormat *g_fStatC = nullptr, *g_fTile = nullptr, *g_fStatV = nullptr, *g_fTime = nullptr, *g_fDate = nullptr, *g_fSmall = nullptr, *g_fTitle = nullptr, *g_fIcon = nullptr, *g_fIconS = nullptr, *g_fLetter = nullptr;
static IDWriteInlineObject *g_ellipsis = nullptr, *g_ellipsis2 = nullptr, *g_ellipsis3 = nullptr;
static float g_alpha = 1.f;

struct AppItem {
    std::wstring key, exe, aumid, target;
    std::vector<HWND> wins;
    bool pinned = false, alive = true, active = false;
    IWICBitmap* wic = nullptr; ID2D1Bitmap* bmp = nullptr; int bmpGen = -1; bool iconTried = false;
    Spring w, appear, hover, press, act, titleA, run;
    float ripple = -1.f;
    std::wstring title; IDWriteTextLayout* tl = nullptr; float tlW = 0, tlH = 0;
    float x = 0, dw = 0, icx = 0; int cycle = 0; uint32_t tone = 0x6C7BFF;
    Spring mag; float bounce = -1.f;
    ID2D1RadialGradientBrush* glow = nullptr; int glowGen = -1; uint32_t glowTone = 0;
    bool special = false, nameTried = false; std::wstring name; Spring offX;
};
static std::vector<AppItem*> g_items;

enum RK { RK_NONE, RK_START, RK_APP, RK_STATS, RK_QUICK, RK_CLOCK, RK_DESK, RK_MINFO, RK_MPREV, RK_MPLAY, RK_MNEXT, RK_LANG };
struct Hit { RK k = RK_NONE; AppItem* it = nullptr; bool operator==(const Hit& o) const { return k == o.k && it == o.it; } };
static Hit g_hover, g_press;
static Spring g_hStart, g_pStart, g_hStats, g_hQuick, g_pQuick, g_hClock, g_pClock, g_hDesk, g_pDesk;
static Spring g_indX, g_indW, g_indA, g_enter, g_mouseX, g_barHover, g_volShow, g_volV, g_cpuV, g_ramV, g_batV;
static D2D1_RECT_F g_rStart, g_rStats, g_rQuick, g_rClock, g_rDesk;
static std::vector<uint32_t> g_wallPx; static int g_wallW = 0, g_wallH = 0, g_wallDown = 2, g_wallPad = 0;
static std::wstring g_wallKey; static bool g_wallDirty = false; static int g_wallGen = -1;
static ID2D1Bitmap* g_wallBmp = nullptr; static ID2D1BitmapBrush* g_wallBrush = nullptr;
static ID2D1Bitmap* g_noiseBmp = nullptr; static ID2D1BitmapBrush* g_noiseBrush = nullptr; static int g_noiseGen = -1;
static ID2D1StrokeStyle* g_round = nullptr;
static RECT g_winRc = {}, g_monRc = {};
static float g_md = 8.f;
static Spring g_divW;
static D2D1_RECT_F g_islL, g_islC, g_islR;
// media / extras / drag / popup shared state
static Spring g_mW, g_mA, g_mPlayT, g_hMInfo, g_hMPrev, g_hMPlay, g_hMNext, g_pMPrev, g_pMPlay, g_pMNext, g_hLang, g_pLang;
static float g_mProg = 0;
static D2D1_RECT_F g_islM, g_rMInfo, g_rMPrev, g_rMPlay, g_rMNext, g_rLang;
static Spring g_div2W;
static std::wstring g_lang; static bool g_langShow = false;
static bool g_dragSwapPending = false;
static AppItem *g_dragCand = nullptr, *g_dragItem = nullptr; static float g_dragDownX = 0, g_dragX = 0; static Spring g_dragLift;
static Spring g_popA, g_popY; static AppItem *g_popItem = nullptr, *g_hoverPend = nullptr;
static long long g_trashCount = -1;
static void ClosePopup(); static void ClosePopupNow(); static void OpenPopup(AppItem* it); static void PopupSync();
static void SendKeys(std::initializer_list<WORD> keys); static void ActivateWin(HWND w); static void ReleasePopupDevice(); static void ClickApp(AppItem* it);
static bool g_fullscreen = false;
static bool g_tracking = false, g_mouseIn = false; static float g_mouseRawX = 0;

static bool g_animating = false;
static LARGE_INTEGER g_qpf, g_last;

// system info
static std::wstring g_timeStr, g_dateStr; static IDWriteTextLayout *g_tlTime = nullptr, *g_tlDate = nullptr; static float g_clockW = 60, g_timeH = 16, g_dateH = 14;
static float g_cpu = 0, g_ram = 0; static ULONGLONG g_lastIdle = 0, g_lastTotal = 0;
static IAudioEndpointVolume* g_epv = nullptr; static float g_vol = 0.5f; static bool g_mute = false;
static int g_net = 1; // 0 none, 1 wifi, 2 ethernet
static bool g_hasBatt = false, g_charging = false; static int g_batt = 100;
static bool g_win11 = true;
static int g_tick = 0;

// taskbar hiding
static bool g_tbHidden = false;
static HWND g_fg = nullptr;

// ───────────────────────────── animation pump ─────────────────────────────
static void Kick() {
    if (!g_animating) { g_animating = true; QueryPerformanceCounter(&g_last); g_last.QuadPart -= g_qpf.QuadPart / 60; }
}

// ───────────────────────────── text ─────────────────────────────
static bool FontExists(const wchar_t* fam) {
    IDWriteFontCollection* col = nullptr; BOOL ex = FALSE; UINT32 idx;
    if (SUCCEEDED(g_dw->GetSystemFontCollection(&col, FALSE)) && col) { col->FindFamilyName(fam, &idx, &ex); col->Release(); }
    return ex != FALSE;
}
static IDWriteTextFormat* MakeFormat(const wchar_t* fam, float size, DWRITE_FONT_WEIGHT wt, DWRITE_TEXT_ALIGNMENT al) {
    IDWriteTextFormat* f = nullptr;
    g_dw->CreateTextFormat(fam, nullptr, wt, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
    if (f) { f->SetTextAlignment(al); f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); }
    return f;
}
static void CreateTextResources() {
    const wchar_t* ui = FontExists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    const wchar_t* disp = FontExists(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : ui;
    const wchar_t* ico = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    g_fTime = MakeFormat(disp, 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fStatV = MakeFormat(disp, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fStatC = MakeFormat(disp, 11.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fDate = MakeFormat(ui, 10.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fSmall = MakeFormat(ui, 9.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fTitle = MakeFormat(ui, 12.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    g_fIcon = MakeFormat(ico, 14.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fIconS = MakeFormat(ico, 8.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    g_fLetter = MakeFormat(disp, 13.f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (g_fTitle && SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(g_fTitle, &g_ellipsis))) {
        DWRITE_TRIMMING tr = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        g_fTitle->SetTrimming(&tr, g_ellipsis);
    }
    g_fTile = MakeFormat(ui, 11.f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    if (g_fTile && SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(g_fTile, &g_ellipsis2))) {
        DWRITE_TRIMMING tr = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; g_fTile->SetTrimming(&tr, g_ellipsis2);
    }
    if (g_fDate && SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(g_fDate, &g_ellipsis3))) {
        DWRITE_TRIMMING tr = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; g_fDate->SetTrimming(&tr, g_ellipsis3);
    }
}
static IDWriteTextLayout* Layout(const std::wstring& s, IDWriteTextFormat* f, float maxW, float* w, float* h) {
    IDWriteTextLayout* tl = nullptr;
    g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), f, maxW, 40.f, &tl);
    if (tl) { tl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR); DWRITE_TEXT_METRICS m; tl->GetMetrics(&m); if (w) *w = std::min(maxW, m.widthIncludingTrailingWhitespace); if (h) *h = m.height; }
    return tl;
}

// ───────────────────────────── icons ─────────────────────────────
static IWICBitmap* WicFromHBITMAP(HBITMAP hb) {
    BITMAP bm = {}; if (!GetObject(hb, sizeof(bm), &bm) || bm.bmWidth <= 0) return nullptr;
    int w = bm.bmWidth, h = abs(bm.bmHeight);
    std::vector<uint32_t> px((size_t)w * h);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr); int ok = GetDIBits(dc, hb, 0, h, px.data(), &bi, DIB_RGB_COLORS); ReleaseDC(nullptr, dc);
    if (!ok) return nullptr;
    bool anyA = false, premul = true;
    for (uint32_t p : px) {
        uint32_t a = p >> 24; if (a) anyA = true;
        if (((p >> 16) & 255) > a || ((p >> 8) & 255) > a || (p & 255) > a) premul = false;
    }
    for (auto& p : px) {
        if (!anyA) { p |= 0xFF000000u; continue; }
        if (!premul) {
            uint32_t a = p >> 24;
            uint32_t r = ((p >> 16) & 255) * a / 255, g = ((p >> 8) & 255) * a / 255, b = (p & 255) * a / 255;
            p = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    IWICBitmap* wb = nullptr;
    g_wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, (UINT)(px.size() * 4), (BYTE*)px.data(), &wb);
    return wb;
}
static IWICBitmap* WicFromShell(const std::wstring& path, int size) {
    IShellItemImageFactory* f = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, kIID_IImageFactory, (void**)&f)) || !f) return nullptr;
    HBITMAP hb = nullptr; SIZE sz = {size, size};
    HRESULT hr = f->GetImage(sz, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hb);
    f->Release();
    if (FAILED(hr) || !hb) return nullptr;
    IWICBitmap* r = WicFromHBITMAP(hb); DeleteObject(hb); return r;
}
static IWICBitmap* WicFromWindow(HWND h) {
    HICON hi = nullptr;
    DWORD_PTR res = 0;
    if (SendMessageTimeoutW(h, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 60, &res) && res) hi = (HICON)res;
    if (!hi) hi = (HICON)GetClassLongPtrW(h, GCLP_HICON);
    if (!hi) return nullptr;
    IWICBitmap* wb = nullptr; g_wic->CreateBitmapFromHICON(hi, &wb); return wb;
}
static uint32_t ToneFromWic(IWICBitmap* b) {
    UINT w = 0, h = 0; if (FAILED(b->GetSize(&w, &h)) || !w || !h || w * h > 512 * 512) return 0;
    std::vector<uint32_t> px((size_t)w * h);
    if (FAILED(b->CopyPixels(nullptr, w * 4, (UINT)(px.size() * 4), (BYTE*)px.data()))) return 0;
    double R = 0, G = 0, B = 0, Wt = 0;
    for (uint32_t p : px) {
        uint32_t a = p >> 24; if (a < 160) continue;
        float r = ((p >> 16) & 255) * 255.f / a, g = ((p >> 8) & 255) * 255.f / a, bl = (p & 255) * 255.f / a;
        r = std::min(r, 255.f); g = std::min(g, 255.f); bl = std::min(bl, 255.f);
        float mx = std::max(r, std::max(g, bl)), mn = std::min(r, std::min(g, bl));
        if (mx < 40) continue;
        float s = (mx - mn) / mx, wt = s * s * (mx / 255.f);
        R += r * wt; G += g * wt; B += bl * wt; Wt += wt;
    }
    if (Wt < px.size() * 0.004) return 0; // mostly grey icon
    float r = (float)(R / Wt), g = (float)(G / Wt), bl = (float)(B / Wt), mx = std::max(r, std::max(g, bl));
    float f = mx > 1 ? 235.f / mx : 1.f; r *= f; g *= f; bl *= f;
    return ((uint32_t)std::min(255.f, r) << 16) | ((uint32_t)std::min(255.f, g) << 8) | (uint32_t)std::min(255.f, bl);
}
static void LoadItemIcon(AppItem* it) {
    if (it->iconTried) return; it->iconTried = true;
    int px = (int)ceilf(38.f * g_scale);
    if (it->special) {
        it->wic = WicFromShell(L"shell:RecycleBinFolder", px);
        if (!it->wic) it->wic = WicFromShell(L"::{645FF040-5081-101B-9F08-00AA002F954E}", px);
        if (it->wic) { uint32_t t = ToneFromWic(it->wic); it->tone = t ? t : g_cfg.accent; }
        SafeRelease(it->bmp); it->bmpGen = -1; return;
    }
    if (!it->aumid.empty()) it->wic = WicFromShell(L"shell:AppsFolder\\" + it->aumid, px);
    if (!it->wic && !it->exe.empty()) it->wic = WicFromShell(it->exe, px);
    if (!it->wic && !it->target.empty() && it->target.find(L'\\') != std::wstring::npos) it->wic = WicFromShell(it->target, px);
    if (!it->wic && !it->wins.empty()) it->wic = WicFromWindow(it->wins[0]);
    if (it->wic) { uint32_t t = ToneFromWic(it->wic); it->tone = t ? t : g_cfg.accent; }
    SafeRelease(it->bmp); it->bmpGen = -1;
}
static void ReloadAllIcons() { for (auto* it : g_items) { SafeRelease(it->wic); SafeRelease(it->bmp); it->iconTried = false; LoadItemIcon(it); } }

// ───────────────────────────── window enumeration ─────────────────────────────
struct WinInfo { std::wstring key, exe, aumid; };
static std::unordered_map<HWND, WinInfo> g_winCache;

static std::wstring ProcPath(DWORD pid) {
    std::wstring r; HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p) { wchar_t b[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; if (QueryFullProcessImageNameW(p, 0, b, &n)) r.assign(b, n); CloseHandle(p); }
    return r;
}
static bool IsTaskWindow(HWND h) {
    if (h == g_hwnd || !IsWindowVisible(h)) return false;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    HWND owner = GetWindow(h, GW_OWNER);
    if (owner && !(ex & WS_EX_APPWINDOW)) return false;
    if ((ex & WS_EX_NOACTIVATE) && !(ex & WS_EX_APPWINDOW)) return false;
    int cloaked = 0; DwmGetWindowAttribute(h, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked));
    if (cloaked) return false;
    if (GetWindowTextLengthW(h) == 0) return false;
    wchar_t cls[64]; GetClassNameW(h, cls, 64);
    if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd") || !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) return false;
    return true;
}
static BOOL CALLBACK FindCoreChild(HWND c, LPARAM lp) {
    auto* io = (std::pair<DWORD, DWORD>*)lp; DWORD pid; GetWindowThreadProcessId(c, &pid);
    if (pid != io->first) { io->second = pid; return FALSE; }
    return TRUE;
}
static WinInfo& GetInfo(HWND h) {
    auto f = g_winCache.find(h); if (f != g_winCache.end() && !f->second.key.empty()) return f->second;
    WinInfo wi; DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    wi.exe = ProcPath(pid);
    std::wstring le = Lower(wi.exe);
    IPropertyStore* ps = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(h, kIID_IPropStore, (void**)&ps)) && ps) {
        PROPVARIANT pv; PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kPKEY_AUMID, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) wi.aumid = pv.pwszVal;
        PropVariantClear(&pv); ps->Release();
    }
    if (le.size() >= 24 && le.compare(le.size() - 24, 24, L"applicationframehost.exe") == 0 && wi.aumid.empty()) {
        std::pair<DWORD, DWORD> io(pid, 0); EnumChildWindows(h, FindCoreChild, (LPARAM)&io);
        if (io.second) wi.exe = ProcPath(io.second);
    }
    bool isExplorer = le.size() >= 12 && le.compare(le.size() - 12, 12, L"explorer.exe") == 0;
    if (isExplorer) wi.aumid.clear();
    wi.key = Lower(!wi.aumid.empty() ? wi.aumid : (!wi.exe.empty() ? wi.exe : std::to_wstring((uintptr_t)h)));
    { std::wstring le2 = Lower(wi.exe); for (auto& p : g_pins) if (!le2.empty() && p.key == le2) { wi.key = p.key; break; } }
    g_winCache[h] = wi; return g_winCache[h];
}
static BOOL CALLBACK EnumCollect(HWND h, LPARAM lp) { if (IsTaskWindow(h)) ((std::vector<HWND>*)lp)->push_back(h); return TRUE; }

static uint32_t ToneFor(const std::wstring& k) {
    static const uint32_t tones[] = {0x6C8CFF, 0xA77BFF, 0x4FC3C8, 0xF2A65A, 0xE96F92, 0x7BD88F};
    uint32_t h = 2166136261u; for (wchar_t c : k) h = (h ^ c) * 16777619u; return tones[h % 6];
}
static AppItem* NewItem(const std::wstring& key) {
    AppItem* it = new AppItem(); it->key = key; it->tone = ToneFor(key);
    it->appear.v = 0; it->appear.t = 1; it->w.v = 0; it->w.t = 42; return it;
}
static void UpdateActive();

static bool g_inRefresh = false;
static void RefreshWindows() {
    if (g_inRefresh) { SetTimer(g_hwnd, TMR_REFRESH, 120, nullptr); return; }
    struct Guard { Guard() { g_inRefresh = true; } ~Guard() { g_inRefresh = false; } } guard;
    std::vector<HWND> list; EnumWindows(EnumCollect, (LPARAM)&list);
    // purge cache of dead windows
    for (auto i = g_winCache.begin(); i != g_winCache.end();) { if (!IsWindow(i->first)) i = g_winCache.erase(i); else ++i; }
    std::unordered_map<std::wstring, std::vector<HWND>> groups; std::vector<std::wstring> order;
    std::unordered_map<std::wstring, WinInfo> infos;
    for (HWND h : list) {
        WinInfo& wi = GetInfo(h);
        if (!groups.count(wi.key)) { order.push_back(wi.key); infos[wi.key] = wi; }
        groups[wi.key].push_back(h);
    }
    bool changed = false;
    // pinned items first, in pin order
    for (size_t i = 0; i < g_pins.size(); i++) {
        auto f = std::find_if(g_items.begin(), g_items.end(), [&](AppItem* a) { return a->key == g_pins[i].key; });
        if (f == g_items.end()) {
            AppItem* it = NewItem(g_pins[i].key); it->target = g_pins[i].target;
            if (g_pins[i].target.find(L'\\') != std::wstring::npos) it->exe = g_pins[i].target;
            else if (g_pins[i].target.rfind(L"shell:AppsFolder\\", 0) == 0) it->aumid = g_pins[i].target.substr(17);
            else if (g_pins[i].key.find(L'\\') != std::wstring::npos) it->exe = g_pins[i].key;
            size_t pos = std::min(i, g_items.size()); g_items.insert(g_items.begin() + pos, it); changed = true;
        }
    }
    for (auto* it : g_items) {
        if (it->special) continue;
        it->pinned = std::any_of(g_pins.begin(), g_pins.end(), [&](const Pin& p) { return p.key == it->key; });
        auto g = groups.find(it->key);
        std::vector<HWND> nw = g != groups.end() ? g->second : std::vector<HWND>();
        if (nw != it->wins) { it->wins = nw; changed = true; }
        if (g != groups.end()) {
            const WinInfo& wi = infos[it->key];
            if (it->exe.empty()) it->exe = wi.exe;
            if (it->aumid.empty()) it->aumid = wi.aumid;
        }
        bool shouldLive = it->pinned || !it->wins.empty();
        if (shouldLive != it->alive) { it->alive = shouldLive; it->appear.t = shouldLive ? 1.f : 0.f; changed = true; }
        it->run.t = it->wins.empty() ? 0.f : 1.f;
    }
    for (auto& k : order) {
        if (std::none_of(g_items.begin(), g_items.end(), [&](AppItem* a) { return a->key == k; })) {
            AppItem* it = NewItem(k); it->wins = groups[k]; it->exe = infos[k].exe; it->aumid = infos[k].aumid;
            it->run.snap(0); it->run.t = 1; g_items.push_back(it); changed = true;
        }
    }
    // recycle bin always sits at the end of the dock
    {
        auto f = std::find_if(g_items.begin(), g_items.end(), [](AppItem* a) { return a->special; });
        if (g_cfg.trash) {
            AppItem* t = nullptr;
            if (f == g_items.end()) { t = NewItem(L"::trash"); t->special = true; t->target = L"shell:RecycleBinFolder"; g_items.push_back(t); changed = true; }
            else { t = *f; if (f + 1 != g_items.end()) { g_items.erase(f); g_items.push_back(t); } }
            if (!t->alive) { t->alive = true; t->appear.t = 1; changed = true; }
            t->pinned = false; t->run.t = 0; t->wins.clear();
        } else if (f != g_items.end() && (*f)->alive) { (*f)->alive = false; (*f)->appear.t = 0; changed = true; }
    }
    { std::vector<AppItem*> snap = g_items; for (auto* it : snap) if (!it->iconTried) LoadItemIcon(it); }
    UpdateActive();
    PopupSync();
    if (changed) Kick();
}

static HWND RootOwner(HWND h) { HWND r = h ? GetAncestor(h, GA_ROOTOWNER) : nullptr; return r ? r : h; }
static void UpdateActive() {
    HWND fg = RootOwner(GetForegroundWindow());
    if (fg == g_hwnd) return; // our menus etc. — keep previous
    g_fg = fg;
    bool any = false;
    for (auto* it : g_items) {
        bool a = it->alive && std::find(it->wins.begin(), it->wins.end(), fg) != it->wins.end();
        if (a) {
            any = true; wchar_t t[512]; GetWindowTextW(fg, t, 512); std::wstring ts = t;
            if (ts != it->title || !it->tl) {
                it->title = ts; SafeRelease(it->tl);
                it->tl = Layout(ts, g_fTitle, 150.f, &it->tlW, &it->tlH);
                Kick();
            }
        }
        if (a != it->active) { it->active = a; Kick(); }
    }
    if (!any) g_indA.t = 0;
}

// ───────────────────────────── system info ─────────────────────────────
static void AcquireVolume() {
    SafeRelease(g_epv); IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(kCLSID_MMDevEnum, nullptr, CLSCTX_ALL, kIID_IMMDevEnum, (void**)&en)) && en) {
        IMMDevice* d = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &d)) && d) { d->Activate(kIID_IEndpointVol, CLSCTX_ALL, nullptr, (void**)&g_epv); d->Release(); }
        en->Release();
    }
}
static void ReadVolume() {
    if (!g_epv) return; float v; BOOL m;
    if (SUCCEEDED(g_epv->GetMasterVolumeLevelScalar(&v)) && SUCCEEDED(g_epv->GetMute(&m))) {
        if (fabsf(v - g_vol) > 0.004f || (m != 0) != g_mute) { g_vol = v; g_mute = m != 0; g_volV.t = v; Kick(); }
    } else SafeRelease(g_epv);
}
static void ReadNet() {
    ULONG sz = 16 * 1024; std::vector<BYTE> buf(sz);
    ULONG r = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, (PIP_ADAPTER_ADDRESSES)buf.data(), &sz);
    if (r == ERROR_BUFFER_OVERFLOW) { buf.resize(sz); r = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, (PIP_ADAPTER_ADDRESSES)buf.data(), &sz); }
    int net = 0;
    if (r == NO_ERROR) for (auto* a = (PIP_ADAPTER_ADDRESSES)buf.data(); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress) continue;
        if (a->IfType == 71) { net = 1; break; }
        if (a->IfType == IF_TYPE_ETHERNET_CSMACD) net = 2;
    }
    if (net != g_net) { g_net = net; Kick(); }
}
static void ReadStats() {
    FILETIME fi, fk, fu;
    if (GetSystemTimes(&fi, &fk, &fu)) {
        ULONGLONG i = ((ULONGLONG)fi.dwHighDateTime << 32) | fi.dwLowDateTime, k = ((ULONGLONG)fk.dwHighDateTime << 32) | fk.dwLowDateTime, u = ((ULONGLONG)fu.dwHighDateTime << 32) | fu.dwLowDateTime;
        ULONGLONG tot = k + u;
        if (g_lastTotal && tot > g_lastTotal) g_cpu = 1.f - (float)(i - g_lastIdle) / (float)(tot - g_lastTotal);
        g_lastIdle = i; g_lastTotal = tot;
    }
    MEMORYSTATUSEX ms = {sizeof(ms)}; if (GlobalMemoryStatusEx(&ms)) g_ram = ms.dwMemoryLoad / 100.f;
    g_cpuV.t = Clamp01(g_cpu); g_ramV.t = Clamp01(g_ram);
    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps)) {
        g_hasBatt = !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255 && ps.BatteryLifePercent <= 100;
        g_batt = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : 100; g_charging = ps.ACLineStatus == 1; g_batV.t = g_batt / 100.f;
    }
    if (g_cfg.stats || g_hasBatt) Kick();
}
static void ReadClock() {
    wchar_t t[64] = {}, d[64] = {};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, nullptr, nullptr, t, 64);
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, nullptr, L"d MMM ddd", d, 64, nullptr);
    if (g_timeStr != t || g_dateStr != d || !g_tlTime) {
        g_timeStr = t; g_dateStr = d; SafeRelease(g_tlTime); SafeRelease(g_tlDate);
        float w1 = 0, w2 = 0; g_tlTime = Layout(g_timeStr, g_fTime, 200, &w1, &g_timeH); g_tlDate = Layout(g_dateStr, g_fDate, 200, &w2, &g_dateH);
        g_clockW = std::max(w1, w2) + 22.f; Kick();
    }
}

// ───────────────────────────── media (GlobalSystemMediaTransportControls via raw WinRT ABI) ─────────────────────────────
struct NbAsyncInfo : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct NbAsyncOp : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void*) = 0;
};
struct NbStreamRef : public IInspectable { virtual HRESULT STDMETHODCALLTYPE OpenReadAsync(NbAsyncOp**) = 0; };
struct NbMediaProps : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTrackCount(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(NbStreamRef**) = 0;
};
struct NbPlayback : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int*) = 0;
};
struct NbTimeline : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(INT64*) = 0;
};
struct NbSession : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(NbTimeline**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(NbPlayback**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPlayAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPauseAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryStopAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRecordAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryFastForwardAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRewindAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipNextAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipPreviousAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelUpAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelDownAsync(NbAsyncOp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryTogglePlayPauseAsync(NbAsyncOp**) = 0;
};
struct NbManager : public IInspectable { virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(NbSession**) = 0; };
struct NbManagerStatics : public IInspectable { virtual HRESULT STDMETHODCALLTYPE RequestAsync(NbAsyncOp**) = 0; };
static const GUID kIID_MgrStatics = {0x2050c4ee, 0x11a0, 0x57de, {0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45}};
static const GUID kIID_AsyncInfo = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kIID_IStream = {0x0000000c, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

struct MediaState {
    bool disabled = false;
    NbManagerStatics* st = nullptr; NbAsyncOp* mgrOp = nullptr; NbManager* mgr = nullptr;
    NbAsyncOp *propsOp = nullptr, *thumbOp = nullptr;
    std::wstring aumid, title, artist, artKey;
    int status = 0; bool has = false; int propsAge = 99;
    INT64 posTicks = 0, endTicks = 0, lastUpd = 0;
    IWICBitmap* art = nullptr; ID2D1Bitmap* artBmp = nullptr; ID2D1BitmapBrush* artBrush = nullptr; int artGen = -1; uint32_t tone = 0;
    IDWriteTextLayout *tlT = nullptr, *tlA = nullptr; float tW = 0, aW = 0, tH = 16, aH = 14;
} g_media;

static std::wstring HStr(HSTRING h) {
    if (!h) return L""; UINT32 n = 0; const wchar_t* p = WindowsGetStringRawBuffer(h, &n);
    std::wstring s(p ? p : L"", p ? n : 0); WindowsDeleteString(h); return s;
}
static int OpStatus(NbAsyncOp* op) {
    NbAsyncInfo* ai = nullptr; int s = 3;
    if (op && SUCCEEDED(op->QueryInterface(kIID_AsyncInfo, (void**)&ai)) && ai) { if (FAILED(ai->get_Status(&s))) s = 3; ai->Release(); }
    return s; // 0 started, 1 completed, 2 canceled, 3 error
}
static void MediaSetText() {
    auto& m = g_media; SafeRelease(m.tlT); SafeRelease(m.tlA);
    m.tlT = Layout(m.title, g_fTitle, 150.f, &m.tW, &m.tH);
    m.tlA = Layout(m.artist.empty() ? std::wstring(L"—") : m.artist, g_fDate, 150.f, &m.aW, &m.aH);
}
static void MediaLoadArt(IUnknown* ras) {
    auto& m = g_media; IStream* is = nullptr;
    if (FAILED(CreateStreamOverRandomAccessStream(ras, kIID_IStream, (void**)&is)) || !is) return;
    IWICBitmapDecoder* dec = nullptr;
    if (SUCCEEDED(g_wic->CreateDecoderFromStream(is, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) && dec) {
        IWICBitmapFrameDecode* fr = nullptr;
        if (SUCCEEDED(dec->GetFrame(0, &fr)) && fr) {
            UINT w = 0, h = 0; fr->GetSize(&w, &h);
            if (w && h) {
                UINT sq = std::min(w, h); WICRect rc = {(INT)((w - sq) / 2), (INT)((h - sq) / 2), (INT)sq, (INT)sq};
                IWICBitmapClipper* cl = nullptr; IWICFormatConverter* cv = nullptr; IWICBitmapScaler* sc = nullptr;
                g_wic->CreateBitmapClipper(&cl); g_wic->CreateFormatConverter(&cv); g_wic->CreateBitmapScaler(&sc);
                UINT px = (UINT)std::max(24.f, 64.f * g_scale);
                if (cl && cv && sc && SUCCEEDED(cl->Initialize(fr, &rc)) &&
                    SUCCEEDED(cv->Initialize(cl, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
                    SUCCEEDED(sc->Initialize(cv, px, px, WICBitmapInterpolationModeFant))) {
                    SafeRelease(m.art); SafeRelease(m.artBrush); SafeRelease(m.artBmp);
                    if (SUCCEEDED(g_wic->CreateBitmapFromSource(sc, WICBitmapCacheOnLoad, &m.art)) && m.art) m.tone = ToneFromWic(m.art);
                }
                SafeRelease(sc); SafeRelease(cv); SafeRelease(cl);
            }
            fr->Release();
        }
        dec->Release();
    }
    is->Release();
}
static NbSession* MediaSession() {
    NbSession* s = nullptr; if (g_media.mgr) g_media.mgr->GetCurrentSession(&s); return s;
}
static void MediaPoll() {
    auto& m = g_media;
    if (m.disabled || !g_cfg.media) { if (m.has) { m.has = false; Kick(); } return; }
    if (!m.st) {
        HSTRING cls = nullptr; const wchar_t* n = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
        if (FAILED(WindowsCreateString(n, (UINT32)wcslen(n), &cls)) || FAILED(RoGetActivationFactory(cls, kIID_MgrStatics, (void**)&m.st)) || !m.st) m.disabled = true;
        if (cls) WindowsDeleteString(cls);
        if (m.disabled) return;
    }
    if (!m.mgr) {
        if (!m.mgrOp && FAILED(m.st->RequestAsync(&m.mgrOp))) { m.disabled = true; return; }
        int s = OpStatus(m.mgrOp); if (s == 0) return;
        if (s == 1) m.mgrOp->GetResults((void*)&m.mgr);
        SafeRelease(m.mgrOp); if (!m.mgr) return;
    }
    bool had = m.has;
    NbSession* ses = MediaSession();
    if (!ses) { m.has = false; m.status = 0; if (had) Kick(); return; }
    HSTRING ha = nullptr; ses->get_SourceAppUserModelId(&ha); std::wstring aumid = HStr(ha);
    NbPlayback* pb = nullptr; int status = 0;
    if (SUCCEEDED(ses->GetPlaybackInfo(&pb)) && pb) { pb->get_PlaybackStatus(&status); pb->Release(); }
    NbTimeline* tl = nullptr;
    if (SUCCEEDED(ses->GetTimelineProperties(&tl)) && tl) {
        INT64 st = 0, en = 0, pos = 0, lu = 0; tl->get_StartTime(&st); tl->get_EndTime(&en); tl->get_Position(&pos); tl->get_LastUpdatedTime(&lu); tl->Release();
        m.endTicks = en - st; m.posTicks = pos - st; m.lastUpd = lu;
    }
    if (m.propsOp) {
        int s = OpStatus(m.propsOp);
        if (s != 0) {
            NbMediaProps* pr = nullptr; if (s == 1) m.propsOp->GetResults((void*)&pr); SafeRelease(m.propsOp);
            if (pr) {
                HSTRING t = nullptr, a = nullptr; pr->get_Title(&t); pr->get_Artist(&a);
                std::wstring ti = HStr(t), ar = HStr(a);
                if (ar.empty()) { HSTRING aa = nullptr; pr->get_AlbumArtist(&aa); ar = HStr(aa); }
                if (ti != m.title || ar != m.artist) { m.title = ti; m.artist = ar; MediaSetText(); Kick(); }
                std::wstring key = aumid + L"|" + ti + L"|" + ar;
                if (key != m.artKey) {
                    m.artKey = key; SafeRelease(m.art); SafeRelease(m.artBrush); SafeRelease(m.artBmp); m.tone = 0; SafeRelease(m.thumbOp);
                    NbStreamRef* ref = nullptr;
                    if (SUCCEEDED(pr->get_Thumbnail(&ref)) && ref) { ref->OpenReadAsync(&m.thumbOp); ref->Release(); }
                    Kick();
                }
                pr->Release();
            }
        }
    } else if (aumid != m.aumid || ++m.propsAge >= 4) { m.propsAge = 0; ses->TryGetMediaPropertiesAsync(&m.propsOp); }
    if (m.thumbOp) {
        int s = OpStatus(m.thumbOp);
        if (s != 0) { IUnknown* ras = nullptr; if (s == 1) m.thumbOp->GetResults((void*)&ras); SafeRelease(m.thumbOp); if (ras) { MediaLoadArt(ras); ras->Release(); Kick(); } }
    }
    ses->Release();
    m.aumid = aumid; m.status = status;
    m.has = !m.title.empty() && status != 0;
    float pt = status == 4 ? 1.f : 0.f; if (g_mPlayT.t != pt) { g_mPlayT.t = pt; Kick(); }
    // progress (extrapolated while playing)
    float prog = 0;
    if (m.endTicks > 10000000) {
        INT64 pos = m.posTicks;
        if (status == 4 && m.lastUpd > 0) { FILETIME ft; GetSystemTimeAsFileTime(&ft); INT64 now = ((INT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime; if (now > m.lastUpd) pos += now - m.lastUpd; }
        prog = Clamp01((float)((double)pos / (double)m.endTicks));
    }
    if (fabsf(prog - g_mProg) > 0.003f) { g_mProg = prog; Kick(); }
    if (m.has != had) Kick();
}
static void MediaCmd(int which) {
    NbSession* s = MediaSession();
    if (!s) { SendKeys({(WORD)(which == 0 ? VK_MEDIA_PREV_TRACK : which == 1 ? VK_MEDIA_PLAY_PAUSE : VK_MEDIA_NEXT_TRACK)}); return; }
    NbAsyncOp* op = nullptr;
    if (which == 0) s->TrySkipPreviousAsync(&op); else if (which == 1) s->TryTogglePlayPauseAsync(&op); else s->TrySkipNextAsync(&op);
    SafeRelease(op); s->Release();
    if (which == 1) { g_mPlayT.t = g_mPlayT.t > 0.5f ? 0.f : 1.f; }
    Kick();
}
static void MediaActivate() {
    std::wstring a = Lower(g_media.aumid); if (a.empty()) return;
    std::wstring a2 = a; if (a2.size() > 4 && a2.compare(a2.size() - 4, 4, L".exe") == 0) a2 = a2.substr(0, a2.size() - 4);
    for (auto* it : g_items) {
        if (it->wins.empty()) continue;
        std::wstring base = Lower(it->exe); size_t s0 = base.find_last_of(L"\\/"); if (s0 != std::wstring::npos) base = base.substr(s0 + 1);
        std::wstring b2 = base; if (b2.size() > 4 && b2.compare(b2.size() - 4, 4, L".exe") == 0) b2 = b2.substr(0, b2.size() - 4);
        if (it->key == a || Lower(it->aumid) == a || base == a || b2 == a2 || (!a2.empty() && it->key.find(a2) != std::wstring::npos)) { ActivateWin(it->wins[0]); return; }
    }
}

// ───────────────────────────── app names, keyboard layout, recycle bin ─────────────────────────────
static std::wstring FileDescription(const std::wstring& path) {
    DWORD hnd = 0, sz = GetFileVersionInfoSizeW(path.c_str(), &hnd); if (!sz) return L"";
    std::vector<BYTE> buf(sz); if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return L"";
    struct LangCp { WORD l, c; }* tr = nullptr; UINT n = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &n) || n < sizeof(LangCp)) return L"";
    wchar_t q[80]; swprintf(q, 80, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].l, tr[0].c);
    wchar_t* v = nullptr; UINT vl = 0;
    if (VerQueryValueW(buf.data(), q, (void**)&v, &vl) && v && vl > 1) return std::wstring(v);
    return L"";
}
static void EnsureName(AppItem* it) {
    if (it->nameTried) return; it->nameTried = true;
    if (it->special) { it->name = L"Geri Dönüşüm Kutusu"; return; }
    if (!it->aumid.empty()) {
        IShellItem* si = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName((L"shell:AppsFolder\\" + it->aumid).c_str(), nullptr, __uuidof(IShellItem), (void**)&si)) && si) {
            LPWSTR n = nullptr; if (SUCCEEDED(si->GetDisplayName(SIGDN_NORMALDISPLAY, &n)) && n) { it->name = n; CoTaskMemFree(n); }
            si->Release();
        }
    }
    std::wstring p = !it->exe.empty() ? it->exe : it->target;
    if (it->name.empty() && !p.empty() && Lower(p).find(L".lnk") == std::wstring::npos) it->name = FileDescription(p);
    if (it->name.empty()) {
        size_t s0 = p.find_last_of(L"\\/"); std::wstring b = s0 == std::wstring::npos ? p : p.substr(s0 + 1);
        size_t d = b.rfind(L'.'); if (d != std::wstring::npos && d > 0) b = b.substr(0, d);
        it->name = b.empty() ? it->key : b;
    }
}
static void ReadLang() {
    HKL list[16]; int n = GetKeyboardLayoutList(16, list);
    bool show = g_cfg.lang && n > 1; if (show != g_langShow) { g_langShow = show; Kick(); }
    HWND fg = GetForegroundWindow(); if (!fg || fg == g_hwnd) return;
    HKL k = GetKeyboardLayout(GetWindowThreadProcessId(fg, nullptr)); if (!k) return;
    LANGID lid = LOWORD((UINT_PTR)k); wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(lid, SORT_DEFAULT), loc, LOCALE_NAME_MAX_LENGTH, 0)) {
        wchar_t iso[16] = {}; GetLocaleInfoEx(loc, LOCALE_SISO639LANGNAME, iso, 16);
        std::wstring s = iso; for (auto& c : s) c = (wchar_t)towupper(c);
        if (!s.empty() && s != g_lang) { g_lang = s; Kick(); }
    }
}
static void ReadTrash() {
    SHQUERYRBINFO q = {sizeof(q)}; if (FAILED(SHQueryRecycleBinW(nullptr, &q))) return;
    long long c = q.i64NumItems; bool wasFull = g_trashCount > 0, full = c > 0;
    bool changed = (g_trashCount < 0) || wasFull != full; g_trashCount = c;
    if (changed) for (auto* it : g_items) if (it->special) { SafeRelease(it->wic); SafeRelease(it->bmp); it->iconTried = false; LoadItemIcon(it); Kick(); }
}
static std::wstring ResolveLnk(const std::wstring& lnk) {
    std::wstring r; IShellLinkW* sl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl)) && sl) {
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)) && pf) {
            if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) { wchar_t b[MAX_PATH * 2] = {}; if (SUCCEEDED(sl->GetPath(b, MAX_PATH * 2, nullptr, 0)) && b[0]) r = b; }
            pf->Release();
        }
        sl->Release();
    }
    return r;
}
static bool PinPath(const std::wstring& path) {
    std::wstring key = Lower(path);
    if (key.size() > 4 && key.compare(key.size() - 4, 4, L".lnk") == 0) { std::wstring t = ResolveLnk(path); if (!t.empty()) key = Lower(t); }
    for (auto& p : g_pins) if (p.key == key) return false;
    g_pins.push_back({key, path});
    return true;
}

// ───────────────────────────── Windows taskbar hide / restore ─────────────────────────────
static void ForEachTaskbar(bool show) {
    HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr); if (tb) ShowWindow(tb, show ? SW_SHOWNA : SW_HIDE);
    HWND s = nullptr; while ((s = FindWindowExW(nullptr, s, L"Shell_SecondaryTrayWnd", nullptr))) ShowWindow(s, show ? SW_SHOWNA : SW_HIDE);
}
static void HideWinTaskbar() {
    HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr); if (!tb) return;
    APPBARDATA a = {sizeof(a)}; a.hWnd = tb;
    std::wstring saved = IniGet(L"State", L"OriginalTaskbarState", L"");
    if (saved.empty()) {
        UINT st = (UINT)SHAppBarMessage(ABM_GETSTATE, &a);
        WritePrivateProfileStringW(L"State", L"OriginalTaskbarState", std::to_wstring(st).c_str(), g_iniPath.c_str());
    }
    a.lParam = ABS_AUTOHIDE; SHAppBarMessage(ABM_SETSTATE, &a);
    ForEachTaskbar(false); g_tbHidden = true;
}
static void RestoreWinTaskbar() {
    std::wstring saved = IniGet(L"State", L"OriginalTaskbarState", L"");
    HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (tb) {
        APPBARDATA a = {sizeof(a)}; a.hWnd = tb;
        a.lParam = saved.empty() ? ABS_ALWAYSONTOP : (LPARAM)_wtoi(saved.c_str());
        if (!saved.empty() || g_tbHidden) SHAppBarMessage(ABM_SETSTATE, &a);
    }
    ForEachTaskbar(true);
    WritePrivateProfileStringW(L"State", L"OriginalTaskbarState", nullptr, g_iniPath.c_str());
    g_tbHidden = false;
}
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS*) { if (g_tbHidden) RestoreWinTaskbar(); return EXCEPTION_CONTINUE_SEARCH; }

// ───────────────────────────── input helpers ─────────────────────────────
static void SendKeys(std::initializer_list<WORD> keys) {
    std::vector<INPUT> in;
    for (WORD k : keys) { INPUT i = {}; i.type = INPUT_KEYBOARD; i.ki.wVk = k; if (k == VK_LWIN) i.ki.dwFlags = KEYEVENTF_EXTENDEDKEY; in.push_back(i); }
    for (auto it = keys.end(); it != keys.begin();) { --it; INPUT i = {}; i.type = INPUT_KEYBOARD; i.ki.wVk = *it; i.ki.dwFlags = KEYEVENTF_KEYUP | (*it == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0); in.push_back(i); }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}
static void ActivateWin(HWND w) {
    if (IsIconic(w)) ShowWindowAsync(w, SW_RESTORE);
    if (!SetForegroundWindow(w)) SwitchToThisWindow(w, TRUE);
}
static void Launch(AppItem* it) {
    std::wstring t = !it->target.empty() ? it->target : (!it->aumid.empty() ? L"shell:AppsFolder\\" + it->aumid : it->exe);
    if (t.empty()) return;
    ShellExecuteW(nullptr, L"open", t.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
static void ClickApp(AppItem* it) {
    it->ripple = 0.f; Kick();
    if (it->special) { ShellExecuteW(nullptr, L"open", L"explorer.exe", L"shell:RecycleBinFolder", nullptr, SW_SHOWNORMAL); return; }
    if (it->wins.empty()) { it->bounce = 0.f; Launch(it); return; }
    HWND fg = RootOwner(GetForegroundWindow());
    if (it->wins.size() == 1) {
        HWND w = it->wins[0];
        if (fg == w && !IsIconic(w)) ShowWindowAsync(w, SW_MINIMIZE); else ActivateWin(w);
        return;
    }
    auto f = std::find(it->wins.begin(), it->wins.end(), fg);
    HWND next = f == it->wins.end() ? it->wins[0] : it->wins[((f - it->wins.begin()) + 1) % it->wins.size()];
    ActivateWin(next);
}

// ───────────────────────────── autostart ─────────────────────────────
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static bool AutoStartOn() {
    HKEY k; bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) == ERROR_SUCCESS) { on = RegQueryValueExW(k, L"NovaBar", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS; RegCloseKey(k); }
    return on;
}
static void SetAutoStart(bool on) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &k) != ERROR_SUCCESS) return;
    if (on) { std::wstring v = L"\"" + g_exePath + L"\""; RegSetValueExW(k, L"NovaBar", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2)); }
    else RegDeleteValueW(k, L"NovaBar");
    RegCloseKey(k);
}

// ───────────────────────────── graphics device ─────────────────────────────
static void ReleaseBrushes() { SafeRelease(g_br); SafeRelease(g_acc); SafeRelease(g_sheen); SafeRelease(g_spot); SafeRelease(g_shadow); SafeRelease(g_edge); SafeRelease(g_accGlow); SafeRelease(g_rim); }
static void CreateBrushes(ID2D1RenderTarget* rt) {
    ReleaseBrushes();
    { D2D1_COLOR_F w1 = Col(1, 1, 1, 1); rt->CreateSolidColorBrush(&w1, nullptr, &g_br); }
    if (!g_round) { D2D1_STROKE_STYLE_PROPERTIES sp = {D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.f, D2D1_DASH_STYLE_SOLID, 0.f}; ((ID2D1Factory*)g_d2d)->CreateStrokeStyle(&sp, nullptr, 0, &g_round); }
    ID2D1GradientStopCollection* sc = nullptr;
    D2D1_GRADIENT_STOP st[2] = {{0.f, Hex(g_cfg.accent)}, {1.f, Hex(g_cfg.accent2)}};
    rt->CreateGradientStopCollection(st, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc);
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp = {PT(0, 0), PT(1, 0)};
    if (sc) { rt->CreateLinearGradientBrush(&lp, nullptr, sc, &g_acc); sc->Release(); }
    D2D1_GRADIENT_STOP rm[3] = {{0.f, Col(1, 1, 1, 0.22f)}, {0.5f, Col(1, 1, 1, 0.07f)}, {1.f, Col(1, 1, 1, 0.04f)}};
    sc = nullptr; rt->CreateGradientStopCollection(rm, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc);
    if (sc) { rt->CreateLinearGradientBrush(&lp, nullptr, sc, &g_rim); sc->Release(); }
    D2D1_GRADIENT_STOP s2[2] = {{0.f, Col(1, 1, 1, 0.085f)}, {1.f, Col(1, 1, 1, 0)}};
    sc = nullptr; rt->CreateGradientStopCollection(s2, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc);
    if (sc) { rt->CreateLinearGradientBrush(&lp, nullptr, sc, &g_sheen); sc->Release(); }
    D2D1_GRADIENT_STOP s3[3] = {{0.f, Col(1, 1, 1, 0.09f)}, {0.45f, Col(1, 1, 1, 0.03f)}, {1.f, Col(1, 1, 1, 0)}};
    sc = nullptr; rt->CreateGradientStopCollection(s3, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc);
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp = {PT(0, 0), PT(0, 0), 150.f, 60.f};
    if (sc) { rt->CreateRadialGradientBrush(&rp, nullptr, sc, &g_spot); sc->Release(); }
    auto radial = [&](const D2D1_GRADIENT_STOP* stops, UINT n, ID2D1RadialGradientBrush** out) {
        ID2D1GradientStopCollection* c = nullptr; rt->CreateGradientStopCollection(stops, n, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &c);
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES p = {PT(0, 0), PT(0, 0), 1.f, 1.f};
        if (c) { rt->CreateRadialGradientBrush(&p, nullptr, c, out); c->Release(); }
    };
    D2D1_GRADIENT_STOP sh[3] = {{0.f, Col(0, 0, 0, 0.55f)}, {0.55f, Col(0, 0, 0, 0.2f)}, {1.f, Col(0, 0, 0, 0)}};
    radial(sh, 3, &g_shadow);
    D2D1_GRADIENT_STOP ed[3] = {{0.f, Hex(g_cfg.accent2, 0.95f)}, {0.45f, Hex(g_cfg.accent, 0.45f)}, {1.f, Hex(g_cfg.accent, 0.f)}};
    radial(ed, 3, &g_edge);
    D2D1_GRADIENT_STOP ag[2] = {{0.f, Hex(g_cfg.accent, 0.45f)}, {1.f, Hex(g_cfg.accent2, 0.f)}};
    radial(ag, 2, &g_accGlow);
    g_gen++;
}
static void ReleaseGlass();
static void DiscardDevice() {
    ReleaseBrushes(); ReleaseGlass(); ReleasePopupDevice();
    for (auto* it : g_items) { SafeRelease(it->bmp); SafeRelease(it->glow); }
    SafeRelease(g_target); if (g_dc) g_dc->SetTarget(nullptr);
    SafeRelease(g_visual); SafeRelease(g_dtarget); SafeRelease(g_dcomp);
    SafeRelease(g_dc); SafeRelease(g_d2dDev); SafeRelease(g_swap); SafeRelease(g_d3d);
}
static bool CreateTargetBitmap() {
    SafeRelease(g_target);
    IDXGISurface* surf = nullptr; if (FAILED(g_swap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf))) return false;
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX = bp.dpiY = 96.f * g_scale; bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    HRESULT hr = g_dc->CreateBitmapFromDxgiSurface(surf, &bp, &g_target); surf->Release();
    if (FAILED(hr)) return false;
    g_dc->SetTarget(g_target); g_dc->SetDpi(96.f * g_scale, 96.f * g_scale);
    return true;
}
static bool CreateDevice() {
    if (g_dc) return true;
    D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 5, D3D11_SDK_VERSION, &g_d3d, nullptr, nullptr);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, fl, 5, D3D11_SDK_VERSION, &g_d3d, nullptr, nullptr);
    if (FAILED(hr)) return false;
    IDXGIDevice* dxdev = nullptr; g_d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
    IDXGIAdapter* ad = nullptr; dxdev->GetAdapter(&ad);
    IDXGIFactory2* fac = nullptr; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = std::max(1, g_pxW); sd.Height = std::max(1, g_pxH); sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; sd.Scaling = DXGI_SCALING_STRETCH;
    hr = fac ? fac->CreateSwapChainForComposition(g_d3d, &sd, nullptr, &g_swap) : E_FAIL;
    if (SUCCEEDED(hr)) hr = g_d2d->CreateDevice(dxdev, &g_d2dDev);
    if (SUCCEEDED(hr)) hr = g_d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_dc);
    if (SUCCEEDED(hr)) hr = DCompositionCreateDevice(dxdev, __uuidof(IDCompositionDevice), (void**)&g_dcomp);
    if (SUCCEEDED(hr)) hr = g_dcomp->CreateTargetForHwnd(g_hwnd, TRUE, &g_dtarget);
    if (SUCCEEDED(hr)) hr = g_dcomp->CreateVisual(&g_visual);
    if (SUCCEEDED(hr)) hr = g_visual->SetContent(g_swap);
    if (SUCCEEDED(hr)) hr = g_dtarget->SetRoot(g_visual);
    if (SUCCEEDED(hr)) hr = g_dcomp->Commit();
    SafeRelease(fac); SafeRelease(ad); SafeRelease(dxdev);
    if (FAILED(hr) || !CreateTargetBitmap()) { DiscardDevice(); return false; }
    g_dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    CreateBrushes(g_dc);
    return true;
}
static void ResizeSwap() {
    if (!g_swap) return;
    g_dc->SetTarget(nullptr); SafeRelease(g_target);
    if (FAILED(g_swap->ResizeBuffers(0, std::max(1, g_pxW), std::max(1, g_pxH), DXGI_FORMAT_UNKNOWN, 0)) || !CreateTargetBitmap()) DiscardDevice();
}

// ───────────────────────────── drawing primitives ─────────────────────────────
static void FillRR(ID2D1RenderTarget* rt, D2D1_RECT_F r, float rad, D2D1_COLOR_F c) {
    c.a *= g_alpha; if (c.a <= 0.002f) return; g_br->SetColor(c);
    D2D1_ROUNDED_RECT rr = {r, rad, rad}; rt->FillRoundedRectangle(&rr, g_br);
}
static void StrokeRR(ID2D1RenderTarget* rt, D2D1_RECT_F r, float rad, D2D1_COLOR_F c, float w = 1.f) {
    c.a *= g_alpha; if (c.a <= 0.002f) return; g_br->SetColor(c);
    D2D1_ROUNDED_RECT rr = {r, rad, rad}; rt->DrawRoundedRectangle(&rr, g_br, w, nullptr);
}
static void FillAcc(ID2D1RenderTarget* rt, D2D1_RECT_F r, float rad, float op) {
    if (op * g_alpha <= 0.002f || !g_acc) return;
    g_acc->SetStartPoint(PT(r.left, r.top)); g_acc->SetEndPoint(PT(r.right, r.bottom)); g_acc->SetOpacity(op * g_alpha);
    D2D1_ROUNDED_RECT rr = {r, rad, rad}; rt->FillRoundedRectangle(&rr, g_acc);
}
static void Txt(ID2D1RenderTarget* rt, const std::wstring& s, IDWriteTextFormat* f, D2D1_RECT_F r, D2D1_COLOR_F c) {
    c.a *= g_alpha; if (c.a <= 0.002f || !f) return; g_br->SetColor(c);
    rt->DrawText(s.c_str(), (UINT32)s.size(), f, &r, g_br, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}
static void TxtL(ID2D1RenderTarget* rt, IDWriteTextLayout* tl, float x, float y, D2D1_COLOR_F c) {
    c.a *= g_alpha; if (c.a <= 0.002f || !tl) return; g_br->SetColor(c);
    rt->DrawTextLayout(PT(x, y), tl, g_br, D2D1_DRAW_TEXT_OPTIONS_NONE);
}
static void Bitmap(ID2D1RenderTarget* rt, ID2D1Bitmap* b, D2D1_RECT_F r, float op) {
    if (g_curDC) g_curDC->DrawBitmap(b, &r, op * g_alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
    else rt->DrawBitmap(b, &r, op * g_alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
}

// ───────────────────────────── update ─────────────────────────────
struct Tile {
    HWND w = nullptr; HTHUMBNAIL th = nullptr; D2D1_RECT_F r = {}, area = {}; SIZE src = {0, 0};
    std::wstring title; bool iconic = false; Spring hover, closeH; IDWriteTextLayout* tl = nullptr;
};
static HWND g_pop = nullptr;
static IDXGISwapChain1* g_popSwap = nullptr; static ID2D1Bitmap1* g_popTarget = nullptr;
static IDCompositionTarget* g_popDT = nullptr; static IDCompositionVisual* g_popVis = nullptr;
static int g_popPxW = 0, g_popPxH = 0, g_popSwapW = 0, g_popSwapH = 0;
static float g_popW = 0, g_popH = 0; static bool g_popShown = false, g_popTracking = false;
static std::vector<Tile> g_tiles; static IDWriteTextLayout* g_popLabel = nullptr; static float g_popLabelW = 0, g_popLabelH = 0;
static int g_popHoverTile = -1, g_popPressTile = -1; static bool g_popHoverClose = false;
static const float kTileW = 188.f, kThumbH = 112.f, kPad = 10.f, kGapT = 8.f, kTitleH = 22.f;

static bool UpdateAnims(float dt) {
    bool m = false;
    m |= g_enter.step(dt, 140.f, 20.f, 0.001f);
    m |= g_hStart.ui(dt); m |= g_pStart.ui(dt); m |= g_hStats.ui(dt); m |= g_hQuick.ui(dt); m |= g_pQuick.ui(dt);
    m |= g_hClock.ui(dt); m |= g_pClock.ui(dt); m |= g_hDesk.ui(dt); m |= g_pDesk.ui(dt);
    m |= g_indX.slide(dt); m |= g_indW.slide(dt); m |= g_indA.ui(dt);
    m |= g_mouseX.step(dt, 180.f, 26.f, 0.2f); m |= g_barHover.ui(dt);
    m |= g_volShow.step(dt, 260.f, 28.f, 0.002f); m |= g_volV.ui(dt);
    m |= g_divW.lay(dt); m |= g_div2W.lay(dt);
    m |= g_mW.lay(dt); m |= g_mA.ui(dt); m |= g_mPlayT.step(dt, 320.f, 26.f, 0.002f);
    m |= g_hMInfo.ui(dt); m |= g_hMPrev.ui(dt); m |= g_hMPlay.ui(dt); m |= g_hMNext.ui(dt); m |= g_pMPrev.ui(dt); m |= g_pMPlay.ui(dt); m |= g_pMNext.ui(dt);
    m |= g_hLang.ui(dt); m |= g_pLang.ui(dt); m |= g_dragLift.ui(dt);
    m |= g_popA.step(dt, 420.f, 40.f, 0.002f); m |= g_popY.lay(dt);
    for (auto& t : g_tiles) { m |= t.hover.ui(dt); m |= t.closeH.ui(dt); }
    m |= g_cpuV.step(dt, 90.f, 19.f, 0.002f); m |= g_ramV.step(dt, 90.f, 19.f, 0.002f); m |= g_batV.ui(dt);
    for (size_t i = 0; i < g_items.size();) {
        AppItem* it = g_items[i];
        bool act = it->active && it->alive;
        it->act.t = act ? 1.f : 0.f;
        it->titleA.t = (act && g_cfg.title && it->tl) ? 1.f : 0.f;
        it->w.t = !it->alive ? 0.f : 42.f + ((act && g_cfg.title && it->tl && it->tlW > 1) ? it->tlW + 12.f : 0.f);
        it->hover.t = (g_hover.k == RK_APP && g_hover.it == it) ? 1.f : 0.f;
        it->press.t = (g_press.k == RK_APP && g_press.it == it && g_hover.it == it) ? 1.f : 0.f;
        m |= it->w.lay(dt); m |= it->appear.pop(dt); m |= it->hover.ui(dt); m |= it->press.ui(dt);
        m |= it->act.ui(dt); m |= it->titleA.ui(dt); m |= it->run.ui(dt); m |= it->mag.step(dt, 420.f, 34.f, 0.002f); m |= it->offX.step(dt, 300.f, 30.f, 0.05f);
        if (it->bounce >= 0) {
            it->bounce += dt; m = true;
            bool settle = fabsf(sinf(it->bounce * 3.14159f * 2.4f)) < 0.1f;
            if (it->bounce > 2.4f || (!it->wins.empty() && it->bounce > 0.45f && settle)) it->bounce = -1;
        }
        if (it->ripple >= 0) { it->ripple += dt * 2.4f; if (it->ripple >= 1) it->ripple = -1; m = true; }
        if (!it->alive && it->appear.v < 0.02f && it->w.v < 0.6f) {
            if (g_hover.it == it) g_hover = Hit(); if (g_press.it == it) g_press = Hit();
            if (g_popItem == it) ClosePopupNow(); if (g_hoverPend == it) g_hoverPend = nullptr;
            if (g_dragItem == it) g_dragItem = nullptr; if (g_dragCand == it) g_dragCand = nullptr;
            SafeRelease(it->wic); SafeRelease(it->bmp); SafeRelease(it->tl); SafeRelease(it->glow); delete it; g_items.erase(g_items.begin() + i); m = true; continue;
        }
        i++;
    }
    return m;
}

// ───────────────────────────── scene ─────────────────────────────
// ───────────────────────────── wallpaper-backed glass ─────────────────────────────

static void ReleaseGlass() { SafeRelease(g_wallBrush); SafeRelease(g_wallBmp); SafeRelease(g_noiseBrush); SafeRelease(g_noiseBmp); SafeRelease(g_media.artBrush); SafeRelease(g_media.artBmp); }

static std::wstring WallPath(int& style, bool& tile) {
    wchar_t p[1024] = {}; SystemParametersInfoW(SPI_GETDESKWALLPAPER, 1024, p, 0);
    style = 10; tile = false; HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t v[16] = {}; DWORD n = sizeof(v) - 2;
        if (RegQueryValueExW(k, L"WallpaperStyle", nullptr, nullptr, (BYTE*)v, &n) == ERROR_SUCCESS) style = _wtoi(v);
        wchar_t t[16] = {}; n = sizeof(t) - 2;
        if (RegQueryValueExW(k, L"TileWallpaper", nullptr, nullptr, (BYTE*)t, &n) == ERROR_SUCCESS) tile = _wtoi(t) != 0;
        RegCloseKey(k);
    }
    std::wstring s = p;
    if (s.empty()) return s; // solid colour background
    // TranscodedWallpaper always holds the image currently on screen (also for slideshows)
    wchar_t ad[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH)) {
        std::wstring t = std::wstring(ad) + L"\\Microsoft\\Windows\\Themes\\TranscodedWallpaper";
        if (GetFileAttributesW(t.c_str()) != INVALID_FILE_ATTRIBUTES) return t;
    }
    if (GetFileAttributesW(s.c_str()) == INVALID_FILE_ATTRIBUTES) s.clear();
    return s;
}
static void BoxBlur(std::vector<uint32_t>& px, int w, int h, int r) {
    if (r < 1 || w < 2 || h < 2) return;
    std::vector<uint32_t> tmp(px.size());
    auto pass = [&](const uint32_t* src, uint32_t* dst, int len, int count, int stride, int step) {
        for (int line = 0; line < count; line++) {
            const uint32_t* s = src + line * stride; uint32_t* d = dst + line * stride;
            int sr = 0, sg = 0, sb = 0, n = 2 * r + 1;
            for (int i = -r; i <= r; i++) { uint32_t c = s[std::max(0, std::min(len - 1, i)) * step]; sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; }
            for (int i = 0; i < len; i++) {
                d[i * step] = 0xFF000000u | ((uint32_t)(sr / n) << 16) | ((uint32_t)(sg / n) << 8) | (uint32_t)(sb / n);
                uint32_t a = s[std::min(len - 1, i + r + 1) * step], b = s[std::max(0, i - r) * step];
                sr += (int)((a >> 16) & 255) - (int)((b >> 16) & 255); sg += (int)((a >> 8) & 255) - (int)((b >> 8) & 255); sb += (int)(a & 255) - (int)(b & 255);
            }
        }
    };
    for (int k = 0; k < 3; k++) {
        pass(px.data(), tmp.data(), w, h, w, 1);   // horizontal
        pass(tmp.data(), px.data(), h, w, 1, w);   // vertical
    }
}
static void BuildWallpaper(bool force = false) {
    if (g_winRc.right <= g_winRc.left) return;
    int style; bool tile; std::wstring path = WallPath(style, tile);
    WIN32_FILE_ATTRIBUTE_DATA fa = {}; if (!path.empty()) GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa);
    COLORREF bgc = GetSysColor(COLOR_DESKTOP); uint32_t bg = (GetRValue(bgc) << 16) | (GetGValue(bgc) << 8) | GetBValue(bgc);
    wchar_t kb[256];
    swprintf(kb, 256, L"|%lu|%lu|%lu|%d|%d|%ld,%ld,%ld,%ld|%ld,%ld,%ld,%ld|%.2f|%x", fa.ftLastWriteTime.dwLowDateTime, fa.ftLastWriteTime.dwHighDateTime, fa.nFileSizeLow, style, (int)tile,
             g_winRc.left, g_winRc.top, g_winRc.right, g_winRc.bottom, g_monRc.left, g_monRc.top, g_monRc.right, g_monRc.bottom, g_scale, bg);
    std::wstring key = path + kb;
    if (!force && key == g_wallKey) return;
    g_wallKey = key;
    const int d = 2, pad = (int)(72 * g_scale);
    int winW = g_winRc.right - g_winRc.left, winH = g_winRc.bottom - g_winRc.top;
    int monW = g_monRc.right - g_monRc.left, monH = g_monRc.bottom - g_monRc.top;
    int sw = (winW + 2 * pad) / d + 1, sh = (winH + 2 * pad) / d + 1;
    std::vector<uint32_t> img; int iw2 = 0, ih2 = 0; double ox = 0, oy = 0; bool letterbox = false, tiled = false;
    if (!path.empty()) {
        IWICBitmapDecoder* dec = nullptr;
        if (SUCCEEDED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) && dec) {
            IWICBitmapFrameDecode* fr = nullptr;
            if (SUCCEEDED(dec->GetFrame(0, &fr)) && fr) {
                UINT iw = 0, ih = 0; fr->GetSize(&iw, &ih);
                if (iw && ih) {
                    double sx, sy;
                    if (style == 2) { sx = (double)monW / iw; sy = (double)monH / ih; }
                    else if (style == 6) { sx = sy = std::min((double)monW / iw, (double)monH / ih); letterbox = true; }
                    else if (style == 0) { sx = sy = 1; if (tile) tiled = true; else letterbox = true; }
                    else { sx = sy = std::max((double)monW / iw, (double)monH / ih); }
                    double dw = iw * sx, dh = ih * sy;
                    ox = tiled ? 0 : (monW - dw) / 2; oy = tiled ? 0 : (monH - dh) / 2;
                    iw2 = std::max(1, (int)(dw / d)); ih2 = std::max(1, (int)(dh / d));
                    if ((long long)iw2 * ih2 <= 4096LL * 4096LL) {
                        IWICFormatConverter* cv = nullptr; IWICBitmapScaler* scl = nullptr;
                        g_wic->CreateFormatConverter(&cv); g_wic->CreateBitmapScaler(&scl);
                        if (cv && scl && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
                            SUCCEEDED(scl->Initialize(cv, iw2, ih2, WICBitmapInterpolationModeFant))) {
                            img.resize((size_t)iw2 * ih2);
                            if (FAILED(scl->CopyPixels(nullptr, iw2 * 4, (UINT)(img.size() * 4), (BYTE*)img.data()))) img.clear();
                        }
                        SafeRelease(scl); SafeRelease(cv);
                    }
                }
                fr->Release();
            }
            dec->Release();
        }
    }
    std::vector<uint32_t> buf((size_t)sw * sh);
    double baseX = g_winRc.left - g_monRc.left - pad, baseY = g_winRc.top - g_monRc.top - pad;
    for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
        double mx = baseX + x * d, my = baseY + y * d; uint32_t c = 0xFF000000u | bg;
        if (!img.empty()) {
            double u = (mx - ox) / d, v = (my - oy) / d;
            bool inside = u >= 0 && v >= 0 && u < iw2 && v < ih2, onMon = mx >= 0 && my >= 0 && mx < monW && my < monH;
            if (tiled) { int ui = ((int)floor(u) % iw2 + iw2) % iw2, vi = ((int)floor(v) % ih2 + ih2) % ih2; c = img[(size_t)vi * iw2 + ui] | 0xFF000000u; }
            else if (inside || !letterbox || !onMon) {
                int ui = std::max(0, std::min(iw2 - 1, (int)u)), vi = std::max(0, std::min(ih2 - 1, (int)v));
                c = img[(size_t)vi * iw2 + ui] | 0xFF000000u;
            }
        }
        buf[(size_t)y * sw + x] = c;
    }
    BoxBlur(buf, sw, sh, std::max(3, (int)(10 * g_scale)));
    for (auto& c : buf) { // vibrancy: saturation boost
        float r = (float)((c >> 16) & 255), g = (float)((c >> 8) & 255), b = (float)(c & 255), l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        r = l + (r - l) * 1.45f; g = l + (g - l) * 1.45f; b = l + (b - l) * 1.45f;
        auto cl = [](float v) { return (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
        c = 0xFF000000u | (cl(r) << 16) | (cl(g) << 8) | cl(b);
    }
    g_wallPx.swap(buf); g_wallW = sw; g_wallH = sh; g_wallDown = d; g_wallPad = pad; g_wallDirty = true;
}
static void EnsureGlass(ID2D1RenderTarget* rt) {
    if (!g_wallPx.empty() && (g_wallDirty || g_wallGen != g_gen || !g_wallBrush)) {
        SafeRelease(g_wallBrush); SafeRelease(g_wallBmp);
        D2D1_BITMAP_PROPERTIES bp = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.f * g_scale / g_wallDown, 96.f * g_scale / g_wallDown};
        D2D1_SIZE_U sz = {(UINT32)g_wallW, (UINT32)g_wallH};
        if (SUCCEEDED(rt->CreateBitmap(sz, g_wallPx.data(), g_wallW * 4, &bp, &g_wallBmp)) && g_wallBmp) {
            D2D1_BITMAP_BRUSH_PROPERTIES bbp = {D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR};
            rt->CreateBitmapBrush(g_wallBmp, &bbp, nullptr, &g_wallBrush);
        }
        g_wallGen = g_gen; g_wallDirty = false;
    }
    if (g_noiseGen != g_gen || !g_noiseBrush) {
        SafeRelease(g_noiseBrush); SafeRelease(g_noiseBmp);
        const int N = 128; std::vector<uint32_t> nz(N * N); uint32_t s = 0x9E3779B9u;
        for (auto& p : nz) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; uint32_t a = (s >> 24) % 15; p = (a << 24) | (a << 16) | (a << 8) | a; }
        D2D1_BITMAP_PROPERTIES bp = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.f * g_scale, 96.f * g_scale};
        D2D1_SIZE_U sz = {N, N};
        if (SUCCEEDED(rt->CreateBitmap(sz, nz.data(), N * 4, &bp, &g_noiseBmp)) && g_noiseBmp) {
            D2D1_BITMAP_BRUSH_PROPERTIES bbp = {D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR};
            rt->CreateBitmapBrush(g_noiseBmp, &bbp, nullptr, &g_noiseBrush);
        }
        g_noiseGen = g_gen;
    }
}

// ───────────────────────────── vector glyphs ─────────────────────────────
static void ArcStroke(ID2D1RenderTarget* rt, float cx, float cy, float r, float a0, float a1, float w, ID2D1Brush* b) {
    if (a1 - a0 >= 359.9f) { D2D1_ELLIPSE e = {PT(cx, cy), r, r}; rt->DrawEllipse(&e, b, w, g_round); return; }
    if (a1 - a0 < 0.5f) return;
    ID2D1PathGeometry* g = nullptr; if (FAILED(g_d2d->CreatePathGeometry(&g)) || !g) return;
    ID2D1GeometrySink* s = nullptr;
    if (SUCCEEDED(g->Open(&s)) && s) {
        float r0 = a0 * 3.14159265f / 180.f, r1 = a1 * 3.14159265f / 180.f;
        s->BeginFigure(PT(cx + r * cosf(r0), cy + r * sinf(r0)), D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_ARC_SEGMENT seg = {PT(cx + r * cosf(r1), cy + r * sinf(r1)), {r, r}, 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE, (a1 - a0) > 180.f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL};
        s->AddArc(&seg); s->EndFigure(D2D1_FIGURE_END_OPEN); s->Close(); s->Release();
        rt->DrawGeometry(g, b, w, g_round);
    }
    g->Release();
}
static void Line(ID2D1RenderTarget* rt, float x0, float y0, float x1, float y1, float w, D2D1_COLOR_F c) {
    c.a *= g_alpha; if (c.a < 0.003f) return; g_br->SetColor(c); rt->DrawLine(PT(x0, y0), PT(x1, y1), g_br, w, g_round);
}
static void DrawWifi(ID2D1RenderTarget* rt, float cx, float cy, float a) {
    float by = cy + 5.f; g_br->SetColor(Col(1, 1, 1, a * g_alpha));
    for (int i = 0; i < 3; i++) ArcStroke(rt, cx, by, 3.6f + i * 3.4f, 227.f, 313.f, 1.55f, g_br);
    D2D1_ELLIPSE e = {PT(cx, by), 1.25f, 1.25f}; rt->FillEllipse(&e, g_br);
}
static void DrawGlobe(ID2D1RenderTarget* rt, float cx, float cy, float a) {
    g_br->SetColor(Col(1, 1, 1, a * g_alpha));
    D2D1_ELLIPSE e = {PT(cx, cy), 7.f, 7.f}; rt->DrawEllipse(&e, g_br, 1.4f, g_round);
    D2D1_ELLIPSE m = {PT(cx, cy), 3.f, 7.f}; rt->DrawEllipse(&m, g_br, 1.2f, g_round);
    Line(rt, cx - 7, cy, cx + 7, cy, 1.2f, Col(1, 1, 1, a));
}
static void DrawSpeaker(ID2D1RenderTarget* rt, float cx, float cy, float vol, bool mute, float a) {
    ID2D1PathGeometry* g = nullptr; float x = cx - 4.f;
    if (SUCCEEDED(g_d2d->CreatePathGeometry(&g)) && g) {
        ID2D1GeometrySink* s = nullptr;
        if (SUCCEEDED(g->Open(&s)) && s) {
            s->BeginFigure(PT(x - 4.f, cy - 2.6f), D2D1_FIGURE_BEGIN_FILLED);
            D2D1_POINT_2F pts[5] = {PT(x - 1.2f, cy - 2.6f), PT(x + 2.6f, cy - 6.f), PT(x + 2.6f, cy + 6.f), PT(x - 1.2f, cy + 2.6f), PT(x - 4.f, cy + 2.6f)};
            s->AddLines(pts, 5); s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release();
            g_br->SetColor(Col(1, 1, 1, a * g_alpha)); rt->FillGeometry(g, g_br, nullptr); rt->DrawGeometry(g, g_br, 1.f, g_round);
        }
        g->Release();
    }
    if (mute) { Line(rt, x + 5.5f, cy - 2.8f, x + 11.f, cy + 2.8f, 1.5f, Col(1, 1, 1, a)); Line(rt, x + 11.f, cy - 2.8f, x + 5.5f, cy + 2.8f, 1.5f, Col(1, 1, 1, a)); return; }
    g_br->SetColor(Col(1, 1, 1, a * g_alpha));
    if (vol > 0.01f) ArcStroke(rt, x + 2.6f, cy, 4.2f, -48.f, 48.f, 1.5f, g_br);
    g_br->SetColor(Col(1, 1, 1, a * g_alpha * (vol > 0.34f ? 1.f : 0.28f)));
    ArcStroke(rt, x + 2.6f, cy, 7.6f, -50.f, 50.f, 1.5f, g_br);
    g_br->SetColor(Col(1, 1, 1, a * g_alpha * (vol > 0.67f ? 1.f : 0.28f)));
    ArcStroke(rt, x + 2.6f, cy, 11.f, -48.f, 48.f, 1.5f, g_br);
}
static void DrawBattery(ID2D1RenderTarget* rt, float cx, float cy) {
    D2D1_RECT_F body = RF(cx - 9.5f, cy - 5.2f, cx + 7.5f, cy + 5.2f);
    StrokeRR(rt, body, 2.6f, Col(1, 1, 1, 0.75f), 1.2f);
    FillRR(rt, RF(cx + 8.4f, cy - 2, cx + 9.9f, cy + 2), 0.8f, Col(1, 1, 1, 0.75f));
    float lv = Clamp01(g_batV.v), wFill = (body.right - body.left - 3.6f) * lv;
    D2D1_RECT_F fr = RF(body.left + 1.8f, body.top + 1.8f, body.left + 1.8f + wFill, body.bottom - 1.8f);
    if (wFill > 0.3f) {
        if (g_charging) FillRR(rt, fr, 1.2f, Hex(0x5BE49B));
        else if (g_batt <= 20) FillRR(rt, fr, 1.2f, Hex(0xFF6B6B));
        else FillAcc(rt, fr, 1.2f, 1.f);
    }
    if (g_charging) Txt(rt, L"\xE945", g_fIconS, RF(cx - 9.5f, cy - 6, cx + 7.5f, cy + 6), Col(0.04f, 0.05f, 0.07f, 0.95f));
}
static void Ring(ID2D1RenderTarget* rt, float cx, float cy, float r, float v, float w) {
    g_br->SetColor(Col(1, 1, 1, 0.10f * g_alpha));
    D2D1_ELLIPSE e = {PT(cx, cy), r, r}; rt->DrawEllipse(&e, g_br, w, nullptr);
    v = Clamp01(v); if (v < 0.004f || !g_acc) return;
    g_acc->SetStartPoint(PT(cx - r, cy - r)); g_acc->SetEndPoint(PT(cx + r, cy + r)); g_acc->SetOpacity(g_alpha);
    ArcStroke(rt, cx, cy, r, -90.f, -90.f + 360.f * v, w, g_acc);
}

// ───────────────────────────── islands ─────────────────────────────
static float g_slideY = 0;
static void DrawIsland(ID2D1RenderTarget* rt, D2D1_RECT_F r, float rad, bool shadow) {
    if (r.right - r.left < 2) return;
    if (shadow) for (int i = 1; i <= 4; i++) {
        float e = i * 1.25f;
        FillRR(rt, RF(r.left - e, r.top - e + 2.f, r.right + e, r.bottom + e + 2.f), rad + e, Col(0, 0, 0, 0.05f));
    }
    D2D1_ROUNDED_RECT rr = {r, rad, rad};
    if (g_wallBrush && !g_cfg.solid) {
        float o = g_wallPad / g_scale;
        g_wallBrush->SetTransform(MTrans(-o - g_md, -o - g_md - g_slideY)); g_wallBrush->SetOpacity(g_alpha);
        rt->FillRoundedRectangle(&rr, g_wallBrush);
        FillRR(rt, r, rad, Hex(0x090A0F, 0.26f + g_cfg.tint * 0.55f));
    } else FillRR(rt, r, rad, Hex(0x14151B, 0.95f));
    if (g_noiseBrush) { g_noiseBrush->SetOpacity(g_alpha); rt->FillRoundedRectangle(&rr, g_noiseBrush); }
    if (g_sheen) { g_sheen->SetStartPoint(PT(0, r.top)); g_sheen->SetEndPoint(PT(0, r.top + (r.bottom - r.top) * 0.62f)); g_sheen->SetOpacity(g_alpha); rt->FillRoundedRectangle(&rr, g_sheen); }
    if (g_spot && g_barHover.v > 0.003f) {
        g_spot->SetCenter(PT(g_mouseX.v, (r.top + r.bottom) * 0.5f)); g_spot->SetRadiusX(150.f); g_spot->SetRadiusY((r.bottom - r.top) * 1.3f);
        g_spot->SetOpacity(Clamp01(g_barHover.v) * g_alpha); rt->FillRoundedRectangle(&rr, g_spot);
    }
    D2D1_ROUNDED_RECT ri = {RF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), rad - 0.5f, rad - 0.5f};
    if (g_rim) { g_rim->SetStartPoint(PT(0, r.top)); g_rim->SetEndPoint(PT(0, r.bottom)); g_rim->SetOpacity(g_alpha); rt->DrawRoundedRectangle(&ri, g_rim, 1.f, nullptr); }
    if (g_edge && g_barHover.v > 0.003f) {
        g_edge->SetCenter(PT(g_mouseX.v, g_cfg.top ? r.bottom : r.top)); g_edge->SetRadiusX(200.f); g_edge->SetRadiusY((r.bottom - r.top) * 0.9f);
        g_edge->SetOpacity(Clamp01(g_barHover.v) * g_alpha); rt->DrawRoundedRectangle(&ri, g_edge, 1.2f, nullptr);
    }
}
static void HoverChip(ID2D1RenderTarget* rt, D2D1_RECT_F r, float hv, float pr) {
    float a = 0.07f * hv + 0.05f * pr; if (a < 0.003f) return;
    FillRR(rt, r, 11.f, Col(1, 1, 1, a));
    StrokeRR(rt, RF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 10.5f, Col(1, 1, 1, 0.06f * hv));
}

// ───────────────────────────── scene ─────────────────────────────
static void DrawScene(ID2D1RenderTarget* rt, float Wwin, float Hwin, bool& more) {
    rt->Clear(Col(0, 0, 0, 0));
    EnsureGlass(rt);
    const float md = g_md, W = Wwin - 2 * md, H = std::max(24.f, Hwin - 2 * md);
    float ent = Clamp01(g_enter.v); g_alpha = ent;
    g_slideY = (1.f - ent) * (g_cfg.top ? -12.f : 12.f);
    const D2D1_MATRIX_3X2_F base = MTrans(md, md + g_slideY);
    rt->SetTransform(base);
    const float cy = H * 0.5f, rad = std::min(16.f, H * 0.5f - 6.f);
    const bool islands = g_cfg.islands && md > 0.5f;

    // ── layout: left island
    const float sb = std::min(36.f, H - 10.f);
    g_rStart = RF(6, cy - sb / 2, 6 + sb, cy + sb / 2);
    float leftEnd = g_rStart.right + 6;
    if (g_cfg.stats) { g_rStats = RF(leftEnd, cy - sb / 2, leftEnd + 130, cy + sb / 2); leftEnd = g_rStats.right + 6; }
    else g_rStats = RF(0, 0, 0, 0);
    g_islL = RF(0, 0, leftEnd, H);
    const float gap = islands ? 10.f : 6.f, dockPad = 8.f;
    // ── layout: media island (now playing)
    bool mediaOn = g_cfg.media && g_media.has;
    float mTextW = std::max(60.f, std::min(150.f, std::max(g_media.tW, g_media.aW)));
    float mTarget = mediaOn ? (7.f + 34.f + 9.f + mTextW + 10.f + 86.f + 4.f) : 0.f;
    if (fabsf(g_mW.t - mTarget) > 0.1f) { g_mW.t = mTarget; more = true; }
    if (g_mA.t != (mediaOn ? 1.f : 0.f)) { g_mA.t = mediaOn ? 1.f : 0.f; more = true; }
    float mw = std::max(0.f, g_mW.v);
    g_islM = RF(g_islL.right + gap, 0, g_islL.right + gap + mw, H);
    float leftBound = mw > 1.f ? g_islM.right : g_islL.right;

    // ── layout: right island (from the right edge)
    g_rDesk = RF(W - 16, cy - sb / 2, W - 5, cy + sb / 2);
    g_rClock = RF(g_rDesk.left - 3 - g_clockW, cy - sb / 2, g_rDesk.left - 3, cy + sb / 2);
    float vsRaw = std::max(0.f, g_volShow.v);
    float qw = 10.f + 26.f + 28.f + 76.f * vsRaw + (g_hasBatt ? 26.f + 30.f : 0.f) + 6.f;
    g_rQuick = RF(g_rClock.left - 7 - qw, cy - sb / 2, g_rClock.left - 7, cy + sb / 2);
    if (g_langShow && !g_lang.empty()) g_rLang = RF(g_rQuick.left - 4 - 32, cy - sb / 2, g_rQuick.left - 4, cy + sb / 2);
    else g_rLang = RF(g_rQuick.left, cy, g_rQuick.left, cy);
    g_islR = RF(g_rLang.left - 6, 0, W, H);

    // ── layout: dock (magnification)
    const float maxIcon = islands ? std::min(40.f, H - 8.f) : std::min(34.f, H - 15.f), maxS = maxIcon / 24.f, dotY = cy + 16.f;
    // divider between pinned and other apps
    bool hasPinned = false, hasOther = false;
    for (auto* it : g_items) { if (!it->alive) continue; if (it->pinned) hasPinned = true; else hasOther = true; }
    float divT = (hasPinned && hasOther) ? 13.f : 0.f; if (g_divW.t != divT) { g_divW.t = divT; more = true; }
    float divW = std::max(0.f, g_divW.v);
    AppItem* lastPinned = nullptr; for (auto* it : g_items) if (it->pinned && it->alive) lastPinned = it;
    // second divider in front of the recycle bin when unpinned apps are running
    AppItem* trashIt = nullptr; bool runOther = false;
    for (auto* it : g_items) { if (!it->alive) continue; if (it->special) trashIt = it; else if (!it->pinned) runOther = true; }
    float div2T = (trashIt && runOther) ? 13.f : 0.f; if (g_div2W.t != div2T) { g_div2W.t = div2T; more = true; }
    float div2W = std::max(0.f, g_div2W.v);
    float restTotal = divW + div2W; for (auto* it : g_items) restTotal += std::max(0.f, it->w.v);
    float minX = leftBound + gap + dockPad, maxX = g_islR.left - gap - dockPad;
    {
        float rx = g_cfg.center ? W * 0.5f - restTotal * 0.5f : minX;
        rx = std::max(minX, std::min(rx, maxX - restTotal));
        for (auto* it : g_items) {
            if (it->special && trashIt == it) rx += div2W;
            float w = std::max(0.f, it->w.v);
            float c = rx + std::min(21.f, w * 0.5f), tgt = 0.f;
            if (g_mouseIn && it->alive) { float dd = (g_mouseRawX - c) / 46.f; tgt = expf(-dd * dd); if (tgt < 0.03f) tgt = 0.f; }
            if (fabsf(it->mag.t - tgt) > 0.001f) { it->mag.t = tgt; more = true; }
            rx += w; if (it == lastPinned) rx += divW;
        }
    }
    float total = divW + div2W;
    for (auto* it : g_items) total += std::max(0.f, it->w.v) + 16.f * std::max(0.f, it->mag.v) * Clamp01(it->appear.v);
    float x0 = g_cfg.center ? W * 0.5f - total * 0.5f : minX;
    x0 = std::max(minX, std::min(x0, maxX - total));
    g_islC = RF(x0 - dockPad, 0, x0 + total + dockPad, H);

    // ── backgrounds
    if (islands) {
        DrawIsland(rt, g_islL, rad, true);
        if (mw > 2.f) DrawIsland(rt, g_islM, rad, true);
        if (total > 2.f) DrawIsland(rt, g_islC, rad, true);
        DrawIsland(rt, g_islR, rad, true);
    } else DrawIsland(rt, RF(0, 0, W, H), md > 0.5f ? rad : 0.f, md > 0.5f);

    // ── start button
    HoverChip(rt, g_rStart, g_hStart.v, g_pStart.v);
    if (g_accGlow && g_hStart.v > 0.01f) {
        float gx = (g_rStart.left + g_rStart.right) / 2;
        g_accGlow->SetCenter(PT(gx, cy)); g_accGlow->SetRadiusX(24.f); g_accGlow->SetRadiusY(24.f); g_accGlow->SetOpacity(Clamp01(g_hStart.v) * g_alpha);
        D2D1_ELLIPSE e = {PT(gx, cy), 24.f, 24.f}; rt->FillEllipse(&e, g_accGlow);
    }
    {
        float lx = (g_rStart.left + g_rStart.right) / 2, ly = cy, h = g_hStart.v, p = g_pStart.v;
        rt->SetTransform(MMul(MRotScale(45.f * h, 1.f - 0.12f * p, lx, ly), base));
        float s = 7.f, gp = 1.8f + 1.5f * h, o = gp / 2 + s / 2;
        for (int i = 0; i < 4; i++) {
            float ox = (i & 1) ? o : -o, oy = (i & 2) ? o : -o;
            D2D1_RECT_F q = RF(lx + ox - s / 2, ly + oy - s / 2, lx + ox + s / 2, ly + oy + s / 2);
            if (g_acc) {
                g_acc->SetStartPoint(PT(lx - o - s / 2, ly - o - s / 2)); g_acc->SetEndPoint(PT(lx + o + s / 2, ly + o + s / 2));
                g_acc->SetOpacity(g_alpha); D2D1_ROUNDED_RECT rr = {q, 2.1f, 2.1f}; rt->FillRoundedRectangle(&rr, g_acc);
            }
        }
        rt->SetTransform(base);
    }

    // ── stats rings
    if (g_cfg.stats) {
        HoverChip(rt, g_rStats, g_hStats.v, 0.f);
        const wchar_t* lab[2] = {L"CPU", L"RAM"}; float val[2] = {g_cpuV.v, g_ramV.v};
        for (int i = 0; i < 2; i++) {
            float sx = g_rStats.left + 6 + i * 60.f, rcx = sx + 11.5f;
            Ring(rt, rcx, cy, 9.5f, val[i], 2.6f);
            Txt(rt, lab[i], g_fSmall, RF(sx + 26, cy - 13, sx + 60, cy), Col(1, 1, 1, 0.48f));
            wchar_t pc[16]; swprintf(pc, 16, L"%d%%", (int)lroundf(Clamp01(val[i]) * 100));
            Txt(rt, pc, g_fStatV, RF(sx + 26, cy - 1, sx + 62, cy + 13), Col(1, 1, 1, 0.95f));
        }
    }

    // ── media island content
    if (mw > 2.f) {
        auto& m = g_media; float ma = Clamp01(g_mA.v), L = g_islM.left;
        rt->PushAxisAlignedClip(RF(g_islM.left, -g_md, g_islM.right, H + g_md), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float sa = g_alpha; g_alpha *= ma;
        g_rMInfo = RF(L + 4, cy - sb / 2, L + 50 + mTextW + 4, cy + sb / 2);
        HoverChip(rt, g_rMInfo, g_hMInfo.v, 0.f);
        D2D1_RECT_F art = RF(L + 8, cy - 17, L + 42, cy + 17);
        uint32_t tone = m.tone ? m.tone : g_cfg.accent;
        if (g_mPlayT.v > 0.02f && g_accGlow) { // breathing glow behind the cover while playing
            D2D1_GRADIENT_STOP gs[2] = {{0.f, Hex(tone, 0.55f)}, {1.f, Hex(tone, 0.f)}};
            static ID2D1RadialGradientBrush* gb = nullptr; static uint32_t gbTone = 0; static int gbGen = -1;
            if (!gb || gbTone != tone || gbGen != g_gen) {
                SafeRelease(gb); ID2D1GradientStopCollection* sc = nullptr; rt->CreateGradientStopCollection(gs, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc);
                D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp = {PT(0, 0), PT(0, 0), 1.f, 1.f};
                if (sc) { rt->CreateRadialGradientBrush(&rp, nullptr, sc, &gb); sc->Release(); } gbTone = tone; gbGen = g_gen;
            }
            if (gb) {
                float acx = (art.left + art.right) / 2;
                gb->SetCenter(PT(acx, cy)); gb->SetRadiusX(30.f); gb->SetRadiusY(26.f); gb->SetOpacity(Clamp01(g_mPlayT.v) * g_alpha);
                D2D1_ELLIPSE e = {PT(acx, cy), 30.f, 26.f}; rt->FillEllipse(&e, gb);
            }
        }
        if (m.art && (m.artGen != g_gen || !m.artBrush)) {
            SafeRelease(m.artBrush); SafeRelease(m.artBmp);
            if (SUCCEEDED(rt->CreateBitmapFromWicBitmap(m.art, nullptr, &m.artBmp)) && m.artBmp) {
                D2D1_BITMAP_BRUSH_PROPERTIES bbp = {D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR};
                rt->CreateBitmapBrush(m.artBmp, &bbp, nullptr, &m.artBrush);
            }
            m.artGen = g_gen;
        }
        D2D1_ROUNDED_RECT arr = {art, 9.f, 9.f};
        if (m.artBrush && m.artBmp) {
            D2D1_SIZE_F bs = m.artBmp->GetSize(); float k = (art.right - art.left) / std::max(1.f, bs.width);
            m.artBrush->SetTransform(MMul(MRotScale(0, k, 0, 0), MTrans(art.left, art.top))); m.artBrush->SetOpacity(g_alpha);
            rt->FillRoundedRectangle(&arr, m.artBrush);
        } else {
            FillAcc(rt, art, 9.f, 1.f);
            float nx = (art.left + art.right) / 2 - 2, ny = cy + 4; // music note
            g_br->SetColor(Col(1, 1, 1, 0.95f * g_alpha)); D2D1_ELLIPSE e = {PT(nx - 2.5f, ny), 3.2f, 2.6f}; rt->FillEllipse(&e, g_br);
            Line(rt, nx + 0.6f, ny, nx + 0.6f, ny - 10.f, 1.6f, Col(1, 1, 1, 0.95f)); Line(rt, nx + 0.6f, ny - 10.f, nx + 5.f, ny - 8.f, 1.6f, Col(1, 1, 1, 0.95f));
        }
        StrokeRR(rt, RF(art.left + 0.5f, art.top + 0.5f, art.right - 0.5f, art.bottom - 0.5f), 8.5f, Col(1, 1, 1, 0.14f));
        float tx = L + 50;
        rt->PushAxisAlignedClip(RF(tx, 0, tx + mTextW, H), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (m.tlT) TxtL(rt, m.tlT, tx, cy - m.tH + 1.f, Col(1, 1, 1, 0.96f));
        if (m.tlA) TxtL(rt, m.tlA, tx, cy + 1.f, Col(1, 1, 1, 0.55f));
        rt->PopAxisAlignedClip();
        float cx0 = tx + mTextW + 10;
        g_rMPrev = RF(cx0, cy - 15, cx0 + 28, cy + 15); g_rMPlay = RF(cx0 + 29, cy - 15, cx0 + 57, cy + 15); g_rMNext = RF(cx0 + 58, cy - 15, cx0 + 86, cy + 15);
        auto chip = [&](D2D1_RECT_F r, float hv, float pr) {
            float a = 0.09f * hv + 0.06f * pr; if (a < 0.003f) return;
            g_br->SetColor(Col(1, 1, 1, a * g_alpha)); D2D1_ELLIPSE e = {PT((r.left + r.right) / 2, cy), 14.f, 14.f}; rt->FillEllipse(&e, g_br);
        };
        chip(g_rMPrev, g_hMPrev.v, g_pMPrev.v); chip(g_rMPlay, g_hMPlay.v, g_pMPlay.v); chip(g_rMNext, g_hMNext.v, g_pMNext.v);
        auto poly = [&](const D2D1_POINT_2F* p, int n, float a) {
            ID2D1PathGeometry* g = nullptr; if (FAILED(g_d2d->CreatePathGeometry(&g)) || !g) return;
            ID2D1GeometrySink* sk = nullptr;
            if (SUCCEEDED(g->Open(&sk)) && sk) {
                sk->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED); sk->AddLines(p + 1, n - 1); sk->EndFigure(D2D1_FIGURE_END_CLOSED); sk->Close(); sk->Release();
                g_br->SetColor(Col(1, 1, 1, a * g_alpha)); rt->FillGeometry(g, g_br, nullptr); rt->DrawGeometry(g, g_br, 1.f, g_round);
            }
            g->Release();
        };
        { // previous
            float c = (g_rMPrev.left + g_rMPrev.right) / 2, k = 1.f - 0.12f * g_pMPrev.v;
            D2D1_POINT_2F tri[3] = {PT(c + 4.5f * k, cy - 5 * k), PT(c - 2.5f * k, cy), PT(c + 4.5f * k, cy + 5 * k)}; poly(tri, 3, 0.92f);
            FillRR(rt, RF(c - 5.f * k, cy - 5 * k, c - 3.2f * k, cy + 5 * k), 0.9f, Col(1, 1, 1, 0.92f));
        }
        { // next
            float c = (g_rMNext.left + g_rMNext.right) / 2, k = 1.f - 0.12f * g_pMNext.v;
            D2D1_POINT_2F tri[3] = {PT(c - 4.5f * k, cy - 5 * k), PT(c + 2.5f * k, cy), PT(c - 4.5f * k, cy + 5 * k)}; poly(tri, 3, 0.92f);
            FillRR(rt, RF(c + 3.2f * k, cy - 5 * k, c + 5.f * k, cy + 5 * k), 0.9f, Col(1, 1, 1, 0.92f));
        }
        { // play ⇄ pause morph + progress ring
            float c = (g_rMPlay.left + g_rMPlay.right) / 2, t = Clamp01(g_mPlayT.v), k = 1.f - 0.12f * g_pMPlay.v;
            g_br->SetColor(Col(1, 1, 1, 0.10f * g_alpha)); D2D1_ELLIPSE e = {PT(c, cy), 13.f, 13.f}; rt->DrawEllipse(&e, g_br, 2.f, nullptr);
            if (g_mProg > 0.002f) { g_br->SetColor(Hex(tone, 0.95f * g_alpha)); ArcStroke(rt, c, cy, 13.f, -90.f, -90.f + 360.f * g_mProg, 2.f, g_br); }
            auto L2 = [&](float ax, float ay, float bx, float by) { return PT(c + (ax + (bx - ax) * t) * k, cy + (ay + (by - ay) * t) * k); };
            D2D1_POINT_2F q1[4] = {L2(-3.5f, -6, -4.5f, -5.5f), L2(1.5f, -3, -1.3f, -5.5f), L2(1.5f, 3, -1.3f, 5.5f), L2(-3.5f, 6, -4.5f, 5.5f)};
            D2D1_POINT_2F q2[4] = {L2(1.5f, -3, 1.3f, -5.5f), L2(6.5f, 0, 4.5f, -5.5f), L2(6.5f, 0, 4.5f, 5.5f), L2(1.5f, 3, 1.3f, 5.5f)};
            poly(q1, 4, 0.95f); poly(q2, 4, 0.95f);
        }
        g_alpha = sa;
        rt->PopAxisAlignedClip();
    } else { g_rMInfo = g_rMPrev = g_rMPlay = g_rMNext = RF(0, 0, 0, 0); }

    // ── keyboard layout chip
    if (g_langShow && !g_lang.empty()) {
        HoverChip(rt, g_rLang, g_hLang.v, g_pLang.v);
        Txt(rt, g_lang, g_fStatC, RF(g_rLang.left, cy - 8, g_rLang.right, cy + 8), Col(1, 1, 1, 0.88f));
        Line(rt, g_rLang.right + 2.f, cy - 9, g_rLang.right + 2.f, cy + 9, 1.f, Col(1, 1, 1, 0.09f));
    }

    // ── right island: quick settings, clock, show desktop
    HoverChip(rt, g_rQuick, g_hQuick.v, g_pQuick.v);
    {
        float x = g_rQuick.left + 10;
        if (g_net == 2) DrawGlobe(rt, x + 13, cy, 0.92f); else DrawWifi(rt, x + 13, cy, g_net ? 0.92f : 0.3f);
        x += 26;
        DrawSpeaker(rt, x + 14, cy, g_vol, g_mute, 0.92f);
        x += 28;
        if (vsRaw > 0.003f) {
            float ew = 76.f * vsRaw, a = Clamp01(g_volShow.v);
            rt->PushAxisAlignedClip(RF(x, cy - 14, x + ew, cy + 14), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            D2D1_RECT_F tr = RF(x + 4, cy - 2, x + 48, cy + 2);
            FillRR(rt, tr, 2.f, Col(1, 1, 1, 0.13f * a));
            float fw = 44.f * Clamp01(g_volV.v); if (fw > 0.5f) FillAcc(rt, RF(tr.left, tr.top, tr.left + fw, tr.bottom), 2.f, a);
            float kx = tr.left + fw;
            FillRR(rt, RF(kx - 5, cy - 5, kx + 5, cy + 5), 5.f, Col(0, 0, 0, 0.25f * a));
            FillRR(rt, RF(kx - 4.5f, cy - 4.5f, kx + 4.5f, cy + 4.5f), 4.5f, Col(1, 1, 1, 0.97f * a));
            wchar_t pc[8]; swprintf(pc, 8, L"%d", (int)lroundf(Clamp01(g_vol) * 100));
            Txt(rt, pc, g_fStatV, RF(x + 52, cy - 8, x + 76, cy + 8), Col(1, 1, 1, 0.9f * a));
            rt->PopAxisAlignedClip();
            x += ew;
        }
        if (g_hasBatt) {
            DrawBattery(rt, x + 13, cy); x += 26;
            wchar_t pc[8]; swprintf(pc, 8, L"%d%%", g_batt);
            Txt(rt, pc, g_fSmall, RF(x, cy - 8, x + 30, cy + 8), Col(1, 1, 1, 0.82f));
        }
    }
    Line(rt, g_rClock.left - 3.5f, cy - 9, g_rClock.left - 3.5f, cy + 9, 1.f, Col(1, 1, 1, 0.09f));
    HoverChip(rt, g_rClock, g_hClock.v, g_pClock.v);
    {
        float cw = g_rClock.right - g_rClock.left;
        if (g_tlTime) { g_tlTime->SetMaxWidth(cw); TxtL(rt, g_tlTime, g_rClock.left, cy - g_timeH + 1.5f, Col(1, 1, 1, 0.97f)); }
        if (g_tlDate) { g_tlDate->SetMaxWidth(cw); TxtL(rt, g_tlDate, g_rClock.left, cy + 1.f, Col(1, 1, 1, 0.52f)); }
    }
    HoverChip(rt, g_rDesk, g_hDesk.v, g_pDesk.v);
    Line(rt, (g_rDesk.left + g_rDesk.right) / 2, cy - 8, (g_rDesk.left + g_rDesk.right) / 2, cy + 8, 1.2f, Col(1, 1, 1, 0.2f + 0.3f * g_hDesk.v));

    // ── dock items
    g_dragSwapPending = false;
    float x = x0;
    AppItem* activeIt = nullptr;
    for (auto* it : g_items) {
        if (it == trashIt && div2W > 0.3f) {
            Line(rt, x + div2W / 2, cy - 10, x + div2W / 2, cy + 10, 1.f, Col(1, 1, 1, 0.13f * Clamp01(div2W / 13.f)));
            x += div2W;
        }
        float op = Clamp01(it->appear.v), mg = std::max(0.f, it->mag.v);
        float w = std::max(0.f, it->w.v) + 16.f * mg * op;
        it->x = x; it->dw = w;
        if (w > 0.5f) {
            float pr = it->press.v, ac = it->act.v;
            float bounceY = it->bounce >= 0 ? -fabsf(sinf(it->bounce * 3.14159f * 2.4f)) * 7.f * expf(-1.4f * it->bounce) : 0.f;
            float slotX = x + std::min(21.f, w * 0.5f) + 8.f * mg * op;
            it->icx = slotX;
            float icx = slotX + it->offX.v;
            bool dragged = it == g_dragItem; float lift = dragged ? Clamp01(g_dragLift.v) : 0.f;
            if (dragged) icx = g_dragX;
            float sc = (0.55f + 0.45f * std::min(1.15f, it->appear.v)) * (1.f + (maxS - 1.f) * mg) * (1.f - 0.1f * pr) * (1.f + 0.12f * lift);
            float sz = 24.f * sc;
            float icy = cy - 1.f - (islands ? 9.f : 3.2f) * mg + 0.6f * pr + bounceY - 5.f * lift;
            if (!islands && icy + sz / 2 > dotY - 2.f) icy = dotY - 2.f - sz / 2;
            if (ac > 0.01f && !dragged) {
                D2D1_RECT_F pill = RF(x + 2, cy - 18, x + w - 2, cy + 18);
                FillRR(rt, pill, 11.f, Hex(it->tone, 0.17f * ac * op));
                FillRR(rt, RF(pill.left, pill.top, pill.right, cy), 11.f, Col(1, 1, 1, 0.04f * ac * op));
                StrokeRR(rt, RF(pill.left + 0.5f, pill.top + 0.5f, pill.right - 0.5f, pill.bottom - 0.5f), 10.5f, Hex(it->tone, 0.32f * ac * op));
            }
            if (mg > 0.02f || pr > 0.02f) {
                if (it->glowGen != g_gen || it->glowTone != it->tone) {
                    SafeRelease(it->glow);
                    D2D1_GRADIENT_STOP gs[3] = {{0.f, Hex(it->tone, 0.6f)}, {0.5f, Hex(it->tone, 0.2f)}, {1.f, Hex(it->tone, 0.f)}};
                    ID2D1GradientStopCollection* sc2 = nullptr; rt->CreateGradientStopCollection(gs, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &sc2);
                    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES rp = {PT(0, 0), PT(0, 0), 1.f, 1.f};
                    if (sc2) { rt->CreateRadialGradientBrush(&rp, nullptr, sc2, &it->glow); sc2->Release(); }
                    it->glowGen = g_gen; it->glowTone = it->tone;
                }
                if (it->glow) {
                    float gr = sz * 0.9f;
                    it->glow->SetCenter(PT(icx, icy + 3.f)); it->glow->SetRadiusX(gr * 1.3f); it->glow->SetRadiusY(gr);
                    it->glow->SetOpacity(std::min(1.f, 0.8f * mg + 0.4f * pr) * op * g_alpha);
                    D2D1_ELLIPSE e = {PT(icx, icy + 3.f), gr * 1.3f, gr}; rt->FillEllipse(&e, it->glow);
                }
            }
            if (g_shadow) {
                float sy = icy + sz * 0.47f + 1.5f * mg, rxs = sz * 0.40f, rys = sz * 0.11f + 1.2f;
                g_shadow->SetCenter(PT(icx, sy)); g_shadow->SetRadiusX(rxs); g_shadow->SetRadiusY(rys);
                g_shadow->SetOpacity((0.32f + 0.4f * mg) * op * g_alpha);
                D2D1_ELLIPSE e = {PT(icx, sy), rxs, rys}; rt->FillEllipse(&e, g_shadow);
            }
            if (it->ripple >= 0) {
                float rr = 8.f + 26.f * it->ripple; g_br->SetColor(Hex(it->tone, 0.3f * (1.f - it->ripple) * g_alpha));
                D2D1_ELLIPSE e = {PT(icx, icy), rr, rr}; rt->FillEllipse(&e, g_br);
            }
            if (it->wic && it->bmpGen != g_gen) { SafeRelease(it->bmp); rt->CreateBitmapFromWicBitmap(it->wic, nullptr, &it->bmp); it->bmpGen = g_gen; }
            D2D1_RECT_F q = RF(icx - sz / 2, icy - sz / 2, icx + sz / 2, icy + sz / 2);
            if (it->bmp) Bitmap(rt, it->bmp, q, op * (it->wins.empty() && mg < 0.5f ? 0.86f : 1.f));
            else {
                FillRR(rt, q, 7.f * sc, Hex(it->tone, 0.95f * op));
                FillRR(rt, RF(q.left, q.top, q.right, (q.top + q.bottom) / 2), 7.f * sc, Col(1, 1, 1, 0.12f * op));
                std::wstring nm = it->exe.empty() ? it->key : it->exe;
                size_t s0 = nm.find_last_of(L"\\/"); std::wstring bn = s0 == std::wstring::npos ? nm : nm.substr(s0 + 1);
                wchar_t L0[2] = {bn.empty() ? L'?' : (wchar_t)towupper(bn[0]), 0};
                rt->SetTransform(MMul(MRotScale(0, sc, icx, icy), base));
                Txt(rt, L0, g_fLetter, RF(icx - 12, icy - 12, icx + 12, icy + 12), Col(1, 1, 1, op));
                rt->SetTransform(base);
            }
            if (it->titleA.v > 0.01f && it->tl) {
                float ta = Clamp01(it->titleA.v), tx0 = x + 38.f + 16.f * mg * op;
                rt->PushAxisAlignedClip(RF(tx0 - 8, 0, x + w - 8, H), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                TxtL(rt, it->tl, tx0 - 6.f * (1.f - ta), cy - it->tlH / 2 - 0.5f, Col(1, 1, 1, 0.95f * ta * op));
                rt->PopAxisAlignedClip();
            }
            float rv = it->run.v * (1.f - ac) * op * (1.f - lift);
            if (rv > 0.01f) {
                int n = it->wins.size() > 1 ? 2 : 1;
                for (int d = 0; d < n; d++) {
                    float dx = slotX + (n == 2 ? (d ? 3.f : -3.f) : 0.f);
                    FillRR(rt, RF(dx - 2.f, dotY - 1.3f, dx + 2.f, dotY + 1.3f), 1.3f, Col(1, 1, 1, (0.45f + 0.35f * mg) * rv));
                }
            }
            if (it->active && it->alive) activeIt = it;
        }
        x += w;
        if (it == lastPinned && divW > 0.3f) {
            Line(rt, x + divW / 2, cy - 10, x + divW / 2, cy + 10, 1.f, Col(1, 1, 1, 0.13f * Clamp01(divW / 13.f)));
            x += divW;
        }
    }
    // sliding active indicator
    if (activeIt) {
        float tx = activeIt->icx, tw = activeIt->wins.size() > 1 ? 20.f : 16.f;
        if (g_indA.v < 0.02f) { g_indX.snap(tx); g_indW.v = 4; }
        if (fabsf(g_indX.t - tx) > 0.01f || g_indW.t != tw || g_indA.t != 1.f) { g_indX.t = tx; g_indW.t = tw; g_indA.t = 1; more = true; }
    } else if (g_indA.t != 0) { g_indA.t = 0; more = true; }
    if (g_indA.v > 0.01f) {
        float a = Clamp01(g_indA.v), ix = g_indX.v, iw = g_indW.v * (0.4f + 0.6f * a), iy = dotY;
        iw += std::min(18.f, fabsf(g_indX.vel) * 0.02f);
        FillAcc(rt, RF(ix - iw / 2 - 4, iy - 4.5f, ix + iw / 2 + 4, iy + 4.5f), 4.5f, 0.10f * a);
        FillAcc(rt, RF(ix - iw / 2 - 2, iy - 3.f, ix + iw / 2 + 2, iy + 3.f), 3.f, 0.2f * a);
        FillAcc(rt, RF(ix - iw / 2, iy - 1.5f, ix + iw / 2, iy + 1.5f), 1.5f, a);
    }
    rt->SetTransform(MIdent());
    g_alpha = 1.f;
}


// ───────────────────────────── window preview popup (live DWM thumbnails) ─────────────────────────────

static void ReleasePopupDevice() {
    SafeRelease(g_popTarget); SafeRelease(g_popVis); SafeRelease(g_popDT); SafeRelease(g_popSwap); g_popSwapW = g_popSwapH = 0;
}
static bool EnsurePopupDevice() {
    if (!g_dc || !g_pop || !g_d3d || !g_dcomp || g_popPxW < 1 || g_popPxH < 1) return false;
    if (!g_popSwap) {
        IDXGIDevice* dx = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* fac = nullptr;
        g_d3d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dx);
        if (dx) dx->GetAdapter(&ad); if (ad) ad->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = g_popPxW; sd.Height = g_popPxH; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; sd.Scaling = DXGI_SCALING_STRETCH;
        HRESULT hr = fac ? fac->CreateSwapChainForComposition(g_d3d, &sd, nullptr, &g_popSwap) : E_FAIL;
        if (SUCCEEDED(hr)) hr = g_dcomp->CreateTargetForHwnd(g_pop, TRUE, &g_popDT);
        if (SUCCEEDED(hr)) hr = g_dcomp->CreateVisual(&g_popVis);
        if (SUCCEEDED(hr)) hr = g_popVis->SetContent(g_popSwap);
        if (SUCCEEDED(hr)) hr = g_popDT->SetRoot(g_popVis);
        if (SUCCEEDED(hr)) hr = g_dcomp->Commit();
        SafeRelease(fac); SafeRelease(ad); SafeRelease(dx);
        if (FAILED(hr)) { ReleasePopupDevice(); return false; }
        g_popSwapW = g_popPxW; g_popSwapH = g_popPxH;
    } else if (g_popSwapW != g_popPxW || g_popSwapH != g_popPxH) {
        SafeRelease(g_popTarget);
        if (FAILED(g_popSwap->ResizeBuffers(0, g_popPxW, g_popPxH, DXGI_FORMAT_UNKNOWN, 0))) { ReleasePopupDevice(); return false; }
        g_popSwapW = g_popPxW; g_popSwapH = g_popPxH;
    }
    if (!g_popTarget) {
        IDXGISurface* surf = nullptr; if (FAILED(g_popSwap->GetBuffer(0, __uuidof(IDXGISurface), (void**)&surf)) || !surf) return false;
        D2D1_BITMAP_PROPERTIES1 bp = {};
        bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        bp.dpiX = bp.dpiY = 96.f * g_scale; bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        g_dc->CreateBitmapFromDxgiSurface(surf, &bp, &g_popTarget); surf->Release();
    }
    return g_popTarget != nullptr;
}
static void UnregisterTiles() {
    for (auto& t : g_tiles) { if (t.th) DwmUnregisterThumbnail(t.th); SafeRelease(t.tl); }
    g_tiles.clear(); SafeRelease(g_popLabel); g_popHoverTile = g_popPressTile = -1; g_popHoverClose = false;
}
static D2D1_RECT_F TileClose(const Tile& t) { return RF(t.r.right - 24, t.r.top + 4, t.r.right - 4, t.r.top + 24); }
static void OpenPopup(AppItem* it) {
    if (!g_cfg.previews || !it || !it->alive || g_dragItem || !g_pop || g_fullscreen) return;
    bool wasShown = g_popShown && g_popA.t > 0.5f;
    UnregisterTiles();
    g_popItem = it; EnsureName(it);
    size_t n = std::min<size_t>(it->wins.size(), 5);
    if (n > 0) {
        for (size_t i = 0; i < n; i++) {
            Tile t; t.w = it->wins[i];
            wchar_t tt[256] = {}; GetWindowTextW(t.w, tt, 256); t.title = tt; t.iconic = IsIconic(t.w) != 0;
            if (SUCCEEDED(DwmRegisterThumbnail(g_pop, t.w, &t.th)) && t.th) DwmQueryThumbnailSourceSize(t.th, &t.src);
            t.tl = Layout(t.title.empty() ? it->name : t.title, g_fTile, kTileW - 40.f, nullptr, nullptr);
            float x = kPad + i * (kTileW + kGapT);
            t.r = RF(x, kPad, x + kTileW, kPad + kThumbH + kTitleH);
            t.area = RF(t.r.left + 7, t.r.top + kTitleH, t.r.right - 7, t.r.bottom - 7);
            g_tiles.push_back(t);
        }
        g_popW = kPad * 2 + n * kTileW + (n - 1) * kGapT; g_popH = kPad * 2 + kThumbH + kTitleH;
    } else {
        std::wstring label = it->name;
        if (it->special && g_trashCount >= 0) label += g_trashCount > 0 ? L"  ·  " + std::to_wstring(g_trashCount) + L" öğe" : L"  ·  boş";
        g_popLabel = Layout(label, g_fTitle, 360.f, &g_popLabelW, &g_popLabelH);
        g_popW = g_popLabelW + 28.f; g_popH = 32.f;
    }
    const float S = g_scale;
    g_popPxW = (int)ceilf(g_popW * S); g_popPxH = (int)ceilf(g_popH * S);
    float iconX = g_winRc.left + (g_md + it->icx) * S;
    int x = (int)(iconX - g_popPxW / 2.f);
    x = std::max((int)g_monRc.left + 8, std::min(x, (int)g_monRc.right - 8 - g_popPxW));
    int islandTop = g_winRc.top + (int)(g_md * S), islandBot = g_winRc.top + (int)((g_md + g_cfg.height) * S);
    int y = g_cfg.top ? islandBot + (int)(12 * S) : islandTop - (int)(14 * S) - g_popPxH;
    SetWindowPos(g_pop, HWND_TOPMOST, x, y, g_popPxW, g_popPxH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!wasShown) { g_popA.v = 0; g_popY.v = g_cfg.top ? -8.f : 8.f; g_popY.vel = 0; }
    g_popA.t = 1; g_popY.t = 0; g_popShown = true;
    Kick();
}
static void ClosePopup() { if (g_popShown && g_popA.t != 0) { g_popA.t = 0; g_popY.t = g_cfg.top ? -6.f : 6.f; Kick(); } KillTimer(g_hwnd, TMR_POPUP); }
static void ClosePopupNow() {
    UnregisterTiles(); if (g_pop) ShowWindow(g_pop, SW_HIDE);
    g_popShown = false; g_popItem = nullptr; g_popA.snap(0); g_popY.snap(0); g_popTracking = false;
}
static void PopupSync() {
    if (!g_popShown || !g_popItem) return;
    if (!g_popItem->alive) { ClosePopup(); return; }
    size_t n = std::min<size_t>(g_popItem->wins.size(), 5);
    bool same = n == g_tiles.size();
    for (size_t i = 0; same && i < n; i++) if (g_tiles[i].w != g_popItem->wins[i]) same = false;
    if (!same && g_popA.t > 0.5f) OpenPopup(g_popItem);
}
static void RenderPopup() {
    if (!g_popShown || !EnsurePopupDevice()) return;
    const float S = g_scale, a = Clamp01(g_popA.v), oy = 0.f;
    const float psc = 0.94f + 0.06f * a, pvx = g_popW * 0.5f, pvy = g_cfg.top ? 0.f : g_popH;
    g_dc->SetTarget(g_popTarget); g_dc->SetDpi(96.f * S, 96.f * S);
    g_dc->BeginDraw();
    g_dc->Clear(Col(0, 0, 0, 0));
    g_alpha = a; g_dc->SetTransform(MRotScale(0, psc, pvx, pvy));
    D2D1_RECT_F card = RF(0.5f, 0.5f, g_popW - 0.5f, g_popH - 0.5f);
    float rad = g_tiles.empty() ? 11.f : 14.f;
    FillRR(g_dc, card, rad, Hex(0x17181F, 0.97f));
    if (g_acc && !g_tiles.empty()) { // faint accent wash at the top
        D2D1_RECT_F top = RF(card.left, card.top, card.right, card.top + 30);
        g_acc->SetStartPoint(PT(card.left, card.top)); g_acc->SetEndPoint(PT(card.right, card.top)); g_acc->SetOpacity(0.06f * a);
        D2D1_ROUNDED_RECT rr = {top, rad, rad}; g_dc->FillRoundedRectangle(&rr, g_acc);
    }
    if (g_sheen) { g_sheen->SetStartPoint(PT(0, card.top)); g_sheen->SetEndPoint(PT(0, card.top + 40)); g_sheen->SetOpacity(a); D2D1_ROUNDED_RECT rr = {card, rad, rad}; g_dc->FillRoundedRectangle(&rr, g_sheen); }
    if (g_rim) { g_rim->SetStartPoint(PT(0, card.top)); g_rim->SetEndPoint(PT(0, card.bottom)); g_rim->SetOpacity(a); D2D1_ROUNDED_RECT rr = {card, rad, rad}; g_dc->DrawRoundedRectangle(&rr, g_rim, 1.f, nullptr); }
    if (g_tiles.empty() && g_popLabel) TxtL(g_dc, g_popLabel, 14.f, (g_popH - g_popLabelH) / 2.f, Col(1, 1, 1, 0.95f));
    for (auto& t : g_tiles) {
        float hv = Clamp01(t.hover.v);
        FillRR(g_dc, t.r, 10.f, Col(1, 1, 1, 0.04f + 0.07f * hv));
        if (hv > 0.01f) StrokeRR(g_dc, RF(t.r.left + 0.5f, t.r.top + 0.5f, t.r.right - 0.5f, t.r.bottom - 0.5f), 9.5f, Col(1, 1, 1, 0.10f * hv));
        // header: app icon + title
        if (g_popItem && g_popItem->bmp) Bitmap(g_dc, g_popItem->bmp, RF(t.r.left + 8, t.r.top + 4, t.r.left + 22, t.r.top + 18), 1.f);
        if (t.tl) TxtL(g_dc, t.tl, t.r.left + 27, t.r.top + 3.5f, Col(1, 1, 1, 0.88f));
        FillRR(g_dc, t.area, 6.f, Col(0, 0, 0, 0.28f));
        if ((t.iconic || !t.th) && g_popItem && g_popItem->bmp) {
            float cx = (t.area.left + t.area.right) / 2, cy = (t.area.top + t.area.bottom) / 2;
            Bitmap(g_dc, g_popItem->bmp, RF(cx - 20, cy - 20, cx + 20, cy + 20), 0.9f);
        }
        if (hv > 0.01f) {
            D2D1_RECT_F c = TileClose(t); float ch = Clamp01(t.closeH.v), ccx = (c.left + c.right) / 2, ccy = (c.top + c.bottom) / 2;
            g_br->SetColor(ch > 0.01f ? Hex(0xFF5F57, (0.85f + 0.15f * ch) * hv * a) : Col(1, 1, 1, 0.14f * hv * a));
            D2D1_ELLIPSE e = {PT(ccx, ccy), 8.f, 8.f}; g_dc->FillEllipse(&e, g_br);
            Line(g_dc, ccx - 3, ccy - 3, ccx + 3, ccy + 3, 1.4f, Col(1, 1, 1, 0.95f * hv));
            Line(g_dc, ccx + 3, ccy - 3, ccx - 3, ccy + 3, 1.4f, Col(1, 1, 1, 0.95f * hv));
        }
    }
    g_dc->SetTransform(MIdent());
    HRESULT hr = g_dc->EndDraw();
    g_alpha = 1.f;
    if (SUCCEEDED(hr)) g_popSwap->Present(0, 0);
    g_dc->SetTarget(g_target);
    // live thumbnails, aspect-fit into each tile
    for (auto& t : g_tiles) {
        if (!t.th) continue;
        float aw = t.area.right - t.area.left - 4, ah = t.area.bottom - t.area.top - 4;
        float sw = (float)std::max<LONG>(1, t.src.cx), sh = (float)std::max<LONG>(1, t.src.cy);
        float sc = std::min(aw / sw, ah / sh), dw = sw * sc, dh = sh * sc;
        float cx = pvx + ((t.area.left + t.area.right) / 2 - pvx) * psc, cy = pvy + ((t.area.top + t.area.bottom) / 2 - pvy) * psc + oy;
        dw *= psc; dh *= psc;
        DWM_THUMBNAIL_PROPERTIES p = {};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        p.rcDestination = RECT{(LONG)((cx - dw / 2) * S), (LONG)((cy - dh / 2) * S), (LONG)((cx + dw / 2) * S), (LONG)((cy + dh / 2) * S)};
        p.opacity = (BYTE)(255.f * a); p.fVisible = (!t.iconic && a > 0.02f) ? TRUE : FALSE; p.fSourceClientAreaOnly = FALSE;
        DwmUpdateThumbnailProperties(t.th, &p);
    }
}
static LRESULT CALLBACK PopProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_MOUSEMOVE: {
        if (!g_popTracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_popTracking = true; }
        KillTimer(g_hwnd, TMR_POPHIDE);
        if (g_popA.t < 0.5f && g_popItem) { g_popA.t = 1; g_popY.t = 0; }
        float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
        int ht = -1; bool cl = false;
        for (size_t i = 0; i < g_tiles.size(); i++) if (InR(g_tiles[i].r, x, y)) { ht = (int)i; cl = InR(TileClose(g_tiles[i]), x, y); }
        if (ht != g_popHoverTile || cl != g_popHoverClose) {
            g_popHoverTile = ht; g_popHoverClose = cl;
            for (size_t i = 0; i < g_tiles.size(); i++) { g_tiles[i].hover.t = (int)i == ht ? 1.f : 0.f; g_tiles[i].closeH.t = ((int)i == ht && cl) ? 1.f : 0.f; }
        }
        Kick(); return 0;
    }
    case WM_MOUSELEAVE:
        g_popTracking = false; g_popHoverTile = -1; g_popHoverClose = false;
        for (auto& t : g_tiles) { t.hover.t = 0; t.closeH.t = 0; }
        SetTimer(g_hwnd, TMR_POPHIDE, 280, nullptr); Kick(); return 0;
    case WM_LBUTTONDOWN: g_popPressTile = g_popHoverTile; return 0;
    case WM_LBUTTONUP: {
        int i = g_popHoverTile;
        if (i >= 0 && i == g_popPressTile && i < (int)g_tiles.size()) {
            HWND w = g_tiles[i].w;
            if (g_popHoverClose) PostMessageW(w, WM_CLOSE, 0, 0); else { ActivateWin(w); ClosePopup(); }
        } else if (g_tiles.empty() && g_popItem) { AppItem* it = g_popItem; ClosePopup(); ClickApp(it); }
        g_popPressTile = -1; return 0;
    }
    case WM_MBUTTONUP: if (g_popHoverTile >= 0 && g_popHoverTile < (int)g_tiles.size()) PostMessageW(g_tiles[g_popHoverTile].w, WM_CLOSE, 0, 0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void Frame() {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - g_last.QuadPart) / (float)g_qpf.QuadPart; g_last = now;
    if (dt > 0.05f) dt = 0.05f; if (dt < 0) dt = 0;
    bool more = UpdateAnims(dt);
    if (!CreateDevice()) { g_animating = false; return; }
    ID2D1DeviceContext* dc = g_dc;
    g_curDC = dc;
    dc->BeginDraw();
    DrawScene(dc, g_pxW / g_scale, g_pxH / g_scale, more);
    HRESULT hr = dc->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) { DiscardDevice(); more = true; }
    else {
        if (g_popShown) RenderPopup();
        hr = g_swap->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { DiscardDevice(); more = true; }
    }
    if (g_popShown && g_popA.t == 0 && g_popA.v < 0.02f) ClosePopupNow();
    g_animating = more;
}

// ───────────────────────────── positioning / appbar ─────────────────────────────
static bool g_abRegistered = false, g_inRepos = false;
static void Reposition() {
    if (g_inRepos) return;
    struct Guard { Guard() { g_inRepos = true; } ~Guard() { g_inRepos = false; } } guard;
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof(mi)}; GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    float newScale = dx / 96.f; bool dpiChanged = fabsf(newScale - g_scale) > 0.001f; g_scale = newScale;
    int H = (int)lroundf(g_cfg.height * g_scale), M = (int)lroundf(g_cfg.margin * g_scale);
    RECT mr = mi.rcMonitor;
    APPBARDATA abd = {sizeof(abd)}; abd.hWnd = g_hwnd; abd.uCallbackMessage = WM_APPBAR;
    if (!g_abRegistered) { SHAppBarMessage(ABM_NEW, &abd); g_abRegistered = true; }
    abd.uEdge = g_cfg.top ? ABE_TOP : ABE_BOTTOM; abd.rc = mr;
    int reserve = H + 2 * M;
    if (g_cfg.top) abd.rc.bottom = abd.rc.top + reserve; else abd.rc.top = abd.rc.bottom - reserve;
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    if (g_cfg.top) abd.rc.bottom = abd.rc.top + reserve; else abd.rc.top = abd.rc.bottom - reserve;
    SHAppBarMessage(ABM_SETPOS, &abd);
    int x = mr.left, w = mr.right - mr.left, h = reserve;
    int y = g_cfg.top ? abd.rc.top : abd.rc.bottom - reserve;
    g_pxW = w; g_pxH = h; g_md = (float)g_cfg.margin;
    g_monRc = mr; g_winRc = RECT{x, y, x + w, y + h};
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | (g_fullscreen ? 0 : SWP_SHOWWINDOW));
    int corner = 1 /*DONOTROUND*/;
    DwmSetWindowAttribute(g_hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    ResizeSwap();
    BuildWallpaper();
    if (dpiChanged) ReloadAllIcons();
    Kick();
}
static void ApplyBackdrop() {
    struct ACCENTPOLICY { int state; int flags; DWORD color; int anim; };
    struct WCAD { int attr; PVOID data; SIZE_T size; };
    typedef BOOL(WINAPI * pSWCA)(HWND, WCAD*);
    static pSWCA swca = (pSWCA)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
    BOOL dark = TRUE; DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF border = 0xFFFFFFFE /*DWMWA_COLOR_NONE*/;
    DwmSetWindowAttribute(g_hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
    if (!swca) return;
    ACCENTPOLICY ap = {0 /*disabled — glass is rendered by NovaBar itself*/, 0, 0, 0};
    WCAD d = {19 /*WCA_ACCENT_POLICY*/, &ap, sizeof(ap)};
    swca(g_hwnd, &d);
}

// ───────────────────────────── menus ─────────────────────────────
static void DarkMenus() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); if (!ux) return;
    typedef int(WINAPI * fnMode)(int); typedef void(WINAPI * fnFlush)();
    auto setMode = (fnMode)GetProcAddress(ux, MAKEINTRESOURCEA(135)); auto flush = (fnFlush)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (setMode) setMode(2 /*ForceDark*/); if (flush) flush();
}
enum { ID_TASKMGR = 100, ID_SETTINGS, ID_RELOAD, ID_TOGGLETB, ID_AUTOSTART, ID_TOP, ID_EXIT, ID_NEWWIN, ID_PIN, ID_CLOSE, ID_STATS, ID_TITLE, ID_CENTER, ID_MEDIA, ID_PREVIEWS, ID_TRASH, ID_STYLE, ID_EMPTYTRASH, ID_LANG };
static int ShowMenu(HMENU m) {
    POINT p; GetCursorPos(&p); SetForegroundWindow(g_hwnd);
    UINT fl = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY | (g_cfg.top ? TPM_TOPALIGN : TPM_BOTTOMALIGN);
    int r = TrackPopupMenuEx(m, fl, p.x, p.y, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0); DestroyMenu(m);
    if (g_fg && IsWindow(g_fg) && r != ID_CLOSE) SetForegroundWindow(g_fg);
    return r;
}
static void SetIniBool(const wchar_t* k, bool v) { WritePrivateProfileStringW(L"Bar", k, v ? L"1" : L"0", g_iniPath.c_str()); }
static void BarMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TASKMGR, L"Görev Yöneticisi");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_cfg.stats ? MF_CHECKED : 0), ID_STATS, L"CPU / RAM göster");
    AppendMenuW(m, MF_STRING | (g_cfg.title ? MF_CHECKED : 0), ID_TITLE, L"Aktif pencere başlığını göster");
    AppendMenuW(m, MF_STRING | (g_cfg.center ? MF_CHECKED : 0), ID_CENTER, L"Uygulamaları ortala");
    AppendMenuW(m, MF_STRING | (g_cfg.media ? MF_CHECKED : 0), ID_MEDIA, L"Müzik çalar");
    AppendMenuW(m, MF_STRING | (g_cfg.previews ? MF_CHECKED : 0), ID_PREVIEWS, L"Pencere önizlemeleri");
    AppendMenuW(m, MF_STRING | (g_cfg.trash ? MF_CHECKED : 0), ID_TRASH, L"Geri Dönüşüm Kutusu");
    AppendMenuW(m, MF_STRING | (g_cfg.lang ? MF_CHECKED : 0), ID_LANG, L"Klavye dili göstergesi");
    AppendMenuW(m, MF_STRING | (g_cfg.islands ? MF_CHECKED : 0), ID_STYLE, L"Ada görünümü");
    AppendMenuW(m, MF_STRING | (g_cfg.top ? MF_CHECKED : 0), ID_TOP, L"Ekranın üstüne al");
    AppendMenuW(m, MF_STRING | (g_cfg.hideTaskbar ? 0 : MF_CHECKED), ID_TOGGLETB, L"Windows görev çubuğunu da göster");
    AppendMenuW(m, MF_STRING | (AutoStartOn() ? MF_CHECKED : 0), ID_AUTOSTART, L"Windows ile başlat");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"Ayar dosyasını aç…");
    AppendMenuW(m, MF_STRING, ID_RELOAD, L"Ayarları yeniden yükle");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"NovaBar'dan çık");
    int r = ShowMenu(m);
    auto reload = [] { LoadConfig(); ApplyBackdrop(); if (g_dc) CreateBrushes(g_dc); Reposition(); BuildWallpaper(true); RefreshWindows(); };
    switch (r) {
    case ID_TASKMGR: ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
    case ID_STATS: SetIniBool(L"ShowStats", !g_cfg.stats); reload(); break;
    case ID_TITLE: SetIniBool(L"ShowActiveTitle", !g_cfg.title); reload(); break;
    case ID_CENTER: SetIniBool(L"CenterApps", !g_cfg.center); reload(); break;
    case ID_MEDIA: SetIniBool(L"ShowMedia", !g_cfg.media); reload(); MediaPoll(); break;
    case ID_PREVIEWS: SetIniBool(L"WindowPreviews", !g_cfg.previews); reload(); break;
    case ID_TRASH: SetIniBool(L"ShowTrash", !g_cfg.trash); reload(); break;
    case ID_LANG: SetIniBool(L"ShowKeyboardLayout", !g_cfg.lang); reload(); ReadLang(); break;
    case ID_STYLE: WritePrivateProfileStringW(L"Bar", L"Style", g_cfg.islands ? L"bar" : L"islands", g_iniPath.c_str()); reload(); break;
    case ID_TOP: WritePrivateProfileStringW(L"Bar", L"Position", g_cfg.top ? L"bottom" : L"top", g_iniPath.c_str()); reload(); break;
    case ID_TOGGLETB:
        SetIniBool(L"HideWindowsTaskbar", !g_cfg.hideTaskbar); LoadConfig();
        if (g_cfg.hideTaskbar) HideWinTaskbar(); else RestoreWinTaskbar();
        Reposition(); break;
    case ID_AUTOSTART: SetAutoStart(!AutoStartOn()); break;
    case ID_SETTINGS: ShellExecuteW(nullptr, L"open", L"notepad.exe", (L"\"" + g_iniPath + L"\"").c_str(), nullptr, SW_SHOWNORMAL); break;
    case ID_RELOAD: reload(); break;
    case ID_EXIT: DestroyWindow(g_hwnd); break;
    }
}
static void AppMenu(AppItem* it) {
    HMENU m = CreatePopupMenu();
    if (it->special) {
        AppendMenuW(m, MF_STRING, ID_NEWWIN, L"Aç");
        AppendMenuW(m, MF_STRING | (g_trashCount == 0 ? MF_GRAYED : 0), ID_EMPTYTRASH, L"Geri Dönüşüm Kutusunu boşalt");
        int r = ShowMenu(m);
        if (r == ID_NEWWIN) ClickApp(it);
        else if (r == ID_EMPTYTRASH) { SHEmptyRecycleBinW(nullptr, nullptr, 0); g_trashCount = -1; ReadTrash(); }
        return;
    }
    AppendMenuW(m, MF_STRING, ID_NEWWIN, it->wins.empty() ? L"Aç" : L"Yeni pencere");
    AppendMenuW(m, MF_STRING, ID_PIN, it->pinned ? L"Sabitlemeyi kaldır" : L"Bara sabitle");
    if (!it->wins.empty()) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); AppendMenuW(m, MF_STRING, ID_CLOSE, it->wins.size() > 1 ? L"Tüm pencereleri kapat" : L"Pencereyi kapat"); }
    int r = ShowMenu(m);
    if (r == ID_NEWWIN) { if (it->wins.empty()) it->bounce = 0; Launch(it); Kick(); }
    else if (r == ID_PIN) {
        if (it->pinned) g_pins.erase(std::remove_if(g_pins.begin(), g_pins.end(), [&](const Pin& p) { return p.key == it->key; }), g_pins.end());
        else {
            std::wstring tgt = !it->aumid.empty() ? L"shell:AppsFolder\\" + it->aumid : it->exe;
            if (tgt.empty()) return;
            g_pins.push_back({it->key, tgt}); it->target = tgt;
            // move to end of pinned block
            auto f = std::find(g_items.begin(), g_items.end(), it); g_items.erase(f);
            size_t pos = 0; for (auto* o : g_items) { if (o->pinned) pos++; else break; }
            g_items.insert(g_items.begin() + std::min(pos, g_items.size()), it);
        }
        SavePins(); g_winCache.clear(); RefreshWindows(); Kick();
    } else if (r == ID_CLOSE) for (HWND w : it->wins) PostMessageW(w, WM_CLOSE, 0, 0);
}

// ───────────────────────────── input ─────────────────────────────
static Hit HitTest(float x, float y) {
    x -= g_md; y -= g_md;
    Hit h;
    if (InR(g_rStart, x, y)) h.k = RK_START;
    else if (g_cfg.stats && InR(g_rStats, x, y)) h.k = RK_STATS;
    else if (InR(g_rQuick, x, y)) h.k = RK_QUICK;
    else if (InR(g_rClock, x, y)) h.k = RK_CLOCK;
    else if (g_langShow && InR(g_rLang, x, y)) h.k = RK_LANG;
    else if (g_mW.v > 40.f && InR(g_rMPrev, x, y)) h.k = RK_MPREV;
    else if (g_mW.v > 40.f && InR(g_rMPlay, x, y)) h.k = RK_MPLAY;
    else if (g_mW.v > 40.f && InR(g_rMNext, x, y)) h.k = RK_MNEXT;
    else if (g_mW.v > 40.f && InR(g_rMInfo, x, y)) h.k = RK_MINFO;
    else if (InR(g_rDesk, x, y) || (x >= g_rDesk.left && y > -g_md && y < g_rDesk.bottom + g_md)) h.k = RK_DESK;
    else for (auto* it : g_items) if (it->alive && x >= it->x && x < it->x + it->dw && it->dw > 4) { h.k = RK_APP; h.it = it; break; }
    return h;
}
static void SetHover(const Hit& h) {
    g_hover = h;
    g_hStart.t = h.k == RK_START; g_hStats.t = h.k == RK_STATS; g_hQuick.t = h.k == RK_QUICK; g_hClock.t = h.k == RK_CLOCK; g_hDesk.t = h.k == RK_DESK;
    bool p = g_press.k != RK_NONE && g_press == h;
    g_pStart.t = p && h.k == RK_START; g_pQuick.t = p && h.k == RK_QUICK; g_pClock.t = p && h.k == RK_CLOCK; g_pDesk.t = p && h.k == RK_DESK;
    g_hMInfo.t = h.k == RK_MINFO; g_hMPrev.t = h.k == RK_MPREV; g_hMPlay.t = h.k == RK_MPLAY; g_hMNext.t = h.k == RK_MNEXT; g_hLang.t = h.k == RK_LANG;
    g_pMPrev.t = p && h.k == RK_MPREV; g_pMPlay.t = p && h.k == RK_MPLAY; g_pMNext.t = p && h.k == RK_MNEXT; g_pLang.t = p && h.k == RK_LANG;
    // window-preview popup: open after a short dwell, follow the cursor once open
    if (h.k == RK_APP && h.it && !g_dragItem) {
        KillTimer(g_hwnd, TMR_POPHIDE);
        if (g_popShown && g_popA.t > 0.5f) { if (h.it != g_popItem) OpenPopup(h.it); }
        else { g_hoverPend = h.it; SetTimer(g_hwnd, TMR_POPUP, 420, nullptr); }
    } else {
        KillTimer(g_hwnd, TMR_POPUP);
        if (g_popShown) SetTimer(g_hwnd, TMR_POPHIDE, 280, nullptr);
    }
    Kick();
}
static void DoClick(const Hit& h) {
    switch (h.k) {
    case RK_START: SendKeys({VK_LWIN}); break;
    case RK_APP: if (h.it) { ClosePopup(); ClickApp(h.it); } break;
    case RK_STATS: ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL); break;
    case RK_QUICK: SendKeys({VK_LWIN, 'A'}); break;
    case RK_CLOCK: SendKeys({VK_LWIN, (WORD)(g_win11 ? 'N' : 'A')}); break;
    case RK_DESK: SendKeys({VK_LWIN, 'D'}); break;
    case RK_MPREV: MediaCmd(0); break;
    case RK_MPLAY: MediaCmd(1); break;
    case RK_MNEXT: MediaCmd(2); break;
    case RK_MINFO: MediaActivate(); break;
    case RK_LANG: SendKeys({VK_LWIN, VK_SPACE}); break;
    default: break;
    }
}
static void ChangeVolume(float delta) {
    if (!g_epv) AcquireVolume(); if (!g_epv) return;
    float v = Clamp01(g_vol + delta);
    g_epv->SetMasterVolumeLevelScalar(v, nullptr); if (g_mute && delta > 0) g_epv->SetMute(FALSE, nullptr);
    g_vol = v; g_mute = g_mute && delta <= 0; g_volV.t = v; g_volShow.t = 1;
    SetTimer(g_hwnd, TMR_VOLHIDE, 1600, nullptr); Kick();
}

// ───────────────────────────── win events ─────────────────────────────
static void CALLBACK WinEvent(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || child != CHILDID_SELF || !h) return;
    if (ev == EVENT_SYSTEM_FOREGROUND || ev == EVENT_SYSTEM_MINIMIZEEND) { UpdateActive(); if (ev == EVENT_SYSTEM_FOREGROUND) ReadLang(); Kick(); return; }
    if (ev == EVENT_OBJECT_NAMECHANGE) { if (h == g_fg) UpdateActive(); return; }
    if (ev == EVENT_OBJECT_SHOW && g_cfg.hideTaskbar && g_tbHidden) {
        wchar_t cls[32]; GetClassNameW(h, cls, 32);
        if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd")) { ShowWindow(h, SW_HIDE); return; }
    }
    if (ev == EVENT_OBJECT_DESTROY) { if (!g_winCache.count(h)) return; }
    else if (GetAncestor(h, GA_ROOT) != h) return;
    SetTimer(g_hwnd, TMR_REFRESH, 60, nullptr);
}

// ───────────────────────────── window proc ─────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TASKBARCREATED && WM_TASKBARCREATED) {
        if (g_cfg.hideTaskbar) SetTimer(hwnd, TMR_REHIDE, 800, nullptr);
        return 0;
    }
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_MOUSEMOVE: {
        float x = GET_X_LPARAM(lp) / g_scale, y = GET_Y_LPARAM(lp) / g_scale;
        if (!g_tracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); g_tracking = true; g_mouseX.snap(x - g_md); }
        g_mouseX.t = x - g_md; g_barHover.t = 1; g_mouseIn = true; g_mouseRawX = x - g_md;
        if ((wp & MK_LBUTTON) && g_dragCand && !g_dragItem && fabsf(x - g_md - g_dragDownX) > 6.f) {
            g_dragItem = g_dragCand; g_dragLift.t = 1; g_press = Hit(); ClosePopupNow(); KillTimer(hwnd, TMR_POPUP);
        }
        if (g_dragItem) {
            g_dragX = x - g_md;
            for (auto* o : g_items) {
                if (g_dragSwapPending) break;
                if (o == g_dragItem || !o->pinned || o->special || !o->alive) continue;
                if (g_dragX >= o->x + 4 && g_dragX < o->x + o->dw - 4) {
                    auto ia = std::find(g_items.begin(), g_items.end(), g_dragItem), ib = std::find(g_items.begin(), g_items.end(), o);
                    float shift = (ib > ia) ? g_dragItem->dw : -g_dragItem->dw;
                    std::iter_swap(ia, ib); o->offX.v += shift; g_dragSwapPending = true;
                    auto pa = std::find_if(g_pins.begin(), g_pins.end(), [&](const Pin& p) { return p.key == g_dragItem->key; });
                    auto pb = std::find_if(g_pins.begin(), g_pins.end(), [&](const Pin& p) { return p.key == o->key; });
                    if (pa != g_pins.end() && pb != g_pins.end()) std::iter_swap(pa, pb);
                    break;
                }
            }
            Kick(); return 0;
        }
        Hit h = HitTest(x, y); if (!(h == g_hover)) SetHover(h); else Kick();
        return 0;
    }
    case WM_MOUSELEAVE: g_tracking = false; g_mouseIn = false; g_barHover.t = 0; SetHover(Hit()); return 0;
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd); g_press = HitTest(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale); SetHover(g_hover);
        g_dragCand = (g_press.k == RK_APP && g_press.it && g_press.it->pinned && !g_press.it->special) ? g_press.it : nullptr;
        g_dragDownX = GET_X_LPARAM(lp) / g_scale - g_md;
        KillTimer(hwnd, TMR_POPUP);
        if (g_press.k == RK_START) g_pStart.t = 1; return 0;
    }
    case WM_LBUTTONUP: {
        Hit h = HitTest(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale);
        if (g_dragItem) {
            AppItem* d = g_dragItem; d->offX.v = g_dragX - d->icx; d->offX.vel = 0; d->offX.t = 0;
            g_dragItem = nullptr; g_dragCand = nullptr; g_dragLift.t = 0; g_press = Hit(); SavePins();
            ReleaseCapture(); SetHover(h); return 0;
        }
        g_dragCand = nullptr;
        ReleaseCapture();
        Hit p = g_press; g_press = Hit(); SetHover(h);
        if (p == h) DoClick(h);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_dragItem && (HWND)lp != hwnd) { g_dragItem->offX.v = g_dragX - g_dragItem->icx; g_dragItem->offX.t = 0; g_dragItem = nullptr; g_dragLift.t = 0; SavePins(); Kick(); }
        g_dragCand = nullptr; return 0;
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp; UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0); bool any = false;
        for (UINT i = 0; i < n && i < 32; i++) { wchar_t pth[MAX_PATH * 2] = {}; if (DragQueryFileW(hd, i, pth, MAX_PATH * 2)) any |= PinPath(pth); }
        DragFinish(hd);
        if (any) { SavePins(); g_winCache.clear(); RefreshWindows(); for (auto* it : g_items) if (it->pinned && it->appear.v < 0.01f) it->bounce = 0; Kick(); }
        return 0;
    }
    case WM_MBUTTONUP: { Hit h = HitTest(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale); if (h.k == RK_APP && h.it) { h.it->ripple = 0; if (h.it->wins.empty()) h.it->bounce = 0; Launch(h.it); Kick(); } return 0; }
    case WM_RBUTTONUP: {
        Hit h = HitTest(GET_X_LPARAM(lp) / g_scale, GET_Y_LPARAM(lp) / g_scale);
        ClosePopupNow();
        if (h.k == RK_APP && h.it) AppMenu(h.it);
        else if (h.k == RK_START) SendKeys({VK_LWIN, 'X'});
        else BarMenu();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(hwnd, &p);
        Hit h = HitTest(p.x / g_scale, p.y / g_scale);
        float notches = GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        if (h.k == RK_QUICK || h.k == RK_CLOCK || h.k == RK_DESK || h.k == RK_LANG || h.k == RK_MINFO || h.k == RK_MPREV || h.k == RK_MPLAY || h.k == RK_MNEXT) ChangeVolume(notches * 0.02f);
        return 0;
    }
    case WM_TIMER:
        if (wp == TMR_REFRESH) { KillTimer(hwnd, TMR_REFRESH); RefreshWindows(); }
        else if (wp == TMR_VOLHIDE) { KillTimer(hwnd, TMR_VOLHIDE); g_volShow.t = 0; Kick(); }
        else if (wp == TMR_FAST) { MediaPoll(); ReadLang(); }
        else if (wp == TMR_POPUP) {
            KillTimer(hwnd, TMR_POPUP);
            if (g_hover.k == RK_APP && g_hover.it && g_hover.it == g_hoverPend && !g_dragItem && g_press.k == RK_NONE) OpenPopup(g_hoverPend);
        }
        else if (wp == TMR_POPHIDE) {
            KillTimer(hwnd, TMR_POPHIDE);
            POINT pt; GetCursorPos(&pt); RECT pr = {};
            if (g_pop && g_popShown) GetWindowRect(g_pop, &pr);
            if (!(g_popShown && PtInRect(&pr, pt)) && !(g_hover.k == RK_APP && g_hover.it)) ClosePopup();
        }
        else if (wp == TMR_REHIDE) { KillTimer(hwnd, TMR_REHIDE); g_abRegistered = false; if (g_cfg.hideTaskbar) HideWinTaskbar(); Reposition(); RefreshWindows(); }
        else if (wp == TMR_POLL) {
            g_tick++;
            ReadClock(); ReadStats(); ReadVolume();
            if (g_tick % 4 == 0) ReadNet();
            if (g_tick % 6 == 0) { if (!g_epv) AcquireVolume(); }
            if (g_tick % 3 == 0) RefreshWindows();
            if (g_tick % 6 == 0 && g_cfg.trash) ReadTrash();
            if (g_tick % 5 == 0) { std::wstring k = g_wallKey; BuildWallpaper(); if (k != g_wallKey) Kick(); }
            if (g_cfg.hideTaskbar && g_tbHidden) { HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr); if (tb && IsWindowVisible(tb)) ForEachTaskbar(false); }
        }
        return 0;
    case WM_APPBAR:
        if (wp == ABN_POSCHANGED) Reposition();
        else if (wp == ABN_FULLSCREENAPP) {
            g_fullscreen = lp != 0;
            if (lp) { ShowWindow(hwnd, SW_HIDE); ClosePopupNow(); }
            else { SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW); Kick(); }
        }
        return 0;
    case WM_DISPLAYCHANGE: case WM_DPICHANGED: Reposition(); return 0;
    case WM_SETTINGCHANGE: if (wp == SPI_SETWORKAREA) return 0; if (wp == SPI_SETDESKWALLPAPER || wp == 0) { BuildWallpaper(); Kick(); } ReadClock(); return 0;
    case WM_SYSCOLORCHANGE: BuildWallpaper(); Kick(); return 0;
    case WM_PAINT: { ValidateRect(hwnd, nullptr); Kick(); return 0; }
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (wp) { if (g_tbHidden) RestoreWinTaskbar(); } return 0;
    case WM_DESTROY: {
        KillTimer(hwnd, TMR_POLL);
        APPBARDATA abd = {sizeof(abd)}; abd.hWnd = hwnd; SHAppBarMessage(ABM_REMOVE, &abd);
        if (g_tbHidden) RestoreWinTaskbar();
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ───────────────────────────── entry ─────────────────────────────
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cmd, int) {
    wchar_t ep[MAX_PATH * 2]; GetModuleFileNameW(nullptr, ep, MAX_PATH * 2); g_exePath = ep;
    g_iniPath = g_exePath.substr(0, g_exePath.find_last_of(L'\\') + 1) + L"NovaBar.ini";
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring a1 = argc > 1 ? Lower(argv[1]) : L"";
    if (a1 == L"--restore") { LoadConfig(); RestoreWinTaskbar(); return 0; }
    (void)cmd;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QueryPerformanceFrequency(&g_qpf);
    D2D1_FACTORY_OPTIONS fo = {};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo, (void**)&g_d2d))) return 1;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&g_wic);
    if (!g_dw || !g_wic) return 1;
    LoadConfig(); CreateTextResources();


    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"NovaBar.SingleInstance.Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    SetUnhandledExceptionFilter(CrashFilter);
    DarkMenus();
    typedef LONG(WINAPI * fnRtl)(PRTL_OSVERSIONINFOW);
    if (auto rtl = (fnRtl)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) { RTL_OSVERSIONINFOW v = {sizeof(v)}; rtl(&v); g_win11 = v.dwBuildNumber >= 22000; }

    WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NovaBarWnd"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP | WS_EX_ACCEPTFILES, wc.lpszClassName, L"NovaBar",
                             WS_POPUP, 0, 0, 100, 40, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd) return 1;
    {
        WNDCLASSEXW pc = {sizeof(pc)}; pc.lpfnWndProc = PopProc; pc.hInstance = hi; pc.hCursor = LoadCursorW(nullptr, IDC_ARROW); pc.lpszClassName = L"NovaBarPreview";
        RegisterClassExW(&pc);
        g_pop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, pc.lpszClassName, L"NovaBar Preview", WS_POPUP, 0, 0, 10, 10, g_hwnd, nullptr, hi, nullptr);
        if (g_pop) {
            int corner = 1; DwmSetWindowAttribute(g_pop, 33, &corner, sizeof(corner));
            COLORREF none = 0xFFFFFFFE; DwmSetWindowAttribute(g_pop, 34, &none, sizeof(none));
            BOOL dark = TRUE; DwmSetWindowAttribute(g_pop, 20, &dark, sizeof(dark));
        }
    }
    ChangeWindowMessageFilterEx(g_hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(g_hwnd, 0x0049 /*WM_COPYGLOBALDATA*/, MSGFLT_ALLOW, nullptr);
    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_hwnd, WM_TASKBARCREATED, MSGFLT_ALLOW, nullptr);
    ApplyBackdrop();
    if (g_cfg.hideTaskbar) HideWinTaskbar();
    Reposition();
    AcquireVolume(); ReadVolume(); g_volV.snap(g_vol); ReadNet(); ReadStats(); ReadClock();
    g_cpuV.snap(0); g_ramV.snap(0);
    RefreshWindows();
    for (auto* it : g_items) { it->w.snap(42); it->appear.v = 0; }
    g_enter.snap(0); g_enter.t = 1;
    SetTimer(g_hwnd, TMR_POLL, 1000, nullptr);
    SetTimer(g_hwnd, TMR_FAST, 400, nullptr);
    ReadLang(); if (g_cfg.trash) ReadTrash();
    HWINEVENTHOOK hk[5];
    hk[0] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hk[1] = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, WinEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hk[2] = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, WinEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hk[3] = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, WinEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hk[4] = SetWinEventHook(0x8017 /*CLOAKED*/, 0x8018 /*UNCLOAKED*/, nullptr, WinEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    Kick();

    MSG m;
    for (;;) {
        if (g_animating) {
            bool quit = false;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) { quit = true; break; } TranslateMessage(&m); DispatchMessageW(&m); }
            if (quit) break;
            if (g_animating) Frame();
        } else {
            BOOL r = GetMessageW(&m, nullptr, 0, 0); if (r <= 0) break;
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    }
    for (auto h : hk) if (h) UnhookWinEvent(h);
    DiscardDevice();
    if (mtx) { ReleaseMutex(mtx); CloseHandle(mtx); }
    CoUninitialize();
    return 0;
}
