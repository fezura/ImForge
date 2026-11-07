#!/usr/bin/env python3
"""Amalgamate the header-only ImForge modules into single drop-in headers in dist/.
Repo headers get inlined once per #if context, so IMFORGE_LIVE-only includes stay inside their block.
--check compiles every output with g++ against the pinned ImGui checkouts (run it under WSL on Windows).
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SEARCH_DIRS = [ROOT / "include", ROOT / "include" / "imforge", ROOT / "packs" / "essentials"]
OUTPUTS = [
    ("include/imforge/imforge_client.h", "dist/imforge_client_single.h"),
    ("include/imforge/imb_runtime.h", "dist/imb_runtime_single.h"),
    ("packs/essentials/imb_essentials.h", "dist/imb_essentials_single.h"),
]
CHECK_FLAGS = ["-std=c++17", "-fsyntax-only", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-x", "c++"]
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"\s*(//.*)?$')
PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\s*$")
IF_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b")
ELSE_RE = re.compile(r"^\s*#\s*(else|elif)\b")
ENDIF_RE = re.compile(r"^\s*#\s*endif\b")


def resolve(name, from_dir):
    for c in [from_dir / name] + [d / name for d in SEARCH_DIRS]:
        if c.is_file():
            c = c.resolve()
            try:
                c.relative_to(ROOT)
            except ValueError:
                return None
            return c
    return None


def guard_name(path):
    rel = path.relative_to(ROOT).as_posix()
    return "IMFORGE_AMALG_" + re.sub(r"[^A-Za-z0-9]", "_", rel).upper()


class Amalgamator:
    def __init__(self):
        self.out = []
        self.done = {}
        self.stack = []
        self.cond = []
        self.next_id = 0

    def emit(self, line):
        self.out.append(line)

    def new_id(self):
        self.next_id += 1
        return self.next_id

    def is_done(self, path):
        ctx = self.done.get(path)
        return ctx is not None and tuple(self.cond[: len(ctx)]) == ctx

    def inline(self, path):
        if path in self.stack or self.is_done(path):
            return
        self.done[path] = tuple(self.cond)
        rel = path.relative_to(ROOT).as_posix()
        guard = guard_name(path)
        self.stack.append(path)
        self.emit(f"#ifndef {guard}")
        self.emit(f"#define {guard}")
        self.emit(f"// ---- begin {rel} ----")
        self.process_file(path)
        self.emit(f"// ---- end {rel} ----")
        self.emit(f"#endif  // {guard}")
        self.stack.pop()

    def process_file(self, path):
        lines = path.read_text(encoding="utf-8").split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        base = len(self.cond)
        verbatim_level = None
        for line in lines:
            if PRAGMA_ONCE_RE.match(line):
                continue
            if IF_RE.match(line):
                if verbatim_level is None and "__has_include" in line:
                    verbatim_level = len(self.cond)
                self.cond.append(self.new_id())
                self.emit(line)
                continue
            if ELSE_RE.match(line):
                if len(self.cond) <= base:
                    raise SystemExit(f"amalgamate: unbalanced #else in {path}")
                self.cond[-1] = self.new_id()
                self.emit(line)
                continue
            if ENDIF_RE.match(line):
                if len(self.cond) <= base:
                    raise SystemExit(f"amalgamate: unbalanced #endif in {path}")
                self.cond.pop()
                if verbatim_level is not None and len(self.cond) == verbatim_level:
                    verbatim_level = None
                self.emit(line)
                continue
            if verbatim_level is None:
                m = INCLUDE_RE.match(line)
                if m:
                    target = resolve(m.group(1), path.parent)
                    if target is not None:
                        self.inline(target)
                        continue
            self.emit(line)
        if len(self.cond) != base:
            raise SystemExit(f"amalgamate: unbalanced #if/#endif in {path}")


def amalgamate(entry):
    a = Amalgamator()
    rel = entry.relative_to(ROOT).as_posix()
    a.emit("#pragma once")
    a.emit(f"// Amalgamated from {rel} by tools/amalgamate.py. Do not edit; edit the sources and regenerate.")
    a.inline(entry.resolve())
    return "\n".join(a.out) + "\n"


def run_check(args, outputs):
    imgui_dirs = [pathlib.Path(os.path.expanduser(d)) for d in args.imgui_dirs]
    imgui_dirs = [d for d in imgui_dirs if (d / "imgui.h").is_file()]
    if not imgui_dirs:
        print("amalgamate: --check: no Dear ImGui checkout found in", args.imgui_dirs, file=sys.stderr)
        return 1
    failures = 0
    for out_path in outputs:
        for imgui in imgui_dirs:
            for define in ([], ["-DIMFORGE_LIVE"]):
                cmd = [args.compiler, *CHECK_FLAGS, f"-I{imgui}", *define, str(out_path)]
                res = subprocess.run(cmd, capture_output=True, text=True)
                stderr = "\n".join(l for l in res.stderr.splitlines() if "pragma once in main file" not in l)
                label = f"{out_path.name} vs {imgui.name} {' '.join(define) or '(no IMFORGE_LIVE)'}"
                if res.returncode != 0 or "warning:" in stderr or "error:" in stderr:
                    failures += 1
                    print(f"FAIL {label}\n{stderr}")
                else:
                    print(f"ok   {label}")
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", default=str(ROOT / "dist"))
    ap.add_argument("--check", action="store_true", help="compile each output with the compiler")
    ap.add_argument("--compiler", default="g++")
    ap.add_argument("--imgui-dirs", nargs="*", default=["~/deps/imgui-v1.85", "~/deps/imgui-v1.91.9b"])
    args = ap.parse_args()
    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    outputs = []
    for src, dst in OUTPUTS:
        entry = ROOT / src
        if not entry.is_file():
            print(f"amalgamate: skipping {src} (missing)", file=sys.stderr)
            continue
        dst_path = out_dir / pathlib.Path(dst).name
        text = amalgamate(entry)
        dst_path.write_text(text, encoding="utf-8", newline="\n")
        outputs.append(dst_path)
        print(f"wrote {dst_path} ({len(text.splitlines())} lines)")
    if args.check:
        return run_check(args, outputs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
