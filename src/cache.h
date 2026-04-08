#ifndef CACHE_H
#define CACHE_H
#include "arena.h"
#include "headers.h"

#define SETASSOC            ((u64) 16)
#define SETASSOCLOG         ((u64) 4)

#define VIRTUALSETASSOC     ((u64) 4)
#define VIRTUALSETASSOCLOG  ((u64) 2)

#define SA_MAX_ITERATIONS   ((u64) 500)
#define SA_INITIAL_TEMP     ((f64) 0.02)
#define SA_COOLING_RATE     ((f64) 0.99)
#define RATE_THRESHOLD      ((f64) 0.20)
#define BIAS                ((u64) 23)

// NOTE(ejs): I only enum-ified the cachescheme codes used/described in main.cpp.
// some undecipherable magic slop remains in simulator.cpp. CACHE_SCHEME_BASE is
// set to a high number so these enums (hopefully) do not conflict with the magic slop.
enum cache_scheme {
    CACHE_SCHEME_BASE = 1000000, // formerly magic 0
    CACHE_SCHEME_MAPPING,        // formerly magic 1
    CACHE_SCHEME_FLFU,           // formerly magic 88
    CACHE_SCHEME_INNER_SP,       // formerly magic 11100
    CACHE_SCHEME_SPARCH          // formerly magic 11101
};


struct cache_config {
    u64 block_nelems;
    u64 block_nelems_log2;
    enum cache_scheme scheme;
};

struct cache_stats {
};


struct cache {
    struct Arena        *backing;
    struct cache_config cfg;
    struct cache_stats  stats;

};

extern struct cache cache;

extern u64 cachesize;

extern enum cache_scheme cacheScheme;

extern u64 CACHE_BLOCK_NELEMS;
extern u64 CACHE_BLOCK_NELEMS_LOG2;

extern bool useVirtualTag;
extern u64 inputcachesize;
extern long long elements_processed_since_last_adjustment;

extern queue<int> *nextposvector;

extern int LFUmax;
extern int *LFUtag;

extern b8 *Valid;
extern i32 *Tag;
extern i32 *lrubit;

extern i32 *lfubit;

extern b8 *virtualValid;
extern i32 *virtualTag;
extern i32 *virtuallfubit;

extern u16 *PosOrig;

extern long long prefetch_discards;
extern long long prefetch_increments;
extern long long data_access_hit;
extern long long data_access_total;

extern int sa_iteration_k;
extern f64 current_prefetch_size;
extern f64 previous_prefetch_size;

extern f64 last_iteration_data_miss_rate;
extern f64 best_data_miss_rate;
extern long long adjustment_interval;

extern long long totalhit;
extern long long totalaccess;

extern int hitcnt;
extern int misscnt;

extern u64 CACHE_NSETS;
extern u64 CACHE_NSETS_LOG2;

void initializeCacheValid();

__attribute__((noinline)) void cacheAccessFiber(int jj, int fibersize, int ii);

long long getCacheAddr(int fiberid, int relative);
unsigned int getSet(long long addr);
unsigned int getTag(long long addr);
unsigned int getSet2(long long addr);
unsigned int getTag2(long long addr);
int getSetPS(long long fiberId);
long long getTagPS(long long fiberId);
unsigned short getOrig(long long addr);

void setSET();

void initialize_cache();

#endif
