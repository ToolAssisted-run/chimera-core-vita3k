/* InputTest: once a frame, what the machine reads of its input, as a line in
 * ux0:data/inputtest/input.txt:
 *
 *   v<vcount> b<buttons> e<buttons, Ext2> l<lx>,<ly> r<rx>,<ry>
 *   f<reports>[:<x>,<y>,<id>] k<reports>[:<x>,<y>,<id>]
 *   a<accel x,y,z in thousandths of a g> g<gyro x,y,z in milliradians a second>
 *
 * f is the front panel, k the rear. Each read is 8 ms after a vblank, in the
 * middle of the frame, so a line tagged v<n> is frame n+1's input. The save,
 * savedata0:count.bin, counts the runs: read at start ("loaded N" or "loaded
 * none"), written one higher at vcount 30 ("saved N"). CC0. */
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static SceUID out;

static void put(const char *s)
{
	sceIoWrite(out, s, strlen(s));
}

static int touch(char *p, size_t n, char tag, const SceTouchData *t)
{
	if (t->reportNum == 0)
		return snprintf(p, n, " %c0", tag);
	return snprintf(p, n, " %c%d:%d,%d,%d", tag, (int)t->reportNum, t->report[0].x, t->report[0].y, t->report[0].id);
}

int main(void)
{
	SceAppUtilInitParam init;
	SceAppUtilBootParam boot;
	memset(&init, 0, sizeof init);
	memset(&boot, 0, sizeof boot);
	sceAppUtilInit(&init, &boot);

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	sceMotionStartSampling();
	/* the sensors as they are, unfiltered */
	sceMotionSetGyroBiasCorrection(0);
	sceMotionSetDeadband(0);

	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir("ux0:data/inputtest", 0777);
	out = sceIoOpen("ux0:data/inputtest/input.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);

	char line[256];
	int count = -1;
	SceUID fd = sceIoOpen("savedata0:count.bin", SCE_O_RDONLY, 0);
	if (fd >= 0) {
		if (sceIoRead(fd, &count, sizeof count) != sizeof count)
			count = -2;
		sceIoClose(fd);
	}
	if (count == -1)
		put("loaded none\n");
	else {
		snprintf(line, sizeof line, "loaded %d\n", count);
		put(line);
	}

	for (;;) {
		sceDisplayWaitVblankStart();
		sceKernelDelayThread(8000);
		const int vcount = sceDisplayGetVcount();

		SceCtrlData pad, ext;
		SceTouchData front, rear;
		SceMotionSensorState ms;
		memset(&pad, 0, sizeof pad);
		memset(&ext, 0, sizeof ext);
		memset(&front, 0, sizeof front);
		memset(&rear, 0, sizeof rear);
		memset(&ms, 0, sizeof ms);
		sceCtrlPeekBufferPositive(0, &pad, 1);
		sceCtrlPeekBufferPositiveExt2(0, &ext, 1);
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1);
		sceTouchPeek(SCE_TOUCH_PORT_BACK, &rear, 1);
		sceMotionGetSensorState(&ms, 1);

		int n = snprintf(line, sizeof line, "v%d b%08x e%08x l%d,%d r%d,%d", vcount,
			(unsigned)pad.buttons, (unsigned)ext.buttons, pad.lx, pad.ly, pad.rx, pad.ry);
		n += touch(line + n, sizeof line - n, 'f', &front);
		n += touch(line + n, sizeof line - n, 'k', &rear);
		snprintf(line + n, sizeof line - n, " a%ld,%ld,%ld g%ld,%ld,%ld\n",
			lrintf(ms.accelerometer.x * 1000), lrintf(ms.accelerometer.y * 1000), lrintf(ms.accelerometer.z * 1000),
			lrintf(ms.gyro.x * 1000), lrintf(ms.gyro.y * 1000), lrintf(ms.gyro.z * 1000));
		put(line);

		if (vcount == 30) {
			const int next = count < 0 ? 1 : count + 1;
			fd = sceIoOpen("savedata0:count.bin", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
			if (fd >= 0) {
				sceIoWrite(fd, &next, sizeof next);
				sceIoClose(fd);
			}
			snprintf(line, sizeof line, "saved %d\n", next);
			put(line);
		}
	}
	return 0;
}
