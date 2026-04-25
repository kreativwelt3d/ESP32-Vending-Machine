Import("env")

import json
import hashlib
import os
import re
import shutil


def _get_board_flash_mode(env):
    memory_type = env.BoardConfig().get("build.arduino.memory_type", "")
    mode = env.BoardConfig().get("build.flash_mode", "dio")

    # Mirror PlatformIO's ESP32 builder logic so the merged image header matches
    # the mode used during regular upload/build steps.
    if env.get("PIOFRAMEWORK") == ["arduino"] and memory_type in ("opi_opi", "opi_qspi"):
        return "dout"
    if mode in ("qio", "qout"):
        return "dio"
    return mode


def _get_image_flash_freq(env):
    board = env.BoardConfig()
    return board.get("build.f_boot", board.get("build.f_flash", "40000000L"))


def _normalize_frequency(value):
    if isinstance(value, int):
        value = str(value)
    else:
        value = str(value).strip()

    if value.endswith("L"):
        value = value[:-1]

    if value.isdigit():
        return "{}m".format(int(value) // 1000000)

    return value.lower()


def _hash_file(path, algorithm):
    digest = hashlib.new(algorithm)
    with open(path, "rb") as firmware_file:
        for chunk in iter(lambda: firmware_file.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _get_firmware_version(env):
    sketch_path = os.path.join(env.subst("$PROJECT_DIR"), "src", "sketch.ino")
    if not os.path.exists(sketch_path):
        return "local"

    with open(sketch_path, "r", encoding="utf-8", errors="ignore") as sketch_file:
        sketch = sketch_file.read()

    match = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', sketch)
    return match.group(1) if match else "local"


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    python_exe = env.subst("$PYTHONEXE")
    chip = env.BoardConfig().get("build.mcu", "esp32")
    board = env.BoardConfig()
    bootloader_offset = "0x1000" if chip in ("esp32", "esp32s2") else "0x0"

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")
    output = os.path.join(build_dir, "firmware-merged.bin")

    esptool_pkg_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")

    if not esptool_pkg_dir:
        raise RuntimeError("tool-esptoolpy package not found")

    missing = [path for path in (bootloader, partitions, firmware) if not os.path.exists(path)]
    if missing:
        raise RuntimeError("Missing build artifacts for merge_bin: {}".format(", ".join(missing)))

    esptool_py = os.path.join(esptool_pkg_dir, "esptool.py")
    cmd = [
        python_exe,
        esptool_py,
        "--chip",
        chip,
        "merge_bin",
        "--flash_mode",
        _get_board_flash_mode(env),
        "--flash_freq",
        _normalize_frequency(_get_image_flash_freq(env)),
        "--flash_size",
        board.get("upload.flash_size", "16MB"),
        "-o",
        output,
        bootloader_offset,
        bootloader,
        "0x8000",
        partitions,
    ]

    if framework_dir:
        boot_app0 = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
        if os.path.exists(boot_app0):
            cmd.extend(["0xE000", boot_app0])
            shutil.copyfile(boot_app0, os.path.join(build_dir, "boot_app0.bin"))

    cmd.extend(["0x10000", firmware])

    print("Merging firmware to {}".format(output))
    result = env.Execute(" ".join('"{}"'.format(part) for part in cmd))
    if result != 0:
        raise RuntimeError("merge_bin failed with exit code {}".format(result))

    manifest = {
        "name": env.subst("$PIOENV"),
        "version": "local",
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [
                    {"path": "bootloader.bin", "offset": 0},
                    {"path": "partitions.bin", "offset": 32768},
                    {"path": "boot_app0.bin", "offset": 57344},
                    {"path": "firmware.bin", "offset": 65536},
                ],
            }
        ],
    }
    manifest_path = os.path.join(build_dir, "webflash-manifest.json")
    with open(manifest_path, "w", encoding="ascii") as manifest_file:
        json.dump(manifest, manifest_file, indent=2)
        manifest_file.write("\n")

    ota_manifest = {
        "version": _get_firmware_version(env),
        "firmware": "firmware.bin",
        "size": os.path.getsize(firmware),
        "sha256": _hash_file(firmware, "sha256"),
        "md5": _hash_file(firmware, "md5"),
        "notes": "",
    }
    ota_manifest_path = os.path.join(build_dir, "manifest.json")
    with open(ota_manifest_path, "w", encoding="ascii") as manifest_file:
        json.dump(ota_manifest, manifest_file, indent=2)
        manifest_file.write("\n")


env.AddPostAction(os.path.join(env.subst("$BUILD_DIR"), "firmware.bin"), merge_bin)
