## L4FixesVSP

**L4FixesVSP** (`l4fixes_vsp`) is a Linux i386 Valve Server Plugin for native Left 4 Dead 2
engine/game fixes. Its VSP lifecycle and patch ownership follow the approach used by [Tickrate
Enabler Remastered](https://github.com/Krevik/Tickrate-Enabler-Remastered). It does not change the tickrate and can run alongside Tickrate Enabler.

The fixes are instruction patches. No SourcePawn, SourceScramble, SourceMod or Metamod installation
is required, and the plugin performs no per-frame or per-player processing.

### Included fixes

- **Null CUserCmd lag compensation crash** — ports [`l4d2_null_cusercmd_fix.sp`](https://github.com/Krevik/Kether.pl-L4D2-Server/blob/kether_2.0/addons/sourcemod/scripting/l4d2_null_cusercmd_fix.sp) by fdxx.
  Patches `CLagCompensationManager::StartLagCompensation` to jump to its existing return path
  when the null-command path is reached.
- **HLTV client-info crash exploit** — ports [`l4d2_hltv_crash_fix.sp`](https://github.com/Krevik/Kether.pl-L4D2-Server/blob/kether_2.0/addons/sourcemod/scripting/l4d2_hltv_crash_fix.sp) by backwards, ProdigySim
  and A1m`. Patches `CBaseClient::ProcessClientInfo` to force the client-supplied HLTV flag to zero.
- **Boomer ladder fix** — ports [`l4d2_boomer_ladder_fix.sp`](https://github.com/Krevik/Kether.pl-L4D2-Server/blob/kether_2.0/addons/sourcemod/scripting/l4d2_boomer_ladder_fix.sp) by BHaType.
  Removes the same conditional branch in `CTerrorGameMovement::CheckForLadders` as the SourcePawn fix.

The Linux symbols, offsets, verification bytes and replacement bytes are compiled into
`src/fixes.cpp`, directly ported from the corresponding SourcePawn gamedata. This initial version
supports L4D2 Linux x86 only. It does not contain Windows or L4D1 patch definitions.

### Install

1. Build the plugin or obtain its Linux release package.
2. Stop the server and disable the three corresponding SourcePawn plugins (`.smx` files) if they
   are installed. Keep unrelated SourcePawn plugins and Tickrate Enabler as they are.
3. Copy the package's `addons` directory into the server's `left4dead2` directory. The files are
   `addons/l4fixes_vsp.vdf` and `addons/l4fixes_vsp/l4fixes_vsp.so`.
4. Start the server. Check `plugin_print` and the `[L4FixesVSP]` startup messages for the status
   of all three fixes.

All three fixes must validate before the plugin enables any of them. Missing symbols, incompatible
bytes or a write failure cause loading to fail; any patches already applied by this load are rolled
back. Private function symbols must be retained in the engine/server ELF images.

An exact replacement-byte match is treated as an externally owned patch. L4FixesVSP reports it
and leaves it untouched on unload. Other byte sequences must match the gamedata's original verifier. If another
owner changes bytes after L4FixesVSP patches them, restoration refuses to overwrite the foreign
bytes and logs the conflict. Remove the VDF and restart the server to uninstall permanently.

Pausing the VSP keeps its safety fixes enabled. Unloading restores the original bytes of patches
owned by this VSP. The replacement instructions do not reference plugin memory.

### Build and test

Requirements: Linux, Python 3, an HL2SDK checkout on the `l4d2` branch, and a C++20 compiler with
i386 multilib support. On Ubuntu, install `g++-multilib` (and `clang` if using Clang).

```sh
git clone --branch l4d2 https://github.com/alliedmodders/hl2sdk.git ../hl2sdk-l4d2
python3 build.py --sdk ../hl2sdk-l4d2 --build-dir build --tests
```

Use `--cxx clang++` to select Clang, or `--std c++23` to build in C++23 mode. The package is written
to `build/package`. The SDK is used for interface declarations only. By default, `libstdc++` and
`libgcc` are linked statically to avoid srcds loading its bundled older C++ runtime.

Use `--shared-runtime` to link both runtimes dynamically:

```sh
python3 build.py --sdk ../hl2sdk-l4d2 --build-dir build_shared --shared-runtime --tests
```

Shared builds use the runtime libraries resolved by the server's dynamic loader; those libraries
must satisfy the binary's `GLIBCXX` and `CXXABI` requirements. CI tests both runtime variants in
C++20 and C++23 mode and publishes separate `static` and `shared` C++20 packages.

Tests use synthetic shared-library functions, never a running game server. They exercise private
ELF symbol lookup, malformed ELF rejection, all three patch definitions, rollback, external ownership,
page-permission restoration, inaccessible memory and the exported VSP interface. UBSan is enabled
for the test executable.

Build releases on Ubuntu 24.04 to match the glibc ceiling enforced by CI:

```sh
MAX_GLIBC=2.39 bash .github/verify_linux_so.sh build/package/addons/l4fixes_vsp/l4fixes_vsp.so
RUNTIME_LINKAGE=shared MAX_GLIBC=2.39 bash .github/verify_linux_so.sh build_shared/package/addons/l4fixes_vsp/l4fixes_vsp.so
```

### Adding or updating fixes

`src/module.cpp` resolves bounded functions from ELF symbol tables and verifies their live executable
segments. `src/patch.cpp` owns byte validation, temporary page permissions, enabling and restoration.
`src/fixes.cpp` declares the game-specific patches, and `src/plugin.cpp` handles the VSP lifecycle.

Revalidate offsets and instruction semantics against the actual dedicated-server binaries after game
updates. The null-command gamedata supplies only a one-byte verifier (`C7`); that verifier is retained
here, with additional function bounds and branch-target checks. These checks cannot establish that
a changed function still has the same semantics. The other two fixes verify their full replaced
instruction sequences. Add regression fixtures when adding a fix, and keep replacement instructions
independent of the plugin's own address space so cleanup does not leave dangling code references.

### Changelog

1.0.0:
- Ported the null CUserCmd, HLTV crash and boomer ladder SourcePawn fixes to a native Linux VSP.
- Added centralized patch validation, rollback, ownership tracking and unload restoration.
- Added C++20/C++23 builds, Linux packaging and sanitized regression tests.

### Credits and license

Fix authors: fdxx; backwards, ProdigySim and A1m`; BHaType. SourcePawn gamedata was taken from
the corresponding files in the Kether.pl-L4D2-Server project.

Architecture reference: Tickrate Enabler Remastered and
[Tickrate Enabler](https://github.com/accelerator74/Tickrate-Enabler) by ProdigySim and contributors.
See `LICENSE.txt` for the GNU GPL v3 license and applicable terms.
