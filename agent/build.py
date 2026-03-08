"""
Build script for the 2048 AI C engine.

Usage:
    uv run python -m agent.build
"""

import subprocess
import sys
import os


def get_lib_filename():
    if sys.platform == "darwin":
        return "engine.dylib"
    elif sys.platform == "win32":
        return "engine.dll"
    else:
        return "engine.so"


def build():
    agent_dir = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(agent_dir, "engine.c")
    out = os.path.join(agent_dir, get_lib_filename())

    cmd = [
        "cc",
        "-O3",
        "-march=native",
        "-shared",
        "-fPIC",
        "-lm",
        "-o",
        out,
        src,
    ]

    print(f"Building: {' '.join(cmd)}")
    subprocess.check_call(cmd)
    print(f"Built successfully: {out}")
    return out


if __name__ == "__main__":
    build()
