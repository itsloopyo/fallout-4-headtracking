# Third-Party Notices

Fallout4HeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

Nothing in this repository is derived from, or redistributes any part of,
Fallout 4.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| Ultimate ASI Loader | v9.7.2 | MIT | Bundled verbatim in the installer ZIP |
| MinHook | v1.3.4 (`05c06c5`), modified | BSD-2-Clause | Compiled into `Fallout4HeadTracking.asi` |
| CommonLibF4 | n/a | MIT | Not bundled; a reference for engine struct offsets |
| CommonLibSSE-NG | n/a | MIT | Not bundled; cross-checked the same offsets |
| AutoBeam | n/a | see upstream | Not bundled; corroborated one engine finding |
| cameraunlock-core | bf7d0ee00079cf2ee0f2323d45305527b75adc46 | MIT | Compiled into `Fallout4HeadTracking.asi` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |

---

## Ultimate ASI Loader

Vendored at `vendor/ultimate-asi-loader/`, shipped in the installer ZIP and used as the
install-time source. Taken from the upstream release asset untouched; the
upstream licence file ships beside it at `vendor/ultimate-asi-loader/LICENSE`.

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Version: `v9.7.2`
- Commit: `ab722befd52581a34449b603926cfab476e66b05`
- SHA-256: `22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c`

```
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## MinHook

Source committed at `extern/minhook/` and compiled into `Fallout4HeadTracking.asi`. The
committed tree is the authoritative record of exactly what is built.

- Upstream: https://github.com/TsudaKageyu/minhook
- Base version: `v1.3.4`
- Base commit: `05c06c5bbca226b72ffb40fc0caaef33bcaf6f74` (`v1.3.4-13-g05c06c5`)

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from. Both notices appear below exactly as upstream ships them, and the same two
appear verbatim in `extern/minhook/LICENSE.txt`, which is the upstream file
unaltered.

**This copy is modified.** BSD-2-Clause permits that, and asks only that the
notice, the conditions and the disclaimer travel with the source, which they do.
The changes are listed in full in `extern/minhook/MODIFICATIONS.md` in the
project repository, so nothing here is mistaken for a claim of an unmodified
snapshot. In summary: `MH_Initialize` takes the process heap rather than
creating a private one (and `MH_Uninitialize` skips the matching `HeapDestroy`),
several jump-operand fields and their casts changed from signed to unsigned, and
four globals lost their `static` storage class. Only the headers and translation
units this mod compiles were taken; upstream's build files and documentation are
not reproduced, though `LICENSE.txt` and `AUTHORS.txt` are.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## CommonLibF4 and CommonLibSSE-NG

Neither is bundled, linked, vendored, submoduled or compiled into anything we
ship. No source from either was copied. They are credited because
`src/game/fallout4_types.h` and `scripts/static_rtti_scan.py` say plainly where
some of their numbers came from, and a project that names its sources in the
code should name them here too.

- CommonLibF4 - https://github.com/Ryan-rsm-McKenzie/CommonLibF4 (MIT), by
  Ryan-rsm-McKenzie. The source of the `NiAVObject` / `NiNode` / `NiCamera` /
  `TESCamera` field offsets and of the `Actor::Update` and `TESCamera::Update`
  vtable indices this mod hooks.
- CommonLibSSE-NG - https://github.com/alandtse/CommonLibVR (MIT), a maintained
  fork of Ryan-rsm-McKenzie's CommonLibSSE. Used to cross-check those offsets
  against the Skyrim layout, which is where the "Skyrim: 0x..." comparisons in
  `fallout4_types.h` come from.

What was taken is a set of integers describing a third party's binary layout.
Facts of that kind are not the expressive work an MIT licence attaches to, so
this section is attribution rather than a licence obligation being discharged.
Both projects are MIT regardless, which would permit the use with attribution
even if it were.

---

## AutoBeam

Not bundled, not linked, no code copied. Credited because a comment in
`src/hooks/aim_decoupling.cpp` cites this Fallout 4 mod as one of two
independent confirmations that the projectile launch path reads its direction
from the same camera node we found, and the confirmation was worth having.

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `Fallout4HeadTracking.asi`. Our own code,
MIT licensed, reproduced here so the notices are complete.

- Pinned commit: `bf7d0ee00079cf2ee0f2323d45305527b75adc46`

```
MIT License

Copyright (c) 2026 CameraUnlock

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Fallout 4 footage and screenshots

- **Files:** `assets/readme-clip.gif`
- **Rights holder:** the developers and publishers of Fallout 4, together with the
  rights holders of any third-party marks visible in frame.
- **Usage:** recorded from the game running with this mod, captured on a
  legitimately purchased copy, shown so a reader can see what the mod does
  before installing it.
- **Bundled:** `assets/readme-clip.gif`: kept in this repository only. The packaging scripts
  ship no part of `assets/`, so these are in neither release ZIP nor
  anything the launcher deploys.
- **Licence:** none is granted or implied by this repository. This material is
  not covered by the MIT licence in `LICENSE`, and nothing here permits reuse
  of it. Rights holders who would rather it were not published: open an issue
  or reach us on Discord and it comes down.

---

## Fallout 4

Fallout 4 and all related names, logos, characters and marks are trademarks of
their respective owners. They are used here only to identify the game this mod
applies to, which is nominative use and not a claim of any right in them. This
project is an unofficial, fan-made modification. It is not affiliated with,
endorsed by, or sponsored by the game's developers, its publishers, its engine
vendor, or any other rights holder. It redistributes no game code, no game
assets and no proprietary DLLs, and it requires a legitimately purchased copy
of the game.

Any engine structure offsets, function addresses and byte patterns in the source
were derived by the authors through independent analysis of a legitimately owned
copy. To be precise about what that leaves in the repository, rather than resting
on a broad denial:

- **Offsets, vtable indices and RVAs are numbers.** They record where a field or
  a function sits in a binary. Nothing of the game is reproduced by them.
- **The six hook signatures in `src/hooks/` are short byte strings**, 18 to 31
  bytes each, and every one of them is a compiler-emitted function prologue with
  the frame sizes and call displacements masked out. They exist solely to locate
  a function at runtime, they carry none of what the function does, and a handful
  of `push`/`sub rsp`/`mov` bytes is the smallest thing that can do that job. A
  few source comments give the corresponding mnemonics so a reader can see what
  is being matched.
- **No decompiler or disassembler output is stored here.** No game source, no
  reconstructed function bodies, no assembly listings, no headers lifted from the
  game, no assets, and no part of any game binary beyond the signatures described
  above. Nothing this project ships contains any of it either.

Analysis of a lawfully obtained program to achieve interoperability with it is
what this is, and the results kept here are the minimum needed for the mod to
find its way around at runtime.
