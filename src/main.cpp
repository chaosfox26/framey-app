#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

constexpr const char* kOwner = "chaosfox26";
constexpr const char* kRepos[] = {"framey", "frame-fan"};
constexpr const char* kHome = "/home/steamos";

enum { ID_HOST = 100, ID_PASS, ID_FAN, ID_INSTALL, ID_CHECK, ID_REMOVE, ID_LOG, ID_STATUS, ID_SWATCH = 200 };
enum { WM_LOG = WM_APP + 1, WM_STATUS, WM_DONE };

struct Theme {
  const char* name;
  COLORREF bg, field, text, accent;
};

constexpr Theme kThemes[] = {
    {"Magenta", RGB(20, 0, 28), RGB(44, 0, 60), RGB(255, 222, 255), RGB(255, 0, 200)},
    {"Blue", RGB(0, 8, 26), RGB(0, 26, 60), RGB(212, 232, 255), RGB(0, 150, 255)},
    {"Black", RGB(0, 0, 0), RGB(24, 24, 24), RGB(214, 255, 214), RGB(0, 255, 80)},
    {"White", RGB(238, 235, 222), RGB(255, 255, 250), RGB(24, 24, 24), RGB(24, 24, 24)},
};

HWND g_wnd, g_title, g_host, g_pass, g_fan, g_log, g_status;
HWND g_buttons[3], g_swatches[4];
HFONT g_font, g_big;
HBRUSH g_bg, g_field;
int g_dpi = 96, g_theme = 0;
bool g_fan_on = false;
std::string g_pw, g_target;
fs::path g_dir;
std::atomic<bool> g_busy;

int S(int v) { return MulDiv(v, g_dpi, 96); }
const Theme& T() { return kThemes[g_theme]; }

void apply_theme(int i) {
  g_theme = i;
  if (g_bg) DeleteObject(g_bg);
  if (g_field) DeleteObject(g_field);
  g_bg = CreateSolidBrush(T().bg);
  g_field = CreateSolidBrush(T().field);
  if (!g_wnd) return;
  BOOL dark = i != 3;
  DwmSetWindowAttribute(g_wnd, 20, &dark, sizeof dark);
  COLORREF bg = T().bg, fg = T().text;
  DwmSetWindowAttribute(g_wnd, 35, &bg, sizeof bg);
  DwmSetWindowAttribute(g_wnd, 36, &fg, sizeof fg);
  SetWindowTheme(g_log, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
  RedrawWindow(g_wnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
  fs::create_directories(g_dir);
  std::ofstream(g_dir / "theme.txt") << i;
}

std::string text(HWND h) {
  std::string s(GetWindowTextLengthA(h) + 1, '\0');
  s.resize(GetWindowTextA(h, s.data(), (int)s.size()));
  return s;
}

void post(UINT msg, std::string s) { PostMessageA(g_wnd, msg, 0, (LPARAM) new std::string(std::move(s))); }
void log(std::string s) { post(WM_LOG, std::move(s)); }

std::string self() {
  char p[MAX_PATH];
  return std::string(p, GetModuleFileNameA(nullptr, p, MAX_PATH));
}

struct Run {
  std::string out;
  DWORD code = 1;
};

Run run(const std::string& cmd, const std::string& input = {}, bool askpass = false) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE outR, outW, inR, inW;
  CreatePipe(&outR, &outW, &sa, 0);
  CreatePipe(&inR, &inW, &sa, 0);
  SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
  if (askpass) {
    SetEnvironmentVariableA("FRAMEY_ASKPASS", "1");
    SetEnvironmentVariableA("FRAMEY_PW", g_pw.c_str());
    SetEnvironmentVariableA("SSH_ASKPASS", self().c_str());
    SetEnvironmentVariableA("SSH_ASKPASS_REQUIRE", "force");
  }
  STARTUPINFOA si{sizeof si};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = inR;
  si.hStdOutput = si.hStdError = outW;
  PROCESS_INFORMATION pi{};
  std::string line = cmd;
  Run r;
  if (CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(inR);
    CloseHandle(outW);
    DWORD n;
    if (!input.empty()) WriteFile(inW, input.data(), (DWORD)input.size(), &n, nullptr);
    CloseHandle(inW);
    char buf[4096];
    while (ReadFile(outR, buf, sizeof buf, &n, nullptr) && n) r.out.append(buf, n);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &r.code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  } else {
    r.out = "could not start: " + cmd.substr(0, cmd.find(' '));
    for (HANDLE h : {inR, inW, outW}) CloseHandle(h);
  }
  CloseHandle(outR);
  if (askpass)
    for (auto v : {"FRAMEY_ASKPASS", "FRAMEY_PW", "SSH_ASKPASS", "SSH_ASKPASS_REQUIRE"}) SetEnvironmentVariableA(v, nullptr);
  return r;
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

std::string tail(const std::string& s, size_t lines = 4) {
  std::vector<std::string> v;
  std::istringstream in(s);
  for (std::string l; std::getline(in, l);)
    if (!trim(l).empty()) v.push_back(trim(l));
  std::string out;
  for (size_t i = v.size() > lines ? v.size() - lines : 0; i < v.size(); i++) out += "  " + v[i] + "\n";
  return out;
}

std::string ssh_opts() {
  return std::format("-i \"{}\" -o IdentitiesOnly=yes -o UserKnownHostsFile=\"{}\" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=12",
                     (g_dir / "id_ed25519").string(), (g_dir / "known_hosts").string());
}

Run ssh(const std::string& remote, const std::string& in = {}) {
  return run(std::format("ssh {} -o BatchMode=yes steamos@{} \"{}\"", ssh_opts(), g_target, remote), in);
}

bool valid_host(const std::string& h) {
  if (h.empty() || h.size() > 100) return false;
  for (char c : h)
    if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != ':') return false;
  return true;
}

bool need_tools() {
  for (const char* t : {"ssh", "scp", "ssh-keygen", "tar", "curl"})
    if (run(std::format("where {}", t)).code) {
      log(std::format("Missing Windows tool: {}.exe. Turn on the OpenSSH Client in Windows optional features.", t));
      return false;
    }
  return true;
}

bool authorize() {
  if (!fs::exists(g_dir / "id_ed25519")) {
    run(std::format("ssh-keygen -q -t ed25519 -N \"\" -C framey-app -f \"{}\"", (g_dir / "id_ed25519").string()));
    if (!fs::exists(g_dir / "id_ed25519")) return log("Could not create an SSH key."), false;
  }
  if (!ssh("true").code) return true;
  if (g_pw.empty()) return log("First time on this headset: enter the password you set in its Developer settings."), false;
  log("Adding this app's key to the headset so it can reconnect without the password...");
  std::ifstream f(g_dir / "id_ed25519.pub");
  std::string pub;
  std::getline(f, pub);
  pub = trim(pub);
  auto r = run(std::format("ssh -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1 -o UserKnownHostsFile=\"{}\" -o StrictHostKeyChecking=accept-new -o ConnectTimeout=12 steamos@{} \"sh -s\"",
                           (g_dir / "known_hosts").string(), g_target),
               std::format("umask 077\nmkdir -p ~/.ssh\ntouch ~/.ssh/authorized_keys\ngrep -qxF '{0}' ~/.ssh/authorized_keys || echo '{0}' >> ~/.ssh/authorized_keys\n", pub), true);
  if (r.code || ssh("true").code) {
    log("Could not log in. Check the address and the password you set in Developer settings.\n" + tail(r.out));
    return false;
  }
  return true;
}

std::string latest_sha(const std::string& repo) {
  auto r = run(std::format("curl -fsSL --max-time 20 -H \"Accept: application/vnd.github.sha\" https://api.github.com/repos/{}/{}/commits/main", kOwner, repo));
  auto s = trim(r.out);
  return !r.code && s.size() == 40 ? s : "";
}

bool bundled() { return FindResourceA(nullptr, "PAYLOAD", RT_RCDATA) != nullptr; }

bool unpack_bundle(fs::path to) {
  HRSRC h = FindResourceA(nullptr, "PAYLOAD", RT_RCDATA);
  if (!h) return false;
  HGLOBAL g = LoadResource(nullptr, h);
  fs::create_directories(to);
  std::ofstream(to / "payload.tar", std::ios::binary).write((const char*)LockResource(g), SizeofResource(nullptr, h));
  return !run(std::format("tar -xf \"{}\" -C \"{}\"", (to / "payload.tar").string(), to.string())).code;
}

std::string stage(const std::string& repo, const fs::path& root) {
  fs::path dest = root / repo;
  fs::remove_all(dest);
  fs::create_directories(dest);
  auto tgz = (root / (repo + ".tar.gz")).string();
  std::string sha = latest_sha(repo);
  if (!sha.empty() &&
      !run(std::format("curl -fsSL --max-time 60 -o \"{}\" https://github.com/{}/{}/archive/refs/heads/main.tar.gz", tgz, kOwner, repo)).code &&
      !run(std::format("tar -xzf \"{}\" -C \"{}\" --strip-components 1", tgz, dest.string())).code) {
    std::ofstream(dest / ".version") << sha;
    log(std::format("{}: downloaded from GitHub ({})", repo, sha.substr(0, 7)));
    return sha;
  }
  fs::remove_all(dest);
  fs::path bundle = g_dir / "bundle";
  fs::remove_all(bundle);
  if (!bundled() || !unpack_bundle(bundle) || !fs::exists(bundle / repo)) return log(std::format("{}: GitHub is unreachable and there is no bundled copy.", repo)), "";
  fs::copy(bundle / repo, dest, fs::copy_options::recursive);
  std::ifstream v(dest / ".version");
  std::string s;
  std::getline(v, s);
  log(std::format("{}: GitHub unreachable, using the bundled copy ({})", repo, s.substr(0, 7)));
  return s.empty() ? "bundled" : s;
}

void upload(const std::string& repo, const fs::path& root) {
  auto r = run(std::format("scp -r -q -o BatchMode=yes {} \"{}\" steamos@{}:{}/", ssh_opts(), (root / repo).string(), g_target, kHome));
  if (r.code) throw std::runtime_error("upload failed\n" + tail(r.out));
}

const char* kService =
    "[Unit]\nDescription=Framey\n\n[Service]\nEnvironment=PYTHONDONTWRITEBYTECODE=1\n"
    "ExecStart=/usr/bin/python3 /home/steamos/framey/framey.py\nRestart=always\nRestartSec=3\nNice=19\n"
    "CPUSchedulingPolicy=idle\nCPUWeight=1\nMemoryHigh=40M\nMemoryMax=60M\n\n[Install]\nWantedBy=default.target\n";

void install(bool fan) {
  if (!need_tools()) return;
  fs::create_directories(g_dir);
  log("[1/5] Connecting to the headset...");
  if (!authorize()) return;
  fs::path root = g_dir / "stage";
  fs::create_directories(root);
  std::vector<std::string> repos = {"framey"};
  if (fan) repos.push_back("frame-fan");
  log("[2/5] Getting the latest versions...");
  for (auto& r : repos)
    if (stage(r, root).empty()) return;
  log("[3/5] Copying files to the headset...");
  for (auto& r : repos) upload(r, root);
  log("[4/5] Setting up Framey...");
  std::string script = "set -e\ncd /home/steamos\nmkdir -p framey/plugins .config/systemd/user\ncat > .config/systemd/user/framey.service <<'EOT'\n";
  script += kService;
  script += "EOT\n";
  if (fan) script += "ln -sfn /home/steamos/frame-fan framey/plugins/fan\n";
  script += "systemctl --user daemon-reload\nsystemctl --user enable framey.service\nsystemctl --user restart framey.service\n";
  auto r = ssh("sh -s", script);
  if (r.code) return log("Framey setup failed.\n" + tail(r.out));
  if (fan) {
    if (g_pw.empty()) return log("Fan Control needs the headset password for its system step. Enter it and run Install again.");
    log("[5/5] Installing Fan Control (about 20 seconds)...");
    r = ssh(std::format("sudo -S -p '' bash {}/frame-fan/install-root.sh", kHome), g_pw + "\n");
    if (r.code) return log("Fan Control install failed. Is the password right?\n" + tail(r.out));
    log(tail(r.out, 3));
  }
  r = ssh("systemctl --user is-active framey.service; ss -ltn 2>/dev/null | grep -q ':8080 ' && echo debug-port-open || echo debug-port-closed");
  std::string o = r.out;
  if (o.find("debug-port-closed") != std::string::npos)
    log("Warning: Steam's debug port is not open. Make sure Steam is running on the headset.");
  if (o.find("active") != 0 && o.find("\nactive") == std::string::npos) return log("Framey is not running.\n" + tail(o));
  log("\nDone. Put the headset on and tap the Framey icon in the bottom bar.");
  post(WM_STATUS, "Installed.");
}

void check() {
  if (!need_tools()) return;
  fs::create_directories(g_dir);
  if (!authorize()) return;
  auto r = ssh("for r in framey frame-fan; do echo $r=$(cat /home/steamos/$r/.version 2>/dev/null); done");
  std::string status;
  for (auto repo : kRepos) {
    auto at = r.out.find(std::string(repo) + "=");
    std::string have;
    if (at != std::string::npos) {
      have = r.out.substr(at + strlen(repo) + 1, 40);
      if (have.find_first_of("\r\n") != std::string::npos) have.clear();
    }
    std::string now = latest_sha(repo);
    std::string line = std::format("{}: {}, latest {}", repo, have.empty() ? "not installed" : have.substr(0, 7), now.empty() ? "unknown" : now.substr(0, 7));
    if (!have.empty() && !now.empty()) line += have == now ? " (up to date)" : " (update available)";
    log(line);
    status += line + "   ";
  }
  post(WM_STATUS, status);
}

void remove_all_of_it() {
  if (!need_tools() || !authorize()) return;
  if (g_pw.empty()) return log("Removing Fan Control needs the headset password. Enter it and try again.");
  log("Removing Fan Control (restores stock fan control)...");
  auto r = ssh(std::format("test -f {0}/frame-fan/uninstall-root.sh && sudo -S -p '' bash {0}/frame-fan/uninstall-root.sh", kHome), g_pw + "\n");
  log(tail(r.out, 2));
  log("Removing Framey...");
  r = ssh("sh -s", "systemctl --user disable --now framey.service\nrm -f ~/.config/systemd/user/framey.service\nsystemctl --user daemon-reload\nrm -rf ~/framey ~/frame-fan ~/.config/framey ~/.config/frame-fan\n");
  log(r.code ? "Removal had errors.\n" + tail(r.out) : "Done. Everything the app installed is removed.");
  post(WM_STATUS, "Removed.");
}

void start(void (*job)(bool), bool arg) {
  g_target = trim(text(g_host));
  g_pw = text(g_pass);
  if (!valid_host(g_target)) return (void)MessageBoxA(g_wnd, "Enter the headset address, for example frame or its IP address.", "Framey App", MB_ICONINFORMATION);
  if (g_busy.exchange(true)) return;
  for (HWND b : g_buttons) EnableWindow(b, FALSE);
  fs::create_directories(g_dir);
  std::ofstream(g_dir / "host.txt") << g_target;
  std::thread([job, arg] {
    try {
      job(arg);
    } catch (const std::exception& e) {
      log(std::string("Error: ") + e.what());
    }
    g_pw.assign(g_pw.size(), '\0');
    g_busy = false;
    PostMessageA(g_wnd, WM_DONE, 0, 0);
  }).detach();
}

HWND make(const char* cls, const char* label, DWORD style, int x, int y, int w, int h, int id = 0, DWORD ex = 0) {
  HWND c = CreateWindowExA(ex, cls, label, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
  SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
  return c;
}

LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
    case WM_CREATE: {
      g_wnd = w;
      g_font = CreateFontA(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
      g_big = CreateFontA(-S(30), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
      g_title = make("STATIC", "FRAMEY", SS_LEFT, 24, 14, 300, 44);
      SendMessageA(g_title, WM_SETFONT, (WPARAM)g_big, TRUE);
      for (int i = 0; i < 4; i++) g_swatches[i] = make("BUTTON", kThemes[i].name, BS_OWNERDRAW, 424 + i * 38, 22, 30, 30, ID_SWATCH + i);
      make("STATIC",
           "Before you start, on the headset:\r\n"
           "1.  Steam Settings > System > Enable Developer Mode.\r\n"
           "2.  Developer (left menu) > scroll to the bottom > Set User Password.\r\n"
           "3.  Keep the headset awake and on the same network as this PC.\r\n\r\n"
           "Then enter its address and password and click Install. The password is needed the first time and for Fan Control, and is never saved.",
           SS_LEFT, 24, 66, 552, 148);
      make("STATIC", "Headset address", SS_LEFT, 24, 222, 250, 20);
      g_host = make("EDIT", "frame", ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER, 24, 244, 260, 28, ID_HOST);
      make("STATIC", "Password (never saved)", SS_LEFT, 300, 222, 276, 20);
      g_pass = make("EDIT", "", ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP | WS_BORDER, 300, 244, 276, 28, ID_PASS);
      g_fan = make("BUTTON", "Also install Fan Control", BS_OWNERDRAW | WS_TABSTOP, 24, 284, 400, 24, ID_FAN);
      g_buttons[0] = make("BUTTON", "Install / Update", BS_OWNERDRAW | WS_TABSTOP, 24, 320, 170, 36, ID_INSTALL);
      g_buttons[1] = make("BUTTON", "Check for updates", BS_OWNERDRAW | WS_TABSTOP, 204, 320, 190, 36, ID_CHECK);
      g_buttons[2] = make("BUTTON", "Remove", BS_OWNERDRAW | WS_TABSTOP, 404, 320, 172, 36, ID_REMOVE);
      g_status = make("STATIC", "", SS_LEFT | SS_ENDELLIPSIS, 24, 366, 552, 22, ID_STATUS);
      g_log = make("EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, 24, 394, 552, 250, ID_LOG);
      std::ifstream f(g_dir / "host.txt");
      std::string h;
      if (std::getline(f, h) && !h.empty()) SetWindowTextA(g_host, h.c_str());
      apply_theme(g_theme);
      return 0;
    }
    case WM_DRAWITEM: {
      auto* d = (DRAWITEMSTRUCT*)lp;
      int id = (int)d->CtlID;
      bool down = d->itemState & ODS_SELECTED, off = d->itemState & ODS_DISABLED;
      HDC dc = d->hDC;
      RECT r = d->rcItem;
      COLORREF edge = off ? T().field : T().accent;
      auto frame = [&](COLORREF c, int px) {
        HBRUSH b = CreateSolidBrush(c);
        for (int i = 0; i < px; i++) {
          FrameRect(dc, &r, b);
          InflateRect(&r, -1, -1);
        }
        DeleteObject(b);
      };
      auto fill = [&](COLORREF c) {
        HBRUSH b = CreateSolidBrush(c);
        FillRect(dc, &r, b);
        DeleteObject(b);
      };
      SelectObject(dc, g_font);
      SetBkMode(dc, TRANSPARENT);
      if (id >= ID_SWATCH) {
        const Theme& t = kThemes[id - ID_SWATCH];
        fill(t.bg);
        frame(id - ID_SWATCH == g_theme ? T().text : t.text, id - ID_SWATCH == g_theme ? 3 : 1);
        InflateRect(&r, -2, -2);
        fill(t.accent);
      } else if (id == ID_FAN) {
        fill(T().bg);
        RECT box{r.left, r.top + 3, r.left + S(18), r.top + 3 + S(18)};
        r = box;
        frame(T().accent, 2);
        if (g_fan_on) fill(T().accent);
        RECT label = d->rcItem;
        label.left += S(28);
        SetTextColor(dc, T().text);
        DrawTextA(dc, "Also install Fan Control", -1, &label, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
      } else {
        fill(down ? T().accent : T().bg);
        frame(edge, 2);
        char label[64];
        GetWindowTextA(d->hwndItem, label, sizeof label);
        SetTextColor(dc, off ? T().field : down ? T().bg : T().accent);
        RECT tr = d->rcItem;
        DrawTextA(dc, label, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
      }
      return TRUE;
    }
    case WM_COMMAND:
      if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) >= ID_SWATCH) {
        apply_theme(LOWORD(wp) - ID_SWATCH);
        return 0;
      }
      if (HIWORD(wp) == BN_CLICKED && LOWORD(wp) == ID_FAN) {
        g_fan_on = !g_fan_on;
        InvalidateRect(g_fan, nullptr, TRUE);
        return 0;
      }
      if (HIWORD(wp) == BN_CLICKED && !g_busy) {
        bool fan = g_fan_on;
        switch (LOWORD(wp)) {
          case ID_INSTALL: start(install, fan); break;
          case ID_CHECK: start([](bool) { check(); }, false); break;
          case ID_REMOVE:
            if (MessageBoxA(w, "Remove Framey and Fan Control from the headset, with their saved settings?", "Framey App", MB_YESNO | MB_ICONWARNING) == IDYES)
              start([](bool) { remove_all_of_it(); }, false);
            break;
        }
      }
      return 0;
    case WM_LOG: {
      std::unique_ptr<std::string> s((std::string*)lp);
      std::string t;
      for (char c : *s) {
        if (c == '\n') t += '\r';
        t += c;
      }
      SendMessageA(g_log, EM_SETSEL, -1, -1);
      SendMessageA(g_log, EM_REPLACESEL, FALSE, (LPARAM)(t + "\r\n").c_str());
      return 0;
    }
    case WM_STATUS: {
      std::unique_ptr<std::string> s((std::string*)lp);
      SetWindowTextA(g_status, s->c_str());
      return 0;
    }
    case WM_DONE:
      for (HWND b : g_buttons) EnableWindow(b, TRUE);
      SetWindowTextA(g_pass, "");
      return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      SetTextColor((HDC)wp, (HWND)lp == g_title || (HWND)lp == g_status ? T().accent : T().text);
      SetBkColor((HDC)wp, T().bg);
      return (LRESULT)g_bg;
    case WM_CTLCOLOREDIT:
      SetTextColor((HDC)wp, T().text);
      SetBkColor((HDC)wp, T().field);
      return (LRESULT)g_field;
    case WM_ERASEBKGND: {
      RECT r;
      GetClientRect(w, &r);
      FillRect((HDC)wp, &r, g_bg);
      return 1;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcA(w, m, wp, lp);
}

int askpass_mode() {
  char pw[512] = {};
  GetEnvironmentVariableA("FRAMEY_PW", pw, sizeof pw);
  std::string prompt = GetCommandLineA();
  for (auto& c : prompt) c = (char)tolower((unsigned char)c);
  std::string reply = prompt.find("assword") != std::string::npos ? std::string(pw) + "\n" : "yes\n";
  DWORD n;
  WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), reply.data(), (DWORD)reply.size(), &n, nullptr);
  return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
  if (GetEnvironmentVariableA("FRAMEY_ASKPASS", nullptr, 0)) return askpass_mode();
  char appdata[MAX_PATH];
  GetEnvironmentVariableA("LOCALAPPDATA", appdata, sizeof appdata);
  g_dir = fs::path(appdata) / "Framey";
  g_dpi = (int)GetDpiForSystem();
  std::ifstream tf(g_dir / "theme.txt");
  int saved = -1;
  if (tf >> saved && saved >= 0 && saved < 4) g_theme = saved;
  apply_theme(g_theme);
  INITCOMMONCONTROLSEX cc{sizeof cc, ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&cc);
  WNDCLASSA wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.lpszClassName = "FrameyApp";
  wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
  RegisterClassA(&wc);
  RECT r{0, 0, S(600), S(664)};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  HWND w = CreateWindowA("FrameyApp", "Framey App", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
  ShowWindow(w, show);
  MSG msg;
  while (GetMessageA(&msg, nullptr, 0, 0))
    if (!IsDialogMessageA(w, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
  return 0;
}
