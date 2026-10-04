#!/usr/bin/env python3
"""The package declares what the core reads: waterbox.config's controls, in
order, are the ones vita3k_driver.h maps; its renderer defaults to an "-hw"
one, so a frontend hands it the GPU it cannot run without; its firmware ids are the files
wbx-entry.cpp opens; its language options are wbx-entry.cpp's, in the
Vita's own order; every setting it declares is one the core or the engine
reads; and the keybinds name the declared controller and only its controls.

  check-declaration.py <waterbox dir>     exit 1 on any mismatch
SPDX-License-Identifier: MIT
"""
import json
import re
import sys


def main():
    here = sys.argv[1]
    cfg = json.load(open(here + "/waterbox.config"))
    binds = json.load(open(here + "/default_keybinds.json"))
    slots = json.load(open(here + "/file_slots.json"))
    header = open(here + "/driver/vita3k_driver.h").read()
    entry = open(here + "/driver/wbx-entry.cpp").read()
    bad = []

    # the controls, as the driver documents and maps them
    doc = re.search(r"// Buttons: (.*?)\.\n// Axes:", header, re.S).group(1)
    driver_buttons = [b.strip() for b in re.sub(r"\s*//\s*", " ", doc).replace(" and ", ", ").split(",")]
    if cfg["input"]["buttons"] != driver_buttons:
        bad.append("buttons: config %s, driver %s" % (cfg["input"]["buttons"], driver_buttons))
    for name, n in (("BUTTONS", len(cfg["input"]["buttons"])), ("AXES", len(cfg["input"]["axes"]))):
        m = re.search(r"constexpr int %s = (\d+);" % name, header)
        if not m or int(m.group(1)) != n:
            bad.append("%s: config has %d, driver says %s" % (name, n, m.group(1) if m else "nothing"))
    neutral = re.search(r"g_axes\[chimera_vita3k::AXES\] = \{([^}]*)\}", entry).group(1)
    if [int(x) for x in neutral.split(",")] != [a["neutral"] for a in cfg["input"]["axes"]]:
        bad.append("axis neutrals: config %s, core's resting axes {%s}" % ([a["neutral"] for a in cfg["input"]["axes"]], neutral))

    # the GPU: Vita3K draws with OpenGL only, and a frontend hands the GPU over
    # when the renderer setting's value ends in "-hw" (Chimera's convention)
    renderer = next((s for s in cfg["settings"] if s["name"] == "renderer"), None)
    if not renderer or not str(renderer.get("default", "")).endswith("-hw"):
        bad.append("renderer: no setting whose default ends in -hw, so no frontend hands this core a GPU")

    # firmware: the ids the core opens
    opened = re.findall(r'"(PS\w+UPDAT\.PUP)"', entry)
    declared = [f["id"] for f in cfg.get("firmware", [])]
    if sorted(set(opened)) != sorted(declared):
        bad.append("firmware: config %s, core opens %s" % (declared, sorted(set(opened))))

    # settings: every one read by the core, or (system_software) by the engine's firmware conditions
    used_by_engine = {c["requiredWhen"]["setting"] for c in cfg.get("firmware", [])}
    for s in cfg["settings"]:
        if s["name"] not in used_by_engine and '"%s"' % s["name"] not in entry:
            bad.append("setting %s: declared, read by nothing" % s["name"])
    langs = re.search(r"LANGUAGES\[\] = \{(.*?)\};", entry, re.S).group(1)
    core_langs = re.findall(r'"([a-z-]+)"', langs)
    decl_langs = next(s for s in cfg["settings"] if s["name"] == "language")["options"]
    if core_langs != decl_langs:
        bad.append("language options: config %s, core %s" % (decl_langs, core_langs))

    # keybinds: the declared controller, nothing it does not have
    name = cfg["input"]["name"]
    controls = set(cfg["input"]["buttons"]) | {a["name"] for a in cfg["input"]["axes"]}
    for table in ("AllTrollers", "AllTrollersAnalog"):
        if set(binds.get(table, {})) != {name}:
            bad.append("%s: controllers %s, declared %s" % (table, list(binds.get(table, {})), name))
        extra = set(binds.get(table, {}).get(name, {})) - controls
        if extra:
            bad.append("%s binds controls the config does not declare: %s" % (table, sorted(extra)))

    # slots: the ids the core reads
    for slot in slots["slots"]:
        if 'slot_first(slots, "%s")' % slot["id"] not in entry:
            bad.append("slot %s: declared, read by nothing" % slot["id"])

    for b in bad:
        print(b)
    print("declaration: %d buttons, %d axes, %d settings, %d firmware, %d slots; %s"
          % (len(cfg["input"]["buttons"]), len(cfg["input"]["axes"]), len(cfg["settings"]), len(declared),
             len(slots["slots"]), "%d mismatches" % len(bad) if bad else "all read by the core"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
