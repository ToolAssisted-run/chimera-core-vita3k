/* run-wbx: the core in miniBox, for the gate. The same harness as run-native
 * (gate-harness.h), the same lines; the app is mounted under its basename
 * with rom.name naming it and the settings in "settings", the shape a
 * project mounts, and the GPU bridge's host half answers in this process.
 *
 * usage: run-wbx <core.wbx> <app.vpk> --work <dir> [options: see gate-harness.h]
 * SPDX-License-Identifier: MIT */
#include "minibox.h"
#include "gate-harness.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>

int chimera_gl_host_init(char *err, int errlen);
const char *chimera_gl_host_description(void);
uintptr_t chimera_gl_host_dispatch(uintptr_t op, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e);
unsigned long chimera_gl_host_unhandled(long *last_op);

typedef struct { FILE *f; } freader;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s) { return (intptr_t)fread(d, 1, s, ((freader *)ud)->f); }
typedef struct { const uint8_t *p; size_t n, pos; } memreader;
static intptr_t mem_reader(uintptr_t ud, uint8_t *d, uintptr_t s)
{
	memreader *m = (memreader *)ud;
	size_t left = m->n - m->pos;
	if (s > left) s = left;
	memcpy(d, m->p + m->pos, s);
	m->pos += s;
	return (intptr_t)s;
}

static mb_host *g_host;

static uintptr_t proc(const char *n)
{
	mb_return r;
	wbx_get_proc_addr(g_host, n, &r);
	if (r.error_message[0] || !r.data) { fprintf(stderr, "missing export %s %s\n", n, r.error_message); exit(2); }
	return r.data;
}

int main(int argc, char **argv)
{
	if (getenv("MB_ALLOW_PTRACE")) prctl(PR_SET_PTRACER, -1L, 0, 0, 0);
	if (argc < 2) { fprintf(stderr, "usage: run-wbx <core.wbx> <app.vpk> --work <dir> [options]\n"); return 2; }
	const char *core = argv[1];
	struct harness_opts o;
	if (!harness_parse(argc - 1, argv + 1, &o)) return 2;
	if (!harness_fresh_work(o.work)) return 2;

	FILE *wf = fopen(core, "rb");
	if (!wf) { fprintf(stderr, "cannot open %s\n", core); return 1; }
	/* The Vita's 4 GiB address space is one reservation, each guest thread's
	 * JIT has its own code cache, and the GPU's shadows and the machine's
	 * filesystem want a heap; Linux backs the block lazily. */
	mb_memory_layout_template layout = { 1024u << 20, 16u << 20, 64u << 20, 256u << 20, (uintptr_t)16 << 30 };
	freader fr = { wf };
	mb_return r;
	wbx_create_host(&layout, "core.wbx", file_read, (uintptr_t)&fr, &r);
	fclose(wf);
	if (r.error_message[0]) { fprintf(stderr, "create: %s\n", r.error_message); return 1; }
	g_host = (mb_host *)r.data;

	/* the app under its basename, and rom.name naming it: the frontend's shape */
	const char *base = strrchr(o.app, '/');
	base = base ? base + 1 : o.app;
	static char vfsname[512];
	snprintf(vfsname, sizeof vfsname, "/%s", base);
	wbx_mount_file_path(g_host, vfsname, o.app, &r);
	if (r.error_message[0]) { fprintf(stderr, "mount %s: %s\n", vfsname, r.error_message); return 1; }
	memreader nr = { (const uint8_t *)vfsname, strlen(vfsname), 0 };
	wbx_mount_file(g_host, "rom.name", mem_reader, (uintptr_t)&nr, false, &r);
	if (r.error_message[0]) { fprintf(stderr, "mount rom.name: %s\n", r.error_message); return 1; }
	static char settings[256];
	harness_settings(&o, settings, sizeof settings);
	memreader sr = { (const uint8_t *)settings, strlen(settings), 0 };
	wbx_mount_file(g_host, "settings", mem_reader, (uintptr_t)&sr, false, &r);
	if (r.error_message[0]) { fprintf(stderr, "mount settings: %s\n", r.error_message); return 1; }
	wbx_activate_host(g_host, &r);

	/* the GPU bridge, handed over before Init */
	char glerr[256] = "";
	if (chimera_gl_host_init(glerr, sizeof glerr) != 0) { fprintf(stderr, "gpu bridge: %s\n", glerr); return 5; }
	{
		void (*set_bridge)(uint64_t) = (void (*)(uint64_t))proc("SetGpuBridge");
		mb_return gr;
		wbx_get_callback_addr(g_host, (mb_external_callback)chimera_gl_host_dispatch, 0, &gr);
		if (!gr.data) { fprintf(stderr, "gpu bridge: could not register the callback\n"); return 5; }
		set_bridge((uint64_t)gr.data);
	}

	/* on Linux the guest's calling convention is the host's: the exports are
	 * called as plain functions */
	const struct harness_core c = {
		(int (*)(void))proc("Init"),
		(const char *(*)(void))proc("GetLoadError"),
		(void (*)(uint64_t))proc("FrameAdvance"),
		(uint32_t *(*)(void))proc("GetVideoBgra"),
		(int (*)(void))proc("GetVideoWidth"),
		(int (*)(void))proc("GetVideoHeight"),
		(uint64_t (*)(void))proc("GetFrameCount"),
		(uint64_t (*)(void))proc("GetExitedAt"),
		(uint64_t (*)(void))proc("GetMachineTimeNs"),
		(uint64_t (*)(void))proc("GetSwitchCount"),
		(int64_t (*)(void))proc("GetLogSize"),
		(const uint8_t *(*)(void))proc("GetLogBuffer"),
	};
	const int rc = harness_run(&c, &o, base);
	if (getenv("CHIMERA_LIST_FILES"))
		fputs(((const char *(*)(void))proc("DebugListFiles"))(), stderr);
	long last = 0;
	const unsigned long unhandled = chimera_gl_host_unhandled(&last);
	if (unhandled)
		fprintf(stderr, "gpu bridge: %lu calls had no case (last opcode %ld)\n", unhandled, last);
	fflush(stdout);
	_Exit(rc);
}
