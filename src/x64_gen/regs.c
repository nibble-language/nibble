#include "cstring.h"
#include "x64_gen/regs.h"
#include "x64_gen/startup_linux.c"
#include "x64_gen/startup_windows.c"

// Linux System V ABI
static X64_Reg x64_linux_leaf_scratch_int_regs[] = {
    X64_R10, X64_R11, X64_RAX, X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9, // NOTE: Caller saved
    X64_R12, X64_R13, X64_R14, X64_R15, X64_RBX, // NOTE: Callee saved
};
static X64_Reg x64_linux_leaf_scratch_flt_regs[] = {
    X64_XMM0, X64_XMM1, X64_XMM2, X64_XMM3, X64_XMM4, X64_XMM5, X64_XMM6, X64_XMM7, X64_XMM8, X64_XMM9, // NOTE: FP Caller-saved
    X64_XMM10, X64_XMM11, X64_XMM12, X64_XMM13, X64_XMM14, X64_XMM15, // NOTE: FP Caller-saved
};
static X64_ScratchRegs x64_linux_leaf_scratch_regs[X64_REG_CLASS_COUNT] = {
    [X64_REG_CLASS_INT] = {
        .num_regs = ARRAY_LEN(x64_linux_leaf_scratch_int_regs),
        .regs = x64_linux_leaf_scratch_int_regs
    },
    [X64_REG_CLASS_FLOAT] = {
        .num_regs = ARRAY_LEN(x64_linux_leaf_scratch_flt_regs),
        .regs = x64_linux_leaf_scratch_flt_regs
    }
};

static X64_Reg x64_linux_nonleaf_scratch_int_regs[] = {
    X64_R12, X64_R13, X64_R14, X64_R15, X64_RBX, // NOTE: Callee saved
    X64_R10, X64_R11, X64_RAX, X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9, // NOTE: Caller saved
};
static X64_Reg x64_linux_nonleaf_scratch_flt_regs[] = {
    X64_XMM10, X64_XMM11, X64_XMM12, X64_XMM13, X64_XMM14, X64_XMM15, // NOTE: FP Caller-saved
    X64_XMM0, X64_XMM1, X64_XMM2, X64_XMM3, X64_XMM4, X64_XMM5, X64_XMM6, X64_XMM7, X64_XMM8, X64_XMM9, // NOTE: FP Caller-saved
};
static X64_ScratchRegs x64_linux_nonleaf_scratch_regs[X64_REG_CLASS_COUNT] = {
    [X64_REG_CLASS_INT] = {
        .num_regs = ARRAY_LEN(x64_linux_nonleaf_scratch_int_regs),
        .regs = x64_linux_nonleaf_scratch_int_regs
    },
    [X64_REG_CLASS_FLOAT] = {
        .num_regs = ARRAY_LEN(x64_linux_nonleaf_scratch_flt_regs),
        .regs = x64_linux_nonleaf_scratch_flt_regs
    }
};

static X64_Reg x64_linux_arg_regs[] = {X64_RDI, X64_RSI, X64_RDX, X64_RCX, X64_R8, X64_R9};

// Bit is 1 for caller saved registers: RAX, RCX, RDX, _, _, _, RSI, RDI, R8, R9, R10, R11, _, _, _, _, XMM0, ..., XMM15
static const u32 x64_linux_caller_saved_reg_mask = 0xFFFF0FC7;

// Bit is 1 for arg registers: _, RCX, RDX, _, _, _, RSI, RDI, R8, R9, _, _, _, _, _, _
static const u32 x64_linux_arg_reg_mask = 0x03C6;

// Windows ABI
static X64_Reg x64_windows_leaf_scratch_int_regs[] = {
    X64_R10, X64_R11, X64_RAX, X64_RCX, X64_RDX, X64_R8,  X64_R9, // NOTE: Caller saved
    X64_R12, X64_R13, X64_R14, X64_R15, X64_RBX, X64_RSI, X64_RDI, // NOTE: Callee saved
};
static X64_Reg x64_windows_leaf_scratch_flt_regs[] = {
    X64_XMM0, X64_XMM1, X64_XMM2, X64_XMM3, X64_XMM4, X64_XMM5, // NOTE: FP Caller-saved
    X64_XMM6, X64_XMM7, X64_XMM8, X64_XMM9, // NOTE: FP Callee-saved
};
static X64_ScratchRegs x64_windows_leaf_scratch_regs[X64_REG_CLASS_COUNT] = {
    [X64_REG_CLASS_INT] = {
        .num_regs = ARRAY_LEN(x64_windows_leaf_scratch_int_regs),
        .regs = x64_windows_leaf_scratch_int_regs
    },
    [X64_REG_CLASS_FLOAT] = {
        .num_regs = ARRAY_LEN(x64_windows_leaf_scratch_flt_regs),
        .regs = x64_windows_leaf_scratch_flt_regs
    }
};

static X64_Reg x64_windows_nonleaf_scratch_int_regs[] = {
    X64_R12, X64_R13, X64_R14, X64_R15, X64_RBX, X64_RSI, X64_RDI, // NOTE: Callee saved
    X64_R10, X64_R11, X64_RAX, X64_RCX, X64_RDX, X64_R8,  X64_R9, // NOTE: Caller saved
};
static X64_Reg x64_windows_nonleaf_scratch_flt_regs[] = {
    X64_XMM6, X64_XMM7, X64_XMM8, X64_XMM9, // NOTE: FP Callee-saved
    X64_XMM0, X64_XMM1, X64_XMM2, X64_XMM3, X64_XMM4, X64_XMM5, // NOTE: FP Caller-saved
};
static X64_ScratchRegs x64_windows_nonleaf_scratch_regs[X64_REG_CLASS_COUNT] = {
    [X64_REG_CLASS_INT] = {
        .num_regs = ARRAY_LEN(x64_windows_nonleaf_scratch_int_regs),
        .regs = x64_windows_nonleaf_scratch_int_regs
    },
    [X64_REG_CLASS_FLOAT] = {
        .num_regs = ARRAY_LEN(x64_windows_nonleaf_scratch_flt_regs),
        .regs = x64_windows_nonleaf_scratch_flt_regs
    }
};

static X64_Reg x64_windows_arg_regs[] = {X64_RCX, X64_RDX, X64_R8, X64_R9};

// RAX, RCX, RDX, _, _, _, _, _, R8, R9, R10, R11, _, _, _, _,
// XMM0 XMM1 XMM2 XMM3, XMM4 XMM5
static const u32 x64_windows_caller_saved_reg_mask = 0x003F0F07;

// _, RCX, RDX, _, _, _, _, _, R8, R9, _, _, _, _, _, _
static const u32 x64_windows_arg_reg_mask = 0x0306;

X64_Target x64_target;

const X64_RegClass x64_reg_classes[X64_REG_COUNT] = {
    [X64_RAX] = X64_REG_CLASS_INT,     [X64_RCX] = X64_REG_CLASS_INT,     [X64_RDX] = X64_REG_CLASS_INT,
    [X64_RBX] = X64_REG_CLASS_INT,     [X64_RSP] = X64_REG_CLASS_INT,     [X64_RBP] = X64_REG_CLASS_INT,
    [X64_RSI] = X64_REG_CLASS_INT,     [X64_RDI] = X64_REG_CLASS_INT,     [X64_R8] = X64_REG_CLASS_INT,
    [X64_R9] = X64_REG_CLASS_INT,      [X64_R10] = X64_REG_CLASS_INT,     [X64_R11] = X64_REG_CLASS_INT,
    [X64_R12] = X64_REG_CLASS_INT,     [X64_R13] = X64_REG_CLASS_INT,     [X64_R14] = X64_REG_CLASS_INT,
    [X64_R15] = X64_REG_CLASS_INT,     [X64_XMM0] = X64_REG_CLASS_FLOAT,  [X64_XMM1] = X64_REG_CLASS_FLOAT,
    [X64_XMM2] = X64_REG_CLASS_FLOAT,  [X64_XMM3] = X64_REG_CLASS_FLOAT,  [X64_XMM4] = X64_REG_CLASS_FLOAT,
    [X64_XMM5] = X64_REG_CLASS_FLOAT,  [X64_XMM6] = X64_REG_CLASS_FLOAT,  [X64_XMM7] = X64_REG_CLASS_FLOAT,
    [X64_XMM8] = X64_REG_CLASS_FLOAT,  [X64_XMM9] = X64_REG_CLASS_FLOAT,  [X64_XMM10] = X64_REG_CLASS_FLOAT,
    [X64_XMM11] = X64_REG_CLASS_FLOAT, [X64_XMM12] = X64_REG_CLASS_FLOAT, [X64_XMM13] = X64_REG_CLASS_FLOAT,
    [X64_XMM14] = X64_REG_CLASS_FLOAT, [X64_XMM15] = X64_REG_CLASS_FLOAT,
};

const char* x64_flt_reg_names[X64_REG_COUNT] = {
    [X64_XMM0] = "xmm0",
    [X64_XMM1] = "xmm1",
    [X64_XMM2] = "xmm2",
    [X64_XMM3] = "xmm3",
    [X64_XMM4] = "xmm4",
    [X64_XMM5] = "xmm5",
    [X64_XMM6] = "xmm6",
    [X64_XMM7] = "xmm7",
    [X64_XMM8] = "xmm8",
    [X64_XMM9] = "xmm9",
    [X64_XMM10] = "xmm10",
    [X64_XMM11] = "xmm11",
    [X64_XMM12] = "xmm12",
    [X64_XMM13] = "xmm13",
    [X64_XMM14] = "xmm14",
    [X64_XMM15] = "xmm15"
};
const char* x64_mem_size_label[X64_MAX_INT_REG_SIZE + 1] = {[1] = "byte", [2] = "word", [4] = "dword", [8] = "qword"};
const char* x64_data_size_label[X64_MAX_INT_REG_SIZE + 1] = {[1] = "db", [2] = "dw", [4] = "dd", [8] = "dq"};

const char* x64_condition_codes[] = {
    [COND_U_LT] = "b", [COND_S_LT] = "l",    [COND_U_LTEQ] = "be", [COND_S_LTEQ] = "le", [COND_U_GT] = "a",
    [COND_S_GT] = "g", [COND_U_GTEQ] = "ae", [COND_S_GTEQ] = "ge", [COND_EQ] = "e",      [COND_NEQ] = "ne",
};

const char* x64_sext_ax_into_dx[X64_MAX_INT_REG_SIZE + 1] = {[2] = "cwd", [4] = "cdq", [8] = "cqo"};

bool init_x64_target(OS target_os)
{
    x64_target.os = target_os;

    // RAX, RCX, RDX, RBX, _, _, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15
    x64_target.scratch_reg_mask = 0xFFCF; // TODO: Not sync'd with actual scratch register arrays.

    switch (target_os) {
    case OS_LINUX:
        x64_target.num_arg_regs = ARRAY_LEN(x64_linux_arg_regs);
        x64_target.arg_regs = x64_linux_arg_regs;

        x64_target.leaf_scratch_regs = &x64_linux_leaf_scratch_regs;
        x64_target.nonleaf_scratch_regs = &x64_linux_nonleaf_scratch_regs;

        x64_target.caller_saved_reg_mask = x64_linux_caller_saved_reg_mask;
        x64_target.arg_reg_mask = x64_linux_arg_reg_mask;

        x64_target.startup_code = x64_linux_startup_code;
        return true;
    case OS_WIN32:
        x64_target.num_arg_regs = ARRAY_LEN(x64_windows_arg_regs);
        x64_target.arg_regs = x64_windows_arg_regs;

        x64_target.leaf_scratch_regs = &x64_windows_leaf_scratch_regs;
        x64_target.nonleaf_scratch_regs = &x64_windows_nonleaf_scratch_regs;

        x64_target.caller_saved_reg_mask = x64_windows_caller_saved_reg_mask;
        x64_target.arg_reg_mask = x64_windows_arg_reg_mask;

        x64_target.startup_code = x64_windows_startup_code;
        return true;
    default:
        return false;
    }
}

bool X64_is_caller_saved_reg(X64_Reg reg)
{
    return u32_is_bit_set(x64_target.caller_saved_reg_mask, reg);
}

bool X64_is_callee_saved_reg(X64_Reg reg)
{
    return !u32_is_bit_set(x64_target.caller_saved_reg_mask, reg);
}

bool X64_is_arg_reg(X64_Reg reg)
{
    return u32_is_bit_set(x64_target.arg_reg_mask, reg);
}

bool X64_is_obj_retarg_large(size_t size)
{
    if (x64_target.os == OS_LINUX) {
        return X64_linux_is_obj_retarg_large(size);
    }

    assert(x64_target.os == OS_WIN32);

    return X64_windows_is_obj_retarg_large(size);
}
