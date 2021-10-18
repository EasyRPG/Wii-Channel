#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ogc/machine/processor.h>
#include <sdcard/wiisd_io.h>
#include <sdcard/gcsd.h>
#include <fat.h>
#include "video.h"
#include "usbgecko.h"
#include "dolloader.h"
#include "elfloader.h"

// executable gets copied here (mid of MEM2)
#define EXECUTABLE_MEM_ADDR 0x92000000

extern void __exception_closeall();
typedef void (*entrypoint) (void);

static struct {
	const char *name;
	const DISC_INTERFACE *di;
	bool avail;
} fs[] = {
	{ "sd",  &__io_wiisd, false },
	{ "usb", &__io_usbstorage, false },
	{ "gca", &__io_gcsda, false },
	{ "gcb", &__io_gcsdb, false },
	{ NULL }
};

// possible locations of the executable (without device and extension)
static const char* locations[] = {
	"apps/easyrpg-player/boot",
	"apps/easyrpg/boot",
	"apps/easyrpg/player",
	"apps/easyrpg-player",
	"easyrpg-player",
	NULL
};

void ShowFailureMsg(const char* msg) {
	StopRenderThread(false);
	EnableConsole();
	// reset stdout to usbgecko
	EnableUSBGecko(CARD_SLOTB);
	printf("%s\n", msg);
	// print to console
	fprintf(stderr, "%s\n", msg);
#if !DEBUG
	sleep(5);
#endif
}

bool DoMount() {
	bool result = false;
	for (int i = 0; fs[i].name != NULL; i++) {
#if DEBUG
		printf("Mounting \"%s\"...", fs[i].name);
#endif
		if (fatMountSimple(fs[i].name, fs[i].di)) {
			fs[i].avail = true;
#if DEBUG
			printf("OK\n");
#endif
			result = true;
		}
#if DEBUG
		else
			printf("N/A\n");
#endif

	}
	return result;
}

void DoUnmount() {
	for (int i = 0; fs[i].name != NULL; i++) {
		if (!fs[i].avail) continue;

#if DEBUG
		printf("Unmounting \"%s\"...\n", fs[i].name);
#endif
		fatUnmount(fs[i].name);
	}
}

bool CheckPaths(const char* path, char *output) {
	for (int i = 0, j = 0; fs[i].name != NULL; j++) {
		// skip
		if (!fs[i].avail) {
			j = 0;
			i++;
			continue;
		}

		sprintf(output, "%s:/%s.%s", fs[i].name, path, (j % 2) ? "elf" : "dol");
#if DEBUG
		printf("Checking \"%s\"...\n", output);
#endif
		if (access(output, F_OK) == 0)
			return true;

		if (j % 2) {
			j = 0;
			i++;
		}
#if !DEBUG
		// nicer progress display
		usleep(2000);
#endif
 	}

	return false;
}

int main(int argc, char *argv[]) {
	(void) argc;
	(void) argv;

	FILE *exe_file = NULL;
	bool wait_fade = true;
#if DEBUG
	// no time to wait
	wait_fade = false;
#endif

	EnableUSBGecko(CARD_SLOTB);

	// start our nice graphics
	InitVideo();
	StartRenderThread(wait_fade);

	// mount devices SD/USB
	if (!DoMount()) {
		// exit, since no device present
		ShowFailureMsg("Could not mount device or no device present! Exiting...");
		goto _failure;
	}
	SetProgress(10);

	char player_path[48];
	bool found = false;
	// check each location
	int i = 0;
	while (locations[i] != 0) {
		if (CheckPaths(locations[i], player_path)) {
			found = true;
			break;
		}
		i++;
		SetProgress(10+i*5);
	}

	// exit, since nothing was found
	if (!found) {
		ShowFailureMsg("Did not find executable! Exiting...");
		goto _failure;
	}
	SetProgress(50);

#if DEBUG
	printf("Found executable: \"%s\".\n", player_path);
#endif

	// load file
	exe_file = fopen(player_path,"rb");
	fseek(exe_file, 0, SEEK_END);
	size_t exe_size = ftell(exe_file);
	fseek(exe_file, 0, SEEK_SET);
	SetProgress(60);

	void *exe_buffer = (void *)EXECUTABLE_MEM_ADDR;
	if(fread(exe_buffer, 1, exe_size, exe_file) != exe_size) {
		ShowFailureMsg("Could not read executable! Exiting...");
		goto _failure;
	}
	fclose(exe_file);
	DoUnmount();
	SetProgress(80);

	// set arguments
	struct __argv args;
	bzero(&args, sizeof(args));
	args.argvMagic = ARGV_MAGIC;
	args.length = strlen(player_path) + 2;
	args.commandLine = (char*)malloc(args.length);
	if (!args.commandLine) {
		ShowFailureMsg("Could not reserve memory for arguments! Exiting...");
		goto _failure;
	}
	strcpy(args.commandLine, player_path);
	args.commandLine[args.length - 1] = '\0';
	args.argc = 1;
	args.argv = &args.commandLine;
	args.endARGV = args.argv + 1;

	u32 exe_address = 0;
	if (valid_elf_image(exe_buffer))
		exe_address = load_elf_image(exe_buffer);
	else
		exe_address = load_dol_image(exe_buffer, &args);

	// exit, since executable possibly broken
	if (!exe_address) {
		ShowFailureMsg("Could not load executable! Exiting...");
		goto _failure;
	}
	SetProgress(100);

	StopRenderThread(wait_fade);
	DeinitVideo();

	entrypoint exe_entrypoint = (entrypoint)exe_address;

	// clean up and execute
#if DEBUG
	printf(":)\n");
#endif
	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
	u32 level;
	_CPU_ISR_Disable(level);
	__exception_closeall();
	exe_entrypoint();
	_CPU_ISR_Restore(level);

	// should never be reached :)
	exit(0);

_failure:
	if (exe_file)
		fclose(exe_file);

	DoUnmount();

	DeinitVideo();

#if DEBUG
	printf(":(\n");
#endif

	// TODO: check stub
	//SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
	exit(1);
}
