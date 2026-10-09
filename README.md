# Framey App

An installer for [Framey](https://github.com/chaosfox26/framey) and, optionally, [Fan Control](https://github.com/chaosfox26/frame-fan) on the Steam Frame. It runs on Windows, Linux and macOS, connects to the headset over SSH, sets everything up, and keeps it updated from GitHub, so it is all ready before you put the headset back on. Unofficial. Not affiliated with Valve.

One small program, written in C++26, with no installer and no libraries to install. It opens a local page in your browser (four dark retro themes: Magenta, Blue, Black and a light White one) and uses your system's own `ssh`, `scp`, `ssh-keygen`, `curl` and `tar`.

## Use

On the headset:

1. Steam Settings > System > Enable Developer Mode.
2. Developer (left menu) > scroll to the bottom > Set User Password.
3. Keep the headset awake and on the same network as your computer.

Then download the build for your system from [Releases](../../releases) and run it. On Linux and macOS unpack it and run `./FrameyApp` in a terminal. Enter the headset address (usually `frame`) and the password, and click **Install / Update**. Tick **Also install Fan Control** to add it as well.

- **Check for updates** compares what is on the headset with the latest version on GitHub.
- **Add plugin** installs a Framey plugin from a `.zip` file or a GitHub link. It appears in Framey's side menu on the headset by itself, with nothing more to do. Plugins added from a link are updated automatically every time you click **Install / Update**.
- **Remove** takes Framey, its plugins and Fan Control off the headset, restores stock fan control, removes the app's key from the headset, and deletes the app's own data folder.
- The first time, the password authorizes an SSH key that the app creates for itself, so later updates need no password. The password is also used for the Fan Control root install through `sudo`. It is never saved.

Close the app's window (or its terminal) to quit. macOS and Windows may warn about an unsigned download. On macOS right-click the app and choose Open the first time.

## Portable and tidy

Nothing is installed and nothing is written to the registry or your home folder. The app keeps its key and settings in a `FrameyApp-data` folder next to itself and uses a temporary folder while it works, which is deleted after every job. Delete the app and that folder and nothing is left. **Remove** does it for you.

## What it changes on the headset

- One added line in your user's `~/.ssh/authorized_keys`.
- `~/framey`, and optionally `~/frame-fan`, plus a user service `framey.service`.
- With Fan Control: root-owned files in `/etc/frame-fan` and a systemd drop-in for `deckard-fan-control`.

The app downloads the latest `main` of both repos from GitHub and runs the Fan Control install script as root, so only use it with repos you trust. If GitHub is unreachable it installs the copy bundled inside the app. Plugins run code on your headset, so only add ones you trust.

## Safety of the local page

The page is served only on `127.0.0.1`, needs a random token that is part of the link the app opens, and rejects requests with a different `Host` header, so other websites cannot drive it.

## Build

Needs a C++26 compiler (clang or gcc), CMake, Python, and git. Clone this repo next to clones of `framey` and `frame-fan` (folders `Framey` and `frame-fan`), run `python tools/pack.py`, then build with CMake. `tools/build.ps1` does both on Windows. Releases are built for all systems by the workflow in `.github/workflows`.

## License

GPL-2.0, see `LICENSE`.
