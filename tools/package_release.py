#!/usr/bin/env python3
"""Build, test, install and archive an ImForge release (zip/tar.gz, standalone headers, SHA256SUMS).
Archives are reproducible: sorted entries, SOURCE_DATE_EPOCH (or 1980) timestamps, normalized modes.
e.g. python3 tools/package_release.py --preset wsl-gcc-release --out dist
"""
import argparse
import gzip
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
ZIP_MIN_EPOCH = 315532800
TRUE_VALUES = {"ON", "1", "TRUE", "YES", "Y"}
DIST_HEADERS_SUBDIR = pathlib.Path("share") / "imforge" / "dist"


def log(msg):
    print(f"[package_release] {msg}", flush=True)


def run(cmd, cwd=None):
    log("$ " + " ".join(str(c) for c in cmd))
    result = subprocess.run([str(c) for c in cmd], cwd=str(cwd) if cwd else None)
    if result.returncode != 0:
        sys.exit(f"[package_release] command failed with exit code {result.returncode}: {cmd[0]}")


def find_cmake(explicit):
    if explicit:
        return pathlib.Path(explicit)
    found = shutil.which("cmake")
    if found:
        return pathlib.Path(found)
    if sys.platform == "win32":
        for candidate in (
            pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "CMake" / "bin" / "cmake.exe",
            pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "CMake" / "bin" / "cmake.exe",
        ):
            if candidate.is_file():
                return candidate
    sys.exit("[package_release] cmake not found on PATH (use --cmake <path>)")


def sibling_tool(cmake, name):
    candidate = cmake.with_name(name + cmake.suffix)
    if candidate.is_file():
        return candidate
    found = shutil.which(name)
    if found:
        return pathlib.Path(found)
    sys.exit(f"[package_release] {name} not found next to {cmake} or on PATH")


def load_configure_presets(source_dir):
    presets = {}
    for file_name in ("CMakePresets.json", "CMakeUserPresets.json"):
        path = source_dir / file_name
        if not path.is_file():
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        for preset in data.get("configurePresets", []):
            presets.setdefault(preset["name"], preset)
    return presets


def preset_binary_dir(source_dir, preset_name):
    presets = load_configure_presets(source_dir)
    seen = set()

    def lookup(name):
        if name in seen or name not in presets:
            return None
        seen.add(name)
        preset = presets[name]
        if "binaryDir" in preset:
            return preset["binaryDir"]
        parents = preset.get("inherits", [])
        if isinstance(parents, str):
            parents = [parents]
        for parent in parents:
            found = lookup(parent)
            if found:
                return found
        return None

    raw = lookup(preset_name)
    if raw is None:
        return source_dir / "build" / preset_name
    raw = raw.replace("${sourceDir}", str(source_dir))
    raw = raw.replace("${sourceParentDir}", str(source_dir.parent))
    raw = raw.replace("${sourceDirName}", source_dir.name)
    raw = raw.replace("${presetName}", preset_name)
    raw = raw.replace("${hostSystemName}", platform.system())
    raw = raw.replace("${pathListSep}", os.pathsep)
    raw = raw.replace("${dollar}", "$")
    raw = re.sub(r"\$env\{([^}]+)\}", lambda m: os.environ.get(m.group(1), ""), raw)
    raw = re.sub(r"\$penv\{([^}]+)\}", lambda m: os.environ.get(m.group(1), ""), raw)
    return pathlib.Path(raw)


def read_cmake_cache(build_dir):
    values = {}
    cache = build_dir / "CMakeCache.txt"
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith(("#", "//")):
            continue
        match = re.match(r"^([^:=]+)(?::[^=]*)?=(.*)$", line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def read_cpack_var(build_dir, name):
    config = build_dir / "CPackConfig.cmake"
    if not config.is_file():
        return None
    pattern = re.compile(r'^\s*set\(\s*' + re.escape(name) + r'\s+"([^"]*)"\s*\)', re.IGNORECASE)
    for line in config.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            return match.group(1)
    return None


def project_version(cache, source_dir):
    version = cache.get("CMAKE_PROJECT_VERSION")
    if version:
        return version
    text = (source_dir / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"^project\(\s*ImForge\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)", text, re.MULTILINE)
    if not match:
        sys.exit("[package_release] cannot determine the project version")
    return match.group(1)


def host_os_arch():
    system = platform.system().lower()
    os_name = {"windows": "windows", "darwin": "macos"}.get(system, system)
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64", "x64"):
        arch = "x64"
    elif machine in ("arm64", "aarch64"):
        arch = "arm64"
    elif re.match(r"^(x86|i[3-6]86)$", machine):
        arch = "x86"
    else:
        arch = machine
    return os_name, arch


def package_name(args, build_dir, cache):
    if args.name:
        return args.name
    from_cpack = read_cpack_var(build_dir, "CPACK_PACKAGE_FILE_NAME")
    if from_cpack:
        return from_cpack
    os_name, arch = host_os_arch()
    return f"ImForge-{project_version(cache, args.source_dir)}-{os_name}-{arch}"


def archive_epoch():
    raw = os.environ.get("SOURCE_DATE_EPOCH")
    if raw and raw.strip().isdigit():
        return int(raw.strip())
    return ZIP_MIN_EPOCH


def normalized_mode(path):
    if sys.platform == "win32":
        return 0o755 if path.suffix.lower() in (".exe", ".dll", ".bat", ".cmd", ".sh", ".py") else 0o644
    return 0o755 if path.stat().st_mode & 0o111 else 0o644


def sorted_entries(stage_dir):
    entries = []
    for path in stage_dir.rglob("*"):
        rel = path.relative_to(stage_dir).as_posix()
        entries.append((rel.encode("utf-8"), path))
    entries.sort()
    return [path for _, path in entries]


def write_zip(stage_dir, prefix, zip_path, epoch):
    date_time = time.gmtime(max(epoch, ZIP_MIN_EPOCH))[:6]
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in sorted_entries(stage_dir):
            rel = path.relative_to(stage_dir).as_posix()
            if path.is_dir():
                info = zipfile.ZipInfo(f"{prefix}/{rel}/", date_time=date_time)
                info.create_system = 3
                info.external_attr = (0o040755 << 16) | 0x10
                zf.writestr(info, b"")
                continue
            info = zipfile.ZipInfo(f"{prefix}/{rel}", date_time=date_time)
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100000 | normalized_mode(path)) << 16
            with path.open("rb") as src, zf.open(info, "w") as dst:
                shutil.copyfileobj(src, dst)


def write_tgz(stage_dir, prefix, tgz_path, epoch):
    def tarinfo(name, is_dir, size=0, mode=0o755):
        info = tarfile.TarInfo(name)
        info.mtime = epoch
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.type = tarfile.DIRTYPE if is_dir else tarfile.REGTYPE
        info.mode = mode
        info.size = size
        return info

    with open(tgz_path, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as gz:
            with tarfile.open(fileobj=gz, mode="w", format=tarfile.GNU_FORMAT) as tf:
                tf.addfile(tarinfo(prefix, True))
                for path in sorted_entries(stage_dir):
                    rel = path.relative_to(stage_dir).as_posix()
                    if path.is_dir():
                        tf.addfile(tarinfo(f"{prefix}/{rel}", True))
                        continue
                    info = tarinfo(f"{prefix}/{rel}", False, path.stat().st_size, normalized_mode(path))
                    with path.open("rb") as src:
                        tf.addfile(info, src)


def sha256_of(path):
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_formats(raw):
    if raw == "auto":
        return ["zip"] if sys.platform == "win32" else ["zip", "tgz"]
    formats = [f.strip().lower() for f in raw.split(",") if f.strip()]
    unknown = [f for f in formats if f not in ("zip", "tgz")]
    if unknown:
        sys.exit(f"[package_release] unknown archive format(s): {', '.join(unknown)} (use zip,tgz)")
    return formats


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source-dir", type=pathlib.Path, default=ROOT)
    ap.add_argument("--preset", help="configure preset from CMakePresets.json")
    ap.add_argument("--build-dir", type=pathlib.Path, help="build directory (default: the preset's binaryDir)")
    ap.add_argument("--config", default="Release", help="configuration for multi-config generators (default: Release)")
    ap.add_argument("--out", type=pathlib.Path, default=pathlib.Path("dist"), help="output directory (default: dist)")
    ap.add_argument("--cmake-arg", action="append", default=[], help="extra configure argument, e.g. --cmake-arg=-DIMFORGE_BUILD_BUILDER=OFF (forces a reconfigure)")
    ap.add_argument("--generator", "-G", help="generator for a non-preset configure")
    ap.add_argument("--target", action="append", default=[], help="build only these targets (default: all)")
    ap.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--formats", default="auto", help="comma list of zip,tgz (default: zip on Windows, zip,tgz elsewhere)")
    ap.add_argument("--name", help="override the package name (default: CPACK_PACKAGE_FILE_NAME)")
    ap.add_argument("--cmake", help="path to the cmake executable")
    ap.add_argument("--reconfigure", action="store_true", help="run the configure step even if the build dir is configured")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--skip-tests", action="store_true")
    ap.add_argument("--keep-stage", action="store_true", help="keep <out>/_stage/<package> after archiving")
    args = ap.parse_args()
    if not args.preset and not args.build_dir:
        ap.error("one of --preset or --build-dir is required")
    args.source_dir = args.source_dir.resolve()
    return args


def main():
    args = parse_args()
    cmake = find_cmake(args.cmake)
    ctest = sibling_tool(cmake, "ctest")
    build_dir = args.build_dir.resolve() if args.build_dir else preset_binary_dir(args.source_dir, args.preset).resolve()
    out_dir = args.out.resolve()
    log(f"source={args.source_dir} build={build_dir} out={out_dir}")

    needs_configure = args.reconfigure or bool(args.cmake_arg) or not (build_dir / "CMakeCache.txt").is_file()
    if needs_configure:
        cmd = [cmake, "-S", args.source_dir, "-B", build_dir]
        if args.preset:
            cmd += ["--preset", args.preset]
        elif args.generator:
            cmd += ["-G", args.generator]
        cmd += args.cmake_arg
        run(cmd, cwd=args.source_dir)
    else:
        log("build directory already configured, skipping configure (use --reconfigure to force)")

    cache = read_cmake_cache(build_dir)
    multi_config = bool(cache.get("CMAKE_CONFIGURATION_TYPES"))
    config_args = ["--config", args.config] if multi_config else []
    if not multi_config and cache.get("CMAKE_BUILD_TYPE") and cache["CMAKE_BUILD_TYPE"] != args.config:
        log(f"note: single-config build tree is {cache['CMAKE_BUILD_TYPE']}, --config {args.config} is ignored")

    if not args.skip_build:
        cmd = [cmake, "--build", build_dir, "--parallel", str(args.jobs)] + config_args
        if args.target:
            cmd += ["--target"] + args.target
        run(cmd)

    tests_enabled = cache.get("IMFORGE_BUILD_TESTS", "OFF").upper() in TRUE_VALUES
    if args.skip_tests:
        log("tests skipped (--skip-tests)")
    elif not tests_enabled:
        log("tests skipped (IMFORGE_BUILD_TESTS is OFF in this build tree)")
    else:
        cmd = [ctest, "--test-dir", build_dir, "--output-on-failure", "--timeout", "600", "--no-tests=error"]
        if multi_config:
            cmd += ["-C", args.config]
        run(cmd)

    name = package_name(args, build_dir, cache)
    stage_root = out_dir / "_stage"
    stage_dir = stage_root / name
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True)
    run([cmake, "--install", build_dir, "--prefix", stage_dir] + config_args)

    out_dir.mkdir(parents=True, exist_ok=True)
    epoch = archive_epoch()
    produced = []
    for fmt in parse_formats(args.formats):
        if fmt == "zip":
            target = out_dir / f"{name}.zip"
            write_zip(stage_dir, name, target, epoch)
        else:
            target = out_dir / f"{name}.tar.gz"
            write_tgz(stage_dir, name, target, epoch)
        produced.append(target)
        log(f"wrote {target} ({target.stat().st_size} bytes)")

    headers_dir = stage_dir / DIST_HEADERS_SUBDIR
    for header in sorted(headers_dir.glob("*.h")) if headers_dir.is_dir() else []:
        target = out_dir / header.name
        shutil.copyfile(header, target)
        produced.append(target)
        log(f"wrote {target}")
    if not headers_dir.is_dir():
        log(f"note: {DIST_HEADERS_SUBDIR.as_posix()} not installed (Python3 not found at configure time or IMFORGE_PACKAGING=OFF)")

    sums_path = out_dir / "SHA256SUMS"
    lines = [f"{sha256_of(p)}  {p.name}\n" for p in sorted(produced, key=lambda p: p.name)]
    sums_path.write_text("".join(lines), encoding="utf-8", newline="\n")
    log(f"wrote {sums_path}")
    print("".join(lines), end="")

    if not args.keep_stage:
        shutil.rmtree(stage_dir)
        if not any(stage_root.iterdir()):
            stage_root.rmdir()
    return 0


if __name__ == "__main__":
    sys.exit(main())
