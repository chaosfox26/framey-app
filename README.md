# Framey App

A small Windows installer for [Framey](https://github.com/chaosfox26/framey) and, optionally, [Fan Control](https://github.com/chaosfox26/frame-fan) on the Steam Frame. It connects to the headset over SSH, sets everything up, and keeps it updated from GitHub, so it is all ready before you put the headset back on. Unofficial. Not affiliated with Valve.

One exe, about 770 KB, written in C++26. It uses Windows' own `ssh`, `scp`, `curl` and `tar`, and has four dark retro themes (Magenta, Blue, Black and a light White one).

## Use

On the headset:

1. Steam Settings > System > Enable Developer Mode.
2. Developer (left menu) > scroll to the bottom > Set User Password.
3. Keep the headset awake and on the same network as your PC.

Then download `FrameyApp.exe` from [Releases](../../releases), run it, enter the headset address (usually `frame`) and the password, and click **Install / Update**. Tick **Also install Fan Control** to add it as well.

- **Check for updates** compares what is on the headset with the latest version on GitHub.
- **Remove** takes Framey and Fan Control off the headset and restores stock fan control.
- The first time, the password authorizes an SSH key that the app creates for itself, so later updates need no password. The password is also used for the Fan Control root install through `sudo`. It is never saved.

Windows may show a SmartScreen warning because the exe is not signed.

## What it changes

- On your PC: `%LOCALAPPDATA%\Framey` holds the app's SSH key, known hosts, the last address and the theme.
- On the headset: your user's `~/.ssh/authorized_keys` gets one added line, `~/framey` and (optionally) `~/frame-fan` are installed, and a user service `framey.service` is enabled. Fan Control adds root-owned files in `/etc/frame-fan` and a systemd drop-in for `deckard-fan-control`.
- It downloads the latest `main` of both repos from GitHub and runs the Fan Control install script as root, so only use it with repos you trust. If GitHub is unreachable it installs the copy bundled inside the exe.

## Build

Needs clang, CMake, Ninja, the Windows SDK, Python with Pillow and Chrome (for the icon). Clone this repo next to clones of `framey` and `frame-fan` (folders `Framey` and `frame-fan`), then run `tools\build.ps1`. The result is `build\FrameyApp.exe`.

## License

GPL-2.0, see `LICENSE`.
