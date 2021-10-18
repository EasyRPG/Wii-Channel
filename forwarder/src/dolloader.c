// This code was originally written by shagkur of the devkitpro team

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include "dolloader.h"

#define MAXSECT_TEXT 7
#define MAXSECT_DATA 11

typedef struct _dolheader {
	u32 text_pos[MAXSECT_TEXT];
	u32 data_pos[MAXSECT_DATA];
	u32 text_start[MAXSECT_TEXT];
	u32 data_start[MAXSECT_DATA];
	u32 text_size[MAXSECT_TEXT];
	u32 data_size[MAXSECT_DATA];
	u32 bss_start;
	u32 bss_size;
	u32 entry_point;
} dolheader;

u32 load_dol_image(void *dolstart, struct __argv *argv) {
	u32 i;
	dolheader *dolfile;

	if (dolstart) {
		dolfile = (dolheader *) dolstart;
		for (i = 0; i < MAXSECT_TEXT; i++) {
			// Skip empty sections
			if ((!dolfile->text_size[i]) || (dolfile->text_start[i] < 0x100))
				continue;
#if DEBUG
			printf("loading text section %u @ 0x%08x (%u bytes)\n",
				i, dolfile->text_start[i],	dolfile->text_size[i]);
#endif
			ICInvalidateRange((void *)dolfile->text_start[i], dolfile->text_size[i]);
			memmove((void *)dolfile->text_start[i],
				dolstart + dolfile->text_pos[i], dolfile->text_size[i]);
		}

		for(i = 0; i < MAXSECT_DATA; i++) {
			// skip empty sections
			if ((!dolfile->data_size[i]) || (dolfile->data_start[i] < 0x100))
				continue;
#if DEBUG
			printf ("loading data section %u @ 0x%08x (%u bytes)\n",
				i, dolfile->data_start[i], dolfile->data_size[i]);
#endif
			memmove((void *)dolfile->data_start[i],
				dolstart + dolfile->data_pos[i], dolfile->data_size[i]);
			DCFlushRangeNoSync((void *)dolfile->data_start[i], dolfile->data_size[i]);
		}

#if DEBUG
		printf ("clearing bss\n");
#endif
		memset((void *)dolfile->bss_start, 0, dolfile->bss_size);
		DCFlushRange((void *)dolfile->bss_start, dolfile->bss_size);

		if (argv && argv->argvMagic == ARGV_MAGIC) {
#if DEBUG
			printf ("setting arguments\n");
#endif
			void *new_argv = (void *)(dolfile->entry_point + 8);
			memmove(new_argv, argv, sizeof(*argv));
			DCFlushRange(new_argv, sizeof(*argv));
		}

		// success
		return dolfile->entry_point;
	}

	return 0;
}
