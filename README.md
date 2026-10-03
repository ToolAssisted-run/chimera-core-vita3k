# chimera-core-vita3k

[Vita3K](https://github.com/Vita3K/Vita3K)'s PS Vita emulator as a
[Chimera](https://github.com/ToolAssisted-run/chimera) core (work in
progress, chimera#154).

Layout: `extern/vita3k` (upstream, pinned; clone with `--recursive`),
`patches/` (the series applied to it), `waterbox/` (the driver and the
builds) and `docs/PLAN.md` (the design and the reasoning behind it).

- `waterbox/build-native.sh` builds the native reference,
  `build/native/bin/vita3k-run-native`.
- `waterbox/build-testapps.sh` builds the test apps (vitasdk's CC0 samples
  and our own, `tests/apps`) into `build/testapps/`.
- `waterbox/run-gate.sh` runs every test app twice natively and once in the
  sandbox and compares them frame by frame: the machine keeps its own clock,
  so a run never depends on the host. States, input, sound and save data are
  checked too, against oracles that predict them. Games and firmware go in
  `build/content` (or `VITA3K_CONTENT`); without them those legs say SKIP.
- `waterbox/build-package.sh` builds `vita3k.chimeraCore` and installs it
  into a Chimera checkout's `build/Cores`.
