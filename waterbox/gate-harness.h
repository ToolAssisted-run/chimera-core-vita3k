/* The gate's harness, shared by run-native (the exports called directly) and
 * run-wbx (the exports through miniBox), so the two print the same lines from
 * the same loop and differ only by the sandbox.
 *
 * usage: <runner> <app.vpk> --work <dir> [--frames N] [--timeout S]
 *        [--digest-every N] [--screenshot F=PATH]... [--cpu-mhz N] [--rtc-start S]
 *
 * Lines: "frame F time_ns=T video=H" every --digest-every frames, then
 * "app=NAME frames=F exited=E time_ns=T switches=S video=H WxH". The work
 * directory (made fresh; only one carrying the harness's mark is emptied)
 * receives the machine's log at the end.
 * SPDX-License-Identifier: MIT */
#pragma once

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HARNESS_MAX_SHOTS 64

struct harness_opts {
	const char *app;
	const char *work;
	uint64_t frames;
	int timeout;
	uint64_t digest_every;
	uint64_t cpu_mhz, rtc_start;
	int shots;
	uint64_t shot_frame[HARNESS_MAX_SHOTS];
	const char *shot_path[HARNESS_MAX_SHOTS];
};

struct harness_core {
	int (*init)(void);
	const char *(*load_error)(void);
	void (*frame_advance)(uint64_t);
	uint32_t *(*video)(void);
	int (*width)(void);
	int (*height)(void);
	uint64_t (*frame_count)(void);
	uint64_t (*exited_at)(void);
	uint64_t (*time_ns)(void);
	uint64_t (*switches)(void);
	int64_t (*log_size)(void);
	const uint8_t *(*log_buffer)(void);
};

static int harness_parse(int argc, char **argv, struct harness_opts *o)
{
	memset(o, 0, sizeof *o);
	o->frames = 300;
	o->timeout = 120;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--work") && i + 1 < argc) o->work = argv[++i];
		else if (!strcmp(a, "--frames") && i + 1 < argc) o->frames = strtoull(argv[++i], NULL, 10);
		else if (!strcmp(a, "--timeout") && i + 1 < argc) o->timeout = atoi(argv[++i]);
		else if (!strcmp(a, "--digest-every") && i + 1 < argc) o->digest_every = strtoull(argv[++i], NULL, 10);
		else if (!strcmp(a, "--cpu-mhz") && i + 1 < argc) o->cpu_mhz = strtoull(argv[++i], NULL, 10);
		else if (!strcmp(a, "--rtc-start") && i + 1 < argc) o->rtc_start = strtoull(argv[++i], NULL, 10);
		else if (!strcmp(a, "--screenshot") && i + 1 < argc && o->shots < HARNESS_MAX_SHOTS) {
			char *spec = argv[++i], *eq = strchr(spec, '=');
			if (!eq) { fprintf(stderr, "--screenshot wants F=PATH\n"); return 0; }
			*eq = '\0';
			o->shot_frame[o->shots] = strtoull(spec, NULL, 10);
			o->shot_path[o->shots++] = eq + 1;
		} else if (!o->app && a[0] != '-') o->app = a;
		else { fprintf(stderr, "unknown argument %s\n", a); return 0; }
	}
	if (!o->app || !o->work) {
		fprintf(stderr, "usage: %s <app.vpk> --work <dir> [--frames N] [--timeout S] [--digest-every N]"
			" [--screenshot F=PATH]... [--cpu-mhz N] [--rtc-start S]\n", argv[0]);
		return 0;
	}
	return 1;
}

/* The settings object both flavours read, as the frontend would mount it. */
static void harness_settings(const struct harness_opts *o, char *buf, size_t n)
{
	snprintf(buf, n, "{\"cpu_mhz\": \"%" PRIu64 "\", \"rtc_start\": \"%" PRIu64 "\"}", o->cpu_mhz, o->rtc_start);
}

/* A fresh work directory: made, or emptied when it carries our mark. */
static int harness_fresh_work(const char *work)
{
	char mark[4096], cmd[8300];
	snprintf(mark, sizeof mark, "%s/.vita3k-harness", work);
	struct stat st;
	if (stat(work, &st) == 0) {
		if (stat(mark, &st) != 0) {
			fprintf(stderr, "%s exists and is not a work directory of ours\n", work);
			return 0;
		}
		snprintf(cmd, sizeof cmd, "find '%s' -mindepth 1 ! -name .vita3k-harness -delete", work);
		if (system(cmd) != 0) return 0;
		return 1;
	}
	snprintf(cmd, sizeof cmd, "mkdir -p '%s' && touch '%s'", work, mark);
	return system(cmd) == 0;
}

static uint64_t harness_fnv1a(const uint8_t *p, size_t n)
{
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
	return h;
}

/* uncompressed 32-bit TGA, top row first */
static int harness_tga(const char *path, const uint8_t *bgra, int w, int h)
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	uint8_t hd[18] = { 0 };
	hd[2] = 2; hd[12] = w & 0xff; hd[13] = w >> 8; hd[14] = h & 0xff; hd[15] = h >> 8;
	hd[16] = 32; hd[17] = 0x28;
	fwrite(hd, 1, sizeof hd, f);
	fwrite(bgra, 1, (size_t)w * h * 4, f);
	return fclose(f) == 0;
}

/* the machine's log, into the work directory */
static void harness_save_log(const struct harness_core *c, const struct harness_opts *o)
{
	const int64_t n = c->log_size();
	if (n > 0) {
		char path[4096];
		snprintf(path, sizeof path, "%s/vita3k.log", o->work);
		FILE *f = fopen(path, "wb");
		if (f) { fwrite(c->log_buffer(), 1, (size_t)n, f); fclose(f); }
	}
}

/* Init, the frames, the lines, the log. Returns the process's exit status. */
static int harness_run(const struct harness_core *c, const struct harness_opts *o, const char *app_name)
{
	alarm((unsigned)o->timeout);
	if (!c->init()) {
		fprintf(stderr, "Init failed: %s\n", c->load_error());
		harness_save_log(c, o);
		return 6;
	}
	const int w = c->width(), h = c->height();
	const size_t bytes = (size_t)w * h * 4;
	for (uint64_t f = 1; f <= o->frames; f++) {
		c->frame_advance(0);
		if (c->exited_at())
			break;
		const uint8_t *pic = (const uint8_t *)c->video();
		if (o->digest_every && f % o->digest_every == 0)
			printf("frame %" PRIu64 " time_ns=%" PRIu64 " video=%016" PRIx64 "\n", f, c->time_ns(), harness_fnv1a(pic, bytes));
		for (int s = 0; s < o->shots; s++)
			if (o->shot_frame[s] == f && !harness_tga(o->shot_path[s], pic, w, h))
				fprintf(stderr, "could not write %s\n", o->shot_path[s]);
	}
	printf("app=%s frames=%" PRIu64 " exited=%" PRIu64 " time_ns=%" PRIu64 " switches=%" PRIu64 " video=%016" PRIx64 " %dx%d\n",
		app_name, c->frame_count(), c->exited_at(), c->time_ns(), c->switches(),
		harness_fnv1a((const uint8_t *)c->video(), bytes), w, h);
	fflush(stdout);
	harness_save_log(c, o);
	return 0;
}
