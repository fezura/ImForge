#!/usr/bin/env python3
"""Compile every public ImForge header on its own against several Dear ImGui checkouts.

    python3 tools/check_version_matrix.py --imgui-dirs ~/deps/imgui-* --compiler g++
"""
import argparse
import concurrent.futures
import glob
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_SKIP = r"_(dx9|dx11|dx12|vulkan)\.h$"
BASE_FLAGS = ["-fsyntax-only", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra"]


def collect_headers(subset):
    if subset:
        return [pathlib.Path(h) for h in subset]
    headers = sorted(p.relative_to(ROOT) for p in (ROOT / "include" / "imforge").rglob("*.h"))
    headers += sorted(p.relative_to(ROOT) for p in (ROOT / "packs" / "essentials").glob("*.h"))
    return headers


def imgui_label(imgui_dir):
    text = (pathlib.Path(imgui_dir) / "imgui.h").read_text(encoding="utf-8", errors="replace")
    m = re.search(r'#define\s+IMGUI_VERSION\s+"([^"]+)"', text)
    label = m.group(1) if m else pathlib.Path(imgui_dir).name
    if re.search(r"#define\s+IMGUI_HAS_DOCK", text):
        label += "-docking"
    return label


def compile_one(args, header, imgui_dir):
    cmd = [args.compiler, f"-std={args.std}", *BASE_FLAGS]
    if args.werror:
        cmd.append("-Werror")
    cmd += [f"-I{ROOT / 'include'}", f"-I{ROOT / 'packs' / 'essentials'}", f"-I{imgui_dir}"]
    cmd += [f"-D{d}" for d in args.define]
    cmd += args.extra_flags.split()
    cmd += ["-x", "c++", "-"]
    source = f'#include "{(ROOT / header).as_posix()}"\n'
    try:
        proc = subprocess.run(cmd, input=source, capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return "FAIL", "compiler timed out"
    except OSError as exc:
        return "FAIL", f"cannot run {args.compiler}: {exc}"
    output = (proc.stderr or "") + (proc.stdout or "")
    if proc.returncode != 0:
        return "FAIL", output
    if "warning:" in output:
        return "WARN", output
    return "ok", ""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--imgui-dirs", nargs="+", default=None,
                        help="Dear ImGui checkouts (default: ~/deps/imgui-*)")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--std", default="c++17")
    parser.add_argument("--headers", nargs="*", help="only these headers (paths relative to the repo root)")
    parser.add_argument("--skip", default=DEFAULT_SKIP, help=f"regex of headers to skip (default: {DEFAULT_SKIP})")
    parser.add_argument("--no-skip", action="store_true", help="compile every header, including platform-specific ones")
    parser.add_argument("-D", "--define", action="append", default=[], help="extra preprocessor define")
    parser.add_argument("--extra-flags", default="", help="extra compiler flags (whitespace separated)")
    parser.add_argument("--werror", action="store_true", help="treat warnings as failures")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--max-lines", type=int, default=6, help="error lines shown per failure")
    args = parser.parse_args()

    imgui_dirs = args.imgui_dirs or sorted(glob.glob(os.path.expanduser("~/deps/imgui-*")))
    imgui_dirs = [str(pathlib.Path(d).expanduser().resolve()) for d in imgui_dirs]
    bad = [d for d in imgui_dirs if not (pathlib.Path(d) / "imgui.h").is_file()]
    if bad or not imgui_dirs:
        print("no imgui.h in: " + ", ".join(bad or ["<no --imgui-dirs given and ~/deps/imgui-* is empty>"]), file=sys.stderr)
        return 2

    headers = collect_headers(args.headers)
    if not headers:
        print("no headers found under include/imforge or packs/essentials", file=sys.stderr)
        return 2
    skip_re = None if args.no_skip else re.compile(args.skip)

    labels = {d: imgui_label(d) for d in imgui_dirs}
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {}
        for header in headers:
            for d in imgui_dirs:
                if skip_re and skip_re.search(header.name):
                    results[(header, d)] = ("skip", "")
                    continue
                futures[pool.submit(compile_one, args, header, d)] = (header, d)
        for fut in concurrent.futures.as_completed(futures):
            results[futures[fut]] = fut.result()

    name_w = max(len(str(h)) for h in headers)
    col_w = max(6, *(len(l) for l in labels.values()))
    print("header".ljust(name_w) + "  " + "  ".join(labels[d].ljust(col_w) for d in imgui_dirs))
    print("-" * (name_w + 2 + (col_w + 2) * len(imgui_dirs)))
    for header in headers:
        row = [results[(header, d)][0].ljust(col_w) for d in imgui_dirs]
        print(str(header).ljust(name_w) + "  " + "  ".join(row))

    failures = [(h, d) for (h, d), (status, _) in results.items() if status == "FAIL"]
    warnings = [(h, d) for (h, d), (status, _) in results.items() if status == "WARN"]
    for header in headers:
        for d in imgui_dirs:
            status, output = results[(header, d)]
            if status not in ("FAIL", "WARN"):
                continue
            print(f"\n== {status}: {header} @ {labels[d]}")
            lines = [l for l in output.splitlines() if "error" in l or "warning" in l] or output.splitlines()
            for line in lines[: args.max_lines]:
                print("   " + line)

    total = sum(1 for s, _ in results.values() if s != "skip")
    print(f"\n{total - len(failures)}/{total} compiled, {len(failures)} failed, {len(warnings)} with warnings")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
