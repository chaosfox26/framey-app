# Framey App development notes

For what the app does, see the [README](../README.md). Identifiers keep the spelling `framey`.

## Layout

| Path | Purpose |
|---|---|
| `src/main.cpp` | The whole program: process spawning, local web server, install, update, plugin and remove jobs. |
| `src/ui.html` | The page served at `/`, with the four themes. |
| `tools/pack.py` | Builds `generated/payload.inc` (a gzip tar of the `framey` and `frame-fan` repositories plus their commit ids) and `generated/ui.inc` (the page). |
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

The program starts a small HTTP server on `127.0.0.1` (first free port from 47321), prints a link containing a random token, and opens it in the browser (`--no-browser` skips that). Every request must carry the token and a local `Host` header. Jobs run on a worker thread and write to a log the page polls.

External tools are started directly without a shell (`posix_spawn` or `CreateProcess`). The password reaches `ssh` through `SSH_ASKPASS`: the app starts itself with `FRAMEY_ASKPASS` set, and in that mode it prints the password or `yes` and exits. Fan Control's root step pipes the password to `sudo -S`.

The app keeps its data in `FrameyApp-data` beside the executable and uses a per-run temporary folder that is removed after each job.

## Plugin packages

A package is a `.zip` or `.tar.gz` with `plugin.json` at its top level or inside one top folder, plus `main.js` and/or `backend.py`. The id comes from `plugin.json` and falls back to a name derived from the file or repository. Extraction is rejected if the result contains anything other than regular files and directories. See the [Framey plugin guide](https://github.com/chaosfox26/framey/blob/main/docs/plugins.md).
