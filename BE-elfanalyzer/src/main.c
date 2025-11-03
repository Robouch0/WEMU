/*
** EPITECH PROJECT, 2024
** objdump
** File description:
** main
*/

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stddef.h>
#include <unistd.h>

#include "my_utils.h"

void print_elf32_ehdr(Elf32_Ehdr const *ehdr)
{
    dprintf(1, "ELF header:\n");
    dprintf(1, "e_ident\t\t%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
        ehdr->e_ident[0], ehdr->e_ident[1], ehdr->e_ident[2], ehdr->e_ident[3],
        ehdr->e_ident[4], ehdr->e_ident[5], ehdr->e_ident[6], ehdr->e_ident[7],
        ehdr->e_ident[8], ehdr->e_ident[9], ehdr->e_ident[10], ehdr->e_ident[11],
        ehdr->e_ident[12], ehdr->e_ident[13], ehdr->e_ident[14], ehdr->e_ident[15]);
    dprintf(1, "e_type\t\t0x%04x [%s]\n", ehdr->e_type, (ehdr->e_type == 0xFE01) ? "RPL" : "UNKNOWN");
    dprintf(1, "e_machine\t0x%04x [%s]\n", ehdr->e_machine, (ehdr->e_machine == 0x0014) ? "PowerPC" : "UNKNOWN");
    dprintf(1, "e_version\t0x%08x\n", ehdr->e_version);
    dprintf(1, "e_entry\t\t0x%08x\n", ehdr->e_entry);
    dprintf(1, "e_phoff\t\t0x%08x\n", ehdr->e_phoff);
    dprintf(1, "e_shoff\t\t0x%08x\n", ehdr->e_shoff);
    dprintf(1, "e_flags\t\t0x%08x\n", ehdr->e_flags);
    dprintf(1, "e_ehsize\t0x%04x\n", ehdr->e_ehsize);
    dprintf(1, "e_phentsize\t0x%04x\n", ehdr->e_phentsize);
    dprintf(1, "e_phnum\t\t0x%04x\n", ehdr->e_phnum);
    dprintf(1, "e_shentsize\t0x%04x\n", ehdr->e_shentsize);
    dprintf(1, "e_shnum\t\t0x%04x\n", ehdr->e_shnum);
    dprintf(1, "e_shstrndx\t0x%04x\n", ehdr->e_shstrndx);
    dprintf(1, "\n");
}

static void print_instructions_infos(unsigned char const *line)
{
    uint32_t instruction = 0;

    memcpy(&instruction, line, sizeof(uint32_t));
    instruction = be32((void *)&instruction);
    for (int i = 31; i >= 0; i--) {
        dprintf(1, "%d", (instruction >> i) & 1);
    }
    uint8_t const opcode = (instruction >> 26) & 0b111111;
    dprintf(1, "\topcode: %hd", opcode);
    if (opcode == 14 || opcode == 15) {
        uint8_t const rs = (instruction >> 21) & 0b11111;
        uint8_t const ra = (instruction >> 16) & 0b11111;
        uint16_t const imm = (instruction) & 0b1111111111111111;
        dprintf(1, "\trs: %hd", rs);
        dprintf(1, "\tra: %hd", ra);
        dprintf(1, "\timm: %hd", imm);
    }
    dprintf(1, "\n");
}

static void print_section_content1(void const *elfptr, Elf32_Shdr const *sh)
{
    Elf32_Off offset = 0;

    while (offset < sh->sh_size) {
        Elf32_Off const index = offset + sh->sh_offset;
        dprintf(1, " %04x\t", (int)(offset + sh->sh_addr));
        print_instructions_infos(&((unsigned char *)elfptr)[index]);
        offset += 4;
    }
}


void print_functions(uint8_t const *rpx, Elf32_Ehdr const *ehdr) {
    Elf32_Word type = 0;
    Elf32_Shdr shtab[ehdr->e_shnum];
    uint32_t shdr_rpl_offset = ehdr->e_shoff;
    for (int i = 0; i < ehdr->e_shnum; i++) {
        shtab[i].sh_name      = be32(rpx + shdr_rpl_offset + 0x00);
        shtab[i].sh_type      = be32(rpx + shdr_rpl_offset + 0x04);
        shtab[i].sh_flags     = be32(rpx + shdr_rpl_offset + 0x08);
        shtab[i].sh_addr      = be32(rpx + shdr_rpl_offset + 0x0c);
        shtab[i].sh_offset    = be32(rpx + shdr_rpl_offset + 0x10);
        shtab[i].sh_size      = be32(rpx + shdr_rpl_offset + 0x14);
        shtab[i].sh_link      = be32(rpx + shdr_rpl_offset + 0x18);
        shtab[i].sh_info      = be32(rpx + shdr_rpl_offset + 0x1c);
        shtab[i].sh_addralign = be32(rpx + shdr_rpl_offset + 0x20);
        shtab[i].sh_entsize   = be32(rpx + shdr_rpl_offset + 0x24);
        dprintf(1, "ELF section header #%d:\n", i);
        dprintf(1, "sh_name:\t0x%08x\n", shtab[i].sh_name);
        dprintf(1, "sh_type:\t0x%08x\n", shtab[i].sh_type);
        dprintf(1, "sh_flags:\t0x%08x\n", shtab[i].sh_flags);
        dprintf(1, "sh_addr:\t0x%08x\n", shtab[i].sh_addr);
        dprintf(1, "sh_offset:\t0x%08x\n", shtab[i].sh_offset);
        dprintf(1, "sh_size:\t0x%08x\n", shtab[i].sh_size);
        dprintf(1, "sh_link:\t0x%08x\n", shtab[i].sh_link);
        dprintf(1, "sh_info:\t0x%08x\n", shtab[i].sh_info);
        dprintf(1, "sh_addralign:\t0x%08x\n", shtab[i].sh_addralign);
        dprintf(1, "sh_entsize:\t0x%08x\n\n", shtab[i].sh_entsize);
        shdr_rpl_offset += ehdr->e_shentsize;
    }

    const Elf32_Shdr *symtab = NULL;
    const Elf32_Shdr *strtab = NULL;
    Elf32_Shdr const *shstr = &shtab[be16(rpx + offsetof(Elf32_Ehdr, e_shstrndx))];
    char const *section_names = (char *)(rpx + shstr->sh_offset);

    for (int i = 0; i < ehdr->e_shnum; i++) {
        char const *name = section_names + shtab[i].sh_name;
        if (strcmp(name, ".symtab") == 0) {
            symtab = &shtab[i];
        }
        if (strcmp(name, ".strtab") == 0) {
            strtab = &shtab[i];
        }
    }
    if (symtab == NULL || strtab == NULL) {
        dprintf(1, "symtab or strtab section not found!\n");
        return;
    }
    for (int i = 0; i < ehdr->e_shnum; i++) {
        type = shtab[i].sh_type;
        if (shtab[i].sh_size == 0 || type == SHT_NOBITS)
            continue;
        if ((type == SHT_STRTAB || type == SHT_SYMTAB) && !shtab[i].sh_flags)
            continue;
        if ((type & (SHT_REL | SHT_RELA | SHT_RELR)) &&
            shtab[i].sh_flags == SHF_INFO_LINK)
            continue;
        dprintf(1, "Contents of section %s:\n", &section_names[shtab[i].sh_name]);
        print_section_content1(rpx, &shtab[i]);
    }
}

void my_elfanalyze(char const *filename)
{
    uint8_t *rpx = NULL;
    Elf32_Ehdr *ehdr = NULL;
    struct stat statinfos = {0};
    const int fd = open(filename, O_RDONLY);

    if (fd == -1) {
        dprintf(2, "objdump: '%s': No such file\n", filename);
        return;
    }
    ehdr = calloc(1, sizeof(Elf32_Ehdr));
    fstat(fd, &statinfos);
    rpx = mmap(NULL, statinfos.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    for (size_t i = 0; i < offsetof(Elf32_Ehdr, e_type); i++) {
	    ehdr->e_ident[i] = rpx[i];
    }
    ehdr->e_type = be16(rpx + offsetof(Elf32_Ehdr, e_type));
    ehdr->e_machine = be16(rpx + offsetof(Elf32_Ehdr, e_machine));
    ehdr->e_version = be32(rpx + offsetof(Elf32_Ehdr, e_version));
    ehdr->e_entry = be32(rpx + offsetof(Elf32_Ehdr, e_entry));
    ehdr->e_phoff = be32(rpx + offsetof(Elf32_Ehdr, e_phoff));
    ehdr->e_shoff = be32(rpx + offsetof(Elf32_Ehdr, e_shoff));
    ehdr->e_flags = be32(rpx + offsetof(Elf32_Ehdr, e_flags));
    ehdr->e_ehsize = be16(rpx + offsetof(Elf32_Ehdr, e_ehsize));
    ehdr->e_phentsize = be16(rpx + offsetof(Elf32_Ehdr, e_phentsize));
    ehdr->e_phnum = be16(rpx + offsetof(Elf32_Ehdr, e_phnum));
    ehdr->e_shentsize = be16(rpx + offsetof(Elf32_Ehdr, e_shentsize));
    ehdr->e_shnum = be16(rpx + offsetof(Elf32_Ehdr, e_shnum));
    ehdr->e_shstrndx = be16(rpx + offsetof(Elf32_Ehdr, e_shstrndx));
    print_elf32_ehdr(ehdr);
    print_functions(rpx, ehdr);
    munmap(rpx, statinfos.st_size);
    free(ehdr);
}

int main(const int ac, char const *const *av)
{
    if (ac != 2)
        return 1;
    my_elfanalyze(av[1]);
    return 0;
}
