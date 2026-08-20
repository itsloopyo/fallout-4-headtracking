# Fallout 4 Head Tracking

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/fallout-4-headtracking/main/assets/readme-clip.gif)

An unofficial, OpenTrack compatible head tracking mod for Fallout 4 - move your head to look around while your mouse or controller keeps aiming the weapon, using an ordinary webcam, phone, or VR headset.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [Fallout 4](https://store.steampowered.com/app/377160/Fallout_4/) (Steam, GOG, or Game Pass install).
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or VR headset, or a phone app that speaks the OpenTrack UDP protocol.
- Windows 10 or 11, 64-bit.

## Installation

1. Download `Fallout4HeadTracking-v<version>-installer.zip` from the
   [Releases](https://github.com/itsloopyo/fallout-4-headtracking/releases)
   page.
2. Extract it anywhere.
3. Double-click `install.cmd`. The installer auto-detects the game and
   installs Ultimate ASI Loader (`dxgi.dll`) plus the mod
   (`Fallout4HeadTracking.asi`) into the game's exe directory.
4. Configure your tracker to send UDP to `127.0.0.1` on port `4242`. See
   [Setting Up OpenTrack](#setting-up-opentrack) below.
5. Launch Fallout 4. On first launch the mod writes `HeadTracking.ini` next
   to the `.asi` and reads it from there on every launch after.

If the installer cannot find your game, point it at the install folder
yourself. Either pass the path as an argument:

```powershell
install.cmd "D:\Games\Steam\steamapps\common\Fallout 4"
```

Or set the `FALLOUT_4_PATH` environment variable before running it:

```powershell
$env:FALLOUT_4_PATH = "D:\Games\Steam\steamapps\common\Fallout 4"
.\install.cmd
```

### Manual Installation

Use the Nexus ZIP (`Fallout4HeadTracking-v<version>-nexus.zip`) if you would
rather place files by hand or deploy through a mod manager. Its `Root/`
folder holds the game-root payload, which Mod Organizer 2's Root Builder
deploys for you; Vortex and manual installers copy the contents of `Root/`
into the game folder themselves.

1. Install an ASI loader in a proxy slot Fallout 4 actually imports:
   `dxgi.dll`, `d3d11.dll`, `xinput1_3.dll`, or `winhttp.dll`. The
   [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
   bundled in our installer ZIP works, renamed to `dxgi.dll`. `dinput8.dll`
   and `winmm.dll` do **not** work here: `Fallout4.exe` never imports them,
   so the loader is never loaded.
2. Drop `Fallout4HeadTracking.asi` next to `Fallout4.exe`. The mod writes its
   own `HeadTracking.ini` beside it on first launch.

If you already run ReShade as `dxgi.dll`, install the ASI loader as
`d3d11.dll` instead (or rename it to one of the other slots above) so the two
do not fight over the same filename.

## Setting Up OpenTrack

Whatever you track with, the output side is the same:

- Output protocol: **UDP over network**.
- Address: `127.0.0.1`, port `4242`.
- Start tracking in OpenTrack first, then launch Fallout 4.
- The mod never picks a centre of its own; it applies whatever your tracker
  sends. Centre the view in your tracker app once you are settled, with
  OpenTrack's Center bind or your phone app's CENTER button.

### VR Headset Setup

Although this is a 2D, flatscreen mod, you can still use a VR headset to play.

1. Connect the headset over Air Link or
   [Virtual Desktop](https://www.vrdesktop.net/) and start SteamVR.
2. In OpenTrack, set Input to **SteamVR**.
3. Set Output to **UDP over network**, `127.0.0.1:4242`.
4. Start the game

### Webcam Setup

1. In OpenTrack, set Input to **neuralnet tracker**. It needs no markers and
   works with any ordinary webcam.
2. Set Output to **UDP over network**, `127.0.0.1:4242`.
3. Center yourself in frame with even lighting, then hit Start.

### Phone App Setup

I made [Headcam](https://headcam.app) with user friendliness and performance
in mind. It does its own smoothing on device and is free to download.

- If the app you are using does its own smoothing, point it straight at your PC's LAN
  address on port `4242` and skip OpenTrack entirely.
- If you want OpenTrack's curve mapping and per-axis filters, have the app
  send to OpenTrack instead and let OpenTrack relay to `127.0.0.1:4242`.

## Controls

Two equivalent binding sets - use whichever your keyboard has. The nav-cluster
keys and the chords do exactly the same thing.

| Action                 | Nav-cluster | Chord           |
|------------------------|-------------|-----------------|
| Toggle tracking        | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode    | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode        | `Page Down` | `Ctrl+Shift+H`  |
| Next tracker source    | -           | `Ctrl+Shift+U`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

`HeadTracking.ini` sits next to `Fallout4.exe`. It is written with defaults on
first launch; delete it to reset. Updating the mod never overwrites it.

```ini
[Network]
; UDP port for OpenTrack data (default: 4242)
UDPPort=4242

[Sensitivity]
; Rotation sensitivity multipliers (1.0 = 1:1)
YawMultiplier=1.0
PitchMultiplier=1.0
RollMultiplier=1.0
; Smoothing applied when the tracker runs on this machine (loopback).
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
LocalSmoothing=0.0
; Smoothing applied when the tracker is a remote device on the network.
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
RemoteSmoothing=0.15

[Position]
; Position tracking sensitivity (0.1-10.0, higher = more movement)
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Position limits in meters (how far the camera can move)
LimitX=0.30
LimitY=0.20
LimitZ=0.40
; Backward lean limit (prevents camera clipping through player model)
LimitZBack=0.10
; Invert position axes
InvertX=true
InvertY=false
InvertZ=true
; Enable/disable position tracking (6DOF)
Enabled=true

[Hotkeys]
; Virtual key codes (hex)
ToggleKey=0x23         ; End - Enable/disable head tracking
PositionToggleKey=0x21 ; Page Up - Cycle tracking mode
YawModeKey=0x22        ; Page Down - Toggle world/local yaw

[General]
; Auto-enable tracking on game start
AutoEnable=true
; Write notification messages to HeadTracking.log
ShowNotifications=true
; Yaw mode: true = horizon-locked (default), false = camera-local
WorldSpaceYaw=true
```

`WorldSpaceYaw=true` (default) keeps "up" locked to the world horizon, so
yawing while looking at the floor still pans left and right. Set it to `false`
for camera-local yaw, which rotates around the camera's current up-axis.
Toggle it live with `Page Down`.

## Troubleshooting

- **Mod not loading.** Confirm `dxgi.dll` and `Fallout4HeadTracking.asi` are
  both next to `Fallout4.exe`. Check that `HeadTracking.log` appears in the
  same directory after a launch; if it does not, the ASI loader is not
  running. If another mod already owns the `dxgi.dll` slot, move one of them
  to `d3d11.dll`.
- **No tracking response.** Your tracker must be sending UDP to
  `127.0.0.1:4242` and must be started before the game. `HeadTracking.log`
  records `First UDP packet received` with the sender address the moment
  anything arrives; if that line is absent the tracker is not reaching the
  game, so confirm `UDPPort` matches and that Windows Firewall is not blocking
  loopback. For a phone app, use your PC's LAN address rather than
  `127.0.0.1`.
- **Jittery or unstable tracking.** Raise the smoothing value your tracker
  actually uses: `RemoteSmoothing` for a phone or another device on the
  network, `LocalSmoothing` for a tracker running on this PC. Each covers
  rotation and position together.
  Webcam tracking benefits most from brighter, more even lighting. Lower the
  sensitivity multipliers if small head movements overshoot.
- **Wrong rotation axis or inverted movement.** Flip the relevant
  `[Position] InvertX/InvertY/InvertZ` value. If yaw feels wrong at extreme
  up or down angles, toggle between horizon-locked and camera-local yaw with
  `Page Down`.
- **Reporting a crash.** `HeadTracking.log` sits next to `Fallout4.exe` and is
  rewritten from scratch on every launch. The previous launch is kept as
  `HeadTracking.prev.log`, so the session that crashed survives the relaunch
  that follows it - attach both files.

## Updating

Download the new release and run `install.cmd` again. Your config is
preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files. The ASI loader is only
removed if the installer put it there; if you already had your own, it is left
alone. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires CMake 3.20 or newer, Visual Studio 2022 with the C++ workload, and
[pixi](https://pixi.sh). The mod targets MSVC x64.

```bash
git clone --recursive https://github.com/itsloopyo/fallout-4-headtracking
cd fallout-4-headtracking
pixi run build-release
pixi run package
```

`pixi run package` writes the installer and Nexus ZIPs to `release/`.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Bundled and linked
third-party components keep their own licenses; see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Fallout 4 (c) Bethesda Game Studios / Bethesda Softworks.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by
  ThirteenAG.
- [OpenTrack](https://github.com/opentrack/opentrack).
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu.
- [CameraUnlock Core](https://github.com/itsloopyo/cameraunlock-core), the
  shared head-tracking library behind this mod.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Bethesda Game
Studios or Bethesda Softworks. It requires a legitimately purchased copy of the
game. Use at your own risk.
