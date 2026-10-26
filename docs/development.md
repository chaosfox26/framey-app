# Framey App development notes

For what the app does, see the [README](../README.md). Identifiers keep the spelling `framey`. This describes version 1.0.2.

## Layout

| Path | Purpose |
|---|---|
| `src/core.cpp`, `src/core.h` | Process spawning, SSH, install, check, plugin and remove jobs, archive vetting. Shared by the native `ui_*` files. |
| `src/ui_win.cpp`, `src/ui_mac.mm`, `src/ui_gtk.cpp` | The native window for Windows (Win32), macOS (Cocoa) and Linux (GTK 3). |
| `CMakeLists.txt` | One target, `FrameyApp`; picks the `ui_*` file by platform. |
| `tools/pack.py` | Builds `generated/payload.inc`: a gzip tar of the `framey` and `frame-fan` repositories at their current `HEAD`, plus a `.version` file in each holding the commit id. Text members are normalized from CRLF to LF, and the build fails if any `.sh` member still contains a CR byte. The gzip header carries no time stamp. |
| `tools/build.ps1` | Windows build: runs `pack.py`, then CMake with clang, Ninja and `llvm-rc` inside `vcvars64.bat`. |
| `tools/make_icon.py` | Rebuilds `assets/framey.ico` from the sibling `Framey/icon.svg` brand source (needs Pillow and Chrome; the paths are hard-coded). |
| `app.rc`, `app.manifest`, `assets/` | Windows icon and manifest (manifest version 1.0.2.0). |
| `.github/workflows/build.yml` | Builds Windows, Linux x86_64, Linux arm64 and macOS, and attaches them to an existing release. |

`generated/` and `build/` are git-ignored.

Icon regeneration retains the sibling `Framey/icon.svg` source convention. The app also tracks `assets/icon.svg` for its README; the two SVG copies currently match, but that alone does not establish the app copy as the canonical brand source.

## Build

All builds first need `generated/payload.inc`, which the code includes as Framey's bundled fallback copy and as the Fan Control uninstall script that Remove runs. Fan Control is never installed or updated from it. `tools/pack.py` needs Python 3 and git, and expects clones of `framey` and `frame-fan` next to this repository, in folders named `Framey` and `frame-fan`. It bundles whatever is committed at their `HEAD`.

The compiler must accept `-std=c++2c` (the code uses `std::format` and `ends_with`). `CMakeLists.txt` sets `-fno-rtti`, `-Wall`, and in Release `-Os`, function and data sections, and the static MSVC runtime.

### Windows

```
powershell -File tools\build.ps1
```

`tools/build.ps1` hard-codes `C:\Program Files\Microsoft Visual Studio\18\Community` and `C:\Program Files\LLVM\bin`; edit those paths for your install. The result is `build\FrameyApp.exe`, linked with `user32 gdi32 comctl32 comdlg32 shell32`, lld and the `WINDOWS` subsystem. The manifest is embedded only through `app.rc`; the linker's own manifest generation is off (`/MANIFEST:NO`).

### Linux

Install `libgtk-3-dev`, `pkg-config`, CMake and a C++26-capable compiler (`g++-14` as in CI, or a recent clang).

```
python3 tools/pack.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14
cmake --build build
```

The result is `build/FrameyApp`, stripped and linked against GTK 3 (`pkg-config` name `gtk+-3.0`). It needs `libgtk-3` at run time.

### macOS

Xcode's clang and CMake. The deployment target is macOS 13.3, ARC is on, and it links Cocoa and QuartzCore. CI builds one universal binary:

```
python3 tools/pack.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build
lipo -info build/FrameyApp
```

## Release workflow

`.github/workflows/build.yml` is started by hand (`workflow_dispatch`) with one optional input, `tag`: an existing release tag to attach the builds to. With no tag it only builds and uploads workflow artifacts.

1. `pin` job: reads the current `main` commit of `chaosfox26/framey` and `chaosfox26/frame-fan` with `git ls-remote`, and fails unless each is a 40-digit hex commit id.
2. `build` job on `windows-latest`, `ubuntu-latest`, `ubuntu-24.04-arm` and `macos-latest`: checks out this repository (`actions/checkout` with `persist-credentials: false`), fetches exactly the two pinned commits into `../Framey` and `../frame-fan`, runs `tools/pack.py`, and builds. Windows uses clang++ and llvm-rc with Ninja inside the Visual Studio 2022 Enterprise environment. Linux uses `g++-14`. macOS builds the universal binary and fails unless `lipo -verify_arch` finds both `arm64` and `x86_64`. Packaging gives `FrameyApp-windows.exe`, and for the others a `.tar.gz` (`FrameyApp-linux-x86_64`, `FrameyApp-linux-arm64`, `FrameyApp-macos`) holding `FrameyApp` and `LICENSE`.
3. `publish` job, only when `tag` is set: checks that the tag looks like `v1.0.2` and points at the commit the workflow runs on, then writes `SHA256SUMS.txt` (for the `FrameyApp-*` files) and `PROVENANCE.txt` (the commits of `framey-app`, `framey` and `frame-fan`), fetches `LICENSE.txt` at that commit, and runs `gh release upload`. The release must already exist, and the workflow must run from the tagged commit.

The release has seven files: the four builds, `SHA256SUMS.txt`, `PROVENANCE.txt` and `LICENSE.txt`. Builds are not code-signed. Actions are pinned by commit SHA: `actions/checkout` v4.4.0, `actions/upload-artifact` v4.6.2, `actions/download-artifact` v4.3.0.

## How it runs

`core.cpp` holds the jobs and exposes `snapshot`, `busy`, `start_job`, `saved` and `save_theme` (see `core.h`), `core.h` also declares `kThemes` (Magenta, Blue, Black, White) and `ui_run`, which each `ui_*` file implements. `start_job` sets the busy flag before it starts a detached worker thread. The window polls `snapshot` every 500 ms for new log text and the status line. While a job runs, the action buttons are disabled, and closing the window (and on macOS, quitting) is refused; the close handlers also ask `busy()`. The cached `displayed_busy` state records what the last poll showed, while `busy()` covers work started since that poll. Both guards are needed. Each native palette asserts that its count matches `kThemes`; their order and the White theme at index 3 remain part of the UI contract.

External tools are started directly, without a shell (`posix_spawnp` or `CreateProcess`), with captured output capped at 4 MiB. Each call has a deadline enforced by a watchdog thread that kills the process: 120 s by default, 600 s for `scp`, 150 s for `curl`, 60 s for listing and extracting archives. A timeout returns exit code 124 and logs "did not finish within N seconds". `ssh` and `scp` get `ServerAliveInterval=10` and `ServerAliveCountMax=3`. Extraction is also watched by a 20 MB on-disk quota (exit code 125). Before use, `ssh`, `scp`, `ssh-keygen`, `tar` and `curl` are checked; `.zip` plugins also need `bsdtar`, `unzip` or a `tar` that reports itself as bsdtar.

The password reaches `ssh` through `SSH_ASKPASS`: the app starts itself with `FRAMEY_ASKPASS` set, and in that mode prints the password (for a password prompt) or `yes`, then exits. Fan Control's root step pipes the password to `sudo -S`. Passwords are set only in each child's environment block. All remote work runs as `steamos` in `/home/steamos`.

Data lives in `FrameyApp-data` beside the executable: `key-<address>` and `key-<address>.pub` (the address with `.` turned into `_` and `:` into `~`; a legacy `id_ed25519` is renamed on first use), `known_hosts`, `host.txt`, `theme.txt`, and a `work-<token>` folder. The token is random per app run, and the folder is removed after each job. Job setup does not scavenge other `work-*` folders: age alone cannot prove that another instance has stopped using one. A killed run can leave its folder behind. At start the app logs `Framey App 1.0.2, GPL-2.0 only` with the source link.

### Jobs

- **Install** (`do_install`): connect and authorize the key. If Fan Control is ticked, require a password that `sudo -S -p '' true` accepts, before changing anything. Stage each repository (`stage()`, which returns whether a usable local tree was prepared): Framey from the latest `main` commit (GitHub API, then the archive at that commit); Fan Control from the latest published release, its `frame-fan.tar.gz` and `.sig`, checked with `ssh-keygen -Y verify -n framey-fan-release` against `kSigners`. Before extraction and before upload, the tarball is re-read from the work folder and its SHA-256 is compared with the hash taken at verification. Fan Control is never staged from the bundle. If GitHub cannot be reached or rate-limits the request, or the release is missing, unsigned, fails verification, fails the hash check, is older than the installed `.version` (anti-rollback) or does not unpack, Fan Control is skipped with the log line `Fan Control was not installed: could not reach or verify the signed release. Run Install / Update again when online.` (after the specific reason where one is logged), and Framey is still installed. Then delete each staged `tests/` folder, upload the rest to `.new` folders (`scp -r`), and swap in with `.next` rollback. Then write `framey.service` (`kService`; its `ExecStartPre` restores from `framey.next`, then `framey.bak`, then `framey.bak.prev`, and only when `framey.py` is missing or does not parse), restart it, and wait up to 30 seconds (15 checks, 2 seconds apart) for `is-active`, otherwise `roll()` attempts to restore the old trees. Its marker does not verify the filesystem postconditions, so setup failures report the installed state as unconfirmed. Then, after a confirmed swap, `promote_backups_script` moves `.next` to `.bak` and the previous `.bak` to `.bak.prev`. Then the Fan Control root step, if ticked (below). Then re-install plugins that have a `.source` file, restart SteamVR (below), check that Framey is active, and delete `framey.bak` and `framey.bak.prev`, plus `frame-fan.bak` and `frame-fan.bak.prev` when Fan Control was updated. Only a failed Framey check keeps them; a port 8080 check only logs a warning.
- **Fan Control root step**: the verified tarball is uploaded to `~/frame-fan-root.tgz`. One `sudo -S -p ''` shell copies it to a root-owned `/var/tmp/ff.*` folder, checks its SHA-256 (computed over the verified bytes), unpacks it and runs that copy's `install-root.sh --signer-fp <fingerprint>`. The uploaded file is removed afterwards. The password goes on stdin only. `kSignerFp` is the publisher's release-signing fingerprint; the script refuses to install `/etc/frame-fan/allowed_signers` unless the shipped key matches it, so a new signing key needs a new app release. On exit code 0, the log says the protected updater and its trust key were installed. On a wrapper, script or transport failure, `rollback_script("frame-fan", ".bak")` attempts to restore the previous Fan Control folder (or remove it on a first install, with its dangling `plugins/fan` link). The app reports system state and folder restoration as unconfirmed because neither a generic failure nor the rollback marker verifies those postconditions; Framey stays updated.
- **SteamVR restart** (`restart_vr`): if `steamvr.service` is active, `systemctl --user try-restart steamvr.service`, a 3 s wait and an `is-active` check. The log says "SteamVR was restarted" or "SteamVR was not running, so it was left stopped". Nothing is started. There is no separate button; Install / Update and Add plugin call it.
- **Fetching**: each repository decides separately. Framey's bundled copy is used only if the headset has no Framey or its `.version` equals the bundle's commit; otherwise `stage()` stops and changes nothing. Fan Control has no bundled copy: when its signed release cannot be fetched or verified, the installed Fan Control is left unchanged and Framey still updates. The archive limits (500 entries, 8 levels, 20 MB) apply to the Framey tree too.
- **Check** (`do_check`): reads `.version` on the headset line by line (so a release tag is read whole). Framey is compared with the latest commit of `main`. Fan Control is compared with `tag_name` of `releases/latest` (`latest_tag()`, the same call `fetch_fan()` makes): a leading `v` is stripped, dotted numbers are compared by `compare_versions()`, and a prerelease sorts before the same release. When the stored Fan Control value is not a version (for example a 40-hex commit id), Check uses the `version` in `frame-fan/plugin.json`; only if that is not a version either does it give "installed version unknown: update available". A failed call gives "could not check (reason)"; curl exit 22 is reported as a possible rate limit. Like every job, Check authorizes first, so on first use it creates the key.
- **Remove** (`do_remove`): derive and validate the public key > check Fan Control's root files > if present, root uninstall (the bundled `uninstall-root.sh` is uploaded to `~/frame-fan-uninstall.sh` and run from a root-owned, SHA-256-checked copy; the existing privileged wrapper first invalidates `/run/frame-fan.stamp`, independently of the bundled script, and stops before invoking it if invalidation fails; the check afterwards requires stock fan control) > remove Framey files, settings, service, `framey.broken`, `framey.next`, `framey.bak`, `framey.bak.prev`, `frame-fan*` folders and `~/.config/framey`, `~/.config/frame-fan` > remove this app's key by exact match from `authorized_keys` > delete the local key files, this headset's `known_hosts` entry (checked with `ssh-keygen -F`) and, if no other `key-*` file is left, the whole data folder. Every local deletion is checked; leftovers are listed and the status reads "Removed, with leftovers on this computer." Remove does not restart SteamVR.
- **Add plugin** (`install_plugin(src, true)`): stage the package, check it, upload to `.stage-<id>`, swap with `.old-<id>` kept until the swap succeeds, and clear the plugin from `disabled` in `settings.json`. Then restart `framey.service` and SteamVR (if running). Plugins from a link get a `.source` file so Install / Update can refresh them; the refresh accepts only supported `github.com` sources. Other HTTPS archive installs require an explicit reinstall.
- **Authorization failures**: `Permission denied` from `ssh true` means the key is not accepted yet (ask for the password if empty); any other failure is reported as unreachable, with a hint when the host key changed.

## Plugin packages

A package is a `.zip` or `.tar.gz` with `plugin.json` at its top level or inside one top folder, plus `main.js` and/or `backend.py`. `plugin.json` is checked with a strict JSON parser in `core.cpp` (no trailing commas, no `NaN`, no bad escapes, no raw control characters, depth at most 32, nothing after the object; it does not check UTF-8) and must be an object. Duplicate top-level keys are rejected. The `id` is lowercase letters, digits, `-` and `_`, up to 32 characters, not starting with `-` and not `_core`. `name`, `version` and `short` must be strings if present.

Before anything is unpacked, the listing is read (`tar -tf` / `-tvf`, or `unzip -Z`) and rejected for absolute paths, `..`, backslashes, colons or control characters, names over 512 characters, links or special files, duplicate names, more than 500 entries, more than 8 levels, or more than 20 MB declared. A `__MACOSX` folder is ignored. A plugin whose id belongs to a symlinked plugin on the headset (for example Fan Control) is refused. The source is a local file (up to 20 MB), a `github.com` link (a repository link downloads the default branch, a `/tree/<branch>` link that branch; archive, release and `.zip`/`.tar.gz` links are used as they are), or another https link ending in `.zip` or `.tar.gz`.

## Known gaps

- `save_theme` ignores filesystem errors, so a read-only data folder silently keeps the old theme.
- The debug-port check runs once, a few seconds after the restart, and does not confirm the overlay.
