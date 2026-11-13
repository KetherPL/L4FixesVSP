## L4FixesVSP

**L4FixesVSP** (`l4fixes_vsp`) is a Linux i386 Valve Server Plugin for native Left 4 Dead 2
engine/game fixes. Its VSP lifecycle and patch ownership follow the approach used by [Tickrate
Enabler Remastered](https://github.com/Krevik/Tickrate-Enabler-Remastered). It does not change the tickrate and can run alongside Tickrate Enabler.

The fixes are instruction patches; the plugin also adds one server command, `te_reliable_sound`. No
SourcePawn, SourceScramble, SourceMod or Metamod installation is required, and the plugin performs no
per-frame or per-player processing.

### Included fixes

- **Null CUserCmd lag compensation crash** — ports [`l4d2_null_cusercmd_fix.sp`](https://github.com/Krevik/Kether.pl-L4D2-Server/blob/kether_2.0/addons/sourcemod/scripting/l4d2_null_cusercmd_fix.sp) by fdxx.
  Patches `CLagCompensationManager::StartLagCompensation` to jump to its existing return path
  when the null-command path is reached.
- **HLTV client-info crash exploit** — ports [`l4d2_hltv_crash_fix.sp`](https://github.com/Krevik/Kether.pl-L4D2-Server/blob/kether_2.0/addons/sourcemod/scripting/l4d2_hltv_crash_fix.sp) by backwards, ProdigySim
  and A1m`. Patches `CBaseClient::ProcessClientInfo` to force the client-supplied HLTV flag to zero.
- **Boomer ladder fix** — ports [`l4d2_boomer_ladder_fix.sp`](https://github.com/Krevik/Kether.pl-L4D2-Server/blob/kether_2.0/addons/sourcemod/scripting/l4d2_boomer_ladder_fix.sp) by BHaType.
  Removes the same conditional branch in `CTerrorGameMovement::CheckForLadders` as the SourcePawn fix.
- **"No Steam logon" kicks** — moved from Tickrate Enabler Remastered 1.9.0. Players are no longer
  kicked when Steam answers their auth ticket with 1 (not connected to Steam), 6 (ticket canceled),
  7 (ticket already used) or 8 (ticket invalid). See ["No Steam logon" kicks](#no-steam-logon-kicks).
- **`te_reliable_sound`** — moved from Tickrate Enabler Remastered 1.8.0. A server command that plays a
  sound on the reliable stream, so packet loss can't drop it. See
  [Reliable sounds](#reliable-sounds-te_reliable_sound).

The Linux symbols, offsets, verification bytes and replacement bytes are compiled into
`src/fixes.cpp`; the first three are ported from the corresponding SourcePawn gamedata. This version
supports L4D2 Linux x86 only. It does not contain Windows or L4D1 patch definitions.

#### "No Steam logon" kicks

Steam answers every connecting client's auth ticket, and the engine
(`CSteam3Server::OnValidateAuthTicketResponseHelper`) kicks the client with "No Steam logon" when the
answer is 1, 6, 7 or 8. Players who are logged in to Steam get these too, e.g. after joining from a
lobby, and then keep getting them until they restart the game or Steam (reported in
[ValveSoftware/Source-1-Games#7758](https://github.com/ValveSoftware/Source-1-Games/issues/7758)).

The engine already skips that kick on servers that run without Steam authentication; the patch turns
the conditional jump into that path into an unconditional one, so those four answers no longer kick on
any server. The engine still prints every failed answer (`STEAMAUTH: Client <name> received failure
code <n>`) to the console and the server log. The other answers (no license, VAC ban, account used
elsewhere, VAC check timeout) are kicked as before.

What it costs: the server stops enforcing those answers.

- A client Steam validated earlier in the connection keeps its verified Steam ID; nothing changes for it.
- A client Steam never validated stays on the server with the Steam ID its own ticket claims. The engine
  still reports it as not authenticated, so SourceMod doesn't authorize it: no admin rights and no
  `OnClientAuthorized` / `OnClientPostAdminCheck`, which also means SourceMod-side ban checks don't run
  for it. Someone without a valid Steam session can therefore stay and play.

#### Reliable sounds (te_reliable_sound)

Every sound the game or SourceMod plays is unreliable: it is sent once, inside the next snapshot, and a
lost packet loses it for good. `te_reliable_sound` makes the engine send the sound on the reliable
stream (retransmitted until it arrives) to each recipient right away:

    te_reliable_sound <entity> <channel 0-7> <soundlevel 0-255> <volume 0-1> <pitch 1-255> <flags> <x,y,z|-> "<sample>" [client,client,...]

`entity` is the source (0 = world), `-` instead of an origin uses the entity's own position, and the
optional client list (indices) limits the recipients; by default every connected human and SourceTV
get it. The arguments match `EmitSound` / a SourceMod normal sound hook, so a hooked sound can be
re-emitted unchanged, only reliably. Only the server (console, rcon, plugins via `ServerCommand`) can
run it. The name and arguments are the same as in Tickrate Enabler Remastered 1.8.0-1.9.0, so plugins
written for it keep working.

- The sample must already be precached (the command never precaches; an unknown sample is rejected
  with a console message). Put it in quotes, as above.
- To make a hooked sound reliable, block it in the hook (`Plugin_Stop`) and run `te_reliable_sound` on
  the next frame. Returning `Plugin_Changed` from a SourceMod sound hook re-emits the sound through
  SourceMod, which is never reliable.

### Install

1. Build the plugin or obtain its Linux release package.
2. Stop the server and disable the three corresponding SourcePawn plugins (`.smx` files) if they
   are installed. Keep unrelated SourcePawn plugins and Tickrate Enabler as they are.
   Tickrate Enabler Remastered 1.9.0 also contains the "No Steam logon" fix and `te_reliable_sound`;
   start it with `-nosteamlogonfix -noreliablesound` so each feature has one owner. Running both
   anyway is safe: the two Steam logon patches change different instructions, and the engine keeps
   whichever `te_reliable_sound` was registered first.
3. Copy the package's `addons` directory into the server's `left4dead2` directory. The files are
   `addons/l4fixes_vsp.vdf` and `addons/l4fixes_vsp/l4fixes_vsp.so`.
4. Start the server. Check `plugin_print` and the `[L4FixesVSP]` startup messages for the status
   of all fixes and the command.

All fixes must validate before the plugin enables any of them. Missing symbols or interfaces,
incompatible bytes or a write failure cause loading to fail; any patches already applied by this load
are rolled back. Private function symbols must be retained in the engine/server ELF images. If another
plugin already registered `te_reliable_sound`, L4FixesVSP keeps that command and says so at startup.

An exact replacement-byte match is treated as an externally owned patch. L4FixesVSP reports it
and leaves it untouched on unload. Other byte sequences must match the gamedata's original verifier. If another
owner changes bytes after L4FixesVSP patches them, restoration refuses to overwrite the foreign
bytes and logs the conflict. Remove the VDF and restart the server to uninstall permanently.

Pausing the VSP keeps its fixes and the command enabled. Unloading restores the original bytes of
patches owned by this VSP and unregisters the command. The replacement instructions do not reference
plugin memory.

### Build and test

Requirements: Linux, Python 3, an HL2SDK checkout on the `l4d2` branch, and a C++20 compiler with
i386 multilib support. On Ubuntu, install `g++-multilib` (and `clang` if using Clang).

```sh
git clone --branch l4d2 https://github.com/alliedmodders/hl2sdk.git ../hl2sdk-l4d2
python3 build.py --sdk ../hl2sdk-l4d2 --build-dir build --tests
```

Use `--cxx clang++` to select Clang, or `--std c++23` to build in C++23 mode. The package is written
to `build/package`. The SDK provides the interface declarations and, for the `te_reliable_sound`
console command, its `lib/linux` libraries: `tier1_i486.a` is linked statically, and the plugin links
against `libtier0_srv.so` (and, if needed, `libvstdlib_srv.so`), which srcds has already loaded.
By default, `libstdc++` and `libgcc` are linked statically to avoid srcds loading its bundled older
C++ runtime.

Use `--shared-runtime` to link both runtimes dynamically:

```sh
python3 build.py --sdk ../hl2sdk-l4d2 --build-dir build_shared --shared-runtime --tests
```

Shared builds use the runtime libraries resolved by the server's dynamic loader; those libraries
must satisfy the binary's `GLIBCXX` and `CXXABI` requirements. CI tests both runtime variants in
C++20 and C++23 mode and publishes separate `static` and `shared` C++20 packages.

Tests use synthetic shared-library functions, never a running game server. They exercise private
ELF symbol lookup, malformed ELF rejection, all patch definitions, rollback, external ownership,
page-permission restoration, inaccessible memory, `te_reliable_sound` argument parsing and emission
(against a fake `IEngineSound`) and the exported VSP interface. UBSan is enabled for the test
executable.

Build releases on Ubuntu 24.04 to match the glibc ceiling enforced by CI:

```sh
MAX_GLIBC=2.39 bash .github/verify_linux_so.sh build/package/addons/l4fixes_vsp/l4fixes_vsp.so
RUNTIME_LINKAGE=shared MAX_GLIBC=2.39 bash .github/verify_linux_so.sh build_shared/package/addons/l4fixes_vsp/l4fixes_vsp.so
```

### Adding or updating fixes

`src/module.cpp` resolves bounded functions from ELF symbol tables and verifies their live executable
segments. `src/patch.cpp` owns byte validation, temporary page permissions, enabling and restoration.
`src/fixes.cpp` declares the game-specific patches, `src/reliablesound.cpp` implements
`te_reliable_sound`, and `src/plugin.cpp` handles the VSP lifecycle and the command registration.

Revalidate offsets and instruction semantics against the actual dedicated-server binaries after game
updates. The null-command gamedata supplies only a one-byte verifier (`C7`); that verifier is retained
here, with additional function bounds and branch-target checks. These checks cannot establish that
a changed function still has the same semantics. The other fixes verify their full replaced
instruction sequences; a fix that installs a relative jump declares its position (`branch`) so its
target is checked against the function bounds. Add regression fixtures when adding a fix, and keep replacement instructions
independent of the plugin's own address space so cleanup does not leave dangling code references.

### Changelog

Unreleased:
- "No Steam logon" kick fix (moved from Tickrate Enabler Remastered 1.9.0, as a one-byte engine patch
  that keeps the engine's own failure log).
- `te_reliable_sound` server command (moved from Tickrate Enabler Remastered, same name and arguments).
- The plugin links the SDK's tier1 statically and libtier0 for the console command.

1.0.0:
- Ported the null CUserCmd, HLTV crash and boomer ladder SourcePawn fixes to a native Linux VSP.
- Added centralized patch validation, rollback, ownership tracking and unload restoration.
- Added C++20/C++23 builds, Linux packaging and sanitized regression tests.

### Credits and license

Fix authors: fdxx; backwards, ProdigySim and A1m`; BHaType. SourcePawn gamedata was taken from
the corresponding files in the Kether.pl-L4D2-Server project. The "No Steam logon" fix and
`te_reliable_sound` come from Tickrate Enabler Remastered.

Architecture reference: Tickrate Enabler Remastered and
[Tickrate Enabler](https://github.com/accelerator74/Tickrate-Enabler) by ProdigySim and contributors.
See `LICENSE.txt` for the GNU GPL v3 license and applicable terms.
