# Changelog

All notable changes to this mod are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
- Through a scope that fills the screen, tilting your head now tilts the view. Turning and leaning are still held off there, unless you set `TrackThroughScopes=true`.
- Settings move to `CameraUnlock.ini`, next to `Fallout4.exe`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- In first person, leaning carries on through the aim. As the sights come up, a lean to the side or up and down moves from the view to the first-person arms and weapon, so the sights stay in front of your eye and your rounds leave from where your eye is. It moves back to the view as the sights come down. Leaning toward or away from the sights stays on the view. The lean is stopped at walls before it is shared out, so the muzzle stops where your eye does. In third person the view keeps the whole lean while you aim, including when you switch out of first person with the sights up.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - A hotkey set to Ctrl, Shift or Alt on its own. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord where it has one.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] PositionToggleKey` is now `CycleTrackingModeKey`, and the chords, fixed in code before, can be changed or removed like any other key.
- The tracking mode (`Page Up`) and the yaw mode (`Page Down`) are saved to `CameraUnlock.ini` the moment you change them, so the next launch starts with the same choice. They lasted for the session only before. `End` still changes the session only.
- `[General] AutoEnable` is now `EnableOnStartup`, `[Network] UDPPort` is now `UdpPort`, `[Sensitivity] LocalSmoothing` and `RemoteSmoothing` move to `[Smoothing]`, and `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are now `PositionLimitX`, `PositionLimitY`, `PositionLimitZ` and `PositionLimitZBack`. `LimitY` bounded leaning down as well as up; the import writes its value into both `PositionLimitY` and the new `PositionLimitYDown`. `[Position] Enabled` chose the tracking mode the game started in and is now that mode, as `RotationEnabled` and `PositionEnabled`.

- Weapon debris (the game's NVIDIA FleX effect) is switched off while the mod is loaded. With it on the game can crash inside its own FleX library shortly after a save loads, with or without the mod. `bNVFlexEnable` in `Fallout4Prefs.ini` is left as you have it, so the effect is back once the mod is removed.

### Fixed
- Closing the Pip-Boy no longer throws the view off to one side. Head tracking came straight back while the game was still lowering the arm and returning its camera, and for a third of a second it was applied at up to three and a half times your head's turn, so the arm left through the edge of the screen. It now stays off until the arm is down and eases back in.
- The character the game names while you aim down the sights, and the prompt that comes with it, is the one your sights are on. With your head turned it was whoever sat at the centre of the view.
- In third person the view no longer jerks back and forth while you lean and move. The check that stops a lean at a wall was being set off by the game's own camera, which trails the view while you walk, so the lean was dropped and eased back in several times a second. The same check was set off in first person by your own body and power armour. A lean now stops only for walls, floors and the rest of the level.
- The view no longer snaps to where your body is aimed and back when the game drops below 25 frames a second or stalls for a moment, as it does while it loads the area ahead.
- What you can pick up or use is what the crosshair is on. With your head turned or leaned the game was offering whatever sat at the centre of the view instead.
- The crosshair stays on what you are aiming at when you lean. It was placed by the direction of your aim alone, which is off by several degrees for something close while you lean.
- The muzzle flash comes out of the muzzle when you lean. It was drawn beside the weapon. Your arms and weapon are now drawn from where your head is, so they move across the view with the world, and with the sights up in the free look modes the weapon stays with your body while your head moves around it.
- Leaning in with the sights up brings the sights closer, and your eye stops just behind them.

### Removed
- The sensitivity, scale, deadzone, response curve and axis inversion settings: `[Sensitivity] YawMultiplier`, `PitchMultiplier` and `RollMultiplier`, and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`, `InvertX`, `InvertY` and `InvertZ`. Set these in your tracker app instead. The x inversion every earlier version shipped switched on (`InvertX=true`) is now part of how the mod converts the tracker's axes to the game's, so leaning goes the same way it did.
- With these settings at their shipped defaults the camera moves as it did before.

### Added
- `[General] TrackThroughScopes` in `CameraUnlock.ini`. It starts at `false`: a scope that fills the screen is handled as the stock sights aim mode, whichever aim mode you have picked for other sights, so the view through it is the game's own, the scope's reticle is where the round goes, and head roll still tilts it. Set it to `true` and the view follows your head through the scope as your aim mode says.
- `[Hotkeys] CycleTrackerSourceKey` in `CameraUnlock.ini`, the key list that switches to the next tracker app. It starts at `Ctrl+Shift+J` and can be changed or removed. `Ctrl+Shift+U`, the chord that did this before, now cycles the aim mode.
- `Insert` / `Ctrl+Shift+U` (`[Hotkeys] TrueFreeLookKey`) cycles four aim modes: sights locked, free look with a marker, true free look and stock sights. The choice is saved to `[Position] TrueFreeLook` and the new `[Position] FreeLookMarker` and `[Position] StockSights` in `CameraUnlock.ini` each time you press it. In the two free look modes the lean stays on your view through the aim, the weapon stays with your body and your rounds leave from it.
- Free look with a marker draws a small white marker where your round will land while the sights are up, through a scope that fills the screen too.
- Stock sights: while the sights are up your head stops turning and leaning the view, so the sights sit in the centre as they do without head tracking. The view eases onto your aim as the sights come up and back to where you are looking as they go down. Tilting your head still tilts the view, and at the hip nothing changes.
- Leaning stops at walls instead of moving the view through them: `[Position] CollisionEnabled` (on by default), `CollisionMargin`, `CollisionChannel` and `CollisionReleaseSmoothing` in `CameraUnlock.ini`. Settings imported from `HeadTracking.ini` leave `CollisionEnabled` and `CollisionReleaseSmoothing` to `Defaults.ini`.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Fixed
- Head movement is now scaled to the zoom on saves where the game's first-person view rests slightly narrower than the field of view you set. On those saves the mod never found its un-zoomed reference, so a head turn swept the picture further through the sights than at the hip.
- Leaning in towards the screen is no longer scaled down by the zoom. Through a sight that narrows the view, leaning sideways and up and down is scaled like a head turn, and leaning in moves the view the full distance.
- Head tracking moves the view by the same amount on screen whatever the game
  is doing with its field of view. Down a scope or through iron sights the view
  is drawn much narrower, which magnified head tracking along with everything
  else, so the same head turn swept the picture further and felt like the
  sensitivity had jumped the moment you aimed. The mod now reads the field of
  view the game is rendering and scales the pose to match. Roll is unchanged, a
  head tilt already rotates the picture by the same angle at any field of view.
- Changing the field of view with the console's `fov` command, or in
  `Fallout4.ini`, is picked up without restarting the game.

### Added
- The log now ends with a line saying the session ended normally. Without it a
  log from a player who simply quit and a log from a game that crashed both
  just stopped, so every crash report had to begin by working out whether there
  had been a crash at all. The line is missing from any session that crashed or
  was killed, which is what makes it useful.
- The previous session's log is kept as `HeadTracking.prev.log`. The log is
  rewritten on every launch, so a crash report sent after a relaunch used to
  arrive with the crashed session already gone. A rename that fails is reported
  in the fresh log, so a stale `.prev.log` is never mistaken for the last
  session.

### Fixed
- Head tracking now works with the Xbox/Game Pass build of Fallout 4 1.11.240.
- The installer finds a Game Pass copy of the game. It reads the Xbox app's own
  install folders off every drive and identifies the game by its executable, so
  a library on any drive is found without being pointed at it.
- VATS body-part percentages sit on the target again instead of being offset by
  however far the head is turned. The gate that takes the head pose off before
  VATS freezes the game thread was pinned to a per-build address, so the Fallout
  4 1.11.240 patch left it dormant and the overlay kept the projection it was
  laid out with.
- Uninstalling now deletes `HeadTracking.log`, `HeadTracking.prev.log` and
  `HeadTracking.verdict.txt`. The mod writes them next to the game exe at
  runtime, so they were left behind after the payload was removed.

### Changed
- Removed recentring from the mod. The `Home` key, the `Ctrl+Shift+T` chord and
  the `[Hotkeys] RecenterKey` setting are gone, and the mod no longer announces a
  CENTER press made in the tracker app. The tracker owns the centre: a second
  centre inside the mod sat in series with it and the two drifted apart, so the
  view could be wrong with no way to tell which side was holding the bad offset.
  Centre the view in your tracker app instead.
- `BAD SHOT` is throttled to one line a second, the same as `SHOT`. It was
  written for every player shot, so an automatic weapon on a build where the
  decoupling is broken produced about 12 MB an hour of it.
- The eight-line frame-verdict block now prints every second for the first
  minute a fault lasts and once a minute after that. A session that stayed
  unhealthy wrote about 7 MB an hour of identical counters.
- The VATS targeting-menu gate reads a mode byte inside the game's VATS
  singleton, located by RTTI at runtime, rather than a `.data` address pinned to
  a PE fingerprint. A game patch now moves it for free, so the overlay no longer
  breaks on every update.
- Replaced `[Sensitivity] RotationSmoothing` and `[Position] Smoothing` with
  `[Sensitivity] LocalSmoothing` (default `0.0`) and `[Sensitivity]
  RemoteSmoothing` (default `0.15`). The mod picks between them per connection
  from the packet's source address, and each covers rotation and position
  together.
- Removed the hidden 0.15 baseline smoothing floor. A tracker running on the
  same machine now gets zero-latency tracking by default instead of being
  silently smoothed against the user's setting.

## [0.0.0] - 2026-05-31

### Added
- Added the initial mod scaffold built on cameraunlock-core.
