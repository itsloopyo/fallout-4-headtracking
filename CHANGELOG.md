# Changelog

All notable changes to this mod are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- The previous session's log is kept as `HeadTracking.prev.log`. The log is
  rewritten on every launch, so a crash report sent after a relaunch used to
  arrive with the crashed session already gone. A rename that fails is reported
  in the fresh log, so a stale `.prev.log` is never mistaken for the last
  session.

### Fixed
- VATS body-part percentages sit on the target again instead of being offset by
  however far the head is turned. The gate that takes the head pose off before
  VATS freezes the game thread was pinned to a per-build address, so the Fallout
  4 1.11.240 patch left it dormant and the overlay kept the projection it was
  laid out with.

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
