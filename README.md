# Fallout 4 Head Tracking

![Fallout 4 running with this mod](https://raw.githubusercontent.com/itsloopyo/fallout-4-headtracking/main/assets/readme-clip.gif)

*Fallout 4 footage (c) Bethesda Game Studios / Bethesda Softworks, recorded on a legitimately purchased copy and shown only to demonstrate this mod. It is not covered by this project's MIT licence - see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).*

An unofficial head tracking mod for Fallout 4 that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

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

The installer detects Steam, GOG and Game Pass copies, and stops at the first
it finds in that order. If you own the game on more than one of them, point it
at the one you actually play. A Game Pass copy lives in `XboxGames` on whichever
drive you told the Xbox app to install to, and the folder to pass is the
`Content` one inside it:

```powershell
install.cmd "D:\XboxGames\Fallout 4\Content"
```

Or set the `FALLOUT_4_PATH` environment variable before running it:

```powershell
$env:FALLOUT_4_PATH = "D:\Games\Steam\steamapps\common\Fallout 4"
.\install.cmd
```

### Manual Installation

`install.cmd` does everything below for you. Place the files by hand only if
you would rather see exactly what lands where; both come out of the installer
ZIP, as `plugins/Fallout4HeadTracking.asi` and
`vendor/ultimate-asi-loader/dinput8.dll`.

Mod managers do not deploy this mod. An ASI plugin has to sit next to
`Fallout4.exe`, and Fallout 4 mod managers deploy into `Data/`: Vortex has no
way to reach the game root at all, and Mod Organizer 2 needs Root Builder on
top of its virtual file system. Install into the game folder instead, with
`install.cmd` or by hand.

1. Install an ASI loader in a proxy slot Fallout 4 actually imports:
   `dxgi.dll`, `d3d11.dll`, `xinput1_3.dll`, or `winhttp.dll`. The
   [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
   bundled in our installer ZIP works, renamed to `dxgi.dll`. `dinput8.dll`
   does **not** work here: `Fallout4.exe` never imports it, so the loader is
   never loaded.
2. Drop `Fallout4HeadTracking.asi` next to `Fallout4.exe`. The mod writes its
   own `HeadTracking.ini` beside it on first launch.

If you already run ReShade as `dxgi.dll`, install the ASI loader as
`d3d11.dll` instead (or rename it to one of the other slots above) so the two
do not fight over the same filename.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

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

### Field of view

The mod adds no field-of-view setting, because Fallout 4 already has two and a
third would only disagree with them.

- **In game**, open the console and type `fov 80 70`. The first number is first
  person, the second is everything else. It takes effect immediately and lasts
  for that session. Which key opens the console depends on your keyboard
  layout; `` ` `` and `'` are the usual ones.
- **Permanently**, set `fDefault1stPersonFOV` and `fDefaultWorldFOV` under
  `[Display]` in `Documents\My Games\Fallout4\Fallout4.ini`. Those are the
  values the game ships at 80 and 70.

Head tracking follows whichever you use, without a restart, and it keeps the
amount your head moves the view the same at any field of view. When the game
narrows the view by itself, down a scope or through iron sights, the same head
turn would otherwise sweep the picture much further and feel like the
sensitivity had jumped; the mod scales the pose so it does not. Roll is left
alone, because a head tilt rotates the picture by the same angle whatever the
field of view.

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

`pixi run package` writes the installer ZIP to `release/`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Bundled and linked
third-party components keep their own licenses; see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Fallout 4 (c) Bethesda Game Studios / Bethesda Softworks.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by
  ThirteenAG.
- [OpenTrack](https://github.com/opentrack/opentrack).
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, and the
  Hacker Disassembler Engine by Vyacheslav Patkov that it builds on. Our copy is
  modified; the changes are listed in `extern/minhook/MODIFICATIONS.md`.
- [CommonLibF4](https://github.com/Ryan-rsm-McKenzie/CommonLibF4) by
  Ryan-rsm-McKenzie, and [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR),
  for the engine struct offsets and vtable indices this mod hooks. Neither is
  bundled or linked; no code from either was copied.
- [CameraUnlock Core](https://github.com/itsloopyo/cameraunlock-core), the
  shared head-tracking library behind this mod.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Bethesda Game
Studios or Bethesda Softworks. It requires a legitimately purchased copy of the
game. Use at your own risk.
