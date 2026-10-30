@echo off
rem = r"""
rem SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates open-source-office@arm.com
rem SPDX-License-Identifier: MIT
setlocal

python -B -x "%~f0"
set "RESULT=%ERRORLEVEL%"

echo.
if not "%RESULT%"=="0" echo ERROR: Download failed. See the error above.
echo.
pause
exit /b %RESULT%
"""

import os
import re
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path


PLUGIN_ROOT = Path(__file__).resolve().parent
PACKAGE_FILES = {
    "bin/VkLayer_Graph.dll": "VkLayer_Graph.dll",
    "bin/VkLayer_Graph.json": "VkLayer_Graph.json",
    "bin/VkLayer_Tensor.dll": "VkLayer_Tensor.dll",
    "bin/VkLayer_Tensor.json": "VkLayer_Tensor.json",
    "lib/ai-ml-sdk-emulation-layer.spdx.json": "ai-ml-sdk-emulation-layer.spdx.json",
}


def read_version():
    readme = PLUGIN_ROOT / "README.md"
    rows = re.findall(
        r"^[ \t]*\|[ \t]*"
        r"(?:Arm ML Emulation Layer for Vulkan|\[Arm ML Emulation Layer for Vulkan\]\([^\s)]+\))"
        r"[ \t]*\|([^|\r\n]*)\|",
        readme.read_text(encoding="utf-8-sig"),
        re.MULTILINE,
    )
    if len(rows) != 1:
        raise RuntimeError(
            f"Expected exactly one Arm ML Emulation Layer for Vulkan version row in {readme}; "
            f"found {len(rows)}."
        )
    version_text = rows[0].strip()
    link = re.fullmatch(r"\[([^\]\r\n]+)\]\(([^)\s]+)\)", version_text)
    if link:
        version_text = link.group(1)
    match = re.fullmatch(r"`(v[0-9]+\.[0-9]+\.[0-9]+)`", version_text)
    if not match:
        raise RuntimeError(
            f"Invalid emulation-layer version in {readme}: {rows[0].strip()!r}. "
            "Expected a version in backticks using the vMAJOR.MINOR.PATCH format."
        )
    version = match.group(1)
    if link and link.group(2) != (
        "https://github.com/arm/ai-ml-emulation-layer-for-vulkan/"
        f"releases/tag/release/{version}"
    ):
        raise RuntimeError(f"Release link does not match emulation-layer version {version} in {readme}.")
    return version


def download(url, destination):
    request = urllib.request.Request(url, headers={"User-Agent": "arm-ng-plugin"})
    with urllib.request.urlopen(request, timeout=60) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


def main():
    try:
        version = read_version()
        archive_name = f"ai_ml_emulation_layer_for_vulkan-{version[1:]}-Windows_AMD64.zip"
        url = (
            "https://github.com/arm/ai-ml-emulation-layer-for-vulkan/"
            f"releases/download/release/{version}/{archive_name}"
        )
        license_url = (
            "https://raw.githubusercontent.com/arm/ai-ml-emulation-layer-for-vulkan/"
            f"refs/tags/release/{version}/LICENSES/Apache-2.0.txt"
        )
        target = PLUGIN_ROOT / "Binaries" / "ThirdParty" / "VulkanMLEmulation" / "Win64"
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=target.parent) as temporary:
            staging = Path(temporary)
            archive = staging / archive_name
            runtime = staging / "runtime"
            runtime.mkdir()
            print(f"Downloading {url}", flush=True)
            download(url, archive)
            with zipfile.ZipFile(archive) as package:
                for member, name in PACKAGE_FILES.items():
                    if member not in package.namelist():
                        raise RuntimeError(f"Required file missing from package: {member}")
                    with package.open(member) as source, (runtime / name).open("wb") as output:
                        shutil.copyfileobj(source, output)
            print(f"Downloading {license_url}", flush=True)
            download(license_url, runtime / "Apache-2.0.txt")
            backup = staging / "previous"
            if target.exists():
                os.replace(target, backup)
            try:
                os.replace(runtime, target)
            except OSError:
                if backup.exists():
                    os.replace(backup, target)
                raise
        print(f"Emulation layer {version} installed in: {target}")
        print()
        print("IMPORTANT: Downloading does not enable the layers.")
        print("This script does not change environment variables.")
        print("Before running Unreal Engine, follow the setup guide to enable the layers")
        print("using the output directory above:")
        print("https://learn.arm.com/learning-paths/mobile-graphics-and-gaming/nss-unreal/2-emulation-layer/")
        return 0
    except (OSError, RuntimeError, UnicodeError, zipfile.BadZipFile) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
