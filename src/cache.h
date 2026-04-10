#ifndef CACHE_H
#define CACHE_H
#include "headers.h"

#define SETASSOC 16
#define SETASSOCLOG 4

#define VIRTUALSETASSOC 4
#define VIRTUALSETASSOCLOG 2

// #define MAXSET 1000005

#define BIAS 23

extern int cachesize;

extern int cacheScheme;

extern int CACHEBLOCK;
extern int CACHEBLOCKLOG;

extern bool useVirtualTag;
extern int inputcachesize;
extern long long elements_processed_since_last_adjustment;

extern queue<int> *nextposvector;

extern int LFUmax;
extern int *LFUtag;

extern bool *Valid;
extern int *Tag;
extern int *lrubit;

extern int *lfubit;

extern bool *virtualValid;
extern int *virtualTag;
extern int *virtuallfubit;

extern unsigned short *PosOrig;
extern unsigned short *vPosOrig;

extern long long prefetch_discards;
extern long long prefetch_increments;
extern long long data_access_misses;
extern long long data_access_hit;
extern long long data_access_total;

extern const int SA_MAX_ITERATIONS;
extern const double SA_INITIAL_TEMP;
extern const double SA_COOLING_RATE;
extern const double RATE_THRESHOLD;

extern int sa_iteration_k;
extern double current_prefetch_size;
extern double previous_prefetch_size;
extern double best_prefetch_size;

extern double last_iteration_data_miss_rate;
extern double best_data_miss_rate;
extern long long elements_processed_since_last_adjustment;
extern long long adjustment_interval;

extern long long totalhit;
extern long long totalaccess;

extern int hitcnt;
extern int misscnt;

extern int SET;
extern int SETLOG;

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
void deinitialize_cache();

#endif
