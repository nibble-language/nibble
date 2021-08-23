#include "x64_gen.h"
#include "stream.h"

#define INIT_LINE_LEN 128
#define MAX_OP_BYTE_SIZE PTR_SIZE

typedef enum X64_Reg {
    X64_RAX = 0,
    X64_RCX,
    X64_RDX,
    X64_RBX,
    X64_RSP,
    X64_RBP,
    X64_RSI,
    X64_RDI,
    X64_R8,
    X64_R9,
    X64_R10,
    X64_R11,
    X64_R12,
    X64_R13,
    X64_R14,
    X64_R15,
    X64_REG_COUNT,
} X64_Reg;

static const char* x64_reg_names[MAX_OP_BYTE_SIZE + 1][X64_REG_COUNT] = {
    [1] =
        {
            [RAX] = "al",
            [RCX] = "cl",
            [RDX] = "dl",
            [RBX] = "bl",
            [RSP] = "spl",
            [RBP] = "bpl",
            [RSI] = "sil",
            [RDI] = "dil",
            [R8] = "r8b",
            [R9] = "r9b",
            [R10] = "r10b",
            [R11] = "r11b",
            [R12] = "r12b",
            [R13] = "r13b",
            [R14] = "r14b",
            [R15] = "r15b",
        },
    [2] =
        {
            [RAX] = "ax",
            [RCX] = "cx",
            [RDX] = "dx",
            [RBX] = "bx",
            [RSP] = "sp",
            [RBP] = "bp",
            [RSI] = "si",
            [RDI] = "di",
            [R8] = "r8w",
            [R9] = "r9w",
            [R10] = "r10w",
            [R11] = "r11w",
            [R12] = "r12w",
            [R13] = "r13w",
            [R14] = "r14w",
            [R15] = "r15w",
        },
    [4] =
        {
            [RAX] = "eax",
            [RCX] = "ecx",
            [RDX] = "edx",
            [RBX] = "ebx",
            [RSP] = "esp",
            [RBP] = "ebp",
            [RSI] = "esi",
            [RDI] = "edi",
            [R8] = "r8d",
            [R9] = "r9d",
            [R10] = "r10d",
            [R11] = "r11d",
            [R12] = "r12d",
            [R13] = "r13d",
            [R14] = "r14d",
            [R15] = "r15d",
        },
    [8] =
        {
            [RAX] = "rax",
            [RCX] = "rcx",
            [RDX] = "rdx",
            [RBX] = "rbx",
            [RSP] = "rsp",
            [RBP] = "rbp",
            [RSI] = "rsi",
            [RDI] = "rdi",
            [R8] = "r8",
            [R9] = "r9",
            [R10] = "r10",
            [R11] = "r11",
            [R12] = "r12",
            [R13] = "r13",
            [R14] = "r14",
            [R15] = "r15",
        },
};

// TODO: Leaf procedures should prefer to use caller-saved regs, and
// non-leaf procedures should prefer to use callee-save regs.
// SO, resolver should mark leaf procedures while resolving.
static X64_Reg x64_scratch_regs[] = {
    X64_R10, X64_R11, X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9, X64_RAX, // NOTE: Caller saved
    X64_R12, X64_R13, X64_R14, X64_R15, X64_RBX,                   // NOTE: Callee saved
};

// Bit is 1 for caller saved registers: RAX, RCX, RDX, _, _, _, RSI, RDI, R8, R9, R10, R11, _, _, _, _
static const u32 x64_caller_saved_reg_mask = 0x0FC7;

typedef struct X64_ProcState {
    Symbol* sym;

    // NOTE: Bit is 1 if corresponding reg has been used at all within procedure.
    // This is used to generate push/pop instructions to save/restore reg values
    // across procedure calls.
    u32 used_regs;

    // NOTE: Bit is 1 if corresponding instruction is a jump target.
    u64* jmp_tgts;
    u32 jmp_tgt_count;
} X64_ProcState;

typedef struct X64_Generator {
    BucketList* text_lines;
    BucketList* data_lines;

    Allocator* gen_mem;
    Allocator* tmp_mem;
} X64_Generator;

static void X64_set_reg(u32* reg_mask, X64_Reg reg)
{
    *reg_mask |= (1 << reg);
}

static void X64_unset_reg(u32* reg_mask, X64_Reg reg)
{
    *reg_mask &= ~(1 << reg);
}

static bool X64_is_reg_set(u32 reg_mask, X64_Reg reg)
{
    return reg_mask & (1 << reg);
}

static bool X64_is_reg_used(X64_Generator* generator, X64_Reg x64_reg)
{
    return X64_is_reg_set(generator->curr_proc.used_regs, x64_reg);
}

static bool X64_is_reg_caller_saved(X64_Reg x64_reg)
{
    return X64_is_reg_set(x64_caller_saved_reg_mask, x64_reg);
}

static bool X64_is_reg_callee_saved(X64_Reg x64_reg)
{
    return !X64_is_reg_set(x64_caller_saved_reg_mask, x64_reg);
}

static X64_Reg X64_get_reg(X64_Generator* generator, X64_Reg x64_reg)
{
    X64_set_reg(&generator->curr_proc.used_regs, x64_reg);

    return x64_reg;
}

static X64_Reg X64_convert_reg(X64_Generator* generator, IR_Reg ir_reg)
{
    assert(ir_reg < IR_REG_COUNT);
    assert(ir_reg < X64_REG_COUNT);

    X64_Reg x64_reg = x64_scratch_regs[ir_reg];

    X64_set_reg(&generator->curr_proc.used_regs, x64_reg);

    return x64_reg;
}

static char** X64_emit_line(BucketList* sstream, Allocator* gen_mem, Allocator* tmp_mem, const char* format, ...)
{
    char** line_ptr = NULL;

    if (format)
    {
        AllocatorState mem_state = allocator_get_state(tmp_mem);
        {
            char* tmp_line = array_create(tmp_mem, char, INIT_LINE_LEN);
            va_list vargs;

            va_start(vargs, format);
            size_t size = ftprintv_char_array(&tmp_line, true, format, vargs);
            va_end(vargs);

            line_ptr = sstream_add(sstream, gen_mem, tmp_line, size);
        }
        allocator_restore_state(mem_state);
    }
    else
    {
        line_ptr = sstream_add(sstream, gen_mem, NULL, 0);
    }

    return line_ptr;
}

static char** X64_emit_text(X64_Generator* gen, const char* format, ...)
{
    char** line = NULL;
    va_list vargs;

    va_start(vargs, format);
    line = X64_emit_line(gen->text_lines, gen->gen_mem, gen->tmp_mem, format, vargs);
    va_end(vargs);

    return line;
}

static char** X64_emit_data(X64_Generator* generator, const char* format, ...)
{
    char** line = NULL;
    va_list vargs;

    va_start(vargs, format);
    line = X64_emit_line(gen->data_lines, gen->gen_mem, gen->tmp_mem, format, vargs);
    va_end(vargs);

    return line;
}

static void X64_fill_line(X64_Generator* gen, char** line, const char* format, ...)
{

    AllocatorState mem_state = allocator_get_state(gen->tmp_mem);
    {
        char* tmp_line = array_create(gen->tmp_mem, char, INIT_LINE_LEN);
        va_list vargs;

        va_start(vargs, format);
        size_t size = ftprintv_char_array(&tmp_line, true, format, vargs);
        va_end(vargs);

        *line = mem_dup(gen->gen_mem, tmp_line, size + 1, DEFAULT_ALIGN);
    }
    allocator_restore_state(mem_state);
}

static size_t X64_assign_scope_stack_offsets(Scope* scope, size_t offset)
{
    size_t stack_size = offset;

    //
    // Sum sizes of local variables declared in this scope.
    //
    {
        List* head = &scope->sym_list;
        List* it = head->next;

        while (it != head)
        {
            Symbol* sym = list_entry(it, Symbol, lnode);

            if (sym->kind == SYMBOL_VAR)
            {
                stack_size += sym->type->size;
                stack_size = ALIGN_UP(stack_size, sym->type->align);
                sym->as_var.offset = -stack_size;
            }

            it = it->next;
        }
    }

    //
    // Recursively compute stack sizes for child scopes. Take the largest.
    //
    {
        List* head = &scope->children;
        List* it = head->next;
        size_t child_offset = stack_size;

        while (it != head)
        {
            Scope* child_scope = list_entry(it, Scope, lnode);
            size_t child_size = X64_assign_scope_stack_offsets(child_scope, child_offset);

            if (child_size > stack_size)
                stack_size = child_size;

            it = it->next;
        }
    }

    return ALIGN_UP(stack_size, 16);
}

static size_t X64_assign_proc_stack_offsets(DeclProc* dproc)
{
    //
    // Sum sizes of local variables declared in this scope.
    //

    size_t stack_size = 0;
    unsigned arg_index = 0;
    unsigned stack_arg_offset = 0x10;

    Scope* scope = dproc->scope;
    List* head = &scope->sym_list;
    List* it = head->next;

    while (it != head)
    {
        Symbol* sym = list_entry(it, Symbol, lnode);

        // Assign stack offsets to procedure params.
        if (arg_index < dproc->num_params)
        {
            assert(sym->kind == SYMBOL_VAR);

            Type* arg_type = sym->type;
            size_t arg_size = arg_type->size;
            size_t arg_align = arg_type->align;
            bool arg_in_reg = (arg_index < ARRAY_LEN(arg_regs)) && (arg_size <= MAX_OP_BYTE_SIZE);

            // Spill argument register onto the stack.
            if (arg_in_reg)
            {
                Register arg_reg = arg_regs[arg_index];

                stack_size += arg_size;
                stack_size = ALIGN_UP(stack_size, arg_align);
                sym->as_var.offset = -stack_size;

                emit_text("    mov %s [rbp + %d], %s", x64_mem_size_label[arg_size], sym->as_var.offset,
                          x64_reg_names[arg_size][arg_reg]);

                arg_index += 1;
            }
            else
            {
                sym->as_var.offset = stack_arg_offset;
                stack_arg_offset += arg_size;
                stack_arg_offset = ALIGN_UP(stack_arg_offset, arg_align);
            }
        }
        // Assign stack offsets to local variables in procedure.
        else if (sym->kind == SYMBOL_VAR)
        {
            stack_size += sym->type->size;
            stack_size = ALIGN_UP(stack_size, sym->type->align);
            sym->as_var.offset = -stack_size;
        }

        it = it->next;
    }

    //
    // Recursively compute stack sizes for child scopes. Take the largest.
    //
    {
        List* head = &scope->children;
        List* it = head->next;
        size_t child_offset = stack_size;

        while (it != head)
        {
            Scope* child_scope = list_entry(it, Scope, lnode);
            size_t child_size = X64_assign_scope_stack_offsets(child_scope, child_offset);

            if (child_size > stack_size)
                stack_size = child_size;

            it = it->next;
        }
    }

    return ALIGN_UP(stack_size, 16);
}

static char* X64_print_imm(X64_Generator* generator, Scalar imm, Type* type)
{
    char* dstr = array_create(generator->tmp_mem, char, 8);

    if (type->kind == TYPE_INTEGER)
    {
        switch (type->as_integer.kind)
        {
            case INTEGER_U8:
                ftprint_char_array(&dstr, false, "%u", imm.as_int._u8);
                break;
            case INTEGER_S8:
                ftprint_char_array(&dstr, false, "%d", imm.as_int._s8);
                break;
            case INTEGER_U16:
                ftprint_char_array(&dstr, false, "%u", imm.as_int._u16);
                break;
            case INTEGER_S16:
                ftprint_char_array(&dstr, false, "%d", imm.as_int._s16);
                break;
            case INTEGER_U32:
                ftprint_char_array(&dstr, false, "%u", imm.as_int._u32);
                break;
            case INTEGER_S32:
                ftprint_char_array(&dstr, false, "%d", imm.as_int._s32);
                break;
            case INTEGER_U64:
                ftprint_char_array(&dstr, false, "%lu", imm.as_int._u64);
                break;
            case INTEGER_S64:
                ftprint_char_array(&dstr, false, "%ld", imm.as_int._s64);
                break;
            default:
                assert(0);
                break;
        }
    }
    else if (type->kind == TYPE_PTR)
    {
        ftprint_char_array(&dstr, false, "%lu", imm.as_int._u64);
    }
    else if (type->kind == TYPE_FLOAT)
    {
        assert(0);
    }
    else
    {
        assert(0);
    }

    array_push(dstr, '\0');

    return dstr;
}

static const char* X64_print_reg(X64_Generator* generator, IR_Reg ir_reg, Type* type)
{
    size_t size = type->size;

    X64_Reg x64_reg = X64_convert_reg(generator, ir_reg);

    return x64_reg_names[size][x64_reg];
}

static char* X64_print_mem(X64_Generator* generator, IR_MemAddr* addr, Type* type)
{
    char* dstr = array_create(generator->tmp_mem, char, 16);
    size_t size = type->size;
    const char* mem_label = x64_mem_size_label[size];
    bool has_base = addr->base_kind != IR_MEM_BASE_NONE;
    bool has_index = addr->scale && (addr->index_reg < IR_REG_COUNT);

    assert(has_base || has_index);

    if (has_base)
    {
        X64_Reg base_reg;
        s32 disp = 0;

        if (addr->base_kind == IR_MEM_BASE_SYM)
        {
            Symbol* sym = addr->base.sym;

            // Early exit for global variable addresses.
            if (!sym->is_local)
            {
                ftprint_char_array(&dstr, true, "%s [rel %s]", mem_label, sym->name);
                return dstr;
            }

            disp += addr->base.sym->offset;
            base_reg = X64_RBP;
        }
        else
        {
            base_reg = X64_convert_reg(generator, addr->base.reg);
        }

        bool has_disp = disp != 0;
        const char* base_reg_name = x64_reg_names[MAX_OP_BYTE_SIZE][base_reg];

        if (has_index)
        {
            const char* index_reg_name = X64_print_reg(arena, addr->index_reg);

            if (has_disp)
                ftprint_char_array(&dstr, false, "%s [%s + %d*%s + %d]", mem_label, base_reg_name, addr->scale, index_reg_name,
                                   (s32)addr->disp);
            else
                ftprint_char_array(&dstr, false, "%s [%s + %d*%s]", mem_label, base_reg_name, addr->scale, index_reg_name);
        }
        else
        {
            if (has_disp)
                ftprint_char_array(&dstr, false, "%s [%s + %d]", mem_label, base_reg_name, (s32)addr->disp);
            else
                ftprint_char_array(&dstr, false, "%s [%s]", mem_label, base_reg_name);
        }
    }
    else
    {
        const char* index_reg_name = X64_print_reg(arena, addr->index_reg);

        if (addr->disp)
            ftprint_char_array(&dstr, false, "%s [%d*%s + %d]", mem_label, addr->scale, index_reg_name, (s32)addr->disp);
        else
            ftprint_char_array(&dstr, false, "%s [%d*%s]", mem_label, addr->scale, index_reg_name);
    }

    array_push(dstr, '\0');
}

static char* X64_print_rm(X64_Generator* generator, IR_OpRM* rm, Type* type)
{
    switch (rm.kind)
    {
        case IR_OP_REG:
            return X64_print_reg(generator, rm->reg, type);
        case IR_OP_MEM:
            return X64_print_mem(generator, rm->mem, type);
        default:
            return NULL;
    }
}

static char* X64_print_rmi(X64_Generator* generator, IR_OpRMI* rmi, Type* type)
{
    switch (rm.kind)
    {
        case IR_OP_REG:
            return X64_print_reg(generator, rmi->reg, type);
        case IR_OP_MEM:
            return X64_print_mem(generator, rmi->mem, type);
        case IR_OP_IMM:
            return X64_print_imm(generator, rmi->imm, type);
        default:
            return NULL;
    }
}

static char* X64_print_ri(X64_Generator* generator, IR_OpRI* ri, Type* type)
{
    switch (rm.kind)
    {
        case IR_OP_REG:
            return X64_print_reg(generator, rmi->reg, type);
        case IR_OP_IMM:
            return X64_print_imm(generator, rmi->imm, type);
        default:
            return NULL;
    }
}

static void X64_gen_instr(X64_Generator* generator, u32 instr_index, IR_Instr* instr)
{
    AllocatorState mem_state = allocator_get_state(generator->tmp_mem);

    switch (instr->kind)
    {
        case IR_INSTR_ADD:
        {
            X64_emit_text(generator, "    add %s, %s",
                          X64_print_rm(generator, &instr->_add.dst, instr->_add.type),
                          X64_print_rmi(generator, &instr->_add.src, instr->_add.type));
            break;
        }
        case IR_INSTR_SUB:
        {
            X64_emit_text(generator, "    sub %s, %s",
                          X64_print_rm(generator, &instr->_sub.dst, instr->_sub.type),
                          X64_print_rmi(generator, &instr->_sub.src, instr->_sub.type));
            break;
        }
        case IR_INSTR_SHR:
        {
            X64_emit_text(generator, "    shr %s, %s",
                          X64_print_rm(generator, &instr->_shr.dst, instr->_shr.type),
                          X64_print_rmi(generator, &instr->_shr.src, instr->_shr.type));
            break;
        }
        case IR_INSTR_SAR:
        {
            X64_emit_text(generator, "    sar %s, %s",
                          X64_print_rm(generator, &instr->_sar.dst, instr->_sar.type),
                          X64_print_rmi(generator, &instr->_sar.src, instr->_sar.type));
            break;
        }
        case IR_INSTR_NEG:
        {
            X64_emit_text(generator, "    neg %s", X64_print_rm(generator, &instr->_neg.dst, instr->_neg.type));
            break;
        }
        case IR_INSTR_NOT:
        {
            X64_emit_text(generator, "    not %s", X64_print_rm(generator, &instr->_not.dst, instr->_not.type));
            break;
        }
        case IR_INSTR_MOV:
        {

            X64_emit_text(generator, "    mov %s, %s",
                          X64_print_reg(generator, instr->_mov.dst, instr->_mov.type),
                          X64_print_ri(generator, instr->_mov.src, instr->_mov.type));
            break;
        }
        case IR_INSTR_TRUNC:
        {
            X64_emit_text(generator, "    mov %s, %s",
                          X64_print_reg(generator, instr->_trunc.dst, instr->_trunc.dst_type),
                          X64_print_rm(generator, instr->_trunc.src, instr->_trunc.src_type));
            break;
        }
        case IR_INSTR_ZEXT:
        {
            X64_emit_text(generator, "    movzx %s, %s",
                          X64_print_reg(generator, instr->_zext.dst, instr->_zext.dst_type),
                          X64_print_rm(generator, instr->_zext.src, instr->_zext.src_type));
            break;
        }
        case IR_INSTR_SEXT:
        {
            X64_emit_text(generator, "    movsx %s, %s",
                          X64_print_reg(generator, instr->_sext.dst, instr->_sext.dst_type),
                          X64_print_rm(generator, instr->_sext.src, instr->_sext.src_type));
            break;
        }
        case IR_INSTR_STORE:
        {

            X64_emit_text(generator, "    mov %s, %s",
                          X64_print_mem(generator, instr->_store.dst, instr->_store.type),
                          X64_print_ri(generator, instr->_store.src, instr->_store.type));
            break;
        }
        case IR_INSTR_LOAD:
        {

            X64_emit_text(generator, "    mov %s, %s",
                          X64_print_reg(generator, instr->_load.dst, instr->_load.type),
                          X64_print_mem(generator, instr->_load.src, instr->_load.type));
            break;
        }
        case IR_INSTR_LADDR:
        {

            X64_emit_text(generator, "    lea %s, %s",
                          X64_print_reg(generator, instr->_laddr.dst, type_ptr_void),
                          X64_print_mem(generator, instr->_laddr.mem, type_ptr_void));
            break;
        }
        case IR_INSTR_RET:
        {
            Type* ret_type = instr->_ret.type;
            X64_Reg x64_reg = X64_convert_reg(generator, instr->_ret.src);

            if (x64_reg != X64_RAX)
            {
                // TODO: Dont know if spilling is necessary!
                X64_emit_text(generator, "    mov %s, %s",
                              x64_reg_names[ret_type->size][X64_RAX],
                              x64_reg_names[ret_type->size][x64_reg]);

                X64_get_reg(generator, X64_RAX);
            }

            X64_emit_text(generator, "    jmp end.%s", generator->curr_proc.sym->name);
            break;
        }
    }

    allocator_restore_state(mem_state);
}

static void X64_gen_proc(X64_Generator* generator, Symbol* sym)
{
    generator->curr_proc.sym = sym;
    generator->curr_proc.used_regs = 0;

    X64_emit_text(generator, "");
    X64_emit_text(generator, "SECTION .text");
    X64_emit_text(generator, "global %s", sym->name);
    X64_emit_text(generator, "%s:", sym->name);

    X64_emit_text(generator, "    push rbp");
    X64_emit_text(generator, "    mov rbp, rsp");

    char** save_regs_inst = X64_emit_text(NULL);
    char** sub_rsp_inst = X64_emit_text(NULL);

    u32 stack_size = X64_assign_proc_stack_offsets((DeclProc*)sym->decl); // NOTE: Spills argument registers.

    if (stack_size) X64_fill_line(generator, sub_rsp_inst, "    sub rsp, %u", stack_size);

    // Generate instructions.
    IR_Instr** instrs = sym->as_proc.instrs;
    size_t num_instrs = array_len(instrs);

    AllocatorState mem_state = allocator_get_state(generator->tmp_mem);
    {
        generator->curr_proc.jump_tgt_count = (num_instrs + 63) / 64;
        generator->curr_proc.jump_tgts = alloc_array(generator->tmp_mem, u64, generator->curr_proc.jump_tgt_count, true);

        for (size_t i = 0; i < num_instrs; i += 1)
        {
            X64_gen_instr(generator, i, instrs[i]);
        }
    }
    allocator_restore_state(mem_state);

    // End label
    X64_emit_text(generator, "    end.%s:", sym->name);

    // Save/Restore callee-saved registers.
    AllocatorState mem_state = allocator_get_state(generator->tmp_mem);
    {
        char* tmp_line = array_create(generator->tmp_mem, char, INIT_LINE_LEN);
        u32 used_regs = generator->curr_proc.used_regs;

        for (uint32_t r = 0; r < X64_REG_COUNT; r += 1)
        {
            X64_Reg reg = (X64_Reg)r;
            bool is_used = X64_is_reg_set(used_regs, reg);
            bool is_callee_saved = !(x64_caller_saved_reg_mask & (1 << reg));

            if (is_used && is_callee_saved)
            {
                ftprint_char_array(&tmp_line, false, "    push %s\n", reg_names[8][reg]);
                X64_emit_text(generator, "    pop %s", reg_names[8][reg]);
            }
        }

        array_push(tmp_line, '\0');

        *save_regs_inst = mem_dup(generator->gen_mem, tmp_line, array_len(tmp_line), DEFAULT_ALIGN);
    }
    allocator_restore_state(mem_state);

    if (stack_size) X64_emit_text(generator, "    mov rsp, rbp");

    X64_emit_text(generator, "    pop rbp");
    X64_emit_text(generator, "    ret");

}

bool x64_gen_module(Allocator* gen_mem, Allocator* tmp_mem, IR_Module* module, const char* output_file)
{
    FILE* out_fd = fopen(output_file, "w");
    if (!out_fd)
    {
        ftprint_err("Failed to open output file `%s`\n", output_file);
        return false;
    }

    X64_Generator generator = {
        .gen_mem = gen_mem,
        .tmp_mem = tmp_mem,
        .text_lines = new_bucket_list(gen_mem, 256),
        .data_lines = new_bucket_list(gen_mem, 256),
    };

    // Generate global variables.
    X64_emit_data(&generator, "SECTION .data\n");

    for (u32 i = 0; i < num_vars; i += 1)
    {
        Symbol* sym = module->vars[i];
        DeclVar* dvar = (DeclVar*)sym->decl;

        X64_emit_data(&generator, "ALIGN %d", sym->type->align);
        X64_emit_data(&generator, "%s: ", sym->name);

        if (dvar->init)
        {
            X64_emit_data_value(&generator, sym->type, dvar->init->const_val);
        }
        else
        {
            Scalar zero_val = {0};
            X64_emit_data_value(&generator, sym->type, zero_val);
        }
    }

    // Generate instructions for each procedure.
    for (u32 i = 0; i < num_procs; i += 1)
    {
        X64_gen_proc(&generator, module->procs[i]);
    }
}
