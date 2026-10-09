<p align="center"><img src="assets/icon.svg" width="128" height="128" alt="Framey icon"></p>

# Framey App

> **Framey is an AI-made project, developed by ChaosFox using AI coding tools.**
>
> **Framey was inspired by [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) and its contributors’ work making Steam Deck customization accessible through plugins. We gratefully acknowledge that inspiration. Framey is an independent project for Steam Frame, with no claimed affiliation or endorsement.**

The Framey App is a small, portable desktop app that installs, updates and removes [Framey](https://github.com/chaosfox26/framey) and, optionally, [Fan Control](https://github.com/chaosfox26/frame-fan) on a Steam Frame. It connects to the headset over SSH so everything is ready before you put the headset back on. It is the officially recommended and supported way to install Framey; installing by hand is possible but unsupported. Unofficial and independent of Valve.

## Origins and purpose

Framey grew from a wish to make the Steam Frame easier to customize through a lightweight, VR-first plugin interface, starting with convenient fan controls. The Framey App exists so that setting it up does not mean typing commands on the headset.

## The Framey projects

| Project | What it is |
|---|---|
| [framey](https://github.com/chaosfox26/framey) | The plugin loader that runs on the headset. |
| [frame-fan](https://github.com/chaosfox26/frame-fan) | Fan Control, a plugin with privileged cooling components. |
| [framey-app](https://github.com/chaosfox26/framey-app) (this repo) | The desktop app that installs and manages the other two. |

## Download

Get the build for your system from the [Releases page](https://github.com/chaosfox26/framey-app/releases) (current release: Framey App 1.0, tag `v1.0.0`):

| File | System |
|---|---|
| `FrameyApp-windows.exe` | Windows (x64) |
| `FrameyApp-linux-x86_64.tar.gz` | Linux x86_64 (needs the GTK 3 runtime, `libgtk-3`) |
| `FrameyApp-linux-arm64.tar.gz` | Linux arm64 (needs the GTK 3 runtime, `libgtk-3`) |
| `FrameyApp-macos.tar.gz` | macOS, one universal build for Apple silicon and Intel |

The Linux and macOS archives contain `FrameyApp` and `LICENSE`. The release also has `SHA256SUMS.txt` (checksums of the four builds), `PROVENANCE.txt` (the commit of this app and of the Framey and Fan Control copies bundled inside it) and `LICENSE.txt`. The builds are not code-signed, so Windows or macOS may warn about or block them until you allow them.

## Quick start

1. On the headset: Steam Settings > System > Enable Developer Mode.
2. Developer (left menu) > scroll to the bottom > Set User Password.
3. Keep the headset awake and on the same network as your computer.
4. Run the app (on Linux and macOS unpack the archive first). Enter the headset address (default `frame`, or its IP address) and the password from step 2.
5. Click **Install / Update**. Tick **Also install Fan Control** first if you want it. The app asks first, because if SteamVR is running it is restarted at the end, which ends the running VR session.
6. Put the headset on and tap the Framey icon in the bottom bar.

## What it does

It is one C++26 program with a native window on each system: Win32 on Windows, Cocoa on macOS and GTK 3 on Linux. There is no browser, no local web server and no open port. It has four retro themes (Magenta, Blue, Black in green phosphor, and a light White one) and uses your system's own `ssh`, `scp`, `ssh-keygen`, `curl` and `tar`, which must be on the `PATH`; opening a `.zip` plugin also needs `bsdtar`, `unzip` or a `tar` that is bsdtar. It connects to the headset as the user `steamos`.

- **Install / Update:** installs Framey and, if you tick **Also install Fan Control**, Fan Control. For each it looks up the latest `main` commit on GitHub and downloads that exact commit over https. If GitHub is unreachable it uses the copy bundled in the app (the commit listed in `PROVENANCE.txt`), but only when nothing of that kind is installed yet or the installed commit is the same; otherwise it stops without changing anything, because the bundled copy could be older. When Fan Control is ticked, the app first checks that the headset accepts the password, before anything is changed. The new version is uploaded next to the old one and swapped in, Framey must be running again two seconds later, and the previous version is restored if that fails. The old copies (`framey.bak`, `frame-fan.bak`) are deleted only after Fan Control's system step and the final checks have passed. If Fan Control's system step fails, Fan Control is put back as it was and Framey stays updated. Installed plugins are kept. Fan Control is only updated when the box is ticked, and its system step needs the headset password every time. Plugins you added from a link are refreshed too. SteamVR is then restarted if it is running.
- **Check for updates:** compares the version on the headset with the latest commit on GitHub for Framey and Fan Control. It changes nothing on the headset, apart from the one-time key authorization on first use: if the app has no working key for that headset yet and you entered the password, it creates its SSH key on this computer and adds it to `~/.ssh/authorized_keys` on the headset (see Connection setup).
- **Add plugin:** installs a Framey plugin from a `.zip` (or `.tar.gz`) file you browse to, or from a GitHub link or an https link to such a file. It then restarts Framey, and SteamVR if it is running, so a running VR session ends. Framey must already be installed. Plugins added from a link are updated again each time you click **Install / Update**.
- **Remove:** removes Framey, its plugins, Fan Control (which restores stock fan control), their saved settings, and this app's own SSH key and data. If something on this computer cannot be deleted, it says so, lists it and reports "Removed, with leftovers" instead of claiming everything is clean.

SteamVR is restarted with `systemctl --user try-restart steamvr.service` after a successful Install / Update and after a successful Add plugin, and only if it is running. The log says whether it was restarted or was not running (an idle SteamVR is not started). Install / Update and Add plugin ask for confirmation first and mention this. If the install stops early (for example a wrong password or a failed Fan Control step), SteamVR is not restarted. There is no setting to turn the restart off.

The window cannot be closed, and on macOS the app cannot be quit, while a job is running. Every step has a time limit so that a dropped Wi-Fi link cannot hang it forever: `ssh` and `scp` send keepalives every 10 seconds and give up after three missed ones, each headset step is stopped after 120 seconds (uploads after 600, downloads after 150), and the log says which step timed out. The license notice (GPL-2.0 only, with the source link) is shown in the app's log when it starts.

## Connection setup and permission prompts

- The first time on a headset you enter the password you set in Developer settings. The app uses it once to add its own SSH key to the headset, so later jobs need no password. If the headset cannot be reached, or its saved host key changed (for example after reinstalling SteamOS), the log says that instead of asking for the password; to trust the new host key, delete the `known_hosts` file in `FrameyApp-data`. The password is also used for the Fan Control root step through `sudo`. It is only sent to your headset and is never saved; the field is cleared when a job ends.
- The key is an ed25519 key created by the app, with a separate key for each headset address (`key-<address>` in the data folder). Adding it puts one line, with a comment like `framey-app-` plus 12 random characters that is unique to that key, into `~/.ssh/authorized_keys` on the headset, on its own line even if the file did not end with a newline. If the `.pub` file next to the key is missing or unreadable, the app derives the public key from the private key. The headset's host key is trusted the first time it is seen.
- The app asks you to confirm before Install / Update, before a plugin is installed ("only install plugins you trust") and before **Remove**. The Install / Update and Add plugin prompts mention the SteamVR restart.
- Your system may warn about an unsigned download. See [Download](#download).

## Portable data

Nothing is installed, and the app writes nothing to the registry or your home folder. It keeps its keys, known-hosts file, last address and theme in a `FrameyApp-data` folder next to the app, so run it from a folder you can write to. It uses a temporary work folder inside that data folder during each job and deletes it afterwards. Deleting the app and that folder leaves nothing behind.

## Updating and removal

- Update with **Install / Update**. Framey is restarted so its panel picks up changes, then SteamVR is restarted if it is running.
- **Remove** works in a fixed order. It first checks whether Fan Control's system files are on the headset. If they are, it needs the headset password and removes them first. If that fails, or stock fan control is not running again afterwards, nothing else is deleted and Remove reports a failure so you can try again. Then it removes Framey, its plugins and settings from the headset, then removes only the exact key this app created for that headset from `authorized_keys`, not any other key, and deletes that key on your computer. The key is read (or derived from the private key) before anything is deleted, and Remove stops early if that is impossible. If a local file cannot be deleted, the result says "Removed, with leftovers" and lists it. The data folder is deleted last, and only if no key for another headset remains in it. Remove does not restart SteamVR.

## Platforms and verification

| Platform | Build | Runtime evidence |
|---|---|---|
| Windows (x64) | Built locally and by the release workflow. | The native window was launched and inspected on Windows. Version 1.0.1 was run by the author against a Steam Frame (SteamOS 0.4.5): first-time password authorization, Install with Fan Control through the app, and the SteamVR restart worked, and Framey's panel and Fan Control then opened and worked in VR. |
| Linux x86_64, Linux arm64 | Built by the release workflow; it compiles. | Not run by the author. The native window has not been run on Linux. |
| macOS (universal: arm64 and Intel) | Built by the release workflow; it compiles. | Not run by the author. |

**Currently unverified:**

- The Linux and macOS windows and runtime.
- The new remove sequence, updating an already installed headset (the staged swap of Framey, Fan Control and plugins (with the health check, the Fan Control undo and the kept `.bak` folders), the time limits and keepalives, and the archive checks, none of which have been run against a headset yet.

AI authorship and a successful build are not evidence that something works.

## Security notes

- The app has no network listener: it is a native window and opens no local port.
- Framey and Fan Control are downloaded over https from the `chaosfox26` repositories, pinned to the one commit that was looked up. The app then runs Fan Control's install script as root on the headset, so use it only with repositories you trust. Plugins run code on your headset, and plugins added from a link are not pinned to a commit.
- Plugin and suite packages are listed and checked before anything is extracted. Absolute paths, `..`, links, special files, duplicate names and oversized or over-deep packages are rejected (limits: 500 entries, 8 levels, 20 MB). The sizes are read from the listing by matching each entry's name, not by column position. Because a `.zip` can understate its sizes, extraction is also watched: if the files on disk pass 20 MB, the extractor is stopped and the package is refused. Extraction has a 60 second limit, and the extracted tree is checked again afterwards. A plugin's `plugin.json` must be strictly valid JSON: an object with a valid `id`, and `name`, `version` and `short` (if present) must be strings, as Framey itself requires.
- Plugin installs and updates are staged next to the live copy and swapped in with a rollback, so a failed update keeps the working copy.
- Fan Control's install and uninstall scripts are run as root from `/home/steamos/frame-fan/` on the headset, a folder owned by the `steamos` user, not from a root-owned location. Any program running as `steamos` on the headset, such as a plugin, could change those scripts before the app runs them with your password. Install Fan Control only on a headset where you trust every plugin. The app does not yet copy the scripts into a root-owned folder first.

## Roadmap (not implemented)

- Verify the unverified items above.
- A temperature and fan readout shown over flat-screen games is a possible future Framey feature. It does not exist yet and is not part of this app.

## Release history

The earlier releases 1.0.0, 1.0.1, 1.0.2 and 1.0.3 were deleted and replaced by the current release, 1.0 (tag `v1.0.0`). They were removed, not just superseded, and their tags were removed too, so that nobody downloads a build with the known defects below. The source history is still in this repository.

- Those builds served a local web page and opened it in the default browser. They were not a self-contained native window.
- Version 1.0.1 and earlier would accept a crafted plugin package containing a symlink and upload a local file from the user's computer to the headset. Automated review found this, and it was fixed in 1.0.2.
- An independent audit of 1.0.3 found further defects, all addressed in 1.0:
  - Remove could delete files and credentials even when the privileged Fan Control removal failed.
  - Archives were checked only after extraction.
  - Plugin and suite updates could lose a working copy.
  - Every copy of the app shared one SSH key comment.
  - The local server could be blocked by an incomplete request.
  - Downloads were not pinned to one commit.
  - The macOS build was arm64-only.
  - Release packages had no visible license.
- The version numbering was restarted at 1.0 for the native rewrite.

## Build

See [docs/development.md](docs/development.md).

## Inspiration and acknowledgments

Framey was inspired by [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) and its contributors’ work making Steam Deck customization accessible through plugins. Thank you to the Decky Loader maintainers and contributors. Framey aims to bring that kind of convenience to the Steam Frame while respecting the work that inspired it.

The Framey App contains no Decky Loader code, assets or documentation, and has no relationship with the Decky Loader project. The project as a whole is not a clean-room implementation: Decky Loader's public source was read for reference while the loader was designed. A comparison of this repository against Decky Loader's source found no identical code lines. Framey is unofficial and independent of Valve.

## License

The Framey App is licensed under the GNU General Public License, version 2 only. The full text is in [LICENSE](LICENSE), and GitHub identifies it as GPL-2.0. The source files do not carry their own license notices.
