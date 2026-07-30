# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
# SPDX-License-Identifier: MIT

#!/usr/bin/env python3
import argparse
import subprocess
import sys
import platform
import shutil
from pathlib import Path

def printf(*args, **kwargs):
    kwargs['flush'] = True
    print(*args, **kwargs)
    sys.stdout.flush()
    sys.stderr.flush()

def copy_outputs(backend, build_type, script_dir):
    sdk_dir = script_dir / "Source" / "NG-SDK"
    bin_dir = sdk_dir / "bin"
    output_dir = script_dir / "Binaries" / "ThirdParty" / "PreBuiltNGSDK"
    output_dir.mkdir(parents=True, exist_ok=True)

    is_windows = backend == "vk_windows_x64"
    is_linux_x64 = backend == "vk_linux_x64"
    is_linux_arm = backend == "vk_linux_arm"
    is_android_arm = backend == "vk_android_arm"

    if build_type == "Debug":
        suffix = "d"
    elif build_type == "RelWithDebInfo":
        suffix = "drel"
    else:
        suffix = ""

    if is_windows:
        dll_src = bin_dir / f"ngsdk_windows_x64{suffix}.dll"
        lib_src = bin_dir / f"ngsdk_windows_x64{suffix}.lib"
        pdb_src = bin_dir / f"ngsdk_windows_x64{suffix}.pdb"
        printf(f"Copying: {dll_src} -> {output_dir / 'ngsdk_windows_x64.dll'}")
        shutil.copy2(dll_src, output_dir / "ngsdk_windows_x64.dll")
        printf(f"Copying: {lib_src} -> {output_dir / 'ngsdk_windows_x64.lib'}")
        shutil.copy2(lib_src, output_dir / "ngsdk_windows_x64.lib")
        if pdb_src.exists():
            printf(f"Copying: {pdb_src} -> {output_dir / 'ngsdk_windows_x64.pdb'}")
            shutil.copy2(pdb_src, output_dir / "ngsdk_windows_x64.pdb")
    elif is_linux_arm:
        so_src = bin_dir / f"libngsdk_linux_arm{suffix}.so"
        printf(f"Copying: {so_src} -> {output_dir / 'libngsdk_linux_arm.so'}")
        shutil.copy2(so_src, output_dir / "libngsdk_linux_arm.so")
    elif is_linux_x64:
        so_src = bin_dir / f"libngsdk_linux_x64{suffix}.so"
        printf(f"Copying: {so_src} -> {output_dir / 'libngsdk_linux_x64.so'}")
        shutil.copy2(so_src, output_dir / "libngsdk_linux_x64.so")
    elif is_android_arm:
        so_src = bin_dir / "libngsdk_android_arm.so"
        if not so_src.exists():
            printf(f"Android so not found: {so_src}")
            return
        printf(f"Copying: {so_src} -> {output_dir / 'libngsdk_android_arm.so'}")
        shutil.copy2(so_src, output_dir / "libngsdk_android_arm.so")

    layer_so_src   = bin_dir / "libVkLayer_arm_NG.so"
    layer_dll_src  = bin_dir / "VkLayer_arm_NG.dll"
    layer_json_src = bin_dir / "VkLayer_arm_NG.json"

    def _copy_if(src: Path, dst: Path):
        if src.exists():
            dst.parent.mkdir(parents=True, exist_ok=True)
            printf(f"Copying: {src} -> {dst}")
            shutil.copy2(src, dst)
        else:
            printf(f"NGLayer artifact not found, skipping: {src}")

    if is_windows:
        _copy_if(layer_dll_src,  output_dir / "VkLayer_arm_NG.dll")
        _copy_if(layer_json_src, output_dir / "VkLayer_arm_NG.json")
    elif is_linux_x64:
        _copy_if(layer_so_src,   output_dir / "Linux" / "libVkLayer_arm_NG.so")
        _copy_if(layer_json_src, output_dir / "Linux" / "VkLayer_arm_NG.json")
    elif is_linux_arm:
        _copy_if(layer_so_src,   output_dir / "LinuxArm64" / "libVkLayer_arm_NG.so")
        _copy_if(layer_json_src, output_dir / "LinuxArm64" / "VkLayer_arm_NG.json")
    elif is_android_arm:
        _copy_if(layer_so_src,   output_dir / "libVkLayer_arm_NG.so")

def main():
    parser = argparse.ArgumentParser(description="Unified build/copy script for NG SDK")
    parser.add_argument(
        "--build-type", "-t", dest="build_type",
        choices=["Debug", "Release", "RelWithDebInfo"], default="Release",
        help="Build type: Debug, Release, or RelWithDebInfo (default: Release)"
    )
    parser.add_argument(
        "--backend", "-b", dest="backend", default=None,
        choices=["vk_windows_x64", "vk_linux_x64", "vk_linux_arm", "vk_android_arm"],
        help="Target backend: vk_windows_x64, vk_linux_x64, vk_linux_arm, vk_android_arm (default: auto-detect)"
    )
    parser.add_argument("--jobs", "-j", type=int, default=4, help="Number of build jobs (default: 4)")
    parser.add_argument("--clean", action="store_true", default=False, help="Clean before build")
    parser.add_argument("--toolchain", type=str, help="CMake toolchain file path")
    parser.add_argument("--vulkan-lib", type=str, help="Path to Vulkan library")
    args = parser.parse_args()

    script_dir = Path(__file__).parent.resolve()
    sdk_dir = script_dir / "Source" / "NG-SDK"
    build_py = sdk_dir / "build.py"

    # Detect backend if not specified
    if not args.backend:
        system = platform.system().lower()
        machine = platform.machine().lower()
        if system == "windows":
            backend = "vk_windows_x64"
        elif system == "linux":
            if machine in ["aarch64", "arm64"]:
                backend = "vk_linux_arm"
            else:
                backend = "vk_linux_x64"
        else:
            printf(f"Error: Unsupported platform: {system}")
            sys.exit(1)
    else:
        backend = args.backend

    # Build command
    build_cmd = [sys.executable, str(build_py), "-t", args.build_type, "-b", backend, "-j", str(args.jobs)]
    if args.clean:
        build_cmd.append("--clean")
    if args.toolchain:
        build_cmd.extend(["--toolchain", args.toolchain])
    if args.vulkan_lib:
        build_cmd.extend(["--vulkan-lib", args.vulkan_lib])

    printf(f"Calling NG-SDK/build.py: {' '.join(str(x) for x in build_cmd)}")
    result = subprocess.run(build_cmd, cwd=sdk_dir)
    if result.returncode != 0:
        printf(f"Error: build.py failed with exit code {result.returncode}")
        sys.exit(result.returncode)

    # Copy outputs
    copy_outputs(backend, args.build_type, script_dir)
    printf("\nAll done!")

if __name__ == "__main__":
    main()
