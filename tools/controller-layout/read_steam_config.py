r"""Read-only: which physical control on a Steam Input config sends which XInput button.
    py -3 -I read_steam_config.py <controller config .vdf>
Prints no titles, only sources, modes and bindings.
"""
import re
import sys


def parse(text):
    tok = re.compile(r'"((?:[^"\\]|\\.)*)"|([{}])')
    stack = [[]]
    pending = None
    for m in tok.finditer(text):
        if m.group(2) == "{":
            new = []
            stack[-1].append((pending, new))
            stack.append(new)
            pending = None
        elif m.group(2) == "}":
            stack.pop()
        elif pending is None:
            pending = m.group(1)
        else:
            stack[-1].append((pending, m.group(1)))
            pending = None
    return stack[0]


def get(node, key):
    return [v for k, v in node if k == key]


root = get(parse(open(sys.argv[1], encoding="utf-8", errors="replace").read()), "controller_mappings")[0]
print("controller_type", get(root, "controller_type"), "revision", get(root, "revision"), "version", get(root, "version"))
groups = {get(g, "id")[0]: g for g in get(root, "group")}
presets = get(root, "preset")
print("presets", len(presets))
out = {}
for preset in presets:
    print("preset id", get(preset, "id"))
    for gid, src in get(preset, "group_source_bindings")[0]:
        source, *flags = src.split()
        group = groups.get(gid)
        mode = get(group, "mode")[0] if group else "?"
        active = "active" in flags and "inactive" not in flags
        line = []
        if group and get(group, "inputs"):
            for name, body in get(group, "inputs")[0]:
                for acts in get(body, "activators"):
                    for act, abody in acts:
                        for binds in get(abody, "bindings"):
                            for _, b in binds:
                                line.append(f"{name}/{act}: {b.strip(' ,')}")
        print(f"  [{'ACTIVE' if active else 'off   '}] {source:16} {mode:16} {flags[1:] or ''} group {gid}")
        if active:
            for ln in line:
                print("        ", ln)
        else:
            print("         ", len(line), "bindings")
        # a stick mode's own output
        if active and group and get(group, "settings"):
            for k, v in get(group, "settings")[0]:
                if k in ("output_joystick", "output_trigger", "curve_exponent", "virtual_stick"):
                    print("         setting", k, v)
