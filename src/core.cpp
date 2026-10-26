#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;

constexpr unsigned char kPayload[] = {
#include "payload.inc"
};

constexpr const char* kOwner = "chaosfox26";
constexpr const char* kHome = "/home/steamos";
constexpr const char* kSignerFp = "SHA256:sumclwMiONyyDcNbUAtihk4QQHu8PtIkneKbnNkMt9A";
constexpr const char* kSigners = "framey-release namespaces=\"framey-fan-release\" ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAINFqtW/GmqLGf01EFtx2xiWCzndt63Qm2YUFipdWkdOt\n";
constexpr size_t kMaxFiles = 500, kMaxDepth = 8, kMaxOut = 1 << 22;
constexpr uintmax_t kMaxBytes = 20'000'000;

using Args = std::vector<std::string>;

struct Run {
  std::string out;
  int code = 1;
};

std::mutex g_mu;
std::string g_log, g_status, g_pw, g_target, g_source;
fs::path g_fan_tar;
std::string g_fan_sha;
bool g_fan = false;
std::atomic<bool> g_busy{false};
fs::path g_dir, g_work;
std::string g_token;

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

std::vector<std::string> env_list(const Args& extra) {
  std::vector<std::string> v;
#ifdef _WIN32
  if (char* e = GetEnvironmentStringsA()) {
    for (const char* p = e; *p; p += strlen(p) + 1) v.push_back(p);
    FreeEnvironmentStringsA(e);
  }
#else
  for (char** e = environ; *e; e++) v.push_back(*e);
#endif
  for (auto& x : extra) {
    std::string k = x.substr(0, x.find('=')) + "=";
    std::erase_if(v, [&](const std::string& s) { return s.starts_with(k); });
    v.push_back(x);
  }
  return v;
}

#ifdef _WIN32
std::string exe_path(const std::string& tool) {
  std::string name = tool + ".exe";
  const char* sr = std::getenv("SystemRoot");
  std::string root = sr ? sr : "C:\\Windows";
  for (const char* dir : {"\\System32\\", "\\System32\\OpenSSH\\"})
    if (fs::exists(root + dir + name)) return root + dir + name;
  char out[MAX_PATH];
  const char* path = std::getenv("PATH");
  if (path && SearchPathA(path, name.c_str(), nullptr, MAX_PATH, out, nullptr)) return out;
  return {};
}
#endif

uintmax_t du(const fs::path& p) {
  std::error_code ec, e2;
  uintmax_t n = 0;
  for (auto it = fs::recursive_directory_iterator(p, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    if (it->is_regular_file(e2)) {
      std::ifstream f(it->path(), std::ios::binary | std::ios::ate);
      if (f) n += (uintmax_t)std::max<std::streamoff>(f.tellg(), 0);
    }
  return n;
}

int verdict(int ms, int secs, const fs::path& quota) { return secs && ms >= secs * 1000 ? 124 : !quota.empty() && du(quota) > kMaxBytes ? 125 : 0; }

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

Run run_raw(const Args& a, const std::string& in, int secs, const fs::path& quota, const Args& extra) {
  std::string exe = exe_path(a[0]);
  if (exe.empty()) return {"could not start " + a[0], 1};
  std::string block;
  for (auto& s : env_list(extra)) block += s + '\0';
  block += '\0';
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
  line = quote(exe);
  for (size_t i = 1; i < a.size(); i++) line += " " + quote(a[i]);
  Run r;
  if (CreateProcessA(exe.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, block.data(), nullptr, &si, &pi)) {
    CloseHandle(inR);
    CloseHandle(outW);
    int fate = 0;
    std::thread dog([&] {
      const int tick = quota.empty() ? 250 : 20;
      for (int ms = 0; WaitForSingleObject(pi.hProcess, tick) == WAIT_TIMEOUT; ms += tick)
        if ((fate = verdict(ms, secs, quota))) {
          TerminateProcess(pi.hProcess, 1);
          return;
        }
    });
    DWORD n;
    if (!in.empty()) WriteFile(inW, in.data(), (DWORD)in.size(), &n, nullptr);
    CloseHandle(inW);
    char buf[4096];
    while (ReadFile(outR, buf, sizeof buf, &n, nullptr) && n)
      if (r.out.size() < kMaxOut) r.out.append(buf, n);
    dog.join();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.code = fate ? fate : (int)code;
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
Run run_raw(const Args& a, const std::string& in, int secs, const fs::path& quota, const Args& extra) {
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
  auto env = env_list(extra);
  std::vector<char*> ev;
  for (auto& s : env) ev.push_back(s.data());
  ev.push_back(nullptr);
  pid_t pid;
  Run r;
  if (posix_spawnp(&pid, av[0], &fa, nullptr, av.data(), ev.data())) {
    r.out = "could not start " + a[0];
    for (int fd : {ip[0], ip[1], op[0], op[1]}) close(fd);
  } else {
    close(ip[0]);
    close(op[1]);
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    int fate = 0;
    std::thread dog([&] {
      std::unique_lock l(m);
      const int tick = quota.empty() ? 250 : 20;
      for (int ms = 0; !cv.wait_for(l, std::chrono::milliseconds(tick), [&] { return done; }); ms += tick)
        if ((fate = verdict(ms, secs, quota))) {
          kill(pid, SIGKILL);
          return;
        }
    });
    for (size_t off = 0; off < in.size();) {
      ssize_t n = write(ip[1], in.data() + off, in.size() - off);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      off += (size_t)n;
    }
    close(ip[1]);
    char buf[4096];
    for (ssize_t n; (n = read(op[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR);)
      if (n > 0 && r.out.size() < kMaxOut) r.out.append(buf, (size_t)n);
    close(op[0]);
    int st = 0;
    pid_t w;
    while ((w = waitpid(pid, &st, 0)) < 0 && errno == EINTR) {
    }
    {
      std::lock_guard l(m);
      done = true;
    }
    cv.notify_all();
    dog.join();
    r.code = fate ? fate : w > 0 && WIFEXITED(st) ? WEXITSTATUS(st) : 1;
  }
  posix_spawn_file_actions_destroy(&fa);
  return r;
}
#endif

Run run(const Args& a, const std::string& in = {}, bool askpass = false, int secs = 120, const fs::path& quota = {}) {
  Args env;
  if (askpass) env = {"FRAMEY_ASKPASS=1", "FRAMEY_PW=" + g_pw, "SSH_ASKPASS=" + self(), "SSH_ASKPASS_REQUIRE=force"};
  Run r = run_raw(a, in, secs, quota, env);
  if (r.code == 124) log(std::format("{} did not finish within {} seconds and was stopped. If this was a headset step, check that it is awake and on the network.", a[0], secs));
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

fs::path key_file() {
  std::string h = g_target;
  std::replace(h.begin(), h.end(), '.', '_');
  std::replace(h.begin(), h.end(), ':', '~');
  return g_dir / ("key-" + h);
}

Args ssh_opts() {
  return {"-o", path_opt("UserKnownHostsFile", g_dir / "known_hosts"), "-o", "StrictHostKeyChecking=accept-new", "-o", "ConnectTimeout=12", "-o", "ServerAliveInterval=10", "-o", "ServerAliveCountMax=3"};
}

Args ssh_base() {
  Args a{"-i", key_file().string(), "-o", "IdentitiesOnly=yes"};
  for (auto& x : ssh_opts()) a.push_back(x);
  return a;
}

Run ssh(const std::string& remote, const std::string& in = {}) {
  Args a{"ssh"};
  for (auto& x : ssh_base()) a.push_back(x);
  for (auto& x : Args{"-o", "BatchMode=yes", "steamos@" + g_target, remote}) a.push_back(x);
  return run(a, in);
}

std::string unquote(const std::string& s) {
  return s.size() > 1 && s.front() == '"' && s.back() == '"' ? s.substr(1, s.size() - 2) : s;
}

bool is_zip(const std::string& src) {
  std::string s = unquote(src);
  s = s.substr(0, s.find_first_of("?#"));
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
  return s.ends_with(".zip");
}

bool need_tools(const std::string& action, const std::string& source) {
  for (const char* t : {"ssh", "scp", "ssh-keygen", "tar", "curl"})
    if (!have(t)) {
      log(std::format("Missing tool: {}. Install the OpenSSH client, curl and tar for your system.", t));
      return false;
    }
  if (action == "plugin" && is_zip(source) && !have("bsdtar") && !have("unzip") && run({"tar", "--version"}, {}, false, 10).out.find("bsdtar") == std::string::npos) return log("Opening .zip packages needs bsdtar or unzip on this computer. Install one of them, or use a .tar.gz package."), false;
  return true;
}

std::string rand_hex(size_t n) {
  std::random_device rd;
  std::string s;
  while (s.size() < n) s += std::format("{:08x}", rd());
  return s.substr(0, n);
}

std::string pub_line(const fs::path& key) {
  auto ok = [](const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return isalnum(c) || c == '+' || c == '/' || c == '=' || c == '-'; }); };
  for (int pass = 0; pass < 2; pass++) {
    std::ifstream f(key.string() + ".pub");
    std::istringstream y(pass ? run({"ssh-keygen", "-y", "-f", key.string()}, {}, false, 30).out : "");
    std::string t, b, c;
    if (pass) y >> t >> b;
    else f >> t >> b >> c;
    if (t == "ssh-ed25519" && ok(b) && (c.empty() || ok(c))) return t + " " + b + (c.empty() ? "" : " " + c);
  }
  return {};
}

bool authorize() {
  fs::path key = key_file(), old = g_dir / "id_ed25519";
  std::error_code ec;
  if (!fs::exists(key) && fs::exists(old)) {
    fs::rename(old, key, ec);
    fs::rename(old.string() + ".pub", key.string() + ".pub", ec);
  }
  if (!fs::exists(key)) {
    run({"ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "framey-app-" + rand_hex(12), "-f", key.string()});
    if (!fs::exists(key)) return log("Could not create an SSH key."), false;
  }
  auto t = ssh("true");
  if (!t.code) return true;
  if (t.out.find("Permission denied") == std::string::npos)
    return log("Could not reach the headset. Check the address, and that it is awake and on this network.\n" + tail(t.out) + (t.out.find("HOST IDENTIFICATION HAS CHANGED") == std::string::npos ? "" : "Its host key changed, for example after reinstalling SteamOS. If you trust it, delete the known_hosts file in FrameyApp-data and try again.\n")), false;
  if (g_pw.empty()) return log("The headset does not know this app yet. Enter the password you set in its Developer settings."), false;
  std::string pub = pub_line(key);
  if (pub.empty()) return log("Could not read this app's SSH key."), false;
  log("Adding this app's key to the headset so it can reconnect without the password...");
  Args pa{"ssh", "-o", "PubkeyAuthentication=no", "-o", "NumberOfPasswordPrompts=1"};
  for (auto& x : ssh_opts()) pa.push_back(x);
  pa.insert(pa.end(), {"steamos@" + g_target, "sh -s"});
  auto r = run(pa, std::format("umask 077\nmkdir -p ~/.ssh\nf=~/.ssh/authorized_keys\ntouch $f\ngrep -qxF '{0}' $f || {{ [ -z \"$(tail -c1 $f)\" ] || echo >> $f; echo '{0}' >> $f; }}\n", pub), true);
  if (r.code || ssh("true").code) {
    log("Could not log in. Check the address and the password you set in Developer settings.\n" + tail(r.out));
    return false;
  }
  return true;
}

Run curl(Args extra, const std::string& url) {
  Args a{"curl", "-fsSL", "--globoff", "--proto", "=https", "--proto-redir", "=https"};
  a.insert(a.end(), extra.begin(), extra.end());
  a.push_back(url);
  return run(a, {}, false, 150);
}

Run fetch(const std::string& url, const fs::path& to, int secs) {
  auto r = curl({"--max-time", std::to_string(secs), "--max-filesize", std::to_string(kMaxBytes), "-o", to.string()}, url);
  std::error_code ec;
  if (!r.code && fs::file_size(to, ec) > kMaxBytes) r = {"The download is over 20 MB.", 1};
  return r;
}

std::string latest_sha(const std::string& repo) {
  auto r = curl({"--max-time", "20", "-H", "Accept: application/vnd.github.sha"}, std::format("https://api.github.com/repos/{}/{}/commits/main", kOwner, repo));
  auto s = trim(r.out);
  return !r.code && s.size() == 40 && s.find_first_not_of("0123456789abcdef") == std::string::npos ? s : "";
}

bool plain_tree(const fs::path& p) {
  std::error_code ec;
  size_t files = 0;
  uintmax_t bytes = 0;
  for (auto it = fs::recursive_directory_iterator(p, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    auto st = it->symlink_status(ec);
    if (!fs::is_regular_file(st) && !fs::is_directory(st)) return false;
    if (fs::is_regular_file(st)) bytes += it->file_size(ec);
    if (++files > kMaxFiles || bytes > kMaxBytes) return false;
  }
  return !ec;
}

std::vector<std::string> lines(const std::string& s, bool zipinfo) {
  std::vector<std::string> v;
  std::istringstream in(s);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (zipinfo && !(l.size() > 10 && std::strchr("-dlcbps?", l[0]) && (l[1] == 'r' || l[1] == '-'))) continue;
    if (!l.empty()) v.push_back(l);
  }
  return v;
}

long long declared(const std::string& line, const std::string& name, bool zipinfo) {
  if (!zipinfo && !line.ends_with(name)) return -1;
  std::istringstream in(zipinfo ? line : line.substr(0, line.size() - name.size()));
  std::vector<std::string> t;
  for (std::string x; in >> x;) t.push_back(x);
  auto num = [](const std::string& s) { return !s.empty() && s.size() < 15 && s.find_first_not_of("0123456789") == std::string::npos; };
  if (zipinfo) return t.size() > 3 && num(t[3]) ? std::atoll(t[3].c_str()) : -1;
  for (size_t i = t.size(); i-- > 1;)
    if (num(t[i - 1]) && (isalpha((unsigned char)t[i][0]) || (t[i].size() == 10 && t[i][4] == '-'))) return std::atoll(t[i - 1].c_str());
  return -1;
}

std::string vet(const std::string& names, const std::string& verbose, bool zipinfo, bool strip) {
  auto n = lines(names, false), v = lines(verbose, zipinfo);
  if (n.empty() || n.size() != v.size()) return "Could not read that package's file list.";
  if (n.size() > kMaxFiles) return "That package has too many files.";
  uintmax_t total = 0;
  std::set<std::string> seen;
  for (size_t i = 0; i < n.size(); i++) {
    if (v[i][0] != '-' && v[i][0] != 'd' && !(zipinfo && v[i][0] == '?')) return "That package contains links or special files, which are not allowed.";
    long long size = declared(v[i], n[i], zipinfo);
    if (size < 0) return "Could not read that package's file list.";
    if ((total += (uintmax_t)size) > kMaxBytes) return "That package is too large (over 20 MB unpacked).";
    const std::string& p = n[i];
    if (p.size() > 512 || p[0] == '/' || p.find_first_of("\\:") != std::string::npos || std::any_of(p.begin(), p.end(), [](unsigned char c) { return c < ' '; })) return "That package has file names that are not allowed.";
    std::vector<std::string> c;
    std::istringstream in(p);
    for (std::string part; std::getline(in, part, '/');)
      if (!part.empty() && part != ".") c.push_back(part);
    if (std::find(c.begin(), c.end(), "..") != c.end()) return "That package has file names that are not allowed.";
    if (strip && !c.empty()) c.erase(c.begin());
    if (c.empty()) continue;
    if (c.size() > kMaxDepth) return "That package is nested too deeply.";
    std::string key;
    for (auto& part : c) key += "/" + part;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return (char)tolower(ch); });
    if (!seen.insert(key).second) return "That package lists the same file twice.";
  }
  return {};
}

bool unpack(const fs::path& archive, const fs::path& to, bool strip, std::string& why) {
  std::string a = archive.string(), d = to.string();
  std::error_code ec;
  for (const char* exe : {"tar", "bsdtar", "unzip"}) {
    bool zi = !std::strcmp(exe, "unzip");
    if (!have(exe) || (zi && strip)) continue;
    auto names = run(zi ? Args{exe, "-Z1", a} : Args{exe, "-tf", a}, {}, false, 60);
    auto verbose = run(zi ? Args{exe, "-Z", a} : Args{exe, "-tvf", a}, {}, false, 60);
    if (names.code || verbose.code) continue;
    why = vet(names.out, verbose.out, zi, strip);
    if (!why.empty()) break;
    fs::remove_all(to, ec);
    fs::create_directories(to);
    Args x = zi ? Args{exe, "-qo", a, "-d", d} : Args{exe, "-xf", a, "-C", d};
    if (strip) x.insert(x.end(), {"--strip-components", "1"});
    int code = run(x, {}, false, 60, to).code;
    if (code == 125) {
      why = "That package is too large (over 20 MB unpacked).";
      break;
    }
    if (code) continue;
    if (plain_tree(to)) return true;
    why = "That package unpacked to links, special files or too much data, which is not allowed.";
    break;
  }
  fs::remove_all(to, ec);
  return false;
}

bool extract(const fs::path& archive, const fs::path& to, bool strip = false) {
  std::string why;
  if (unpack(archive, to, strip, why)) return true;
  if (!why.empty()) log(why);
  return false;
}

bool unpack_bundle(const fs::path& to) {
  fs::path tgz = to.parent_path() / "payload.tgz";
  std::ofstream(tgz, std::ios::binary).write((const char*)kPayload, sizeof kPayload);
  return extract(tgz, to);
}

bool upload(const fs::path& local, const std::string& remote_dir) {
  Args a{"scp", "-r", "-q", "-o", "BatchMode=yes"};
  for (auto& x : ssh_base()) a.push_back(x);
  a.push_back(local.string());
  a.push_back(std::format("steamos@{}:{}/", g_target.find(':') == std::string::npos ? g_target : "[" + g_target + "]", remote_dir));
  auto r = run(a, {}, false, 600);
  if (r.code) log("Upload failed.\n" + tail(r.out));
  return !r.code;
}

struct Json {
  const std::string& j;
  size_t i = 0;
  bool pk(char c) { return i < j.size() && j[i] == c; }
  bool at(char c) {
    while (pk(' ') || pk('\t') || pk('\n') || pk('\r')) i++;
    return pk(c);
  }
  bool eat(char c) { return at(c) && ++i; }
  bool digits() {
    size_t b = i;
    while (i < j.size() && isdigit((unsigned char)j[i])) i++;
    return i > b;
  }
  bool num() {
    if (pk('-')) i++;
    if (pk('0')) i++;
    else if (!digits()) return false;
    if (pk('.') && (i++, !digits())) return false;
    if (pk('e') || pk('E')) {
      i++;
      if (pk('+') || pk('-')) i++;
      if (!digits()) return false;
    }
    return true;
  }
  bool str(std::string* o) {
    if (!eat('"')) return false;
    size_t s = i;
    for (; i < j.size(); i++) {
      unsigned char c = j[i];
      if (c < ' ') return false;
      if (c == '"') {
        if (o) o->assign(j, s, i - s);
        return i++, true;
      }
      if (c != '\\') continue;
      if (++i == j.size() || !j[i] || !strchr("\"\\/bfnrtu", j[i])) return false;
      if (j[i] == 'u')
        for (int k = 0; k < 4; k++)
          if (++i == j.size() || !isxdigit((unsigned char)j[i])) return false;
    }
    return false;
  }
  bool obj(int d, std::map<std::string, std::string>* m) {
    if (!eat('{')) return false;
    if (eat('}')) return true;
    do {
      std::string k, v;
      if (!at('"') || !str(&k) || !eat(':')) return false;
      bool s = at('"');
      if (!val(d + 1, s ? &v : nullptr) || (m && !m->emplace(k, s ? v : "\\").second)) return false;
    } while (eat(','));
    return eat('}');
  }
  bool val(int d, std::string* sv = nullptr) {
    if (d > 32) return false;
    if (at('"')) return str(sv);
    if (at('{')) return obj(d, nullptr);
    if (eat('[')) {
      if (eat(']')) return true;
      do {
        if (!val(d + 1)) return false;
      } while (eat(','));
      return eat(']');
    }
    for (const char* w : {"true", "false", "null"})
      if (!j.compare(i, strlen(w), w)) return i += strlen(w), true;
    return num();
  }
};

std::map<std::string, std::string> top_json(const std::string& j) {
  Json p{j};
  std::map<std::string, std::string> m;
  if (!p.obj(0, &m)) return {};
  p.at(0);
  return p.i == j.size() ? m : decltype(m){};
}

bool valid_id(const std::string& s) {
  if (s.empty() || s.size() > 32 || s[0] == '-' || s == "_core") return false;
  for (char c : s)
    if (!islower((unsigned char)c) && !isdigit((unsigned char)c) && c != '-' && c != '_') return false;
  return true;
}

bool safe_url(const std::string& u) {
  for (char c : u)
    if (c <= ' ' || c == '\'' || c == '"' || c == '`' || c == '\\' || c == '<' || c == '>' || c == '|') return false;
  return true;
}

bool github_source(const std::string& u) { return u.starts_with("https://github.com/") && u.size() <= 512 && safe_url(u); }

bool allowed_fan_url(const std::string& u) {
  auto on = [&](const char* host) { return u.starts_with(std::string("https://") + host + "/"); };
  return u.size() <= 512 && safe_url(u) &&
         ((on("github.com") && u.starts_with(std::format("https://github.com/{}/frame-fan/", kOwner))) || on("objects.githubusercontent.com") || on("release-assets.githubusercontent.com"));
}

std::string read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string sha256_hex(const std::string& msg) {
  static constexpr uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  std::string d = msg + char(0x80);
  while (d.size() % 64 != 56) d += '\0';
  uint64_t bits = (uint64_t)msg.size() * 8;
  for (int i = 7; i >= 0; i--) d += (char)(bits >> (8 * i));
  for (size_t off = 0; off < d.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      const unsigned char* p = (const unsigned char*)d.data() + off + 4 * i;
      w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    std::copy(h, h + 8, v);
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = v[7] + (rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
      uint32_t t2 = (rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
      for (int j = 7; j > 0; j--) v[j] = v[j - 1];
      v[4] += t1;
      v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
  }
  std::string out;
  for (uint32_t x : h) out += std::format("{:08x}", x);
  return out;
}

bool verify_release(const fs::path& tar, const fs::path& sig, const fs::path& signers) {
  std::string bytes = read_file(tar);
  auto r = run({"ssh-keygen", "-Y", "verify", "-f", signers.string(), "-I", "framey-release", "-n", "framey-fan-release", "-s", sig.string()}, bytes, false, 60);
  if (r.code) log("Fan Control's release signature did not verify, so Fan Control was not installed. Nothing was changed.\n" + tail(r.out));
  g_fan_sha = sha256_hex(bytes);
  return !r.code;
}

std::string latest_tag(std::string& why) {
  auto api = curl({"--max-time", "20", "-H", "Accept: application/vnd.github+json"}, std::format("https://api.github.com/repos/{}/frame-fan/releases/latest", kOwner));
  auto tag = top_json(api.out)["tag_name"];
  if (api.code == 22) why = "GitHub refused the request, possibly the rate limit";
  else if (api.code) why = "GitHub is unreachable";
  else if (tag.empty() || tag.size() > 64 || tag.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos) why = "no release tag found";
  else return tag;
  return "";
}

struct Version {
  std::vector<int> core;
  std::string pre;
};

std::optional<Version> version_of(std::string s) {
  if (s.starts_with('v')) s.erase(0, 1);
  size_t dash = s.find('-');
  Version v{{}, dash == std::string::npos ? "" : s.substr(dash + 1)};
  if (v.pre.find_first_not_of("0123456789.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos) return {};
  std::istringstream in(s.substr(0, dash));
  for (std::string part; std::getline(in, part, '.');) {
    if (part.empty() || part.size() > 9 || part.find_first_not_of("0123456789") != std::string::npos) return {};
    v.core.push_back(std::stoi(part));
  }
  if (v.core.empty()) return {};
  return v;
}

int compare_versions(const Version& a, const Version& b) {
  for (size_t i = 0; i < std::max(a.core.size(), b.core.size()); i++) {
    int x = i < a.core.size() ? a.core[i] : 0, y = i < b.core.size() ? b.core[i] : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  if (a.pre == b.pre) return 0;
  if (a.pre.empty() || b.pre.empty()) return a.pre.empty() ? 1 : -1;
  return a.pre < b.pre ? -1 : 1;
}

bool fetch_fan(const fs::path& tgz, std::string& tag, const std::string& signers) {
  std::string why;
  tag = latest_tag(why);
  if (tag.empty()) {
    if (why == "no release tag found") log("Fan Control's release could not be found, so Fan Control was not installed. Nothing was changed.");
    return false;
  }
  std::string base = std::format("https://github.com/{}/frame-fan/releases/download/{}/frame-fan.tar.gz", kOwner, tag);
  if (!allowed_fan_url(base) || fetch(base, tgz, 120).code) return false;
  fs::path sig = tgz.string() + ".sig", signers_file = tgz.parent_path() / "allowed_signers";
  if (fetch(base + ".sig", sig, 60).code) return log("Fan Control's release has no signature file, so Fan Control was not installed. Nothing was changed."), false;
  std::ofstream(signers_file, std::ios::binary) << signers;
  return verify_release(tgz, sig, signers_file);
}

bool stage(const std::string& repo, const fs::path& root) {
  fs::path dest = root / (repo + ".new");
  fs::remove_all(dest);
  fs::create_directories(dest);
  auto tgz = root / (repo + ".tar.gz");
  if (repo == "frame-fan") {
    std::string tag;
    if (!fetch_fan(tgz, tag, kSigners)) return false;
    if (sha256_hex(read_file(tgz)) != g_fan_sha) return log("The Fan Control archive changed after its signature was checked, so Fan Control was not installed. Nothing was changed."), false;
    auto want = version_of(tag), installed = version_of(trim(ssh(std::format("cat {}/frame-fan/.version 2>/dev/null", kHome)).out));
    if (want && installed && compare_versions(*want, *installed) < 0) return log(std::format("Fan Control's latest signed release ({}) is older than the installed one, so Fan Control was not changed.", tag)), false;
    if (!extract(tgz, dest, true)) return log("Fan Control's release was verified but could not be unpacked, so Fan Control was not installed. Nothing was changed."), false;
    std::ofstream(dest / ".version") << tag;
    g_fan_tar = tgz;
    log(std::format("{}: signed release {} verified", repo, tag));
    return true;
  } else {
    std::string sha = latest_sha(repo);
    if (!sha.empty() && !fetch(std::format("https://github.com/{}/{}/archive/{}.tar.gz", kOwner, repo, sha), tgz, 60).code && extract(tgz, dest, true)) {
      std::ofstream(dest / ".version") << sha;
      log(std::format("{}: downloaded from GitHub ({})", repo, sha.substr(0, 7)));
      return true;
    }
  }
  fs::remove_all(dest);
  fs::path bundle = g_work / "bundle";
  fs::remove_all(bundle);
  if (!unpack_bundle(bundle) || !fs::exists(bundle / repo)) return log(std::format("{}: GitHub is unreachable and the bundled copy could not be opened.", repo)), false;
  fs::copy(bundle / repo, dest, fs::copy_options::recursive | fs::copy_options::skip_symlinks);
  std::ifstream v(dest / ".version");
  std::string s, cur;
  std::getline(v, s);
  cur = trim(ssh(std::format("cat {}/{}/.version 2>/dev/null", kHome, repo)).out);
  if (!cur.empty() && cur != s) {
    fs::remove_all(dest);
    return log(std::format("{}: GitHub is unreachable, and the bundled copy ({}) is not the installed version ({}). It could be older, so nothing was changed. Try again when GitHub is reachable.", repo, s.substr(0, 7), cur.substr(0, 7))), false;
  }
  log(std::format("{}: GitHub unreachable, using the bundled copy ({})", repo, s.substr(0, 7)));
  return true;
}

void restart_vr() {
  log("Restarting SteamVR if it is running, so Framey loads cleanly. A running VR session will end.");
  auto r = ssh("if systemctl --user is-active --quiet steamvr.service; then systemctl --user try-restart steamvr.service && sleep 3 && systemctl --user is-active --quiet steamvr.service && echo vr-restarted; else echo vr-idle; fi");
  if (r.out.find("vr-idle") != std::string::npos) log("SteamVR was not running, so it was left stopped.");
  else if (r.out.find("vr-restarted") != std::string::npos) log("SteamVR was restarted.");
  else log("SteamVR did not restart. Restart it from the headset, then check Framey.\n" + tail(r.out));
}

bool install_plugin(std::string src, bool restart) {
  src = unquote(src);
  fs::path work = g_work / ("plugin-" + std::to_string(std::hash<std::string>{}(src) % 100000));
  fs::remove_all(work);
  fs::create_directories(work / "x");
  fs::path archive = work / "plugin.pkg";
  std::string url;
  std::error_code ec;
  if (fs::is_regular_file(src, ec)) {
    if (fs::file_size(src) > 20'000'000) return log("That file is over 20 MB."), false;
    fs::copy_file(src, archive);
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
      std::string repo = p[1].ends_with(".git") ? p[1].substr(0, p[1].size() - 4) : p[1], ref, branch;
      for (size_t k = 3; k < p.size() && p[2] == "tree"; k++) branch += "/" + p[k];
      for (char c : branch) ref += isalnum((unsigned char)c) || strchr("-._~/", c) ? std::string(1, c) : std::format("%{:02X}", (unsigned char)c);
      bool direct = path.find("/archive/") != std::string::npos || path.find("/releases/download/") != std::string::npos || path.ends_with(".zip") || path.ends_with(".tar.gz");
      if (!direct) url = std::format("https://github.com/{}/{}/archive/{}.tar.gz", p[0], repo, branch.empty() ? "HEAD" : "refs/heads" + ref);
    } else if (!url.starts_with("https://") || !(url.ends_with(".zip") || url.ends_with(".tar.gz"))) {
      return log("File not found, or not a link. Use an existing .zip or .tar.gz file, a github.com link, or an https link to one."), false;
    }
    if (!safe_url(url)) return log("That link has characters that are not allowed."), false;
    log("Downloading the plugin...");
    auto r = fetch(url, archive, 90);
    if (r.code) return log("Download failed. Check the link.\n" + tail(r.out)), false;
  }
  if (!extract(archive, work / "x")) return log("Could not open that package. It must be a .zip or .tar.gz."), false;
  fs::path root = work / "x";
  if (!fs::exists(root / "plugin.json")) {
    std::vector<fs::path> dirs;
    for (auto& e : fs::directory_iterator(root))
      if (e.is_directory() && e.path().filename() != "__MACOSX") dirs.push_back(e.path());
    if (dirs.size() != 1 || !fs::exists(dirs[0] / "plugin.json")) return log("No plugin.json at the top of that package. A Framey plugin has plugin.json plus main.js and/or backend.py."), false;
    root = dirs[0];
  }
  if (!fs::exists(root / "main.js") && !fs::exists(root / "backend.py")) return log("That plugin has neither main.js nor backend.py."), false;
  std::ifstream mf(root / "plugin.json");
  auto meta = top_json(std::string((std::istreambuf_iterator<char>(mf)), {}));
  std::string id = meta["id"], name = meta["name"];
  if (!valid_id(id)) return log("plugin.json must be valid JSON: an object with an \"id\" (lowercase letters, digits, - and _, up to 32, not starting with -)."), false;
  for (auto k : {"name", "version", "short"})
    if (meta.contains(k) && meta[k] == "\\") return log(std::format("plugin.json: \"{}\" must be a string, as Framey requires.", k)), false;
  fs::path stage_dir = work / "stage" / (".stage-" + id);
  fs::create_directories(stage_dir.parent_path());
  fs::copy(root, stage_dir, fs::copy_options::recursive | fs::copy_options::skip_symlinks);
  fs::remove_all(stage_dir / ".git");
  if (!url.empty()) std::ofstream(stage_dir / ".source") << url;
  log(std::format("Installing plugin {} ({}) on the headset...", name.empty() ? id : name, id));
  auto r = ssh("sh -s", std::format("P={0}/framey/plugins\n[ -L $P/{1} ] && exit 3\nmkdir -p $P || exit 1\nrm -rf $P/.stage-{1} $P/.old-{1} || exit 1\n", kHome, id));
  if (r.code == 3) return log("A linked plugin with that id is already there (for example Fan Control). Remove it first."), false;
  if (r.code) return log("Could not prepare the plugins folder.\n" + tail(r.out)), false;
  if (!upload(stage_dir, std::format("{}/framey/plugins", kHome))) return false;
  r = ssh("sh -s", std::format("cd {0}/framey/plugins || exit 1\n[ -f .stage-{1}/plugin.json ] || exit 5\nif [ -e {1} ]; then mv {1} .old-{1} || exit 1; fi\n"
                               "mv .stage-{1} {1} || {{ if [ -e .old-{1} ]; then mv .old-{1} {1}; fi; exit 1; }}\nrm -rf .old-{1}\n"
                               "python3 - <<'EOT' || exit 4\nimport json,os\np=os.path.expanduser('~/.config/framey/settings.json')\ntry: s=json.load(open(p))\nexcept Exception: raise SystemExit\n"
                               "s['disabled']=[x for x in s.get('disabled',[]) if x!='{1}']\njson.dump(s,open(p,'w'))\nEOT\n",
                               kHome, id));
  if (r.code) return log((r.code == 5 ? "The upload arrived incomplete; the old plugin was kept.\n" : r.code == 4 ? "The plugin was installed but could not be re-enabled in Framey's settings.\n" : "Could not swap the plugin in; the old one was kept.\n") + tail(r.out)), false;
  if (restart) {
    r = ssh(std::format("systemctl --user restart framey.service && sleep 2 && systemctl --user is-active framey.service && test -f {}/framey/plugins/{}/plugin.json && echo plugin-in-place", kHome, id));
    if (r.out.find("plugin-in-place") == std::string::npos) return log("The plugin was copied but Framey did not restart cleanly.\n" + tail(r.out)), false;
    restart_vr();
  }
  log(std::format("Plugin {} is installed.", id));
  return true;
}

const char* kService =
    "[Unit]\nDescription=Framey\n\n[Service]\nEnvironment=PYTHONDONTWRITEBYTECODE=1\n"
    "ExecStartPre=-/bin/sh -c \"f=%h/framey/framey.py; if [ ! -f $$f ] || ! python3 -c 'import ast,sys;ast.parse(open(sys.argv[1]).read())' $$f; then "
    "s=%h/framey.next; for c in %h/framey.bak %h/framey.bak.prev; do if ! python3 -c 'import ast,sys;ast.parse(open(sys.argv[1]).read())' $$s/framey.py 2>/dev/null; then s=$$c; fi; done; "
    "if python3 -c 'import ast,sys;ast.parse(open(sys.argv[1]).read())' $$s/framey.py 2>/dev/null; then rm -rf %h/framey.broken; for k in plugins store; do if [ -e %h/framey/$$k ] && [ ! -e $$s/$$k ]; then mv %h/framey/$$k $$s/$$k; fi; done; "
    "if [ -e %h/framey ]; then mv %h/framey %h/framey.broken || exit 1; fi; mv $$s %h/framey; fi; fi\"\n"
    "ExecStart=/usr/bin/python3 /home/steamos/framey/framey.py\nRestart=always\nRestartSec=5\nNice=19\n"
    "CPUSchedulingPolicy=idle\nCPUWeight=1\nMemoryHigh=128M\nMemoryMax=150M\nKillMode=mixed\nTimeoutStopSec=15\n\n[Install]\nWantedBy=default.target\n";

std::string rollback_script(const std::string& initial_repositories, const std::string& backup_suffix) {
  return "d='" + initial_repositories + "'\nroll() {\n  for n in $d; do\n    for k in plugins store; do if [ -d $n" + backup_suffix + " ] && [ -e $n/$k ] && [ ! -e $n" + backup_suffix + "/$k ]; then mv $n/$k $n" + backup_suffix + "/$k; fi; done\n"
         "    rm -rf $n\n    if [ -d $n" + backup_suffix + " ]; then mv $n" + backup_suffix + " $n; fi\n  done\n  [ -e framey/plugins/fan ] || rm -f framey/plugins/fan\n  systemctl --user restart framey.service\n  echo rolled-back\n  exit 1\n}\n";
}

std::string promote_backups_script(const std::string& names) {
  return std::format("for n in {0}; do if [ -e $n.next ]; then rm -rf $n.bak.prev; if [ -e $n.bak ]; then mv $n.bak $n.bak.prev || exit 1; fi; mv $n.next $n.bak || exit 1; fi; done\n", names);
}

void do_install() {
  log("[1/6] Connecting to the headset...");
  if (!authorize()) return;
  if (g_fan && g_pw.empty()) return log("Fan Control needs the headset password for its system step. Enter it and run Install again.");
  if (g_fan && ssh("sudo -S -p '' true", g_pw + "\n").code) return log("The headset did not accept that password, so nothing was changed.");
  fs::path root = g_work / "stage";
  fs::create_directories(root);
  std::vector<std::string> repos = {"framey"};
  if (g_fan) repos.push_back("frame-fan");
  log("[2/6] Getting the latest versions...");
  for (auto& r : repos) {
    if (!stage(r, root)) {
      if (r == "framey") return;
      log("Fan Control was not installed: could not reach or verify the signed release. Run Install / Update again when online.");
      fs::remove_all(root / (r + ".new"));
      g_fan = false;
      repos.pop_back();
      break;
    }
    fs::remove_all(root / (r + ".new") / "tests");
  }
  log("[3/6] Copying files to the headset...");
  std::string names;
  for (auto& r : repos) names += (names.empty() ? "" : " ") + r;
  auto r = ssh("sh -s", std::format("cd {} || exit 1\nfor n in {}; do\n  if [ ! -e $n ] && [ -d $n.next ]; then mv $n.next $n || exit 1; fi\n  if [ ! -e $n ] && [ -d $n.bak ]; then mv $n.bak $n || exit 1; fi\n  rm -rf $n.new || exit 1\ndone\n{}", kHome, names, promote_backups_script(names)));
  if (r.code) return log("Could not prepare the headset for the update.\n" + tail(r.out));
  for (auto& n : repos)
    if (!upload(root / (n + ".new"), kHome)) return;
  log("[4/6] Setting up Framey...");
  r = ssh("sh -s", std::format("cd {0} || exit 1\nfor n in {1}; do [ -s $n.new/.version ] && {{ [ -f $n.new/framey.py ] || [ -f $n.new/plugin.json ]; }} || exit 1; done\n"
                               "python3 -c 'import ast,sys;ast.parse(open(sys.argv[1]).read())' framey.new/framey.py || exit 1\n{4}"
                               "mkdir -p .config/systemd/user || exit 1\nif systemctl --user is-active --quiet framey.service; then systemctl --user stop framey.service || exit 1; fi\n"
                               "for n in {1}; do\n  if [ -e $n ]; then mv $n $n.next || roll; fi\n  d=\"$d $n\"\n  mv $n.new $n || roll\ndone\n"
                               "for k in plugins store; do if [ -e framey.next/$k ] && [ ! -e framey/$k ]; then mv framey.next/$k framey/$k || roll; fi; done\n"
                               "mkdir -p framey/plugins || roll\ncat > .config/systemd/user/framey.service <<'EOT' || roll\n{2}EOT\n{3}"
                               "systemctl --user daemon-reload || roll\nsystemctl --user enable framey.service || roll\nsystemctl --user restart framey.service || roll\ni=0\nuntil systemctl --user is-active --quiet framey.service || [ $i -ge 15 ]; do sleep 2; i=$((i+1)); done\nsystemctl --user is-active --quiet framey.service || roll\n{5}",
                               kHome, names, kService, g_fan ? "ln -sfn /home/steamos/frame-fan framey/plugins/fan || roll\n" : "", rollback_script("", ".next"), promote_backups_script(names)));
  if (r.code) return log("Framey setup did not finish; the installed state is unconfirmed (the connection may have dropped). Run Install again.\n" + tail(r.out));
  if (g_fan) {
    log("[5/6] Installing Fan Control (about 20 seconds)...");
    std::string tar = read_file(g_fan_tar), sha = sha256_hex(tar);
    if (sha != g_fan_sha) return log("The Fan Control archive changed after its signature was checked, so Fan Control was not installed. Framey was updated.");
    std::ofstream(g_work / "frame-fan-root.tgz", std::ios::binary).write(tar.data(), tar.size());
    if (!upload(g_work / "frame-fan-root.tgz", kHome)) return;
    r = ssh(std::format("sudo -S -p '' sh -c 'd=$(mktemp -d /var/tmp/ff.XXXXXX) || exit 1; cp {0}/frame-fan-root.tgz $d/t.tgz && echo \"{1}  $d/t.tgz\" | sha256sum -c - >/dev/null && "
                        "tar --no-same-owner --no-same-permissions -xzf $d/t.tgz -C $d && bash $d/frame-fan/install-root.sh --signer-fp {2}; rc=$?; rm -rf $d; exit $rc'; rc=$?; rm -f {0}/frame-fan-root.tgz; exit $rc",
                        kHome, sha, kSignerFp),
            g_pw + "\n");
    if (r.code) {
      ssh("sh -s", std::format("cd {} || exit 1\n{}roll\n", kHome, rollback_script("frame-fan", ".bak")));
      return log("Fan Control's system step failed. Its system state and folder restoration are unconfirmed; restoring the previous Fan Control folder was attempted. Framey itself was updated (the old Framey stays in framey.bak until the next Install). Check fan control on the headset and run Install again.\n" + tail(r.out));
    }
    log(tail(r.out, 3));
    log("The protected updater and its trust key were installed (the system step finished with exit code 0). Routine Fan Control and Framey updates can now be done inside the headset from Settings and Modules, without the password.");
  }
  log("[6/6] Updating plugins installed from links...");
  r = ssh(std::format("for d in {0}/framey/plugins/*/; do d=${{d%/}}; [ -L \"$d\" ] && continue; [ -f \"$d/.source\" ] && echo \"$(cat \"$d/.source\")\"; done", kHome));
  std::istringstream list(r.out);
  int updated = 0;
  for (std::string u; std::getline(list, u);) {
    u = trim(u);
    if (u.empty()) continue;
    if (!github_source(u)) log("A plugin's saved source is not a github.com https link, so it was not refreshed.");
    else if (install_plugin(u, false)) updated++;
  }
  if (updated) ssh("systemctl --user restart framey.service");
  restart_vr();
  r = ssh("systemctl --user is-active --quiet framey.service && echo framey-active; ss -ltn 2>/dev/null | grep -q ':8080 ' && echo debug-port-open || echo debug-port-closed");
  if (r.out.find("debug-port-closed") != std::string::npos) log("Warning: Steam's debug port is not open. Make sure Steam is running on the headset.");
  if (r.out.find("framey-active") == std::string::npos) return log("Framey is not running. The previous version was kept as framey.bak on the headset.\n" + tail(r.out));
  ssh(std::format("cd {} && for n in {}; do rm -rf $n.bak $n.bak.prev; done", kHome, names));
  log("\nDone. Put the headset on and tap the Framey icon in the bottom bar.");
  status("Installed.");
}

void do_check() {
  if (!authorize()) return;
  auto r = ssh("for r in framey frame-fan; do echo $r=$(cat /home/steamos/$r/.version 2>/dev/null); done");
  if (r.code) return log("Could not read the versions from the headset.\n" + tail(r.out)), status("Check failed.");
  auto have = [&](const std::string& repo) {
    std::istringstream in(r.out);
    for (std::string l; std::getline(in, l);)
      if (l.starts_with(repo + "=")) return trim(l.substr(repo.size() + 1));
    return std::string{};
  };
  std::string fw = have("framey"), now = latest_sha("framey");
  std::string line = std::format("framey: {}, latest {}", fw.empty() ? "not installed" : fw.substr(0, 7), now.empty() ? "unknown" : now.substr(0, 7));
  if (!fw.empty() && !now.empty()) line += fw == now ? " (up to date)" : " (update available)";
  log(line);
  std::string all = line + "   ";
  std::string why, cur = have("frame-fan"), tag = latest_tag(why);
  if (!cur.empty() && !version_of(cur)) {
    std::string v = top_json(ssh(std::format("cat {}/frame-fan/plugin.json 2>/dev/null", kHome)).out)["version"];
    if (!v.empty()) cur = v;
  }
  auto want = version_of(tag), installed = version_of(cur);
  std::string state;
  if (!want) state = "could not check (" + (tag.empty() ? why : "unrecognized release tag") + ")";
  else if (cur.empty()) state = "not installed";
  else if (!installed) state = "installed version unknown: update available";
  else state = compare_versions(*installed, *want) < 0 ? "update available" : "up to date";
  line = std::format("frame-fan: installed {}, latest {}, {}", cur.empty() ? "none" : cur, tag.empty() ? "unknown" : tag, state);
  log(line);
  status(all + line);
}

std::string drop_key(const std::string& blob) {
  return std::format("f=$HOME/.ssh/authorized_keys\n[ -f $f ] || exit 0\nawk -v b='{0}' '{{k=0;for(i=1;i<=NF;i++)if($i==b)k=1;if(!k)print}}' $f > $f.tmp || exit 1\ncat $f.tmp > $f || exit 1\nrm -f $f.tmp\n! grep -qF '{0}' $f\n", blob);
}

void do_remove() {
  auto fail = [](const std::string& m) {
    log(m + "\nRemove stopped. The app's key and data were kept so you can run Remove again.");
    status("Remove failed.");
  };
  if (!authorize()) return status("Remove failed.");
  fs::path key = key_file();
  std::string type, blob;
  std::istringstream(pub_line(key)) >> type >> blob;
  if (blob.empty()) return fail("Could not read the app's key from " + key.string() + " or derive it from the private key.");
  auto root = [] {
    return trim(tail(ssh("sh -s", "for p in /etc/frame-fan /etc/systemd/system/frame-fan.path /etc/systemd/system/frame-fan.service /etc/systemd/system/frame-fan-stock.service /etc/systemd/system/deckard-fan-control.service.d/frame-fan.conf; do\n"
                                  "  if [ -e $p ]; then echo present; exit 0; fi\ndone\necho clean\n").out, 1));
  };
  std::string st = root();
  if (st != "present" && st != "clean") return fail("Could not check Fan Control on the headset.");
  if (st == "present") {
    if (g_pw.empty()) return fail("Fan Control is installed, and removing it needs the headset password. Enter it and try again.");
    log("Removing Fan Control (restores stock fan control)...");
    fs::path b = g_work / "bundle", u = g_work / "frame-fan-uninstall.sh";
    if (!unpack_bundle(b) || !fs::exists(b / "frame-fan" / "uninstall-root.sh")) return fail("Could not find Fan Control's uninstall script.");
    fs::copy_file(b / "frame-fan" / "uninstall-root.sh", u, fs::copy_options::overwrite_existing);
    if (!upload(u, kHome)) return fail("Could not upload Fan Control's uninstall script.");
    std::string sha = sha256_hex(read_file(u));
    auto r = ssh(std::format("sudo -S -p '' sh -c 'd=$(mktemp -d /var/tmp/ff.XXXXXX) || exit 1; cp {0}/frame-fan-uninstall.sh $d/u.sh && echo \"{1}  $d/u.sh\" | sha256sum -c - >/dev/null && "
                             "rm -f /run/frame-fan.stamp && bash $d/u.sh; rc=$?; rm -rf $d; exit $rc'; rc=$?; rm -f {0}/frame-fan-uninstall.sh; exit $rc",
                             kHome, sha),
                 g_pw + "\n");
    log(tail(r.out, 2));
    auto on = ssh("for i in 1 2 3 4 5; do systemctl is-active --quiet deckard-fan-control && echo active && exit 0; sleep 2; done; echo inactive");
    if (r.code || root() != "clean" || trim(tail(on.out, 1)) != "active") return fail("Fan Control could not be removed (wrong password or a failed system step), so its system files may still be on the headset.");
    log("Fan Control's uninstall script finished with exit code 0; it also removes the protected updater, its sudo rule and its trust key.");
  }
  log("Removing Framey and its plugins...");
  auto r = ssh("sh -s", "if systemctl --user cat framey.service >/dev/null 2>&1; then systemctl --user disable --now framey.service || exit 1; fi\nrm -f ~/.config/systemd/user/framey.service || exit 1\nsystemctl --user daemon-reload || exit 1\n"
                        "rm -rf ~/framey ~/framey.new ~/framey.next ~/framey.bak ~/framey.bak.prev ~/framey.broken ~/frame-fan ~/frame-fan.new ~/frame-fan.next ~/frame-fan.bak ~/frame-fan.bak.prev ~/.config/framey ~/.config/frame-fan || exit 1\n");
  if (r.code) return fail("Removal had errors.\n" + tail(r.out));
  log("Removing this app's key from the headset...");
  r = ssh("sh -s", drop_key(blob));
  if (r.code) return fail("Could not remove the app's key.\n" + tail(r.out));
  std::string left;
  std::error_code ec;
  auto del = [&](const fs::path& p) {
    fs::remove_all(p, ec);
    if (fs::exists(p, ec)) left += "\n  " + p.string();
  };
  del(key);
  del(key.string() + ".pub");
  fs::path hosts = g_dir / "known_hosts";
  run({"ssh-keygen", "-q", "-R", g_target, "-f", hosts.string()});
  if (!run({"ssh-keygen", "-F", g_target, "-f", hosts.string()}).code) left += "\n  this headset's entry in " + hosts.string();
  del(hosts.string() + ".old");
  bool others = false;
  for (auto& e : fs::directory_iterator(g_dir, ec))
    if (e.path().filename().string().starts_with("key-")) others = true;
  if (!others) del(g_dir);
  if (!left.empty()) return log("The headset is clean, but these items on this computer could not be removed. Delete them yourself:" + left), status("Removed, with leftovers on this computer.");
  log(others ? "Done. This headset is clean and its key is gone; keys for other headsets were kept." : "Done. The headset and this computer are clean: the app's key, settings and temporary files are gone too.");
  status("Removed.");
}

bool valid_host(const std::string& h) {
  if (h.empty() || h.size() > 100) return false;
  for (char c : h)
    if (!isalnum((unsigned char)c) && c != '.' && c != '-' && c != ':') return false;
  return true;
}

void finish() {
  g_pw.assign(g_pw.size(), '\0');
  g_pw.clear();
  std::error_code ec;
  fs::remove_all(g_work, ec);
  {
    std::lock_guard l(g_mu);
    if (g_status == "Working...") g_status.clear();
  }
  g_busy = false;
}

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
  try {
    fs::create_directories(g_dir);
    g_work = g_dir / ("work-" + g_token);
    fs::create_directory(g_work);
    fs::permissions(g_work, fs::perms::owner_all, fs::perm_options::replace);
    std::ofstream(g_dir / "host.txt") << g_target;
    status("Working...");
    std::thread([action] {
      struct Done {
        ~Done() { finish(); }
      } done;
      try {
        if (!need_tools(action, g_source)) return;
        if (action == "install") {
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
      } catch (...) {
        log("Error: the job stopped unexpectedly.");
      }
    }).detach();
  } catch (const std::exception& e) {
    log(std::string("Error: ") + e.what());
    finish();
  }
}

std::string saved(const char* file) {
  std::ifstream f(g_dir / file);
  std::string s;
  std::getline(f, s);
  return trim(s);
}

void save_theme(const std::string& t) {
  if (std::find(std::begin(kThemes), std::end(kThemes), t) == std::end(kThemes)) return;
  std::error_code ec;
  fs::create_directories(g_dir, ec);
  std::ofstream(g_dir / "theme.txt") << t;
}

bool busy() { return g_busy; }

Snap snapshot(size_t since) {
  std::lock_guard l(g_mu);
  since = std::min(since, g_log.size());
  return {g_busy, g_status, g_log.substr(since), g_log.size()};
}

int main(int argc, char** argv) {
  if (std::getenv("FRAMEY_ASKPASS")) {
    std::string prompt = argc > 1 ? argv[1] : "";
    std::transform(prompt.begin(), prompt.end(), prompt.begin(), [](unsigned char ch) { return (char)tolower(ch); });
    const char* pw = std::getenv("FRAMEY_PW");
    std::string answer = prompt.find("assword") != std::string::npos ? std::string(pw ? pw : "") + "\n" : "yes\n";
#ifdef _WIN32
    DWORD n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), answer.data(), (DWORD)answer.size(), &n, nullptr);
#else
    std::fwrite(answer.data(), 1, answer.size(), stdout);
#endif
    return 0;
  }
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif
  g_dir = fs::path(self()).parent_path() / "FrameyApp-data";
#ifdef _WIN32
  SetCurrentDirectoryA(fs::path(self()).parent_path().string().c_str());
#endif
  g_token = rand_hex(32);
  log("Framey App 1.0.2, GPL-2.0 only. Source and license: https://github.com/chaosfox26/framey-app");
  return ui_run();
}
