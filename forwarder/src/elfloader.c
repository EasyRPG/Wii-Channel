/*
 * Copyright (c) 2001 William L. Pitts
 * Modifications (c) 2004 Felix Domke
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely permitted
 * provided that the above copyright notice and this paragraph and the
 * following disclaimer are duplicated in all such forms.
 *
 * This software is provided "AS IS" and without any express or implied
 * warranties, including, without limitation, the implied warranties of
 * merchantability and fitness for a particular purpose.
 */

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include "elf_abi.h"

bool valid_elf_image(void *addr) {
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *) addr;
	if (!IS_ELF (*ehdr)) return false;
	if (ehdr->e_type != ET_EXEC) return false;
	if (ehdr->e_machine != EM_PPC) return false;

	return true;
}

u32 load_elf_image(void *addr) {
	Elf32_Ehdr *ehdr;
	Elf32_Shdr *shdr;
	u8 *strtab = 0;
	u8 *image;
	int i;

	ehdr = (Elf32_Ehdr *)addr;
	/* Find the section header string table for output info */
	shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff +
		(ehdr->e_shstrndx * sizeof(Elf32_Shdr)));

	/* Use string table, if present */
	if (shdr->sh_type == SHT_STRTAB)
		strtab = (u8 *)(addr + shdr->sh_offset);

	for (i = 0; i < ehdr->e_shnum; ++i) {
		shdr = (Elf32_Shdr *)(addr + ehdr->e_shoff + (i * sizeof(Elf32_Shdr)));

		/* Skip empty and invalid sections */
		if (!(shdr->sh_flags & SHF_ALLOC) || shdr->sh_addr == 0 ||
			shdr->sh_size == 0)
				continue;

		shdr->sh_addr &= 0x3FFFFFFF;
		shdr->sh_addr |= 0x80000000;

#ifdef DEBUG
		printf("%sing section %d @ 0x%08x (%u bytes): %s\n",
			(shdr->sh_type == SHT_NOBITS) ?	"clear" : "load",
			i, (u32)shdr->sh_addr, (u32)shdr->sh_size,
			strtab ? (char *)&strtab[shdr->sh_name] : "unknown");
#endif

		if (shdr->sh_type == SHT_NOBITS)
			/* Clear section */
			memset((void *)shdr->sh_addr, 0, shdr->sh_size);
		else {
			/* Load each appropriate section */
			image = (u8 *)addr + shdr->sh_offset;
			memcpy((void *)shdr->sh_addr, (const void *)image,
				shdr->sh_size);
		}
		DCFlushRangeNoSync((void *)shdr->sh_addr, shdr->sh_size);
	}

	return (ehdr->e_entry & 0x3FFFFFFF) | 0x80000000;
}
