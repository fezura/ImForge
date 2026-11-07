#!/usr/bin/env python3
"""Lint ImForge JSON: canonical formatting, schemas, and the cross-file rules schemas can't express.

Usage: python tools/validate_json.py [files or dirs...] [--strict] [--no-schema]   (exit 1 on errors)
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA_DIR = os.path.join(ROOT, "schemas")
SCHEMA_ID_BASE = "https://raw.githubusercontent.com/fezura/ImForge/main/schemas/"
DEFAULT_DIRS = ["examples", "templates", "packs"]
KIND_SCHEMA = {
    "project": "project.schema.json", "panel": "panel.schema.json", "component": "component.schema.json",
    "widget": "widget.schema.json", "theme": "theme.schema.json", "imblib": "imblib-manifest.schema.json",
    "types": "types.schema.json", "templates": "templates.schema.json",
}
PLACEHOLDERS = {"{{PROJECT_NAME}}": "SampleProject", "{{NAMESPACE}}": "ui"}
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PATH_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*|\[[^\]]+\])*$")
COLOR_RE = re.compile(r"^#[0-9a-fA-F]{8}$")
HEXISH_RE = re.compile(r"^#[0-9a-fA-F]+$")
STRING_LIT_RE = re.compile(r'"(?:[^"\\]|\\.)*"')
ENUM_LIT_RE = re.compile(r"\b[A-Za-z_]\w*\s*::\s*[A-Za-z_]\w*")
INNER_INDEX_RE = re.compile(r"\[([^\[\]]*)\]")
PATH_TOKEN_RE = re.compile(r"(?<![\w.])[A-Za-z_]\w*(?:\.[A-Za-z_]\w*|\[[^\]]*\])*")
INTERP_RE = re.compile(r"\{([^{}]*)\}")
COL_ALIASES = {"TabActive": "TabSelected", "TabUnfocused": "TabDimmed", "TabUnfocusedActive": "TabDimmedSelected", "NavHighlight": "NavCursor"}
COLOR_PROPS = {("Text", "color"), ("ColorButton", "color"), ("Image", "tint"), ("Image", "border"), ("ImageButton", "bg"), ("ImageButton", "tint")}
VEC_FIELDS = {"Vec2": ("x", "y"), "Vec4": ("x", "y", "z", "w")}
SEQ_KINDS = {"Vector", "Array", "FloatArray", "IntArray", "CharArray"}
PRIMITIVE_CTYPES = {"bool": "Bool", "int": "I32", "float": "Float", "double": "Double", "std::string": "StdString", "ImVec2": "Vec2", "ImVec4": "Vec4"}


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, where, msg):
        self.errors.append("%s: %s" % (where, msg))

    def warn(self, where, msg):
        self.warnings.append("%s: %s" % (where, msg))


def rel_path(path):
    return os.path.relpath(path, ROOT).replace("\\", "/")


def collect_files(targets):
    out = []
    for t in targets:
        p = t if os.path.isabs(t) else os.path.join(ROOT, t)
        if os.path.isdir(p):
            for dirpath, dirnames, filenames in os.walk(p):
                dirnames.sort()
                for f in sorted(filenames):
                    if f.endswith(".json"):
                        out.append(os.path.join(dirpath, f))
        elif os.path.isfile(p):
            out.append(p)
    return sorted(set(out), key=rel_path)


def substitute(obj):
    if isinstance(obj, str):
        for k, v in PLACEHOLDERS.items():
            obj = obj.replace(k, v)
        return obj
    if isinstance(obj, list):
        return [substitute(x) for x in obj]
    if isinstance(obj, dict):
        return {k: substitute(v) for k, v in obj.items()}
    return obj


def check_format(rel, text, obj, rep):
    if "\r" in text:
        rep.error(rel, "CRLF line endings; use LF")
    if not text.endswith("\n"):
        rep.error(rel, "missing trailing newline")
    canonical = json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    canonical_utf8 = json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    if text.replace("\r\n", "\n") not in (canonical, canonical_utf8):
        rep.error(rel, "not canonically formatted (2-space indent, sorted keys, one property per line)")


def load_files(paths, rep):
    files = {}
    for p in paths:
        rel = rel_path(p)
        try:
            with open(p, "r", encoding="utf-8", newline="") as f:
                text = f.read()
            obj = json.loads(text)
        except (OSError, ValueError) as e:
            rep.error(rel, "cannot parse: %s" % e)
            continue
        check_format(rel, text, obj, rep)
        if rel.startswith("templates/"):
            obj = substitute(obj)
        files[rel] = obj
    return files


def load_schemas(rep):
    schemas = {}
    for name in sorted(os.listdir(SCHEMA_DIR)) if os.path.isdir(SCHEMA_DIR) else []:
        if not name.endswith(".json"):
            continue
        p = os.path.join(SCHEMA_DIR, name)
        rel = rel_path(p)
        try:
            with open(p, "r", encoding="utf-8", newline="") as f:
                text = f.read()
            obj = json.loads(text)
        except (OSError, ValueError) as e:
            rep.error(rel, "cannot parse schema: %s" % e)
            continue
        check_format(rel, text, obj, rep)
        if obj.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
            rep.error(rel, "$schema must be draft 2020-12")
        if not str(obj.get("$id", "")).startswith(SCHEMA_ID_BASE):
            rep.error(rel, "$id must start with " + SCHEMA_ID_BASE)
        schemas[name] = obj
    return schemas


def make_validators(schemas, rep):
    try:
        import jsonschema
        from referencing import Registry, Resource
        from referencing.jsonschema import DRAFT202012
    except ImportError:
        return None
    resources = []
    for name, s in schemas.items():
        try:
            jsonschema.Draft202012Validator.check_schema(s)
        except jsonschema.exceptions.SchemaError as e:
            rep.error("schemas/" + name, "invalid schema: %s" % e.message)
        resources.append((s["$id"], Resource.from_contents(s, default_specification=DRAFT202012)))
    registry = Registry().with_resources(resources)
    out = {}
    for kind, name in KIND_SCHEMA.items():
        if name in schemas:
            out[kind] = jsonschema.Draft202012Validator(schemas[name], registry=registry)
    out["_module"] = jsonschema
    return out


def validate_against_schema(validators, kind, obj, rel, rep):
    v = validators.get(kind)
    if v is None:
        rep.warn(rel, "no schema for kind %r" % kind)
        return
    jsonschema = validators["_module"]
    for err in sorted(v.iter_errors(obj), key=lambda e: list(map(str, e.absolute_path))):
        best = jsonschema.exceptions.best_match([err]) or err
        loc = "/".join(str(x) for x in best.absolute_path) or "<root>"
        msg = best.message if len(best.message) < 240 else best.message[:240] + "..."
        rep.error(rel, "schema: %s: %s" % (loc, msg))


def schema_enum(schemas, file, *keys):
    cur = schemas.get(file, {})
    for k in keys:
        cur = cur.get(k, {}) if isinstance(cur, dict) else {}
    return cur if isinstance(cur, list) else list(cur.keys()) if isinstance(cur, dict) else []


class Catalog:
    def __init__(self, schemas):
        self.stock = set(schema_enum(schemas, "stock-widgets.schema.json", "$defs", "StockWidgetType", "enum"))
        self.containers = set(schema_enum(schemas, "stock-widgets.schema.json", "$defs", "ContainerType", "enum"))
        self.cols = set(schema_enum(schemas, "node.schema.json", "$defs", "ColorMap", "properties")) | set(COL_ALIASES)
        vars_props = schemas.get("node.schema.json", {}).get("$defs", {}).get("StyleVarMap", {}).get("properties", {})
        self.vars = {k: ("$ref" in v) for k, v in vars_props.items()}
        if not self.stock:
            self.stock = {"Window", "VStack", "HStack", "Group", "Child", "Grid", "TabBar", "Tab", "CollapsingHeader", "TreeNode", "Popup", "Modal", "MenuBar", "Menu", "Canvas", "Component", "Slot", "Code",
                          "Text", "SeparatorText", "Separator", "Spacing", "NewLine", "Dummy", "Button", "SmallButton", "ArrowButton", "Checkbox", "RadioButton", "Combo", "ListBox", "SliderFloat", "SliderInt",
                          "VSliderFloat", "VSliderInt", "DragFloat", "DragInt", "InputText", "InputTextMultiline", "InputInt", "InputFloat", "ColorEdit", "ColorPicker", "ColorButton", "ProgressBar", "Image",
                          "ImageButton", "Selectable", "MenuItem", "PlotLines", "PlotHistogram"}
            self.containers = {"Window", "VStack", "HStack", "Group", "Child", "Grid", "TabBar", "Tab", "CollapsingHeader", "TreeNode", "Popup", "Modal", "MenuBar", "Menu", "Canvas"}


class TreeCtx:
    def __init__(self, rel, rep, catalog, kind, params, state, components, fonts, classes, types, slots):
        self.rel, self.rep, self.catalog, self.kind = rel, rep, catalog, kind
        self.params, self.state, self.components = params, state, components
        self.fonts, self.classes, self.types, self.slots = fonts, classes, types, slots
        self.ids = {}
        self.seen_slots = set()

    def error(self, node, msg):
        self.rep.error(self.rel, "node %r: %s" % (node.get("id", "?"), msg))

    def warn(self, node, msg):
        self.rep.warn(self.rel, "node %r: %s" % (node.get("id", "?"), msg))


def iter_nodes(node, parent=None):
    yield node, parent
    for c in node.get("children", []) or []:
        if isinstance(c, dict):
            for item in iter_nodes(c, node):
                yield item
    for nodes in (node.get("slots", {}) or {}).values():
        for c in nodes or []:
            if isinstance(c, dict):
                for item in iter_nodes(c, node):
                    yield item


def struct_of(ctx, type_name):
    if not ctx.types:
        return None
    return ctx.types.get("structs", {}).get(type_name)


def field_ctype(field):
    return {"ctype": field.get("ctype"), "type": field.get("type"), "elem": field.get("elem"), "count": field.get("count")}


def elem_ctype(cur):
    ck = cur["ctype"]
    if ck == "FloatArray":
        return {"ctype": "Float", "type": "float"}
    if ck == "IntArray":
        return {"ctype": "I32", "type": "int"}
    if ck == "CharArray":
        return {"ctype": "I8", "type": "char"}
    elem = cur.get("elem") or ""
    return {"ctype": PRIMITIVE_CTYPES.get(elem, "Struct"), "type": elem}


def resolve_segments(ctx, node, path, cur, segments):
    for seg in segments:
        ck = cur.get("ctype")
        if seg.startswith("["):
            if ck not in SEQ_KINDS:
                ctx.error(node, "path %r indexes a non-array field of type %s" % (path, cur.get("type")))
                return None
            cur = elem_ctype(cur)
            continue
        if ck in SEQ_KINDS and seg == "size":
            return {"ctype": "I32", "type": "int"}
        if ck in VEC_FIELDS:
            if seg not in VEC_FIELDS[ck]:
                ctx.error(node, "path %r: %s has no member %r" % (path, ck, seg))
                return None
            cur = {"ctype": "Float", "type": "float"}
            continue
        if ck != "Struct":
            ctx.error(node, "path %r: cannot access %r on %s" % (path, seg, cur.get("type")))
            return None
        st = struct_of(ctx, cur.get("type"))
        if st is None:
            ctx.warn(node, "path %r: struct %r is not described in types.json; cannot check further" % (path, cur.get("type")))
            return None
        field = next((f for f in st.get("fields", []) if f.get("name") == seg), None)
        if field is None:
            ctx.error(node, "path %r: struct %s has no field %r" % (path, cur.get("type"), seg))
            return None
        cur = field_ctype(field)
    return cur


def split_path(path):
    head = re.match(r"^[A-Za-z_]\w*", path).group(0)
    rest = path[len(head):]
    segments = re.findall(r"\.([A-Za-z_]\w*)|(\[[^\]]+\])", rest)
    return head, [a or b for a, b in segments]


def check_path(ctx, node, path, where):
    # returns the ctype dict, or None when it can't be resolved
    if not isinstance(path, str) or not PATH_RE.match(path):
        ctx.error(node, "%s: invalid path %r" % (where, path))
        return None
    if ctx.kind == "component":
        ctx.error(node, "%s: component trees must reference bindings through {\"param\": ...}, got %r" % (where, path))
        return None
    root, segments = split_path(path)
    if root == "state":
        if not segments or segments[0].startswith("["):
            ctx.error(node, "%s: path %r must name a state variable" % (where, path))
            return None
        st = ctx.state.get(segments[0])
        if st is None:
            ctx.error(node, "%s: unknown state variable %r in %r" % (where, segments[0], path))
            return None
        kind = {"bool": "Bool", "int": "I32", "float": "Float", "double": "Double", "string": "StdString", "vec2": "Vec2", "vec4": "Vec4", "color": "Vec4"}[st]
        return resolve_segments(ctx, node, path, {"ctype": kind, "type": st}, segments[1:])
    if root not in ctx.params:
        ctx.error(node, "%s: path %r does not start with a panel param or state" % (where, path))
        return None
    ptype = ctx.params[root]
    if struct_of(ctx, ptype) is None:
        return None
    return resolve_segments(ctx, node, path, {"ctype": "Struct", "type": ptype}, segments)


def check_expr(ctx, node, expr, where):
    if not isinstance(expr, str) or not expr.strip():
        ctx.error(node, "%s: expression must be a non-empty string" % where)
        return
    s = STRING_LIT_RE.sub('""', expr)
    s = ENUM_LIT_RE.sub(" 0 ", s)
    if s.count("(") != s.count(")"):
        ctx.error(node, "%s: unbalanced parentheses in %r" % (where, expr))
    while True:
        m = INNER_INDEX_RE.search(s)
        if not m:
            break
        inner = m.group(1).strip()
        if inner and not inner.isdigit():
            check_expr(ctx, node, inner, where + " (index)")
        s = s[:m.start()] + "[0]" + s[m.end():]
    for m in PATH_TOKEN_RE.finditer(s):
        tok = m.group(0)
        if tok in ("true", "false"):
            continue
        check_path(ctx, node, tok, where)


def check_interpolation(ctx, node, text, where):
    if not isinstance(text, str):
        return
    t = text.replace("{{", "").replace("}}", "")
    if t.count("{") != t.count("}"):
        ctx.error(node, "%s: unbalanced braces in %r (use {{ and }} for literals)" % (where, text))
        return
    for m in INTERP_RE.finditer(t):
        inner = m.group(1).strip()
        if inner.startswith("icon:"):
            if not re.match(r"^icon:[a-z0-9-]+$", inner):
                ctx.warn(node, "%s: unusual icon name %r" % (where, inner))
            continue
        path, _, fmt = inner.partition(":")
        if not path:
            ctx.error(node, "%s: empty interpolation in %r" % (where, text))
            continue
        if fmt and not fmt.startswith("%"):
            ctx.warn(node, "%s: format %r should be a printf spec" % (where, fmt))
        check_expr(ctx, node, path, where)


def check_param_ref(ctx, node, ref, where, want_binding):
    name = ref.get("param")
    if ctx.kind != "component":
        ctx.error(node, "%s: {\"param\": %r} is only valid inside a component definition" % (where, name))
        return
    ptype = ctx.params.get(name)
    if ptype is None:
        ctx.error(node, "%s: unknown component param %r" % (where, name))
        return
    is_binding = ptype.startswith("binding<")
    if want_binding and not is_binding:
        ctx.error(node, "%s: bind slot references param %r of type %s; expected binding<T>" % (where, name, ptype))
    if not want_binding and is_binding:
        ctx.error(node, "%s: prop references binding param %r; binding params go in \"bind\"" % (where, name))


def check_value_colors(ctx, node, value, where):
    if isinstance(value, str):
        if HEXISH_RE.match(value) and not COLOR_RE.match(value):
            ctx.error(node, "%s: color %r must be #rrggbbaa" % (where, value))
    elif isinstance(value, list):
        for i, v in enumerate(value):
            check_value_colors(ctx, node, v, "%s[%d]" % (where, i))
    elif isinstance(value, dict):
        for k, v in value.items():
            check_value_colors(ctx, node, v, "%s.%s" % (where, k))


def check_props(ctx, node):
    ntype = node.get("type")
    for name, value in (node.get("props") or {}).items():
        where = "props.%s" % name
        if isinstance(value, dict) and "param" in value:
            check_param_ref(ctx, node, value, where, False)
        elif isinstance(value, dict) and "expr" in value:
            check_expr(ctx, node, value["expr"], where)
        elif isinstance(value, str):
            if ntype == "Code" and name == "code":
                continue
            if (ntype, name) in COLOR_PROPS and not COLOR_RE.match(value):
                ctx.error(node, "%s: color %r must be #rrggbbaa" % (where, value))
            check_interpolation(ctx, node, value, where)
        elif isinstance(value, list):
            for i, item in enumerate(value):
                if isinstance(item, str):
                    check_interpolation(ctx, node, item, "%s[%d]" % (where, i))


def check_binds(ctx, node):
    for slot, value in (node.get("bind") or {}).items():
        where = "bind.%s" % slot
        if isinstance(value, dict) and "param" in value:
            check_param_ref(ctx, node, value, where, True)
        elif isinstance(value, str):
            check_path(ctx, node, value, where)
        else:
            ctx.error(node, "%s: binding must be a path string or {\"param\": ...}" % where)


def check_action(ctx, node, ev, action):
    where = "events.%s" % ev
    t = action.get("type")
    target, value = action.get("target"), action.get("value")
    if t in ("set", "toggle"):
        ct = check_path(ctx, node, target, where + " target") if target else ctx.error(node, "%s: %s needs a target" % (where, t))
        if t == "toggle" and ct and ct.get("ctype") not in (None, "Bool"):
            ctx.error(node, "%s: toggle target %r is not a bool" % (where, target))
        if t == "set":
            if isinstance(value, dict) and "expr" in value:
                check_expr(ctx, node, value["expr"], where + " value")
            elif isinstance(value, dict) and "param" in value:
                check_param_ref(ctx, node, value, where + " value", False)
            else:
                check_value_colors(ctx, node, value, where + " value")
    elif t == "openPopup":
        info = ctx.ids.get(target)
        if not info or info[0] not in ("Popup", "Modal"):
            ctx.error(node, "%s: openPopup target %r is not a Popup or Modal node" % (where, target))
    elif t == "setTab":
        info = ctx.ids.get(target)
        if not info or info[0] != "TabBar":
            ctx.error(node, "%s: setTab target %r is not a TabBar node" % (where, target))
        tab = ctx.ids.get(value) if isinstance(value, str) else None
        if not tab or tab[0] != "Tab":
            ctx.error(node, "%s: setTab value %r is not a Tab node id" % (where, value))
        elif info and tab[1] != target:
            ctx.error(node, "%s: Tab %r is not a child of TabBar %r" % (where, value, target))
    elif t == "copyClipboard":
        if isinstance(value, dict) and "expr" in value:
            check_expr(ctx, node, value["expr"], where + " value")
        elif isinstance(value, str):
            check_interpolation(ctx, node, value, where + " value")
        else:
            ctx.error(node, "%s: copyClipboard needs a string or expression value" % where)


def check_events(ctx, node):
    for ev, spec in (node.get("events") or {}).items():
        if not isinstance(spec, dict):
            continue
        if not spec.get("actions") and not spec.get("handler"):
            ctx.warn(node, "event %s has neither actions nor handler and is ignored" % ev)
        for a in spec.get("actions") or []:
            if isinstance(a, dict):
                check_action(ctx, node, ev, a)


def check_style_maps(ctx, node, style, where):
    cat = ctx.catalog
    for name, value in (style.get("colors") or {}).items():
        if name not in cat.cols:
            ctx.warn(node, "%s.colors: unknown ImGuiCol %r" % (where, name))
        if not (isinstance(value, str) and COLOR_RE.match(value)):
            ctx.error(node, "%s.colors.%s: color must be #rrggbbaa" % (where, name))
    for name, value in (style.get("vars") or {}).items():
        if name not in cat.vars:
            ctx.warn(node, "%s.vars: unknown ImGuiStyleVar %r" % (where, name))
        elif cat.vars[name] != isinstance(value, dict):
            ctx.error(node, "%s.vars.%s expects %s" % (where, name, "{x, y}" if cat.vars[name] else "a number"))
    for cls in style.get("classes") or []:
        if cls not in ctx.classes:
            ctx.error(node, "%s.classes: unknown style class %r" % (where, cls))


def check_anim(ctx, node):
    anim = node.get("anim") or {}
    for t in anim.get("transitions") or []:
        prop, to = t.get("prop", ""), t.get("to")
        if prop.startswith("color."):
            if prop[6:] not in ctx.catalog.cols:
                ctx.warn(node, "anim: unknown ImGuiCol in transition %r" % prop)
            if not (isinstance(to, str) and COLOR_RE.match(to)):
                ctx.error(node, "anim: transition %r needs a #rrggbbaa color in \"to\"" % prop)
        elif prop.startswith("var."):
            var = prop[4:]
            if var not in ctx.catalog.vars:
                ctx.warn(node, "anim: unknown ImGuiStyleVar in transition %r" % prop)
            elif ctx.catalog.vars[var] != isinstance(to, dict):
                ctx.error(node, "anim: transition %r has a \"to\" value of the wrong kind" % prop)
        if t.get("ease") == "Spring" and not (t.get("stiffness") or t.get("damping")):
            ctx.warn(node, "anim: Spring transition %r without stiffness/damping uses the defaults" % prop)


def check_component_instance(ctx, node):
    name = node.get("component")
    comp = ctx.components.get(name)
    if comp is None:
        ctx.error(node, "unknown component %r" % name)
        return
    if ctx.kind == "component" and name == ctx.self_name:
        ctx.error(node, "component %r cannot instantiate itself" % name)
    params = {p["name"]: p for p in comp.get("params", [])}
    for pname, value in (node.get("props") or {}).items():
        where = "props.%s" % pname
        pdef = params.get(pname)
        if pdef is None:
            ctx.error(node, "%s: component %s has no param %r" % (where, name, pname))
            continue
        ptype = pdef.get("type", "")
        if isinstance(value, dict) and "param" in value:
            check_param_ref(ctx, node, value, where, ptype.startswith("binding<"))
        elif ptype.startswith("binding<"):
            if isinstance(value, str):
                check_path(ctx, node, value, where)
            else:
                ctx.error(node, "%s: binding param needs a path string" % where)
        elif isinstance(value, dict) and "expr" in value:
            check_expr(ctx, node, value["expr"], where)
        elif ptype == "color" and not (isinstance(value, str) and COLOR_RE.match(value)):
            ctx.error(node, "%s: color param must be #rrggbbaa" % where)
        elif ptype == "string" and isinstance(value, str):
            check_interpolation(ctx, node, value, where)
        elif ptype.startswith("enum:") and value not in ptype[5:].split(","):
            ctx.error(node, "%s: %r is not one of %s" % (where, value, ptype[5:]))
    given = set(node.get("props") or {}) | set(node.get("bind") or {})
    for pname, pdef in params.items():
        if "default" not in pdef and pname not in given:
            ctx.error(node, "missing value for component param %r" % pname)
    slots = set(comp.get("slots", []))
    for sname in (node.get("slots") or {}):
        if sname not in slots:
            ctx.error(node, "component %s has no slot %r" % (name, sname))
    if node.get("children"):
        ctx.error(node, "Component instances take content through \"slots\", not \"children\"")


def check_structure(ctx, node, parent):
    t, pt = node.get("type"), parent.get("type") if parent else None
    cat = ctx.catalog
    if t not in cat.stock:
        ctx.warn(node, "type %r is not a stock widget (custom widget manifests are not checked here)" % t)
    if t == "Window" and parent is not None:
        ctx.error(node, "Window is only allowed as the panel root")
    if t == "Tab" and pt != "TabBar":
        ctx.error(node, "Tab must be a direct child of a TabBar")
    if pt == "TabBar" and t != "Tab":
        ctx.error(node, "TabBar children must be Tab nodes")
    if t == "MenuBar" and (pt != "Window" or not (parent.get("props") or {}).get("menuBar")):
        ctx.error(node, "MenuBar must be a direct child of a Window with menuBar = true")
    if pt == "Menu" and t not in ("MenuItem", "Menu", "Separator"):
        ctx.error(node, "Menu accepts only MenuItem, Menu or Separator children")
    if node.get("children") and t in cat.stock and t not in cat.containers:
        ctx.error(node, "%s cannot have children" % t)
    if node.get("slots") and t != "Component":
        ctx.error(node, "only Component instances have slots")
    if node.get("layout") and pt != "Canvas":
        ctx.warn(node, "layout is only used for direct children of a Canvas")
    if t == "Slot":
        sname = (node.get("props") or {}).get("name")
        if ctx.kind != "component":
            ctx.error(node, "Slot is only allowed inside a component definition")
        elif sname not in ctx.slots:
            ctx.error(node, "slot %r is not declared by the component" % sname)
        else:
            ctx.seen_slots.add(sname)


def check_node(ctx, node, parent):
    check_structure(ctx, node, parent)
    if node.get("type") == "Component":
        check_component_instance(ctx, node)
    else:
        check_props(ctx, node)
    check_binds(ctx, node)
    for key in ("visibleIf", "enabledIf"):
        if key in node:
            check_expr(ctx, node, node[key], key)
    if "tooltip" in node:
        check_interpolation(ctx, node, node["tooltip"], "tooltip")
    font = node.get("font")
    if font and ctx.fonts and font not in ctx.fonts:
        ctx.error(node, "unknown font %r (declare it in imforge.json fonts)" % font)
    if node.get("style"):
        check_style_maps(ctx, node, node["style"], "style")
    check_events(ctx, node)
    check_anim(ctx, node)
    for i, fx in enumerate(node.get("fx") or []):
        check_value_colors(ctx, node, fx, "fx[%d]" % i)


def check_tree(ctx, root, self_name=None):
    ctx.self_name = self_name
    nodes = list(iter_nodes(root))
    for node, parent in nodes:
        nid = node.get("id")
        if not isinstance(nid, str) or not IDENT_RE.match(nid):
            ctx.rep.error(ctx.rel, "invalid node id %r" % (nid,))
        elif nid in ctx.ids:
            ctx.rep.error(ctx.rel, "duplicate node id %r" % nid)
        else:
            ctx.ids[nid] = (node.get("type"), parent.get("id") if parent else None)
    for node, parent in nodes:
        check_node(ctx, node, parent)
    if ctx.kind == "component":
        for s in ctx.slots - ctx.seen_slots:
            ctx.rep.warn(ctx.rel, "declared slot %r has no Slot node" % s)


def check_declarations(rel, rep, obj):
    names = set()
    for key in ("params", "state"):
        for p in obj.get(key) or []:
            n = p.get("name")
            if n in names:
                rep.error(rel, "duplicate %s name %r" % (key, n))
            names.add(n)
            if n == "state" and key == "params":
                rep.error(rel, "param cannot be named 'state'")


def check_theme(rel, rep, obj, catalog):
    for name, value in obj.get("colors", {}).items():
        if name not in catalog.cols:
            rep.warn(rel, "unknown ImGuiCol %r" % name)
        if not (isinstance(value, str) and COLOR_RE.match(value)):
            rep.error(rel, "colors.%s must be #rrggbbaa" % name)
    for name, value in obj.get("vars", {}).items():
        if name not in catalog.vars:
            rep.warn(rel, "unknown ImGuiStyleVar %r" % name)
        elif catalog.vars[name] != isinstance(value, dict):
            rep.error(rel, "vars.%s expects %s" % (name, "{x, y}" if catalog.vars[name] else "a number"))


def check_types_file(rel, rep, obj):
    structs = obj.get("structs", {})
    enums = obj.get("enums", {})
    for sname, st in structs.items():
        seen = set()
        for f in st.get("fields", []):
            if f.get("name") in seen:
                rep.error(rel, "struct %s: duplicate field %r" % (sname, f.get("name")))
            seen.add(f.get("name"))
            ck = f.get("ctype")
            if ck == "Struct" and f.get("type") not in structs:
                rep.warn(rel, "struct %s: field %r has Struct type %r not described in this file" % (sname, f.get("name"), f.get("type")))
            if ck == "Enum" and f.get("type") not in enums:
                rep.warn(rel, "struct %s: field %r has Enum type %r not described in this file" % (sname, f.get("name"), f.get("type")))


def project_file(files, base, path):
    rel = (base + "/" + path) if base else path
    return rel, files.get(rel)


def check_project(rel, proj, files, rep, catalog):
    base = os.path.dirname(rel)
    themes, components, panels = {}, {}, []
    for key, kind, store in (("themes", "theme", themes), ("components", "component", components), ("panels", "panel", panels)):
        for path in proj.get(key, []):
            frel, obj = project_file(files, base, path)
            if obj is None:
                rep.error(rel, "%s entry %r does not exist or is not a JSON file" % (key, path))
                continue
            if obj.get("kind") != kind:
                rep.error(rel, "%s entry %r has kind %r, expected %r" % (key, path, obj.get("kind"), kind))
                continue
            if isinstance(store, dict):
                store[obj.get("name")] = obj
            else:
                store.append((frel, obj))
    active = proj.get("activeTheme")
    if active and active not in themes:
        rep.error(rel, "activeTheme %r is not among the listed themes" % active)
    types = None
    tj = (proj.get("binding") or {}).get("typesJson")
    if tj:
        trel, types = project_file(files, base, tj)
        if types is None:
            rep.error(rel, "binding.typesJson %r does not exist" % tj)
        elif types.get("kind") != "types":
            rep.error(rel, "binding.typesJson %r is not a types file" % tj)
            types = None
    for tname in (proj.get("binding") or {}).get("types", []):
        if types is not None and tname not in types.get("structs", {}):
            rep.warn(rel, "binding.types entry %r is not described in %s" % (tname, tj))
    for path in proj.get("libraries", []):
        if project_file(files, base, path)[1] is None and not os.path.isfile(os.path.join(ROOT, base, path)):
            rep.warn(rel, "library %r not found" % path)
    fonts = {f.get("name") for f in proj.get("fonts", [])}
    classes = set((proj.get("styleClasses") or {}).keys())
    for cname, cls in (proj.get("styleClasses") or {}).items():
        dummy = TreeCtx(rel, rep, catalog, "project", {}, {}, {}, fonts, classes, None, set())
        check_style_maps(dummy, {"id": "styleClasses." + cname}, cls, "styleClasses." + cname)
    for frel, panel in panels:
        check_declarations(frel, rep, panel)
        params = {p["name"]: p["type"] for p in panel.get("params", []) if "name" in p}
        state = {s["name"]: s["type"] for s in panel.get("state", []) if "name" in s}
        for pname, ptype in params.items():
            if types is not None and ptype not in types.get("structs", {}):
                rep.warn(frel, "param %r has type %r not described in types.json; its paths are not checked" % (pname, ptype))
        ctx = TreeCtx(frel, rep, catalog, "panel", params, state, components, fonts, classes, types, set())
        if isinstance(panel.get("root"), dict):
            check_tree(ctx, panel["root"])
    for cname, comp in components.items():
        crel = next((k for k, v in files.items() if v is comp), rel)
        check_declarations(crel, rep, comp)
        params = {p["name"]: p["type"] for p in comp.get("params", []) if "name" in p}
        ctx = TreeCtx(crel, rep, catalog, "component", params, {}, components, fonts, classes, None, set(comp.get("slots", [])))
        if isinstance(comp.get("root"), dict):
            check_tree(ctx, comp["root"], cname)


def check_templates_index(rel, obj, files, rep):
    ids = set()
    for t in obj.get("templates", []):
        if t.get("id") in ids:
            rep.error(rel, "duplicate template id %r" % t.get("id"))
        ids.add(t.get("id"))
        d = os.path.dirname(rel) + "/" + t.get("dir", "")
        if (d + "/imforge.json") not in files:
            rep.error(rel, "template %r has no %s/imforge.json" % (t.get("id"), d))


def main(argv):
    strict = "--strict" in argv
    use_schema = "--no-schema" not in argv
    targets = [a for a in argv if not a.startswith("--")] or DEFAULT_DIRS
    rep = Report()
    schemas = load_schemas(rep)
    catalog = Catalog(schemas)
    validators = make_validators(schemas, rep) if use_schema else None
    files = load_files(collect_files(targets), rep)
    referenced = set()
    for rel, obj in files.items():
        if not isinstance(obj, dict):
            rep.error(rel, "top level must be an object")
            continue
        if obj.get("schemaVersion") != 1:
            rep.error(rel, "schemaVersion must be 1")
        kind = obj.get("kind")
        if not isinstance(kind, str):
            rep.error(rel, "missing \"kind\"")
            continue
        if validators:
            validate_against_schema(validators, kind, obj, rel, rep)
        if kind == "theme":
            check_theme(rel, rep, obj, catalog)
        elif kind == "types":
            check_types_file(rel, rep, obj)
        elif kind == "templates":
            check_templates_index(rel, obj, files, rep)
    for rel, obj in files.items():
        if isinstance(obj, dict) and obj.get("kind") == "project":
            check_project(rel, obj, files, rep, catalog)
            base = os.path.dirname(rel)
            for key in ("panels", "components", "themes"):
                referenced.update(project_file(files, base, p)[0] for p in obj.get(key, []))
    for rel, obj in files.items():
        if isinstance(obj, dict) and obj.get("kind") in ("panel", "component") and rel not in referenced:
            rep.warn(rel, "not referenced by any imforge.json; binding paths were not checked")
    for w in rep.warnings:
        print("WARN  " + w)
    for e in rep.errors:
        print("ERROR " + e)
    if validators:
        from importlib.metadata import version
        mode = "jsonschema %s" % version("jsonschema")
    else:
        mode = "jsonschema not available, schema validation skipped"
    print("validate_json: %d files, %d schemas (%s), %d errors, %d warnings" % (len(files), len(schemas), mode, len(rep.errors), len(rep.warnings)))
    return 1 if rep.errors or (strict and rep.warnings) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
