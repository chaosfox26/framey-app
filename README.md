<p align="center"><img src="assets/icon.svg" width="128" height="128" alt="Framey icon"></p>

# Framey App

> **Framey is an AI-made project, developed by ChaosFox using AI coding tools.**

Framey App is a native desktop installer for [Framey](https://github.com/chaosfox26/framey), the plugin loader for the Steam Frame, and optionally [Fan Control](https://github.com/chaosfox26/frame-fan). It connects to the headset over SSH and installs, updates, checks and removes them. It is written in C++26 and shows a native window (Win32, Cocoa or GTK 3). It is not a browser page: it opens no browser window, no local web server and no network port. Version 1.0.2.

Framey was inspired by [Decky Loader](https://github.com/SteamDeckHomebrew/decky-loader) and its contributors. Framey is unofficial and independent of Valve.

| Project | What it is |
|---|---|
| [framey](https://github.com/chaosfox26/framey) | The plugin loader that runs on the headset. |
| [frame-fan](https://github.com/chaosfox26/frame-fan) | Fan Control, a plugin with privileged cooling components. |
| [framey-app](https://github.com/chaosfox26/framey-app) (this repo) | The desktop app that installs and manages the other two. |

## Download

Release builds are produced by the Build workflow in this repository and attached to a GitHub release. Check the release's tag, `SHA256SUMS.txt` and `PROVENANCE.txt`.

The `experimental` branches contain source changes, not new release assets or a new update channel. Published installers contain the source recorded in their provenance. This app still fetches Framey `main` and the latest signed Fan Control release. The build workflow also pins the bundled Framey and Fan Control copies to their respective `main` commits, even when run from experimental App source; selecting that App ref does not select the other projects' experimental branches.

| File | System |
|---|---|
| `FrameyApp-windows.exe` | Windows (x64) |
| `FrameyApp-linux-x86_64.tar.gz` | Linux x86_64 (needs the GTK 3 runtime, `libgtk-3`) |
| `FrameyApp-linux-arm64.tar.gz` | Linux arm64 (needs the GTK 3 runtime, `libgtk-3`) |
| `FrameyApp-macos.tar.gz` | macOS, universal (arm64 and x86_64) |

The Linux and macOS archives contain `FrameyApp` and `LICENSE`. Releases also carry `SHA256SUMS.txt`, `PROVENANCE.txt` (the commits of this app and of the bundled Framey and Fan Control copies) and `LICENSE.txt`. The builds are not code-signed, so Windows or macOS may warn until you allow them.

## Quick start

1. On the headset: Steam Settings > System > Enable Developer Mode.
2. Developer (left menu) > scroll to the bottom > Set User Password.
3. Keep the headset awake and on the same network as your computer.
4. Run the app (on Linux and macOS, unpack the archive first). Enter the headset address (default `frame`, or its IP address) and the password from step 2.
5. Click **Install / Update**. Tick **Also install Fan Control** first if you want it. The app asks first, because a running SteamVR session is restarted at the end.
6. Put the headset on and tap the Framey icon in the bottom bar.

## What the buttons do

- **Install / Update** (asks first). The log numbers the steps:
  1. Connects and authorizes the app's key (see [Connection](#connection-and-keys)). If Fan Control is ticked, checks the password with `sudo -S` before anything changes.
  2. Gets the latest commit of `main` of Framey from GitHub over https. For Fan Control it gets the latest published release (`frame-fan.tar.gz` and `.sig`), never `main`, and checks the signature on this computer with `ssh-keygen -Y verify` against the release key built into the app. Fan Control is installed only from a signed release. If the release cannot be fetched (GitHub unreachable or rate-limited) or cannot be verified (unsigned, failed check, changed after verification, older than the installed one, or does not unpack), Fan Control is skipped with the notice `Fan Control was not installed: could not reach or verify the signed release. Run Install / Update again when online.` (the log also gives the specific reason), and Framey is still installed. An offline Framey-only update leaves the installed Fan Control alone.
  3. Uploads each tree (without `tests/`) to a `.new` folder and swaps it in. The outgoing tree is kept as `.next` until the swap is confirmed.
  4. Writes the `framey.service` unit, restarts Framey and waits up to 30 seconds (checked every 2 seconds) for it to become active. If setup fails, rollback is attempted when possible, but the installed state remains unconfirmed; run Install again.
  5. If Fan Control is ticked, runs its root step (see [Fan Control](#fan-control-root-step)). If that step fails, restoring the previous Fan Control folder is attempted, but the system state and folder restoration remain unconfirmed. Check fan control on the headset and run Install again. Framey stays updated.
  6. Refreshes plugins that were added from a GitHub link, restarts SteamVR if it is running, and warns if Steam's debug port (8080) is closed.
  Backups: after the new version is confirmed, `framey.bak` moves to `framey.bak.prev` and `.next` moves to `.bak`. When the install finishes, `.bak` and `.bak.prev` are deleted. If the install fails, they stay on the headset.
- **Check for updates** (labelled "Check for updates"). Reads `.version` on the headset. Framey is compared with the latest commit of `main`. Fan Control is compared with the latest release tag, never with `main`; a leading `v` is ignored and a prerelease sorts before its release. Each line shows the state: up to date, update available, installed version unknown (the stored value is not a version, for example a commit id, and the `version` in `plugin.json` is not one either; otherwise that version is used), not installed, or could not check, with the reason (GitHub unreachable, GitHub refused the request, possibly the rate limit, no release tag found). Check changes nothing on the headset except the one-time key setup on first use.
- **Add plugin** (asks first). Installs a plugin from a `.zip` or `.tar.gz` file, a `github.com` repository or branch link, or an https link to such a file. Framey must already be installed. Framey and, if running, SteamVR are restarted, which ends a running VR session. Only plugins installed from supported GitHub links are refreshed on each Install / Update; other HTTPS archive installs require an explicit reinstall.
- **Remove** (asks first). In this order: if Fan Control's system files are present, removes them with the headset password (restoring stock fan control). If that fails, nothing else is deleted. Then removes Framey (service, folders, backups, `framey.broken`, plugins and settings) and Fan Control's folders and settings. Then removes only this app's key line from `~/.ssh/authorized_keys` on the headset, the key files on this computer, this headset's known-hosts entry, and the data folder if no other headset's key remains. Local files that cannot be deleted are listed as leftovers. Remove does not restart SteamVR.

**SteamVR restart.** Install / Update and Add plugin run `systemctl --user try-restart steamvr.service` after a successful run, and only if SteamVR is running. A stopped SteamVR stays stopped. There is no setting that turns this off, and Remove does not restart SteamVR.

The separate Framey dashboard panel may not return automatically after a SteamVR restart, even when the bottom-bar/embedded UI returns and the dashboard preference is saved. In Framey Settings, use **Retry**, then reselect the dashboard option. This restored the panel in a direct headset check; automatic separate-panel recovery remains open, and the app's service/debug-port checks do not confirm overlay visibility.

**Themes.** Four colour themes, chosen with the swatches: Magenta, Blue, Black (green text) and White (light). The choice is saved in `FrameyApp-data/theme.txt`.

## Connection and keys

- SSH is run with the computer's own `ssh`, `scp`, `ssh-keygen`, `curl` and `tar` from `%SystemRoot%\System32` (on Windows) or `PATH`, as user `steamos`, with `BatchMode` and `IdentitiesOnly`.
- Host keys: `StrictHostKeyChecking=accept-new` with a `known_hosts` file in `FrameyApp-data`. The first key a headset presents is trusted. A changed key stops the job; if you trust the new key, delete the `known_hosts` file in `FrameyApp-data` and try again.
- Key: the app creates one ed25519 key per headset address (`key-<address>` in `FrameyApp-data`). It has no passphrase and is stored unencrypted on this computer.
- First use: the password you type is used once, to add the public key as one line (comment `framey-app-` plus 12 random characters) to `~/.ssh/authorized_keys` on the headset. Later jobs use the key and need no password.
- Password: typed into the app, never saved on this computer or the headset, and never placed on a command line. It reaches `ssh` through `SSH_ASKPASS` (the app runs itself as the askpass helper) and `sudo` through stdin. The field is cleared when a job ends. The password is set only in the environment of the child process, not of the app. A process running as the same user can still read a child's environment.

## Fan Control root step

Fan Control is installed only when its box is ticked, and then the headset password is required. The release signature is checked on this computer first. The verified tarball is uploaded to a temporary file in the headset's home folder. One `sudo -S` command has root copy it to a new folder under `/var/tmp`, check its SHA-256 against the hash of the bytes the app verified, unpack it and run that copy's `install-root.sh --signer-fp SHA256:sumclwMiONyyDcNbUAtihk4QQHu8PtIkneKbnNkMt9A`. The script refuses to install the trust key unless it matches that fingerprint. Root never runs the scripts in `~/frame-fan`, which the `steamos` user owns.

Root-owned files installed on the headset: `/etc/frame-fan/update-helper`, a sudoers rule allowing only its `update` and `rollback` commands, and `/etc/frame-fan/allowed_signers`. After that, routine Fan Control and Framey updates can be run from the headset without the password. Remove runs the bundled `uninstall-root.sh` the same way, from a root-owned copy checked by SHA-256, and first invalidates `/run/frame-fan.stamp` in that privileged wrapper even when the bundled script predates this cleanup; invalidation failure stops removal before the script runs.

- Trust: Fan Control files are trusted only when the release signature verifies against the key built into this app. A new publisher key needs a new app release.
- Caveat: there is no bundled Fan Control copy to install. When GitHub cannot be reached or rate-limits the request, Fan Control is skipped and the installed one is kept. Fan Control is never installed without a verified signed release.

## What runs on the headset

- The `framey.service` unit the app writes sets `Restart=always`, `RestartSec=5`, `KillMode=mixed`, `TimeoutStopSec=15`, `Nice=19`, `CPUSchedulingPolicy=idle`, `CPUWeight=1`, `MemoryHigh=128M` and `MemoryMax=150M`. The memory limits are a ceiling, not a budget: above 128 MB the kernel slows and reclaims memory for the service, and at 150 MB it may kill processes of the service. The loader and the worker processes it starts share that ceiling. No task-count limit is set.
- Recovery: before each start, an `ExecStartPre=-` line checks that `~/framey/framey.py` exists and parses. If not, it uses the first of `~/framey.next` (the previous tree, moved aside by an interrupted install), `~/framey.bak` and `~/framey.bak.prev` that parses, and moves the broken tree to `~/framey.broken` (its `plugins` and `store` folders are carried over first). If none parses, nothing changes. It repairs only a missing or unparsable `framey.py`, not a loader that crashes after it started.
- The unit is written only by Install / Update, so a headset gets new limits the next time you run it.
- Plugins run code as the `steamos` user. Backend workers are separate processes for failure containment; they are not a security sandbox.

## Packages, limits and timeouts

- Plugin packages are listed and checked before extraction. Absolute paths, `..`, backslashes, links, special files, duplicate names (case-insensitive) and packages over 500 entries, 8 levels or 20 MB are refused. Extraction has a 60-second limit and an on-disk 20 MB quota, and the result is checked again afterwards.
- `plugin.json` must be strict JSON: an object with an `id` (lowercase letters, digits, `-` and `_`, up to 32 characters, not starting with `-`, not `_core`), and `name`, `version` and `short` as strings if present.
- Each headset step has a time limit: 120 s for SSH commands, 150 s for downloads, 600 s for uploads, 60 s for archive listing and extraction. `ssh` and `scp` send keepalives every 10 s and give up after three missed ones. The log names the step that timed out.
- Downloads are https only and capped at 20 MB.

## Portable data

The app keeps its keys, `known_hosts`, last address, theme and a per-run `work-` folder (removed after each job; folders left by killed runs are retained because their age cannot establish whether another app instance still owns them) in `FrameyApp-data` next to the executable. It writes nothing to the registry or your home folder. Run it from a folder you can write to; if the theme cannot be saved, the choice is not kept. Deleting the app and its data folder leaves nothing behind, apart from any headset key you have not removed with Remove.

## What has been verified

- **Current experimental source, 2026-10-26:** the core and GTK UI compiled and linked with Clang/CMake/Ninja on Linux; selected clang-tidy checks were clean. All 21 inert regression checks passed for workspace ownership, uncertain failure reporting and removal ordering/status propagation. They use substitutes for external operations and do not perform a real install. The existing generated payload was reused.
- **Current Windows and macOS source:** static review only; no fresh native build or dialog execution was performed.
- **Direct headset checks, 2026-10-26:** the experimental Framey and Fan Control snapshots were deployed and checked directly, including a SteamVR restart and manual dashboard recovery. This did not exercise this desktop app end to end, its signed Fan Control install or the signed updater path.
- **Historical 1.0.2 checks:** an earlier Windows build's native window was launched and inspected; Linux and macOS builds compiled in CI. Earlier local installer checks used fake `ssh`, `scp` and `curl`, and stubbed remote scripts. Those results do not validate today's source.
- **Historical 1.0.1 headset check:** run by the author against a Steam Frame (SteamOS 0.4.5). First-time password authorization, Install with Fan Control, and the SteamVR restart worked. Framey's panel and Fan Control opened and worked in VR.

The current app's staged update, health check, backup rotation, `ExecStartPre` recovery, Remove sequence, signed Fan Control install, release-tag check and memory limits still lack end-to-end headset acceptance through the app.

AI authorship and a successful build are not evidence that something works.

## Known open items

- A theme swatch click that fails to save the theme is not reported to the user.
- A connection drop between stopping and restarting Framey during Install can leave Framey stopped until the headset reboots or Install runs again. Startup recovery can restore a missing or unparsable tree only if a usable backup remains.
- The recovery step does not restore a loader that crashes repeatedly after it has started.
- Fan Control's root step unpacks the verified archive with plain `tar`, without the name checks the helper uses. The signature and the SHA-256 pin are the protection.
- Current Fan Control source handles INT, TERM and HUP by exiting through its EXIT cleanup trap. Forced termination or failed cleanup can still leave partial system state; the app runs the script from the selected signed release.
- The signature check reads the release and its `.sig` from the user-writable work folder. A same-user process could race that window; the fix needs the verifier to read from a file descriptor.
- The source saved for a plugin on the headset can be any `github.com` repository. Restricting it is undecided, because third-party plugins live in other repositories; a compromised headset could steer plugin installs until then.

## Security notes

- The app has no network listener. It makes outbound https requests to GitHub and SSH connections to the headset only.
- Framey is downloaded at the latest commit of `main` and pinned to that commit for the run. Plugins from links are not pinned.
- Root runs only the verified copy of Fan Control's scripts described above. Use the app only with repositories you trust.
- On Windows, the app starts its tools only from `System32` and `PATH`, never from its own or the current folder.

## Build

The current experimental source makes staging success explicit, gives native actions and cached UI state clearer names, checks theme palette counts, and separates rollback/backup script construction. Failure messages distinguish attempted recovery from confirmed state, and job cleanup removes its own workspace without pruning other instances' folders by age. These are maintenance changes; release selection and trust checks remain as described above.

See [docs/development.md](docs/development.md) for build commands and implementation details.

## License

Framey App is licensed under the GNU General Public License, version 2 only. The full text is in [LICENSE](LICENSE). Source: <https://github.com/chaosfox26/framey-app>.
