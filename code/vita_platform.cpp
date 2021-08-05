#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h> 
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <psp2/rtc.h> 

#include <stdlib.h> //malloc, free
#include <string.h> //Used only for the config

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

#include "utils.c"
#include "math.c"
#include "string.c"

#include "platform_common.c"

// Global Variables for the game
global_variable Render_Buffer render_buffer;
global_variable f32 current_time;
global_variable b32 lock_fps = true;
global_variable struct Os_Job_Queue *general_queue;

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_ASSERT assert
#include "stb_image.h"

#include <alloca.h>
#include "wav_importer.h"
#include "ogg_importer.h"

#include "cooker_common.c"
#include "asset_loader.c"

#include "profiler.c"
#include "software_rendering.c"
#include "console.c"

//TODO: Implement this when doing audio
#define interlocked_compare_exchange(a, b, c) (Sound_Reading_Access)0
#include "audio.c"

#include "game.c"

#define DISPLAY_WIDTH 640
#define DISPLAY_HEIGHT 368

struct BufferPixel {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	unsigned char alpha;
};

struct GamePixel {
	unsigned char blue;
	unsigned char green;
	unsigned char red;
	unsigned char alpha;
};

///File IO
internal void os_free_file(String s) {
	free(s.data);
}

internal String os_read_entire_file(char *file_path) {
	SceUID fileID = sceIoOpen(file_path, SCE_O_RDONLY, 0777);
	SceIoStat fileStats;
	sceIoGetstatByFd(fileID, &fileStats);
	
	SceSize fileNumBytes = fileStats.st_size;
	
	//TODO: sceKernelAllocMemBlock wasn't working for this case for some reason, maybe try to figure out why
	void* data = malloc(fileNumBytes);
	
	sceIoRead(fileID, data, fileNumBytes);

	sceIoClose(fileID);
	return { (char*)data, fileNumBytes };
}

inline String os_get_pak_data() {
	return os_read_entire_file("app0:assets.pak");
}

internal String os_read_save_file() {
	//TODO: Read the save for real
	const char* dummy = "00000";
	return {(char*)dummy, strlen(dummy)};
}

internal String os_read_config_file() {
	//TODO: See if its necessary to actually read the .txt
	const char* config = "mouse_sensitivity = 1.0\nwindowed = false\nlock_fps = true";
	return {(char*)config, strlen(config)};
}

internal b32 os_write_save_file(String data) {
	b32 result = false;
	
	//TODO: Implement

	return result;
}

//Multi-threading
internal void os_add_job_to_queue(Os_Job_Queue *queue, Os_Job_Callback *callback, void *data1, void* data2, void *data3) {
	//TODO: Implement
}

///Misc
internal void os_toggle_fullscreen() {
	//Do nothing
}

int main(void){
	
	//TODO: Pegar a resolução da tela com essa função: sceDisplayGetMaximumFrameBufResolution()
	void* base;
	SceUID memblock = sceKernelAllocMemBlock(
		"frame_buffer", 
		SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 
		256 * 1024 * 5, 
		NULL
	);
	sceKernelGetMemBlockBase(memblock, &base);
	
	SceDisplayFrameBuf dbuf = { 
		sizeof(SceDisplayFrameBuf), 
		base, 
		DISPLAY_WIDTH, 
		SCE_DISPLAY_PIXELFORMAT_A8B8G8R8, 
		DISPLAY_WIDTH, 
		DISPLAY_HEIGHT
	};

	render_buffer.width  = DISPLAY_WIDTH;
	render_buffer.height = DISPLAY_HEIGHT;
	//TODO: Change malloc here to something else
	render_buffer.pixels = (u32*) malloc(sizeof(u32)*render_buffer.width*render_buffer.height);

	BufferPixel* buffer = (BufferPixel*) base;
	GamePixel* gameBuffer = (GamePixel*) render_buffer.pixels;

	f32 targetFPS;
	if (sceDisplayGetRefreshRate(&targetFPS) < 0){
		targetFPS = 60;
	}
	f32 lastDelta = 1.f / targetFPS;
	f32 targetDelta = lastDelta;

	unsigned int tickResolution = sceRtcGetTickResolution();
	f32 secondsPerTick = 1.0f / ((f32) tickResolution);
	SceRtcTick lastTick;
	sceRtcGetCurrentTick(&lastTick);
	
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	SceCtrlData ctrl_peek;
	SceCtrlData ctrl_press;
	Input gameInput = { 0 };
	
	do{
		ctrl_press = ctrl_peek;
		sceCtrlPeekBufferPositive(0, &ctrl_peek, 1);
		ctrl_press.buttons = ctrl_peek.buttons & ~ctrl_press.buttons;

		//TODO: Get input here

		update_game(&gameInput, lastDelta);

		for (unsigned int x = 0; x < DISPLAY_WIDTH; x++) {
			for (unsigned int y = 0; y < DISPLAY_HEIGHT; y++) {
				unsigned int pixelIndex = ((DISPLAY_WIDTH * y) + x);
				BufferPixel pixel;
				pixel.alpha = 255;
				pixel.red = gameBuffer[pixelIndex].red;
				pixel.green = gameBuffer[pixelIndex].green;
				pixel.blue = gameBuffer[pixelIndex].blue;
				
				unsigned int oppositeIndex = ((DISPLAY_WIDTH * (DISPLAY_HEIGHT - 1 - y)) + x);
				buffer[oppositeIndex] = pixel;
			}
		}

		sceDisplaySetFrameBuf(&dbuf, SCE_DISPLAY_SETBUF_NEXTFRAME);

		//Frame rate control
		SceRtcTick currentTick;
		sceRtcGetCurrentTick(&currentTick);
		SceUInt64 tickDiff = currentTick.tick - lastTick.tick;

		f32 deltaBeforeSleep = min(0.1f, secondsPerTick * tickDiff);

		//Sleep
		sceKernelDelayThread((SceUInt)((targetDelta - deltaBeforeSleep) * 1000000));

		sceRtcGetCurrentTick(&currentTick);
		tickDiff = currentTick.tick - lastTick.tick;
		
		lastDelta = min(0.1f, secondsPerTick * tickDiff);
		lastTick = currentTick;

		current_time += lastDelta;
		
	}while(!(ctrl_press.buttons & SCE_CTRL_START));
	
	sceKernelFreeMemBlock(memblock);
	sceKernelExitProcess(0);
	return 0;
}