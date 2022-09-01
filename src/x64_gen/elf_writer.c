#define ELF_MAGIC0 127
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_CLASS64 2
#define ELF_DATA_2_LSB 1  // 2's complement, little-endian
#define ELF_OS_ABI_SYSV 0
#define ELF_REL_FILE 1     // Relocatable file
#define ELF_EXEC_FILE 2    // Executable file
#define ELF_MACHINE_X86_64 62

// Values for sh_type
#define ELF_SHT_NULL 0 // Unused section header table entry
#define ELF_SHT_PROGBITS 1 // Program data
#define ELF_SHT_SYMTAB 2 // Symbol table
#define ELF_SHT_STRTAB 3 // String table
#define ELF_SHT_RELA 4 // Relocations with addends

// Values for sh_flags
#define ELF_SHF_WRITE (1 << 0) // Writeable.
#define ELF_SHF_ALLOC (1 << 1) // Occupies execution memory.
#define ELF_SHF_EXECINSTR (1 << 2) // Executable.

// Insert info to symbol's st_info field.
#define ELF_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xF))

// ST_BIND subfield of st_info
#define ELF_STB_LOCAL 0
#define ELF_STB_GLOBAL 1

// ST_TYPE subfield of st_info
#define ELF_STT_NOTYPE 0
#define ELF_STT_OBJECT 1
#define ELF_STT_FUNC 2
#define ELF_STT_SECTION 3
#define ELF_STT_FILE 4

// Symbol visibility
#define ELF_STV_DEFAULT 0

#define ELF_NEED_PATCH 0

typedef struct Elf64_Hdr {
    u8 e_ident[16];  // Magic number + stuff
    u16 e_type;      // Object file type.
    u16 e_machine;   // Architecture.
    u32 e_version;   // Object file version.
    u64 e_entry;     // Virtual address of entry point.
    u64 e_phoff;     // File offset for the program header table.
    u64 e_shoff;     // File offset for the section header table.
    u32 e_flags;     // Processor-specific flags.
    u16 e_ehsize;    // Size of this elf header.
    u16 e_phentsize; // Size of each program header table entry.
    u16 e_phnum;     // Number of program header table entries.
    u16 e_shentsize; // Size of each section header table entry.
    u16 e_shnum;     // Number of section header table entries.
    u16 e_shstrndx;  // Index (in section header table) of the entry for the section header strings.
} Elf64_Hdr;

typedef struct Elf64_Shdr{
   u32 sh_name;
   u32 sh_type;
   u64 sh_flags;
   u64 sh_addr;
   u64 sh_offset;
   u64 sh_size;
   u32 sh_link;
   u32 sh_info;
   u64 sh_addralign;
   u64 sh_entsize;
} Elf64_Shdr;

typedef struct Elf64_Sym {
   u32 st_name;
   u8 st_info;
   u8 st_other;
   u16 st_shndx;
   u64 st_value;
   u64 st_size;
} Elf64_Sym;

typedef struct Elf_StrTable {
    Array(u8) bytes;
} Elf_StrTable;

static void Elf_strtab_init(Elf_StrTable* table, Allocator* arena, u32 cap)
{
    table->bytes = array_create(arena, u8, cap);

    array_push(table->bytes, '\0');
}

static u32 Elf_strtab_add(Elf_StrTable* table, const char* str)
{
    u32 loc = (u32) array_len(table->bytes);

    for (const char* p = str; *p; p += 1) {
        array_push(table->bytes, *p);
    }

    array_push(table->bytes, '\0');

    return loc;
}

static u32 Elf_strtab_size(const Elf_StrTable* table)
{
    return (u32) array_len(table->bytes);
}

/*
$ xxd -g 1 -s $((0x180)) -l $((0x41)) out.o
00000180: 48 31 ed 8b 3c 24 48 8d 74 24 08 48 8d 54 fc 10  H1..<$H.t$.H.T..
00000190: 31 c0 e8 09 00 00 00 89 c7 b8 3c 00 00 00 0f 05  1.........<.....
000001a0: 55 48 89 e5 48 83 ec 10 c7 45 fc 0a 00 00 00 c7  UH..H....E......
000001b0: 45 f8 01 00 00 00 8b 45 fc 03 45 f8 48 89 ec 5d  E......E..E.H..]
000001c0: c3
*/
// Hard-coded for now.
static const u8 text_bin[] = {
    0x48, 0x31, 0xed, 0x8b, 0x3c, 0x24, 0x48, 0x8d, 0x74, 0x24, 0x08, 0x48, 0x8d, 0x54, 0xfc, 0x10,
    0x31, 0xc0, 0xe8, 0x09, 0x00, 0x00, 0x00, 0x89, 0xc7, 0xb8, 0x3c, 0x00, 0x00, 0x00, 0x0f, 0x05,
    0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xec, 0x10, 0xc7, 0x45, 0xfc, 0x0a, 0x00, 0x00, 0x00, 0xc7,
    0x45, 0xf8, 0x01, 0x00, 0x00, 0x00, 0x8b, 0x45, 0xfc, 0x03, 0x45, 0xf8, 0x48, 0x89, 0xec, 0x5d,
    0xc3
};

static u32 write_bin(FILE* fd, const void* bin, u32 size, u32 tgt_offset, u32 curr_offset)
{
    u32 offset = curr_offset;

    while (offset < tgt_offset) {
        fputc('\0', fd);
        offset += 1;
    }

    assert(offset == tgt_offset);

    u32 num_written = (u32)fwrite(bin, 1, size, fd);

    if (num_written != size) {
        NIBBLE_FATAL_EXIT("Failed to write elf binary.");
        return offset;
    }

    return offset + size;
}

bool x64_gen_elf(Allocator* gen_mem, Allocator* tmp_mem, BucketList* vars, BucketList* procs, BucketList* str_lits,
                 BucketList* float_lits, BucketList* foreign_procs, const char* output_file)
{
    FILE* out_fd = fopen("elf.o", "wb");
    if (!out_fd) {
        ftprint_err("Failed to open output file `elf.o`\n");
        return false;
    }

    AllocatorState gen_mem_state = allocator_get_state(gen_mem);

    Elf64_Hdr elf_hdr = {
        .e_ident = {ELF_MAGIC0, ELF_MAGIC1, ELF_MAGIC2, ELF_MAGIC3, ELF_CLASS64, ELF_DATA_2_LSB, 1, ELF_OS_ABI_SYSV},
        .e_type = ELF_REL_FILE,
        .e_machine = ELF_MACHINE_X86_64,
        .e_version = 1,
        .e_entry = 0,
        .e_phoff = 0,
        .e_shoff = sizeof(Elf64_Hdr), // Right after this header
        .e_flags = 0,
        .e_ehsize = sizeof(Elf64_Hdr),
        .e_phentsize = 0,
        .e_phnum = 0,
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum = 5, // NULL, .text, .shstrtab, .symtab, .strtab,
        .e_shstrndx = 2,
    };

    Elf_StrTable shstrtab = {0};
    Elf_strtab_init(&shstrtab, gen_mem, 38);

    const u32 text_off = elf_hdr.e_ehsize + elf_hdr.e_shnum * elf_hdr.e_shentsize;
    const u32 text_size = sizeof(text_bin);
    const u32 text_shndx = 1;

    Elf64_Shdr elf_shdrs[5] = {
        // .text
        [1] = {
            .sh_name = Elf_strtab_add(&shstrtab, ".text"),
            .sh_type = ELF_SHT_PROGBITS,
            .sh_flags = ELF_SHF_ALLOC | ELF_SHF_EXECINSTR,
            .sh_addr = 0,
            .sh_offset = text_off,
            .sh_size = text_size,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 0x10,
            .sh_entsize = 0
        },
        // .shstrtab
        [2] = {
            .sh_name = Elf_strtab_add(&shstrtab, ".shstrtab"),
            .sh_type = ELF_SHT_STRTAB,
            .sh_flags = 0,
            .sh_addr = 0,
            .sh_offset = ALIGN_UP(text_off + text_size, 16),
            .sh_size = ELF_NEED_PATCH, // Patched below.
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0
        },
        // .symtab
        [3] = {
            .sh_name = Elf_strtab_add(&shstrtab, ".symtab"),
            .sh_type = ELF_SHT_SYMTAB,
            .sh_flags = 0,
            .sh_addr = 0,
            .sh_offset = ELF_NEED_PATCH, // Patched below.
            .sh_size = ELF_NEED_PATCH, // Patched after adding all symbols to the table.
            .sh_link = 4, // Points to index (in section header table) of associated string table entry.
            .sh_info = ELF_NEED_PATCH, // Should point to index of the first global symbol.
            .sh_addralign = 0x8,
            .sh_entsize = sizeof(Elf64_Sym)
        },
        // .strtab
        [4] = {
            .sh_name = Elf_strtab_add(&shstrtab, ".strtab"),
            .sh_type = ELF_SHT_STRTAB,
            .sh_flags = 0,
            .sh_addr = 0,
            .sh_offset = ELF_NEED_PATCH, // Patch after placing .symtab (goes last)
            .sh_size = ELF_NEED_PATCH, // Patch after adding all symbol names.
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0
        }
    };

    // Path size of shstrtab in the corresponding section header table entry.
    elf_shdrs[2].sh_size = Elf_strtab_size(&shstrtab);

    // Patch location of symtab (after .shstrtab)
    elf_shdrs[3].sh_offset = ALIGN_UP(elf_shdrs[2].sh_offset + elf_shdrs[2].sh_size, 16);

    // Create string table for symbols.
    Elf_StrTable strtab = {0};
    Elf_strtab_init(&strtab, gen_mem, 16);

    // Add symbols to symtab. Try just adding global syms.
    Elf64_Sym elf_syms[2] = {
        [1] = {
            .st_name = Elf_strtab_add(&strtab, "_start"),
            .st_info = ELF_ST_INFO(ELF_STB_GLOBAL, ELF_STT_NOTYPE),
            .st_other = ELF_STV_DEFAULT,
            .st_shndx = text_shndx,
            .st_value = 0,
            .st_size = 0
        }
    };

    // Patch .symtab size in section header table.
    Elf64_Shdr* symtab_she = &elf_shdrs[3];
    symtab_she->sh_size = sizeof(elf_syms);
    symtab_she->sh_info = 1; // Point to first global sym in table.

    // Patch .strtab offset and size in section header table.
    Elf64_Shdr* strtab_she = &elf_shdrs[4];
    strtab_she->sh_offset = ALIGN_UP(symtab_she->sh_offset + symtab_she->sh_size, 16);
    strtab_she->sh_size = Elf_strtab_size(&strtab);

    u32 curr_file_off = 0;

    curr_file_off = write_bin(out_fd, &elf_hdr, sizeof(Elf64_Hdr), 0, curr_file_off);
    curr_file_off = write_bin(out_fd, elf_shdrs, sizeof(elf_shdrs), elf_hdr.e_shoff, curr_file_off);
    curr_file_off = write_bin(out_fd, text_bin, elf_shdrs[1].sh_size, elf_shdrs[1].sh_offset, curr_file_off);
    curr_file_off = write_bin(out_fd, shstrtab.bytes, elf_shdrs[2].sh_size, elf_shdrs[2].sh_offset, curr_file_off);
    curr_file_off = write_bin(out_fd, elf_syms, elf_shdrs[3].sh_size, elf_shdrs[3].sh_offset, curr_file_off);
    curr_file_off = write_bin(out_fd, strtab.bytes, elf_shdrs[4].sh_size, elf_shdrs[4].sh_offset, curr_file_off);

    fclose(out_fd);
    allocator_restore_state(gen_mem_state);
    return true;
}
