#ifndef NIBBLE_PATH_UTILS_H
#define NIBBLE_PATH_UTILS_H

#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#define OS_PATH_SEP '/'
#define NIBBLE_MAX_PATH PATH_MAX
#define NIBBLE_PATH_SEP '/'

#ifndef NIBBLE_MAX_PATH
#define NIBBLE_MAX_PATH 4096
#warning "Cannot determine maximum path length (PATH_MAX or MAX_PATH)"
#endif

#include "array.h"
#include "allocator.h"

typedef enum PathRelativity {
    PATH_REL_INVALID, // Invalid
    PATH_REL_ROOT, // Absolute path: /
    PATH_REL_CURR, // Relative to current directory: ./ or ../
    PATH_REL_PROG_ENTRY, // Relative to program entry dir: $/
    PATH_REL_UNKNOWN, // Relative to unknown path (must resolve with search paths): some_dir/
} PathRelativity;

typedef struct Path {
    char* str; // Stretchy buffer.
} Path;

enum DirentFlags {
    DIRENT_IS_VALID = 1 << 0,
    DIRENT_IS_DIR = 1 << 1,
};

typedef struct DirentIter {
    Path base;
    Path name;
    unsigned flags;
    void* os_handle;
} DirentIter;

typedef enum FileKind {
    FILE_NONE,
    FILE_REG,
    FILE_DIR,
    FILE_OTHER,
} FileKind;

extern const char nib_ext[];

#define path_len(p) (array_len((p)->str) - 1)
#define path_cap(p) array_cap((p)->str)
#define path_allctr(p) array_allctr((p)->str)

#define PATH_AS_ARGS(p) ((p)->str), path_len(p)

Path path_create(Allocator* allctr, const char* path, u32 len);
Path path_createf(Allocator* allctr, const char* format, ...);
void path_init(Path* dst, Allocator* allctr, const char* path, u32 len);
void path_set(Path* dst, const char* path, u32 len);
Path path_norm(Allocator* allctr, const char* path, u32 len);
void path_free(Path* path);
Path path_str_join(Allocator* allctr, const char* a, u32 a_len, const char* b, u32 b_len);
Path path_join(Allocator* allctr, const Path* a, const Path* b);

Path path_abs(Allocator* allctr, const Path* cwd, const Path* path);
Path path_str_abs(Allocator* allctr, const char* cwd_str, u32 cwd_len, const char* p_str, u32 p_len);
bool path_real(Path* dst, const Path* path);
bool path_isabs(const Path* path);
bool path_str_isabs(const char* path);

const char* path_basename_ptr(const Path* path);
const char* path_ext_ptr(const Path* path);
Path path_dirname(Allocator* allctr, const Path* path);

FileKind path_kind(const Path* path);
PathRelativity path_relativity(const char* path, u32 len);

void dirent_it_init(DirentIter* it, const char* path_str, Allocator* alloc);
void dirent_it_next(DirentIter* it);
void dirent_it_free(DirentIter* it);

#endif
