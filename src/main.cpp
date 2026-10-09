#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
using Sock = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
using Sock = int;
#endif

namespace fs = std::filesystem;

constexpr unsigned char kPayload[] = {
#include "payload.inc"
};
constexpr unsigned char kUi[] = {
#include "ui.inc"
};

constexpr const char* kOwner = "chaosfox26";
constexpr const char* kRepos[] = {"framey", "frame-fan"};
constexpr const char* kHome = "/home/steamos";
constexpr const char* kThemes[] = {"Magenta", "Blue", "Black", "White"};

using Args = std::vector<std::string>;

struct Run {
  std::string out;
  int code = 1;
};

std::mutex g_mu;
std::string g_log, g_status, g_pw, g_target, g_source;
bool g_fan = false;
std::atomic<bool> g_busy{false}, g_quit{false};
std::atomic<long long> g_seen{0};
fs::path g_dir, g_work;
std::string g_token;
int g_port = 0;

long long now_s() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void log(const std::string& s) {
  std::lock_guard l(g_mu);
  g_log += s + "\n";
}

void status(const std::string& s) {
  std::lock_guard l(g_mu);
  g_status = s;
}

std::string self() {
#if defined(_WIN32)
  char p[MAX_PATH];
  return std::string(p, GetModuleFileNameA(nullptr, p, MAX_PATH));
#elif defined(__APPLE__)
  char p[4096];
  uint32_t n = sizeof p;
  return _NSGetExecutablePath(p, &n) ? "" : fs::weakly_canonical(p).string();
#else
  return fs::read_symlink("/proc/self/exe").string();
#endif
}

void set_env(const char* k, const char* v) {
#ifdef _WIN32
  SetEnvironmentVariableA(k, v);
#else
  v ? setenv(k, v, 1) : unsetenv(k);
#endif
}

#ifdef _WIN32
std::string quote(const std::string& a) {
  if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
  std::string o = "\"";
  for (size_t i = 0; i < a.size(); i++) {
    size_t bs = 0;
    while (i < a.size() && a[i] == '\\') bs++, i++;
    if (i == a.size()) {
      o.append(bs * 2, '\\');
      break;
    }
    o.append(a[i] == '"' ? bs * 2 + 1 : bs, '\\');
    o += a[i];
  }
  return o + "\"";
}

Run run_raw(const Args& a, const std::string& in) {
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE outR, outW, inR, inW;
  CreatePipe(&outR, &outW, &sa, 0);
  CreatePipe(&inR, &inW, &sa, 0);
  SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOA si{sizeof si};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = inR;
  si.hStdOutput = si.hStdError = outW;
  PROCESS_INFORMATION pi{};
  std::string line;
  for (auto& s : a) line += (line.empty() ? "" : " ") + quote(s);
  Run r;
  if (CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(inR);
    CloseHandle(outW);
    DWORD n;
    if (!in.empty()) WriteFile(inW, in.data(), (DWORD)in.size(), &n, nullptr);
    CloseHandle(inW);
    char buf[4096];
    while (ReadFile(outR, buf, sizeof buf, &n, nullptr) && n) r.out.append(buf, n);
    DWORD code = 1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    r.code = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  } else {
    r.out = "could not start " + a[0];
    for (HANDLE h : {inR, inW, outW}) CloseHandle(h);
  }
  CloseHandle(outR);
  return r;
}
#else
Run run_raw(const Args& a, const std::string& in) {
  int ip[2], op[2];
  if (pipe(ip) || pipe(op)) return {"pipe failed", 1};
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, ip[0], 0);
  posix_spawn_file_actions_adddup2(&fa, op[1], 1);
  posix_spawn_file_actions_adddup2(&fa, op[1], 2);
  for (int fd : {ip[0], ip[1], op[0], op[1]}) posix_spawn_file_actions_addclose(&fa, fd);
  std::vector<char*> av;
  for (auto& s : a) av.push_back(const_cast<char*>(s.c_str()));
  av.push_back(nullptr);
  pid_t pid;
  Run r;
  if (posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), environ)) {
    r.out = "could not start " + a[0];
    for (int fd : {ip[0], ip[1], op[0], op[1]}) close(fd);
  } else {
    close(ip[0]);
    close(op[1]);
    for (size_t off = 0; off < in.size();) {
      ssize_t n = write(ip[1], in.data() + off, in.size() - off);
      if (n <= 0) break;
      off += (size_t)n;
    }
    close(ip[1]);
    char buf[4096];
    for (ssize_t n; (n = read(op[0], buf, sizeof buf)) > 0;) r.out.append(buf, (size_t)n);
    close(op[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    r.code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
  }
  posix_spawn_file_actions_destroy(&fa);
  return r;
}
#endif

Run run(const Args& a, const std::string& in = {}, bool askpass = false) {
  if (askpass) {
    set_env("FRAMEY_ASKPASS", "1");
    set_env("FRAMEY_PW", g_pw.c_str());
    set_env("SSH_ASKPASS", self().c_str());
    set_env("SSH_ASKPASS_REQUIRE", "force");
  }
  Run r = run_raw(a, in);
  if (askpass)
    for (auto k : {"FRAMEY_ASKPASS", "FRAMEY_PW", "SSH_ASKPASS", "SSH_ASKPASS_REQUIRE"}) set_env(k, nullptr);
  return r;
}

bool have(const std::string& tool) {
  const char* path = std::getenv("PATH");
  if (!path) return false;
#ifdef _WIN32
  const char sep = ';';
  const char* ext = ".exe";
#else
  const char sep = ':';
  const char* ext = "";
#endif
  std::istringstream in(path);
  std::error_code ec;
  for (std::string d; std::getline(in, d, sep);)
    if (!d.empty() && fs::exists(fs::path(d) / (tool + ext), ec)) return true;
  return false;
}

std::string trim(std::string s) {
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && isspace((unsigned char)s[i])) i++;
  return s.substr(i);
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

std::string path_opt(const char* name, const fs::path& p) {
  std::string s = p.string();
  return std::string(name) + "=" + (s.find(' ') == std::string::npos ? s : "\"" + s + "\"");
}

Args ssh_base() {
  return {"-i", (g_dir / "id_ed25519").string(), "-o", "IdentitiesOnly=yes", "-o", path_opt("UserKnownHostsFile", g_dir / "known_hosts"), "-o", "StrictHostKeyChecking=accept-new", "-o", "ConnectTimeout=12"};
}

Run ssh(const std::string& remote, const std::string& in = {}) {
  Args a{"ssh"};
  for (auto& x : ssh_base()) a.push_back(x);
  for (auto& x : Args{"-o", "BatchMode=yes", "steamos@" + g_target, remote}) a.push_back(x);
  return run(a, in);
}

bool need_tools() {
  for (const char* t : {"ssh", "scp", "ssh-keygen", "tar", "curl"})
    if (!have(t)) {
      log(std::format("Missing tool: {}. Install the OpenSSH client, curl and tar for your system.", t));
      return false;
    }
  return true;
}

bool authorize() {
  if (!fs::exists(g_dir / "id_ed25519")) {
    run({"ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "framey-app", "-f", (g_dir / "id_ed25519").string()});
    if (!fs::exists(g_dir / "id_ed25519")) return log("Could not create an SSH key."), false;
  }
  if (!ssh("true").code) return true;
  if (g_pw.empty()) return log("First time on this headset: enter the password you set in its Developer settings."), false;
  log("Adding this app's key to the headset so it can reconnect without the password...");
  std::ifstream f(g_dir / "id_ed25519.pub");
  std::string pub;
  std::getline(f, pub);
  pub = trim(pub);
  auto r = run({"ssh", "-o", "PubkeyAuthentication=no", "-o", "NumberOfPasswordPrompts=1", "-o", path_opt("UserKnownHostsFile", g_dir / "known_hosts"), "-o", "StrictHostKeyChecking=accept-new", "-o", "ConnectTimeout=12", "steamos@" + g_target, "sh -s"},
               std::format("umask 077\nmkdir -p ~/.ssh\ntouch ~/.ssh/authorized_keys\ngrep -qxF '{0}' ~/.ssh/authorized_keys || echo '{0}' >> ~/.ssh/authorized_keys\n", pub), true);
  if (r.code || ssh("true").code) {
    log("Could not log in. Check the address and the password you set in Developer settings.\n" + tail(r.out));
    return false;
  }
  return true;
}

std::string latest_sha(const std::string& repo) {
  auto r = run({"curl", "-fsSL", "--max-time", "20", "-H", "Accept: application/vnd.github.sha", std::format("https://api.github.com/repos/{}/{}/commits/main", kOwner, repo)});
  auto s = trim(r.out);
  return !r.code && s.size() == 40 ? s : "";
}

bool extract(const fs::path& archive, const fs::path& to, bool strip = false) {
  fs::create_directories(to);
  Args a{"tar", "-xf", archive.string(), "-C", to.string()};
  if (strip) a.insert(a.end(), {"--strip-components", "1"});
  if (!run(a).code) return true;
  if (strip) return false;
  if (have("bsdtar") && !run({"bsdtar", "-xf", archive.string(), "-C", to.string()}).code) return true;
  return have("unzip") && !run({"unzip", "-qo", archive.string(), "-d", to.string()}).code;
}

bool unpack_bundle(const fs::path& to) {
  fs::create_directories(to);
  std::ofstream(to / "payload.tgz", std::ios::binary).write((const char*)kPayload, sizeof kPayload);
  return extract(to / "payload.tgz", to);
}

std::string stage(const std::string& repo, const fs::path& root) {
  fs::path dest = root / repo;
  fs::remove_all(dest);
  fs::create_directories(dest);
  auto tgz = (root / (repo + ".tar.gz")).string();
  std::string sha = latest_sha(repo);
  if (!sha.empty() && !run({"curl", "-fsSL", "--max-time", "60", "-o", tgz, std::format("https://github.com/{}/{}/archive/refs/heads/main.tar.gz", kOwner, repo)}).code &&
      extract(tgz, dest, true)) {
    std::ofstream(dest / ".version") << sha;
    log(std::format("{}: downloaded from GitHub ({})", repo, sha.substr(0, 7)));
    return sha;
  }
  fs::remove_all(dest);
  fs::path bundle = g_work / "bundle";
  fs::remove_all(bundle);
  if (!unpack_bundle(bundle) || !fs::exists(bundle / repo)) return log(std::format("{}: GitHub is unreachable and the bundled copy could not be opened.", repo)), "";
  fs::copy(bundle / repo, dest, fs::copy_options::recursive);
  std::ifstream v(dest / ".version");
  std::string s;
  std::getline(v, s);
  log(std::format("{}: GitHub unreachable, using the bundled copy ({})", repo, s.substr(0, 7)));
  return s.empty() ? "bundled" : s;
}

bool upload(const fs::path& local, const std::string& remote_dir) {
  Args a{"scp", "-r", "-q", "-o", "BatchMode=yes"};
  for (auto& x : ssh_base()) a.push_back(x);
  a.push_back(local.string());
  a.push_back(std::format("steamos@{}:{}/", g_target, remote_dir));
  auto r = run(a);
  if (r.code) log("Upload failed.\n" + tail(r.out));
  return !r.code;
}

std::string json_str(const std::string& j, const std::string& key) {
  auto k = j.find("\"" + key + "\"");
  if (k == std::string::npos) return {};
  auto q1 = j.find('"', j.find(':', k));
  auto q2 = q1 == std::string::npos ? q1 : j.find('"', q1 + 1);
  return q2 == std::string::npos ? "" : j.substr(q1 + 1, q2 - q1 - 1);
}

std::string slug(const std::string& s) {
  std::string o;
  for (char c : s) {
    c = (char)tolower((unsigned char)c);
    if (isalnum((unsigned char)c) || c == '_') o += c;
    else if (!o.empty() && o.back() != '-') o += '-';
  }
  while (!o.empty() && o.back() == '-') o.pop_back();
  return o.substr(0, 32);
}

bool valid_id(const std::string& s) {
  if (s.empty() || s.size() > 32) return false;
  for (char c : s)
    if (!islower((unsigned char)c) && !isdigit((unsigned char)c) && c != '-' && c != '_') return false;
  return true;
}

bool safe_url(const std::string& u) {
  for (char c : u)
    if (c <= ' ' || c == '\'' || c == '"' || c == '`' || c == '\\' || c == '<' || c == '>' || c == '|') return false;
  return true;
}

bool install_plugin(std::string src, bool restart) {
  if (src.size() > 1 && src.front() == '"' && src.back() == '"') src = src.substr(1, src.size() - 2);
  fs::path work = g_work / ("plugin-" + std::to_string(std::hash<std::string>{}(src) % 100000));
  fs::remove_all(work);
  fs::create_directories(work / "x");
  fs::path archive = work / "plugin.pkg";
  std::string hint, url;
  std::error_code ec;
  if (fs::is_regular_file(src, ec)) {
    if (fs::file_size(src) > 20'000'000) return log("That file is over 20 MB."), false;
    fs::copy_file(src, archive);
    hint = fs::path(src).stem().string();
  } else {
    url = src;
    if (url.starts_with("https://github.com/")) {
      std::string path = url.substr(19);
      path = path.substr(0, path.find_first_of("?#"));
      std::vector<std::string> p;
      std::istringstream in(path);
      for (std::string part; std::getline(in, part, '/');)
        if (!part.empty()) p.push_back(part);
      if (p.size() < 2) return log("That GitHub link needs an owner and a repo, like github.com/owner/repo."), false;
      hint = p[1].ends_with(".git") ? p[1].substr(0, p[1].size() - 4) : p[1];
      bool direct = path.find("/archive/") != std::string::npos || path.find("/releases/download/") != std::string::npos || path.ends_with(".zip") || path.ends_with(".tar.gz");
      if (!direct) url = std::format("https://github.com/{}/{}/archive/{}.tar.gz", p[0], hint, p.size() >= 4 && p[2] == "tree" ? "refs/heads/" + p[3] : "HEAD");
    } else if (url.starts_with("https://") && (url.ends_with(".zip") || url.ends_with(".tar.gz"))) {
      hint = fs::path(url.substr(url.find_last_of('/') + 1)).stem().string();
    } else {
      return log("Use a .zip file, a github.com link, or an https link to a .zip."), false;
    }
    if (!safe_url(url)) return log("That link has characters that are not allowed."), false;
    log("Downloading the plugin...");
    auto r = run({"curl", "-fsSL", "--max-time", "90", "--max-filesize", "20000000", "-o", archive.string(), url});
    if (r.code) return log("Download failed. Check the link.\n" + tail(r.out)), false;
  }
  if (!extract(archive, work / "x")) return log("Could not open that package. It must be a .zip or .tar.gz."), false;
  fs::path root = work / "x";
  if (!fs::exists(root / "plugin.json")) {
    std::vector<fs::path> dirs;
    for (auto& e : fs::directory_iterator(root))
      if (e.is_directory()) dirs.push_back(e.path());
    if (dirs.size() != 1 || !fs::exists(dirs[0] / "plugin.json")) return log("No plugin.json at the top of that package. A Framey plugin has plugin.json plus main.js and/or backend.py."), false;
    root = dirs[0];
  }
  if (!fs::exists(root / "main.js") && !fs::exists(root / "backend.py")) return log("That plugin has neither main.js nor backend.py."), false;
  std::ifstream mf(root / "plugin.json");
  std::string manifest((std::istreambuf_iterator<char>(mf)), {});
  std::string id = json_str(manifest, "id"), name = json_str(manifest, "name");
  if (!valid_id(id)) id = slug(hint);
  if (!valid_id(id)) return log("Could not work out a plugin id. Add an \"id\" to its plugin.json (lowercase letters, digits, - and _)."), false;
  fs::path stage_dir = work / "stage" / id;
  fs::create_directories(stage_dir.parent_path());
  fs::copy(root, stage_dir, fs::copy_options::recursive);
  fs::remove_all(stage_dir / ".git");
  if (!url.empty()) std::ofstream(stage_dir / ".source") << url;
  log(std::format("Installing plugin {} ({}) on the headset...", name.empty() ? id : name, id));
  auto r = ssh("sh -s", std::format("[ -L {0}/framey/plugins/{1} ] && exit 3\nmkdir -p {0}/framey/plugins\nrm -rf {0}/framey/plugins/{1}\n"
                                    "python3 -c \"import json,os;p=os.path.expanduser('~/.config/framey/settings.json');s=json.load(open(p));s['disabled']=[x for x in s.get('disabled',[]) if x!='{1}'];json.dump(s,open(p,'w'))\" 2>/dev/null || true\n",
                                    kHome, id));
  if (r.code == 3) return log("A linked plugin with that id is already there (for example Fan Control). Remove it first."), false;
  if (r.code) return log("Could not prepare the plugins folder.\n" + tail(r.out)), false;
  if (!upload(stage_dir, std::format("{}/framey/plugins", kHome))) return false;
  if (restart) {
    r = ssh(std::format("systemctl --user restart framey.service && sleep 2 && systemctl --user is-active framey.service && test -f {}/framey/plugins/{}/plugin.json && echo plugin-in-place", kHome, id));
    if (r.out.find("plugin-in-place") == std::string::npos) return log("The plugin was copied but Framey did not restart cleanly.\n" + tail(r.out)), false;
  }
  log(std::format("Plugin {} is installed.", id));
  return true;
}

const char* kService =
    "[Unit]\nDescription=Framey\n\n[Service]\nEnvironment=PYTHONDONTWRITEBYTECODE=1\n"
    "ExecStart=/usr/bin/python3 /home/steamos/framey/framey.py\nRestart=always\nRestartSec=3\nNice=19\n"
    "CPUSchedulingPolicy=idle\nCPUWeight=1\nMemoryHigh=40M\nMemoryMax=60M\n\n[Install]\nWantedBy=default.target\n";

void do_install() {
  log("[1/6] Connecting to the headset...");
  if (!authorize()) return;
  fs::path root = g_work / "stage";
  fs::create_directories(root);
  std::vector<std::string> repos = {"framey"};
  if (g_fan) repos.push_back("frame-fan");
  log("[2/6] Getting the latest versions...");
  for (auto& r : repos)
    if (stage(r, root).empty()) return;
  log("[3/6] Copying files to the headset...");
  for (auto& r : repos)
    if (!upload(root / r, kHome)) return;
  log("[4/6] Setting up Framey...");
  std::string script = "set -e\ncd /home/steamos\nmkdir -p framey/plugins .config/systemd/user\ncat > .config/systemd/user/framey.service <<'EOT'\n";
  script += kService;
  script += "EOT\n";
  if (g_fan) script += "ln -sfn /home/steamos/frame-fan framey/plugins/fan\n";
  script += "systemctl --user daemon-reload\nsystemctl --user enable framey.service\nsystemctl --user restart framey.service\n";
  auto r = ssh("sh -s", script);
  if (r.code) return log("Framey setup failed.\n" + tail(r.out));
  if (g_fan) {
    if (g_pw.empty()) return log("Fan Control needs the headset password for its system step. Enter it and run Install again.");
    log("[5/6] Installing Fan Control (about 20 seconds)...");
    r = ssh(std::format("sudo -S -p '' bash {}/frame-fan/install-root.sh", kHome), g_pw + "\n");
    if (r.code) return log("Fan Control install failed. Is the password right?\n" + tail(r.out));
    log(tail(r.out, 3));
  }
  log("[6/6] Updating plugins installed from links...");
  r = ssh(std::format("for d in {0}/framey/plugins/*/; do d=${{d%/}}; [ -L \"$d\" ] && continue; [ -f \"$d/.source\" ] && echo \"$(cat \"$d/.source\")\"; done", kHome));
  std::istringstream list(r.out);
  int updated = 0;
  for (std::string u; std::getline(list, u);)
    if (!trim(u).empty() && install_plugin(trim(u), false)) updated++;
  if (updated) ssh("systemctl --user restart framey.service");
  r = ssh("systemctl --user is-active framey.service; ss -ltn 2>/dev/null | grep -q ':8080 ' && echo debug-port-open || echo debug-port-closed");
  if (r.out.find("debug-port-closed") != std::string::npos) log("Warning: Steam's debug port is not open. Make sure Steam is running on the headset.");
  if (r.out.find("active") != 0 && r.out.find("\nactive") == std::string::npos) return log("Framey is not running.\n" + tail(r.out));
  log("\nDone. Put the headset on and tap the Framey icon in the bottom bar.");
  status("Installed.");
}

void do_check() {
  if (!authorize()) return;
  auto r = ssh("for r in framey frame-fan; do echo $r=$(cat /home/steamos/$r/.version 2>/dev/null); done");
  std::string all;
  for (auto repo : kRepos) {
    auto at = r.out.find(std::string(repo) + "=");
    std::string have_v;
    if (at != std::string::npos) {
      have_v = r.out.substr(at + strlen(repo) + 1, 40);
      if (have_v.find_first_of("\r\n") != std::string::npos) have_v.clear();
    }
    std::string now = latest_sha(repo);
    std::string line = std::format("{}: {}, latest {}", repo, have_v.empty() ? "not installed" : have_v.substr(0, 7), now.empty() ? "unknown" : now.substr(0, 7));
    if (!have_v.empty() && !now.empty()) line += have_v == now ? " (up to date)" : " (update available)";
    log(line);
    all += line + "   ";
  }
  status(all);
}

void do_remove() {
  if (!authorize()) return;
  Run r;
  if (!ssh(std::format("test -f {}/frame-fan/uninstall-root.sh", kHome)).code) {
    if (g_pw.empty()) return log("Fan Control is installed, and removing it needs the headset password. Enter it and try again.");
    log("Removing Fan Control (restores stock fan control)...");
    r = ssh(std::format("sudo -S -p '' bash {}/frame-fan/uninstall-root.sh", kHome), g_pw + "\n");
    log(tail(r.out, 2));
  }
  log("Removing Framey and its plugins...");
  r = ssh("sh -s", "systemctl --user disable --now framey.service\nrm -f ~/.config/systemd/user/framey.service\nsystemctl --user daemon-reload\nrm -rf ~/framey ~/frame-fan ~/.config/framey ~/.config/frame-fan\n");
  if (r.code) return log("Removal had errors.\n" + tail(r.out));
  log("Removing this app's key from the headset...");
  r = ssh("sed -i '/ framey-app$/d' ~/.ssh/authorized_keys");
  if (r.code) return log("Could not remove the app's key.\n" + tail(r.out));
  std::error_code ec;
  fs::remove_all(g_dir, ec);
  log("Done. The headset and this computer are clean: the app's key, settings and temporary files are gone too.");
  status("Removed.");
}

bool valid_host(const std::string& h) {
  if (h.empty() || h.size() > 100) return false;
  for (char c : h)
    if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != ':') return false;
  return true;
}

std::string upload_path() { return (fs::temp_directory_path() / std::format("FrameyUpload-{}.pkg", std::hash<std::string>{}(g_token) % 1000000)).string(); }

void start_job(const std::string& action, const std::string& host, const std::string& pw, bool fan, const std::string& source) {
  if (g_busy.exchange(true)) return;
  if (!valid_host(trim(host))) {
    log("Enter the headset address, for example frame or its IP address.");
    g_busy = false;
    return;
  }
  g_target = trim(host);
  g_pw = pw;
  g_fan = fan;
  g_source = trim(source);
  g_work = fs::temp_directory_path() / std::format("FrameyApp-{}", std::hash<std::string>{}(g_token) % 1000000);
  fs::create_directories(g_dir);
  std::ofstream(g_dir / "host.txt") << g_target;
  status("Working...");
  std::thread([action] {
    try {
      if (!need_tools()) {
      } else if (action == "install") {
        do_install();
      } else if (action == "check") {
        do_check();
      } else if (action == "remove") {
        do_remove();
      } else if (action == "plugin") {
        if (g_source.empty()) log("Choose a .zip file or paste a GitHub link first.");
        else if (authorize()) {
          if (ssh(std::format("test -d {}/framey", kHome)).code) log("Framey is not installed on the headset yet. Click Install / Update first.");
          else if (install_plugin(g_source, true)) status("Plugin installed.");
        }
      }
    } catch (const std::exception& e) {
      log(std::string("Error: ") + e.what());
    }
    g_pw.assign(g_pw.size(), '\0');
    g_pw.clear();
    std::error_code ec;
    fs::remove_all(g_work, ec);
    fs::remove(upload_path(), ec);
    {
      std::lock_guard l(g_mu);
      if (g_status == "Working...") g_status.clear();
    }
    g_busy = false;
  }).detach();
}

std::string url_decode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16), i += 2;
    else o += s[i];
  }
  return o;
}

std::map<std::string, std::string> form(const std::string& body) {
  std::map<std::string, std::string> m;
  std::istringstream in(body);
  for (std::string kv; std::getline(in, kv, '&');) {
    auto eq = kv.find('=');
    if (eq != std::string::npos) m[url_decode(kv.substr(0, eq))] = url_decode(kv.substr(eq + 1));
  }
  return m;
}

std::string jstr(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') o += std::string("\\") + (char)c;
    else if (c == '\n') o += "\\n";
    else if (c < 32) o += ' ';
    else o += (char)c;
  }
  return o + "\"";
}

struct Req {
  std::string method, path, query, body;
  std::map<std::string, std::string> h;
};

bool read_req(Sock c, Req& r) {
  std::string buf;
  char tmp[8192];
  size_t end;
  while ((end = buf.find("\r\n\r\n")) == std::string::npos) {
    int n = (int)recv(c, tmp, sizeof tmp, 0);
    if (n <= 0 || buf.size() > 65536) return false;
    buf.append(tmp, (size_t)n);
  }
  std::istringstream in(buf.substr(0, end));
  std::string line, target, ver;
  std::getline(in, line);
  std::istringstream rl(line);
  rl >> r.method >> target >> ver;
  auto q = target.find('?');
  r.path = target.substr(0, q);
  if (q != std::string::npos) r.query = target.substr(q + 1);
  while (std::getline(in, line)) {
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string k = line.substr(0, colon);
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char ch) { return (char)tolower(ch); });
    r.h[k] = trim(line.substr(colon + 1));
  }
  size_t len = r.h.count("content-length") ? std::strtoull(r.h["content-length"].c_str(), nullptr, 10) : 0;
  if (len > 26'000'000) return false;
  r.body = buf.substr(end + 4);
  while (r.body.size() < len) {
    int n = (int)recv(c, tmp, sizeof tmp, 0);
    if (n <= 0) return false;
    r.body.append(tmp, (size_t)n);
  }
  return true;
}

void reply(Sock c, int code, const std::string& type, const std::string& body) {
  std::string out = std::format(
      "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n"
      "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'\r\nConnection: close\r\n\r\n",
      code, code == 200 ? "OK" : "Forbidden", type, body.size()) + body;
  for (size_t off = 0; off < out.size();) {
    int n = (int)send(c, out.data() + off, (int)(out.size() - off), 0);
    if (n <= 0) break;
    off += (size_t)n;
  }
}

std::string saved(const char* file) {
  std::ifstream f(g_dir / file);
  std::string s;
  std::getline(f, s);
  return trim(s);
}

void handle(Sock c) {
  Req r;
  if (!read_req(c, r)) return;
  std::string k;
  if (r.h.count("x-token")) k = r.h["x-token"];
  else if (r.query.starts_with("k=")) k = r.query.substr(2, r.query.find('&') - 2);
  std::string host = r.h.count("host") ? r.h["host"] : "";
  bool local = host == std::format("127.0.0.1:{}", g_port) || host == std::format("localhost:{}", g_port);
  if (!local || k != g_token) return reply(c, 403, "text/plain", "forbidden");
  if (r.method == "GET" && r.path == "/") return reply(c, 200, "text/html; charset=utf-8", std::string((const char*)kUi, sizeof kUi));
  if (r.method == "GET" && r.path == "/api/state") {
    g_seen = now_s();
    size_t since = 0;
    auto at = r.query.find("since=");
    if (at != std::string::npos) since = std::strtoull(r.query.c_str() + at + 6, nullptr, 10);
    std::string theme = saved("theme.txt");
    if (std::find(std::begin(kThemes), std::end(kThemes), theme) == std::end(kThemes)) theme = "Magenta";
    std::lock_guard l(g_mu);
    since = std::min(since, g_log.size());
    return reply(c, 200, "application/json", std::format("{{\"busy\":{},\"status\":{},\"log\":{},\"next\":{},\"theme\":{},\"host\":{}}}", g_busy ? "true" : "false", jstr(g_status), jstr(g_log.substr(since)), g_log.size(), jstr(theme), jstr(saved("host.txt"))));
  }
  if (r.method == "POST" && r.path == "/api/run") {
    auto f = form(r.body);
    start_job(f["action"], f["host"], f["pw"], f["fan"] == "1", f["source"]);
    return reply(c, 200, "application/json", "{}");
  }
  if (r.method == "POST" && r.path == "/api/upload") {
    std::ofstream(upload_path(), std::ios::binary).write(r.body.data(), (std::streamsize)r.body.size());
    return reply(c, 200, "application/json", "{\"path\":" + jstr(upload_path()) + "}");
  }
  if (r.method == "POST" && r.path == "/api/theme") {
    if (std::find(std::begin(kThemes), std::end(kThemes), r.body) != std::end(kThemes)) {
      fs::create_directories(g_dir);
      std::ofstream(g_dir / "theme.txt") << r.body;
    }
    return reply(c, 200, "application/json", "{}");
  }
  if (r.method == "POST" && r.path == "/api/quit") {
    g_quit = true;
    return reply(c, 200, "application/json", "{}");
  }
  reply(c, 403, "text/plain", "forbidden");
}

void open_url(const std::string& url) {
#if defined(_WIN32)
  ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
  run({"open", url});
#else
  run({"xdg-open", url});
#endif
}

int main(int argc, char** argv) {
  if (std::getenv("FRAMEY_ASKPASS")) {
    std::string prompt = argc > 1 ? argv[1] : "";
    std::transform(prompt.begin(), prompt.end(), prompt.begin(), [](unsigned char ch) { return (char)tolower(ch); });
    const char* pw = std::getenv("FRAMEY_PW");
    std::string answer = prompt.find("assword") != std::string::npos ? std::string(pw ? pw : "") + "\n" : "yes\n";
    std::fwrite(answer.data(), 1, answer.size(), stdout);
    return 0;
  }
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#else
  signal(SIGPIPE, SIG_IGN);
#endif
  g_dir = fs::path(self()).parent_path() / "FrameyApp-data";
  std::random_device rd;
  for (int i = 0; i < 4; i++) g_token += std::format("{:08x}", rd());
  Sock srv = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  for (int p = 47321; p < 47341 && !g_port; p++) {
    addr.sin_port = htons((unsigned short)p);
    if (bind(srv, (sockaddr*)&addr, sizeof addr) == 0) g_port = p;
  }
  if (!g_port || listen(srv, 8)) {
    std::fputs("Could not open a local port.\n", stderr);
    return 1;
  }
  std::string url = std::format("http://127.0.0.1:{}/?k={}", g_port, g_token);
  std::printf("Framey App is running. If your browser did not open, go to:\n%s\nPress Ctrl+C or close this window to quit.\n", url.c_str());
  std::fflush(stdout);
  g_seen = now_s();
  if (argc < 2 || std::string(argv[1]) != "--no-browser") open_url(url);
  while (!g_quit) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(srv, &fds);
    timeval tv{1, 0};
    if (select((int)srv + 1, &fds, nullptr, nullptr, &tv) > 0) {
      Sock c = accept(srv, nullptr, nullptr);
      if (c != (Sock)-1) {
        handle(c);
#ifdef _WIN32
        closesocket(c);
#else
        close(c);
#endif
      }
    }
    if (!g_busy && now_s() - g_seen > 180) break;
  }
  return 0;
}
