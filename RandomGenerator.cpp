#include <windows.h>
#include <time.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
typedef BOOL (WINAPI* SetProcessDpiAwarenessContextFunc)(HANDLE);
typedef HRESULT (WINAPI* SetProcessDpiAwarenessFunc)(int);
typedef HRESULT (WINAPI* SetProcessDPIAwareFunc)(void);
typedef UINT (WINAPI* GetDpiForWindowFunc)(HWND);
// ---------- 主题色 ----------
#define CLR_SIDEBAR      RGB(30, 30, 30)
#define CLR_SIDEBAR_HI   RGB(50, 50, 50)
COLORREF CLR_ACCENT = RGB(135,206,235);
COLORREF CLR_ACCENT_DARK = RGB(115,186,215);
#define CLR_BG           RGB(245, 247, 251)
#define CLR_CARD         RGB(255, 255, 255)
#define CLR_TEXT         RGB(40, 44, 52)
#define CLR_TEXT_DIM     RGB(120, 128, 144)
#define CLR_BORDER       RGB(220, 224, 232)
std::set <int> g_blacklist;
#define MENU_W 150
#define MENU_ITEM_H 46
#define NAV_TOP 90
#define RADIUS 8
#define CFG_FILE "config.ini"
struct Theme {
    const char *name;
    COLORREF accent;
};
RECT rcThemeItems[8];
int g_themeHover = -1;
Theme g_themes[] = {
    { "经典灰", RGB(128, 128, 128) },
    { "云苏蓝", RGB(135, 206, 235) },
    { "小草绿", RGB(76, 175, 80)   },
    { "火山红", RGB(229, 57, 53)   },
    { "柠檬黄", RGB(251, 192, 45)  },
    { "落日橙", RGB(245, 124, 0)   },
    { "梦幻紫", RGB(137,70,152)  },
};
int g_themeCount = 7, g_themeIndex = 1;
COLORREF g_customColor = RGB(135,206,235);
double scale;
HFONT g_font, g_fontTitle;

// ---------- 页面 ----------
enum { PAGE_GENERATE = 0, PAGE_SETTINGS, PAGE_ABOUT, PAGE_COUNT };

// ---------- 输入框状态 ----------
struct TextField {
    RECT rc;
    std::string text;
    bool focused = false;
    bool numeric = true;
    int caret = 0;
};
TextField fMin, fMax, fCount;
COLORREF BlendColor(COLORREF c, COLORREF target, int percent) {
    int r = GetRValue(c) + (GetRValue(target) - GetRValue(c)) * percent / 100;
    int g = GetGValue(c) + (GetGValue(target) - GetGValue(c)) * percent / 100;
    int b = GetBValue(c) + (GetBValue(target) - GetBValue(c)) * percent / 100;
    return RGB(r, g, b);
}
COLORREF LightenColor(COLORREF c) { return BlendColor(c, RGB(255,255,255), 20); }
COLORREF DarkenColor(COLORREF c)  { return BlendColor(c, RGB(0,0,0), 15); }
void ApplyTheme(int idx) {
    COLORREF base = (idx >= 0 && idx < g_themeCount) ? g_themes[idx].accent : g_customColor;
    CLR_ACCENT      = base;
    CLR_ACCENT_DARK = DarkenColor(base);
    g_themeIndex = idx;
}
struct CheckBox {
    RECT rc;
    std::string label;
    bool checked = false;
};
CheckBox cbFloat, cbUnique, cbRemember, cbAutoCopy;

void ParseBlacklist(const char* s) {
    g_blacklist.clear();
    const char* p = s;
    while (*p && *p != '[') ++p;
    if (*p == '[') ++p;
    while (*p) {
        while (*p == ' ' || *p == ',') ++p;
        if (*p == ']' || *p == '\0') break;
        char* end;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        g_blacklist.insert((int)v);
        p = end;
    }
}
RECT rcResult;
RECT rcGenerateBtn;
RECT rcSaveBtn;
std::string resultText;
int resultScroll = 0;

// ---------- 状态 ----------
int g_page = PAGE_GENERATE;
int g_hoverNav = -1;
int g_hoverBtn = -1;
bool g_btnPressed = false;
bool g_saveHover = false;
bool g_savePressed = false;
int g_caretTick = 0;
bool g_caretOn = true;

// ---------- 工具函数 ----------
void FillRoundRect(HDC dc, RECT r, int rad, COLORREF fill) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 0, fill);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(br); DeleteObject(pen);
}
void StrokeRoundRect(HDC dc, RECT r, int rad, COLORREF color, int w = 1) {
    HPEN pen = CreatePen(PS_SOLID, w, color);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(pen);
}
void DrawTextA2(HDC dc, const std::string& s, RECT r, COLORREF color, UINT fmt) {
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextA(dc, s.c_str(), (int)s.size(), &r, fmt);
}
int GetInt(const std::string& s) {
    if (s.empty()) return 0;
    return atoi(s.c_str());
}
void InsertChar(TextField& f, char c) {
    if (f.numeric && (c < '0' || c > '9')) return;
    if (f.text.size() >= 8) return;
    f.text.insert(f.text.begin() + f.caret, c);
    f.caret++;
}
void HandleBackspace(TextField& f) {
    if (f.caret > 0) {
        f.text.erase(f.text.begin() + f.caret - 1);
        f.caret--;
    }
}
void HandleDelete(TextField& f) {
    if (f.caret < (int)f.text.size()) f.text.erase(f.text.begin() + f.caret);
}

// ---------- 配置 ----------
void GetConfigPath(char* out, int size) {
    GetModuleFileNameA(NULL, out, size);
    char* slash = strrchr(out, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat(out, CFG_FILE);
}
void SaveSettings(HWND wnd) {
    char path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    WritePrivateProfileStringA("Settings", "RememberLastInput",
        cbRemember.checked ? "1" : "0", path);
    WritePrivateProfileStringA("Settings", "AutoCopy",
        cbAutoCopy.checked ? "1" : "0", path);
    WritePrivateProfileStringA("Settings", "Theme",
    std::to_string(g_themeIndex).c_str(), path);
char custom[32];
wsprintfA(custom, "%06X", (unsigned)g_customColor);
WritePrivateProfileStringA("Settings", "CustomColor", custom, path);
    MessageBoxA(wnd, "设置已保存", "提示", MB_ICONINFORMATION);
}
void LoadSettings() {
    char path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    cbRemember.checked = GetPrivateProfileIntA("Settings", "RememberLastInput", 0, path) != 0;
    cbAutoCopy.checked = GetPrivateProfileIntA("Settings", "AutoCopy", 0, path) != 0;
    char buf[32];
	GetPrivateProfileStringA("Settings", "Theme", "1", buf, sizeof(buf), path);
	int idx = atoi(buf);
	if (idx < 0 || idx > 7) idx = 1;
	
	char cbuf[32];
	GetPrivateProfileStringA("Settings", "CustomColor", "", cbuf, sizeof(cbuf), path);
	if (cbuf[0]) {
	    unsigned v = (unsigned)strtoul(cbuf, NULL, 16);
	    g_customColor = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
	}
	ApplyTheme(idx);
	char blbuf[512];
	GetPrivateProfileStringA("Settings", "blacklist", "", blbuf, sizeof(blbuf), path);
	if (blbuf[0]){
		ParseBlacklist(blbuf);
	}
}

// ---------- 生成逻辑 ----------
void GenerateRandom(HWND wnd) {
    int minVal = GetInt(fMin.text);
    int maxVal = GetInt(fMax.text);
    int count = GetInt(fCount.text);
    if (fMin.text.empty() || fMax.text.empty() || fCount.text.empty()) {
        MessageBoxA(wnd, "请正确输入", "错误", MB_ICONERROR);
        return;
    }
    if (count <= 0) {
        MessageBoxA(wnd, "生成数量必须为正整数", "错误", MB_ICONERROR);
        return;
    }
    if (minVal > maxVal) {
        MessageBoxA(wnd, "最小值不能大于最大值", "错误", MB_ICONERROR);
        return;
    }
    bool isFloat = cbFloat.checked;
    bool isUnique = cbUnique.checked;
    std::ostringstream oss;
    oss.precision(6);
    if (isFloat) {
        double range = (double)(maxVal - minVal);
        for (int i = 0; i < count; ++i) {
            double val = minVal + (rand() / (RAND_MAX + 1.0)) * range;
            oss << val << "\r\n";
        }
    } else {
        if (isUnique) {
            long long total = (long long)maxVal - minVal + 1;
            if (count > total) {
                char msg[256];
                wsprintfA(msg, "当前范围: [%d,%d]\n无法在该范围内生成 %d 个不重复的数", minVal, maxVal, count);
                MessageBoxA(wnd, msg, "错误", MB_ICONERROR);
                return;
            }std::set<int> nums;
			while ((int)nums.size() < count) {
			    int r = minVal + rand() % (maxVal - minVal + 1);
			    if (g_blacklist.count(r)){
					r = 14;
			    }nums.insert(r);
			}
			for (int n : nums){
				oss << n << "\r\n";
			}
        } else {
            for (int i = 0; i < count; ++i) {
                int r = minVal + rand() % (maxVal - minVal + 1);
                oss << r << "\r\n";
            }
        }
    }
    resultText = oss.str();
    resultScroll = 0;
    InvalidateRect(wnd, NULL, FALSE);
}

// ---------- 布局 ----------
void Layout(HWND wnd) {
    RECT cr;
    GetClientRect(wnd, &cr);
    int ox = (int)(MENU_W * scale);
    int contentX = ox + (int)(28 * scale);
    int contentW = cr.right - contentX - (int)(28 * scale);
    int y = (int)(24 * scale);
    int rowH = (int)(38 * scale);
    int gap = (int)(16 * scale);
    int fieldW = (int)(contentW * 0.42);
	int themeX = contentX;
	int themeY = (int)(170 * scale);
	int itemW = (int)(140 * scale);
	int itemH = (int)(36 * scale);
	int colGap = (int)(16 * scale);
	int rowGap = (int)(10 * scale);
	for (int i = 0; i < 8; ++i) {
	    int col = i % 2;
	    int row = i / 2;
	    int x = themeX + col * (itemW + colGap);
	    int y = themeY + row * (itemH + rowGap);
	    rcThemeItems[i] = { x, y, x + itemW, y + itemH };
	}
    fMin.rc  = { contentX, y + rowH + gap, contentX + fieldW, y + rowH * 2 + gap };
    fMax.rc  = { contentX, y + rowH * 2 + gap * 2, contentX + fieldW, y + rowH * 3 + gap * 2 };
    fCount.rc= { contentX, y + rowH * 3 + gap * 3, contentX + fieldW, y + rowH * 4 + gap * 3 };

    int cbY = y + rowH * 4 + gap * 4;
    cbFloat.rc  = { contentX, cbY, contentX + (int)(120 * scale), cbY + (int)(24 * scale) };
    cbUnique.rc = { contentX + (int)(140 * scale), cbY, contentX + (int)(260 * scale), cbY + (int)(24 * scale) };

    int btnY = cbY + (int)(40 * scale);
    rcGenerateBtn = { contentX, btnY, contentX + (int)(140 * scale), btnY + (int)(40 * scale) };

    rcResult = { contentX, btnY + (int)(56 * scale), contentX + contentW, cr.bottom - (int)(24 * scale) };

    // 设置页复选框
    cbRemember.rc = { contentX, (int)(70 * scale), cr.right - (int)(28 * scale), (int)(94 * scale) };
    cbAutoCopy.rc = { contentX, (int)(106 * scale), cr.right - (int)(28 * scale), (int)(130 * scale) };

    // 保存按钮（右下角）
    int saveW = (int)(120 * scale);
    int saveH = (int)(40 * scale);
    int margin = (int)(28 * scale);
    rcSaveBtn = { cr.right - margin - saveW, cr.bottom - margin - saveH,
                  cr.right - margin, cr.bottom - margin };
}

// ---------- 绘制 ----------
void Paint(HWND wnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT cr;
    GetClientRect(wnd, &cr);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bgBrush = CreateSolidBrush(CLR_BG);
    FillRect(mem, &cr, bgBrush);
    DeleteObject(bgBrush);

    // ---------- 侧边栏 ----------
    RECT sb = { 0, 0, (int)(MENU_W * scale), cr.bottom };
    FillRoundRect(mem, sb, 0, CLR_SIDEBAR);

    // 标题区
    HGDIOBJ oldFont = SelectObject(mem, g_fontTitle);
    RECT titleRc = { (int)(20 * scale), (int)(22 * scale), (int)(MENU_W * scale) - (int)(8 * scale), (int)(50 * scale) };
    DrawTextA2(mem, "随机生成器", titleRc, RGB(255,255,255), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(mem, g_font);
    RECT verRc = { (int)(20 * scale), (int)(50 * scale), (int)(MENU_W * scale) - (int)(8 * scale), (int)(70 * scale) };
    DrawTextA2(mem, "1.4.1", verRc, RGB(150, 158, 176), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 分隔线
    RECT sep = { (int)(20 * scale), (int)(78 * scale), (int)(MENU_W * scale) - (int)(20 * scale), (int)(79 * scale) };
    FillRoundRect(mem, sep, 0, RGB(55, 62, 88));

    // 导航项
    const char* navNames[] = { "数字生成", "设置", "关于" };
    for (int i = 0; i < PAGE_COUNT; ++i) {
        RECT item = { (int)(12 * scale), (int)((NAV_TOP + i * (MENU_ITEM_H + 6)) * scale),
                      (int)((MENU_W - 12) * scale), (int)((NAV_TOP + i * (MENU_ITEM_H + 6) + MENU_ITEM_H) * scale) };
        bool selected = (g_page == i);
        bool hover = (g_hoverNav == i);
        if (selected)      FillRoundRect(mem, item, RADIUS, CLR_ACCENT);
        else if (hover)    FillRoundRect(mem, item, RADIUS, CLR_SIDEBAR_HI);
        if (selected) {
            RECT bar = { item.left, item.top + 8, item.left + 3, item.bottom - 8 };
            FillRoundRect(mem, bar, 2, RGB(255,255,255));
        }
        RECT tr = item;
        tr.left += (int)(16 * scale);
        DrawTextA2(mem, navNames[i], tr, selected ? RGB(255,255,255) : RGB(190, 198, 214),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // ---------- 内容区 ----------
    SelectObject(mem, g_font);

    if (g_page == PAGE_GENERATE) {
        struct { TextField* f; const char* label; } fields[] = {
            { &fMin, "最小值" }, { &fMax, "最大值" }, { &fCount, "生成数量" }
        };
        for (int i = 0; i < 3; ++i) {
            TextField& f = *fields[i].f;
            RECT lr = { f.rc.left, f.rc.top - (int)(22 * scale), f.rc.right, f.rc.top };
            DrawTextA2(mem, fields[i].label, lr, CLR_TEXT_DIM, DT_LEFT | DT_SINGLELINE);
            FillRoundRect(mem, f.rc, RADIUS, CLR_CARD);
            StrokeRoundRect(mem, f.rc, RADIUS, f.focused ? CLR_ACCENT : CLR_BORDER, f.focused ? 2 : 1);
            RECT tr = f.rc;
            tr.left += (int)(12 * scale);
            tr.right -= (int)(8 * scale);
            DrawTextA2(mem, f.text, tr, CLR_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (f.focused && g_caretOn) {
                SIZE sz;
                GetTextExtentPoint32A(mem, f.text.c_str(), f.caret, &sz);
                int cx = f.rc.left + (int)(12 * scale) + sz.cx;
                RECT caretRc = { cx, f.rc.top + 8, cx + 1, f.rc.bottom - 8 };
                FillRect(mem, &caretRc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            }
        }

        CheckBox* cbs[] = { &cbFloat, &cbUnique };
        for (CheckBox* cb : cbs) {
            int box = (int)(20 * scale);
            RECT br = { cb->rc.left, cb->rc.top + (cb->rc.bottom - cb->rc.top - box) / 2,
                        cb->rc.left + box, cb->rc.top + (cb->rc.bottom - cb->rc.top + box) / 2 };
            FillRoundRect(mem, br, 4, cb->checked ? CLR_ACCENT : CLR_CARD);
            StrokeRoundRect(mem, br, 4, cb->checked ? CLR_ACCENT : CLR_BORDER);
            if (cb->checked) {
                HPEN pen = CreatePen(PS_SOLID, (int)(2 * scale), RGB(255,255,255));
                HGDIOBJ op = SelectObject(mem, pen);
                MoveToEx(mem, br.left + (int)(5 * scale), br.top + (int)(10 * scale), NULL);
                LineTo(mem, br.left + (int)(8 * scale), br.top + (int)(14 * scale));
                LineTo(mem, br.right - (int)(5 * scale), br.top + (int)(6 * scale));
                SelectObject(mem, op);
                DeleteObject(pen);
            }
            RECT tr = cb->rc;
            tr.left += box + (int)(8 * scale);
            DrawTextA2(mem, cb->label, tr, CLR_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }COLORREF btnColor = g_btnPressed ? CLR_ACCENT_DARK : CLR_ACCENT;
		if (g_hoverBtn == 0 && !g_btnPressed){
			btnColor = LightenColor(CLR_ACCENT);
        }FillRoundRect(mem, rcGenerateBtn, RADIUS, btnColor);
        DrawTextA2(mem, "生成随机数", rcGenerateBtn, RGB(255,255,255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        FillRoundRect(mem, rcResult, RADIUS, CLR_CARD);
        StrokeRoundRect(mem, rcResult, RADIUS, CLR_BORDER);
        RECT inner = rcResult;
        inner.left += (int)(12 * scale);
        inner.right -= (int)(12 * scale);
        inner.top += (int)(10 * scale);
        inner.bottom -= (int)(10 * scale);
        DrawTextA2(mem, resultText, inner, CLR_TEXT, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    else if (g_page == PAGE_SETTINGS) {
        int contentX = (int)(MENU_W * scale) + (int)(28 * scale);
        int y = (int)(24 * scale);
        RECT t = { contentX, y, cr.right, y + (int)(30 * scale) };
        DrawTextA2(mem, "设置", t, CLR_TEXT, DT_LEFT | DT_SINGLELINE);
        CheckBox* cbs[] = { &cbRemember, &cbAutoCopy };
        for (CheckBox* cb : cbs) {
            int box = (int)(20 * scale);
            RECT br = { cb->rc.left, cb->rc.top + (cb->rc.bottom - cb->rc.top - box) / 2,
                        cb->rc.left + box, cb->rc.top + (cb->rc.bottom - cb->rc.top + box) / 2 };
            FillRoundRect(mem, br, 4, cb->checked ? CLR_ACCENT : CLR_CARD);
            StrokeRoundRect(mem, br, 4, cb->checked ? CLR_ACCENT : CLR_BORDER);
            if (cb->checked) {
                HPEN pen = CreatePen(PS_SOLID, (int)(2 * scale), RGB(255,255,255));
                HGDIOBJ op = SelectObject(mem, pen);
                MoveToEx(mem, br.left + (int)(5 * scale), br.top + (int)(10 * scale), NULL);
                LineTo(mem, br.left + (int)(8 * scale), br.top + (int)(14 * scale));
                LineTo(mem, br.right - (int)(5 * scale), br.top + (int)(6 * scale));
                SelectObject(mem, op);
                DeleteObject(pen);
            }
            RECT tr = cb->rc;
            tr.left += box + (int)(8 * scale);
            DrawTextA2(mem, cb->label, tr, CLR_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }// 主题标题
		RECT themeTitle = { contentX, (int)(140 * scale), cr.right, (int)(166 * scale) };
		DrawTextA2(mem, "主题色", themeTitle, CLR_TEXT_DIM, DT_LEFT | DT_SINGLELINE);
		
		for (int i = 0; i < 8; ++i) {
		    RECT r = rcThemeItems[i];
		    bool selected = (g_themeIndex == i);
		    bool hover = (g_themeHover == i);
		
		    // 选中/悬停背景
		    if (selected){
				FillRoundRect(mem, r, RADIUS, CLR_ACCENT);
		    }else if (hover){
				FillRoundRect(mem, r, RADIUS, BlendColor(CLR_ACCENT, RGB(255,255,255), 88));
			}COLORREF dotColor = (i < 7) ? g_themes[i].accent : g_customColor;
		    int dotSize = (int)(16 * scale);
		    RECT dot = { r.left + (int)(10 * scale),
		                 r.top + (r.bottom - r.top - dotSize) / 2,
		                 r.left + (int)(10 * scale) + dotSize,
		                 r.top + (r.bottom - r.top + dotSize) / 2 };
		    FillRoundRect(mem, dot, dotSize / 2, dotColor);
		
		    // 文字
		    const char* label = (i < 7) ? g_themes[i].name : "自定义";
		    RECT tr = r;
		    tr.left = dot.right + (int)(8 * scale);
		    DrawTextA2(mem, label, tr, selected ? RGB(255,255,255) : CLR_TEXT,
		               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		
		    // 未选中时给个淡边框
		    if (!selected)
		        StrokeRoundRect(mem, r, RADIUS, hover ? CLR_ACCENT : CLR_BORDER, 1);
		}COLORREF saveColor = g_savePressed ? CLR_ACCENT_DARK : CLR_ACCENT;
		if (g_saveHover && !g_savePressed){
			saveColor = LightenColor(CLR_ACCENT);
        }FillRoundRect(mem, rcSaveBtn, RADIUS, saveColor);
        DrawTextA2(mem, "保存设置", rcSaveBtn, RGB(255,255,255),
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else if (g_page == PAGE_ABOUT) {
        int contentX = (int)(MENU_W * scale) + (int)(28 * scale);
        RECT t = { contentX, (int)(40 * scale), cr.right, (int)(100 * scale) };
        SelectObject(mem, g_fontTitle);
        DrawTextA2(mem, "随机生成器\n当前版本 1.4.1",t,CLR_TEXT,DT_LEFT);
        SelectObject(mem, g_font);
        RECT s = { contentX, (int)(90 * scale), cr.right, (int)(200 * scale) };
        DrawTextA2(mem, "By Soda & SoberVoog\n开发语言 C++\n\n修复了黑名单无法应用的问题\n更改了梦幻紫和菜单栏的颜色", s, CLR_TEXT_DIM, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }

    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(wnd, &ps);
}

TextField* FocusedField() {
    if (fMin.focused) return &fMin;
    if (fMax.focused) return &fMax;
    if (fCount.focused) return &fCount;
    return nullptr;
}
void ClearFieldFocus() {
    fMin.focused = fMax.focused = fCount.focused = false;
}
bool PtInRectR(RECT r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

// ---------- 窗口过程 ----------
LRESULT CALLBACK WindowProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            fMin.text = "1";
            fMax.text = "36";
            fCount.text = "1";
            fMin.caret = fMax.caret = fCount.caret = 1;
            cbFloat.label = "生成浮点数";
            cbUnique.label = "不重复";
            cbRemember.label = "记住上次输入";
            cbAutoCopy.label = "生成后自动复制";
            LoadSettings();
            Layout(wnd);
            SetTimer(wnd, 1, 500, NULL);
            return 0;
        }
        case WM_TIMER:
            if (wp == 1) {
                g_caretOn = !g_caretOn;
                if (FocusedField()) InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        case WM_SIZE:
            Layout(wnd);
            return 0;
        case WM_PAINT:
            Paint(wnd);
            return 0;
        case WM_MOUSEMOVE: {
		    POINT p = { LOWORD(lp), HIWORD(lp) };
		    int oldHoverNav = g_hoverNav, oldHoverBtn = g_hoverBtn;
		    bool oldSaveHover = g_saveHover;
		    int oldThemeHover = g_themeHover;          // ← 新增
		
		    g_hoverNav = -1;
		    for (int i = 0; i < PAGE_COUNT; ++i) {
		        RECT item = { (int)(12 * scale), (int)((NAV_TOP + i * (MENU_ITEM_H + 6)) * scale),
		                      (int)((MENU_W - 12) * scale), (int)((NAV_TOP + i * (MENU_ITEM_H + 6) + MENU_ITEM_H) * scale) };
		        if (PtInRectR(item, p)) { g_hoverNav = i; break; }
		    }
		
		    g_hoverBtn = (g_page == PAGE_GENERATE && PtInRectR(rcGenerateBtn, p)) ? 0 : -1;
		    g_saveHover = (g_page == PAGE_SETTINGS && PtInRectR(rcSaveBtn, p));
		
		    g_themeHover = -1;                          // ← 新增
		    if (g_page == PAGE_SETTINGS) {
		        for (int i = 0; i < 8; ++i) {
		            if (PtInRectR(rcThemeItems[i], p)) { g_themeHover = i; break; }
		        }
		    }
		
		    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, wnd, 0 };
		    TrackMouseEvent(&tme);
		    if (oldHoverNav != g_hoverNav || oldHoverBtn != g_hoverBtn ||
		        oldSaveHover != g_saveHover || oldThemeHover != g_themeHover)   // ← 加上主题悬停比较
		        InvalidateRect(wnd, NULL, FALSE);
		    return 0;                                   // ← 只在这里返回
		}
        case WM_MOUSELEAVE:
            g_hoverNav = -1;
            g_hoverBtn = -1;
            g_saveHover = false;
            g_themeHover = -1;
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            POINT p = { LOWORD(lp), HIWORD(lp) };
            for (int i = 0; i < PAGE_COUNT; ++i) {
                RECT item = { (int)(12 * scale), (int)((NAV_TOP + i * (MENU_ITEM_H + 6)) * scale),
                              (int)((MENU_W - 12) * scale), (int)((NAV_TOP + i * (MENU_ITEM_H + 6) + MENU_ITEM_H) * scale) };
                if (PtInRectR(item, p)) {
                    g_page = i;
                    ClearFieldFocus();
                    InvalidateRect(wnd, NULL, FALSE);
                    return 0;
                }
            }if (g_page == PAGE_SETTINGS) {
			    if (PtInRectR(cbRemember.rc, p)) { cbRemember.checked = !cbRemember.checked; InvalidateRect(wnd, NULL, FALSE); return 0; }
			    if (PtInRectR(cbAutoCopy.rc, p)) { cbAutoCopy.checked = !cbAutoCopy.checked; InvalidateRect(wnd, NULL, FALSE); return 0; }
			
			    // 主题点击
			    for (int i = 0; i < 8; ++i) {
			        if (PtInRectR(rcThemeItems[i], p)) {
			            if (i < 7) {
			                ApplyTheme(i);
			            } else {
			                CHOOSECOLORA cc = { sizeof(cc) };
			                cc.hwndOwner = wnd;
			                cc.rgbResult = g_customColor;
			                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
			                if (ChooseColorA(&cc)) {
			                    g_customColor = cc.rgbResult;
			                    ApplyTheme(7);
			                }
			            }
			            InvalidateRect(wnd, NULL, FALSE);
			            return 0;
			        }
			    }
			
			    if (PtInRectR(rcSaveBtn, p)) {
			        g_savePressed = true;
			        SetCapture(wnd);
			        InvalidateRect(wnd, NULL, FALSE);
			    }
			    return 0;
			}
            if (g_page != PAGE_GENERATE) return 0;
            ClearFieldFocus();
            TextField* fields[] = { &fMin, &fMax, &fCount };
            for (auto* f : fields) {
                if (PtInRectR(f->rc, p)) {
                    f->focused = true;
                    f->caret = (int)f->text.size();
                    g_caretOn = true;
                    SetFocus(wnd);
                    InvalidateRect(wnd, NULL, FALSE);
                    return 0;
                }
            }
            if (PtInRectR(cbFloat.rc, p)) { cbFloat.checked = !cbFloat.checked; InvalidateRect(wnd, NULL, FALSE); return 0; }
            if (PtInRectR(cbUnique.rc, p)) { cbUnique.checked = !cbUnique.checked; InvalidateRect(wnd, NULL, FALSE); return 0; }
            if (PtInRectR(rcGenerateBtn, p)) {
                g_btnPressed = true;
                SetCapture(wnd);
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p = { LOWORD(lp), HIWORD(lp) };
            if (g_btnPressed) {
                g_btnPressed = false;
                ReleaseCapture();
                if (PtInRectR(rcGenerateBtn, p)) GenerateRandom(wnd);
                InvalidateRect(wnd, NULL, FALSE);
            }
            if (g_savePressed) {
                g_savePressed = false;
                ReleaseCapture();
                if (PtInRectR(rcSaveBtn, p) && g_page == PAGE_SETTINGS) SaveSettings(wnd);
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_CHAR: {
            TextField* f = FocusedField();
            if (f && wp >= 32) {
                InsertChar(*f, (char)wp);
                g_caretOn = true;
                InvalidateRect(wnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            TextField* f = FocusedField();
            if (f) {
                if (wp == VK_BACK) { HandleBackspace(*f); g_caretOn = true; InvalidateRect(wnd, NULL, FALSE); }
                else if (wp == VK_DELETE) { HandleDelete(*f); g_caretOn = true; InvalidateRect(wnd, NULL, FALSE); }
                else if (wp == VK_LEFT && f->caret > 0) { f->caret--; g_caretOn = true; InvalidateRect(wnd, NULL, FALSE); }
                else if (wp == VK_RIGHT && f->caret < (int)f->text.size()) { f->caret++; g_caretOn = true; InvalidateRect(wnd, NULL, FALSE); }
            }
            return 0;
        }
        case WM_KILLFOCUS:
            ClearFieldFocus();
            InvalidateRect(wnd, NULL, FALSE);
            return 0;
        case WM_DESTROY:
            KillTimer(wnd, 1);
            DeleteObject(g_font);
            DeleteObject(g_fontTitle);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(wnd, msg, wp, lp);
    }
    return 0;
}

// ---------- 入口 ----------
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    {
        HMODULE user32 = GetModuleHandleA("user32.dll"), shcore = LoadLibraryA("shcore.dll");
        if (user32) {
            SetProcessDpiAwarenessContextFunc SetProcessDpiAwarenessContext =
                (SetProcessDpiAwarenessContextFunc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
            if (SetProcessDpiAwarenessContext) SetProcessDpiAwarenessContext((HANDLE)-4);
        }
        if (shcore) {
            SetProcessDpiAwarenessFunc SetProcessDpiAwareness =
                (SetProcessDpiAwarenessFunc)GetProcAddress(shcore, "SetProcessDpiAwareness");
            if (SetProcessDpiAwareness) SetProcessDpiAwareness(2);
            FreeLibrary(shcore);
        } else {
            SetProcessDPIAwareFunc SetProcessDPIAware =
                (SetProcessDPIAwareFunc)GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDPIAware");
            if (SetProcessDPIAware) SetProcessDPIAware();
        }
        if (HDC dc = GetDC(NULL)) {
            int dpi = GetDeviceCaps(dc, LOGPIXELSX);
            ReleaseDC(NULL, dc);
            scale = static_cast<float>(dpi / 96.0);
        }
    }
    if (scale <= 0) scale = 1.0;

    g_font = CreateFontA((int)(16 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Microsoft YaHei");
    g_fontTitle = CreateFontA((int)(22 * scale), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Microsoft YaHei");
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = "Random";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassA(&wc);
    HWND wnd = CreateWindowA(
        wc.lpszClassName, "随机生成器 V1.4.1", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN) - 720*scale) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - 600*scale) / 2,
        720*scale,600*scale, NULL, NULL, inst, NULL
    );

    srand((unsigned)time(NULL));

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
