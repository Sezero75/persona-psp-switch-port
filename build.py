"""Build the Persona AOT runtime from bundled sources using devkitPro."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
UPSTREAM = ROOT / "upstream"
RUNTIME = UPSTREAM / "src/rt"
PORT = ROOT / "port"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--devkitpro", default=os.environ.get("DEVKITPRO"))
parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
parser.add_argument("-j", "--jobs", type=int, default=min(6, os.cpu_count() or 1))
args = parser.parse_args()
if not args.devkitpro:
    parser.error("Set DEVKITPRO or pass --devkitpro with the SDK root.")
sdk = Path(args.devkitpro).resolve()
build = args.build_dir.resolve()
build.mkdir(parents=True, exist_ok=True)
ext = ".exe" if os.name == "nt" else ""
gcc = sdk / ("devkitA64/bin/aarch64-none-elf-gcc" + ext)
tool_dir = sdk / "tools/bin"
env = dict(os.environ, DEVKITPRO=sdk.as_posix(), PATH=str(gcc.parent) + os.pathsep + str(tool_dir) + os.pathsep + os.environ["PATH"])
sources = json.loads((ROOT / "sources.json").read_text())
arch = ["-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIC", "-ftls-model=local-exec"]
base = arch + ["-std=gnu11", "-O2", "-fno-strict-aliasing", "-ffunction-sections", "-fdata-sections",
               "-fno-asynchronous-unwind-tables", "-D__SWITCH__", "-DSR_CORO_BACKEND_ASM",
               "-DSR_H264_FFMPEG", "-D_GNU_SOURCE", "-D_POSIX_C_SOURCE=200809L"]
includes = [sdk / "libnx/include", RUNTIME, UPSTREAM / "src/core", UPSTREAM / "src/ui", PORT,
            ROOT / "generated", RUNTIME / "atrac3p", RUNTIME / "atrac3p/libavcodec", RUNTIME / "atrac3p/libavutil"]
base += ["-ffile-prefix-map=" + str(ROOT) + "=.", "-ffile-prefix-map=" + str(sdk) + "=devkitpro"]
base += ["-I" + str(path) for path in includes]
h264 = RUNTIME / "h264_ffmpeg_sub"
jobs = []
for source in sources["runtime"] + sources["atrac"] + sources["h264"]:
    flags = []
    if source == "src/rt/driver.c":
        flags.append("-Dmain=nx_persona_driver")
    if source in sources["h264"]:
        flags = ["-I" + str(h264), "-DHAVE_AV_CONFIG_H", "-D_ISOC99_SOURCE", "-D_FILE_OFFSET_BITS=64",
                 "-D_LARGEFILE_SOURCE", "-include", str(h264 / "nk_h264_symbols.h")]
    jobs.append((UPSTREAM / source, flags))
jobs += [(path, []) for prefix in (ROOT / "generated", PORT) for path in sorted(prefix.glob("*.c"))]

def compile_unit(job):
    source, flags = job
    obj = build / (source.relative_to(ROOT).as_posix().replace("/", "_") + ".o")
    subprocess.run([gcc, *flags, *base, "-w", "-c", source, "-o", obj], env=env, check=True)
    return obj

with concurrent.futures.ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:
    objects = list(pool.map(compile_unit, jobs))
rsp = build / "objects.rsp"
rsp.write_text("\n".join('"' + p.as_posix() + '"' for p in objects) + "\n")
elf = build / "Persona.elf"
nro = build / "Persona.nro"
nacp = build / "Persona.nacp"
subprocess.run([gcc, *arch, "-specs=" + str(sdk / "libnx/switch.specs"), "-Wl,--gc-sections",
                "-Wl,-Map=" + str(build / "Persona.map"), "-L" + str(sdk / "libnx/lib"), "-o", elf,
                "@" + str(rsp), "-lnx", "-lm"], env=env, check=True)
subprocess.run([tool_dir / ("nacptool" + ext), "--create", "Persona", "Native reconstruction", "0.1.6", nacp],
               env=env, check=True)
subprocess.run([tool_dir / ("elf2nro" + ext), elf, nro,
                "--nacp=" + str(nacp)], env=env, check=True)
print(f"Built {nro} from {len(jobs)} translation units.")



