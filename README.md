<p align="center"><img src="assets/icon.svg" width="128" height="128" alt="Framey icon"></p>

# Framey App

> **Framey is an AI-made project, developed by ChaosFox using AI coding tools.**
>
> **Framey was inspired by [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) and its contributors’ work making Steam Deck customization accessible through plugins. We gratefully acknowledge that inspiration. Framey is an independent project for Steam Frame, with no claimed affiliation or endorsement.**

The Framey App is a small, portable desktop app that installs, updates and removes [Framey](https://github.com/chaosfox26/framey) and, optionally, [Fan Control](https://github.com/chaosfox26/frame-fan) on a Steam Frame. It connects to the headset over SSH so everything is ready before you put the headset back on. Unofficial and independent of Valve.

## Origins and purpose

Framey grew from a wish to make the Steam Frame easier to customize through a lightweight, VR-first plugin interface, starting with convenient fan controls. The Framey App exists so that setting it up does not mean typing commands on the headset.

## The Framey projects

| Project | What it is |
|---|---|
| [framey](https://github.com/chaosfox26/framey) | The plugin loader that runs on the headset. |
| [frame-fan](https://github.com/chaosfox26/frame-fan) | Fan Control, a plugin with privileged cooling components. |
| [framey-app](https://github.com/chaosfox26/framey-app) (this repo) | The desktop app that installs and manages the other two. |

## What it does

It is one C++26 program with a native window on each system (Win32 on Windows, Cocoa on macOS, GTK 3 on Linux) and no browser or web server. It has four retro themes (Magenta, Blue, Black and a light White one), and uses your system's own `ssh`, `scp`, `ssh-keygen`, `curl` and `tar`.

- **Install / Update:** installs Framey and, if you tick **Also install Fan Control**, Fan Control. It takes the latest `main` of both repositories from GitHub and falls back to a copy bundled inside the app if GitHub is unreachable.
- **Check for updates:** compares what is on the headset with the latest commit on GitHub.
- **Add plugin:** installs a Framey plugin from a `.zip` file you browse to or a GitHub link. It then restarts Framey so the plugin appears in the headset's panel on its own. Plugins added from a link are updated again each time you click **Install / Update**.
- **Remove:** removes Framey, its plugins and Fan Control (which restores stock fan control), and the app's own SSH key and data.

## Use

On the headset:

1. Steam Settings > System > Enable Developer Mode.
2. Developer (left menu) > scroll to the bottom > Set User Password.
3. Keep the headset awake and on the same network as your computer.

Then download the build for your system from [Releases](https://github.com/chaosfox26/framey-app/releases). On Linux and macOS unpack it and run `FrameyApp`. On Windows run `FrameyApp-windows.exe`. Enter the headset address (usually `frame`) and click **Install / Update**. Close the window to quit; it will not close while a job is running. On Linux the GTK 3 runtime library (`libgtk-3`) must be installed.

## Connection setup and permission prompts

- The first time, you enter the headset password you set in Developer settings. The app uses it once to add its own SSH key to the headset, so later updates need no password. The password is also used for the Fan Control root step through `sudo`. It is only sent to your headset and is never saved.
- The key is an ed25519 key created by the app. Adding it puts one line, with a comment like `framey-app-` plus 12 random characters that is unique to this copy of the app, into `~/.ssh/authorized_keys` on the headset. The headset's host key is trusted the first time it is seen.
- The app asks you to confirm before a plugin is installed ("only install plugins you trust") and before **Remove**.
- Your system may warn about an unsigned download. On macOS right-click the app and choose Open the first time.

## Portable data

Nothing is installed, and nothing is written to the registry or your home folder. The app keeps its key and settings in a `FrameyApp-data` folder next to itself and uses a temporary folder during each job, which it deletes afterwards. Deleting the app and that folder leaves nothing behind.

## Updating and removal

- Update with **Install / Update**. Framey is restarted so its panel picks up changes.
- **Remove** takes Framey, its plugins and Fan Control off the headset. Fan Control goes first and needs the headset password: if its system files cannot be removed, nothing else is deleted and Remove reports a failure so you can try again. Then it removes only the exact key this app created from `authorized_keys`, not any other key, and last deletes the app's data folder.

## Platforms and verification

| Platform | Build | Runtime evidence |
|---|---|---|
| Windows (x64) | Built locally and by the release workflow. | The native window was launched and inspected on Windows. The install, update, plugin and remove logic is unchanged from earlier builds that ran against a Steam Frame, apart from the fixes listed below, which have not yet been run against a headset. |
| Linux x86_64, Linux arm64 | Built by the release workflow. | Not run by the author. The native window has not been run on Linux. |
| macOS (arm64 and Intel) | Built by the release workflow. | Not run by the author. |

**Currently unverified:**

- The Linux and macOS windows and runtime.
- First-time password authorization in the current app. It worked in an earlier Windows-only version.
- The Fan Control root-install step in the current app. It worked in that earlier version.
- The safer remove sequence, the staged update and swap of Framey, Fan Control and plugins, and the archive checks, none of which have been run against a headset yet.

AI authorship and a successful build are not evidence that something works.

## Security notes

- The app has no network listener: it is a native window and opens no local port.
- The app downloads the latest `main` of the repositories above and runs Fan Control's install script as root on the headset, so use it only with repositories you trust. Plugins run code on your headset.
- Plugin packages are listed and checked before anything is extracted. Absolute paths, `..`, links, special files, duplicates and oversized or over-deep packages are rejected, extraction has a time limit, and the extracted tree is checked again afterwards.

## Roadmap (not implemented)

- Verify the unverified items above.
- A temperature and fan readout shown over flat-screen games is a possible future Framey feature. It does not exist yet and is not part of this app.

## Build

See [docs/development.md](docs/development.md).

## Inspiration and acknowledgments

Framey was inspired by [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) and its contributors’ work making Steam Deck customization accessible through plugins. Thank you to the Decky Loader maintainers and contributors. Framey aims to bring that kind of convenience to the Steam Frame while respecting the work that inspired it.

The Framey App contains no Decky Loader code, assets or documentation, and has no relationship with the Decky Loader project. The project as a whole is not a clean-room implementation: Decky Loader's public source was read for reference while the loader was designed. A comparison of this repository against Decky Loader's source found no identical code lines. Framey is unofficial and independent of Valve.

## License

The Framey App is licensed under the GNU General Public License, version 2 only. The full text is in [LICENSE](LICENSE), and GitHub identifies it as GPL-2.0. The source files do not carry their own license notices.
