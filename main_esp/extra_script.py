Import("env")

import os


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    python_exe = env.subst("$PYTHONEXE")
    chip = env.BoardConfig().get("build.mcu", "esp32")

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
        "-o",
        output,
        "0x1000",
        bootloader,
        "0x8000",
        partitions,
    ]

    if framework_dir:
        boot_app0 = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
        if os.path.exists(boot_app0):
            cmd.extend(["0xE000", boot_app0])

    cmd.extend(["0x10000", firmware])

    print("Merging firmware to {}".format(output))
    result = env.Execute(" ".join('"{}"'.format(part) for part in cmd))
    if result != 0:
        raise RuntimeError("merge_bin failed with exit code {}".format(result))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
