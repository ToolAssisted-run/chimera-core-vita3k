/* AudioTest: two ports playing at once, each a known tone at a known volume,
 * so what the machine hands out can be checked against what was played:
 *
 * - the main port, 48 kHz stereo: a 1000 Hz sine of amplitude 8000 on the
 *   left, silence on the right;
 * - the BGM port, 24 kHz mono: a 250 Hz sine of amplitude 4000, its volume
 *   full on the left and half on the right.
 *
 * Each runs on its own thread, 256 samples a buffer. CC0. */
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#define GRAIN 256

static int main_port(SceSize args, void *argp)
{
	static int16_t buf[2][GRAIN * 2];
	const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, 48000, SCE_AUDIO_OUT_MODE_STEREO);
	uint64_t t = 0;
	for (int b = 0;; b ^= 1) {
		for (int i = 0; i < GRAIN; i++, t++) {
			buf[b][i * 2] = (int16_t)lrint(8000.0 * sin(2.0 * M_PI * 1000.0 * (double)t / 48000.0));
			buf[b][i * 2 + 1] = 0;
		}
		sceAudioOutOutput(port, buf[b]);
	}
	return 0;
}

static int bgm_port(SceSize args, void *argp)
{
	static int16_t buf[2][GRAIN];
	const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, 24000, SCE_AUDIO_OUT_MODE_MONO);
	int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB / 2 };
	sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
	uint64_t t = 0;
	for (int b = 0;; b ^= 1) {
		for (int i = 0; i < GRAIN; i++, t++)
			buf[b][i] = (int16_t)lrint(4000.0 * sin(2.0 * M_PI * 250.0 * (double)t / 24000.0));
		sceAudioOutOutput(port, buf[b]);
	}
	return 0;
}

int main(void)
{
	SceUID a = sceKernelCreateThread("main port", main_port, 0x40, 0x10000, 0, 0, NULL);
	SceUID b = sceKernelCreateThread("bgm port", bgm_port, 0x40, 0x10000, 0, 0, NULL);
	sceKernelStartThread(a, 0, NULL);
	sceKernelStartThread(b, 0, NULL);
	for (;;)
		sceKernelDelayThread(1000000);
	return 0;
}
