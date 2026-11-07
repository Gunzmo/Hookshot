"""Exercise native host boundaries without a game dump or a window server."""
import os
import pathlib
import shlex
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nx2"))
import rtgen

with tempfile.TemporaryDirectory(prefix="hookshot-host-test-") as temp:
    rtgen.write(temp)
    for name in ("a64_suyu.h", "a64_fpx.h"):
        pathlib.Path(temp, name).write_text("uint64_t recomp_cntpct(Ctx *);\n" if name == "a64_suyu.h" else "")
    flags = [os.environ.get("CC", "cc"), "-D_GNU_SOURCE", "-DA64_MEM_ZERO", "-O2", "-std=gnu11",
             "-Werror=implicit-function-declaration", "-ffunction-sections", "-fdata-sections",
             "-I" + temp, "-I" + str(ROOT / "host"), "-Wl,--gc-sections", "-pthread"]
    exe = pathlib.Path(temp, "test_host")
    subprocess.run(flags + [str(ROOT / "tests/test_host_linux.c"),
                           *[str(ROOT / "host" / name) for name in
                             ("hos_platform.c", "hos_thread.c", "hos_hostcall.c", "nn_os_sync.c")],
                           "-lm", "-o", str(exe)], check=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(("HOS_", "HOOKSHOT_", "SDL_"))}
    env.update(HOS_SPIN_US="0", HOS_SHORT_SLEEP_TIMER="1")
    subprocess.run([str(exe)], env=env, timeout=30, check=True)
    subprocess.run([str(exe)], env=dict(env, HOS_VCORES="1", HOS_CORES="1"), timeout=30, check=True)
    boot = pathlib.Path(temp, "test_boot")
    subprocess.run(flags + [str(ROOT / "tests/test_host_boot_linux.c"),
                           str(ROOT / "host/hos_platform.c"), "-o", str(boot)], check=True)
    subprocess.run([str(boot)], env=env, timeout=30, check=True)
    fault = pathlib.Path(temp, "test_fault")
    subprocess.run(flags + [str(ROOT / "tests/test_fault_linux.c"),
                           str(ROOT / "host/hos_fault.c"), "-o", str(fault)], check=True)
    subprocess.run([str(fault)], env=env, timeout=30, check=True)
    if "--core-only" in sys.argv:
        raise SystemExit(0)
    sdl = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
    media = pathlib.Path(temp, "test_media")
    subprocess.run(flags + [str(ROOT / "tests/test_host_media_linux.c"),
                           str(ROOT / "host/hos_platform.c"), *sdl, "-lm", "-o", str(media)], check=True)
    subprocess.run([str(media)], env=dict(env, SDL_AUDIODRIVER="dummy"), timeout=30, check=True)
