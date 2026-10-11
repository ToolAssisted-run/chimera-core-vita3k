#!/bin/sh
# Generates both halves of the GPU bridge (build/generated-gl) from miniBox's
# master list, for every name glad 2's header knows. Vita3K's renderer loads
# its entry points through FrameHost::get_proc_address, which is the bridge's
# lookup, so the install step's assignments to glad's own pointers are taken
# out: they would tie the wrappers to one glad's globals, and Vita3K's glad
# (1) is not the one the host half uses (2).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
mb="${MINIBOX_DIR:-$HOME/chimera/extern/chimera-common-minibox}"
out="$root/build/generated-gl"
mkdir -p "$out"
python3 "$mb/source/gl/gen-gl-bridge.py" "$here/glad/include/glad/gl.h" "$mb/source/gl/gl-entry-points.txt" "$out" >/dev/null
sed -i '/^\tglad_gl[A-Za-z0-9_]* = w_gl[A-Za-z0-9_]*;$/d' "$out/gl-bridge-guest.cpp"
# The Vulkan bridge, both halves, from the registry miniBox carries: the
# guest's wrappers (the vk* functions the renderer calls) and the host's
# dispatch. Generated into a new folder and moved, so a failure leaves the
# old one.
vk="$root/build/generated-vk"
rm -rf "$vk.new"
python3 "$mb/source/vk/gen-vk-bridge.py" "$mb/extern/vulkan-headers/registry/vk.xml" "$mb/source/vk/vk-entry-points.txt" "$vk.new" >/dev/null
mkdir -p "$vk"
for f in vk-bridge-ops.h vk-bridge-guest.c vk-bridge-host.inc; do
	cmp -s "$vk.new/$f" "$vk/$f" 2>/dev/null || cp "$vk.new/$f" "$vk/$f"
done
rm -rf "$vk.new"
python3 "$here/gen-assets.py" "$root/extern/vita3k" "$root/build/generated-assets.cpp" >/dev/null
