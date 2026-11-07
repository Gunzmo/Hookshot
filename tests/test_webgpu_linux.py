"""Native WebGPU platform/shader checks, or --runtime to exercise built renderer objects."""
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
    if not args.runtime:
        exe = temp / "platform"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2",
                        str(ROOT / "tests/test_webgpu_platform_linux.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        shaders = temp / "shaders"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O1",
                        "-I" + str(ROOT / "gpu/third_party/suyu"),
                        str(ROOT / "tests/test_webgpu_shaders.cpp"), "-o", str(shaders)], check=True)
        subprocess.run([str(shaders)], check=True)
    if args.runtime:
        runtime = args.runtime.resolve()
        wgpu = Path(os.environ.get("WGPU_NATIVE") or ROOT / "work/deps/wgpu-native")
        if not (wgpu / "include/webgpu/webgpu.h").is_file():
            parser.error("--runtime requires WGPU_NATIVE pointing to the wgpu-native distribution (headers and library)")
        objects = shlex.split((runtime / "objs.rsp").read_text())
        main = next(p for p in objects if Path(p).name == "host_hos_main.o")
        renamed = temp / "host_main.o"
        subprocess.run(["llvm-objcopy", "--redefine-sym", "main=hookshot_original_main", main, str(renamed)], check=True)
        objects[objects.index(main)] = str(renamed)
        source = temp / "smoke.cpp"
        source.write_text('''#include <cstdio>
#include <cstdlib>
void test_webgpu_nvn();
extern "C" int wgr_start(void*), rx_active();
extern "C" void wgr_resize(int,int), hos_display_start(), hos_renderer_wait();
int main() {
    if (std::getenv("HOOKSHOT_TEST_WINDOW")) {
        hos_display_start(); hos_renderer_wait();
    }
    if (!wgr_start(nullptr) || !rx_active() || !wgr_start(nullptr)) return 1;
    wgr_resize(640,480);
    if (!std::getenv("HOOKSHOT_TEST_WINDOW")) test_webgpu_nvn();
    puts("Native WebGPU device initialized and repeat start/resize passed");
    fflush(nullptr);
    std::_Exit(0);
}
''')
        flags = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl3", "libavcodec", "libswscale",
                                                     "libswresample", "libavutil"], text=True))
        exe = temp / "smoke"
        subprocess.run(["clang++", "-std=c++20", "-fuse-ld=lld", "-include", "algorithm", "-include", "functional", "-include", "cstdio",
                        "-I" + str(ROOT), "-I" + str(ROOT / "gpu/compat"),
                        "-I" + str(ROOT / "gpu/third_party/suyu"),
                        "-I" + str(wgpu / "include"),
                        str(source), str(ROOT / "tests/test_webgpu_nvn_linux.cpp"), *objects,
                        "-L" + str(runtime), "-lwgpu_native", str(runtime / "stub/libhookshot_game.so"),
                        *flags, "-pthread", "-ldl", "-lm", "-rdynamic", "-o", str(exe)], check=True)
        env = {k:v for k,v in os.environ.items() if not k.startswith(("HOS_", "RX_"))}
        env.pop("EDEN_NX2_TENSOR", None) # exercise the unsupported-tensor fallback deterministically
        env.update(LD_LIBRARY_PATH=str(runtime)+":"+str(runtime/"stub"), RX_BACKEND="webgpu", RX_NVN_SYNC="1")
        if args.window:
            env.update(HOOKSHOT_TEST_WINDOW="1", RX_BACKEND="webgpu", SDL_VIDEODRIVER="x11")
        result = subprocess.run([str(exe)], env=env, timeout=60, capture_output=True, text=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        result.check_returncode()
        assert "parsing error:" not in result.stderr, "WGSL module validation failed"
        assert "NVN compute pipeline not created" not in result.stderr, "Compute pipeline validation failed"
        if args.window:
            assert "surface " in result.stderr and "no window (offscreen)" not in result.stderr, "SDL surface was not initialized"
