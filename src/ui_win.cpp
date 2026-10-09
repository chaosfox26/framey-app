#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <string>

#include "core.h"

namespace {

struct Theme {
  COLORREF bg, fd, tx, ac;
};

constexpr Theme kTheme[] = {
    {RGB(0x14, 0x00, 0x1c), RGB(0x2c, 0x00, 0x3c), RGB(0xff, 0xde, 0xff), RGB(0xff, 0x00, 0xc8)},
    {RGB(0x00, 0x08, 0x1a), RGB(0x00, 0x1a, 0x3c), RGB(0xd4, 0xe8, 0xff), RGB(0x00, 0x96, 0xff)},
    {RGB(0x00, 0x00, 0x00), RGB(0x18, 0x18, 0x18), RGB(0xd6, 0xff, 0xd6), RGB(0x00, 0xff, 0x50)},
    {RGB(0xee, 0xeb, 0xde), RGB(0xff, 0xff, 0xfa), RGB(0x18, 0x18, 0x18), RGB(0x18, 0x18, 0x18)},
};

enum Id { TITLE = 100, SW, BEFORE = 110, S1, S2, S3, INTRO, HOSTL, PWL, HOST, PW, FAN, INSTALL, CHECK, REMOVE, SRCL, SRC, PICK, ADD, STATUS, LOG };

struct Ctl {
  int id;
  const wchar_t* cls;
  const wchar_t* text;
  DWORD style;
};

constexpr DWORD kEdit = WS_TABSTOP | ES_AUTOHSCROLL, kBtn = WS_TABSTOP | BS_OWNERDRAW, kStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

constexpr Ctl kCtl[] = {
    {BEFORE, L"STATIC", L"Before you start, on the headset:", 0},
    {S1, L"STATIC", L"1. Steam Settings > System > Enable Developer Mode.", 0},
    {S2, L"STATIC", L"2. Developer (left menu) > scroll to the bottom > Set User Password.", 0},
    {S3, L"STATIC", L"3. Keep the headset awake and on the same network as this computer.", 0},
    {INTRO, L"STATIC",
     L"Then enter its address and password and click Install. The password is needed the first time and for Fan Control, and is never saved.", 0},
    {HOSTL, L"STATIC", L"Headset address", 0},
    {PWL, L"STATIC", L"Password (never saved)", 0},
    {HOST, L"EDIT", L"", kEdit},
    {PW, L"EDIT", L"", kEdit | ES_PASSWORD},
    {FAN, L"BUTTON", L"Also install Fan Control", kBtn},
    {INSTALL, L"BUTTON", L"Install / Update", kBtn},
    {CHECK, L"BUTTON", L"Check for updates", kBtn},
    {REMOVE, L"BUTTON", L"Remove", kBtn},
    {SRCL, L"STATIC", L"Install a plugin: .zip file or GitHub link", 0},
    {SRC, L"EDIT", L"", kEdit},
    {PICK, L"BUTTON", L"Browse...", kBtn},
    {ADD, L"BUTTON", L"Add plugin", kBtn},
    {STATUS, L"STATIC", L"", 0},
    {LOG, L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL},
};

HWND w;
HFONT f, big;
HBRUSH bgb, fdb, acb;
int cur, th, dpi;
bool locked, fan;
size_t since;
std::wstring last;

int u(int x) { return MulDiv(x, dpi, 96); }

HWND c(int id) { return GetDlgItem(w, id); }

std::wstring wide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring r(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
  return r;
}

std::string utf8(const std::wstring& s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string r(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
  return r;
}

std::string text(int id) {
  int n = GetWindowTextLengthW(c(id));
  std::wstring r(n + 1, 0);
  GetWindowTextW(c(id), r.data(), n + 1);
  r.resize(n);
  return utf8(r);
}

COLORREF mix(COLORREF a, COLORREF b) { return RGB((GetRValue(a) * 2 + GetRValue(b) * 3) / 5, (GetGValue(a) * 2 + GetGValue(b) * 3) / 5, (GetBValue(a) * 2 + GetBValue(b) * 3) / 5); }

void box(HDC dc, RECT r, COLORREF edge, int n, COLORREF fill) {
  HBRUSH b = CreateSolidBrush(edge);
  FillRect(dc, &r, b);
  DeleteObject(b);
  InflateRect(&r, -n, -n);
  b = CreateSolidBrush(fill);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

void recolor() {
  const Theme& t = kTheme[cur];
  DeleteObject(bgb);
  DeleteObject(fdb);
  DeleteObject(acb);
  bgb = CreateSolidBrush(t.bg);
  fdb = CreateSolidBrush(t.fd);
  acb = CreateSolidBrush(t.ac);
  RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void layout(bool center) {
  dpi = GetDpiForWindow(w);
  HFONT of = f, ob = big;
  f = CreateFontW(-u(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
  big = CreateFontW(-u(32), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
  EnumChildWindows(w, [](HWND h, LPARAM) -> BOOL { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); return TRUE; }, 0);
  SendMessageW(c(TITLE), WM_SETFONT, (WPARAM)big, TRUE);
  DeleteObject(of);
  DeleteObject(ob);
  HDC dc = GetDC(w);
  HGDIOBJ prev = SelectObject(dc, f);
  TEXTMETRICW tm;
  GetTextMetricsW(dc, &tm);
  SelectObject(dc, prev);
  ReleaseDC(w, dc);
  th = tm.tmHeight;

  auto put = [](int id, int x, int y, int cw, int ch) { SetWindowPos(c(id), nullptr, x, y, cw, ch, SWP_NOZORDER | SWP_NOACTIVATE); };
  int pad = u(8), g = u(10), L = u(24), W = u(592), lh = th * 4 / 3, H = th + 2 * pad, half = (W - u(12)) / 2, bw = (W - 2 * g) / 3;
  auto edit = [&](int id, int x, int y, int cw) { put(id, x + pad, y + pad, cw - 2 * pad, th); };
  int y = u(20);
  put(TITLE, L, y, u(320), u(40));
  for (int i = 0; i < 4; i++) put(SW + i, L + W - (4 - i) * u(30) - (3 - i) * u(8), u(24), u(30), u(30));
  y = u(70);
  put(BEFORE, L, y, W, lh);
  y += lh + u(4);
  for (int i = 0; i < 3; i++, y += lh) put(S1 + i, L + u(22), y, W - u(22), lh);
  y += g;
  put(INTRO, L, y, W, 2 * lh);
  y += 2 * lh + g;
  put(HOSTL, L, y, half, lh);
  put(PWL, L + half + u(12), y, half, lh);
  y += lh + u(2);
  edit(HOST, L, y, half);
  edit(PW, L + half + u(12), y, half);
  y += H + g;
  put(FAN, L, y, u(280), lh + u(2));
  y += lh + u(2) + g;
  put(INSTALL, L, y, bw, H);
  put(CHECK, L + bw + g, y, bw, H);
  put(REMOVE, L + 2 * (bw + g), y, bw, H);
  y += H + 2 * g;
  put(SRCL, L, y, W, lh);
  y += lh + u(2);
  int pick = u(100), add = u(110), gap = u(12), ew = W - pick - add - 2 * gap;
  edit(SRC, L, y, ew);
  put(PICK, L + ew + gap, y, pick, H);
  put(ADD, L + ew + gap + pick + gap, y, add, H);
  y += H + g;
  put(STATUS, L, y, W, lh);
  y += lh + u(2);

  RECT nc{};
  AdjustWindowRectExForDpi(&nc, kStyle, FALSE, 0, dpi);
  MONITORINFO mi{sizeof mi};
  GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi);
  RECT wa = mi.rcWork;
  int tail = 2 * pad + u(24);
  int logh = std::clamp(int(wa.bottom - wa.top - (nc.bottom - nc.top) - y - tail), u(60), u(190));
  put(LOG, L + pad, y + pad, W - 2 * pad, logh);
  RECT r{0, 0, u(640), y + logh + tail};
  AdjustWindowRectExForDpi(&r, kStyle, FALSE, 0, dpi);
  int ww = r.right - r.left, wh = r.bottom - r.top;
  SetWindowPos(w, nullptr, wa.left + (wa.right - wa.left - ww) / 2, std::max<int>(wa.top, wa.top + (wa.bottom - wa.top - wh) / 2), ww, wh,
               SWP_NOZORDER | SWP_NOACTIVATE | (center ? 0 : SWP_NOMOVE));
  InvalidateRect(w, nullptr, TRUE);
}

void build() {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  auto mk = [&](int id, const wchar_t* cls, const wchar_t* t, DWORD st) {
    CreateWindowExW(0, cls, t, WS_CHILD | WS_VISIBLE | st, 0, 0, 0, 0, w, (HMENU)(INT_PTR)id, inst, nullptr);
  };
  mk(TITLE, L"STATIC", L"FRAMEY", 0);
  for (int i = 0; i < 4; i++) mk(SW + i, L"BUTTON", wide(kThemes[i]).c_str(), kBtn);
  for (const Ctl& k : kCtl) mk(k.id, k.cls, k.text, k.style);
  SendMessageW(c(LOG), EM_LIMITTEXT, 0, 0);
  SendMessageW(c(SRC), EM_SETCUEBANNER, FALSE, (LPARAM)L"https://github.com/owner/repo");
  std::string host = saved("host.txt"), theme = saved("theme.txt");
  SetWindowTextW(c(HOST), host.empty() ? L"frame" : wide(host).c_str());
  for (int i = 0; i < 4; i++)
    if (theme == kThemes[i]) cur = i;
  recolor();
  layout(true);
}

void poll() {
  Snap s = snapshot(since);
  since = s.next;
  if (!s.log.empty()) {
    std::wstring t;
    for (wchar_t ch : wide(s.log)) {
      if (ch == L'\r') continue;
      if (ch == L'\n') t += L'\r';
      t += ch;
    }
    SendMessageW(c(LOG), EM_SETSEL, (WPARAM)-1, -1);
    SendMessageW(c(LOG), EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
    SendMessageW(c(LOG), WM_VSCROLL, SB_BOTTOM, 0);
  }
  std::wstring st = wide(s.status);
  if (st != last) {
    last = st;
    SetWindowTextW(c(STATUS), st.c_str());
  }
  if (s.busy != locked) {
    locked = s.busy;
    for (int id : {INSTALL, CHECK, REMOVE, PICK, ADD}) EnableWindow(c(id), !locked);
    if (!locked) SetWindowTextW(c(PW), L"");
  }
}

void run(const char* action) {
  start_job(action, text(HOST), text(PW), fan, text(SRC));
  poll();
}

bool ask(const wchar_t* q) { return MessageBoxW(w, q, L"Framey App", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES; }

void browse() {
  wchar_t p[1024] = {};
  OPENFILENAMEW o{sizeof o};
  o.hwndOwner = w;
  o.lpstrFilter = L"Plugin archives\0*.zip;*.gz;*.tgz\0";
  o.lpstrFile = p;
  o.nMaxFile = 1024;
  o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&o)) SetWindowTextW(c(SRC), p);
}

void draw(DRAWITEMSTRUCT* d) {
  const Theme& t = kTheme[cur];
  HDC dc = d->hDC;
  int id = (int)d->CtlID;
  bool off = d->itemState & ODS_DISABLED, down = d->itemState & ODS_SELECTED;
  RECT r = d->rcItem;
  wchar_t s[64];
  GetWindowTextW(d->hwndItem, s, 64);
  FillRect(dc, &r, bgb);
  SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  if (id >= SW && id < SW + 4) {
    const Theme& s = kTheme[id - SW];
    box(dc, r, t.tx, id - SW == cur ? u(3) : u(1), s.bg);
    RECT i{(r.left + r.right) / 2, r.top + u(3), r.right - u(3), r.bottom - u(3)};
    box(dc, i, s.ac, 0, s.ac);
  } else if (id == FAN) {
    RECT b{r.left, r.top + (r.bottom - r.top - th) / 2, r.left + th, 0};
    b.bottom = b.top + th;
    box(dc, b, t.ac, u(2), t.fd);
    if (fan) {
      InflateRect(&b, -u(4), -u(4));
      box(dc, b, t.ac, 0, t.ac);
    }
    r.left += th + u(8);
    SetTextColor(dc, t.tx);
    DrawTextW(dc, s, -1, &r, DT_SINGLELINE | DT_VCENTER);
  } else {
    COLORREF fg = off ? mix(t.ac, t.bg) : t.ac;
    box(dc, r, fg, u(2), down ? fg : t.bg);
    SetTextColor(dc, down ? t.bg : fg);
    DrawTextW(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
    r = d->rcItem;
    InflateRect(&r, -u(4), -u(4));
    DrawFocusRect(dc, &r);
  }
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
  const Theme& t = kTheme[cur];
  switch (m) {
    case WM_CREATE:
      w = h;
      build();
      SetTimer(h, 1, 500, nullptr);
      return 0;
    case WM_TIMER:
      poll();
      return 0;
    case WM_DPICHANGED: {
      auto* r = (RECT*)lp;
      SetWindowPos(h, nullptr, r->left, r->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      layout(false);
      return 0;
    }
    case WM_ERASEBKGND: {
      HDC dc = (HDC)wp;
      RECT r;
      GetClientRect(h, &r);
      FillRect(dc, &r, bgb);
      for (int id : {HOST, PW, SRC, LOG}) {
        GetWindowRect(c(id), &r);
        MapWindowPoints(nullptr, h, (POINT*)&r, 2);
        InflateRect(&r, u(8), u(8));
        FillRect(dc, &r, fdb);
        FrameRect(dc, &r, acb);
      }
      return 1;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
      int id = GetDlgCtrlID((HWND)lp);
      bool field = m == WM_CTLCOLOREDIT || id == LOG;
      SetTextColor((HDC)wp, id == TITLE || id == STATUS ? t.ac : t.tx);
      SetBkColor((HDC)wp, field ? t.fd : t.bg);
      return (LRESULT)(field ? fdb : bgb);
    }
    case WM_DRAWITEM:
      draw((DRAWITEMSTRUCT*)lp);
      return TRUE;
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (HIWORD(wp) != BN_CLICKED && HIWORD(wp) != BN_DOUBLECLICKED) break;
      if (id >= SW && id < SW + 4) {
        cur = id - SW;
        save_theme(kThemes[cur]);
        recolor();
      } else if (id == FAN) {
        fan = !fan;
        InvalidateRect(c(FAN), nullptr, FALSE);
      } else if (id == INSTALL) {
        if (ask(L"Install or update Framey on the headset? If SteamVR is running it will be restarted, which ends the current VR session.")) run("install");
      } else if (id == CHECK) {
        run("check");
      } else if (id == REMOVE) {
        if (ask(L"Remove Framey, Fan Control, all plugins and their saved settings from the headset, and this app's key and data from this computer?")) run("remove");
      } else if (id == ADD) {
        if (ask(L"A plugin runs code on your headset, inside Steam's interface. Only install plugins you trust. If SteamVR is running it will be restarted, which ends the current VR session. Continue?")) run("plugin");
      } else if (id == PICK) {
        browse();
      }
      return 0;
    }
    case WM_CLOSE:
      if (locked || busy()) return 0;
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, wp, lp);
}

}

int ui_run() {
  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"FrameyApp";
  RegisterClassW(&wc);
  CreateWindowExW(0, wc.lpszClassName, L"Framey App", kStyle, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, inst, nullptr);
  ShowWindow(w, SW_SHOW);
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (IsDialogMessageW(w, &m)) continue;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return (int)m.wParam;
}
