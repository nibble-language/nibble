#ifndef NIBBLE_AST_H
#define NIBBLE_AST_H
#include <stddef.h>
#include <stdint.h>

#include "allocator.h"
#include "lexer/module.h"
#include "llist.h"
#include "nibble.h"
#include "hash_map.h"
#include "stream.h"
#include "compiler.h"

typedef struct Expr Expr;
typedef struct TypeSpec TypeSpec;
typedef struct Decl Decl;
typedef struct Stmt Stmt;

typedef struct Type Type;
typedef struct Symbol Symbol;
typedef struct SymbolVar SymbolVar;
typedef struct SymbolConst SymbolConst;
typedef struct SymbolEnum SymbolEnum;
typedef struct SymbolProc SymbolProc;
typedef struct SymbolModule SymbolModule;
typedef struct AnonObj AnonObj;
typedef struct Scope Scope;
typedef struct Module Module;
typedef struct BBlock BBlock;

typedef struct ConstAddr ConstAddr;
typedef struct ConstArrayMemberInitzer ConstArrayMemberInitzer;
typedef struct ConstArrayInitzer ConstArrayInitzer;
typedef struct ConstStructInitzer ConstStructInitzer;
typedef struct ConstUnionInitzer ConstUnionInitzer;
typedef struct ConstExpr ConstExpr;

typedef struct IdentNode {
    Identifier* ident;
    ListNode lnode;
} IdentNode;

typedef struct NSIdent {
    ProgRange range;
    size_t num_idents;
    List idents;
} NSIdent;

char* ftprint_ns_ident(Allocator* allocator, NSIdent* ns_ident);
///////////////////////////////
//       Type Specifiers
//////////////////////////////

typedef enum TypeSpecKind {
    CST_TYPE_SPEC_NONE,
    CST_TypeSpecIdent,
    CST_TypeSpecProc,
    CST_TypeSpecStruct,
    CST_TypeSpecUnion,
    CST_TypeSpecPtr,
    CST_TypeSpecArray,
    CST_TypeSpecConst,
    CST_TypeSpecTypeof,
    CST_TypeSpecRetType,
} TypeSpecKind;

struct TypeSpec {
    TypeSpecKind kind;
    ProgRange range;
};

typedef struct TypeSpecIdent {
    TypeSpec super;
    NSIdent ns_ident;
} TypeSpecIdent;

typedef struct TypeSpecTypeof {
    TypeSpec super;
    Expr* expr;
} TypeSpecTypeof;

typedef struct TypeSpecRetType {
    TypeSpec super;
    Expr* proc_expr;
} TypeSpecRetType;

typedef struct AggregateField {
    ProgRange range;
    Identifier* name;
    TypeSpec* typespec;
    ListNode lnode;
} AggregateField;

typedef struct TypeSpecAggregate {
    TypeSpec super;
    List fields;
} TypeSpecAggregate;

typedef TypeSpecAggregate TypeSpecStruct;
typedef TypeSpecAggregate TypeSpecUnion;

typedef struct ProcParam {
    ProgRange range;
    Identifier* name;
    bool is_variadic;
    TypeSpec* typespec;
    ListNode lnode;
} ProcParam;

typedef struct TypeSpecProc {
    TypeSpec super;
    bool is_variadic;
    size_t num_params;
    List params;
    TypeSpec* ret;
} TypeSpecProc;

typedef struct TypeSpecPtr {
    TypeSpec super;
    TypeSpec* base;
} TypeSpecPtr;

typedef struct TypeSpecArray {
    TypeSpec super;
    TypeSpec* base;
    Expr* len;
    bool infer_len;
} TypeSpecArray;

typedef struct TypeSpecConst {
    TypeSpec super;
    TypeSpec* base;
} TypeSpecConst;

AggregateField* new_aggregate_field(Allocator* allocator, Identifier* name, TypeSpec* type, ProgRange range);

TypeSpec* new_typespec_ident(Allocator* allocator, NSIdent* ns_ident);
TypeSpec* new_typespec_typeof(Allocator* allocator, Expr* expr, ProgRange range);
TypeSpec* new_typespec_ret_type(Allocator* allocator, Expr* proc_expr, ProgRange range);
TypeSpec* new_typespec_ptr(Allocator* allocator, TypeSpec* base, ProgRange range);
TypeSpec* new_typespec_array(Allocator* allocator, TypeSpec* base, Expr* len, bool infer_len, ProgRange range);
TypeSpec* new_typespec_const(Allocator* allocator, TypeSpec* base, ProgRange range);
ProcParam* new_proc_param(Allocator* allocator, Identifier* name, TypeSpec* type, bool is_variadic, ProgRange range);
TypeSpec* new_typespec_proc(Allocator* allocator, size_t num_params, List* params, TypeSpec* ret, bool is_variadic, ProgRange range);
TypeSpec* new_typespec_struct(Allocator* allocator, List* fields, ProgRange range);
TypeSpec* new_typespec_union(Allocator* allocator, List* fields, ProgRange range);

char* ftprint_typespec(Allocator* allocator, TypeSpec* type);
///////////////////////////////
//       Expressions
//////////////////////////////

typedef enum ExprKind {
    CST_EXPR_NONE,
    CST_ExprTernary,
    CST_ExprBinary,
    CST_ExprUnary,
    CST_ExprCall,
    CST_ExprIndex,
    CST_ExprField,
    CST_ExprFieldIndex,
    CST_ExprInt,
    CST_ExprFloat,
    CST_ExprStr,
    CST_ExprIdent,
    CST_ExprCast,
    CST_ExprBitCast,
    CST_ExprSizeof,
    CST_ExprTypeid,
    CST_ExprOffsetof,
    CST_ExprIndexof,
    CST_ExprLength,
    CST_ExprCompoundLit,
    CST_ExprBoolLit,
    CST_ExprNullLit,
} ExprKind;

struct Expr {
    ExprKind kind;
    ProgRange range;

    // TODO: Composition of ExprOperand?
    // This would save a lot of unnecessary copying of ExprOperands.
    Type* type;
    bool is_constexpr;
    bool is_lvalue;
    bool is_imm;
    Scalar imm;
};

typedef struct ExprTernary {
    Expr super;
    Expr* cond;
    Expr* then_expr;
    Expr* else_expr;
} ExprTernary;

typedef struct ExprBinary {
    Expr super;
    TokenKind op;
    Expr* left;
    Expr* right;
} ExprBinary;

typedef struct ExprUnary {
    Expr super;
    TokenKind op;
    Expr* expr;
} ExprUnary;

typedef struct ProcCallArg {
    Expr* expr;
    Identifier* name;
    ListNode lnode;
} ProcCallArg;

typedef struct ExprCall {
    Expr super;
    Expr* proc;
    size_t num_args;
    List args;
} ExprCall;

typedef struct ExprIndex {
    Expr super;
    Expr* array;
    Expr* index;
} ExprIndex;

typedef struct ExprField {
    Expr super;
    Expr* object;
    Identifier* field;
} ExprField;

typedef struct ExprFieldIndex {
    Expr super;
    Expr* object;
    Expr* index;
} ExprFieldIndex;

typedef struct ExprInt {
    Expr super;
    TokenInt token;
} ExprInt;

typedef struct ExprFloat {
    Expr super;
    FloatKind fkind;
    Float value;
} ExprFloat;

typedef struct ExprStr {
    Expr super;
    StrLit* str_lit;
} ExprStr;

typedef struct ExprIdent {
    Expr super;
    NSIdent ns_ident;
} ExprIdent;

typedef struct ExprCast {
    Expr super;
    TypeSpec* typespec;
    Expr* expr;
    bool implicit;
} ExprCast;

typedef struct ExprBitCast {
    Expr super;
    TypeSpec* typespec;
    Expr* expr;
} ExprBitCast;

typedef struct ExprSizeof {
    Expr super;
    TypeSpec* typespec;
} ExprSizeof;

typedef struct ExprTypeid {
    Expr super;
    TypeSpec* typespec;
} ExprTypeid;

typedef struct ExprIndexof {
    Expr super;
    TypeSpec* obj_ts;
    Identifier* field_ident;
} ExprIndexof;

typedef struct ExprOffsetof {
    Expr super;
    TypeSpec* obj_ts;
    Identifier* field_ident;
} ExprOffsetof;

typedef struct ExprLength {
    Expr super;
    Expr* arg;
} ExprLength;

typedef enum DesignatorKind {
    DESIGNATOR_NONE,
    DESIGNATOR_NAME,
    DESIGNATOR_INDEX,
} DesignatorKind;

typedef struct Designator {
    DesignatorKind kind;

    union {
        Identifier* name;
        Expr* index;
    };
} Designator;

typedef struct MemberInitializer {
    ProgRange range;
    Designator designator;
    Expr* init;
    ListNode lnode;
} MemberInitializer;

typedef struct ExprCompoundLit {
    Expr super;
    TypeSpec* typespec;
    size_t num_initzers;
    List initzers;
} ExprCompoundLit;

typedef struct ExprBoolLit {
    Expr super;
    bool val;
} ExprBoolLit;

typedef struct ExprNullLit {
    Expr super;
} ExprNullLit;

Expr* new_expr_ternary(Allocator* allocator, Expr* cond, Expr* then_expr, Expr* else_expr);
Expr* new_expr_binary(Allocator* allocator, TokenKind op, Expr* left, Expr* right);
Expr* new_expr_unary(Allocator* allocator, TokenKind op, Expr* expr, ProgRange range);
Expr* new_expr_field(Allocator* allocator, Expr* object, Identifier* field, ProgRange range);
Expr* new_expr_field_index(Allocator* allocator, Expr* object, Expr* index, ProgRange range);
Expr* new_expr_index(Allocator* allocator, Expr* array, Expr* index, ProgRange range);
Expr* new_expr_call(Allocator* allocator, Expr* proc, size_t num_args, List* args, ProgRange range);
ProcCallArg* new_proc_call_arg(Allocator* allocator, Expr* expr, Identifier* name);
Expr* new_expr_int(Allocator* allocator, TokenInt token, ProgRange range);
Expr* new_expr_float(Allocator* allocator, FloatKind fkind, Float value, ProgRange range);
Expr* new_expr_str(Allocator* allocator, StrLit* str_lit, ProgRange range);
Expr* new_expr_ident(Allocator* allocator, NSIdent* ns_ident);
Expr* new_expr_cast(Allocator* allocator, TypeSpec* type, Expr* arg, bool implicit, ProgRange range);
Expr* new_expr_bit_cast(Allocator* allocator, TypeSpec* type, Expr* arg, ProgRange range);
Expr* new_expr_sizeof(Allocator* allocator, TypeSpec* type, ProgRange range);
Expr* new_expr_typeid(Allocator* allocator, TypeSpec* type, ProgRange range);
Expr* new_expr_offsetof(Allocator* allocator, TypeSpec* obj_ts, Identifier* field_ident, ProgRange range);
Expr* new_expr_indexof(Allocator* allocator, TypeSpec* obj_ts, Identifier* field_ident, ProgRange range);
Expr* new_expr_length(Allocator* allocator, Expr* arg, ProgRange range);
MemberInitializer* new_member_initializer(Allocator* allocator, Expr* init, Designator designator, ProgRange range);
Expr* new_expr_compound_lit(Allocator* allocator, TypeSpec* type, size_t num_initzers, List* initzers, ProgRange range);
Expr* new_expr_bool_lit(Allocator* allocator, bool val, ProgRange range);
Expr* new_expr_null_lit(Allocator* allocator, ProgRange range);

char* ftprint_expr(Allocator* allocator, Expr* expr);
/////////////////////////////
//        Statements
/////////////////////////////
typedef enum StmtKind {
    CST_STMT_NONE,
    CST_StmtNoOp,
    CST_StmtIf,
    CST_StmtWhile,
    CST_StmtDoWhile,
    CST_StmtFor,
    CST_StmtSwitch,
    CST_StmtReturn,
    CST_StmtBreak,
    CST_StmtContinue,
    CST_StmtGoto,
    CST_StmtLabel,
    CST_StmtExpr,
    CST_StmtExprAssign,
    CST_StmtDecl,
    CST_StmtBlock,
    CST_StmtStaticAssert,
    CST_StmtExport,
    CST_StmtImport,
    CST_StmtInclude,
} StmtKind;

struct Stmt {
    StmtKind kind;
    ProgRange range;
    ListNode lnode;
};

typedef struct StmtStaticAssert {
    Stmt super;
    Expr* cond;
    StrLit* msg;
} StmtStaticAssert;

typedef struct ImportSymbol {
    ProgRange range;
    Identifier* name;
    Identifier* rename;
    ListNode lnode;
} ImportSymbol;

typedef struct StmtImport {
    Stmt super;
    List import_syms;
    size_t num_imports;
    StrLit* mod_pathname;
    Identifier* mod_namespace;
} StmtImport;

typedef struct ExportSymbol {
    ProgRange range;
    NSIdent ns_ident;
    Identifier* rename;
    ListNode lnode;
} ExportSymbol;

typedef struct StmtExport {
    Stmt super;
    List export_syms;
    size_t num_exports;
} StmtExport;

typedef struct StmtInclude {
    Stmt super;
    StrLit* file_pathname;
} StmtInclude;

typedef struct StmtNoOp {
    Stmt super;
} StmtNoOp;

typedef struct StmtBlock {
    Stmt super;
    List stmts;
    u32 num_decls;
    Scope* scope;
} StmtBlock;

typedef struct IfCondBlock {
    ProgRange range;
    Expr* cond;
    Stmt* body;
} IfCondBlock;

typedef struct ElseBlock {
    ProgRange range;
    Stmt* body;
} ElseBlock;

typedef struct StmtIf {
    Stmt super;
    IfCondBlock if_blk;
    ElseBlock else_blk;
} StmtIf;

typedef struct StmtWhile {
    Stmt super;
    Expr* cond;
    Stmt* body;
} StmtWhile;

typedef struct StmtDoWhile {
    Stmt super;
    Expr* cond;
    Stmt* body;
} StmtDoWhile;

typedef struct StmtFor {
    Stmt super;
    Scope* scope;
    Stmt* init;
    Expr* cond;
    Stmt* next;
    Stmt* body;
} StmtFor;

typedef struct SwitchCase {
    ProgRange range;

    Expr* start; // NOTE: Both start and end are null for default case.
    Expr* end;

    Scope* scope;
    List stmts;
    u32 num_decls;
} SwitchCase;

typedef struct CaseInfo {
    Scalar start;
    Scalar end;
    bool is_signed;
    u32 index; // Index to get the full SwitchCase* object.
} CaseInfo;

typedef struct StmtSwitch {
    Stmt super;
    Expr* expr;
    bool has_default_case;
    u32 default_case_index;
    u32 num_cases;
    SwitchCase** cases;

    // Info for case ranges sorted by increasing start value. Does not include default case.
    u32 num_case_infos;
    CaseInfo* case_infos;
} StmtSwitch;

typedef struct StmtReturn {
    Stmt super;
    Expr* expr;
} StmtReturn;

typedef struct StmtExpr {
    Stmt super;
    Expr* expr;
} StmtExpr;

typedef struct StmtExprAssign {
    Stmt super;
    Expr* left;
    TokenKind op_assign;
    Expr* right;
} StmtExprAssign;

typedef struct StmtBreak {
    Stmt super;
    const char* label;
} StmtBreak;

typedef struct StmtContinue {
    Stmt super;
    const char* label;
} StmtContinue;

typedef struct StmtGoto {
    Stmt super;
    const char* label;
} StmtGoto;

typedef struct StmtLabel {
    Stmt super;
    const char* label;
    Stmt* target;
} StmtLabel;

typedef struct StmtDecl {
    Stmt super;
    Decl* decl;
} StmtDecl;

Stmt* new_stmt_noop(Allocator* allocator, ProgRange range);
Stmt* new_stmt_decl(Allocator* allocator, Decl* decl);
Stmt* new_stmt_block(Allocator* allocator, List* stmts, u32 num_decls, ProgRange range);
Stmt* new_stmt_expr(Allocator* allocator, Expr* expr, ProgRange range);
Stmt* new_stmt_expr_assign(Allocator* allocator, Expr* lexpr, TokenKind op_assign, Expr* rexpr, ProgRange range);
Stmt* new_stmt_while(Allocator* allocator, Expr* cond, Stmt* body, ProgRange range);
Stmt* new_stmt_do_while(Allocator* allocator, Expr* cond, Stmt* body, ProgRange range);
Stmt* new_stmt_if(Allocator* allocator, IfCondBlock* if_blk, ElseBlock* else_blk, ProgRange range);
Stmt* new_stmt_for(Allocator* allocator, Stmt* init, Expr* cond, Stmt* next, Stmt* body, ProgRange range);
Stmt* new_stmt_return(Allocator* allocator, Expr* expr, ProgRange range);
Stmt* new_stmt_break(Allocator* allocator, const char* label, ProgRange range);
Stmt* new_stmt_continue(Allocator* allocator, const char* label, ProgRange range);
Stmt* new_stmt_goto(Allocator* allocator, const char* label, ProgRange range);
Stmt* new_stmt_label(Allocator* allocator, const char* label, Stmt* target, ProgRange range);
SwitchCase* new_switch_case(Allocator* allocator, Expr* start, Expr* end, List* stmts, u32 num_decls, ProgRange range);
Stmt* new_stmt_switch(Allocator* allocator, Expr* expr, u32 num_cases, SwitchCase** cases, bool has_default_case,
                      u32 default_case_index, ProgRange range);
Stmt* new_stmt_static_assert(Allocator* allocator, Expr* cond, StrLit* msg, ProgRange range);
ImportSymbol* new_import_symbol(Allocator* allocator, Identifier* name, Identifier* rename, ProgRange range);
Stmt* new_stmt_import(Allocator* allocator, size_t num_imports, List* import_syms, StrLit* mod_pathname, Identifier* mod_namespace,
                      ProgRange range);
ExportSymbol* new_export_symbol(Allocator* allocator, NSIdent* ns_ident, Identifier* rename, ProgRange range);
Stmt* new_stmt_export(Allocator* allocator, size_t num_exports, List* export_syms, ProgRange range);
Stmt* new_stmt_include(Allocator* allocator, StrLit* file_pathname, ProgRange range);

char* ftprint_stmt(Allocator* allocator, Stmt* stmt);
Identifier* get_import_sym_name(StmtImport* stmt, Identifier* name);
///////////////////////////////
//       Declarations
//////////////////////////////

typedef struct DeclAnnotation {
    Identifier* ident;
    ProgRange range;
    size_t num_args;
    List args; // List of ProcCallArg elems
    ListNode lnode;
} DeclAnnotation;

typedef enum DeclKind {
    CST_DECL_NONE = 0,
    CST_DeclVar,
    CST_DeclConst,
    CST_DeclEnum,
    CST_DeclEnumItem,
    CST_DeclUnion,
    CST_DeclStruct,
    CST_DeclProc,
    CST_DeclTypedef,
    CST_DECL_KIND_COUNT
} DeclKind;

enum DeclFlags {
    DECL_IS_EXPORTED = 0x1,
    DECL_IS_FOREIGN = 0x2,
};

struct Decl {
    DeclKind kind;
    ProgRange range;
    Identifier* name;
    unsigned flags;
    List annotations;
    ListNode lnode;
};

enum DeclVarFlags {
    DECL_VAR_IS_VARIADIC = 0x1,
    DECL_VAR_IS_UNINIT = 0x2 // Variable declaration is explicitly uninitialized
};

typedef struct DeclVar {
    Decl super;
    unsigned flags;
    TypeSpec* typespec;
    Expr* init;
} DeclVar;

typedef struct DeclConst {
    Decl super;
    TypeSpec* typespec;
    Expr* init;
} DeclConst;

typedef struct DeclEnumItem {
    Decl super;
    Expr* value;
    ListNode lnode;
} DeclEnumItem;

typedef struct DeclEnum {
    Decl super;
    TypeSpec* typespec;
    size_t num_items;
    List items;
} DeclEnum;

typedef struct DeclAggregate {
    Decl super;
    List fields;
} DeclAggregate;

typedef DeclAggregate DeclUnion;
typedef DeclAggregate DeclStruct;

typedef struct DeclProc {
    Decl super;
    TypeSpec* ret;

    u32 num_params;
    u32 num_decls;
    bool returns;

    // TODO: Use flags variable
    bool is_incomplete;
    bool is_variadic;

    List params;
    List stmts;

    Scope* scope;
} DeclProc;

typedef struct DeclTypedef {
    Decl super;
    TypeSpec* typespec;
} DeclTypedef;

DeclAnnotation* new_annotation(Allocator* allocator, Identifier* ident, u32 num_args, List* args, ProgRange range);
Decl* new_decl_var(Allocator* allocator, Identifier* name, TypeSpec* type, Expr* init, unsigned flags, ProgRange range);
Decl* new_decl_const(Allocator* allocator, Identifier* name, TypeSpec* type, Expr* init, ProgRange range);
Decl* new_decl_typedef(Allocator* allocator, Identifier* name, TypeSpec* type, ProgRange range);
Decl* new_decl_enum(Allocator* allocator, Identifier* name, TypeSpec* type, size_t num_items, List* items, ProgRange range);
DeclEnumItem* new_decl_enum_item(Allocator* allocator, Identifier* name, Expr* value, ProgRange range);

typedef Decl* NewDeclAggregateProc(Allocator* alloc, Identifier* name, List* fields, ProgRange range);
Decl* new_decl_struct(Allocator* allocator, Identifier* name, List* fields, ProgRange range);
Decl* new_decl_union(Allocator* allocator, Identifier* name, List* fields, ProgRange range);
Decl* new_decl_proc(Allocator* allocator, Identifier* name, u32 num_params, List* params, TypeSpec* ret, List* stmts, u32 num_decls,
                    bool is_incomplete, bool is_variadic, ProgRange range);

char* ftprint_decl(Allocator* allocator, Decl* decl);

///////////////////////////////
//       Types
//////////////////////////////

typedef enum TypeKind {
    TYPE_VOID,
    TYPE_INTEGER,
    TYPE_FLOAT,
    TYPE_ENUM,
    TYPE_PTR,
    TYPE_PROC,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_INCOMPLETE_AGGREGATE,
} TypeKind;

typedef struct Type Type;

typedef struct TypeInteger {
    IntegerKind kind;
} TypeInteger;

typedef struct TypeFloat {
    FloatKind kind;
} TypeFloat;

typedef struct TypeProc {
    size_t num_params;
    Type** params;
    Type* ret;
    bool is_variadic;
} TypeProc;

typedef struct TypeArray {
    Type* base;
    size_t len;
} TypeArray;

typedef struct TypePtr {
    Type* base;
} TypePtr;

typedef struct TypeEnum {
    Type* base;
    Symbol* sym;
} TypeEnum;

typedef struct TypeAggregateField {
    Type* type;
    size_t offset;
    size_t index;
    Identifier* name;
} TypeAggregateField;

typedef struct TypeAggregateBody {
    size_t num_fields;
    TypeAggregateField* fields;
} TypeAggregateBody;

typedef enum TypeStructWrapperKind {
    TYPE_STRUCT_IS_NOT_WRAPPER = 0,
    TYPE_STRUCT_IS_SLICE_WRAPPER,
} TypeStructWrapperKind;

typedef struct TypeStruct {
    TypeAggregateBody body;
    TypeStructWrapperKind wrapper_kind;
} TypeStruct;

typedef struct TypeUnion {
    TypeAggregateBody body;
} TypeUnion;

typedef struct TypeIncompleteAggregate {
    Symbol* sym;
    bool is_completing;
} TypeIncompleteAggregate;

struct Type {
    TypeKind kind;
    size_t id;
    size_t size;
    size_t align;

    union {
        TypeInteger as_integer;
        TypeFloat as_float;
        TypePtr as_ptr;
        TypeEnum as_enum;
        TypeProc as_proc;
        TypeArray as_array;
        TypeStruct as_struct;
        TypeUnion as_union;
        TypeIncompleteAggregate as_incomplete;
    };
};

enum BuiltinTypeKind {
    // Basic primitive types
    BUILTIN_TYPE_VOID = 0,
    BUILTIN_TYPE_U8,
    BUILTIN_TYPE_S8,
    BUILTIN_TYPE_U16,
    BUILTIN_TYPE_S16,
    BUILTIN_TYPE_U32,
    BUILTIN_TYPE_S32,
    BUILTIN_TYPE_U64,
    BUILTIN_TYPE_S64,
    BUILTIN_TYPE_F32,
    BUILTIN_TYPE_F64,

    // Aliases for primitive types
    BUILTIN_TYPE_BOOL,
    BUILTIN_TYPE_CHAR,
    BUILTIN_TYPE_SCHAR,
    BUILTIN_TYPE_UCHAR,
    BUILTIN_TYPE_SHORT,
    BUILTIN_TYPE_USHORT,
    BUILTIN_TYPE_INT,
    BUILTIN_TYPE_UINT,
    BUILTIN_TYPE_LONG,
    BUILTIN_TYPE_ULONG,
    BUILTIN_TYPE_LLONG,
    BUILTIN_TYPE_ULLONG,
    BUILTIN_TYPE_SSIZE,
    BUILTIN_TYPE_USIZE,

    BUILTIN_TYPE_ANY,

    NUM_BUILTIN_TYPES,
};

typedef struct BuiltinType {
    const char* name;
    Type* type;
} BuiltinType;

extern BuiltinType builtin_types[NUM_BUILTIN_TYPES];

// Common types used in the compiler for type-checking
extern Type* type_ptr_void;
extern Type* type_ptr_char;
extern Type* type_ptr_ptr_char;

extern size_t PTR_SIZE;
extern size_t PTR_ALIGN;

extern int type_integer_ranks[];

void init_builtin_types(OS target_os, Arch target_arch, Allocator* ast_mem, TypeCache* type_cache);
const char* type_name(const Type* type);
bool value_is_int_max(u64 value, IntegerKind ikind);
bool value_in_int_range(u64 value, IntegerKind ikind);
bool type_is_integer_like(const Type* type);
bool type_is_bool(const Type* type);
bool type_is_signed(const Type* type);
bool type_is_arithmetic(const Type* type);
bool type_is_scalar(const Type* type);
bool type_is_int_scalar(const Type* type);
bool type_is_ptr_like(const Type* type);
bool type_is_aggregate(const Type* type);
bool type_is_obj_like(const Type* type);
bool type_agg_has_non_float(const Type* type);
bool type_is_slice(const Type* type);
bool slice_and_array_compatible(Type* array_type, Type* slice_type);
bool type_is_incomplete_array(Type* type);
bool type_has_incomplete_array(Type* type);
bool types_are_compatible(Type* t, Type* u);
bool ptr_types_are_derived(Type* base, Type* derived);
Type* common_ptr_type(Type* t, Type* u);

Type* try_array_decay(Allocator* allocator, HMap* type_ptr_cache, Type* type);

void complete_struct_type(Allocator* allocator, Type* type, size_t num_fields, const TypeAggregateField* fields);
void complete_union_type(Allocator* allocator, Type* type, size_t num_fields, const TypeAggregateField* fields);

TypeAggregateField* get_type_aggregate_field(Type* type, Identifier* name);
TypeAggregateField* get_type_struct_field(Type* type, Identifier* name);
TypeAggregateField* get_type_union_field(Type* type, Identifier* name);

Type* type_ptr(Allocator* allocator, HMap* type_ptr_cache, Type* base);
Type* type_array(Allocator* allocator, HMap* type_array_cache, Type* base, size_t len);
Type* type_proc(Allocator* allocator, HMap* type_proc_cache, size_t num_params, Type** params, Type* ret, bool is_variadic);
Type* type_unsigned_int(Type* type_int);
Type* type_enum(Allocator* allocator, Type* base, Symbol* sym);
Type* type_incomplete_aggregate(Allocator* allocator, Symbol* sym);
Type* type_anon_aggregate(Allocator* allocator, HMap* type_cache, TypeKind kind, size_t num_fields, const TypeAggregateField* fields);
Type* type_slice(Allocator* allocator, HMap* type_slice_cache, HMap* type_ptr_cache, Type* elem_type);

///////////////////////////////
//       Symbols
//////////////////////////////

typedef enum SymbolKind {
    SYMBOL_NONE,
    SYMBOL_VAR,
    SYMBOL_CONST,
    SYMBOL_PROC,
    SYMBOL_TYPE,
    SYMBOL_MODULE,
    SYMBOL_KIND_COUNT,
} SymbolKind;

extern const SymbolKind decl_sym_kind[CST_DECL_KIND_COUNT];
extern const char* sym_kind_names[SYMBOL_KIND_COUNT];

typedef enum SymbolStatus {
    SYMBOL_STATUS_UNRESOLVED,
    SYMBOL_STATUS_RESOLVING,
    SYMBOL_STATUS_RESOLVED,
} SymbolStatus;

typedef enum ConstExprKind {
    CONST_EXPR_NONE = 0,
    CONST_EXPR_IMM,
    CONST_EXPR_MEM_ADDR,
    CONST_EXPR_DEREF_ADDR,
    CONST_EXPR_ARRAY_INIT,
    CONST_EXPR_STRUCT_INIT,
    CONST_EXPR_UNION_INIT,
    CONST_EXPR_VAR,
    CONST_EXPR_PROC,
    CONST_EXPR_STR_LIT,
    CONST_EXPR_FLOAT_LIT
} ConstExprKind;

typedef enum ConstAddrKind {
    CONST_ADDR_SYM,
    CONST_ADDR_STR_LIT,
    CONST_ADDR_FLOAT_LIT,
} ConstAddrKind;

// The address of symbols or string literals are "constexprs" that can be assigned to global
// variables. This struct represents a symbol or string literal address with an optional displacement.
struct ConstAddr {
    ConstAddrKind kind;

    union {
        const Symbol* sym;
        const StrLit* str_lit;
        const FloatLit* float_lit; // Not really used by front-end since global float var decls just copy the float
                                   // literal at compile-time. However, this is handy to have here for x64 relocations.
    };

    u32 disp;
};

struct ConstArrayInitzer {
    size_t num_initzers;
    ConstArrayMemberInitzer* initzers;
};

struct ConstStructInitzer {
    size_t num_initzers;
    ConstExpr** field_exprs; // One per field
};

struct ConstUnionInitzer {
    size_t field_index;
    ConstExpr* field_expr;
};

struct ConstExpr {
    ConstExprKind kind;
    Type* type;

    union {
        Scalar imm;
        ConstAddr addr;
        Symbol* sym;
        StrLit* str_lit;
        FloatLit* float_lit;
        ConstArrayInitzer array_initzer;
        ConstStructInitzer struct_initzer;
        ConstUnionInitzer union_initzer;
    };
};

struct ConstArrayMemberInitzer {
    size_t index;
    ConstExpr const_expr;
};

struct SymbolVar {
    size_t index; // Index in container

    union {
        // Used by backends to store this var's
        // location in the stack (if local)
        s32 offset;

        // Used to describe initial value (constexpr) for global variable
        ConstExpr const_expr;
    };
};

struct SymbolConst {
    Scalar imm;
};

struct SymbolEnum {
    size_t num_items;
    Symbol** items;
};

struct SymbolProc {
    size_t index; // Index of this symbol in bucket list container.
                  // Used as index for proc metadata in parallel arrays (avoids need for HMap)
    struct BBlock** bblocks; // Stretchy buffer of basic blocks
    size_t num_instrs;

    u32 num_regs;
    bool is_nonleaf;

    List tmp_objs;
    size_t num_tmp_objs;

    // For procs in foreign libs
    // Check (sym->decl->flags & DECL_IS_FOREIGN) first.
    StrLit* foreign_name;
};

struct SymbolModule {
    Module* mod;
    Stmt* stmt;
};

struct Symbol {
    SymbolKind kind;
    SymbolStatus status;

    bool is_local;
    Identifier* name;
    Decl* decl;
    Type* type;
    Module* home;
    List lnode;

    union {
        SymbolVar as_var;
        SymbolConst as_const;
        SymbolEnum as_enum;
        SymbolProc as_proc;
        SymbolModule as_mod;
    };
};

struct AnonObj {
    size_t size;
    size_t align;

    s32 id;
    s32 offset;
    List lnode;
};

Symbol* new_symbol(Allocator* allocator, SymbolKind kind, SymbolStatus status, Identifier* name, Module* home_mod);
Symbol* new_symbol_decl(Allocator* allocator, Decl* decl, Module* home_mod);
Symbol* new_symbol_builtin_type(Allocator* allocator, Identifier* name, Type* type, Module* home_mod);
Symbol* new_symbol_mod(Allocator* alloc, StmtImport* stmt, Module* import_mod, Module* home_mod);
char* symbol_mangled_name(Allocator* allocator, const Symbol* sym);

///////////////////////////////
//       Scope
//////////////////////////////

struct Scope {
    struct Scope* parent;
    List children;

    HMap sym_table;

    // TODO: Make this a vanilla array with a size equal to number of parsed decls.
    // Why? Because we are only store symbols native to the corresponding module.
    List sym_list;
    size_t num_syms;

    // List of anonymous objects on this scope's stack
    List obj_list;
    size_t num_objs;

    ListNode lnode;
};

void scope_init(Scope* scope);

Scope* new_scope(Allocator* allocator, u32 num_syms);
void init_scope_sym_table(Scope* scope, Allocator* allocator, u32 num_syms);

Symbol* lookup_symbol(Scope* curr_scope, Identifier* name);
Symbol* lookup_scope_symbol(Scope* scope, Identifier* name);

void add_scope_symbol(Scope* scope, Identifier* name, Symbol* sym, bool add_list);
Symbol* add_unresolved_symbol(Allocator* allocator, Scope* scope, Module* mod, Decl* decl);
AnonObj* add_anon_obj(Allocator* allocator, List* objs, s32 id, size_t size, size_t align);
bool install_module_decls(Allocator* ast_mem, Module* mod, ErrorStream* errors);
bool module_add_global_sym(Module* mod, Identifier* name, Symbol* sym, ErrorStream* errors);
bool import_all_mod_syms(Module* dst_mod, Module* src_mod, ErrorStream* errors);
bool import_mod_syms(Module* dst_mod, Module* src_mod, StmtImport* stmt, ErrorStream* errors);

///////////////////////////////
//      Module
///////////////////////////////

struct Module {
    ProgRange range;

    u64 id;
    StrLit* abs_path;

    List import_stmts;
    List export_stmts;
    List stmts;
    Scope scope;

    HMap export_table;

    size_t num_decls;
    size_t num_imports;
    size_t num_exports;

    bool is_parsing;
};

void module_init(Module* mod, u64 id, StrLit* abs_path);
void module_init_tables(Module* mod, Allocator* allocator, size_t num_builtins);
Symbol* module_get_export_sym(Module* mod, Identifier* name);
bool module_add_export_sym(Module* mod, Identifier* name, Symbol* sym);
#endif
