#!/bin/sh
# The gate: every test app twice through the native reference and once in
# the sandbox, frame by frame; states in the sandbox, against the native run;
# the frame's input, sound and save data against oracles that predict them
# independently; and the negative controls that prove the comparisons can
# fail.
#
#   run-gate.sh [-n]    -n: do not build (use build/native and build/testapps)
#
# A leg prints PASS, FAIL or SKIP and one line of evidence; the exit status
# is the number of failed legs. Games and firmware are nobody's to put in a
# repository: the legs that need them look in build/content (or
# VITA3K_CONTENT) and SKIP, saying what they lacked, when it is not there.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
run="$root/build/native/bin/vita3k-run-native"
wbx="$root/build/wbx/run-wbx"
core="$root/build/wbx/core.wbx"
apps="$root/build/testapps"
work="$root/build/gate"
chimera_root="${CHIMERA_ROOT:-$HOME/chimera}"
engine="$chimera_root/build/meson-linux/chimera-run"
pkg="$root/build/package/vita3k.chimeraCore"
content="${VITA3K_CONTENT:-$root/build/content}"

# <what> <log> <command...>: a build step; on failure its log's end is said
step() {
	what="$1"; log="$2"; shift 2
	mkdir -p "$root/build"
	"$@" >"$log" 2>&1 && return 0
	echo "FAIL $what (${log#$root/})"
	tail -40 "$log" | sed 's/^/  | /'
	exit 1
}
if [ "${1:-}" != "-n" ]; then
	step "the test apps did not build" "$root/build/testapps-build.log" sh "$here/build-testapps.sh"
	step "the native reference did not build" "$root/build/native-build.log" sh "$here/build-native.sh"
	step "the guest did not build" "$root/build/guest-build.log" sh "$here/build-guest.sh"
	step "core.wbx did not link" "$root/build/core-link.log" sh "$here/build-core.sh"
	step "the package did not build" "$root/build/package.log" sh "$here/build-package.sh" -n -r "$chimera_root"
	echo "PASS the package builds: $(grep -o 'package sha1 [0-9a-f]*' "$root/build/package.log")"
fi
mkdir -p "$work"
unset DISPLAY

fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }
skip() { echo "SKIP $*"; }

# <name> <vpk> <frames> [run-native options...]: the run's output in
# $work/<name>.out; a vpk is a test app's name, or a path from /
native() {
	name="$1"; vpk="$2"; frames="$3"; shift 3
	case "$vpk" in /*) ;; *) vpk="$apps/$vpk" ;; esac
	"$run" "$vpk" --work "$work/$name-work" --frames "$frames" --timeout 900 --digest-every 10 "$@" \
		>"$work/$name.out" 2>"$work/$name.err"
	echo $?
}

# the same, in the sandbox
sandboxed() {
	name="$1"; vpk="$2"; frames="$3"; shift 3
	case "$vpk" in /*) ;; *) vpk="$apps/$vpk" ;; esac
	"$wbx" "$core" "$vpk" --work "$work/$name-work" --frames "$frames" --timeout 900 --digest-every 10 "$@" \
		>"$work/$name.out" 2>"$work/$name.err"
	echo $?
}

pictures() { grep -o 'video=[0-9a-f]*' "$work/$1.out" | sort -u | wc -l; }

# <vpk> <frames> <what it shows> [options...]: two runs, the same in every frame
twice() {
	vpk="$1"; frames="$2"; what="$3"
	shift 3
	n="${vpk%.vpk}"
	ra=$(native "$n-a" "$vpk" "$frames" "$@")
	rb=$(native "$n-b" "$vpk" "$frames" "$@")
	if [ "$ra" != 0 ] || [ "$rb" != 0 ]; then
		fail "$n: a run failed (exit $ra, $rb; build/gate/$n-a.err)"
	elif ! cmp -s "$work/$n-a.out" "$work/$n-b.out"; then
		fail "$n: two runs differ - $(diff "$work/$n-a.out" "$work/$n-b.out" | sed -n 2p)"
	else
		pass "$n: two runs the same in every frame - $(pictures "$n-a") different pictures, $what; $(tail -1 "$work/$n-a.out" | grep -o 'exited=[0-9]* time_ns=[0-9]* switches=[0-9]*')"
	fi
	rw=$(sandboxed "$n-wbx" "$vpk" "$frames" "$@")
	if [ "$rw" != 0 ]; then
		fail "$n: the sandboxed run failed (exit $rw; build/gate/$n-wbx.err)"
	elif ! cmp -s "$work/$n-a.out" "$work/$n-wbx.out"; then
		fail "$n: native and sandbox differ - $(diff "$work/$n-a.out" "$work/$n-wbx.out" | sed -n 2p)"
	else
		pass "$n: native == sandbox in every frame"
	fi
}

twice hello_world.vpk 300 "its text, then it exits"
twice debugscreen.vpk 300 "the debug screen showcase"
twice ctrl_sample.vpk 300 "the pad readout"
twice touch_sample.vpk 300 "the touch readout"
twice rtc_sample.vpk 300 "the clock running in machine time"
twice audio_sample.vpk 300 "the tone, paced in machine time"
twice sdl2_redrectangle.vpk 300 "SDL2's GXM renderer, then it exits"
twice threadtest.vpk 300 "three threads' interleaving drawn through GXM"
script="$work/inputtest-script.txt"
python3 "$here/tests/input-oracle.py" gen "$script" 300
twice inputtest.vpk 300 "every control read once a frame (a black screen)" --input "$script"
twice audiotest.vpk 300 "two ports, two tones (a black screen)"

# The frame's input: InputTest writes down everything it reads of the pad
# (both kinds of read), both touch panels and the motion sensors, once a
# frame, into ux0:data; it comes out through the save data export, and the
# oracle predicts every line from the script alone.
oracle="$here/tests/input-oracle.py"
rw=$(sandboxed inputtest-oracle inputtest.vpk 300 --input "$script" --savedata-out "$work/inputtest-oracle-sd")
rn=$(native inputtest-oracle-n inputtest.vpk 300 --input "$script" --savedata-out "$work/inputtest-oracle-n-sd")
verdict=$(python3 "$oracle" check "$script" "$work/inputtest-oracle-sd/data/inputtest/input.txt")
vr=$?
if [ "$rw" != 0 ] || [ "$rn" != 0 ]; then
	fail "inputtest with an export failed (exit $rw, $rn)"
elif [ "$vr" != 0 ]; then
	fail "inputtest did not read the input its script gives - $verdict"
elif ! diff -r "$work/inputtest-oracle-sd" "$work/inputtest-oracle-n-sd" >/dev/null; then
	fail "inputtest's save data export differs between native and sandbox"
else
	pass "inputtest read exactly the input predicted from its script: $verdict; the export is the same natively"
fi
# every digest line says whether the frame read input
if grep -q 'read=0' "$work/inputtest-a.out" || grep -q 'read=1' "$work/audiotest-a.out"; then
	fail "InputWasRead is wrong: inputtest reads every frame, audiotest none"
else
	pass "InputWasRead: inputtest read input in all $(grep -c 'read=1' "$work/inputtest-a.out") digest frames, audiotest in none"
fi

# The sound: AudioTest's two tones, measured.
r=$(sandboxed audiotest-sound audiotest.vpk 130 --audio-out "$work/audiotest.raw")
verdict=$(python3 "$here/tests/audio-oracle.py" "$work/audiotest.raw" 130)
vr=$?
if [ "$r" = 0 ] && [ "$vr" = 0 ]; then
	pass "audiotest's sound is the two tones it played, 800 pairs a frame: $verdict"
else
	fail "audiotest's sound is not what it played (exit $r): $verdict"
fi

# Save data back in: the export of the run above, zipped, starts another run,
# which finds its count and saves one more; a zip holding anything else is
# refused.
python3 -c "
import os, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, 'w') as z:
    for d, _, files in sorted(os.walk(root)):
        for f in sorted(files):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, root))
with zipfile.ZipFile(sys.argv[3], 'w') as z:
    z.writestr('readme.txt', 'not a save')
" "$work/inputtest-oracle-sd" "$work/inputtest-save.zip" "$work/not-a-save.zip"
rw=$(sandboxed inputtest-reload inputtest.vpk 40 --savedata-in "$work/inputtest-save.zip" --savedata-out "$work/inputtest-reload-sd")
rn=$(native inputtest-reload-n inputtest.vpk 40 --savedata-in "$work/inputtest-save.zip" --savedata-out "$work/inputtest-reload-n-sd")
log="$work/inputtest-reload-sd/data/inputtest/input.txt"
count=$(od -An -td4 "$work/inputtest-reload-sd/savedata/CHMR00002/count.bin" 2>/dev/null | tr -d ' ')
if [ "$rw" = 0 ] && [ "$rn" = 0 ] && [ "$(grep -m1 '^loaded' "$log")" = "loaded 1" ] && grep -qx "saved 2" "$log" && [ "$count" = 2 ] \
	&& diff -r "$work/inputtest-reload-sd" "$work/inputtest-reload-n-sd" >/dev/null; then
	pass "save data round trip: the export, zipped, starts a run that loads count 1 and saves 2 (native == sandbox)"
else
	fail "save data did not come back in (exit $rw, $rn; '$(grep -m1 '^loaded' "$log" 2>/dev/null)', count '$count')"
fi
r=$(sandboxed inputtest-refuse inputtest.vpk 5 --savedata-in "$work/not-a-save.zip")
if [ "$r" = 6 ] && grep -q "not a Vita save" "$work/inputtest-refuse.err"; then
	pass "a zip that is not a Vita save is refused: $(grep -o 'holds readme.txt, which is not a Vita save' "$work/inputtest-refuse.err")"
else
	fail "a zip that is not a Vita save was taken (exit $r)"
fi

# What a system call costs (2026-10-04): every HLE call is 1332 instructions,
# one microsecond, so a game polling one in a loop does not run it millions
# of times a second. HleTest times 10000 calls with the machine's own clock;
# the same run with calls free is the control, and the two differ by exactly
# the 10000 calls and the closing clock read, a microsecond each. Its second
# half: a thread sleeping 100 us while the main thread spins wakes on time
# (a fresh slice stops at the next deadline), and a thread running alone is
# not stopped after every block (the yield count).
hle_result() { tr '\n' ' ' <"$work/$1-sd/data/hletest/result.txt" 2>/dev/null; }
hle_field() { hle_result "$1" | tr ' ' '\n' | grep "^$2=" | cut -d= -f2; }
rw=$(sandboxed hletest hletest.vpk 120 --savedata-out "$work/hletest-sd")
rn=$(native hletest-n hletest.vpk 120 --savedata-out "$work/hletest-n-sd")
rf=$(sandboxed hletest-free hletest.vpk 120 --savedata-out "$work/hletest-free-sd" --free-hle-calls)
us=$(hle_field hletest us); free_us=$(hle_field hletest-free us); late=$(hle_field hletest max_late_us)
yields=$(grep -o 'yields=[0-9]*' "$work/hletest.err" | cut -d= -f2)
if [ "$rw" != 0 ] || [ "$rn" != 0 ] || [ "$rf" != 0 ] || [ -z "$us" ] || [ -z "$free_us" ]; then
	fail "hletest did not run (exit $rw, $rn, $rf; '$(hle_result hletest)')"
elif [ $((us - free_us)) != 10001 ]; then
	fail "an HLE call does not cost 1 us: 10000 calls took $us us, $free_us us free"
else
	pass "an HLE call costs the machine 1 us: 10000 calls took $us us, $free_us us when calls are free"
fi
if ! cmp -s "$work/hletest.out" "$work/hletest-n.out" || ! diff -r "$work/hletest-sd" "$work/hletest-n-sd" >/dev/null; then
	fail "hletest: native and sandbox differ"
elif [ -z "$late" ] || [ "$late" -gt 3 ]; then
	fail "a sleeping thread woke late while another spun: up to $late us after its 100 us"
elif [ -z "$yields" ] || [ "$yields" -gt 1000 ]; then
	fail "a thread running alone gave way $yields times in 120 frames (stopped after every block?)"
else
	pass "a 100 us sleep while another thread spins ends at most $late us late (its two calls), and the run gave way $yields times; native == sandbox"
fi

# States, on the app that draws through the GPU. A load puts the whole machine
# back, pictures included (surface sync writes them to its memory), and the
# renderer rebuilds every GL object in the context it finds after the load.
# Both legs must print what the native run printed, frame for frame.
grep '^frame' "$work/threadtest-a.out" | head -10 >"$work/threadtest-rerecord.want"
r=$(sandboxed threadtest-rerecord threadtest.vpk 100 --rerecord)
grep '^frame' "$work/threadtest-rerecord.out" >"$work/threadtest-rerecord.got"
if [ "$r" != 0 ]; then
	fail "threadtest with a state saved and loaded before every frame failed (exit $r; build/gate/threadtest-rerecord.err)"
elif ! cmp -s "$work/threadtest-rerecord.want" "$work/threadtest-rerecord.got"; then
	fail "threadtest, a state saved and loaded before every frame, differs from the native run - $(diff "$work/threadtest-rerecord.want" "$work/threadtest-rerecord.got" | sed -n 2p)"
else
	pass "threadtest with a state saved and loaded before every frame == the native run, $(grep -c 'rebuilding every GL object' "$work/threadtest-rerecord-work/vita3k.log") GL rebuilds in 100 frames"
fi
# The package declares what the core reads (controls in order, firmware ids,
# settings, language options, keybinds, slots) - and the check sees a swap.
if python3 "$here/tests/check-declaration.py" "$here" >"$work/declaration.txt"; then
	pass "$(tail -1 "$work/declaration.txt")"
else
	fail "the declaration and the core disagree - $(head -1 "$work/declaration.txt")"
fi
rm -rf "$work/decl-neg" && mkdir -p "$work/decl-neg/driver"
cp "$here/file_slots.json" "$here/default_keybinds.json" "$work/decl-neg/"
cp "$here/driver/vita3k_driver.h" "$here/driver/wbx-entry.cpp" "$work/decl-neg/driver/"
sed 's/"Cross",/"CROSS_",/; s/"Circle",/"Cross",/; s/"CROSS_",/"Circle",/' "$here/waterbox.config" >"$work/decl-neg/waterbox.config"
if ! python3 "$here/tests/check-declaration.py" "$work/decl-neg" >/dev/null; then
	pass "the declaration check sees Cross and Circle swapped"
else
	fail "the declaration check passes Cross and Circle swapped"
fi

# Settings reach the machine: InputTest's first line is the language and
# enter button it was given (the Vita's own values).
r=$(sandboxed inputtest-ja inputtest.vpk 20 --language japanese --enter-button circle --savedata-out "$work/inputtest-ja-sd")
first=$(head -1 "$work/inputtest-ja-sd/data/inputtest/input.txt" 2>/dev/null)
default=$(head -1 "$work/inputtest-oracle-sd/data/inputtest/input.txt" 2>/dev/null)
if [ "$r" = 0 ] && [ "$first" = "system lang=0 enter=0" ] && [ "$default" = "system lang=1 enter=1" ]; then
	pass "settings reach the machine: Japanese and Circle read '$first', the defaults '$default'"
else
	fail "settings did not reach the machine (exit $r): '$first' and '$default'"
fi

# Turbo: rendering off for frames 100..199 leaves the machine exactly as it
# was - time, sound and input in every frame, the pictures outside the window.
r=$(sandboxed threadtest-turbo threadtest.vpk 300 --turbo 100:200)
sed 's/ video=[0-9a-f]*//' "$work/threadtest-a.out" >"$work/threadtest-machine.want"
sed 's/ video=[0-9a-f]*//' "$work/threadtest-turbo.out" >"$work/threadtest-machine.got"
outside=$(awk '/^frame/ && ($2 < 100 || $2 >= 200)' "$work/threadtest-a.out")
outside_turbo=$(awk '/^frame/ && ($2 < 100 || $2 >= 200)' "$work/threadtest-turbo.out")
if [ "$r" = 0 ] && cmp -s "$work/threadtest-machine.want" "$work/threadtest-machine.got" && [ "$outside" = "$outside_turbo" ]; then
	pass "turbo for frames 100..199: the machine the same in every frame, the pictures outside the window; $(diff "$work/threadtest-a.out" "$work/threadtest-turbo.out" | grep -c '^>') pictures in it stood still"
else
	fail "turbo changed the machine (exit $r) - $(diff "$work/threadtest-machine.want" "$work/threadtest-machine.got" | sed -n 2p)"
fi

# The package through Chimera's own engine: InputTest driven by a movie of
# the oracle's script, its record out through the engine's save data export.
if [ -x "$engine" ] && [ -f "$pkg" ]; then
	python3 "$oracle" movie "$script" 300 "$work/engine.movie"
	rm -rf "$work/engine-sd"
	"$engine" "$pkg" "$apps/inputtest.vpk" "$work/engine.movie" --gpu --export-savedata "$work/engine-sd" >"$work/engine.out" 2>&1
	r=$?
	verdict=$(python3 "$oracle" check "$script" "$work/engine-sd/data/inputtest/input.txt" 2>&1 | tail -1)
	if [ "$r" = 0 ] && grep -q '^frames=300' "$work/engine.out" && python3 "$oracle" check "$script" "$work/engine-sd/data/inputtest/input.txt" >/dev/null; then
		pass "the package in chimera-run: 300 frames of a movie, $verdict"
	else
		fail "the package in chimera-run (exit $r): $verdict; see build/gate/engine.out"
	fi
	# Without the GPU - what a frontend does for a core whose renderer is not
	# "-hw", and what crashed Chimera on Windows before the renderer setting
	# existed - the core refuses to start and says why, instead of calling GL
	# through nothing.
	"$engine" "$pkg" "$apps/inputtest.vpk" "$work/engine.movie" >"$work/engine-nogpu.out" 2>&1
	r=$?
	if [ "$r" != 0 ] && grep -q "no GPU was handed over" "$work/engine-nogpu.out"; then
		pass "with no GPU the package refuses to start, and says so (chimera-run exit $r)"
	else
		fail "with no GPU the package did not refuse cleanly (exit $r) - $(tail -2 "$work/engine-nogpu.out" | head -1)"
	fi
else
	skip "the package in chimera-run: no $engine or package"
fi

# Games, where they are: the firmware installed into the machine before the
# game, the game the same native and sandboxed. Alien Shooter is Playable on
# Vita3K's list, and runs with the font package alone.
game="$content/alien-shooter.zip"
fonts="$content/PSP2UPDAT.PUP"
if [ -f "$game" ] && [ -f "$fonts" ]; then
	(native alien-shooter-n "$game" 300 --firmware PSP2UPDAT.PUP="$fonts" >"$work/alien-shooter-n.rc") &
	rw=$(sandboxed alien-shooter-w "$game" 300 --firmware PSP2UPDAT.PUP="$fonts")
	wait
	rn=$(cat "$work/alien-shooter-n.rc")
	pics=$(grep -o 'video=[0-9a-f]*' "$work/alien-shooter-w.out" | sort -u | wc -l)
	if [ "$rn" = 0 ] && [ "$rw" = 0 ] && cmp -s "$work/alien-shooter-n.out" "$work/alien-shooter-w.out" && [ "$pics" -ge 3 ]; then
		pass "Alien Shooter: native == sandbox in all 300 frames, $pics pictures; $(tail -1 "$work/alien-shooter-w.out" | grep -o 'switches=[0-9]*')"
	else
		fail "Alien Shooter: native and sandbox differ, or it drew $pics pictures (exit $rn, $rw) - $(diff "$work/alien-shooter-n.out" "$work/alien-shooter-w.out" | sed -n 2p)"
	fi
	r=$(sandboxed swapped-pup "$game" 2 --firmware PSVUPDAT.PUP="$fonts")
	if [ "$r" = 6 ] && grep -q "is it the font package" "$work/swapped-pup.err"; then
		pass "the font package handed over as the system software is refused: $(grep -o 'installed nothing in vs0[^(]*' "$work/swapped-pup.err")"
	else
		fail "the font package handed over as the system software was taken (exit $r)"
	fi
else
	skip "Alien Shooter: no $game and $fonts"
fi
# The system software as well (System Software "full"): Alien Shooter loads
# the system's own libraries from it, and none is missing.
sysw="$content/PSVUPDAT.PUP"
game="$content/alien-shooter.zip"
if [ -f "$game" ] && [ -f "$fonts" ] && [ -f "$sysw" ]; then
	(native alien-shooter-full-n "$game" 300 --firmware PSVUPDAT.PUP="$sysw" --firmware PSP2UPDAT.PUP="$fonts" --digest-every 30 >"$work/alien-shooter-full-n.rc") &
	rw=$(sandboxed alien-shooter-full-w "$game" 300 --firmware PSVUPDAT.PUP="$sysw" --firmware PSP2UPDAT.PUP="$fonts" --digest-every 30)
	wait
	rn=$(cat "$work/alien-shooter-full-n.rc")
	log="$work/alien-shooter-full-w-work/vita3k.log"
	missing=$(grep -ac 'target path: \(vs0\|os0\):' "$log" 2>/dev/null)
	version=$(grep -ao 'installed PSVUPDAT.PUP: system software [0-9.]*' "$log" | grep -o '[0-9.]*$')
	if [ "$rn" = 0 ] && [ "$rw" = 0 ] && cmp -s "$work/alien-shooter-full-n.out" "$work/alien-shooter-full-w.out" && [ -n "$version" ] && [ "$missing" = 0 ]; then
		pass "Alien Shooter on system software $version: native == sandbox in all 300 frames, no system module missing"
	else
		fail "Alien Shooter on the system software (exit $rn, $rw; version '$version', $missing missing system files)"
	fi
else
	skip "Alien Shooter on the system software: no $sysw"
fi

# Alien Breed (PCSE00210; Ingame on Vita3K's list) asks for the network at
# boot and threads heavily (about 1500 switches a frame): the machine has no
# network, and the game carries on without one.
game="$content/alien-breed.zip"
if [ -f "$game" ] && [ -f "$fonts" ]; then
	(native alien-breed-n "$game" 600 --firmware PSP2UPDAT.PUP="$fonts" --digest-every 30 >"$work/alien-breed-n.rc") &
	rw=$(sandboxed alien-breed-w "$game" 600 --firmware PSP2UPDAT.PUP="$fonts" --digest-every 30)
	wait
	rn=$(cat "$work/alien-breed-n.rc")
	pics=$(grep -o 'video=[0-9a-f]*' "$work/alien-breed-w.out" | sort -u | wc -l)
	if [ "$rn" = 0 ] && [ "$rw" = 0 ] && cmp -s "$work/alien-breed-n.out" "$work/alien-breed-w.out" && [ "$pics" -ge 3 ]; then
		pass "Alien Breed: native == sandbox in all 600 frames, $pics pictures; $(tail -1 "$work/alien-breed-w.out" | grep -o 'switches=[0-9]*')"
	else
		fail "Alien Breed: native and sandbox differ, or it drew $pics pictures (exit $rn, $rw) - $(diff "$work/alien-breed-n.out" "$work/alien-breed-w.out" | sed -n 2p)"
	fi
else
	skip "Alien Breed: no $game and $fonts"
fi

# input and sound through states: rerecord legs on the two M4 apps
r=$(sandboxed inputtest-rerecord inputtest.vpk 60 --rerecord --input "$script" --savedata-out "$work/inputtest-rerecord-sd")
verdict=$(python3 "$oracle" check "$script" "$work/inputtest-rerecord-sd/data/inputtest/input.txt")
vr=$?
grep '^frame' "$work/inputtest-a.out" | head -6 >"$work/inputtest-rerecord.want"
grep '^frame' "$work/inputtest-rerecord.out" >"$work/inputtest-rerecord.got"
if [ "$r" = 0 ] && [ "$vr" = 0 ] && cmp -s "$work/inputtest-rerecord.want" "$work/inputtest-rerecord.got"; then
	pass "inputtest with a state saved and loaded before every frame reads the predicted input ($verdict) and matches the native run"
else
	fail "inputtest under rerecord differs (exit $r): $verdict"
fi
r=$(sandboxed audiotest-rerecord audiotest.vpk 60 --rerecord)
grep '^frame' "$work/audiotest-a.out" | head -6 >"$work/audiotest-rerecord.want"
grep '^frame' "$work/audiotest-rerecord.out" >"$work/audiotest-rerecord.got"
if [ "$r" = 0 ] && cmp -s "$work/audiotest-rerecord.want" "$work/audiotest-rerecord.got"; then
	pass "audiotest with a state saved and loaded before every frame sounds as the native run did, frame by frame"
else
	fail "audiotest under rerecord sounds different (exit $r) - $(diff "$work/audiotest-rerecord.want" "$work/audiotest-rerecord.got" | sed -n 2p)"
fi

state="$work/threadtest-150.state"
ra=$(sandboxed threadtest-save threadtest.vpk 150 --save-state "$state")
rb=$(sandboxed threadtest-load threadtest.vpk 150 --state "$state")
# the last line's audio= is the sound of the whole run, which the second
# process heard only half of: it goes (each frame's own sound stays)
whole='/^app=/s/ audio=[0-9a-f]*//'
{ grep '^frame' "$work/threadtest-save.out"; grep -v '^loaded' "$work/threadtest-load.out"; } | sed "$whole" >"$work/threadtest-joined.out"
sed "$whole" "$work/threadtest-a.out" >"$work/threadtest-one.out"
loaded=$(sed -n 's/^loaded //p' "$work/threadtest-load.out")
if [ "$ra" != 0 ] || [ "$rb" != 0 ]; then
	fail "threadtest saved at frame 150 and carried on in another process failed (exit $ra, $rb)"
elif [ "$loaded" != "$(grep '^frame 150 ' "$work/threadtest-a.out" | sed 's/ audio=.*//')" ] || ! cmp -s "$work/threadtest-one.out" "$work/threadtest-joined.out"; then
	fail "threadtest carried on from a state in another process differs from one run - $(diff "$work/threadtest-one.out" "$work/threadtest-joined.out" | sed -n 2p)"
else
	pass "threadtest saved at frame 150 and carried on in another process == one native run; the state is $(($(wc -c <"$state") >> 20)) MiB"
fi
rm -f "$state"

# Negative controls: what the machine is made of must show, or the legs above
# compare nothing.
r=$(native threadtest-1331 threadtest.vpk 300 --cpu-mhz 1331)
if [ "$r" = 0 ] && ! cmp -s "$work/threadtest-a.out" "$work/threadtest-1331.out"; then
	pass "the CPU clock is what buys time - 1 MHz less and $(diff "$work/threadtest-a.out" "$work/threadtest-1331.out" | grep -c '^>') of 31 lines differ"
else
	fail "threadtest at 1331 MHz draws what 1332 MHz did (exit $r): the gate cannot see timing"
fi
r=$(native rtc-1400000000 rtc_sample.vpk 300 --rtc-start 1400000000)
if [ "$r" = 0 ] && ! cmp -s "$work/rtc_sample-a.out" "$work/rtc-1400000000.out"; then
	pass "the calendar is the machine's - another start date and every picture differs"
else
	fail "rtc_sample draws the same date from another start (exit $r)"
fi
python3 "$oracle" gen "$work/inputtest-late.txt" 300 1
r=$(native inputtest-late inputtest.vpk 300 --input "$work/inputtest-late.txt" --savedata-out "$work/inputtest-late-sd")
verdict=$(python3 "$oracle" check "$script" "$work/inputtest-late-sd/data/inputtest/input.txt")
vr=$?
if [ "$r" = 0 ] && [ "$vr" != 0 ]; then
	pass "the input oracle sees a frame's lag - the script one frame late, and $(echo "$verdict" | tail -1 | grep -o '[0-9]* differ') from the prediction"
else
	fail "the input oracle passes a run fed its script one frame late (exit $r): it cannot see when input arrives"
fi
if ! python3 "$here/tests/audio-oracle.py" "$work/audiotest.raw" 130 --drop-one >/dev/null; then
	pass "the audio oracle sees one lost sample - $(python3 "$here/tests/audio-oracle.py" "$work/audiotest.raw" 130 --drop-one | grep -o 'L1000=[0-9.]*') with one pair taken out"
else
	fail "the audio oracle passes the sound with a sample taken out: it cannot see a glitch"
fi
r=$(sandboxed threadtest-nosync threadtest.vpk 100 --rerecord --no-surface-sync)
grep '^frame' "$work/threadtest-nosync.out" >"$work/threadtest-nosync.got"
if [ "$r" = 0 ] && ! cmp -s "$work/threadtest-rerecord.want" "$work/threadtest-nosync.got"; then
	pass "a state holds the pictures only through surface sync - without it, $(diff "$work/threadtest-rerecord.want" "$work/threadtest-nosync.got" | grep -c '^>') of 10 pictures differ under rerecord"
else
	fail "threadtest without surface sync survives a load before every frame (exit $r): the state legs cannot see a lost picture"
fi

exit "$fails"
