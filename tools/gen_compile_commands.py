#!/usr/bin/env python3
"""Regenerate compile_commands.json for clangd/IDE tooling.

No bear needed: every translation unit in the Makefile build uses the same
flags, so this is just a JSON dump of (source file, fixed command) pairs.
"""
import json
import os
import subprocess

MAIN_SRCS = [
    "Core/Test/Main.cpp",
    "Core/Test/BenchMain.cpp",
    "App/Source/App.cpp",
    "App/Source/ItchDump.cpp",
]


def main():
    core_srcs = sorted(
        subprocess.run(
            ["find", "Core/Source", "-name", "*.cpp"], capture_output=True, text=True, check=True
        ).stdout.split()
    )
    srcs = core_srcs + MAIN_SRCS
    cwd = os.getcwd()
    entries = [
        {
            "directory": cwd,
            "file": src,
            "command": f"clang++ -std=c++23 -Wall -Wextra -ICore/Source -c {src}",
        }
        for src in srcs
    ]
    with open("compile_commands.json", "w") as f:
        json.dump(entries, f, indent=2)
    print(f"wrote compile_commands.json ({len(entries)} files)")


if __name__ == "__main__":
    main()
