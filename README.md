<p align="center"><img src="assets/icon.svg" width="128" height="128" alt="Framy icon"></p>

# Framy App

> **Framy is an AI-made project, developed by ChaosFox using AI coding tools.**
>
> **Framy was inspired by [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) and its contributors’ work making Steam Deck customization accessible through plugins. We gratefully acknowledge that inspiration. Framy is an independent project for Steam Frame, with no claimed affiliation or endorsement.**

The Framy App is a small, portable desktop app that installs, updates and removes [Framy](https://github.com/chaosfox26/framey) and, optionally, [Fan Control](https://github.com/chaosfox26/frame-fan) on a Steam Frame. It connects to the headset over SSH so everything is ready before you put the headset back on. Unofficial and independent of Valve.

Naming: the public name is Framy. This repository keeps the spelling `framey-app` in its name, files and release assets (`FrameyApp`). Those identifiers are unchanged.

## Origins and purpose

Framy grew from a wish to make the Steam Frame easier to customize through a lightweight, VR-first plugin interface, starting with convenient fan controls. The Framy App exists so that setting it up does not mean typing commands on the headset.

## The Framy projects

| Project | What it is |
|---|---|
| [framey](https://github.com/chaosfox26/framey) | The plugin loader that runs on the headset. |
| [frame-fan](https://github.com/chaosfox26/frame-fan) | Fan Control, a plugin with privileged cooling components. |
| [framey-app](https://github.com/chaosfox26/framey-app) (this repo) | The desktop app that installs and manages the other two. |

## What it does

It is one C++26 program with no libraries to install. It opens a local page in your browser, with four dark retro themes (Magenta, Blue, Black and a light White one), and uses your system's own `ssh`, `scp`, `ssh-keygen`, `curl` and `tar`.

- **Install / Update:** installs Framy and, if you tick **Also install Fan Control**, Fan Control. It takes the latest `main` of both repositories from GitHub and falls back to a copy bundled inside the app if GitHub is unreachable.
- **Check for updates:** compares what is on the headset with the latest commit on GitHub.
- **Add plugin:** installs a Framy plugin from an uploaded `.zip` or a GitHub link. It then restarts Framy so the plugin appears in the headset's panel on its own. Plugins added from a link are updated again each time you click **Install / Update**.
- **Remove:** removes Framy, its plugins and Fan Control (which restores stock fan control), and the app's own SSH key and data.

## Use

On the headset:

1. Steam Settings > System > Enable Developer Mode.
2. Developer (left menu) > scroll to the bottom > Set User Password.
3. Keep the headset awake and on the same network as your computer.

Then download the build for your system from [Releases](https://github.com/chaosfox26/framey-app/releases). On Linux and macOS unpack it and run `./FrameyApp` in a terminal. On Windows run `FrameyApp-windows.exe`. Enter the headset address (usually `frame`) and click **Install / Update**. Close the app's window or terminal to quit.

## Connection setup and permission prompts

- The first time, you enter the headset password you set in Developer settings. The app uses it once to add its own SSH key to the headset, so later updates need no password. The password is also used for the Fan Control root step through `sudo`. It is only sent to your headset and is never saved.
- The key is an ed25519 key created by the app. Adding it puts one line, ending in `framey-app`, into `~/.ssh/authorized_keys` on the headset. The headset's host key is trusted the first time it is seen.
- The browser asks you to confirm before a plugin is installed ("only install plugins you trust") and before **Remove**.
- Your system may warn about an unsigned download. On macOS right-click the app and choose Open the first time.

## Portable data

Nothing is installed, and nothing is written to the registry or your home folder. The app keeps its key and settings in a `FrameyApp-data` folder next to itself and uses a temporary folder during each job, which it deletes afterwards. Deleting the app and that folder leaves nothing behind.

## Updating and removal

- Update with **Install / Update**. Framy is restarted so its panel picks up changes.
- **Remove** takes Framy, its plugins and Fan Control off the headset. It removes only the lines in `authorized_keys` that end with the app's own `framey-app` comment, not any other key, then deletes the app's data folder. Fan Control removal asks for the headset password.

## Platforms and verification

| Platform | Build | Runtime evidence |
|---|---|---|
| Windows (x64) | Built locally and by the release workflow. | Run by the author against a Steam Frame: check, install, offline install, plugin from a GitHub link and from a zip, automatic plugin update, remove. |
| Linux arm64 | Built by the release workflow and compiled locally. | Started and exercised on the Steam Frame itself: the web page, token and host checks, starting jobs and cleanup. It was not used to install onto a headset. |
| Linux x86_64 | Built by the release workflow. | Not run by the author. |
| macOS | Built by the release workflow. | Not run by the author. |

**Currently unverified:**

- macOS runtime.
- First-time password authorization in the current app. It worked in an earlier Windows-only version.
- The Fan Control root-install step in the current app. It worked in that earlier version.

AI authorship and a successful build are not evidence that something works.

## Security notes

- The local page listens only on `127.0.0.1`, needs a random token that is part of the link the app opens, and rejects requests with a different `Host` header.
- The app downloads the latest `main` of the repositories above and runs Fan Control's install script as root on the headset, so use it only with repositories you trust. Plugins run code on your headset.
- Rejecting plugin packages that contain symlinks or other special files, and skipping symlinks when staging, is in the source on `main`. It is not in the 1.0.1 release builds.

## Roadmap (not implemented)

- Verify the unverified items above.
- A temperature and fan readout shown over flat-screen games is a possible future Framy feature. It does not exist yet and is not part of this app.

## Build

See [docs/development.md](docs/development.md).

## Inspiration and acknowledgments

Framy was inspired by [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) and its contributors’ work making Steam Deck customization accessible through plugins. Thank you to the Decky Loader maintainers and contributors. Framy aims to bring that kind of convenience to the Steam Frame while respecting the work that inspired it.

The Framy App contains no Decky Loader code, assets or documentation, and has no relationship with the Decky Loader project. The project as a whole is not a clean-room implementation: Decky Loader's public source was read for reference while the loader was designed. A comparison of this repository against Decky Loader's source found no identical code lines. Framy is unofficial and independent of Valve.

## License

The Framy App is licensed under the GNU General Public License, version 2. The full text is in [LICENSE](LICENSE), and GitHub identifies it as GPL-2.0. The source files do not carry their own license notices.
