# Credits

| Contributor | Contribution |
| --- | --- |
| ATLUS and the original Persona development team | Original PlayStation Portable game, story, music and artwork |
| This port's maintainer | Project direction, Switch hardware testing and feedback on gameplay, audio and opening-movie playback |
| Jstar269 and the Nakagawa Recomp contributors | Static recompilation tooling, PSP runtime, renderer and media integration. [Pinned source](https://github.com/Jstar269/Nakagawa-Recomp/tree/2263985dd8882acbbf1a7313f3752979e7f88e2d) |
| sal063 and the PSP Recompilation Project authors | Foundational recompiler and runtime used by Nakagawa. [Project](https://github.com/sal063/PSP-recompilation-project) |
| Henrik Rydgård and the PPSSPP contributors | PSP runtime, graphics and VFPU work inherited through the upstream projects. [Project](https://github.com/hrydgard/ppsspp) |
| FFmpeg contributors | H.264 movie and ATRAC audio decoder code used by the runtime. [FFmpeg](https://ffmpeg.org/) |
| devkitPro and switchbrew contributors | devkitA64, libnx and switch-tools used to build the Switch application. [devkitPro](https://devkitpro.org/) · [libnx](https://github.com/switchbrew/libnx) · [switch-tools](https://github.com/switchbrew/switch-tools) |
| John-K and the pspdecrypt contributors | Tool used to prepare the local PSP executable. [Project and credits](https://github.com/John-K/pspdecrypt) |
| The Geist Project Authors | Geist Mono, the source of the dialog glyphs. [Font license](port/GeistMono-OFL.txt) |
| Undub patch contributors | The hardware test used the US Undub variant. The original patch authorship has not been verified for this exact input. |

Reverse-engineering and development tools included REA, Ghidra and OpenAI Codex.

The Switch changes dated 2026-11-21 are distributed as platform source in `port/` and as `switch-runtime.patch` against the pinned Nakagawa revision. They adapt storage, scheduling, input, display and audio to the Switch and optimize the software renderer and opening movie. Preserve the upstream notices and individual file headers when applying these changes. The upstream credits document also identifies contributors to individual components, including JPCSP and uOFW. [Upstream credits](https://github.com/Jstar269/Nakagawa-Recomp/blob/2263985dd8882acbbf1a7313f3752979e7f88e2d/CREDITS.md) · [Upstream notices](https://github.com/Jstar269/Nakagawa-Recomp/blob/2263985dd8882acbbf1a7313f3752979e7f88e2d/NOTICE.md)
