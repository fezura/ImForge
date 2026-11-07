#!/usr/bin/env python3
"""Pack or unpack an .imblib widget library (manifest is validated, the zip is byte-for-byte reproducible).
usage: pack_imblib.py <pack_dir> <out.imblib>
       pack_imblib.py --unpack <file.imblib> <out_dir>
"""
import json
import os
import posixpath
import sys
import zipfile

FIXED_TIME = (1980, 1, 1, 0, 0, 0)
TEXT_SUFFIXES = (".json", ".h", ".hpp", ".md", ".txt")
REQUIRED_KEYS = ("schemaVersion", "kind", "name", "version")


def fail(msg):
    sys.stderr.write("pack_imblib: " + msg + "\n")
    sys.exit(1)


def load_json(path):
    try:
        with open(path, "rb") as f:
            return json.loads(f.read().decode("utf-8"))
    except (OSError, ValueError, UnicodeDecodeError) as e:
        fail("%s: %s" % (path, e))


def norm_rel(rel):
    rel = rel.replace("\\", "/")
    if rel.startswith("/") or rel.split("/")[0] in ("", "..") or ".." in rel.split("/"):
        return None
    return posixpath.normpath(rel)


def collect_files(pack_dir, manifest):
    files = ["manifest.json"]
    for key in ("components", "widgets", "headers"):
        refs = manifest.get(key, [])
        if not isinstance(refs, list) or not all(isinstance(r, str) for r in refs):
            fail('manifest "%s" must be a list of strings' % key)
        for ref in refs:
            rel = norm_rel(ref)
            if rel is None:
                fail('manifest "%s" entry "%s" must be a relative path in the pack' % (key, ref))
            if not os.path.isfile(os.path.join(pack_dir, rel)):
                fail('manifest "%s" references missing file "%s"' % (key, ref))
            if rel not in files:
                files.append(rel)
    for extra in ("README.md", "LICENSE"):
        if os.path.isfile(os.path.join(pack_dir, extra)) and extra not in files:
            files.append(extra)
    return files


def validate_manifest(pack_dir, manifest):
    for key in REQUIRED_KEYS:
        if key not in manifest:
            fail('manifest.json is missing "%s"' % key)
    if manifest["schemaVersion"] != 1:
        fail("manifest.json schemaVersion must be 1")
    if manifest["kind"] != "imblib":
        fail('manifest.json kind must be "imblib"')
    if not isinstance(manifest["name"], str) or not manifest["name"]:
        fail("manifest.json name must be a non-empty string")
    if not isinstance(manifest["version"], str) or not manifest["version"]:
        fail("manifest.json version must be a non-empty string")
    if not isinstance(manifest.get("minImGuiVersion", 18500), int):
        fail("manifest.json minImGuiVersion must be an integer")
    if not isinstance(manifest.get("dependencies", []), list):
        fail("manifest.json dependencies must be a list")
    header_names = {posixpath.basename(norm_rel(h) or "") for h in manifest.get("headers", [])}
    for ref in manifest.get("widgets", []):
        w = load_json(os.path.join(pack_dir, norm_rel(ref)))
        if not isinstance(w, dict) or w.get("kind") != "widget":
            fail('%s: kind must be "widget"' % ref)
        if w.get("schemaVersion") != 1:
            fail("%s: schemaVersion must be 1" % ref)
        for key in ("name", "call"):
            if not isinstance(w.get(key), str) or not w[key]:
                fail('%s: missing "%s"' % (ref, key))
        if w.get("returns", "void") not in ("changed", "clicked", "open", "void"):
            fail('%s: "returns" must be changed|clicked|open|void' % ref)
        inc = w.get("include")
        if inc and posixpath.basename(inc) not in header_names:
            fail('%s: include "%s" is not one of the packed headers' % (ref, inc))
        if w.get("container") and not isinstance(w.get("callEnd", ""), str):
            fail('%s: "callEnd" must be a string' % ref)
    for ref in manifest.get("components", []):
        c = load_json(os.path.join(pack_dir, norm_rel(ref)))
        if not isinstance(c, dict) or c.get("kind") != "component":
            fail('%s: kind must be "component"' % ref)


def read_entry(path, rel):
    with open(path, "rb") as f:
        data = f.read()
    if rel.lower().endswith(TEXT_SUFFIXES):
        data = data.replace(b"\r\n", b"\n")
    return data


def pack(pack_dir, out_path):
    if not os.path.isdir(pack_dir):
        fail("%s is not a directory" % pack_dir)
    manifest_path = os.path.join(pack_dir, "manifest.json")
    if not os.path.isfile(manifest_path):
        fail("%s has no manifest.json" % pack_dir)
    manifest = load_json(manifest_path)
    if not isinstance(manifest, dict):
        fail("manifest.json must be an object")
    validate_manifest(pack_dir, manifest)
    files = sorted(set(collect_files(pack_dir, manifest)))
    out_dir = os.path.dirname(os.path.abspath(out_path))
    os.makedirs(out_dir, exist_ok=True)
    tmp = out_path + ".tmp"
    with zipfile.ZipFile(tmp, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for rel in files:
            info = zipfile.ZipInfo(rel, date_time=FIXED_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (0o644 << 16)
            zf.writestr(info, read_entry(os.path.join(pack_dir, rel), rel))
    os.replace(tmp, out_path)
    print("packed %s (%d files) -> %s" % (manifest["name"], len(files), out_path))


def unpack(archive, out_dir):
    if not zipfile.is_zipfile(archive):
        fail("%s is not a zip/imblib file" % archive)
    with zipfile.ZipFile(archive) as zf:
        names = zf.namelist()
        if "manifest.json" not in names:
            fail("%s has no manifest.json" % archive)
        for name in names:
            if norm_rel(name) is None or name.endswith("/"):
                fail("refusing to extract unsafe entry %s" % name)
        os.makedirs(out_dir, exist_ok=True)
        for name in sorted(names):
            dest = os.path.join(out_dir, *norm_rel(name).split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with zf.open(name) as src, open(dest, "wb") as dst:
                dst.write(src.read())
    print("unpacked %d files -> %s" % (len(names), out_dir))


def main(argv):
    if len(argv) == 4 and argv[1] == "--unpack":
        unpack(argv[2], argv[3])
    elif len(argv) == 3 and not argv[1].startswith("-"):
        pack(argv[1], argv[2])
    else:
        sys.stderr.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main(sys.argv)
