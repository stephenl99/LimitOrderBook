#!/usr/bin/env python3
import json
import os
import subprocess

MAIN_SRCS = [
    "Core/Test/Main.cpp",
    "Core/Test/BenchMain.cpp",
    "Core/Test/QueueBench.cpp",
    "App/Source/App.cpp",
    "App/Source/ItchDump.cpp",
    "tools/verify_mbo_offsets.cpp",
]

DATABENTO_SRCS = [
    "App/Source/DatabentoSmokeTest.cpp",
    "App/Source/DatabentoReplay.cpp",
    "App/Source/DatabentoDownload.cpp",
]


def brew_prefix(formula):
    result = subprocess.run(["brew", "--prefix", formula], capture_output=True, text=True)
    return result.stdout.strip()


def databento_flags():
    includes = ["vendor/install/include"]
    includes += [f"{prefix}/include" for prefix in (brew_prefix("openssl@3"), brew_prefix("zstd")) if prefix]
    return " ".join(f"-I{path}" for path in includes) + " -mmacosx-version-min=14.6"


def main():
    core_srcs = sorted(
        subprocess.run(
            ["find", "Core/Source", "-name", "*.cpp"], capture_output=True, text=True, check=True
        ).stdout.split()
    )
    cwd = os.getcwd()
    base = "clang++ -std=c++23 -Wall -Wextra -ICore/Source"
    extra = databento_flags()
    entries = [
        {"directory": cwd, "file": src, "command": f"{base} -c {src}"}
        for src in core_srcs + MAIN_SRCS
    ] + [
        {"directory": cwd, "file": src, "command": f"{base} {extra} -c {src}"}
        for src in DATABENTO_SRCS
    ]
    with open("compile_commands.json", "w") as f:
        json.dump(entries, f, indent=2)
    print(f"wrote compile_commands.json ({len(entries)} files)")


if __name__ == "__main__":
    main()
