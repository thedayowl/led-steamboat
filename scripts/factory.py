# PlatformIO post-build script: after firmware.bin is built, also writes
# factory.bin next to it - bootloader + partition table + boot_app0 + app merged
# into one image to flash at 0x0 on a blank or WLED board:
#
#   esptool.py --chip esp32 write_flash 0x0 factory.bin
#
# firmware.bin on its own is the OTA image (web page upload or `pio run -e ota`).
import os

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def merge_factory(source, target, env):
    app = target[0].get_abspath()
    out = os.path.join(os.path.dirname(app), "factory.bin")
    board = env.BoardConfig()

    images = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        images += [env.subst(offset), env.subst(path)]
    images += [env.subst("$ESP32_APP_OFFSET"), app]

    cmd = [
        env.subst("$PYTHONEXE"), env.subst("$OBJCOPY"),
        "--chip", board.get("build.mcu", "esp32"),
        "merge_bin", "-o", out,
        "--flash_mode", board.get("build.flash_mode", "dio"),
        "--flash_freq", "%dm" % (int(board.get("build.f_flash", "40000000L").rstrip("L")) // 1000000),
        "--flash_size", board.get("upload.flash_size", "4MB"),
    ] + images

    print("Building factory image " + out)
    if env.Execute(" ".join('"%s"' % c for c in cmd)):
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_factory)
