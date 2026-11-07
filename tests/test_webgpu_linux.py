"""Native WebGPU platform checks; --runtime additionally exercises built renderer objects."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--runtime", type=Path)
parser.add_argument("--window", action="store_true", help="Exercise SDL window startup; requires a display")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="hookshot-webgpu-") as temp:
    temp = Path(temp)
    exe = temp / "platform"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2",
                    str(ROOT / "tests/test_webgpu_platform_linux.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    if args.runtime:
        runtime = args.runtime.resolve()
        objects = shlex.split((runtime / "objs.rsp").read_text())
        main = next(p for p in objects if Path(p).name == "host_hos_main.o")
        renamed = temp / "host_main.o"
        subprocess.run(["llvm-objcopy", "--redefine-sym", "main=hookshot_original_main", main, str(renamed)], check=True)
        objects[objects.index(main)] = str(renamed)
        source = temp / "smoke.cpp"
        source.write_text('''#include <cstdio>
#include <cstdlib>
extern "C" int wgr_start(void*), rx_active();
extern "C" void wgr_resize(int,int), hos_display_start(), hos_renderer_wait();
int main() {
    if (std::getenv("HOOKSHOT_TEST_WINDOW")) {
        hos_display_start(); hos_renderer_wait();
    }
    if (!wgr_start(nullptr) || !rx_active() || !wgr_start(nullptr)) return 1;
    wgr_resize(640,480);
    puts("Native WebGPU device initialized and repeat start/resize passed");
    fflush(nullptr);
    std::_Exit(0);
}
''')
        flags = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl3", "libavcodec", "libswscale",
                                                     "libswresample", "libavutil"], text=True))
        exe = temp / "smoke"
        subprocess.run(["clang++", "-std=c++20", "-fuse-ld=lld", str(source), *objects,
                        "-L" + str(runtime), "-lwgpu_native", str(runtime / "stub/libhookshot_game.so"),
                        *flags, "-pthread", "-ldl", "-lm", "-rdynamic", "-o", str(exe)], check=True)
        env = {k:v for k,v in os.environ.items() if not k.startswith(("HOS_", "RX_"))}
        env.update(LD_LIBRARY_PATH=str(runtime)+":"+str(runtime/"stub"), RX_BACKEND="vulkan")
        if args.window:
            env.update(HOOKSHOT_TEST_WINDOW="1", RX_BACKEND="webgpu", SDL_VIDEODRIVER="x11")
        result = subprocess.run([str(exe)], env=env, timeout=60, check=True, capture_output=True, text=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if args.window:
            assert "surface " in result.stderr and "no window (offscreen)" not in result.stderr, "SDL surface was not initialized"
