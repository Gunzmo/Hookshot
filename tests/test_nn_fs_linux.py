"""Build and exercise the Linux guest filesystem without a game dump or SDL."""
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "nx2"))
import rtgen

with tempfile.TemporaryDirectory(prefix="hookshot-fs-test-") as temp:
    rtgen.write(temp)
    # The host filesystem uses the guest context from a64.h, not the generated instruction helpers.
    for name in ("a64_suyu.h", "a64_fpx.h"):
        pathlib.Path(temp, name).write_text("")
    exe = pathlib.Path(temp, "test_nn_fs")
    subprocess.run([os.environ.get("CC", "cc"), "-D_GNU_SOURCE", "-DA64_MEM_ZERO", "-std=gnu11",
                    "-I" + temp, "-I" + str(ROOT / "host"), str(ROOT / "tests/test_nn_fs_linux.c"),
                    str(ROOT / "host/nn_fs.c"), "-pthread", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
