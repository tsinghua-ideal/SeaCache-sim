#ifdef __cplusplus
extern "C" {
#endif

/*
Arena functions
Credit: Ryan J. Fleury @ Epic Game Tools
(slight simplifications + renames)
*/
#ifndef ARENA_H
#define ARENA_H

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#define ALIGN_POW2(n, b)    (((n) + (b) - 1) & (~((b) - 1)))
#define IS_POW2(n)          ((((n) & ((n) - 1)) == 0) && (n != 0))
#define MIN(x, y)           ((x) < (y) ? (x) : (y))
#define MAX(x, y)           ((x) > (y) ? (x) : (y))
#define UNLIKELY(x)         (__builtin_expect(!!(x), 0))
#define LIKELY(x)           (__builtin_expect(!!(x), 1))

#define GB                  ((uint64_t)1<<30)
#define MB                  ((uint64_t)1<<20)
#define KB                  ((uint64_t)1<<10)

// enum Arena_Flags : uint64_t {
//     ARENA_FLAG_LARGE_PAGE   = (1 << 0)
// };

#define ARENA_HEADER_SIZE sizeof(struct Arena)

struct Arena {
    uint64_t        pos;
    uint64_t        res;
    uint64_t        cmt;
};

struct Arena_Mark {
    struct Arena    *a;
    uint64_t        pos;
};

static void *os_reserve(uint64_t size)
{
    void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if(result == MAP_FAILED)
        result = 0;
    return result;
}

static int32_t os_commit(void *ptr, uint64_t size)
{
    mprotect(ptr, size, PROT_READ|PROT_WRITE);
    return 1;
}

static void os_release(void *ptr, uint64_t size)
{
    munmap(ptr, size);
}

static struct Arena *arena_alloc(uint64_t res_size, uint64_t cmt_size)
{
    struct Arena *rv;
    void *mem;
    uint64_t page_size = getpagesize();
    res_size = ALIGN_POW2(res_size, page_size);
    cmt_size = ALIGN_POW2(cmt_size, page_size);

    mem = os_reserve(res_size);
    os_commit(mem, cmt_size);
    assert(mem);

    rv = (struct Arena *)mem;

    rv->pos     = ARENA_HEADER_SIZE;
    rv->res     = res_size;
    rv->cmt     = cmt_size;

    return rv;
}

static inline
void arena_release(struct Arena *a)
{
    os_release(a, a->res);
}

static
uint64_t arena_pos(struct Arena *a);

static
void *arena_push(struct Arena *a, uint64_t size, uint64_t align, uint8_t zero)
{
    assert(IS_POW2(align));

    uint64_t pos_cur = ALIGN_POW2(a->pos, align);
    uint64_t pos_nex = pos_cur + size;
    assert(pos_nex < a->res); // oom

    if(a->cmt < pos_nex) {
        uint64_t cmt_nex_aligned = pos_nex + a->cmt-1;
        cmt_nex_aligned -= cmt_nex_aligned % a->cmt;
        uint64_t cmt_nex_clamped = MIN(cmt_nex_aligned, a->res);

        uint8_t *cmt_ptr = (uint8_t *)a + a->cmt;
        os_commit(cmt_ptr, cmt_nex_clamped - a->cmt);
        a->cmt = cmt_nex_clamped;
    }

    uint64_t size_to_zero = zero
        ? MIN(a->cmt, pos_nex) - pos_cur
        : 0;

    uint8_t *rv = (uint8_t *)a + pos_cur;
    a->pos = pos_nex;
    if(size_to_zero)
        memset(rv, 0, size_to_zero);

    return rv;
}

static
uint64_t arena_pos(struct Arena *a)
{
    return a->pos;
}

static
void arena_pop_to(struct Arena *a, uint64_t pos)
{
    a->pos = pos;
}

static void arena_clear(struct Arena *a)
{
    arena_pop_to(a, ARENA_HEADER_SIZE);
}

static void arena_pop(struct Arena *a, uint64_t amt)
{
    uint64_t pos_old = arena_pos(a);
    uint64_t pos_new = pos_old;
    if(amt < pos_old)
        pos_new = pos_old - amt;
    arena_pop_to(a, pos_new);
}

static struct Arena_Mark arena_snap(struct Arena *a)
{
    struct Arena_Mark rv;
    rv.a    = a;
    rv.pos  = arena_pos(a);
    return rv;
}

static void arena_rewind(struct Arena_Mark m)
{
    arena_pop_to(m.a, m.pos);
}



static size_t arena_strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

static void *arena_memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (; n; n--) *d++ = *s++;
    return dst;
}

// static int arena_strncmp(const char *_l, const char *_r, size_t n)
// {
// 	const unsigned char *l=(void *)_l, *r=(void *)_r;
// 	if (!n--) return 0;
// 	for (; *l && *r && n && *l == *r ; l++, r++, n--);
// 	return *l - *r;
// }

static int arena_strcmp(const char* s1, const char* s2)
{
    while(*s1 && (*s1==*s2))
        s1++,s2++;
    return *(const unsigned char*)s1-*(const unsigned char*)s2;
}

#endif // ARENA_H

#ifdef __cplusplus
}
#endif