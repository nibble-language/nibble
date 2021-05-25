#include "resolver.h"
#include "parser.h"

static Symbol* resolve_name(Resolver* resolver, const char* name);
static bool resolve_symbol(Resolver* resolver, Symbol* sym);
static bool resolve_decl(Resolver* resolver, Decl* decl);
static bool resolve_decl_var(Resolver* resolver, Decl* decl);
static bool resolve_decl_const(Resolver* resolver, Decl* decl);
static bool resolve_decl_proc(Resolver* resolver, Decl* decl);
static bool resolve_decl_proc_body(Resolver* resolver, Decl* decl);
static bool resolve_expr(Resolver* resolver, Expr* expr, Type* expected_type);
static bool resolve_expr_int(Resolver* resolver, Expr* expr);
static bool resolve_expr_binary(Resolver* resolver, Expr* expr);
static bool resolve_expr_call(Resolver* resolver, Expr* expr);
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
static unsigned resolve_stmt_while(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags);
static unsigned resolve_stmt_if(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags);
static unsigned resolve_cond_block(Resolver* resolver, IfCondBlock* cblock, Type* ret_type, unsigned flags);
static unsigned resolve_stmt_expr_assign(Resolver* resolver, Stmt* stmt);

static Symbol* lookup_symbol(Resolver* resolver, const char* name);
static Symbol* lookup_local_symbol(Resolver* resolver, const char* name);
static Symbol* enter_scope(Resolver* resolver);
static void exit_scope(Resolver* resolver, Symbol* scope_begin);
static bool push_local_var(Resolver* resolver, Decl* decl);

static void init_builtin_syms(Resolver* resolver);
static bool add_global_type_symbol(Resolver* resolver, const char* name, Type* type);
static bool add_global_decl_symbol(Resolver* resolver, Decl* decl);

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

static bool add_global_decl_symbol(Resolver* resolver, Decl* decl)
{
    const char* sym_name = decl->name;

    if (hmap_get(&resolver->global_syms, PTR_UINT(sym_name)))
    {
        resolver_on_error(resolver, "Duplicate definition of `%s`", sym_name);
        return false;
    }

    Symbol* sym = new_symbol_decl(resolver->ast_mem, decl);

    hmap_put(&resolver->global_syms, PTR_UINT(sym_name), PTR_UINT(sym));

    return true;
}

static bool add_global_type_symbol(Resolver* resolver, const char* name, Type* type)
{
    const char* sym_name = intern_ident(name, cstr_len(name), NULL, NULL);

    if (hmap_get(&resolver->global_syms, PTR_UINT(sym_name)))
    {
        resolver_on_error(resolver, "Duplicate definition of `%s`", sym_name);
        return false;
    }

    Symbol* sym = new_symbol_type(resolver->ast_mem, sym_name, type);

    hmap_put(&resolver->global_syms, PTR_UINT(sym_name), PTR_UINT(sym));

    return true;
}

static void init_builtin_syms(Resolver* resolver)
{
    add_global_type_symbol(resolver, "void", type_void);
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
    add_global_type_symbol(resolver, "float32", type_f32);
    add_global_type_symbol(resolver, "float64", type_f64);
}

static Symbol* enter_scope(Resolver* resolver)
{
    return resolver->local_syms_at;
}

static void exit_scope(Resolver* resolver, Symbol* scope_begin)
{
    resolver->local_syms_at = scope_begin;
}

static bool push_local_var(Resolver* resolver, Decl* decl)
{
    // TODO: Only look up to the beginning of the current scope to allow shadowing
    // variables in parent scopes. This currently prohibits all local variable shadowing;
    // global variable shadowing is currently allowed.
    if (lookup_local_symbol(resolver, decl->name))
        return false;

    if (resolver->local_syms_at == resolver->local_syms + MAX_LOCAL_SYMS)
    {
        NIBBLE_FATAL_EXIT("INTERNAL ERROR: Pushed too many local symbols");
        return false;
    }

    Symbol* sym = resolver->local_syms_at;
    sym->kind = SYMBOL_DECL;
    sym->status = SYMBOL_STATUS_RESOLVED;
    sym->name = decl->name;
    sym->decl = decl;

    resolver->local_syms_at += 1;

    return true;
}

static Symbol* lookup_local_symbol(Resolver* resolver, const char* name)
{
    for (Symbol* it = resolver->local_syms_at; it != resolver->local_syms; it -= 1)
    {
        Symbol* sym = it - 1;

        if (sym->name == name)
            return sym;
    }

    return NULL;
}

static Symbol* lookup_symbol(Resolver* resolver, const char* name)
{
    // Lookup local symbols first.
    Symbol* sym = lookup_local_symbol(resolver, name);

    // Lookup global syms.
    if (!sym)
    {
        uint64_t* pval = hmap_get(&resolver->global_syms, PTR_UINT(name));
        sym = pval ? (void*)*pval : NULL;
    }

    return sym;
}

static bool resolve_expr_int(Resolver* resolver, Expr* expr)
{
    (void)resolver;

    ExprInt* eint = (ExprInt*)expr;

    // TODO: Take into account literal suffix (e.g., u, ul, etc.)
    expr->type = type_int;
    expr->is_const = true;
    expr->is_lvalue = false;
    expr->const_val.kind = SCALAR_INTEGER;
    expr->const_val.as_int.kind = INTEGER_INT; // TODO: Redundant
    expr->const_val.as_int.i = (int)eint->value;

    return true;
}

static bool resolve_expr_binary(Resolver* resolver, Expr* expr)
{
    ExprBinary* ebinary = (ExprBinary*)expr;
    Expr* left = ebinary->left;
    Expr* right = ebinary->right;

    if (!resolve_expr(resolver, left, NULL))
        return false;

    if (!resolve_expr(resolver, right, NULL))
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
                        assert(left->type == type_int); // TODO: Support other types
                        expr->type = left->type;
                        expr->is_const = true;
                        expr->is_lvalue = false;
                        expr->const_val.kind = SCALAR_INTEGER;
                        expr->const_val.as_int.kind = INTEGER_INT; // TODO: Redundant
                        expr->const_val.as_int.i = (int)left->const_val.as_int.i + (int)right->const_val.as_int.i;
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
        case SYMBOL_DECL:
            switch (sym->decl->kind)
            {
                case CST_DeclVar:
                    expr->type = type_decay(resolver->ast_mem, &resolver->type_cache->ptrs, sym->decl->type);
                    expr->is_lvalue = true;
                    expr->is_const = false;
                    eident->sym = sym;

                    return true;
                case CST_DeclConst:
                    expr->type = sym->decl->type;
                    expr->is_lvalue = false;
                    expr->is_const = true;
                    expr->const_val = ((DeclConst*)(sym->decl))->init->const_val;
                    eident->sym = sym;

                    return true;
                case CST_DeclProc:
                    expr->type = sym->decl->type;
                    expr->is_lvalue = false;
                    expr->is_const = false;
                    eident->sym = sym;

                    return true;
                default:
                    break;
            }
            break;
        default:
            break;
    }

    resolver_on_error(resolver, "Expression identifier `%s` must refer to a var, const, or proc declaration", eident->name);
    return false;
}

static bool resolve_expr_call(Resolver* resolver, Expr* expr)
{
    ExprCall* ecall = (ExprCall*)expr;

    // Resolve procedure expression.
    if (!resolve_expr(resolver, ecall->proc, NULL))
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
        resolver_on_error(resolver, "Incorrect number of procedure call arguments. Expected `%d` arguments, but got `%d`",
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

        if (!resolve_expr(resolver, arg->expr, NULL))
            return false;

        Type* arg_type = type_decay(resolver->ast_mem, &resolver->type_cache->ptrs, arg->expr->type);
        Type* param_type = type_decay(resolver->ast_mem, &resolver->type_cache->ptrs, params[i]);

        // TODO: Support type conversion
        if (arg->expr->type != param_type)
        {
            resolver_on_error(resolver, "Incorrect type for argument %d of procedure call. Expected type `%s`, but got `%s`",
                             (i + 1), type_name(params[i]), type_name(arg_type));
            return false;
        }

        it = it->next;
        i += 1;
    }

    expr->type = proc_type->as_proc.ret;
    expr->is_lvalue = false;
    expr->is_const = false;

    return true;
}

static bool resolve_expr(Resolver* resolver, Expr* expr, Type* expected_type)
{
    (void)expected_type; // TODO: Necessary when resolving compound initializers

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
            Symbol* ident_sym = lookup_symbol(resolver, ident_name);

            if (!ident_sym)
            {
                resolver_on_error(resolver, "Unresolved type `%s`", ident_name);
                return NULL;
            }

            if (!symbol_is_type(ident_sym))
            {
                resolver_on_error(resolver, "Symbol `%s` is not a type", ident_name);
                return NULL;
            }

            resolve_symbol(resolver, ident_sym);

            return ident_sym->kind == SYMBOL_TYPE ? ident_sym->type : ident_sym->decl->type;
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

static bool resolve_decl_var(Resolver* resolver, Decl* decl)
{
    DeclVar* decl_var = (DeclVar*)decl;
    TypeSpec* typespec = decl_var->typespec;
    Expr* expr = decl_var->init;
    Type* type = NULL;

    if (typespec)
    {
        Type* declared_type = resolve_typespec(resolver, typespec);

        if (!declared_type)
            return NULL;

        if (expr)
        {
            if (resolve_expr(resolver, expr, declared_type))
            {
                Type* inferred_type = expr->type;

                // TODO: Check if can convert type.
                if (inferred_type != declared_type)
                {
                    resolver_on_error(resolver, "Incompatible types. Cannot convert `%s` to `%s`", type_name(inferred_type),
                                     type_name(declared_type));
                }
                else
                {
                    type = declared_type;
                }
            }
        }
        else
        {
            type = declared_type;
        }
    }
    else
    {
        assert(expr); // NOTE: Parser should catch this.

        if (resolve_expr(resolver, expr, NULL))
            type = expr->type;
    }

    // TODO: Complete incomplete aggregate type

    decl->type = type;

    return type != NULL;
}

static bool resolve_decl_const(Resolver* resolver, Decl* decl)
{
    DeclConst* dconst = (DeclConst*)decl;
    TypeSpec* typespec = dconst->typespec;
    Expr* init = dconst->init;

    if (!resolve_expr(resolver, init, NULL))
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

    if (typespec)
    {
        Type* declared_type = resolve_typespec(resolver, typespec);

        if (declared_type != init->type)
        {
            // TODO: Support type conversions
            resolver_on_error(resolver, "Incompatible types. Cannot convert expression of type `%s` to `%s`",
                             type_name(init->type), type_name(declared_type));
            return false;
        }

        decl->type = declared_type;
    }
    else
    {
        decl->type = init->type;
    }

    return true;
}

static bool resolve_decl_proc(Resolver* resolver, Decl* decl)
{
    DeclProc* dproc = (DeclProc*)decl;
    AllocatorState mem_state = allocator_get_state(resolver->tmp_mem);
    Type** params = array_create(resolver->tmp_mem, Type*, 16);
    List* head = &dproc->params;

    for (List* it = head->next; it != head; it = it->next)
    {
        Decl* proc_param = list_entry(it, Decl, lnode);

        if (!resolve_decl_var(resolver, proc_param))
        {
            allocator_restore_state(mem_state);
            return false;
        }

        // TODO: recursive ptr decay on param type
        // TODO: complete incomplete param type (struct, union)

        if (proc_param->type == type_void)
        {
            resolver_on_error(resolver, "Procedure parameter cannot be void");
            allocator_restore_state(mem_state);
            return false;
        }

        array_push(params, proc_param->type);
    }

    assert(array_len(params) == dproc->num_params);
    allocator_restore_state(mem_state);

    Type* ret = type_void;

    if (dproc->ret)
    {
        ret = resolve_typespec(resolver, dproc->ret);

        if (!ret)
            return false;
    }

    decl->type = type_proc(resolver->ast_mem, &resolver->type_cache->procs, array_len(params), params, ret);

    return true;
}

static bool resolve_decl_proc_body(Resolver* resolver, Decl* decl)
{
    assert(decl->kind == CST_DeclProc);

    DeclProc* dproc = (DeclProc*)decl;
    Symbol* scope_begin = enter_scope(resolver);
    List* head = &dproc->params;

    for (List* it = head->next; it != head; it = it->next)
    {
        Decl* proc_param = list_entry(it, Decl, lnode);

        push_local_var(resolver, proc_param);
    }

    Type* ret_type = decl->type->as_proc.ret;
    unsigned r = resolve_stmt(resolver, dproc->body, ret_type, 0);
    bool returns = r & RESOLVE_STMT_RETURNS;
    bool success = r & RESOLVE_STMT_SUCCESS;

    exit_scope(resolver, scope_begin);

    if ((ret_type != type_void) && !returns && success)
    {
        resolver_on_error(resolver, "Not all code paths in procedure `%s` return a value", decl->name);
        return false;
    }

    return success;
}

static bool resolve_decl(Resolver* resolver, Decl* decl)
{
    switch (decl->kind)
    {
        case CST_DeclVar:
            return resolve_decl_var(resolver, decl);
        case CST_DeclConst:
            return resolve_decl_const(resolver, decl);
        case CST_DeclEnum:
        case CST_DeclUnion:
        case CST_DeclStruct:
        case CST_DeclTypedef:
            ftprint_err("Decl kind `%d` not YET supported in resolution\n", decl->kind);
            break;
        case CST_DeclProc:
            return resolve_decl_proc(resolver, decl);
        default:
            ftprint_err("Unknown decl kind `%d` while resolving symbol\n", decl->kind);
            assert(0);
            break;
    }

    return false;
}

static unsigned resolve_stmt_block(Resolver* resolver, Stmt* stmt, Type* ret_type, unsigned flags)
{
    StmtBlock* sblock = (StmtBlock*)stmt;
    Symbol* scope_begin = enter_scope(resolver);

    unsigned ret_success = RESOLVE_STMT_SUCCESS;
    List* head = &sblock->stmts;

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

    exit_scope(resolver, scope_begin);

    return ret_success;
}

static bool resolve_cond_expr(Resolver* resolver, Expr* expr)
{
    // Resolve condition expression.
    if (!resolve_expr(resolver, expr, NULL))
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

    // Resolve elif blocks.
    List* head = &sif->elif_blks;
    List* it = head->next;

    while (it != head)
    {
        IfCondBlock* elif_blk = list_entry(it, IfCondBlock, lnode);

        unsigned elif_ret = resolve_cond_block(resolver, elif_blk, ret_type, flags);

        if (!(elif_ret & RESOLVE_STMT_SUCCESS))
            return 0;

        ret &= elif_ret; // NOTE: All blocks have to return in order to say that all control paths return.

        it = it->next;
    }

    // TODO: Ensure conditions are mutually exclusive (condition ANDed with each previous condition == false)
    // Can probably only do this for successive condition expressions that evaluate to compile-time constants.

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
    unsigned ret =
        resolve_stmt(resolver, swhile->body, ret_type, flags | RESOLVE_STMT_BREAK_ALLOWED | RESOLVE_STMT_CONTINUE_ALLOWED);

    // NOTE: Because while loops don't have an "else" path, we can't say that all control paths return.
    // TODO: Add else to while loop!!
    ret &= ~RESOLVE_STMT_RETURNS;

    return ret;
}

static unsigned resolve_stmt_expr_assign(Resolver* resolver, Stmt* stmt)
{
    StmtExprAssign* sassign = (StmtExprAssign*)stmt;
    Expr* left_expr = sassign->left;
    Expr* right_expr = sassign->right;

    if (!resolve_expr(resolver, left_expr, NULL))
        return 0;

    if (!resolve_expr(resolver, right_expr, NULL))
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
    Type* right_type = type_decay(resolver->ast_mem, &resolver->type_cache->ptrs, right_expr->type);

    // TODO: Support type conversion.
    if (left_type != right_type)
    {
        resolver_on_error(resolver, "Type mismatch in assignment statement: expected type `%s`, but got `%s`",
                         type_name(left_type), type_name(right_type));
        return 0;
    }

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
                resolver_on_error(resolver, "Return statement is missing a return value of type `%s`", type_name(ret_type));
                return RESOLVE_STMT_RETURNS;
            }

            if (sret->expr)
            {
                if (!resolve_expr(resolver, sret->expr, ret_type))
                    return RESOLVE_STMT_RETURNS;

                // TODO: Support type conversions
                if (sret->expr->type != ret_type)
                {
                    resolver_on_error(resolver, "Invalid return type. Wanted `%s`, but got `%s`", type_name(ret_type),
                                     type_name(sret->expr->type));
                    return RESOLVE_STMT_RETURNS;
                }
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

            if (!resolve_expr(resolver, sexpr->expr, NULL))
                return 0;

            return RESOLVE_STMT_SUCCESS;
        }
        case CST_StmtExprAssign:
            return resolve_stmt_expr_assign(resolver, stmt);
        case CST_StmtDecl:
        {
            StmtDecl* sdecl = (StmtDecl*)stmt;
            Decl* decl = sdecl->decl;

            if (decl->kind != CST_DeclVar)
            {
                // TODO: Support other declaration kinds.
                resolver_on_error(resolver, "Only variable declarations are supported inside procedures");
                return 0;
            }

            if (!resolve_decl(resolver, decl))
                return 0;

            if (!push_local_var(resolver, decl))
            {
                resolver_on_error(resolver, "Variable `%s` shadows a previous local declaration", decl->name);
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

    bool resolved = true;

    switch (sym->kind)
    {
        case SYMBOL_TYPE:
            break;
        case SYMBOL_DECL:
            resolved = resolve_decl(resolver, sym->decl);
            break;
        default:
            ftprint_err("Unknown symbol kind `%d`\n", sym->kind);
            assert(0);
            break;
    }

    if (resolved)
        sym->status = SYMBOL_STATUS_RESOLVED;
    else
        ftprint_err("Failed to resolve `%s`\n", sym->name);

    return resolved;
}

static Symbol* resolve_name(Resolver* resolver, const char* name)
{
    Symbol* sym = lookup_symbol(resolver, name);

    if (!sym)
        return NULL;

    if (!resolve_symbol(resolver, sym))
        return NULL;

    return sym;
}

bool resolve_global_decls(Resolver* resolver, List* decls)
{
    List* head = decls;

    // Install decls in global symbol table.
    for (List* it = head->next; it != head; it = it->next)
    {
        Decl* decl = list_entry(it, Decl, lnode);

        add_global_decl_symbol(resolver, decl);
    }

    // Resolve declarations. Will not resolve procedure bodies or complete aggregate types.
    for (List* it = head->next; it != head; it = it->next)
    {
        Decl* decl = list_entry(it, Decl, lnode);
        Symbol* sym = resolve_name(resolver, decl->name);

        if (!sym)
            return false;
    }

    // Resolve procedure bodies and complete aggregate types.
    for (List* it = head->next; it != head; it = it->next)
    {
        Decl* decl = list_entry(it, Decl, lnode);

        if (decl->kind == CST_DeclProc)
        {
            if (!resolve_decl_proc_body(resolver, decl))
                return false;
        }
    }


    return true;
}

void init_resolver(Resolver* resolver, Allocator* ast_mem, Allocator* tmp_mem, ByteStream* errors, TypeCache* type_cache)
{
    resolver->ast_mem = ast_mem;
    resolver->tmp_mem = tmp_mem;
    resolver->errors = errors;
    resolver->global_syms = hmap(8, NULL);
    resolver->local_syms_at = resolver->local_syms;
    resolver->type_cache = type_cache;

    init_builtin_syms(resolver);
}

void free_resolver(Resolver* resolver)
{
#ifndef NDEBUG
    ftprint_out("global syms map: len = %lu, cap = %lu, total_size (malloc) = %lu\n", resolver->global_syms.len,
                resolver->global_syms.cap, resolver->global_syms.cap * sizeof(HMapEntry));
#endif
    hmap_destroy(&resolver->global_syms);
}

