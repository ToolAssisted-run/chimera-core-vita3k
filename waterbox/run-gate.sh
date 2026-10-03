#!/bin/sh
# The gate: every test app twice through the native reference and once in
# the sandbox, frame by frame; states in the sandbox, against the native run;
# and the negative controls that prove the comparisons can fail.
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

# <vpk> <frames> <what it shows>: two runs, the same in every frame
twice() {
	vpk="$1"; frames="$2"; what="$3"
	n="${vpk%.vpk}"
	ra=$(native "$n-a" "$vpk" "$frames")
	rb=$(native "$n-b" "$vpk" "$frames")
	if [ "$ra" != 0 ] || [ "$rb" != 0 ]; then
		fail "$n: a run failed (exit $ra, $rb; build/gate/$n-a.err)"
	elif ! cmp -s "$work/$n-a.out" "$work/$n-b.out"; then
		fail "$n: two runs differ - $(diff "$work/$n-a.out" "$work/$n-b.out" | sed -n 2p)"
	else
		pass "$n: two runs the same in every frame - $(pictures "$n-a") different pictures, $what; $(tail -1 "$work/$n-a.out" | grep -o 'exited=[0-9]* time_ns=[0-9]* switches=[0-9]*')"
	fi
	rw=$(sandboxed "$n-wbx" "$vpk" "$frames")
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
state="$work/threadtest-150.state"
ra=$(sandboxed threadtest-save threadtest.vpk 150 --save-state "$state")
rb=$(sandboxed threadtest-load threadtest.vpk 150 --state "$state")
{ grep '^frame' "$work/threadtest-save.out"; grep -v '^loaded' "$work/threadtest-load.out"; } >"$work/threadtest-joined.out"
loaded=$(sed -n 's/^loaded //p' "$work/threadtest-load.out")
if [ "$ra" != 0 ] || [ "$rb" != 0 ]; then
	fail "threadtest saved at frame 150 and carried on in another process failed (exit $ra, $rb)"
elif [ "$loaded" != "$(grep '^frame 150 ' "$work/threadtest-a.out")" ] || ! cmp -s "$work/threadtest-a.out" "$work/threadtest-joined.out"; then
	fail "threadtest carried on from a state in another process differs from one run - $(diff "$work/threadtest-a.out" "$work/threadtest-joined.out" | sed -n 2p)"
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
r=$(sandboxed threadtest-nosync threadtest.vpk 100 --rerecord --no-surface-sync)
grep '^frame' "$work/threadtest-nosync.out" >"$work/threadtest-nosync.got"
if [ "$r" = 0 ] && ! cmp -s "$work/threadtest-rerecord.want" "$work/threadtest-nosync.got"; then
	pass "a state holds the pictures only through surface sync - without it, $(diff "$work/threadtest-rerecord.want" "$work/threadtest-nosync.got" | grep -c '^>') of 10 pictures differ under rerecord"
else
	fail "threadtest without surface sync survives a load before every frame (exit $r): the state legs cannot see a lost picture"
fi

exit "$fails"
