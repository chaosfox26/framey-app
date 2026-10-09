#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
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
constexpr const char* kRepos[] = {"framey", "frame-fan"};
constexpr const char* kHome = "/home/steamos";
constexpr size_t kMaxFiles = 500, kMaxDepth = 8, kMaxOut = 1 << 22;
constexpr uintmax_t kMaxBytes = 20'000'000;

using Args = std::vector<std::string>;

struct Run {
  std::string out;
  int code = 1;
};

std::mutex g_mu;
std::string g_log, g_status, g_pw, g_target, g_source;
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

void set_env(const char* k, const char* v) {
#ifdef _WIN32
  SetEnvironmentVariableA(k, v);
#else
  v ? setenv(k, v, 1) : unsetenv(k);
#endif
}

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

Run run_raw(const Args& a, const std::string& in, int secs, const fs::path& quota) {
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
Run run_raw(const Args& a, const std::string& in, int secs, const fs::path& quota) {
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
  if (askpass) {
    set_env("FRAMEY_ASKPASS", "1");
    set_env("FRAMEY_PW", g_pw.c_str());
    set_env("SSH_ASKPASS", self().c_str());
    set_env("SSH_ASKPASS_REQUIRE", "force");
  }
  Run r = run_raw(a, in, secs, quota);
  if (askpass)
    for (auto k : {"FRAMEY_ASKPASS", "FRAMEY_PW", "SSH_ASKPASS", "SSH_ASKPASS_REQUIRE"}) set_env(k, nullptr);
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

std::string stage(const std::string& repo, const fs::path& root) {
  fs::path dest = root / (repo + ".new");
  fs::remove_all(dest);
  fs::create_directories(dest);
  auto tgz = root / (repo + ".tar.gz");
  std::string sha = latest_sha(repo);
  if (!sha.empty() && !fetch(std::format("https://github.com/{}/{}/archive/{}.tar.gz", kOwner, repo, sha), tgz, 60).code && extract(tgz, dest, true)) {
    std::ofstream(dest / ".version") << sha;
    log(std::format("{}: downloaded from GitHub ({})", repo, sha.substr(0, 7)));
    return sha;
  }
  fs::remove_all(dest);
  fs::path bundle = g_work / "bundle";
  fs::remove_all(bundle);
  if (!unpack_bundle(bundle) || !fs::exists(bundle / repo)) return log(std::format("{}: GitHub is unreachable and the bundled copy could not be opened.", repo)), "";
  fs::copy(bundle / repo, dest, fs::copy_options::recursive | fs::copy_options::skip_symlinks);
  std::ifstream v(dest / ".version");
  std::string s, cur;
  std::getline(v, s);
  cur = trim(ssh(std::format("cat {}/{}/.version 2>/dev/null", kHome, repo)).out);
  if (!cur.empty() && cur != s) {
    fs::remove_all(dest);
    return log(std::format("{}: GitHub is unreachable, and the bundled copy ({}) is not the installed version ({}). It could be older, so nothing was changed. Try again when GitHub is reachable.", repo, s.substr(0, 7), cur.substr(0, 7))), "";
  }
  log(std::format("{}: GitHub unreachable, using the bundled copy ({})", repo, s.substr(0, 7)));
  return s.empty() ? "bundled" : s;
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
    "ExecStart=/usr/bin/python3 /home/steamos/framey/framey.py\nRestart=always\nRestartSec=3\nNice=19\n"
    "CPUSchedulingPolicy=idle\nCPUWeight=1\nMemoryHigh=40M\nMemoryMax=60M\n\n[Install]\nWantedBy=default.target\n";

std::string roll_fn(const std::string& d) {
  return "d='" + d + "'\nroll() {\n  for n in $d; do\n    for k in plugins store; do if [ -d $n.bak ] && [ -e $n/$k ] && [ ! -e $n.bak/$k ]; then mv $n/$k $n.bak/$k; fi; done\n"
         "    rm -rf $n\n    if [ -d $n.bak ]; then mv $n.bak $n; fi\n  done\n  [ -e framey/plugins/fan ] || rm -f framey/plugins/fan\n  systemctl --user restart framey.service\n  echo rolled-back\n  exit 1\n}\n";
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
  for (auto& r : repos)
    if (stage(r, root).empty()) return;
  log("[3/6] Copying files to the headset...");
  std::string names;
  for (auto& r : repos) names += (names.empty() ? "" : " ") + r;
  auto r = ssh("sh -s", std::format("cd {} || exit 1\nfor n in {}; do\n  if [ ! -e $n ] && [ -d $n.bak ]; then mv $n.bak $n || exit 1; fi\n  rm -rf $n.new $n.bak || exit 1\ndone\n", kHome, names));
  if (r.code) return log("Could not prepare the headset for the update.\n" + tail(r.out));
  for (auto& n : repos)
    if (!upload(root / (n + ".new"), kHome)) return;
  log("[4/6] Setting up Framey...");
  r = ssh("sh -s", std::format("cd {0} || exit 1\nfor n in {1}; do [ -s $n.new/.version ] && {{ [ -f $n.new/framey.py ] || [ -f $n.new/plugin.json ]; }} || exit 1; done\n{4}"
                               "mkdir -p .config/systemd/user || exit 1\nif systemctl --user is-active --quiet framey.service; then systemctl --user stop framey.service || exit 1; fi\n"
                               "for n in {1}; do\n  if [ -e $n ]; then mv $n $n.bak || roll; fi\n  d=\"$d $n\"\n  mv $n.new $n || roll\ndone\n"
                               "for k in plugins store; do if [ -e framey.bak/$k ] && [ ! -e framey/$k ]; then mv framey.bak/$k framey/$k || roll; fi; done\n"
                               "mkdir -p framey/plugins || roll\ncat > .config/systemd/user/framey.service <<'EOT' || roll\n{2}EOT\n{3}"
                               "systemctl --user daemon-reload || roll\nsystemctl --user enable framey.service || roll\nsystemctl --user restart framey.service || roll\nsleep 2\nsystemctl --user is-active --quiet framey.service || roll\n",
                               kHome, names, kService, g_fan ? "ln -sfn /home/steamos/frame-fan framey/plugins/fan || roll\n" : "", roll_fn("")));
  if (r.code) return log((r.out.find("rolled-back") != std::string::npos ? "Framey setup failed; the previous version was restored.\n" : "Framey setup did not finish (the connection may have dropped). Run Install again.\n") + tail(r.out));
  if (g_fan) {
    log("[5/6] Installing Fan Control (about 20 seconds)...");
    r = ssh(std::format("sudo -S -p '' bash {}/frame-fan/install-root.sh", kHome), g_pw + "\n");
    if (r.code) {
      ssh("sh -s", std::format("cd {} || exit 1\n{}roll\n", kHome, roll_fn("frame-fan")));
      return log("Fan Control's system step failed, although the password was accepted. Fan Control was put back as it was; Framey itself was updated (the old Framey stays in framey.bak until the next Install).\n" + tail(r.out));
    }
    log(tail(r.out, 3));
  }
  log("[6/6] Updating plugins installed from links...");
  r = ssh(std::format("for d in {0}/framey/plugins/*/; do d=${{d%/}}; [ -L \"$d\" ] && continue; [ -f \"$d/.source\" ] && echo \"$(cat \"$d/.source\")\"; done", kHome));
  std::istringstream list(r.out);
  int updated = 0;
  for (std::string u; std::getline(list, u);)
    if (!trim(u).empty() && install_plugin(trim(u), false)) updated++;
  if (updated) ssh("systemctl --user restart framey.service");
  restart_vr();
  r = ssh("systemctl --user is-active framey.service; ss -ltn 2>/dev/null | grep -q ':8080 ' && echo debug-port-open || echo debug-port-closed");
  if (r.out.find("debug-port-closed") != std::string::npos) log("Warning: Steam's debug port is not open. Make sure Steam is running on the headset.");
  if (r.out.find("active") != 0 && r.out.find("\nactive") == std::string::npos) return log("Framey is not running. The previous version was kept as framey.bak on the headset.\n" + tail(r.out));
  ssh(std::format("cd {} && rm -rf framey.bak frame-fan.bak", kHome));
  log("\nDone. Put the headset on and tap the Framey icon in the bottom bar.");
  status("Installed.");
}

void do_check() {
  if (!authorize()) return;
  auto r = ssh("for r in framey frame-fan; do echo $r=$(cat /home/steamos/$r/.version 2>/dev/null); done");
  if (r.code) return log("Could not read the versions from the headset.\n" + tail(r.out)), status("Check failed.");
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
    fs::path b = g_work / "bundle";
    if (ssh(std::format("mkdir -p {0}/frame-fan && test -f {0}/frame-fan/uninstall-root.sh", kHome)).code && !(unpack_bundle(b) && upload(b / "frame-fan" / "uninstall-root.sh", std::format("{}/frame-fan", kHome))))
      return fail("Could not find Fan Control's uninstall script.");
    auto r = ssh(std::format("sudo -S -p '' bash {}/frame-fan/uninstall-root.sh", kHome), g_pw + "\n");
    log(tail(r.out, 2));
    auto on = ssh("for i in 1 2 3 4 5; do systemctl is-active --quiet deckard-fan-control && echo active && exit 0; sleep 2; done; echo inactive");
    if (r.code || root() != "clean" || trim(tail(on.out, 1)) != "active") return fail("Fan Control could not be removed (wrong password or a failed system step), so its system files may still be on the headset.");
  }
  log("Removing Framey and its plugins...");
  auto r = ssh("sh -s", "if systemctl --user cat framey.service >/dev/null 2>&1; then systemctl --user disable --now framey.service || exit 1; fi\nrm -f ~/.config/systemd/user/framey.service || exit 1\nsystemctl --user daemon-reload || exit 1\n"
                        "rm -rf ~/framey ~/framey.new ~/framey.bak ~/frame-fan ~/frame-fan.new ~/frame-fan.bak ~/.config/framey ~/.config/frame-fan || exit 1\n");
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
        if (!need_tools(action, g_source)) {
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
  g_token = rand_hex(32);
  log("Framey App 1.0.1, GPL-2.0 only. Source and license: https://github.com/chaosfox26/framey-app");
  return ui_run();
}
