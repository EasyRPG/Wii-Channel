#ifndef ELFLOADER_H
#define ELFLOADER_H

#include <gctypes.h>

bool valid_elf_image(void *addr);
u32 load_elf_image(void *addr);

#endif
