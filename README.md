# IAMVPN

VPN client for Windows and macOS built with C++ and Qt 6 (QML), running on top of sing-box.

## Download

Get the latest build from [Releases](https://github.com/strdr1/IAMVPN/releases/latest):

- **Windows:** `IAMVPN-Setup.exe`. The app checks for updates on every launch and installs them on its own.
- **macOS:** `IAMVPN-mac.dmg`. One universal build covers Apple Silicon and Intel and needs macOS 12 or later.
  Open the DMG and drag IAMVPN into Applications.

  The app isn't signed with an Apple developer certificate, so the first launch is blocked.
  Right-click IAMVPN and choose **Open**, or use **System Settings → Privacy & Security → Open Anyway**.
  On the first connect of each session, macOS asks for an administrator password: creating the VPN tunnel needs it.
  Updates are downloaded as a new DMG that you drag over the old app.
  The DPI bypass (zapret) is Windows-only.

## Features

- Accepts single keys (`vless://`, `vmess://`, `trojan://`, `ss://`, `hysteria2://`, `tuic://`) and subscription links.
  Supported transports: tcp, ws, grpc, httpupgrade and h2.
  xhttp isn't supported yet: sing-box doesn't implement it.
- **Auto** mode puts every server from the subscription into one group.
  The app keeps the fastest working server and switches to another if that one dies.
- Checks that traffic really passes through the tunnel (a delay test through it).
  If no server answers, it disconnects and gives your normal internet back.
- `.ru` sites and Russian IPs go direct, bypassing the VPN.
- You can add your own sites to send direct as well.
- Optional DPI bypass (zapret) and a local Telegram proxy.

## Build

Requirements: Visual Studio 2022 (C++ workload) and Qt 6.8 (msvc2022_64).

Run `build.bat`. It finds Qt, CMake and the compiler, then builds and deploys into `build\`.
The installer is built with Inno Setup from `installer\IAMVPN.iss`.

`engine\bin`, `engine\vpn` and `engine\python` aren't stored in the repo:
they hold the zapret, sing-box and Python binaries.

## Releasing an update

1. Bump the version in `src/main.cpp` (`setApplicationVersion`) and in `installer/IAMVPN.iss`.
2. Build the app and the installer.
3. Create a GitHub release with the tag `vX.Y.Z` and attach `IAMVPN-Setup.exe`.
4. Update `latest.json` in `main`. Installed apps pick up the new version from it.
