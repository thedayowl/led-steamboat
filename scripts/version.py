# PlatformIO pre-build script: stamps the firmware with a version taken from git.
#
#   Commit tagged v1.2.0            -> "1.2.0"
#   3 commits after v1.2.0          -> "1.2.0-3-gabc1234"
#   Uncommitted changes             -> "...-dirty"
#   No tags yet                     -> "0.0.0-gabc1234"
#   No git available                -> FW_VERSION fallback in src/app.h
#
# To release: commit, then `git tag v1.3.0` and build.
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def git_version():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--match", "v[0-9]*", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        return None
    if not out:
        return None
    if out.startswith("v"):
        return out[1:]
    return "0.0.0-g" + out  # no tag yet, just a commit hash


version = git_version()
if version:
    env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
    env["FW_VERSION_STR"] = version
    print("Firmware version: " + version)
else:
    print("Firmware version: git not available, using fallback from src/app.h")
