#ifndef NIBBLE_H
#define NIBBLE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "allocator.h"
#include "hash_map.h"
#include "stream.h"

#define MAX_ERROR_LEN 256
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(p, a) (((p) + (a)-1) & ~((a)-1))
#define BITS(x) (sizeof(x) * 8)
#define IS_POW2(x) (((x) & ((x) - 1)) == 0)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#if defined(_WIN32) || defined(_WIN64)
#define NIBBLE_HOST_WINDOWS
#elif defined(__linux__)
#define NIBBLE_HOST_LINUX
#else
#error "This operating system is not yet supported!"
#endif


typedef enum OS {
    OS_INVALID,
    OS_LINUX,
    OS_WIN32,
    OS_OSX,
    NUM_OS,
} OS;

typedef enum Arch {
    ARCH_INVALID,
    ARCH_X64,
    ARCH_X86,
    NUM_ARCH,
} Arch;

extern const char* os_names[NUM_OS];
extern const char* arch_names[NUM_ARCH];

typedef uint32_t ProgPos;

typedef struct ProgRange {
    ProgPos start;
    ProgPos end;
} ProgRange;

ProgRange merge_ranges(ProgRange a, ProgRange b);

typedef struct StringView {
    const char* str;
    size_t len;
} StringView;
#define string_view_lit(cstr_lit)                    \
    {                                                \
        .str = cstr_lit, .len = sizeof(cstr_lit) - 1 \
    }

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef float f32;
typedef double f64;

typedef enum FloatKind {
    FLOAT_F64 = 0,
    FLOAT_F32,
    FLOAT_KIND_COUNT
} FloatKind;

extern const size_t float_kind_sizes[FLOAT_KIND_COUNT];
extern const char* float_kind_names[FLOAT_KIND_COUNT];

typedef struct Float {
    union {
        f64 _f64;
        f32 _f32;
    };
} Float;

static inline bool float_eq(FloatKind kind, Float a, Float b)
{
    return kind == FLOAT_F64 ? a._f64 == b._f64 : a._f32 == b._f32;
}

typedef struct FloatLit {
    struct FloatLit* next;
    bool used;
    size_t id;
    FloatKind kind;
    Float value;
} FloatLit;

typedef enum IntegerKind {
    INTEGER_BOOL,
    INTEGER_U8,
    INTEGER_S8,
    INTEGER_U16,
    INTEGER_S16,
    INTEGER_U32,
    INTEGER_S32,
    INTEGER_U64,
    INTEGER_S64,
    INTEGER_KIND_COUNT
} IntegerKind;

extern const size_t int_kind_sizes[INTEGER_KIND_COUNT];
extern const char* int_kind_names[INTEGER_KIND_COUNT];
extern const bool int_kind_signed[INTEGER_KIND_COUNT];
extern const u64 int_kind_max[INTEGER_KIND_COUNT];

typedef struct Integer {
    union {
        bool _bool;
        u8 _u8;
        s8 _s8;
        u16 _u16;
        s16 _s16;
        u32 _u32;
        s32 _s32;
        u64 _u64;
        s64 _s64;
    };
} Integer;

typedef struct Scalar {
    union {
        Float as_float;
        Integer as_int;
        void* as_ptr;
    };
} Scalar;

typedef enum Keyword {
    KW_VAR = 0,
    KW_CONST,
    KW_ENUM,
    KW_UNION,
    KW_STRUCT,
    KW_PROC,
    KW_TYPEDEF,
    KW_SIZEOF,
    KW_TYPEOF,
    KW_TYPEID,
    KW_INDEXOF,
    KW_OFFSETOF,
    KW_LENGTH,
    KW_STATIC_ASSERT,
    KW_RET_TYPE,
    KW_EXPORT,
    KW_IMPORT,
    KW_FROM,
    KW_AS,
    KW_INCLUDE,
    KW_LABEL,
    KW_GOTO,
    KW_BREAK,
    KW_CONTINUE,
    KW_RETURN,
    KW_IF,
    KW_ELSE,
    KW_WHILE,
    KW_DO,
    KW_FOR,
    KW_SWITCH,
    KW_CASE,
    KW_UNDERSCORE,

    KW_TRUE,
    KW_FALSE,
    KW_NULL,

    KW_COUNT,
} Keyword;

typedef enum Annotation {
    ANNOTATION_CUSTOM = 0,
    ANNOTATION_EXPORTED,
    ANNOTATION_FOREIGN,
    ANNOTATION_PACKED,

    ANNOTATION_COUNT
} Annotation;

typedef enum Intrinsic {
    INTRINSIC_READIN,
    INTRINSIC_WRITEOUT,
    INTRINSIC_MEMCPY,
    INTRINSIC_MEMSET,
    INTRINSIC_SYSCALL0,
    INTRINSIC_SYSCALL1,
    INTRINSIC_SYSCALL2,
    INTRINSIC_SYSCALL3,
    INTRINSIC_SYSCALL4,
    INTRINSIC_SYSCALL5,
    INTRINSIC_SYSCALL6,
    
    INTRINSIC_COUNT
} Intrinsic;

typedef enum BuiltinStructField {
    BUILTIN_STRUCT_FIELD_LENGTH,
    BUILTIN_STRUCT_FIELD_DATA,
    BUILTIN_STRUCT_FIELD_TYPE,
    BUILTIN_STRUCT_FIELD_PTR,

    BUILTIN_STRUCT_FIELD_COUNT
} BuiltinStructField;

typedef struct StrLit {
    struct StrLit* next;
    bool used;
    size_t id;
    size_t len;
    char str[];
} StrLit;

typedef enum IdentifierKind {
    IDENTIFIER_NAME,
    IDENTIFIER_KEYWORD,
    IDENTIFIER_INTRINSIC,
} IdentifierKind;

typedef struct Identifier {
    struct Identifier* next;

    IdentifierKind kind;
    union {
        Keyword kw;
        Intrinsic intrinsic;
    };

    size_t len;
    char str[];
} Identifier;


extern const char* keyword_names[KW_COUNT];
extern const char* annotation_names[ANNOTATION_COUNT];

extern Identifier* intrinsic_idents[INTRINSIC_COUNT];
extern Identifier* builtin_struct_fields[BUILTIN_STRUCT_FIELD_COUNT];
extern Identifier* main_proc_ident;

bool slurp_file(StringView* contents, Allocator* allocator, const char* filename);

typedef struct Error Error;
typedef struct ErrorStream ErrorStream;

struct Error {
    Error* next;
    ProgRange range;
    size_t size;
    char msg[];
};

struct ErrorStream {
    Error* first;
    Error* last;
    size_t count;
    Allocator* allocator;
};

void error_stream_init(ErrorStream* stream, Allocator* allocator);
void error_stream_free(ErrorStream* stream);
void error_stream_add(ErrorStream* stream, ProgRange range, const char* buf, size_t size);

#define NIBBLE_FATAL_EXIT(f, ...) nibble_fatal_exit(__FILE__, __LINE__, (f), ##__VA_ARGS__)
void nibble_fatal_exit(const char* file, u32 line, const char* format, ...);
#endif


