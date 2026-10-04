/* HleTest: what a system call costs the machine, and how late a sleeping
 * thread wakes while another keeps the CPU busy. ux0:data/hletest/result.txt:
 *
 *   calls=10000 us=U
 *       machine time across 10000 sceKernelGetThreadId calls, read with
 *       sceKernelGetProcessTimeWide (each call costs the machine 1 us);
 *   sleeps=50 max_late_us=M total_late_us=T
 *       a thread sleeping 100 us, 50 times, while the main thread spins in
 *       its own code: how much longer than 100 us each sleep took.
 *
 * CC0. */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdio.h>
#include <string.h>

#define CALLS 10000
#define SLEEPS 50
#define SLEEP_US 100

static volatile int g_done;
static int g_max_late, g_total_late;

static int sleeper(SceSize args, void *argp)
{
	for (int i = 0; i < SLEEPS; i++) {
		const SceUInt64 t0 = sceKernelGetProcessTimeWide();
		sceKernelDelayThread(SLEEP_US);
		const int late = (int)(sceKernelGetProcessTimeWide() - t0) - SLEEP_US;
		if (late > g_max_late)
			g_max_late = late;
		g_total_late += late;
	}
	g_done = 1;
	return 0;
}

int main(void)
{
	char line[160];
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir("ux0:data/hletest", 0777);
	const SceUID out = sceIoOpen("ux0:data/hletest/result.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);

	const SceUInt64 t0 = sceKernelGetProcessTimeWide();
	for (int i = 0; i < CALLS; i++)
		sceKernelGetThreadId();
	const SceUInt64 t1 = sceKernelGetProcessTimeWide();
	snprintf(line, sizeof line, "calls=%d us=%llu\n", CALLS, (unsigned long long)(t1 - t0));
	sceIoWrite(out, line, strlen(line));

	const SceUID th = sceKernelCreateThread("sleeper", sleeper, 0x40, 0x10000, 0, 0, NULL);
	sceKernelStartThread(th, 0, NULL);
	volatile unsigned x = 1;
	while (!g_done)
		x = x * 1103515245u + 12345u;
	snprintf(line, sizeof line, "sleeps=%d max_late_us=%d total_late_us=%d\n", SLEEPS, g_max_late, g_total_late);
	sceIoWrite(out, line, strlen(line));
	sceIoClose(out);

	for (;;)
		sceKernelDelayThread(1000000);
	return 0;
}
