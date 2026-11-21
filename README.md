# Persona PSP for Nintendo Switch

An unofficial native Switch homebrew port of the US PSP version of *Shin Megami Tensei: Persona*. The build translates the PSP game's MIPS code to C ahead of time, compiles it for ARM64, and links it with Nakagawa Recomp's PSP runtime and a Switch backend. The runtime also has an interpreter fallback.

This repository contains the Switch backend, runtime changes and local build instructions. You supply the matching game files and generate the executable on your computer.

## Compatibility

Version **0.1.6** was tested on Switch hardware with the US Undub variant of **ULUS10432**, version **1.00**. The tester reported smooth gameplay and smooth audio and video in the opening movie before the main menu. Full-game completion and compatibility with other editions are unverified.

## Build on your computer

The tested Windows setup used Git, Python 3.12, the devkitPro Switch toolchain (devkitA64, libnx and switch-tools), and the matching game files. The table gives SHA-256 hashes so you can check your inputs before compiling. The examples below use PowerShell in the repository root.

| Local input | SHA-256 |
| --- | --- |
| Undub disc image, saved as `private/persona.iso` | `aa45ea23cb8115004e8bc5aa99983578541185f6478cbaa5a9ca9cc65ca9a821` |
| Original `PSP_GAME/SYSDIR/EBOOT.BIN` from that image, saved as `private/EBOOT.BIN` | `30990909958a3b6e4c09872abb5626e4c130a3b3e8e84f4bef809465dfa629de` |
| Matching decrypted game executable, saved as `private/game.elf` | `260691d0d0a0f797c71957f11cdbc62666c5f877ec139b2e742ff4c426c03926` |

Use your own local copy of the tested edition. Create a `private` folder, place your disc image there as `persona.iso`, and extract `PSP_GAME/SYSDIR/EBOOT.BIN` from it to `private/EBOOT.BIN`. To produce the matching `game.elf` locally with pspdecrypt on your PATH, run the command below. [pspdecrypt](https://github.com/John-K/pspdecrypt)

```powershell
pspdecrypt -o private/game.elf private/EBOOT.BIN
```
A stock US disc and other Undub revisions have not been verified.

1. Fetch the exact upstream revision and apply the Switch runtime changes.

   ```powershell
   git clone --config core.autocrlf=false https://github.com/Jstar269/Nakagawa-Recomp.git upstream
   git -C upstream checkout --detach 2263985dd8882acbbf1a7313f3752979e7f88e2d
   git -C upstream apply --check ../switch-runtime.patch
   git -C upstream apply ../switch-runtime.patch
   ```

2. Generate the game code and relocated memory image locally.

   ```powershell
   New-Item -ItemType Directory -Force generated | Out-Null
   python upstream/tools/codegen.py private/game.elf generated/persona.c --base=0x08804000 --profile=none --name-imports --funcs-per-chunk=80 --target-chunk-bytes=750000
   python upstream/tools/prxload.py private/game.elf 08804000 --psp-header=private/EBOOT.BIN --out=generated/persona.image.bin
   Get-FileHash generated/persona.image.bin
   ```

   The expected image hash is `48b734fc2fb39ab867b6b568a35e07eda31de326aeb071d3d4b2110e672ca219`.

3. With `DEVKITPRO` set to the SDK root, run the build. You can also pass the root with `--devkitpro PATH`.

   ```powershell
   python build.py
   ```

   The executable is written to `build/Persona.nro`. This recipe omits the disc-derived homebrew icon.

## On the Switch

For an existing working installation, replace **only** `SD:/switch/persona-runtime/Persona.nro` with your locally built NRO.

For a fresh setup, place the following in `SD:/switch/persona-runtime/` and launch the NRO in full-memory homebrew application mode.

| File or folder | Where to get it |
| --- | --- |
| `Persona.nro` | Your local `build/` directory |
| `persona.iso` | Your matching local disc image |
| `persona.image.bin` | Your local `generated/` directory |
| `assets/vfpu/` | Copy `upstream/assets/vfpu/`, including its provenance file |
| `font/` | Copy the generated PGF fonts and their accompanying notices from the matching Nakagawa runtime distribution, or build the replacement fonts as described in `upstream/font/README.md` |

Game saves live under `memstick/PSP/SAVEDATA/` in the installation directory. If you report a problem, include your port version, which scene it happens in, and the relevant lines from `runtime.log`.

## Credits and licensing

ATLUS created the original game. This independent fan project uses Nakagawa Recomp, work from sal063's PSP Recompilation Project and PPSSPP, FFmpeg's codecs, and the devkitPro and switchbrew toolchains. The Switch dialogs use glyphs derived from Geist Mono. The port is unofficial.

Nakagawa Recomp is distributed under GPL-3.0-or-later. Its individual source files and the other third-party components retain their respective licenses and copyright notices. These licenses cover the software identified in the notices. The rights to the original game code, artwork, music and other game content remain with their respective owners. Players supply their own matching game files and generate the executable locally. [Credits and upstream notices](CREDITS.md) · [GNU GPL v3 text](LICENSE)

Reverse-engineering and development tools included REA, Ghidra and OpenAI Codex.

The Switch changes dated 2026-11-21 are distributed as platform source in `port/` and as `switch-runtime.patch` against the pinned Nakagawa revision. They adapt storage, scheduling, input, display and audio to the Switch and optimize the software renderer and opening movie. Preserve the upstream notices and individual file headers when applying these changes. The upstream credits document also identifies contributors to individual components, including JPCSP and uOFW. [Upstream credits](https://github.com/Jstar269/Nakagawa-Recomp/blob/2263985dd8882acbbf1a7313f3752979e7f88e2d/CREDITS.md) · [Upstream notices](https://github.com/Jstar269/Nakagawa-Recomp/blob/2263985dd8882acbbf1a7313f3752979e7f88e2d/NOTICE.md)
