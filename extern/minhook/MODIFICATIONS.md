# Modifications to the vendored MinHook copy

MinHook is BSD-2-Clause. That licence permits modification, and requires that
the copyright notice, the list of conditions and the disclaimer travel with the
source. `LICENSE.txt` in this directory is the upstream file reproduced
verbatim, including both copyright holders: Tsuda Kageyu for MinHook, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from.

This file records every way the copy here differs from upstream, so nobody
mistakes it for an unmodified snapshot.

## Provenance

- Upstream: https://github.com/TsudaKageyu/minhook
- Base: `v1.3.4`, commit `05c06c5bbca226b72ffb40fc0caaef33bcaf6f74`
  (`v1.3.4-13-g05c06c5`)

Every file here that is not listed below is byte-identical to that commit.

## Changed files

### `src/hook.c`

- `MH_Initialize` takes the process heap via `GetProcessHeap()` instead of
  standing up a private one with `HeapCreate`, and `MH_Uninitialize` therefore
  skips the matching `HeapDestroy`.
- `g_isLocked`, `g_hHeap` and the `g_hooks` struct lost their `static` storage
  class.
- Two casts on jump operands changed from `INT32` / `INT8` to `UINT32` / `UINT8`
  to match the struct field types below.

### `src/trampoline.h`

- The relative-displacement fields of `JMP_REL_SHORT`, `JMP_REL` and `JCC_REL`
  changed from `INT8` / `INT32` to `UINT8` / `UINT32`.

### `src/trampoline.c`

- Four casts on those same operands changed from `INT32` to `UINT32`. All four
  sit in x86-only branches, which this x64 build never compiles.

### `src/buffer.c`

- `g_pMemoryBlocks` lost its `static` storage class.

### `include/MinHook.h`

- Whitespace only, in the declaration of `MH_StatusToString`.

## Files not carried over

Only the headers and translation units this mod compiles were taken. Upstream's
`.editorconfig`, `.github/`, `.gitignore`, `CMakeLists.txt`, `README.md`,
`build/`, `cmake/` and `dll_resources/` are not reproduced here. `LICENSE.txt`
and `AUTHORS.txt` are.
