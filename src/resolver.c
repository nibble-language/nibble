#include "resolver.h"
#include "parser.h"

typedef struct ExprOperand {
    Type* type;
    bool is_const;
    bool is_lvalue;
    Scalar const_val;
} ExprOperand;

static Symbol* resolve_name(Resolver* resolver, const char* name);
static bool resolve_symbol(Resolver* resolver, Symbol* sym);
static bool resolve_decl_var(Resolver* resolver, Symbol* sym);
static bool resolve_decl_const(Resolver* resolver, Symbol* sym);
static bool resolve_decl_proc(Resolver* resolver, Symbol* sym);
static bool resolve_global_proc_body(Resolver* resolver, Symbol* sym);
static bool resolve_proc_stmts(Resolver* resolver, Symbol* sym);

static bool cast_eop(ExprOperand* eop, Type* type);
static bool convert_eop(ExprOperand* eop, Type* dst_type);
static bool eop_is_null_ptr(ExprOperand eop);
static bool can_convert_eop(ExprOperand* operand, Type* dst_type);
static bool can_cast_eop(ExprOperand* eop, Type* dst_type);
static void eop_decay(Resolver* resolver, ExprOperand* eop);
static Expr* try_wrap_cast_expr(Resolver* resolver, ExprOperand* eop, Expr* orig_expr);

static bool resolve_expr(Resolver* resolver, Expr* expr);
static bool resolve_expr_int(Resolver* resolver, Expr* expr);
static bool resolve_expr_binary(Resolver* resolver, Expr* expr);
static bool resolve_expr_call(Resolver* resolver, Expr* expr);
static bool resolve_expr_ident(Resolver* resolver, Expr* expr);
static bool resolve_cond_expr(Resolver* resolver, Expr* expr);

static Type* resolve_typespec(Resolver* resolver, TypeSpec* typespec);

enum ResolveStmtRetFlags {
    RESOLVE_STMT_SUCCESS = 0x1,
    RESOLVE_STMT_RETURNS = 0x2,
};

enum ResolveStmtInFlags {
    RESOLVE_STMT_BREAK_ALLOWED = 0x1,
    RESOLVE_STMT_CONTINUE_ALLOWED = 0x2,
};

static unsigned resolve_stmt(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags);
static unsigned resolve_stmt_block(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags);
static unsigned resolve_stmt_block_body(Resolver* resolver, List* stmts, Type* ret_type, unsigned flags);
static unsigned resolve_stmt_while(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags);
static unsigned resolve_stmt_if(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags);
static unsigned resolve_cond_block(Resolver* resolver, IfCondBlock* cblock, Type* ret_type, unsigned flags);
static unsigned resolve_stmt_expr_assign(Resolver* resolver, Stmt* stmt);

static void set_scope(Resolver* resolver, Scope* scope);
static Scope* push_scope(Resolver* resolver, size_t num_syms);
static void pop_scope(Resolver* resolver);
static void add_scope_symbol(Scope* scope, Symbol* sym);

static void init_builtin_syms(Resolver* resolver);
static bool add_global_type_symbol(Resolver* resolver, const char* name, Type* type);
static Symbol* add_unresolved_symbol(Resolver* resolver, Scope* scope, SymbolKind kind, const char* name, Decl* decl);

static void resolver_on_error(Resolver* resolver, const char* format, ...)
{
    char buf[MAX_ERROR_LEN];
    size_t size = 0;
    va_list vargs;

    va_start(vargs, format);
    size = vsnprintf(buf, MAX_ERROR_LEN, format, vargs) + 1;
    va_end(vargs);

    add_byte_stream_chunk(resolver->errors, buf, size > sizeof(buf) ? sizeof(buf) : size);
}

static void add_scope_symbol(Scope* scope, Symbol* sym)
{
    hmap_put(&scope->sym_table, PTR_UINT(sym->name), PTR_UINT(sym));
    list_add_last(&scope->sym_list, &sym->lnode);
}

static void fill_decl_symbol_info(Decl* decl, SymbolKind* kind, const char** name)
{
    switch (decl->kind)
    {
        case CST_DeclVar:
        {
            DeclVar* dvar = (DeclVar*)decl;

            *kind = SYMBOL_VAR;
            *name = dvar->name; // TODO: Multiple variables per cst decl
            break;
        }
        case CST_DeclConst:
        {
            DeclConst* dconst = (DeclConst*)decl;

            *kind = SYMBOL_CONST;
            *name = dconst->name;
            break;
        }
        case CST_DeclProc:
        {
            DeclProc* dproc = (DeclProc*)decl;

            *kind = SYMBOL_PROC;
            *name = dproc->name;
            break;
        }
        case CST_DeclEnum:
        {
            DeclEnum* denum = (DeclEnum*)decl;

            *kind = SYMBOL_TYPE;
            *name = denum->name;
            break;
        }
        case CST_DeclUnion:
        {
            DeclUnion* dunion = (DeclUnion*)decl;

            *kind = SYMBOL_TYPE;
            *name = dunion->name;
            break;
        }
        case CST_DeclStruct:
        {
            DeclStruct* dstruct = (DeclStruct*)decl;

            *kind = SYMBOL_TYPE;
            *name = dstruct->name;
            break;
        }
        case CST_DeclTypedef:
        {
            DeclTypedef* dtypedef = (DeclTypedef*)decl;

            *kind = SYMBOL_TYPE;
            *name = dtypedef->name;
            break;
        }
        default:
            *kind = SYMBOL_NONE;
            *name = NULL;
            ftprint_err("Not handling Decl kind %d in file %s, line %d\n", decl->kind, __FILE__, __LINE__);
            assert(0);
            break;
    }
}

static bool add_global_type_symbol(Resolver* resolver, const char* name, Type* type)
{
    const char* sym_name = intern_ident(name, cstr_len(name), NULL, NULL);

    if (lookup_scope_symbol(resolver->global_scope, sym_name))
    {
        resolver_on_error(resolver, "Duplicate definition of `%s`", sym_name);
        return false;
    }

    Symbol* sym = new_symbol_builtin_type(resolver->ast_mem, sym_name, type);

    add_scope_symbol(resolver->global_scope, sym);

    return true;
}

static Symbol* add_unresolved_symbol(Resolver* resolver, Scope* scope, SymbolKind kind, const char* name, Decl* decl)
{
    if (lookup_scope_symbol(scope, name))
        return NULL;

    Symbol* sym = new_symbol_decl(resolver->ast_mem, kind, name, decl);
    sym->status = SYMBOL_STATUS_UNRESOLVED;
    sym->is_local = (scope != resolver->global_scope);

    add_scope_symbol(scope, sym);

    return sym;
}

static void set_scope(Resolver* resolver, Scope* scope)
{
    resolver->curr_scope = scope;
}

static Scope* push_scope(Resolver* resolver, size_t num_syms)
{
    Scope* prev_scope = resolver->curr_scope;
    Scope* scope = new_scope(resolver->ast_mem, num_syms + num_syms);

    scope->parent = prev_scope;

    list_add_last(&prev_scope->children, &scope->lnode);
    set_scope(resolver, scope);

    return scope;
}

static void pop_scope(Resolver* resolver)
{
    resolver->curr_scope = resolver->curr_scope->parent;
}

static void init_builtin_syms(Resolver* resolver)
{
    add_global_type_symbol(resolver, "void", type_void);
    add_global_type_symbol(resolver, "u8", type_u8);
    add_global_type_symbol(resolver, "s8", type_s8);
    add_global_type_symbol(resolver, "u16", type_u16);
    add_global_type_symbol(resolver, "s16", type_s16);
    add_global_type_symbol(resolver, "u32", type_u32);
    add_global_type_symbol(resolver, "s32", type_s32);
    add_global_type_symbol(resolver, "u64", type_u64);
    add_global_type_symbol(resolver, "s64", type_s64);
    add_global_type_symbol(resolver, "f32", type_f32);
    add_global_type_symbol(resolver, "f64", type_f64);

    // Aliased types
    add_global_type_symbol(resolver, "bool", type_bool);
    add_global_type_symbol(resolver, "char", type_char);
    add_global_type_symbol(resolver, "schar", type_schar);
    add_global_type_symbol(resolver, "uchar", type_uchar);
    add_global_type_symbol(resolver, "short", type_short);
    add_global_type_symbol(resolver, "ushort", type_ushort);
    add_global_type_symbol(resolver, "int", type_int);
    add_global_type_symbol(resolver, "uint", type_uint);
    add_global_type_symbol(resolver, "long", type_long);
    add_global_type_symbol(resolver, "ulong", type_ulong);
    add_global_type_symbol(resolver, "llong", type_llong);
    add_global_type_symbol(resolver, "ullong", type_ullong);
    add_global_type_symbol(resolver, "ssize", type_ssize);
    add_global_type_symbol(resolver, "usize", type_usize);
}

#define CASE_INT_CAST(k, o, t, f)                                                                                      \
    case k:                                                                                                            \
        switch (t->kind)                                                                                               \
        {                                                                                                              \
            case INTEGER_U8:                                                                                           \
                o->const_val.as_int._u8 = (u8)o->const_val.as_int.f;                                                   \
                break;                                                                                                 \
            case INTEGER_S8:                                                                                           \
                o->const_val.as_int._s8 = (s8)o->const_val.as_int.f;                                                   \
                break;                                                                                                 \
            case INTEGER_U16:                                                                                          \
                o->const_val.as_int._u16 = (u16)o->const_val.as_int.f;                                                 \
                break;                                                                                                 \
            case INTEGER_S16:                                                                                          \
                o->const_val.as_int._s16 = (s16)o->const_val.as_int.f;                                                 \
                break;                                                                                                 \
            case INTEGER_U32:                                                                                          \
                o->const_val.as_int._u32 = (u32)o->const_val.as_int.f;                                                 \
                break;                                                                                                 \
            case INTEGER_S32:                                                                                          \
                o->const_val.as_int._s32 = (s32)o->const_val.as_int.f;                                                 \
                break;                                                                                                 \
            case INTEGER_U64:                                                                                          \
                o->const_val.as_int._u64 = (u64)o->const_val.as_int.f;                                                 \
                break;                                                                                                 \
            case INTEGER_S64:                                                                                          \
                o->const_val.as_int._s64 = (s64)o->const_val.as_int.f;                                                 \
                break;                                                                                                 \
            default:                                                                                                   \
                o->is_const = false;                                                                                   \
                break;                                                                                                 \
        }                                                                                                              \
        break;

static bool cast_eop(ExprOperand* eop, Type* dst_type)
{
    Type* src_type = eop->type;

    if (src_type == dst_type)
    {
        eop->type = dst_type;
        return true;
    }

    if (!can_cast_eop(eop, dst_type))
        return false;

    // From this point, the following is true:
    // 1) src_type != dst_type
    // 2) types are castable.

    if (eop->is_const)
    {
        if (src_type->kind == TYPE_FLOAT)
        {
            eop->is_const = dst_type->kind != TYPE_INTEGER;
        }
        else
        {
            if (src_type->kind == TYPE_ENUM)
                src_type = src_type->as_enum.base;

            if (dst_type->kind == TYPE_ENUM)
                dst_type = dst_type->as_enum.base;

            switch (src_type->as_integer.kind)
            {
                CASE_INT_CAST(INTEGER_U8, eop, dst_type, _u8)
                CASE_INT_CAST(INTEGER_S8, eop, dst_type, _s8)
                CASE_INT_CAST(INTEGER_U16, eop, dst_type, _u16)
                CASE_INT_CAST(INTEGER_S16, eop, dst_type, _s16)
                CASE_INT_CAST(INTEGER_U32, eop, dst_type, _u32)
                CASE_INT_CAST(INTEGER_S32, eop, dst_type, _s32)
                CASE_INT_CAST(INTEGER_U64, eop, dst_type, _u64)
                CASE_INT_CAST(INTEGER_S64, eop, dst_type, _s64)
                default:
                    eop->is_const = false;
                    break;
            }
        }
    }

    eop->type = dst_type;

    return true;
}

static bool convert_eop(ExprOperand* eop, Type* dst_type)
{
    if (!can_convert_eop(eop, dst_type))
        return false;

    cast_eop(eop, dst_type);

    eop->is_lvalue = false;

    return true;
}

static bool eop_is_null_ptr(ExprOperand eop)
{
    Type* type = eop.type;

    if (eop.is_const && (type->kind == TYPE_INTEGER || type->kind == TYPE_PTR))
    {
        cast_eop(&eop, type_u64);

        return eop.const_val.as_int._u64 == 0;
    }

    return false;
}

static bool can_convert_eop(ExprOperand* operand, Type* dst_type)
{
    bool convertible = false;
    Type* src_type = operand->type;

    // Same types
    if (dst_type == src_type)
    {
        convertible = true;
    }
    // Can convert anything to void
    else if (dst_type == type_void)
    {
        convertible = true;
    }
    // Can convert between arithmetic types
    else if (type_is_arithmetic(dst_type) && type_is_arithmetic(src_type))
    {
        convertible = true;
    }
    // Can convert const NULL (or 0) to a ptr (or proc ptr).
    else if (type_is_ptr_like(dst_type) && eop_is_null_ptr(*operand))
    {
        convertible = true;
    }
    else if ((dst_type->kind == TYPE_PTR) && (src_type->kind == TYPE_PTR))
    {
        Type* dst_pointed_type = dst_type->as_ptr.base;
        Type* src_pointed_type = src_type->as_ptr.base;

        // Can convert a "derived" type to a "base" type.
        // A type is "derived" if its first field is of type "base".
        // Ex: struct Base { ... };  struct Derived { Base base;};
        // Derived* d = malloc(...);
        // Base* b = d;
        if (type_is_aggregate(dst_pointed_type) && type_is_aggregate(src_pointed_type) &&
            (dst_pointed_type == src_pointed_type->as_aggregate.fields[0].type))
        {
            convertible = true;
        }
        // Can convert if either is a void*
        else if ((dst_pointed_type == type_void) || (src_pointed_type == type_void))
        {
            convertible = true;
        }
    }

    return convertible;
}

static bool can_cast_eop(ExprOperand* eop, Type* dst_type)
{
    bool castable = false;
    Type* src_type = eop->type;

    if (can_convert_eop(eop, dst_type))
        castable = true;
    else if (dst_type->kind == TYPE_INTEGER)
        castable = type_is_ptr_like(src_type);
    else if (src_type->kind == TYPE_INTEGER)
        castable = type_is_ptr_like(dst_type);
    else if (type_is_ptr_like(dst_type) && type_is_ptr_like(src_type))
        castable = true;

    return castable;
}

static bool resolve_expr_int(Resolver* resolver, Expr* expr)
{
    (void)resolver;

    ExprInt* eint = (ExprInt*)expr;

    // TODO: Take into account literal suffix (e.g., u, ul, etc.)
    expr->type = type_s32;
    expr->is_const = true;
    expr->is_lvalue = false;
    expr->const_val.as_int._s32 = (int)eint->value;

    return true;
}

static bool resolve_expr_binary(Resolver* resolver, Expr* expr)
{
    ExprBinary* ebinary = (ExprBinary*)expr;
    Expr* left = ebinary->left;
    Expr* right = ebinary->right;

    if (!resolve_expr(resolver, left))
        return false;

    if (!resolve_expr(resolver, right))
        return false;

    switch (ebinary->op)
    {
        case TKN_PLUS:
            if (type_is_arithmetic(left->type) && type_is_arithmetic(right->type))
            {
                if (left->type == right->type)
                {
                    if (left->is_const && right->is_const)
                    {
                        assert(left->type == type_s32); // TODO: Support other types
                        expr->type = left->type;
                        expr->is_const = true;
                        expr->is_lvalue = false;
                        expr->const_val.as_int._s32 = left->const_val.as_int._s32 + right->const_val.as_int._s32;
                    }
                    else
                    {
                        expr->type = left->type;
                        expr->is_const = false;
                        expr->is_lvalue = false;
                    }

                    return true;
                }
                else
                {
                    // TODO: Support type conversion
                    resolver_on_error(resolver, "Cannot add operands of different types");
                    return false;
                }
            }
            else
            {
                // TODO: Support pointer arithmetic.
                resolver_on_error(resolver, "Can only add arithmetic types");
                return false;
            }
            break;
        case TKN_MINUS:
            if (type_is_arithmetic(left->type) && type_is_arithmetic(right->type))
            {
                if (left->type == right->type)
                {
                    if (left->is_const && right->is_const)
                    {
                        assert(left->type == type_s32); // TODO: Support other types
                        expr->type = left->type;
                        expr->is_const = true;
                        expr->is_lvalue = false;
                        expr->const_val.as_int._s32 = left->const_val.as_int._s32 - right->const_val.as_int._s32;
                    }
                    else
                    {
                        expr->type = left->type;
                        expr->is_const = false;
                        expr->is_lvalue = false;
                    }

                    return true;
                }
                else
                {
                    // TODO: Support type conversion
                    resolver_on_error(resolver, "Cannot subtract operands of different types");
                    return false;
                }
            }
            else
            {
                // TODO: Support pointer arithmetic.
                resolver_on_error(resolver, "Can only subtract arithmetic types");
                return false;
            }
            break;
        default:
            resolver_on_error(resolver, "Operation type `%d` not supported", ebinary->op);
            break;
    }

    return false;
}

static bool resolve_expr_ident(Resolver* resolver, Expr* expr)
{
    ExprIdent* eident = (ExprIdent*)expr;
    Symbol* sym = resolve_name(resolver, eident->name);

    if (!sym)
    {
        resolver_on_error(resolver, "Unknown symbol `%s` in expression", eident->name);
        return false;
    }

    switch (sym->kind)
    {
        case SYMBOL_VAR:
            expr->type = sym->type;
            expr->is_lvalue = true;
            expr->is_const = false;

            return true;
        case SYMBOL_CONST:
            expr->type = sym->type;
            expr->is_lvalue = false;
            expr->is_const = true;
            expr->const_val = ((DeclConst*)(sym->decl))->init->const_val;

            return true;
        case SYMBOL_PROC:
            expr->type = sym->type;
            expr->is_lvalue = false;
            expr->is_const = false;

            return true;
        default:
            break;
    }

    resolver_on_error(resolver, "Expression identifier `%s` must refer to a var, const, or proc declaration",
                      eident->name);
    return false;
}

static bool resolve_expr_call(Resolver* resolver, Expr* expr)
{
    ExprCall* ecall = (ExprCall*)expr;

    // Resolve procedure expression.
    if (!resolve_expr(resolver, ecall->proc))
        return false;

    Type* proc_type = ecall->proc->type;

    // Verifty that we're calling an actual procedure type.
    if (proc_type->kind != TYPE_PROC)
    {
        resolver_on_error(resolver, "Cannot use procedure call syntax on a value with a non-procedure type");
        return false;
    }

    // Verify that the number of arguments match number of parameters.
    if (proc_type->as_proc.num_params != ecall->num_args)
    {
        resolver_on_error(resolver,
                          "Incorrect number of procedure call arguments. Expected `%d` arguments, but got `%d`",
                          proc_type->as_proc.num_params, ecall->num_args);
        return false;
    }

    // Resolve argument expressions and verify that argument types match parameter types.
    List* head = &ecall->args;
    List* it = head->next;
    Type** params = proc_type->as_proc.params;
    size_t i = 0;

    while (it != head)
    {
        ProcCallArg* arg = list_entry(it, ProcCallArg, lnode);

        if (!resolve_expr(resolver, arg->expr))
            return false;

        Type* param_type = type_decay(resolver->ast_mem, &resolver->type_cache->ptrs, params[i]); // TODO: Cast at site?

        ExprOperand arg_eop = {.type = arg->expr->type,
                               .is_const = arg->expr->is_const,
                               .is_lvalue = arg->expr->is_lvalue,
                               .const_val = arg->expr->const_val};

        if (param_type->kind == TYPE_PTR)
            eop_decay(resolver, &arg_eop);

        if (!convert_eop(&arg_eop, param_type))
        {
            resolver_on_error(resolver,
                              "Incorrect type for argument %d of procedure call. Expected type `%s`, but got `%s`",
                              (i + 1), type_name(params[i]), type_name(arg->expr->type));
            return false;
        }

        arg->expr = try_wrap_cast_expr(resolver, &arg_eop, arg->expr);

        it = it->next;
        i += 1;
    }

    expr->type = proc_type->as_proc.ret;
    expr->is_lvalue = false;
    expr->is_const = false;

    return true;
}

static bool resolve_expr(Resolver* resolver, Expr* expr)
{
    //(void)expected_type; // TODO: Necessary when resolving compound initializers

    switch (expr->kind)
    {
        case CST_ExprInt:
            return resolve_expr_int(resolver, expr);
        case CST_ExprBinary:
            return resolve_expr_binary(resolver, expr);
        case CST_ExprIdent:
            return resolve_expr_ident(resolver, expr);
        case CST_ExprCall:
            return resolve_expr_call(resolver, expr);
        default:
            ftprint_err("Unsupported expr kind `%d` while resolving\n", expr->kind);
            assert(0);
            break;
    }

    return false;
}

static Type* resolve_typespec(Resolver* resolver, TypeSpec* typespec)
{
    if (!typespec)
        return type_void;

    switch (typespec->kind)
    {
        case CST_TypeSpecIdent:
        {
            TypeSpecIdent* ts = (TypeSpecIdent*)typespec;

            // TODO: Support module path

            const char* ident_name = ts->name;
            Symbol* ident_sym = resolve_name(resolver, ident_name);

            if (!ident_sym)
            {
                resolver_on_error(resolver, "Undefined type `%s`", ident_name);
                return NULL;
            }

            if (ident_sym->kind != SYMBOL_TYPE)
            {
                resolver_on_error(resolver, "Symbol `%s` is not a type", ident_name);
                return NULL;
            }

            return ident_sym->type;
        }
        case CST_TypeSpecPtr:
        {
            TypeSpecPtr* ts = (TypeSpecPtr*)typespec;
            TypeSpec* base_ts = ts->base;
            Type* base_type = resolve_typespec(resolver, base_ts);

            if (!base_type)
                return NULL;

            return type_ptr(resolver->ast_mem, &resolver->type_cache->ptrs, base_type);
        }
        case CST_TypeSpecProc:
        {
            TypeSpecProc* ts = (TypeSpecProc*)typespec;

            AllocatorState mem_state = allocator_get_state(resolver->tmp_mem);
            Type** params = array_create(resolver->tmp_mem, Type*, ts->num_params);
            List* head = &ts->params;

            for (List* it = head->next; it != head; it = it->next)
            {
                ProcParam* proc_param = list_entry(it, ProcParam, lnode);
                Type* param = resolve_typespec(resolver, proc_param->typespec);

                if (!param)
                {
                    allocator_restore_state(mem_state);
                    return NULL;
                }

                if (param == type_void)
                {
                    resolver_on_error(resolver, "Procedure parameter cannot be void");
                    allocator_restore_state(mem_state);
                    return NULL;
                }

                array_push(params, param);
            }
            assert(array_len(params) == ts->num_params);

            Type* ret = type_void;

            if (ts->ret)
            {
                ret = resolve_typespec(resolver, ts->ret);

                if (!ret)
                {
                    allocator_restore_state(mem_state);
                    return NULL;
                }
            }

            Type* type = type_proc(resolver->ast_mem, &resolver->type_cache->procs, array_len(params), params, ret);
            allocator_restore_state(mem_state);

            return type;
        }
        default:
            ftprint_err("Unsupported typespec kind `%d` in resolution\n", typespec->kind);
            assert(0);
            break;
    }

    return NULL;
}

static bool resolve_decl_var(Resolver* resolver, Symbol* sym)
{
    DeclVar* decl = (DeclVar*)sym->decl;
    bool global = !sym->is_local;
    TypeSpec* typespec = decl->typespec;
    Expr* expr = decl->init;
    Type* type = NULL;

    if (typespec)
    {
        Type* declared_type = resolve_typespec(resolver, typespec);

        if (!declared_type)
            return false;

        if (expr)
        {
            if (!resolve_expr(resolver, expr))
                return false;

            ExprOperand right_eop = {.type = expr->type,
                                     .is_const = expr->is_const,
                                     .is_lvalue = expr->is_lvalue,
                                     .const_val = expr->const_val};

            if (declared_type->kind == TYPE_PTR)
                eop_decay(resolver, &right_eop);

            if (!convert_eop(&right_eop, declared_type))
            {
                resolver_on_error(resolver, "Incompatible types. Cannot convert `%s` to `%s`",
                                  type_name(right_eop.type), type_name(declared_type));
                return false;
            }

            if (global && !right_eop.is_const)
            {
                resolver_on_error(resolver, "Global variables must be initialized with a constant value");
                return false;
            }

            decl->init = try_wrap_cast_expr(resolver, &right_eop, decl->init);
            type = declared_type;
        }
        else
        {
            type = declared_type;
        }
    }
    else
    {
        assert(expr); // NOTE: Parser should catch this.

        if (!resolve_expr(resolver, expr))
            return false;

        if (global && !expr->is_const)
        {
            resolver_on_error(resolver, "Global variables must be initialized with a constant value");
            return false;
        }

        type = expr->type;
    }

    assert(type);

    // TODO: Complete incomplete aggregate type
    sym->type = type;
    sym->status = SYMBOL_STATUS_RESOLVED;

    return true;
}

static bool resolve_decl_const(Resolver* resolver, Symbol* sym)
{
    DeclConst* decl = (DeclConst*)sym->decl;
    TypeSpec* typespec = decl->typespec;
    Expr* init = decl->init;

    if (!resolve_expr(resolver, init))
        return false;

    if (!init->is_const)
    {
        resolver_on_error(resolver, "Value for const decl `%s` must be a constant expression", decl->name);
        return false;
    }

    if (!type_is_scalar(init->type))
    {
        resolver_on_error(resolver, "Constant expression must be of a scalar type");
        return false;
    }

    Type* type = NULL;

    if (typespec)
    {
        Type* declared_type = resolve_typespec(resolver, typespec);

        ExprOperand init_eop = {.type = init->type,
                                .is_const = init->is_const,
                                .is_lvalue = init->is_lvalue,
                                .const_val = init->const_val};

        if (declared_type->kind == TYPE_PTR)
            eop_decay(resolver, &init_eop);

        if (!convert_eop(&init_eop, declared_type))
        {
            resolver_on_error(resolver, "Incompatible types. Cannot convert expression of type `%s` to `%s`",
                              type_name(init->type), type_name(declared_type));
            return false;
        }

        decl->init = try_wrap_cast_expr(resolver, &init_eop, decl->init);

        type = declared_type;
    }
    else
    {
        type = init->type;
    }

    assert(type);

    sym->type = type;
    sym->status = SYMBOL_STATUS_RESOLVED;

    return true;
}

static bool resolve_decl_proc(Resolver* resolver, Symbol* sym)
{
    DeclProc* decl = (DeclProc*)sym->decl;
    decl->scope = push_scope(resolver, decl->num_params + decl->num_decls);

    AllocatorState mem_state = allocator_get_state(resolver->tmp_mem);
    Type** params = array_create(resolver->tmp_mem, Type*, 16);
    List* head = &decl->params;

    for (List* it = head->next; it != head; it = it->next)
    {
        DeclVar* proc_param = (DeclVar*)list_entry(it, Decl, lnode);
        Symbol* param_sym =
            add_unresolved_symbol(resolver, decl->scope, SYMBOL_VAR, proc_param->name, (Decl*)proc_param);

        assert(param_sym);

        if (!resolve_decl_var(resolver, param_sym))
        {
            allocator_restore_state(mem_state);
            return false;
        }

        // TODO: recursive ptr decay on param type
        // TODO: complete incomplete param type (struct, union)

        if (param_sym->type == type_void)
        {
            resolver_on_error(resolver, "Procedure parameter cannot be void");
            allocator_restore_state(mem_state);
            return false;
        }

        array_push(params, param_sym->type);
    }

    pop_scope(resolver);
    assert(array_len(params) == decl->num_params);
    allocator_restore_state(mem_state);

    Type* ret_type = type_void;

    if (decl->ret)
    {
        ret_type = resolve_typespec(resolver, decl->ret);

        if (!ret_type)
            return false;
    }

    sym->type = type_proc(resolver->ast_mem, &resolver->type_cache->procs, array_len(params), params, ret_type);
    sym->status = SYMBOL_STATUS_RESOLVED;

    return true;
}

static bool resolve_proc_stmts(Resolver* resolver, Symbol* sym)
{
    assert(sym->kind == SYMBOL_PROC);

    DeclProc* dproc = (DeclProc*)(sym->decl);

    set_scope(resolver, dproc->scope);

    Type* ret_type = sym->type->as_proc.ret;
    unsigned r = resolve_stmt_block_body(resolver, &dproc->stmts, ret_type, 0);
    bool returns = r & RESOLVE_STMT_RETURNS;
    bool success = r & RESOLVE_STMT_SUCCESS;

    pop_scope(resolver);

    if ((ret_type != type_void) && !returns && success)
    {
        resolver_on_error(resolver, "Not all code paths in procedure `%s` return a value", dproc->name);
        return false;
    }

    return success;
}

static bool resolve_global_proc_body(Resolver* resolver, Symbol* sym)
{
    assert(sym->kind == SYMBOL_PROC);

    if (!resolve_proc_stmts(resolver, sym))
        return false;

    // TODO: Support local struct/union/enum declarations inside procedures.
    /*
    while (array_len(resolver->incomplete_syms))
    {
        Symbol* sym = array_pop(resolver->incomplete_syms);

        if (sym->kind == SYMBOL_PROC)
        {
            push_ir_proc(resolver, sym);

            if (!resolve_proc_stmts(resolver, sym))
                return false;
        }
    }
    */

    return true;
}

static unsigned resolve_stmt_block_body(Resolver* resolver, List* stmts, Type* ret_type, unsigned flags)
{
    unsigned ret_success = RESOLVE_STMT_SUCCESS;
    List* head = stmts;

    for (List* it = head->next; it != head; it = it->next)
    {
        if (ret_success & RESOLVE_STMT_RETURNS)
        {
            resolver_on_error(resolver, "Statement will never be executed; all previous control paths return");

            ret_success &= ~RESOLVE_STMT_SUCCESS;
            break;
        }

        Stmt* child_stmt = list_entry(it, Stmt, lnode);
        unsigned r = resolve_stmt(resolver, child_stmt, ret_type, flags);

        // NOTE: Track whether any statement in the block returns from the parent procedure.
        ret_success = (r & RESOLVE_STMT_RETURNS) | ret_success;

        if (!(r & RESOLVE_STMT_SUCCESS))
        {
            ret_success &= ~RESOLVE_STMT_SUCCESS;
            break;
        }
    }

    return ret_success;
}

static unsigned resolve_stmt_block(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags)
{
    StmtBlock* sblock = (StmtBlock*)stmt;
    sblock->scope = push_scope(resolver, sblock->num_decls);

    unsigned ret_success = resolve_stmt_block_body(resolver, &sblock->stmts, ret_type, flags);

    pop_scope(resolver);

    return ret_success;
}

static bool resolve_cond_expr(Resolver* resolver, Expr* expr)
{
    // Resolve condition expression.
    if (!resolve_expr(resolver, expr))
        return false;

    // Ensure that condition express is a scalar type.
    Type* cond_type = type_decay(resolver->ast_mem, &resolver->type_cache->ptrs, expr->type);

    if (!type_is_scalar(cond_type))
    {
        resolver_on_error(resolver, "Conditional expression must resolve to a scalar type, have type `%s`",
                          type_name(cond_type));
        return false;
    }

    return true;
}

static unsigned resolve_cond_block(Resolver* resolver, IfCondBlock* cblock, Type* ret_type, unsigned flags)
{
    if (!resolve_cond_expr(resolver, cblock->cond))
        return 0;

    return resolve_stmt(resolver, cblock->body, ret_type, flags);
}

static unsigned resolve_stmt_if(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags)
{
    StmtIf* sif = (StmtIf*)stmt;

    // Resolve if block.
    unsigned ret = resolve_cond_block(resolver, &sif->if_blk, ret_type, flags);

    if (!(ret & RESOLVE_STMT_SUCCESS))
        return 0;

    // Resolve else block.
    if (sif->else_blk.body)
        ret &= resolve_stmt(resolver, sif->else_blk.body, ret_type, flags);
    else
        ret &= ~RESOLVE_STMT_RETURNS;

    return ret;
}

static unsigned resolve_stmt_while(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags)
{
    StmtWhile* swhile = (StmtWhile*)stmt;

    // Resolve condition expression.
    if (!resolve_cond_expr(resolver, swhile->cond))
        return 0;

    // Resolve loop body.
    unsigned ret = resolve_stmt(resolver, swhile->body, ret_type,
                                flags | RESOLVE_STMT_BREAK_ALLOWED | RESOLVE_STMT_CONTINUE_ALLOWED);

    // NOTE: Because while loops don't have an "else" path, we can't say that all control paths return.
    // TODO: Add else to while loop!!
    ret &= ~RESOLVE_STMT_RETURNS;

    return ret;
}

static void eop_decay(Resolver* resolver, ExprOperand* eop)
{
    eop->type = type_decay(resolver->ast_mem, &resolver->type_cache->ptrs, eop->type);
    eop->is_lvalue = false;
}

static Expr* try_wrap_cast_expr(Resolver* resolver, ExprOperand* eop, Expr* orig_expr)
{
    Expr* expr = orig_expr;

    if (orig_expr->type != eop->type)
    {
        ftprint_out("Implicit cast from %s to %s\n", type_name(orig_expr->type), type_name(eop->type));
        expr = new_expr_cast(resolver->ast_mem, NULL, orig_expr, true, orig_expr->range);

        expr->type = eop->type;
    }

    expr->is_lvalue = eop->is_lvalue;
    expr->is_const = eop->is_const;

    memcpy(&expr->const_val, &eop->const_val, sizeof(Scalar));

    return expr;
}

static unsigned resolve_stmt_expr_assign(Resolver* resolver, Stmt* stmt)
{
    StmtExprAssign* sassign = (StmtExprAssign*)stmt;
    Expr* left_expr = sassign->left;
    Expr* right_expr = sassign->right;

    if (!resolve_expr(resolver, left_expr))
        return 0;

    if (!resolve_expr(resolver, right_expr))
        return 0;

    if (!left_expr->is_lvalue)
    {
        resolver_on_error(resolver, "Left side of assignment statement must be an l-value");
        return 0;
    }

    if (left_expr->type->kind == TYPE_ARRAY)
    {
        resolver_on_error(resolver, "Left side of assignment statement cannot be an array");
        return 0;
    }

    // TODO: Support other assignment operators.
    if (sassign->op_assign != TKN_ASSIGN)
    {
        resolver_on_error(resolver, "Sorry! Only the `=` assignment operator is currently supported. Soon!");
        return 0;
    }

    Type* left_type = left_expr->type;
    Type* right_type = right_expr->type;
    ExprOperand right_eop = {.type = right_type,
                             .is_const = right_expr->is_const,
                             .is_lvalue = right_expr->is_lvalue,
                             .const_val = right_expr->const_val};

    if (left_type->kind == TYPE_PTR)
        eop_decay(resolver, &right_eop);

    if (!convert_eop(&right_eop, left_type))
    {
        resolver_on_error(resolver, "Type mismatch in assignment statement: expected type `%s`, but got `%s`",
                          type_name(left_type), type_name(right_eop.type));
        return 0;
    }

    sassign->right = try_wrap_cast_expr(resolver, &right_eop, sassign->right);

    return RESOLVE_STMT_SUCCESS;
}

static unsigned resolve_stmt(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags)
{
    bool break_allowed = flags & RESOLVE_STMT_BREAK_ALLOWED;
    bool continue_allowed = flags & RESOLVE_STMT_CONTINUE_ALLOWED;

    switch (stmt->kind)
    {
        case CST_StmtNoOp:
            return RESOLVE_STMT_SUCCESS;
        case CST_StmtReturn:
        {
            StmtReturn* sret = (StmtReturn*)stmt;

            if (!sret->expr && (ret_type != type_void))
            {
                resolver_on_error(resolver, "Return statement is missing a return value of type `%s`",
                                  type_name(ret_type));
                return RESOLVE_STMT_RETURNS;
            }

            if (sret->expr)
            {
                if (!resolve_expr(resolver, sret->expr))
                    return RESOLVE_STMT_RETURNS;

                ExprOperand ret_eop = {.type = sret->expr->type,
                                       .is_const = sret->expr->is_const,
                                       .is_lvalue = sret->expr->is_lvalue,
                                       .const_val = sret->expr->const_val};

                eop_decay(resolver, &ret_eop);

                if (!convert_eop(&ret_eop, ret_type))
                {
                    resolver_on_error(resolver, "Invalid return type. Wanted `%s`, but got `%s`", type_name(ret_type),
                                      type_name(ret_eop.type));
                    return RESOLVE_STMT_RETURNS;
                }

                sret->expr = try_wrap_cast_expr(resolver, &ret_eop, sret->expr);
            }

            return RESOLVE_STMT_SUCCESS | RESOLVE_STMT_RETURNS;
        }
        case CST_StmtBreak:
            if (!break_allowed)
            {
                resolver_on_error(resolver, "Illegal break statement");
                return 0;
            }

            return RESOLVE_STMT_SUCCESS;
        case CST_StmtContinue:
            if (!continue_allowed)
            {
                resolver_on_error(resolver, "Illegal continue statement");
                return 0;
            }

            return RESOLVE_STMT_SUCCESS;
        case CST_StmtIf:
            return resolve_stmt_if(resolver, stmt, ret_type, flags);
        case CST_StmtWhile:
        case CST_StmtDoWhile:
            return resolve_stmt_while(resolver, stmt, ret_type, flags);
        case CST_StmtExpr:
        {
            StmtExpr* sexpr = (StmtExpr*)stmt;

            if (!resolve_expr(resolver, sexpr->expr))
                return 0;

            return RESOLVE_STMT_SUCCESS;
        }
        case CST_StmtExprAssign:
            return resolve_stmt_expr_assign(resolver, stmt);
        case CST_StmtDecl:
        {
            StmtDecl* sdecl = (StmtDecl*)stmt;
            Decl* decl = sdecl->decl;
            Scope* scope = resolver->curr_scope;

            switch (decl->kind)
            {
                case CST_DeclVar:
                {
                    DeclVar* dvar = (DeclVar*)decl;
                    Symbol* sym = add_unresolved_symbol(resolver, scope, SYMBOL_VAR, dvar->name, decl);

                    if (!sym)
                    {
                        resolver_on_error(resolver, "Variable `%s` shadows a previous local declaration", dvar->name);
                        return 0;
                    }

                    if (!resolve_decl_var(resolver, sym))
                        return 0;

                    break;
                }
                default:
                    // TODO: Support other declaration kinds.
                    resolver_on_error(resolver, "Only variable and type declarations are supported inside procedures");
                    return 0;
            }

            return RESOLVE_STMT_SUCCESS;
        }
        case CST_StmtBlock:
            return resolve_stmt_block(resolver, stmt, ret_type, flags);
        default:
            break;
    }

    return 0;
}

static bool resolve_symbol(Resolver* resolver, Symbol* sym)
{
    if (sym->status == SYMBOL_STATUS_RESOLVED)
        return true;

    if (sym->status == SYMBOL_STATUS_RESOLVING)
    {
        resolver_on_error(resolver, "Cannot resolve symbol `%s` due to cyclic dependency", sym->name);
        return false;
    }

    assert(sym->status == SYMBOL_STATUS_UNRESOLVED);

    sym->status = SYMBOL_STATUS_RESOLVING;

    switch (sym->kind)
    {
        case SYMBOL_VAR:
            return resolve_decl_var(resolver, sym);
        case SYMBOL_CONST:
            return resolve_decl_const(resolver, sym);
        case SYMBOL_PROC:
            return resolve_decl_proc(resolver, sym);
        default:
            ftprint_err("Unhandled symbol kind `%d`\n", sym->kind);
            assert(0);
            break;
    }

    return false;
}

static Symbol* resolve_name(Resolver* resolver, const char* name)
{
    Symbol* sym = lookup_symbol(resolver->curr_scope, name);

    if (!sym)
        return NULL;

    if (!resolve_symbol(resolver, sym))
        return NULL;

    return sym;
}

bool resolve_global_decls(Resolver* resolver, List* decls)
{
    size_t num_decls = 0;
    List* head = decls;

    // Install decls in global symbol table.
    for (List* it = head->next; it != head; it = it->next)
    {
        Decl* decl = list_entry(it, Decl, lnode);
        SymbolKind kind = SYMBOL_NONE;
        const char* name = NULL;

        fill_decl_symbol_info(decl, &kind, &name);
        add_unresolved_symbol(resolver, resolver->global_scope, kind, name, decl);

        num_decls++;
    }

    // Resolve declaration "headers". Will not resolve procedure bodies or complete aggregate types.
    List* sym_head = &resolver->global_scope->sym_list;

    for (List* it = sym_head->next; it != sym_head; it = it->next)
    {
        Symbol* sym = list_entry(it, Symbol, lnode);

        if (!resolve_symbol(resolver, sym))
            return false;
    }

    // Resolve declaration "bodies".
    for (List* it = sym_head->next; it != sym_head; it = it->next)
    {
        Symbol* sym = list_entry(it, Symbol, lnode);

        if (sym->kind == SYMBOL_PROC)
        {
            if (!resolve_global_proc_body(resolver, sym))
                return false;
        }
    }

    return true;
}

void init_resolver(Resolver* resolver, Allocator* ast_mem, Allocator* tmp_mem, ByteStream* errors,
                   TypeCache* type_cache, Scope* global_scope)
{
    resolver->ast_mem = ast_mem;
    resolver->tmp_mem = tmp_mem;
    resolver->errors = errors;
    resolver->type_cache = type_cache;
    resolver->global_scope = global_scope;

    set_scope(resolver, global_scope);
    init_builtin_syms(resolver);
}
