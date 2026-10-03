# chimera-core-vita3k

[Vita3K](https://github.com/Vita3K/Vita3K)'s PS Vita emulator as a
[Chimera](https://github.com/ToolAssisted-run/chimera) core (work in
progress, chimera#154).

Layout: `extern/vita3k` (upstream, pinned; clone with `--recursive`),
`patches/` (the series applied to it), `waterbox/` (the driver and the
builds) and `docs/PLAN.md` (the design and the reasoning behind it).

- `waterbox/build-native.sh` builds the native reference,
  `build/native/bin/vita3k-run-native`.
- `waterbox/build-testapps.sh` builds the test apps (vitasdk's CC0 samples)
  into `build/testapps/`.
