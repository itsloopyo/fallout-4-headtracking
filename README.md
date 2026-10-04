# Fallout 4 Head Tracking

![Fallout 4 running with this mod](https://raw.githubusercontent.com/itsloopyo/fallout-4-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Fallout 4 that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Fallout 4](https://store.steampowered.com/app/377160/Fallout_4/) (Steam, GOG, or Xbox Game Pass install).
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or VR headset, or a phone app that speaks the OpenTrack UDP protocol.
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Fallout 4**, and click
**Play with head tracking**.

### Standalone Installer

1. Download `Fallout4HeadTracking-v<version>-installer.zip` from the
   [Releases](https://github.com/itsloopyo/fallout-4-headtracking/releases)
   page.
2. Extract it anywhere.
3. Double-click `install.cmd`. The installer auto-detects the game and
   installs Ultimate ASI Loader (`dxgi.dll`) plus the mod
   (`Fallout4HeadTracking.asi`) into the game's exe directory.
4. Configure your tracker to send UDP to `127.0.0.1` on port `4242`. See
   [Setting Up OpenTrack](#setting-up-opentrack) below.
5. Launch Fallout 4. On first launch the mod creates `CameraUnlock.ini` next
   to the `.asi` and reads it from there on every launch after. See
   [Configuration](#configuration).

If the installer cannot find your game, point it at the install folder
yourself. Either pass the path as an argument:

```powershell
install.cmd "D:\Games\Steam\steamapps\common\Fallout 4"
```

The installer detects Steam, GOG and Xbox Game Pass copies, and stops at the first
it finds in that order. If you own the game on more than one of them, point it
at the one you actually play. An Xbox Game Pass copy lives in `XboxGames` on whichever
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
2. Drop `Fallout4HeadTracking.asi` next to `Fallout4.exe`. The mod creates its
   own `CameraUnlock.ini` beside it on first launch.

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
keys and the chords do exactly the same thing. These are the defaults: each
action's keys are a list under `[Hotkeys]` in `CameraUnlock.ini`, chords
included, and any of them can be changed or removed.

| Action                 | Nav-cluster | Chord           |
|------------------------|-------------|-----------------|
| Toggle tracking        | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode    | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode        | `Page Down` | `Ctrl+Shift+H`  |
| Cycle aim mode         | `Insert`    | `Ctrl+Shift+U`  |
| Next tracker source    | -           | `Ctrl+Shift+J`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

The tracking mode, the yaw mode and the aim mode are saved to `CameraUnlock.ini`
the moment you change them, so the next launch starts with the same choice.
Toggling tracking on or off with `End` lasts for the session only: each launch
starts with tracking on or off as `EnableOnStartup` says.

### Aiming down sights

Head tracking stays on while you aim, unless you pick stock sights below. The
weapon stays where your mouse or controller points it, so with your head turned
it sits off to one side with its sights still lined up, and your rounds land
where those sights point. Head movement is scaled to the zoom, so a scope does
not magnify it. Leaning in towards the screen is not scaled: it moves the view
the same distance at any zoom.

`Insert` / `Ctrl+Shift+U` cycles four ways of aiming, and the mod saves the one
you pick, so it holds the next time you start the game:

1. **Sights locked** (default) - leaning never takes your eye off the sights,
   and leaning in towards them brings them closer.
2. **Free look with a marker** - the weapon stays put and your head moves freely
   around it, so the sights only line up with your head behind them. A small
   white marker shows where your rounds will land while the sights are up.
3. **True free look** - the same, with no marker. To place a shot you have to
   put your head behind the sights, as you would in VR. It is hard.
4. **Stock sights** - while the sights are up your head stops turning and
   leaning the view, so the sights sit in the centre as they do without head
   tracking. Tilting your head still tilts the view. When you lower the weapon
   the view goes back to where you are looking.

Leaning carries on through the aim. In sights locked, as the sights come up,
your arms and weapon move with your head when you lean to the side or up and
down, so the sights stay in front of your eye, and your rounds leave from where
your eye is. Lean round a corner with the sights up and you can hit what you can
see from there. In modes 2 and 3 the weapon stays with your body as you lean and
your rounds leave from the weapon. Leaning in brings the sights closer in modes
1 to 3, and your eye stops just behind them while the view goes on leaning in.
In third person the view keeps the whole lean while you aim.

Head tracking carries on through a scope that fills the screen. With your head
off a scope's axis its own reticle is no longer where the round goes. In mode 2
the white marker is. In stock sights the scope is the game's own, and its
reticle is where the round goes.

To keep your aim mode for other sights and still have the game's own view
through a scope, set `TrackThroughScopes=false` in `CameraUnlock.ini`: a scope
that fills the screen is then handled as stock sights, whichever mode you have
picked.

The view is the game's own while the Pip-Boy is up, and stays so for the half
second the arm takes to lower. Head tracking then eases back in.

The character the game names while you aim at them, and the prompt that comes
with it, is the one your sights are on, wherever your head is turned.

The marker of mode 2 is drawn over the finished frame through Direct3D 11. The
mod leaves the game's presentation alone until the first time that marker is
needed, so it touches nothing there in the other three modes.

### Picking things up

The crosshair marks what you can pick up or use. It sits where your mouse or
controller is aiming, wherever your head is turned or leaned, and that is the
thing `E` acts on.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Changing a setting in `Defaults.ini` changes it in every game that has it set to `default`. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `TrueFreeLook=false`
- `FreeLookMarker=false`
- `StockSights=false`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `CollisionEnabled=true`
- `CollisionReleaseSmoothing=0.9`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`
- `TrueFreeLookKey=Insert, Ctrl+Shift+U`

With every setting at its default, the file reads:

```ini
; Fallout 4 head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Change a setting in Defaults.ini to change it in
; every game that has it set to default, or write a value here instead of default to change it
; for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default
; true: write the mod's notices (tracking on or off, a mode change) to HeadTracking.log.
ShowNotifications=true
; false: while you look through a scope that fills the screen the view is the game's own, as in the stock sights aim mode, whichever aim mode you use for other sights. Head roll still tilts it.
TrackThroughScopes=true

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; false: while you aim down the sights, leaning keeps your eye on the sights.
; true: the weapon stays put and your head moves freely around it (true free look).
TrueFreeLook=default
; true, with TrueFreeLook=true: an aim marker shows where your shot will land while you aim down the sights.
; It does nothing while TrueFreeLook is false.
FreeLookMarker=default
; true: while you aim down the sights your head stops moving the view, apart from tilting it,
; so the sights sit in the centre as they do without head tracking. At the hip nothing changes.
StockSights=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default
; true: leaning stops at walls instead of moving the view through them.
; Only games whose mod sweeps the level for walls read this; the rest ignore it.
CollisionEnabled=default
; How far the view is held off a wall when you lean into it, in the game's own units.
CollisionMargin=10.0
; Which of the game's collision channels the wall check tests against.
; CollisionChannel=39
; How gently the view eases back out after a wall stopped a lean.
; 0 is the quickest, 1 the slowest.
CollisionReleaseSmoothing=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default
; Cycles the aim mode: sights locked, free look with a marker, true free look, stock sights
; (TrueFreeLook, FreeLookMarker, StockSights).
TrueFreeLookKey=default
; Switches to the next tracker app when more than one sends to the UDP port.
CycleTrackerSourceKey=Ctrl+Shift+J
```
<!-- /cameraunlock:config -->

`WorldSpaceYaw=true` (the built-in value) keeps "up" locked to the world
horizon, so yawing while looking at the floor still pans left and right. Set it
to `false` for camera-local yaw, which rotates around the camera's current
up-axis. Toggle it live with `Page Down`; the mod saves the choice.

The mod has no sensitivity, deadzone or axis inversion settings. It applies the
pose your tracker sends, so set those in the tracker.

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

### Weapon debris

While the mod is loaded, the game's weapon debris effect (NVIDIA FleX, "Weapon
Debris" in the game launcher's advanced settings) is switched off. With it on,
Fallout 4 can crash inside its own FleX library within a minute or so of a save
loading. That was measured on an RTX 5080 with the mod's hooks not installed, so
it is the game's fault and not the mod's, but it reads as the mod crashing the
game.

The switch lasts for the session only. `bNVFlexEnable` in
`Documents\My Games\Fallout4\Fallout4Prefs.ini` is left as you have it, also
when the game saves its settings, so the game behaves as before once the mod is
removed. `HeadTracking.log` says at startup whether it switched the effect off
or found it off already.

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
  game, so confirm `UdpPort` matches and that Windows Firewall is not blocking
  loopback. For a phone app, use your PC's LAN address rather than
  `127.0.0.1`.
- **Jittery or unstable tracking.** Raise the smoothing value your tracker
  actually uses: `RemoteSmoothing` for a phone or another device on the
  network, `LocalSmoothing` for a tracker running on this PC. Each covers
  rotation and position together.
  Webcam tracking benefits most from brighter, more even lighting. The mod
  applies no sensitivity of its own, so if small head movements overshoot,
  change that in the tracker.
- **Inverted movement.** The mod applies the axes as the tracker sends them, so
  an axis that runs the wrong way is corrected in the tracker. If yaw
  feels wrong at extreme up or down angles, toggle between horizon-locked and
  camera-local yaw with `Page Down`.
- **The weapon is off to one side when I aim down sights.** Your head is
  turned: the weapon stays on your aim and you are looking past it. Turn back to
  it, or move your aim to where you are looking.
- **I can't see down the sights, they are misaligned.** You are in one of the
  free look modes and your head is leaned off them. Move your head back behind
  them, or press `Insert` / `Ctrl+Shift+U` until the log says sights locked.
- **The view swings when I raise or lower the sights.** You are in stock sights
  with your head turned: the view goes to your aim while the sights are up and
  back to where you are looking when they come down. Press `Insert` /
  `Ctrl+Shift+U` for another mode if you want head tracking to carry on through
  the aim.
- **Reporting a crash.** `HeadTracking.log` sits next to `Fallout4.exe` and is
  rewritten from scratch on every launch. The previous launch is kept as
  `HeadTracking.prev.log`, so the session that crashed survives the relaunch
  that follows it - attach both files.

## Updating

Download the new release and run `install.cmd` again. The installer ships no
config, so `CameraUnlock.ini` is left as it is.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files and leaves `CameraUnlock.ini`
in place, so a reinstall keeps your settings. The ASI
loader is only removed if the installer put it there; if you already had your
own, it is left alone. Use `uninstall.cmd /force` to remove it anyway.

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
