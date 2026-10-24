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
        bad = []
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
