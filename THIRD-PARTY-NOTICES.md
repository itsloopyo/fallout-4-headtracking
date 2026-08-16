# Third-Party Notices

Fallout 4 Head Tracking itself is MIT licensed, Copyright (c) 2026 itsloopyo /
CameraUnlock - see [LICENSE](LICENSE).

This mod uses or bundles the following third-party components, each under its
own license.

## Ultimate ASI Loader

- **Version:** v9.7.2 (commit `ab722befd52581a34449b603926cfab476e66b05`)
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** the upstream `dinput8.dll` binary is dropped into the game's exe
  directory as `dxgi.dll` (the proxy slot Fallout4.exe actually imports) to load
  `Fallout4HeadTracking.asi`. See `vendor/ultimate-asi-loader/` for the full
  upstream snapshot (binary, LICENSE, README with tag/SHA).
- **Bundled:** yes. Shipped in the release ZIP and used as the install-time
  source.

Copyright (c) 2023 ThirteenAG

---

## MinHook

- **Version:** vendored source snapshot (`extern/minhook`)
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Statically linked into `Fallout4HeadTracking.asi` for runtime
  function hooking.
- **Bundled:** no. Compiled into the mod binary.

Copyright (C) 2009-2017 Tsuda Kageyu. All rights reserved.

---

## OpenTrack

- **Version:** n/a (wire protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** Wire protocol only. We listen for OpenTrack-format UDP packets;
  no OpenTrack code is bundled.
- **Bundled:** no.

---

## CameraUnlock Core

- **Version:** git submodule (`main` branch)
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared library, included as a git submodule and compiled into
  the mod.
- **Bundled:** no. Compiled into the mod binary.

---

## Credits

Fallout 4 (c) Bethesda Game Studios / Bethesda Softworks. This mod is an
unofficial fan project and is not affiliated with or endorsed by Bethesda.
Requires a legitimately purchased copy of the game.
