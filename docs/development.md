# Framey App development notes

For what the app does, see the [README](../README.md). Identifiers keep the spelling `framey`.

## Layout

| Path | Purpose |
|---|---|
| `src/core.cpp`, `src/core.h` | Process spawning, install, update, plugin and remove jobs, shared by the native `ui_*` files. |
| `src/ui_win.cpp`, `src/ui_mac.mm`, `src/ui_gtk.cpp` | The native window for Windows, macOS and Linux. |
| `tools/pack.py` | Builds `generated/payload.inc` (a gzip tar of the `framey` and `frame-fan` repositories plus their commit ids). |
| `tools/build.ps1` | Windows build (pack, then CMake with clang and Ninja). |
| `tools/make_icon.py` | Rebuilds `assets/framey.ico` from the Framey icon. |
| `app.rc`, `app.manifest`, `assets/` | Windows icon and manifest. |
| `.github/workflows/build.yml` | Builds Windows, Linux x86_64, Linux arm64 and macOS, and attaches them to a release. |

## Build

Needs a C++26 compiler (clang or gcc), CMake, Python and git. `tools/pack.py` expects clones of `framey` and `frame-fan` next to this repository in folders named `Framey` and `frame-fan`.

```
python tools/pack.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows `tools/build.ps1` does this with clang and Ninja. Releases are built by the workflow: create the release, then run it with the release tag as its input.

The Windows manifest is embedded only through `app.rc`, and the linker's own manifest generation is turned off, because merging manifests in the linker produced an invalid manifest on the release runner.

## How it runs

`core.cpp` holds the jobs and exposes `snapshot`, `start_job`, `saved` and `save_theme` (see `core.h`). A native window calls them: `src/ui_win.cpp` (Win32), `src/ui_mac.mm` (Cocoa) or `src/ui_gtk.cpp` (GTK 3, built with `libgtk-3-dev` and `pkg-config`). The window starts jobs on a worker thread and polls the log twice a second. Closing is blocked while a job runs.

External tools are started directly without a shell (`posix_spawn` or `CreateProcess`). The password reaches `ssh` through `SSH_ASKPASS`: the app starts itself with `FRAMEY_ASKPASS` set, and in that mode it prints the password or `yes` and exits. Fan Control's root step pipes the password to `sudo -S`.

The app keeps its data in `FrameyApp-data` beside the executable and uses a per-run temporary folder that is removed after each job.

## Plugin packages

A package is a `.zip` or `.tar.gz` with `plugin.json` at its top level or inside one top folder, plus `main.js` and/or `backend.py`. `plugin.json` must be a JSON object with a valid `id` (there is no fallback name). Before anything is unpacked the package listing is read and rejected for absolute or `..` paths, links, special files, duplicate names, more than 500 entries, more than 8 levels, or more than 20 MB declared; each attempt unpacks into a fresh folder under a 60 second limit, and the result is checked again for links and size. Downloads are https only, and the suite is fetched by the commit hash that was looked up. Updates upload to `.new` folders next to the live ones and are swapped in with a rollback. See the [Framey plugin guide](https://github.com/chaosfox26/framey/blob/main/docs/plugins.md).
