#define ELF_MAGIC0 127
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_CLASS64 2
#define ELF_DATA_2_LSB 1 // 2's complement, little-endian
#define ELF_OS_ABI_SYSV 0
#define ELF_REL_FILE 1 // Relocatable file
#define ELF_EXEC_FILE 2 // Executable file
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
#define ELF_ST_INFO(bind, type) (((bind) << 4) + ((type)&0xF))

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
    u8 e_ident[16]; // Magic number + stuff
    u16 e_type; // Object file type.
    u16 e_machine; // Architecture.
    u32 e_version; // Object file version.
    u64 e_entry; // Virtual address of entry point.
    u64 e_phoff; // File offset for the program header table.
    u64 e_shoff; // File offset for the section header table.
    u32 e_flags; // Processor-specific flags.
    u16 e_ehsize; // Size of this elf header.
    u16 e_phentsize; // Size of each program header table entry.
    u16 e_phnum; // Number of program header table entries.
    u16 e_shentsize; // Size of each section header table entry.
    u16 e_shnum; // Number of section header table entries.
    u16 e_shstrndx; // Index (in section header table) of the entry for the section header strings.
} Elf64_Hdr;

typedef struct Elf64_Shdr {
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

//
// ELF String table
//

typedef struct Elf_StrTable {
    Array(u8) buf;
} Elf_StrTable;

static void Elf_strtab_init(Elf_StrTable* table, Allocator* arena, u32 cap)
{
    table->buf = array_create(arena, u8, cap);

    array_push(table->buf, '\0');
}

static u32 Elf_strtab_add(Elf_StrTable* table, const char* str)
{
    u32 loc = (u32)array_len(table->buf);

    for (const char* p = str; *p; p += 1) {
        array_push(table->buf, *p);
    }

    array_push(table->buf, '\0');

    return loc;
}

static u32 Elf_strtab_size(const Elf_StrTable* table)
{
    return (u32)array_len(table->buf);
}

//
// ELF Symbol table
//

typedef struct Elf_SymTable {
    Elf64_Sym* syms;
    u32 num_syms;
    u32 global_start;
} Elf_SymTable;

//
// Data segment
//

typedef struct X64_DataSection {
    Array(char) buf;
    u32 align;
    HMap var_offs; // Symbol* -> size_t (offset in buf)
} X64_DataSection;

typedef struct X64_RODataSection {
    Array(char) buf;
    u32 align;
    HMap float_offs; // FloatLit* -> size_t (offset in buf)
    HMap str_offs; // StrLit* -> size_t (offset in buf)
} X64_RODataSection;

// Forward declaration.
static void X64_data_serialize(Array(char) * buf, Allocator* tmp_mem, ConstExpr* const_expr);

static inline void X64_data_fill_zeros(Array(char) * buf, size_t size)
{
    for (size_t i = 0; i < size; i += 1) {
        array_push(*buf, 0);
    }
}

static inline void X64_data_serialize_int(Array(char) * buf, Scalar imm, size_t size)
{
    u64 elem_val = imm.as_int._u64;

    // Write each byte of the value
    for (size_t i = 0; i < size; i += 1) {
        array_push(*buf, elem_val & 0xFFLL);
        elem_val = elem_val >> 8;
    }
}

static void X64_data_serialize_array_init(Array(char) * buf, Allocator* tmp_mem, ConstExpr* const_expr)
{
    AllocatorState mem_state = allocator_get_state(tmp_mem);

    Type* type = const_expr->type;
    assert(type->kind == TYPE_ARRAY);

    Type* elem_type = type->as_array.base;
    size_t num_elems = type->as_array.len;
    ConstExpr** init_vals = alloc_array(tmp_mem, ConstExpr*, num_elems, true); // Initialized to NULL

    // Iterate through initializers and overwrite appropriate elements in init_vals array with
    // the specified initializer value.
    ConstArrayInitzer* init = &const_expr->array_initzer;

    for (size_t i = 0; i < init->num_initzers; i += 1) {
        ConstArrayMemberInitzer* initzer = init->initzers + i;

        init_vals[initzer->index] = &initzer->const_expr;
    }

    // Print an initial value for each element.
    for (u64 i = 0; i < num_elems; i += 1) {
        if (init_vals[i]) {
            X64_data_serialize(buf, tmp_mem, init_vals[i]);
        }
        else {
            X64_data_fill_zeros(buf, elem_type->size);
        }
    }

    allocator_restore_state(mem_state);
}

static void X64_data_serialize_struct_init(Array(char) * buf, Allocator* tmp_mem, ConstExpr* const_expr)
{
    Type* type = const_expr->type;
    assert(type->kind == TYPE_STRUCT);

    TypeAggregateBody* type_agg = &type->as_struct.body;
    ConstExpr** field_exprs = const_expr->struct_initzer.field_exprs;

    TypeAggregateField* fields = type_agg->fields;
    size_t num_fields = type_agg->num_fields;
    size_t offset = 0; // Tracks the struct byte offset that has been initialized.

    // Init fields.
    for (size_t i = 0; i < num_fields; i++) {
        TypeAggregateField* field = fields + i;
        size_t field_size = field->type->size;
        size_t padding = field->offset - offset;

        // Fill padding with zeros.
        X64_data_fill_zeros(buf, padding);
        offset += padding;

        // Init field with specified value or zero.
        if (field_exprs[i]) {
            X64_data_serialize(buf, tmp_mem, field_exprs[i]);
        }
        else {
            X64_data_fill_zeros(buf, field_size);
        }

        offset += field_size;
    }

    // Clear padding after last field.
    X64_data_fill_zeros(buf, type->size - offset);
}

static void X64_data_serialize_union_init(Array(char) * buf, Allocator* tmp_mem, ConstExpr* const_expr)
{
    Type* type = const_expr->type;
    assert(type->kind == TYPE_UNION);

    TypeAggregateField* field = &type->as_union.body.fields[const_expr->union_initzer.field_index];
    ConstExpr* field_expr = const_expr->union_initzer.field_expr;

    if (field_expr) {
        X64_data_serialize(buf, tmp_mem, field_expr);
        X64_data_fill_zeros(buf, type->size - field->type->size);
    }
    else {
        X64_data_fill_zeros(buf, type->size);
    }
}

static void X64_data_serialize(Array(char) * buf, Allocator* tmp_mem, ConstExpr* const_expr)
{
    switch (const_expr->kind) {
    case CONST_EXPR_NONE: {
        X64_data_fill_zeros(buf, const_expr->type->size);
        break;
    }
    case CONST_EXPR_IMM: {
        X64_data_serialize_int(buf, const_expr->imm, const_expr->type->size);
        break;
    }
    case CONST_EXPR_MEM_ADDR: {
        // TODO: Need relocations for address of symbols or string literals.
        NIBBLE_FATAL_EXIT("Unhandled ConstExprKind `%d`\n", const_expr->kind);
        break;
    }
    case CONST_EXPR_STR_LIT: {
        StrLit* str_lit = const_expr->str_lit;
        size_t len = str_lit->len;
        const char* str = str_lit->str;

        for (size_t i = 0; i < len; i += 1) {
            array_push(*buf, str[i]);
        }

        array_push(*buf, '\0');
        break;
    }
    case CONST_EXPR_FLOAT_LIT: {
        FloatLit* float_lit = const_expr->float_lit;
        Scalar imm = {.as_float = float_lit->value};
        size_t size = float_kind_sizes[float_lit->kind];

        X64_data_serialize_int(buf, imm, size);
        break;
    }
    case CONST_EXPR_PROC: {
        // TODO: Need relocations for procedure addresses.
        NIBBLE_FATAL_EXIT("Unhandled ConstExprKind `%d`\n", const_expr->kind);
        break;
    }
    case CONST_EXPR_ARRAY_INIT: {
        X64_data_serialize_array_init(buf, tmp_mem, const_expr);
        break;
    }
    case CONST_EXPR_STRUCT_INIT: {
        X64_data_serialize_struct_init(buf, tmp_mem, const_expr);
        break;
    }
    case CONST_EXPR_UNION_INIT: {
        X64_data_serialize_union_init(buf, tmp_mem, const_expr);
        break;
    }
    default:
        NIBBLE_FATAL_EXIT("Unhandled ConstExprKind `%d`\n", const_expr->kind);
        break;
    }
}

static size_t X64_add_data_item(Array(char) * buf, Allocator* tmp_mem, ConstExpr* const_expr)
{
    Type* type = const_expr->type;
    size_t offset = array_len(*buf);
    size_t align_pad = ALIGN_UP(offset, type->align) - offset;

    X64_data_fill_zeros(buf, align_pad);
    X64_data_serialize(buf, tmp_mem, const_expr);

    return offset + align_pad;
}

static void X64_init_data_section(X64_DataSection* data_sec, Allocator* gen_mem, Allocator* tmp_mem, GlobalData* vars)
{
    size_t num_vars = vars->list.num_elems;

    if (!num_vars) {
        return;
    }

    // Set the initial capacity to twice the combined size of all variables to avoid reallocation
    // due to additional alignment buf.
    data_sec->buf = array_create(gen_mem, char, vars->size << 1);
    data_sec->var_offs = hmap(clp2(num_vars), gen_mem);

    // Serialize the first variable separately to get its required alignment (w/o a branch in the loop).
    {
        Symbol* sym = (Symbol*)(*bucket_list_get_elem_packed(&vars->list, 0));
        size_t offset = X64_add_data_item(&data_sec->buf, tmp_mem, &sym->as_var.const_expr);

        hmap_put(&data_sec->var_offs, PTR_UINT(sym), offset); // Record offset.

        data_sec->align = sym->type->align;
    }

    // Serialize all other variables.
    for (size_t i = 1; i < num_vars; i += 1) {
        Symbol* sym = (Symbol*)(*bucket_list_get_elem_packed(&vars->list, i));
        size_t offset = X64_add_data_item(&data_sec->buf, tmp_mem, &sym->as_var.const_expr);

        hmap_put(&data_sec->var_offs, PTR_UINT(sym), offset); // Record offset.
    }
}

static void X64_init_rodata_section(X64_RODataSection* rodata, Allocator* gen_mem, Allocator* tmp_mem, GlobalData* floats,
                                    GlobalData* strs)
{
    rodata->align = 0x10;

    size_t num_floats = floats->list.num_elems;
    size_t num_strs = strs->list.num_elems;

    if (!num_floats && !num_strs) {
        return;
    }

    rodata->buf = array_create(gen_mem, char, floats->size + strs->size);

    // Serialize all floats.
    if (num_floats) {
        rodata->float_offs = hmap(clp2(num_floats), gen_mem);

        for (size_t i = 0; i < num_floats; i += 1) {
            FloatLit* float_lit = (FloatLit*)(*bucket_list_get_elem_packed(&floats->list, i));
            Type* type = float_lit->kind == FLOAT_F64 ? builtin_types[BUILTIN_TYPE_F64].type : builtin_types[BUILTIN_TYPE_F32].type;
            ConstExpr const_expr = {.kind = CONST_EXPR_FLOAT_LIT, .type = type, .float_lit = float_lit};
            size_t offset = X64_add_data_item(&rodata->buf, tmp_mem, &const_expr);

            hmap_put(&rodata->float_offs, PTR_UINT(float_lit), offset);
        }
    }

    // Serialize all strings.
    if (num_strs) {
        rodata->str_offs = hmap(clp2(num_strs), gen_mem);

        for (size_t i = 0; i < num_strs; i += 1) {
            StrLit* str_lit = (StrLit*)(*bucket_list_get_elem_packed(&strs->list, i));
            size_t len = str_lit->len;
            const char* str = str_lit->str;
            size_t offset = array_len(rodata->buf);

            for (size_t i = 0; i < len; i += 1) {
                array_push(rodata->buf, str[i]);
            }

            array_push(rodata->buf, '\0');
            hmap_put(&rodata->str_offs, PTR_UINT(str_lit), offset);
        }
    }

    return;
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
static const u8 text_bin[] = {0x48, 0x31, 0xed, 0x8b, 0x3c, 0x24, 0x48, 0x8d, 0x74, 0x24, 0x08, 0x48, 0x8d, 0x54, 0xfc, 0x10, 0x31,
                              0xc0, 0xe8, 0x09, 0x00, 0x00, 0x00, 0x89, 0xc7, 0xb8, 0x3c, 0x00, 0x00, 0x00, 0x0f, 0x05, 0x55, 0x48,
                              0x89, 0xe5, 0x48, 0x83, 0xec, 0x10, 0xc7, 0x45, 0xfc, 0x0a, 0x00, 0x00, 0x00, 0xc7, 0x45, 0xf8, 0x01,
                              0x00, 0x00, 0x00, 0x8b, 0x45, 0xfc, 0x03, 0x45, 0xf8, 0x48, 0x89, 0xec, 0x5d, 0xc3};

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

typedef enum X64_SectionKind {
    X64_ELF_SECTION_RO_DATA,
    X64_ELF_SECTION_DATA,
    X64_ELF_SECTION_TEXT,
    X64_ELF_SECTION_SYMTAB,
    X64_ELF_SECTION_STRTAB,
    // X64_ELF_SECTION_RELA_DATA,
    // X64_ELF_SECTION_RELA_TEXT,
    X64_ELF_SECTION_SHSTRTAB,
    X64_ELF_SECTION_COUNT
} X64_SectionKind;

typedef struct X64_Section {
    u32 offset;
    X64_SectionKind kind;
    union {
        X64_RODataSection rodata;
        X64_DataSection data;
        const u8* text;
        Elf_StrTable shstrtab;
        Elf_SymTable symtab;
        Elf_StrTable strtab;
    };
} X64_Section;

typedef struct X64_ElfProg {
    u32 num_sections;
    X64_Section sections[X64_ELF_SECTION_COUNT];
    u32 sh_indices[X64_ELF_SECTION_COUNT];
    u32 sh_name_locs[X64_ELF_SECTION_COUNT];
} X64_ElfProg;

static inline const void* x64_prog_sec_data(const X64_ElfProg* prog, X64_SectionKind section_kind)
{
    switch (section_kind) {
    case X64_ELF_SECTION_RO_DATA: {
        return prog->sections[X64_ELF_SECTION_RO_DATA].rodata.buf;
    }
    case X64_ELF_SECTION_DATA: {
        return prog->sections[X64_ELF_SECTION_DATA].data.buf;
    }
    case X64_ELF_SECTION_TEXT: {
        return prog->sections[X64_ELF_SECTION_TEXT].text;
    }
    case X64_ELF_SECTION_SHSTRTAB: {
        return prog->sections[X64_ELF_SECTION_SHSTRTAB].shstrtab.buf;
    }
    case X64_ELF_SECTION_SYMTAB: {
        return prog->sections[X64_ELF_SECTION_SYMTAB].symtab.syms;
    }
    case X64_ELF_SECTION_STRTAB: {
        return prog->sections[X64_ELF_SECTION_STRTAB].strtab.buf;
    }
    default:
        NIBBLE_FATAL_EXIT("Unhandled X64_SectionKind %d", section_kind);
    }

    return NULL;
}

static inline u32 x64_prog_sec_size(const X64_ElfProg* prog, X64_SectionKind section_kind)
{
    switch (section_kind) {
    case X64_ELF_SECTION_RO_DATA: {
        return array_len(prog->sections[X64_ELF_SECTION_RO_DATA].rodata.buf);
    }
    case X64_ELF_SECTION_DATA: {
        return array_len(prog->sections[X64_ELF_SECTION_DATA].data.buf);
    }
    case X64_ELF_SECTION_TEXT: {
        return sizeof(text_bin);
    }
    case X64_ELF_SECTION_SHSTRTAB: {
        return Elf_strtab_size(&prog->sections[X64_ELF_SECTION_SHSTRTAB].shstrtab);
    }
    case X64_ELF_SECTION_SYMTAB: {
        return prog->sections[X64_ELF_SECTION_SYMTAB].symtab.num_syms * sizeof(Elf64_Sym);
    }
    case X64_ELF_SECTION_STRTAB: {
        return Elf_strtab_size(&prog->sections[X64_ELF_SECTION_STRTAB].strtab);
    }
    default:
        NIBBLE_FATAL_EXIT("Unhandled X64_SectionKind %d", section_kind);
    }

    return 0;
}

static inline u32 x64_prog_sec_align(const X64_ElfProg* prog, X64_SectionKind section_kind)
{
    switch (section_kind) {
    case X64_ELF_SECTION_RO_DATA: {
        return prog->sections[X64_ELF_SECTION_RO_DATA].rodata.align;
    }
    case X64_ELF_SECTION_DATA: {
        return prog->sections[X64_ELF_SECTION_DATA].data.align;
    }
    case X64_ELF_SECTION_TEXT: {
        return sizeof(text_bin);
    }
    case X64_ELF_SECTION_SHSTRTAB: {
        return 1;
    }
    case X64_ELF_SECTION_SYMTAB: {
        return 0x8;
    }
    case X64_ELF_SECTION_STRTAB: {
        return 1;
    }
    default:
        NIBBLE_FATAL_EXIT("Unhandled X64_SectionKind %d", section_kind);
    }

    return 0;
}

static inline u32 x64_prog_sec_offset(const X64_ElfProg* prog, X64_SectionKind section_kind)
{
    return prog->sections[section_kind].offset;
}

static inline void x64_init_elf_prog(X64_ElfProg* elf_prog, Allocator* gen_mem, bool has_rodata_sec,
                                     bool has_data_sec, bool has_rela_text_sec, bool has_rela_data_sec)
{
    // Init shstrtab
    Elf_StrTable* shstrtab = &elf_prog->sections[X64_ELF_SECTION_SHSTRTAB].shstrtab;
    Elf_strtab_init(shstrtab, gen_mem, 38);

    // .rodata?, .data?, .text, .shstrtab, .symtab, .strtab, .rela.text?, .rela.data?
    elf_prog->num_sections = 4 + has_rodata_sec + has_data_sec + has_rela_text_sec + has_rela_data_sec;

    u32 sh_idx = 1;

#define _ADD_SECTION(k, i, n)                                \
    do {                                                     \
        elf_prog->sh_indices[k] = i;                             \
        elf_prog->sh_name_locs[k] = Elf_strtab_add(shstrtab, n); \
    } while (0)

    if (has_rodata_sec) {
        _ADD_SECTION(X64_ELF_SECTION_RO_DATA, sh_idx++, ".rodata");
    }

    if (has_data_sec) {
        _ADD_SECTION(X64_ELF_SECTION_DATA, sh_idx++, ".data");
    }

    _ADD_SECTION(X64_ELF_SECTION_TEXT, sh_idx++, ".text");
    _ADD_SECTION(X64_ELF_SECTION_SHSTRTAB, sh_idx++, ".shstrtab");
    _ADD_SECTION(X64_ELF_SECTION_SYMTAB, sh_idx++, ".symtab");
    _ADD_SECTION(X64_ELF_SECTION_STRTAB, sh_idx++, ".strtab");
    //_ADD_SECTION(X64_ELF_SECTION_RELA_TEXT, sh_idx++, ".rela.text");
    //_ADD_SECTION(X64_ELF_SECTION_RELA_DATA, sh_idx++, ".rela.data");

#undef _ADD_SECTION
}

static inline u32 x64_prog_shndx(const X64_ElfProg* prog, X64_SectionKind kind)
{
    return prog->sh_indices[kind];
}

static inline u32 x64_prog_sh_name_loc(const X64_ElfProg* prog, X64_SectionKind kind)
{
    return prog->sh_name_locs[kind];
}

static inline void x64_prog_finalize_offsets(X64_ElfProg* prog, u32 init_offset)
{
    u32 offset = init_offset;

    for (u32 i = 0; i < X64_ELF_SECTION_COUNT; ++i) {
        X64_Section* section = &prog->sections[i];
        u32 index = prog->sh_indices[i];

        if (index > 0) {
            section->offset = ALIGN_UP(offset, x64_prog_sec_align(prog, (X64_SectionKind)i));
            offset = section->offset + x64_prog_sec_size(prog, (X64_SectionKind)i);
        }
    }
}

static inline void x64_prog_sec_fill_shdr(const X64_ElfProg* prog, X64_SectionKind section_kind, Elf64_Shdr* shdr)
{
    const u32 offset = x64_prog_sec_offset(prog, section_kind);
    const u32 size = x64_prog_sec_size(prog, section_kind);
    const u32 align = x64_prog_sec_align(prog, section_kind);
    const u32 name_loc = x64_prog_sh_name_loc(prog, section_kind);

    const X64_Section* section = &prog->sections[section_kind];

    switch (section_kind) {
    case X64_ELF_SECTION_RO_DATA: {
        *shdr = (Elf64_Shdr){.sh_name = name_loc,
                             .sh_type = ELF_SHT_PROGBITS,
                             .sh_flags = ELF_SHF_ALLOC,
                             .sh_addr = 0,
                             .sh_offset = offset,
                             .sh_size = size,
                             .sh_link = 0,
                             .sh_info = 0,
                             .sh_addralign = align,
                             .sh_entsize = 0};
        break;
    }
    case X64_ELF_SECTION_DATA: {
        *shdr = (Elf64_Shdr){.sh_name = name_loc,
                             .sh_type = ELF_SHT_PROGBITS,
                             .sh_flags = ELF_SHF_WRITE | ELF_SHF_ALLOC,
                             .sh_addr = 0,
                             .sh_offset = offset,
                             .sh_size = size,
                             .sh_link = 0,
                             .sh_info = 0,
                             .sh_addralign = align,
                             .sh_entsize = 0};
        break;
    }
    case X64_ELF_SECTION_TEXT: {
        *shdr = (Elf64_Shdr){.sh_name = name_loc,
                             .sh_type = ELF_SHT_PROGBITS,
                             .sh_flags = ELF_SHF_ALLOC | ELF_SHF_EXECINSTR,
                             .sh_addr = 0,
                             .sh_offset = offset,
                             .sh_size = size,
                             .sh_link = 0,
                             .sh_info = 0,
                             .sh_addralign = align,
                             .sh_entsize = 0};
        break;
    }
    case X64_ELF_SECTION_SHSTRTAB: {
        *shdr = (Elf64_Shdr){.sh_name = name_loc,
                             .sh_type = ELF_SHT_STRTAB,
                             .sh_flags = 0,
                             .sh_addr = 0,
                             .sh_offset = offset,
                             .sh_size = size,
                             .sh_link = 0,
                             .sh_info = 0,
                             .sh_addralign = align,
                             .sh_entsize = 0};
        break;
    }
    case X64_ELF_SECTION_SYMTAB: {
        const Elf_SymTable* symtab = &section->symtab;

        *shdr = (Elf64_Shdr){.sh_name = name_loc,
                             .sh_type = ELF_SHT_SYMTAB,
                             .sh_flags = 0,
                             .sh_addr = 0,
                             .sh_offset = offset,
                             .sh_size = size,
                             .sh_link = x64_prog_shndx(prog, X64_ELF_SECTION_STRTAB), // Index of associated string table entry.
                             .sh_info = symtab->global_start, // Should point to index of the first global symbol.
                             .sh_addralign = align,
                             .sh_entsize = sizeof(Elf64_Sym)};
        break;
    }
    case X64_ELF_SECTION_STRTAB: {
        *shdr = (Elf64_Shdr){.sh_name = name_loc,
                             .sh_type = ELF_SHT_STRTAB,
                             .sh_flags = 0,
                             .sh_addr = 0,
                             .sh_offset = offset,
                             .sh_size = size,
                             .sh_link = 0,
                             .sh_info = 0,
                             .sh_addralign = align,
                             .sh_entsize = 0};
        break;
    }
    default:
        NIBBLE_FATAL_EXIT("Unhandled X64_SectionKind %d", section_kind);
    }
}

static void x64_prog_fill_shdrs(const X64_ElfProg* prog, Elf64_Shdr* elf_shdrs)
{
    for (u32 i = 0; i < X64_ELF_SECTION_COUNT; ++i) {
        u32 index = prog->sh_indices[i];

        if (index > 0) {
            x64_prog_sec_fill_shdr(prog, (X64_SectionKind)i, &elf_shdrs[index]);
        }
    }
}

static void x64_prog_write_sections(const X64_ElfProg* prog, FILE* out_fd, u32 init_file_offset)
{
    u32 curr_file_off = init_file_offset;

    for (u32 i = 0; i < X64_ELF_SECTION_COUNT; ++i) {
        X64_SectionKind kind = i;
        u32 index = prog->sh_indices[kind];

        if (index > 0) {
            const void* data = x64_prog_sec_data(prog, kind);
            const u32 size = x64_prog_sec_size(prog, kind);
            const u32 offset = x64_prog_sec_offset(prog, kind);

            curr_file_off = write_bin(out_fd, data, size, offset, curr_file_off);
        }
    }
}

static bool x64_write_elf(Allocator* gen_mem, Allocator* tmp_mem, X64_ElfProg* elf_prog, const BucketList* foreign_procs,
                          const char* output_file)
{
    FILE* out_fd = fopen("elf.o", "wb");
    if (!out_fd) {
        ftprint_err("Failed to open output file `elf.o`\n");
        return false;
    }

    const u32 num_shdrs = 1 + elf_prog->num_sections; // Account for NULL section header

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
        .e_shnum = num_shdrs,
        .e_shstrndx = x64_prog_shndx(elf_prog, X64_ELF_SECTION_SHSTRTAB)
    };

    const u32 sections_init_off = elf_hdr.e_ehsize + elf_hdr.e_shnum * elf_hdr.e_shentsize;
    x64_prog_finalize_offsets(elf_prog, sections_init_off);

    Elf64_Shdr* elf_shdrs = alloc_array(gen_mem, Elf64_Shdr, num_shdrs, true);
    x64_prog_fill_shdrs(elf_prog, elf_shdrs);

    // Write elf file.
    u32 curr_file_off = 0;

    curr_file_off = write_bin(out_fd, &elf_hdr, sizeof(Elf64_Hdr), 0, curr_file_off);
    curr_file_off = write_bin(out_fd, elf_shdrs, num_shdrs * sizeof(Elf64_Shdr), elf_hdr.e_shoff, curr_file_off);

    x64_prog_write_sections(elf_prog, out_fd, curr_file_off);

    fclose(out_fd);
    return true;
}

bool x64_gen_elf(Allocator* gen_mem, Allocator* tmp_mem, GlobalData* vars, BucketList* procs, GlobalData* str_lits,
                 GlobalData* float_lits, BucketList* foreign_procs, const char* output_file)
{
    AllocatorState gen_mem_state = allocator_get_state(gen_mem);

    const bool has_rodata_sec = (str_lits->list.num_elems > 0) || (float_lits->list.num_elems > 0);
    const bool has_data_sec = vars->list.num_elems > 0;

    X64_ElfProg elf_prog = {0};
    x64_init_elf_prog(&elf_prog, gen_mem, has_rodata_sec, has_data_sec, false, false);

    // .rodata
    if (has_rodata_sec) {
        X64_init_rodata_section(&elf_prog.sections[X64_ELF_SECTION_RO_DATA].rodata, gen_mem, tmp_mem, float_lits, str_lits);
    }

    // .data
    if (has_data_sec) {
        X64_init_data_section(&elf_prog.sections[X64_ELF_SECTION_DATA].data, gen_mem, tmp_mem, vars);
    }

    // .text
    X64_Section* text = &elf_prog.sections[X64_ELF_SECTION_TEXT];
    text->text = text_bin;

    // .strtab
    X64_Section* strtab = &elf_prog.sections[X64_ELF_SECTION_STRTAB];
    Elf_strtab_init(&strtab->strtab, gen_mem, 16);

    // .symtab
    // Add symbols to symtab. Try just adding sections and global/external syms.
    const u32 num_sec_syms = 1 + has_data_sec + has_rodata_sec; // .text + .data? + .rodata?
    const u32 num_syms = 1 + num_sec_syms + 1 + foreign_procs->num_elems; // null <sections> _start <foreign>

    X64_Section* symtab = &elf_prog.sections[X64_ELF_SECTION_SYMTAB];
    symtab->symtab.num_syms = num_syms;
    symtab->symtab.syms = alloc_array(gen_mem, Elf64_Sym, symtab->symtab.num_syms, true);

    u32 sym_idx = 1;

    // .rodata symbol
    if (has_rodata_sec) {
        symtab->symtab.syms[sym_idx++] = (Elf64_Sym){.st_name = 0,
                                                     .st_info = ELF_ST_INFO(ELF_STB_LOCAL, ELF_STT_SECTION),
                                                     .st_other = ELF_STV_DEFAULT,
                                                     .st_shndx = x64_prog_shndx(&elf_prog, X64_ELF_SECTION_RO_DATA),
                                                     .st_value = 0,
                                                     .st_size = 0};
    }

    // .data symbol
    if (has_data_sec) {
        symtab->symtab.syms[sym_idx++] = (Elf64_Sym){.st_name = 0,
                                                     .st_info = ELF_ST_INFO(ELF_STB_LOCAL, ELF_STT_SECTION),
                                                     .st_other = ELF_STV_DEFAULT,
                                                     .st_shndx = x64_prog_shndx(&elf_prog, X64_ELF_SECTION_DATA),
                                                     .st_value = 0,
                                                     .st_size = 0};
    }

    // .text symbol
    symtab->symtab.syms[sym_idx++] = (Elf64_Sym){.st_name = 0,
                                                 .st_info = ELF_ST_INFO(ELF_STB_LOCAL, ELF_STT_SECTION),
                                                 .st_other = ELF_STV_DEFAULT,
                                                 .st_shndx = x64_prog_shndx(&elf_prog, X64_ELF_SECTION_TEXT),
                                                 .st_value = 0,
                                                 .st_size = 0};

    symtab->symtab.global_start = sym_idx; // First global symbol.
    symtab->symtab.syms[sym_idx++] = (Elf64_Sym){.st_name = Elf_strtab_add(&strtab->strtab, "_start"),
                                                 .st_info = ELF_ST_INFO(ELF_STB_GLOBAL, ELF_STT_NOTYPE),
                                                 .st_other = ELF_STV_DEFAULT,
                                                 .st_shndx = x64_prog_shndx(&elf_prog, X64_ELF_SECTION_TEXT),
                                                 .st_value = 0,
                                                 .st_size = 0};

    bool success = x64_write_elf(gen_mem, tmp_mem, &elf_prog, foreign_procs, output_file);

    allocator_restore_state(gen_mem_state);

    return success;
}

/*
bool x64_gen_elf(Allocator* gen_mem, Allocator* tmp_mem, GlobalData* vars, BucketList* procs, GlobalData* str_lits,
                 GlobalData* float_lits, BucketList* foreign_procs, const char* output_file)
{
    FILE* out_fd = fopen("elf.o", "wb");
    if (!out_fd) {
        ftprint_err("Failed to open output file `elf.o`\n");
        return false;
    }

    AllocatorState gen_mem_state = allocator_get_state(gen_mem);

    const bool has_data_sec = vars->list.num_elems > 0;
    const bool has_rodata_sec = (str_lits->list.num_elems > 0) || (float_lits->list.num_elems > 0);
    const bool has_rela_sec = false; // TODO: Determined by text section usage of "global" syms

    // NULL, .rodata?, .data?, .text, .shstrtab, .symtab, .strtab, .rela.text?
    const u32 num_shdrs = 5 + has_rodata_sec + has_data_sec + has_rela_sec;

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
        .e_shnum = num_shdrs,
        .e_shstrndx = ELF_NEED_PATCH // Patch to entry for shstrtab after creating section header table.
    };

    const u32 sections_off = elf_hdr.e_ehsize + elf_hdr.e_shnum * elf_hdr.e_shentsize;

    // Serialize .rodata segment.
    X64_RODataSection rodata_sec = {0};
    X64_init_rodata_section(&rodata_sec, gen_mem, tmp_mem, float_lits, str_lits);

    const u32 rodata_size = array_len(rodata_sec.buf);
    const u32 rodata_align = rodata_sec.align;
    const u32 rodata_off = has_rodata_sec ? ALIGN_UP(sections_off, rodata_align) : sections_off;
    const char* rodata_bin = rodata_sec.buf;

    // Serialize .data segment.
    X64_DataSection data_sec = {0};
    X64_init_data_section(&data_sec, gen_mem, tmp_mem, vars);

    const u32 data_size = array_len(data_sec.buf);
    const u32 data_align = data_sec.align;
    const u32 data_off = has_data_sec ? ALIGN_UP(rodata_off + rodata_size, data_align) : rodata_off;
    const char* data_bin = data_sec.buf;

    const u32 text_off = ALIGN_UP(data_off + data_size, 16);
    const u32 text_size = sizeof(text_bin);

    Elf_StrTable shstrtab = {0};
    Elf_strtab_init(&shstrtab, gen_mem, 38);

    Elf64_Shdr* elf_shdrs = alloc_array(gen_mem, Elf64_Shdr, num_shdrs, true);

    u32 she_idx = 1; // Skip null entry
    u32 rodata_shndx = 0;
    u32 data_shndx = 0;
    u32 text_shndx = 0;
    u32 shstrtab_shndx = 0;
    u32 symtab_shndx = 0;
    u32 strtab_shndx = 0;

    // .rodata section header table entry
    if (has_rodata_sec) {
        rodata_shndx = she_idx++;
        elf_shdrs[rodata_shndx] = (Elf64_Shdr){.sh_name = Elf_strtab_add(&shstrtab, ".rodata"),
                                               .sh_type = ELF_SHT_PROGBITS,
                                               .sh_flags = ELF_SHF_ALLOC,
                                               .sh_addr = 0,
                                               .sh_offset = rodata_off,
                                               .sh_size = rodata_size,
                                               .sh_link = 0,
                                               .sh_info = 0,
                                               .sh_addralign = rodata_align,
                                               .sh_entsize = 0};
    }

    // .data section header table entry
    if (has_data_sec) {
        data_shndx = she_idx++;
        elf_shdrs[data_shndx] = (Elf64_Shdr){.sh_name = Elf_strtab_add(&shstrtab, ".data"),
                                             .sh_type = ELF_SHT_PROGBITS,
                                             .sh_flags = ELF_SHF_WRITE | ELF_SHF_ALLOC,
                                             .sh_addr = 0,
                                             .sh_offset = data_off,
                                             .sh_size = data_size,
                                             .sh_link = 0,
                                             .sh_info = 0,
                                             .sh_addralign = data_align,
                                             .sh_entsize = 0};
    }

    // .text section header table entry
    text_shndx = she_idx++;
    elf_shdrs[text_shndx] = (Elf64_Shdr){.sh_name = Elf_strtab_add(&shstrtab, ".text"),
                                         .sh_type = ELF_SHT_PROGBITS,
                                         .sh_flags = ELF_SHF_ALLOC | ELF_SHF_EXECINSTR,
                                         .sh_addr = 0,
                                         .sh_offset = text_off,
                                         .sh_size = text_size,
                                         .sh_link = 0,
                                         .sh_info = 0,
                                         .sh_addralign = 0x10,
                                         .sh_entsize = 0};

    // .shstrtab section header table entry
    shstrtab_shndx = she_idx++;
    elf_shdrs[shstrtab_shndx] = (Elf64_Shdr){.sh_name = Elf_strtab_add(&shstrtab, ".shstrtab"),
                                             .sh_type = ELF_SHT_STRTAB,
                                             .sh_flags = 0,
                                             .sh_addr = 0,
                                             .sh_offset = ALIGN_UP(text_off + text_size, 16),
                                             .sh_size = ELF_NEED_PATCH, // Patched after we create all section header entries.
                                             .sh_link = 0,
                                             .sh_info = 0,
                                             .sh_addralign = 1,
                                             .sh_entsize = 0};

    // .symtab section header table entry
    symtab_shndx = she_idx++;
    elf_shdrs[symtab_shndx] =
        (Elf64_Shdr){.sh_name = Elf_strtab_add(&shstrtab, ".symtab"),
                     .sh_type = ELF_SHT_SYMTAB,
                     .sh_flags = 0,
                     .sh_addr = 0,
                     .sh_offset = ELF_NEED_PATCH, // Patched after we know location and size of .shstrtab section.
                     .sh_size = ELF_NEED_PATCH, // Patched after adding all symbols to the table.
                     .sh_link = ELF_NEED_PATCH, // Patched to index (in section header table) of associated string table entry.
                     .sh_info = ELF_NEED_PATCH, // Should point to index of the first global symbol.
                     .sh_addralign = 0x8,
                     .sh_entsize = sizeof(Elf64_Sym)};

    // .strtab
    strtab_shndx = she_idx++;
    elf_shdrs[strtab_shndx] = (Elf64_Shdr){.sh_name = Elf_strtab_add(&shstrtab, ".strtab"),
                                           .sh_type = ELF_SHT_STRTAB,
                                           .sh_flags = 0,
                                           .sh_addr = 0,
                                           .sh_offset = ELF_NEED_PATCH, // Patch after placing .symtab (goes last)
                                           .sh_size = ELF_NEED_PATCH, // Patch after adding all symbol names.
                                           .sh_link = 0,
                                           .sh_info = 0,
                                           .sh_addralign = 1,
                                           .sh_entsize = 0};

    if (has_rela_sec) {
        // TODO
        assert(0);
    }

    // Patch elf header to point to the entry for .shstrtab
    elf_hdr.e_shstrndx = shstrtab_shndx;

    // Patch size of shstrtab in the corresponding section header table entry.
    elf_shdrs[shstrtab_shndx].sh_size = Elf_strtab_size(&shstrtab);

    // Patch location of symtab (after .shstrtab)
    elf_shdrs[symtab_shndx].sh_offset = ALIGN_UP(elf_shdrs[shstrtab_shndx].sh_offset + elf_shdrs[shstrtab_shndx].sh_size, 16);

    // Patch link from symtab to strtab
    elf_shdrs[symtab_shndx].sh_link = strtab_shndx;

    // Create string table for symbols.
    Elf_StrTable strtab = {0};
    Elf_strtab_init(&strtab, gen_mem, 16);

    // Add symbols to symtab. Try just adding sections and global/external syms.
    const u32 num_sec_syms = 1 + has_data_sec + has_rodata_sec; // .text + .data? + .rodata?
    const u32 num_syms = 1 + num_sec_syms + 1 + foreign_procs->num_elems; // null <sections> _start <foreign>

    Elf64_Sym* elf_syms = alloc_array(gen_mem, Elf64_Sym, num_syms, true);

    u32 sym_idx = 1;

    // .rodata symbol
    if (has_rodata_sec) {
        elf_syms[sym_idx++] = (Elf64_Sym){.st_name = 0,
                                          .st_info = ELF_ST_INFO(ELF_STB_LOCAL, ELF_STT_SECTION),
                                          .st_other = ELF_STV_DEFAULT,
                                          .st_shndx = rodata_shndx,
                                          .st_value = 0,
                                          .st_size = 0};
    }

    // .data symbol
    if (has_data_sec) {
        elf_syms[sym_idx++] = (Elf64_Sym){.st_name = 0,
                                          .st_info = ELF_ST_INFO(ELF_STB_LOCAL, ELF_STT_SECTION),
                                          .st_other = ELF_STV_DEFAULT,
                                          .st_shndx = data_shndx,
                                          .st_value = 0,
                                          .st_size = 0};
    }

    // .text symbol
    elf_syms[sym_idx++] = (Elf64_Sym){.st_name = 0,
                                      .st_info = ELF_ST_INFO(ELF_STB_LOCAL, ELF_STT_SECTION),
                                      .st_other = ELF_STV_DEFAULT,
                                      .st_shndx = text_shndx,
                                      .st_value = 0,
                                      .st_size = 0};

    const u32 start_sym_idx = sym_idx++; // Will be the first global sym.
    elf_syms[start_sym_idx] = (Elf64_Sym){.st_name = Elf_strtab_add(&strtab, "_start"),
                                          .st_info = ELF_ST_INFO(ELF_STB_GLOBAL, ELF_STT_NOTYPE),
                                          .st_other = ELF_STV_DEFAULT,
                                          .st_shndx = text_shndx,
                                          .st_value = 0,
                                          .st_size = 0};

    // Patch .symtab size in section header table.
    Elf64_Shdr* symtab_she = &elf_shdrs[symtab_shndx];
    symtab_she->sh_size = num_syms * sizeof(Elf64_Sym);
    symtab_she->sh_info = start_sym_idx; // Point to first global sym in table.

    // Patch .strtab offset and size in section header table.
    Elf64_Shdr* strtab_she = &elf_shdrs[strtab_shndx];
    strtab_she->sh_offset = ALIGN_UP(symtab_she->sh_offset + symtab_she->sh_size, 16);
    strtab_she->sh_size = Elf_strtab_size(&strtab);

    // Write elf file.
    u32 curr_file_off = 0;

    curr_file_off = write_bin(out_fd, &elf_hdr, sizeof(Elf64_Hdr), 0, curr_file_off);
    curr_file_off = write_bin(out_fd, elf_shdrs, num_shdrs * sizeof(Elf64_Shdr), elf_hdr.e_shoff, curr_file_off);

    if (has_rodata_sec) {
        curr_file_off =
            write_bin(out_fd, rodata_bin, elf_shdrs[rodata_shndx].sh_size, elf_shdrs[rodata_shndx].sh_offset, curr_file_off);
    }

    if (has_data_sec) {
        curr_file_off = write_bin(out_fd, data_bin, elf_shdrs[data_shndx].sh_size, elf_shdrs[data_shndx].sh_offset, curr_file_off);
    }

    curr_file_off = write_bin(out_fd, text_bin, elf_shdrs[text_shndx].sh_size, elf_shdrs[text_shndx].sh_offset, curr_file_off);
    curr_file_off =
        write_bin(out_fd, shstrtab.buf, elf_shdrs[shstrtab_shndx].sh_size, elf_shdrs[shstrtab_shndx].sh_offset, curr_file_off);
    curr_file_off = write_bin(out_fd, elf_syms, elf_shdrs[symtab_shndx].sh_size, elf_shdrs[symtab_shndx].sh_offset, curr_file_off);
    curr_file_off = write_bin(out_fd, strtab.buf, elf_shdrs[strtab_shndx].sh_size, elf_shdrs[strtab_shndx].sh_offset, curr_file_off);

    fclose(out_fd);
    allocator_restore_state(gen_mem_state);
    return true;
}
*/

