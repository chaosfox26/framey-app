# Framey App development notes

For what the app does, see the [README](../README.md). Identifiers keep the spelling `framey`. This describes version 1.0 (tag `v1.0.0`).

## Layout

| Path | Purpose |
|---|---|
| `src/core.cpp`, `src/core.h` | Process spawning, SSH, install, check, plugin and remove jobs, archive vetting. Shared by the native `ui_*` files. |
| `src/ui_win.cpp`, `src/ui_mac.mm`, `src/ui_gtk.cpp` | The native window for Windows (Win32), macOS (Cocoa) and Linux (GTK 3). |
| `CMakeLists.txt` | One target, `FrameyApp`; picks the `ui_*` file by platform. |
| `tools/pack.py` | Builds `generated/payload.inc` (a gzip tar of the `framey` and `frame-fan` repositories at their current `HEAD`, plus a `.version` file with each commit id). |
| `tools/build.ps1` | Windows build (pack, then CMake with clang and Ninja). |
| `tools/make_icon.py` | Rebuilds `assets/framey.ico` from the Framey icon (needs Pillow and Chrome; the paths are hard-coded). |
| `app.rc`, `app.manifest`, `assets/` | Windows icon and manifest. |
| `.github/workflows/build.yml` | Builds Windows, Linux x86_64, Linux arm64 and macOS, and attaches them to an existing release. |

`generated/` and `build/` are git-ignored.

## Build

All builds first need `generated/payload.inc`, which the code includes as the bundled fallback copy. `tools/pack.py` needs Python 3 and git, and expects clones of `framey` and `frame-fan` next to this repository in folders named `Framey` and `frame-fan`. It bundles whatever is committed at their `HEAD`.

The compiler must accept `-std=c++2c` (the code uses `std::format` and `ends_with`). Flags set in `CMakeLists.txt`: `-fno-rtti`, `-Wall`, Release is `-Os`, function and data sections are garbage-collected, and the MSVC runtime is static.

### Windows

```
powershell -File tools\build.ps1
```

`tools/build.ps1` runs `pack.py`, then configures CMake with Ninja, `clang++` and `llvm-rc` inside the Visual Studio `vcvars64.bat` environment, and builds. It hard-codes `C:\Program Files\Microsoft Visual Studio\18\Community` (for `vcvars64.bat` and its bundled CMake Ninja) and `C:\Program Files\LLVM\bin`; edit those paths for your install. The result is `build\FrameyApp.exe`. It links `user32 gdi32 comctl32 comdlg32 shell32`, with lld and the `WINDOWS` subsystem.

The Windows manifest is embedded only through `app.rc`, and the linker's own manifest generation is turned off (`/MANIFEST:NO`), because merging manifests in the linker produced an invalid manifest on the release runner.

### Linux

Install `libgtk-3-dev`, `pkg-config`, CMake and a C++26-capable compiler: `g++-14` (as in CI) or a recent clang.

```
python3 tools/pack.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14
cmake --build build
```

The result is `build/FrameyApp`, stripped (`-s`) and linked against GTK 3 (found with `pkg-config` as `gtk+-3.0`) and Threads. At run time it needs `libgtk-3`.

### macOS

Xcode's clang and CMake. The deployment target is macOS 13.3 (`-mmacosx-version-min=13.3`), ARC is on, and it links Cocoa and QuartzCore. CI builds one universal binary:

```
python3 tools/pack.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
lipo -info build/FrameyApp
```

## Release workflow

`.github/workflows/build.yml` is started by hand (`workflow_dispatch`) with one optional input, `tag`: an existing release tag to attach the builds to. With no tag it only builds and uploads workflow artifacts.

1. `pin` job: reads the current `refs/heads/main` commit of `chaosfox26/framey` and `chaosfox26/frame-fan` with `git ls-remote`.
2. `build` job, on `windows-latest`, `ubuntu-latest`, `ubuntu-24.04-arm` and `macos-latest`: checks out this repository, fetches exactly those two pinned commits into `../Framey` and `../frame-fan`, runs `tools/pack.py`, and builds. Windows uses `clang++` and `llvm-rc` with Ninja inside the Visual Studio 2022 Enterprise environment on the runner. Linux installs `g++-14 libgtk-3-dev pkg-config` and builds with `g++-14`. macOS builds the universal binary above. Packaging produces `FrameyApp-windows.exe`, and for the others a `.tar.gz` named `FrameyApp-linux-x86_64`, `FrameyApp-linux-arm64` or `FrameyApp-macos` that holds `FrameyApp` and `LICENSE`.
3. `publish` job, only when `tag` is not empty: first checks that the tag points at the very commit the workflow is running on and fails otherwise. Then it downloads all builds, writes `SHA256SUMS.txt` (for the `FrameyApp-*` files) and `PROVENANCE.txt` (three lines: the commit of `framey-app`, `framey` and `frame-fan`), fetches `LICENSE.txt` from this repository at that commit, and runs `gh release upload` for everything. The release must therefore already exist, and the workflow must be run from the tagged commit.

The release has seven files: the four builds, `SHA256SUMS.txt`, `PROVENANCE.txt` and `LICENSE.txt`. Builds are not code-signed.

Actions are pinned by commit SHA:

| Action | SHA |
|---|---|
| `actions/checkout` (v4.4.0) | `11d5960a326750d5838078e36cf38b85af677262` |
| `actions/upload-artifact` (v4.6.2) | `ea165f8d65b6e75b540449e92b4886f43607fa02` |
| `actions/download-artifact` (v4.3.0) | `d3f86a106a0bac45b974a628896c90dbdf5c8093` |

## How it runs

`core.cpp` holds the jobs and exposes `snapshot`, `start_job`, `saved` and `save_theme` (see `core.h`), plus the list of themes `kThemes` (Magenta, Blue, Black, White) and `ui_run`, which each `ui_*` file implements. The window starts jobs with `start_job`, which runs them on a detached worker thread, and polls `snapshot` every 500 ms for new log text and the status line. While a job runs, the action buttons are disabled and closing the window is blocked (and on macOS, quitting). When a job ends the password field is cleared.

External tools are started directly without a shell (`posix_spawnp` or `CreateProcess`), with the captured output capped at 4 MiB. The tools `ssh`, `scp`, `ssh-keygen`, `tar` and `curl` are checked up front; `.zip` plugins also need `bsdtar`, `unzip` or a `tar` that reports itself as bsdtar. The password reaches `ssh` through `SSH_ASKPASS`: the app starts itself with `FRAMEY_ASKPASS` set, and in that mode it prints the password (for a password prompt) or `yes` and exits. Fan Control's root step pipes the password to `sudo -S`. All remote work is done as `steamos` in `/home/steamos`, with `BatchMode` and `IdentitiesOnly`.

Data lives in `FrameyApp-data` beside the executable: `key-<address>` and `key-<address>.pub` (the address with `.` turned into `_` and `:` into `~`; a legacy `id_ed25519` is renamed to the current address's key on first use), `known_hosts`, `host.txt`, `theme.txt`, and a `work-<random>` folder per job that is removed afterwards. At start the app logs `Framey App 1.0, GPL-2.0 only` with the source link.

### Jobs

- Install: connect (authorize key) > get the latest commit of `framey` (and `frame-fan` if ticked) > upload to `.new` folders > swap in with `.bak` rollback, keeping the old `plugins` and `store` folders, write `framey.service` for the user systemd instance > Fan Control root script (`sudo`, needs the password) > re-install plugins that have a `.source` file > restart SteamVR > check that Framey is active and that port 8080 (Steam's debug port) is open.
- Fetching: the latest commit id comes from the GitHub API, then `https://github.com/chaosfox26/<repo>/archive/<commit>.tar.gz` is downloaded (https only, redirects also https only, 20 MB cap, 60 s). If any step fails the bundled copy is used instead.
- Check: reads `.version` on the headset and compares it with the latest commit for both repositories.
- Remove: see the README. Order: check Fan Control's root state > root uninstall (uploaded from the bundle if missing on the headset) > remove Framey files, settings and service > remove this app's key by exact match > delete local key, known_hosts entry and, if no other `key-*` file is left, the whole data folder.
- Add plugin: install a package, then restart `framey.service` and SteamVR.

## Plugin packages

A package is a `.zip` or `.tar.gz` with `plugin.json` at its top level or inside one top folder, plus `main.js` and/or `backend.py`. `plugin.json` must be a JSON object (duplicate top-level keys are rejected) with an `id` of lowercase letters, digits, `-` and `_`, up to 32 characters; there is no fallback name. The source is a local file, a `github.com` link (a repository link downloads the default branch, a `/tree/<branch>` link that branch, and direct archive, release or `.zip`/`.tar.gz` links are used as they are), or another https link ending in `.zip` or `.tar.gz`. Local files over 20 MB are refused.

Before anything is unpacked the package listing is read (`tar -tf` / `-tvf`, or `unzip -Z`) and rejected for absolute paths, `..`, backslashes, colons or control characters in names, names over 512 characters, links or special files, duplicate names (case-insensitive), more than 500 entries, more than 8 levels, or more than 20 MB declared. Each attempt unpacks into a fresh folder under a 60 second limit, and the result is checked again for links, special files, entry count and size. A plugin whose id belongs to a symlinked plugin on the headset (for example Fan Control) is refused.

The plugin is uploaded as `.stage-<id>` in `~/framey/plugins`, checked for `plugin.json`, swapped in with the old copy kept as `.old-<id>` until the swap succeeds, and removed from the `disabled` list in `~/.config/framey/settings.json`. Plugins from a link get a `.source` file so Install / Update can refresh them. See the [Framey plugin guide](https://github.com/chaosfox26/framey/blob/main/docs/plugins.md).
