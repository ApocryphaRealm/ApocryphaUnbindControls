"""Check Unbind Vanilla Controls' built-in adapter rows (1.2.0) against the mods they write to.

Every adapter in source/Functions.cpp BuiltInAdapters() names a file, a section and a key in ANOTHER mod's settings. A
wrong name there would make the row adopt nothing and write a setting the mod never reads, silently. So for each adapter:

  1. the fixture under tools/adapter-fixtures/<Name>/ (a few hand-written lines that state where that mod keeps its
     key - facts, not a copy of the mod's files) must hold the section and key, and its "no key" value;
  2. when the mod is installed in an MO2 instance (--instance, default D:/modlists/Njordlinger), the mod's OWN file
     must hold them too: the MCM Helper defaults (MCM/Config/<id>/settings.ini) for an MCM\\Settings target, else the
     target file itself; and the RequiresFile must be in that mod's folder. Mods that are not installed are reported
     as "not installed here" (checked against the fixture only).

    python tools/check_adapters.py [--instance <MO2 instance>]      exit 0 = every adapter checks out
"""
import argparse, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SRC = os.path.join(REPO, "source", "Functions.cpp")
FIX = os.path.join(HERE, "adapter-fixtures")


def adapters():
    """[(name, file, section, key, modifierKey, none, requires, options)] from BuiltInAdapters()."""
    text = open(SRC, encoding="utf-8").read()
    body = text[text.index("std::vector<Function> BuiltInAdapters()"):text.index("int MergeBuiltIns(")]
    out = []
    call = re.compile(r'add\("([^"]+)",\s*target\("([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*(-?\d+),\s*"([^"]*)"\)\)')
    for m in call.finditer(body):
        out.append(m.groups()[:5] + (int(m.group(6)), m.group(7), ""))
    # a target built in a block (True Directional Movement): Target t = target(...); ... add("Name", std::move(t));
    block = re.compile(r'Target t = target\("([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*(-?\d+),\s*"([^"]*)"\);(.*?)add\("([^"]+)",\s*std::move\(t\)\);', re.S)
    for m in block.finditer(body):
        opts = []
        if "gamepadFirst = true" in m.group(7):
            opts.append("padfirst")
        for s, k, b, u, pad in re.findall(r't\.switches\.push_back\(\{\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*(true|false)\s*\}\);', m.group(7)):
            opts.append("%s=%s:%s=%s/%s" % ("padswitch" if pad == "true" else "switch", s, k, b, u))
        d = re.search(r't\.defaultsFile = "([^"]*)";', m.group(7))
        if d:
            opts.append("defaults=" + d.group(1).replace("\\\\", "\\"))
        api = re.search(r't\.api = "([^"]*)";', m.group(7))
        if api:
            opts.append("api=" + api.group(1))
        idc = re.search(r't\.idCheck = "([^"]*)";', m.group(7))
        if idc:
            opts.append("idcheck=" + idc.group(1).replace("\\\\", "\\"))
        out.append((m.group(8),) + m.groups()[:4] + (int(m.group(5)), m.group(6), ";".join(opts)))
    # the C++ literals hold doubled backslashes
    out = [tuple(x.replace("\\\\", "\\") if isinstance(x, str) else x for x in row) for row in out]
    return out


def keys_in(path):
    """{(section_lower, key_lower): value} of an INI/TOML file ('' values kept)."""
    out, sec = {}, ""
    try:
        text = open(path, encoding="utf-8-sig", errors="replace").read()
    except OSError:
        return None
    for line in text.splitlines():
        t = line.strip()
        if t.startswith("[") and t.endswith("]"):
            sec = t[1:-1].strip().lower()
            continue
        if not t or t[0] in ";#" or "=" not in t:
            continue
        k, v = t.split("=", 1)
        out.setdefault((sec, k.strip().lower()), v.split(";")[0].split("#")[0].strip())
    return out


def mod_files(instance, rel):
    """Every copy of Data-relative path `rel` in the instance's mods folders."""
    rel = rel.replace("\\", os.sep)
    return [p for p in glob.glob(os.path.join(instance, "mods", "*", rel)) if os.path.isfile(p)]


def filtered_index_writes(text):
    """[function name] for every function in Functions.cpp that takes the INSTALLED-only list (GetPresentFunctions) and
    then writes g_functions[i] - an index into the filtered list used on the full one. 1.2.0's Adopt did exactly that:
    with an absent mod's built-in row in between, TK Dodge AIO's Left Shift and TDM's R3 were written into "Call Horse
    (Press H to Horse)" and "Dodge (TK Dodge RE)" (combined run 2026-10-23, batch 11)."""
    bad = []
    for m in re.finditer(r"\n\t(?:int|void|bool|std::\w+(?:<[^>]*>)?)\s+(\w+)\([^)]*\)\n\t\{(.*?)\n\t\}", text, re.S):
        name, body = m.group(1), m.group(2)
        if "GetPresentFunctions()" in body and re.search(r"g_functions\[\s*i\s*\]", body):
            bad.append(name)
    return bad


def check_input_manager_row(instance, name, file, key, none, requires, opts=""):
    """An api=InputManager row (1.2.0): its File must be Input Manager's DLL, its Key the actions the fixture names
    (adapter-fixtures/<Name>/inputmanager.ini, [Actions] actions / requires / idcheck), and its "no key" Input Manager's 0.
    The actions are NAMES - an id is a position in Input Manager's list and moves when a mod adds actions (test 4259) -
    and a mod's saved ids may only be an idcheck. Installed in the instance means both the mod's RequiresFile and
    InputManager.dll are in a mods folder."""
    bad = []
    if file.lower() != r"skse\plugins\inputmanager.dll":
        bad.append("File is %s, not SKSE\\Plugins\\InputManager.dll" % file)
    if key.lower().startswith("ids=") or any(x.strip().isdigit() for x in key.split(",")):
        bad.append("Key %r names Input Manager ids - they are positions that move; name the actions" % key)
    idcheck = next((o.split("=", 1)[1] for o in opts.split(";") if o.startswith("idcheck=")), "")
    fx = keys_in(os.path.join(FIX, name, "inputmanager.ini"))
    if fx is None:
        bad.append("no fixture %s" % os.path.join("tools", "adapter-fixtures", name, "inputmanager.ini"))
    else:
        if fx.get(("actions", "actions")) != key:
            bad.append("the fixture names the actions %r, the row %r" % (fx.get(("actions", "actions")), key))
        if fx.get(("actions", "requires"), "").lower() != requires.lower():
            bad.append("the fixture says the mod is %r, the row requires %r" % (fx.get(("actions", "requires")), requires))
        if fx.get(("actions", "idcheck"), "").lower() != idcheck.lower():
            bad.append("the fixture's idcheck is %r, the row's %r" % (fx.get(("actions", "idcheck"), ""), idcheck))
    nk = keys_in(os.path.join(FIX, name, "no-key.ini"))
    if nk is None or nk.get(("", "none")) != str(none) or none != 0:
        bad.append("no-key must be Input Manager's 0 (row says %d)" % none)
    live = "not installed here"
    if os.path.isdir(os.path.join(instance, "mods")):
        mod = mod_files(instance, requires)
        im = mod_files(instance, r"SKSE\Plugins\InputManager.dll")
        if mod and im:
            live = "installed with Input Manager (%s)" % os.path.relpath(os.path.dirname(mod[0]), os.path.join(instance, "mods"))
        elif im:
            live = "Input Manager installed, the mod is not"
    return bad, live


def check_forks(name, file, section, key, requires, opts):
    """adapter-fixtures/<Name>/forks.ini lists mods that REPLACE the row's mod (staalo18's TDM fork, Nexus 160556): each
    must ship the same DLL and keep the same file, section, key and switch settings, or the row would not catch it."""
    path = os.path.join(FIX, name, "forks.ini")
    if not os.path.isfile(path):
        return []
    bad, forks, sec = [], {}, None
    for line in open(path, encoding="utf-8-sig").read().splitlines():
        t = line.strip()
        if t.startswith("[") and t.endswith("]"):
            sec = t[1:-1]
            forks[sec] = {}
        elif sec and "=" in t and not t.startswith(";"):
            k, v = t.split("=", 1)
            forks[sec].setdefault(k.strip().lower(), []).append(v.strip())
    switches = [o.split("=", 1)[1].rsplit("=", 1)[0] for o in opts.split(";") if o.startswith(("padswitch=", "switch="))]
    for fork, f in forks.items():
        want = {"dll": [requires], "file": [file], "key": ["%s:%s" % (section, key)]}
        for k, v in want.items():
            if [x.lower() for x in f.get(k, [])] != [x.lower() for x in v]:
                bad.append("fork %s: %s %s, the row %s" % (fork, k, f.get(k), v))
        for s in f.get("switch", []):
            if s.lower() not in [x.lower() for x in switches]:
                bad.append("fork %s: switch %s is not one the row writes" % (fork, s))
    return bad


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--instance", default=r"D:\modlists\Njordlinger")
    a = p.parse_args()
    ads = adapters()
    fails, lines = 0, []
    if len(ads) < 4:
        print("FAIL: only %d adapters parsed out of BuiltInAdapters() - the parser and the source disagree" % len(ads))
        return 1
    for name, file, section, key, modkey, none, requires, opts in ads:
        if "api=InputManager" in opts.split(";"):
            bad, live = check_input_manager_row(a.instance, name, file, key, none, requires, opts)
            fails += bool(bad)
            lines.append("%s %-42s [Input Manager] %s - %s%s" % ("FAIL" if bad else "ok  ", name, key, live,
                                                                ("\n       " + "\n       ".join(bad)) if bad else ""))
            continue
        bad = []
        bad += check_forks(name, file, section, key, requires, opts)
        fx = os.path.join(FIX, name, file.replace("\\", os.sep))
        k = keys_in(fx)
        if k is None:
            bad.append("no fixture %s" % os.path.relpath(fx, REPO))
        else:
            for want in [key] + ([modkey] if modkey else []):
                if (section.lower(), want.lower()) not in k:
                    bad.append("fixture lacks [%s] %s" % (section, want))
            nk = keys_in(os.path.join(FIX, name, "no-key.ini"))
            if nk is None or nk.get(("", "none")) != str(none):
                bad.append("fixture no-key.ini does not say none=%d" % none)
            for o in filter(None, opts.split(";")):
                if o.startswith(("padswitch=", "switch=")):
                    s, rest = o.split("=", 1)[1].split(":", 1)
                    sk = rest.split("=", 1)[0]
                    if (s.lower(), sk.lower()) not in k:
                        bad.append("fixture lacks the switch setting [%s] %s" % (s, sk))
        # the mod's own files, when it is installed in the instance
        live = "not installed here"
        if os.path.isdir(os.path.join(a.instance, "mods")):
            req = mod_files(a.instance, requires) or mod_files(a.instance, os.path.join("optional", os.path.basename(requires)))
            if req:
                m = re.match(r"(?i)^MCM\\Settings\\([^\\]+)\.ini$", file)
                src = mod_files(a.instance, "MCM\\Config\\%s\\settings.ini" % m.group(1)) if m else mod_files(a.instance, file)
                if not src:
                    bad.append("installed, but no %s in its folder" % ("MCM Config defaults" if m else file))
                else:
                    lk = keys_in(src[0])
                    for want in [key] + ([modkey] if modkey else []):
                        if (section.lower(), want.lower()) not in lk:
                            bad.append("the installed mod's %s lacks [%s] %s" % (os.path.relpath(src[0], a.instance), section, want))
                    live = "installed: %s says %s=%s" % (os.path.relpath(src[0], os.path.join(a.instance, "mods")), key,
                                                          lk.get((section.lower(), key.lower()), "?"))
        status = "FAIL" if bad else "ok  "
        fails += bool(bad)
        lines.append("%s %-42s [%s] %s%s - %s%s" % (status, name, section, key, (" + " + modkey) if modkey else "", live,
                                                   ("\n       " + "\n       ".join(bad)) if bad else ""))
    print("\n".join(lines))
    print("%d adapter(s), %d failed" % (len(ads), fails))

    # the filtered-list case (batch 11): an index into the installed-only list must never be used on the full list
    src = open(SRC, encoding="utf-8").read()
    bad = filtered_index_writes(src)
    print(("FAIL" if bad else "ok  ") + " filtered-list index: " +
          ("%s take GetPresentFunctions() and write g_functions[i] - the index of the filtered list used on the full one"
           % ", ".join(bad) if bad else "no function writes g_functions[i] from the installed-only list"))
    # and the shipped order still has an absent-mod row BEFORE an installed one somewhere (the case that exposed it):
    # in Njordlinger, Press H to Horse's ESP is not in Data while TK Dodge AIO and TDM are installed
    names = [a[0] for a in ads]
    order_ok = names.index("Call Horse (Press H to Horse)") < names.index("Dodge (TK Dodge AIO)") < \
        names.index("Target Lock (True Directional Movement)")
    print(("ok  " if order_ok else "note") + " the built-in order keeps an absent-mod row ahead of installed ones (the batch-11 shape)")
    return 1 if fails or bad else 0


if __name__ == "__main__":
    sys.exit(main())
