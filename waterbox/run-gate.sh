#!/bin/sh
# The gate: every test app twice through the native reference and once in
# the sandbox, frame by frame; states in the sandbox, against the native run;
# the frame's input, sound and save data against oracles that predict them
# independently; and the negative controls that prove the comparisons can
# fail.
#
#   run-gate.sh [-n]    -n: do not build (use build/native and build/testapps)
#
# A leg prints PASS or FAIL and one line of evidence; the exit status is the
# number of failed legs.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$here")"
run="$root/build/native/bin/vita3k-run-native"
wbx="$root/build/wbx/run-wbx"
core="$root/build/wbx/core.wbx"
apps="$root/build/testapps"
work="$root/build/gate"

if [ "${1:-}" != "-n" ]; then
	sh "$here/build-testapps.sh" >/dev/null || { echo "FAIL the test apps did not build"; exit 1; }
	sh "$here/build-native.sh" >"$root/build/native-build.log" 2>&1 || { echo "FAIL the native reference did not build (build/native-build.log)"; exit 1; }
	sh "$here/build-guest.sh" >"$root/build/guest-build.log" 2>&1 || { echo "FAIL the guest did not build (build/guest-build.log)"; exit 1; }
	sh "$here/build-core.sh" >"$root/build/core-link.log" 2>&1 || { echo "FAIL core.wbx did not link (build/core-link.log)"; exit 1; }
fi
mkdir -p "$work"
unset DISPLAY

fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails + 1)); }

# <name> <vpk> <frames> [run-native options...]: the run's output in $work/<name>.out
native() {
	name="$1"; vpk="$2"; frames="$3"; shift 3
	"$run" "$apps/$vpk" --work "$work/$name-work" --frames "$frames" --timeout 600 --digest-every 10 "$@" \
		>"$work/$name.out" 2>"$work/$name.err"
	echo $?
}

# the same, in the sandbox
sandboxed() {
	name="$1"; vpk="$2"; frames="$3"; shift 3
	"$wbx" "$core" "$apps/$vpk" --work "$work/$name-work" --frames "$frames" --timeout 900 --digest-every 10 "$@" \
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
if [ "$rw" = 0 ] && [ "$rn" = 0 ] && [ "$(head -1 "$log")" = "loaded 1" ] && grep -qx "saved 2" "$log" && [ "$count" = 2 ] \
	&& diff -r "$work/inputtest-reload-sd" "$work/inputtest-reload-n-sd" >/dev/null; then
	pass "save data round trip: the export, zipped, starts a run that loads count 1 and saves 2 (native == sandbox)"
else
	fail "save data did not come back in (exit $rw, $rn; first line '$(head -1 "$log" 2>/dev/null)', count '$count')"
fi
r=$(sandboxed inputtest-refuse inputtest.vpk 5 --savedata-in "$work/not-a-save.zip")
if [ "$r" = 6 ] && grep -q "not a Vita save" "$work/inputtest-refuse.err"; then
	pass "a zip that is not a Vita save is refused: $(grep -o 'holds readme.txt, which is not a Vita save' "$work/inputtest-refuse.err")"
else
	fail "a zip that is not a Vita save was taken (exit $r)"
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
