"""Static RTTI -> vtable -> vfunc-RVA discovery for an on-disk PE.

SteamStub wraps only .text; .rdata (RTTI type descriptors, COLs, vtables) stays
plaintext, and vtable slots hold valid function VAs even though the .text bytes
they point at are encrypted at rest. So the PlayerCamera vtable and the RVA of
each virtual (including Update) are recoverable purely statically.
"""
import sys, struct
import pefile

TARGETS = [
    ".?AVPlayerCamera@@",
    ".?AVTESCamera@@",
    ".?AVNiCamera@@",
    ".?AVThirdPersonState@@",
    ".?AVFirstPersonState@@",
    ".?AVPlayerCharacter@@",
]

# Classes whose vtable is large (Actor-derived); dump far enough to reach the
# Update slot. Others only need the first handful of slots.
DEEP_DUMP = {
    ".?AVPlayerCharacter@@": [0xCF, 0xE7],  # Actor::Update / GetEyeVector (CommonLibF4)
}

def main(path):
    pe = pefile.PE(path, fast_load=True)
    data = pe.__data__
    base = pe.OPTIONAL_HEADER.ImageBase
    print(f"ImageBase = 0x{base:X}")

    # rva<->offset helpers spanning all sections
    def off_to_rva(o):
        return pe.get_rva_from_offset(o)
    def rva_to_off(r):
        return pe.get_offset_from_rva(r)

    # Pre-extract .rdata bounds (where RTTI/vtables live)
    sections = [(s.Name.rstrip(b"\x00").decode(errors="replace"),
                 s.VirtualAddress, s.Misc_VirtualSize,
                 s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
    print("Sections:")
    for n, va, vs, pr, sr in sections:
        print(f"  {n:10s} rva=0x{va:08X} vsize=0x{vs:08X} raw=0x{pr:08X}/0x{sr:08X}")

    for cls in TARGETS:
        print("\n" + "=" * 70)
        print(cls)
        needle = cls.encode("ascii")
        pos = data.find(needle)
        if pos < 0:
            print("  string not found"); continue
        name_rva = off_to_rva(pos)
        type_desc_rva = name_rva - 0x10  # TypeDescriptor { vftable, spare, name[] }
        print(f"  name @file 0x{pos:X} rva 0x{name_rva:X}  -> TypeDescriptor rva 0x{type_desc_rva:X}")

        # Find CompleteObjectLocator referencing this TypeDescriptor.
        col_rva = None
        for n, va, vs, pr, sr in sections:
            if n not in (".rdata", "_RDATA", ".data"):
                continue
            seg = data[pr:pr + sr]
            i = 0
            while i + 24 <= len(seg):
                sig, offv, cd, ptd, pcd, pself = struct.unpack_from("<IIIIII", seg, i)
                cand_rva = va + i
                if sig == 1 and ptd == type_desc_rva and pself == cand_rva:
                    col_rva = cand_rva
                    print(f"  COL rva 0x{col_rva:X} (sig=1 offset={offv} cd={cd} pClassDesc rva 0x{pcd:X})")
                    break
                i += 4
            if col_rva is not None:
                break
        if col_rva is None:
            print("  COL not found"); continue

        # Find the meta pointer (abs VA == base+col_rva); vtable = that + 8.
        col_va = base + col_rva
        target = struct.pack("<Q", col_va)
        vtable_rva = None
        for n, va, vs, pr, sr in sections:
            if n not in (".rdata", "_RDATA", ".data"):
                continue
            seg = data[pr:pr + sr]
            j = seg.find(target)
            while j >= 0:
                if j % 8 == 0:  # vtable meta-ptrs are 8-aligned
                    vtable_rva = va + j + 8
                    break
                j = seg.find(target, j + 1)
            if vtable_rva is not None:
                break
        if vtable_rva is None:
            print("  vtable not found"); continue
        print(f"  *** vtable rva 0x{vtable_rva:X}  (VA 0x{base + vtable_rva:X}) ***")

        # Dump vtable entries as function RVAs.
        voff = rva_to_off(vtable_rva)
        print("  vfuncs:")
        deep = DEEP_DUMP.get(cls)
        if deep:
            for k in deep:
                (fn_va,) = struct.unpack_from("<Q", data, voff + k * 8)
                fn_rva = fn_va - base
                ok = 0 <= fn_rva < pe.OPTIONAL_HEADER.SizeOfImage
                print(f"    [0x{k:02X}] rva 0x{fn_rva:08X}{'' if ok else '  <-- out of image'}")
        else:
            for k in range(12):
                (fn_va,) = struct.unpack_from("<Q", data, voff + k * 8)
                if fn_va == 0:
                    print(f"    [{k:2d}] 0x0 (end)"); break
                fn_rva = fn_va - base
                ok = 0 <= fn_rva < pe.OPTIONAL_HEADER.SizeOfImage
                print(f"    [{k:2d}] rva 0x{fn_rva:08X}{'' if ok else '  <-- out of image'}")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4\Fallout4.exe")
