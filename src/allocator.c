#include "allocator.h"
#include "basics.h"
#include "cstring.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef NIBBLE_PRINT_MEM_USAGE
#include <stdatomic.h>

volatile u32 nib_alloc_count = 0;
volatile u32 nib_free_count = 0;
volatile size_t nib_alloc_size = 0;

static inline void* nib_malloc(size_t size)
{
    atomic_fetch_add(&nib_alloc_count, 1);
    atomic_fetch_add(&nib_alloc_size, size);
    return malloc(size);
}

static inline void* nib_calloc(size_t num_elems, size_t elem_size)
{
    atomic_fetch_add(&nib_alloc_count, 1);
    atomic_fetch_add(&nib_alloc_size, num_elems * elem_size);
    return calloc(num_elems, elem_size);
}

static inline void nib_free(void* ptr)
{
    atomic_fetch_add(&nib_free_count, 1);
    free(ptr);
}
#else
#define nib_malloc(s) malloc(s)
#define nib_calloc(e, s) calloc((e), (s))
#define nib_free(p) free(p)
#endif

typedef struct MemBlockFooter {
    char* pbuffer;
    char* pat;
    char* pend;
} MemBlockFooter;

static bool alloc_mem_block(Allocator* allocator, size_t block_size)
{
    // Adds a new block of memory to the allocator.
    // The new block will contain a pointer to the old block in its footer (for cleanup).

    char* block = nib_malloc(block_size + sizeof(MemBlockFooter));

    if (!block)
        return false;

    char* pbuffer = allocator->buffer;
    char* pat = allocator->at;
    char* pend = allocator->end;

    allocator->buffer = block;
    allocator->at = block;
    allocator->pat = NULL;
    allocator->end = block + block_size;
    allocator->num_expanded += 1;

    MemBlockFooter* footer = (MemBlockFooter*)allocator->end;
    footer->pbuffer = pbuffer;
    footer->pat = pat;
    footer->pend = pend;

    return true;
}

void* mem_allocate(Allocator* allocator, size_t size, size_t align, bool clear)
{
    assert(align > 0);
    assert((align & (align - 1)) == 0);

    if (!allocator)
        return clear ? nib_calloc(1, size) : nib_malloc(size);

    void* memory = NULL;

    uintptr_t aligned_at = ALIGN_UP((uintptr_t)allocator->at, align);
    uintptr_t new_at = aligned_at + size;

    // Allocate a new memory block if need more memory.
    if (new_at > (uintptr_t)allocator->end) {
        size_t block_size = allocator->end - allocator->buffer;
        size_t worst_size = size + align;

        if (block_size < worst_size) {
            block_size = worst_size;
        }
        block_size *= 2;

        if (!alloc_mem_block(allocator, block_size)) {
            return NULL;
        }

        aligned_at = ALIGN_UP((uintptr_t)allocator->at, align);
        new_at = aligned_at + size;
    }

    memory = (void*)aligned_at;
    allocator->pat = (char*)aligned_at;
    allocator->at = (char*)new_at;

    if (clear)
        memset(memory, 0, size);

    return memory;
}

void* mem_dup(Allocator* allocator, const void* src, size_t size, size_t align)
{
    void* memory = mem_allocate(allocator, size, align, false);

    memcpy(memory, src, size);

    return memory;
}

void* mem_reallocate(Allocator* allocator, void* ptr, size_t old_size, size_t size, size_t align)
{
    if (!allocator)
        return realloc(ptr, size);

    // TODO: If this allocation is at the top of the arena stack, allocation is not necessary!
    void* memory = mem_allocate(allocator, size, align, false);

    if (memory && ptr)
        memcpy(memory, ptr, old_size);

    return memory;
}

void mem_free(Allocator* allocator, void* ptr)
{
    if (!allocator) {
        nib_free(ptr);
    }
    else {
        // If ptr is the previous allocation, undo it.
        // Otherwise, do nothing.
        char* pat = allocator->pat;

        if (pat && (uintptr_t)ptr == (uintptr_t)pat) {
            allocator->at = pat;
            allocator->pat = NULL;
        }
    }
}

Allocator allocator_create(size_t init_size)
{
    static size_t id = 0;
    Allocator allocator = {0};
    allocator.id = id++;

    alloc_mem_block(&allocator, init_size);

    return allocator;
}

void allocator_reset(Allocator* allocator)
{
    if (!allocator->buffer)
        return;

    // Free all blocks, except for the last (original), which is "cleared".
    MemBlockFooter* footer = (MemBlockFooter*)allocator->end;

    while (footer->pbuffer) {
        char* pbuffer = footer->pbuffer;
        char* pend = footer->pend;

        nib_free(allocator->buffer);

        allocator->buffer = pbuffer;
        allocator->end = pend;
        footer = (MemBlockFooter*)allocator->end;
    }

    allocator->at = allocator->buffer;
    allocator->pat = NULL;
}

void allocator_destroy(Allocator* allocator)
{
    if (!allocator->buffer)
        return;

    char* buffer = allocator->buffer;
    char* end = allocator->end;

    while (buffer) {
        MemBlockFooter footer = *(MemBlockFooter*)(end);

        nib_free(buffer);

        buffer = footer.pbuffer;
        end = footer.pend;
    }

    allocator->buffer = NULL;
    allocator->at = NULL;
    allocator->pat = NULL;
    allocator->end = NULL;
}

AllocatorState allocator_get_state(Allocator* allocator)
{
    AllocatorState state = {0};
    state.allocator = allocator;
    state.buffer = allocator->buffer;
    state.at = allocator->at;
    state.pat = allocator->pat;

    return state;
}

void allocator_restore_state(AllocatorState state)
{
    Allocator* allocator = state.allocator;
    char* dest_buffer = state.buffer;
    char* buffer = allocator->buffer;
    char* end = allocator->end;

    while (buffer != dest_buffer) {
        MemBlockFooter footer = *(MemBlockFooter*)(end);

        nib_free(buffer);

        buffer = footer.pbuffer;
        end = footer.pend;
    }

    allocator->buffer = buffer;
    allocator->end = end;
    allocator->at = state.at;
    allocator->pat = state.pat;
}

AllocatorStats allocator_stats(Allocator* allocator)
{
    AllocatorStats stats = {0};

    if (!allocator || !allocator->buffer)
        return stats;

    stats.num_blocks = 1;
    stats.total_size = (size_t)(allocator->end - allocator->buffer);
    stats.used = (size_t)(allocator->at - allocator->buffer);

    MemBlockFooter* footer = (MemBlockFooter*)allocator->end;

    while (footer->pbuffer) {
        char* buffer = footer->pbuffer;
        char* at = footer->pat;
        char* end = footer->pend;

        stats.num_blocks += 1;
        stats.total_size += (size_t)(end - buffer);
        stats.used += (size_t)(at - buffer);

        footer = (MemBlockFooter*)end;
    }

    return stats;
}

void print_allocator_stats(Allocator* allocator, const char* label)
{
    AllocatorStats stats = allocator_stats(allocator);

    ftprint_out("%s: num_blocks = %lu, total_size = %lu, used = %lu, num_expanded = %lu\n", label, stats.num_blocks, stats.total_size,
                stats.used, allocator->num_expanded);
}
