#include "cache.h"
#include "headers.h"
#include "statistics.h"
#include "util.h"

u64 cachesize = 262144;

u64 CACHE_BLOCK_NELEMS      = 16; // where each elem(ent) is an f64
u64 CACHE_BLOCK_NELEMS_LOG2 = 4;

u64 inputcachesize;

u64 CACHE_NSETS     = cachesize / (CACHE_BLOCK_NELEMS * SETASSOC);
u64 CACHE_NSETS_LOG2= getlog(CACHE_NSETS);

// Params: cachesize, cacheblock
void setSET()
{
    CACHE_NSETS = (cachesize) / (CACHE_BLOCK_NELEMS * SETASSOC);
    CACHE_NSETS_LOG2 = getlog(CACHE_NSETS);
    initialize_cache();
}

b8 *Valid = nullptr;
i32 *Tag = nullptr;
i32 *lrubit = nullptr;

i32 *lfubit = nullptr;

b8  *virtualValid = nullptr;
i32 *virtualTag = nullptr;
i32 *virtuallfubit = nullptr;

int LFUbit = 4;
int LFUmax = (1 << LFUbit) - 1;
int *LFUtag = nullptr;

// split into 4 parts.  witin 16: 0000, 0001, 0010,,,,  1111
// short *partialValid = nullptr;

// for the pack&split
const int N_TAG_L_BITS = 0; // Tag-L bits

u8  *Cnt    = nullptr;
u16 *PosOrig= nullptr;

// use to record the lru.
// higher is better (accessed recently)
int cachecycle = 0;

// cache Scheme 1: the original version, each fiber is a new cacheline; allocate
// another cacheline if exceed. cache Scheme 0: alternative 1 (worse); don't
// allocate another cacheline if exceed -> may bad in both long or short
// scenarios cache Scheme 4: the condensed address version.  need to load a
// whole cacheline each time request a short fiber

// start addr: start of the fiber. fiberid or condensed dram address
// exceed part: the part exceed the cacheline. cut: only cache one cacheline.
// split: split to consective addrs not full part: when less then one cacheline.
// whole: load the whole cacheline anyway. partial: only load a part (need more
// hardware change ) to support partial: need a extra metadata to track whether
// a fiber is valid. (not very expensive. only one bit per each fiber)
//                  start addr     exceed part      not full part                work
// cache Scheme 0       fiber       cut                 whole
// cache Scheme 1       fiber       split               whole
enum cache_scheme cacheScheme;
// cache Scheme 4       addr        split               whole
// cache Scheme 6: scheme 1 + OPT                                                SPARCH
// cache Scheme 11100: scheme0 + static OPT                                      INNERSP
// cache Scheme 11101: sceme0 + dynamic OPT                                      SPARCH
// cache Scheme 66: scheme6 + LFU prefetch + hybrid bit (fewer hardware cost)    SCACHE
// cache Scheme 88: refers to the practical FLFU (enabling 4-bit, virtual tag)   SCACHE
//   (virtual tag can be configured or not (baseline)) the flu information is 
//   no longer kept in the LFUtag, but the extra lfubit

long long getCacheAddr(int fiberid, int relative) {
    long long ret;

    ret = (((long long)fiberid) << CACHE_BLOCK_NELEMS_LOG2);
    if (relative) {
        ret += (((long long)relative) << (CACHE_BLOCK_NELEMS_LOG2 + BIAS));
    }

    return ret;
}

// mapping: tag | set index | offset within cacheblock
unsigned int getSet(long long addr) { return (addr >> (CACHE_BLOCK_NELEMS_LOG2)) % CACHE_NSETS; }

unsigned int getTag(long long addr) {
    return (addr >> (CACHE_BLOCK_NELEMS_LOG2 + CACHE_NSETS_LOG2));
}

// mapping: tag-H | set index | tag-L | offset within cacheblock
unsigned int getSet2(long long addr) {
    return (addr >> (CACHE_BLOCK_NELEMS_LOG2 + N_TAG_L_BITS)) % CACHE_NSETS;
}

unsigned int getTag2(long long addr) {
    long long fiberId = addr >> CACHE_BLOCK_NELEMS_LOG2;
    long long tag_h = fiberId >> (N_TAG_L_BITS + CACHE_NSETS_LOG2);
    long long tag_l = fiberId & ((1 << N_TAG_L_BITS) - 1);
    int _tag = (tag_h << N_TAG_L_BITS) | tag_l;
    return _tag;
}

int getSetPS(long long fiberId) {
    return (fiberId >> N_TAG_L_BITS) & ((1 << CACHE_NSETS_LOG2) - 1);
}

long long getTagPS(long long fiberId) {
    long long tag_h = fiberId >> (N_TAG_L_BITS + CACHE_NSETS_LOG2);
    long long tag_l = fiberId & ((1 << N_TAG_L_BITS) - 1);
    return (tag_h << N_TAG_L_BITS) | tag_l;
}

u16 getOrig(long long addr) {
    return (addr >> CACHE_BLOCK_NELEMS_LOG2) & 0xFFFF;
}

// = 0 when don't use virtual tag
// = 1 when use virtual tag
bool useVirtualTag = 1;

int getLRU(int _set, int _index) { return lrubit[_set * SETASSOC + _index]; }
int getlfubit(int _set, int _index) { return lfubit[_set * SETASSOC + _index]; }

void initPracticalLFU(int _set, int _index, int LFUtime) {
    lfubit[_set * SETASSOC + _index] = LFUtime;
}

bool cacheHit(long long addr) {
    int _set = getSet(addr);
    int _tag = getTag(addr);

    for (int i = 0; i < SETASSOC; i++) {
        if (Valid[_set * SETASSOC + i] && (Tag[_set * SETASSOC + i] == _tag)) {
            // hit !!

            // update lru
            cachecycle++;
            lrubit[_set * SETASSOC + i] = cachecycle;
            return 1;
        }
    }

    // miss !
    return 0;
}

bool cacheHitOPT(long long addr, int nextpos) {

    int _set = getSet(addr);
    int _tag = getTag(addr);

    for (int i = 0; i < SETASSOC; i++) {
        if (Valid[_set * SETASSOC + i] && (Tag[_set * SETASSOC + i] == _tag)) {
            // update lru opt
            lrubit[_set * SETASSOC + i] = nextpos;
            return 1;
        }
    }

    // miss !
    return 0;
}

bool cacheHitOPTLFU(long long addr, int lfutime) {
    int _set = getSet(addr);
    int _tag = getTag(addr);

    for (int i = 0; i < SETASSOC; i++) {
        if (Valid[_set * SETASSOC + i] && (Tag[_set * SETASSOC + i] == _tag)) {
            // update lru opt lfu
            lrubit[_set * SETASSOC + i] = lfutime;
            return 1;
        }
    }
    // miss !
    return 0;
}

bool cacheHitPracticalLFU(long long addr, bool isfirst, long long firstaddr) {
    int _set = getSet2(addr);
    int _tag = getTag2(addr);

    for (int i = 0; i < SETASSOC; i++) {
        if (Valid[_set * SETASSOC + i]) {
            // fuzzy compare
            if ((Tag[_set * SETASSOC + i] <= _tag) && (_tag < Tag[_set * SETASSOC + i] + Cnt[_set * SETASSOC + i] + 1)) {

                if (!isfirst) {
                    // need to check orig
                    if (PosOrig[_set * SETASSOC + i] != getOrig(firstaddr)) {
                        // not the same orig
                        continue;
                    }
                } else {
                    // first
                    if (PosOrig[_set * SETASSOC + i] != 0) {
                        continue;
                    }
                }
                // hit !!
                // updatePracticalLFU; update without lfutime
                if (lfubit[_set * SETASSOC + i]) {
                    lfubit[_set * SETASSOC + i]--;
                }

                return 1;
            }
        }
    }
    // miss !
    return 0;
}

void cacheReplace(long long addr) {

    int replaceindex = -1;
    // higher means need to keep
    // -1 means invalid now
    int replacelru = 10000000;

    int _set = getSet(addr);
    int _tag = getTag(addr);

    for (int i = 0; i < SETASSOC; i++) {
        if (!Valid[_set * SETASSOC + i]) {
            // if has invalid slot, use it
            replacelru = -1;
            replaceindex = i;
            // don't need to find others anymore
            break;
        } else {
            int tmplru = getLRU(_set, i);

            // change to this slot
            if (tmplru < replacelru) {
                replacelru = tmplru;
                replaceindex = i;
            }
        }
    }

    Valid[_set * SETASSOC + replaceindex] = 1;
    Tag[_set * SETASSOC + replaceindex] = _tag;

    // init LRU; play the same as updateLRU in LRU policy
    cachecycle++;
    lrubit[_set * SETASSOC + replaceindex] = cachecycle;
}

void cacheReplaceOPT(long long addr, int nextpos) {

    int replaceindex = -1;
    // higher means need to keep
    // -1 means invalid now
    int replacelru = 10000000;

    int _set = getSet(addr);
    int _tag = getTag(addr);

    for (int i = 0; i < SETASSOC; i++) {
        if (!Valid[_set * SETASSOC + i]) {
            // if has invalid slot, use it
            replacelru = -1;
            replaceindex = i;
            // don't need to find others anymore
            break;
        } else {
            int tmplru = getLRU(_set, i);

            // change to this slot
            if (tmplru < replacelru) {
                replacelru = tmplru;
                replaceindex = i;
            }
        }
    }

    Valid[_set * SETASSOC + replaceindex] = 1;
    Tag[_set * SETASSOC + replaceindex] = _tag;

    // init lru opt
    lrubit[_set * SETASSOC + replaceindex] = nextpos;
}

void cacheReplaceOPTLFU(long long addr, int LFUtime) {
    int replaceindex = -1;
    // higher means need to keep
    // -1 means invalid now
    int replacelru = 10000000;

    int _set = getSet(addr);
    int _tag = getTag(addr);

    for (int i = 0; i < SETASSOC; i++) {
        if (!Valid[_set * SETASSOC + i]) {
            // if has invalid slot, use it
            replacelru = -1;
            replaceindex = i;
            // don't need to find others anymore
            break;
        } else {
            int tmplru = getLRU(_set, i);

            // change to this slot
            if (tmplru < replacelru) {
                replacelru = tmplru;
                replaceindex = i;
            }
        }
    }

    Valid[_set * SETASSOC + replaceindex] = 1;
    Tag[_set * SETASSOC + replaceindex] = _tag;

    // init LRU OPT LFU
    lrubit[_set * SETASSOC + replaceindex] = LFUtime;
}

void cacheReplacePracticalLFU(long long addr, bool isfirst,
                              long long firstaddr) {

    int replaceindex = -1;
    int replacelfu = LFUmax + 1;
    int _set = getSet2(addr);
    int _tag = getTag2(addr);

    // calculate how many fibers can be loaded
    int fibercnt = 1;
    if (isfirst) {

        int fiberid = addr >> CACHE_BLOCK_NELEMS_LOG2;
        int tmpblocksize = CACHE_BLOCK_NELEMS;
        tmpblocksize -= sim.cursor.B.sizes[fiberid - TJ] * 3;
        while (tmpblocksize > 0 && (fiberid + fibercnt < TJ + sim.cfg.jjj)) {
            if (sim.cursor.B.sizes[fiberid + fibercnt - TJ] * 3 <= tmpblocksize) {
                tmpblocksize -= sim.cursor.B.sizes[fiberid + fibercnt - TJ] * 3;
                fibercnt++;
            } else {
                break;
            }
        }
    }

    // check whether in virtual tag. only when useVirtualTag is true
    bool invirtualtag = 0;
    // only use when invirtualtag = 1;
    int virtualindex;

    // cache miss. if use virtual tag, check whether in virtual tag.
    if (useVirtualTag) {
        for (int i = 0; i < VIRTUALSETASSOC; i++) {
            if (virtualValid[_set * VIRTUALSETASSOC + i]) {
                if (virtualTag[_set * VIRTUALSETASSOC + i] == _tag) {
                    // in virtual tag, then first update the virtual tag flfu (-1)
                    // then check whether in cache has invalid or flfu less than this
                    // if has, then put this into cache. if the replaced one is not
                    // invalid, then put it into virtual tag.
                    invirtualtag = 1;
                    virtualindex = i;
                    virtuallfubit[_set * VIRTUALSETASSOC + i]--;
                }
            }
        }
    }

    for (int i = 0; i < SETASSOC; i++) {
        if (!Valid[_set * SETASSOC + i]) {
            // if has invalid slot, use it without other considerations
            replacelfu = -1;
            replaceindex = i;
            break;
        } else {
            int tmplfu = getlfubit(_set, i);

            if (tmplfu < replacelfu) {
                replacelfu = tmplfu;
                replaceindex = i;
            }
        }
    }

    if (!useVirtualTag) {
        // has invalid slot, fill
        if (replacelfu == -1) {
            Valid[_set * SETASSOC + replaceindex] = 1;
            Tag[_set * SETASSOC + replaceindex] = _tag;
            Cnt[_set * SETASSOC + replaceindex] = fibercnt - 1;
            if (!isfirst) {
                PosOrig[_set * SETASSOC + replaceindex] = getOrig(firstaddr);
            } else {
                PosOrig[_set * SETASSOC + replaceindex] = 0;
            }
            initPracticalLFU(_set, replaceindex, 0);
            return;
        }

        // has 0 slot, replace
        if (replacelfu == 0) {
            Valid[_set * SETASSOC + replaceindex] = 1;
            Tag[_set * SETASSOC + replaceindex] = _tag;
            Cnt[_set * SETASSOC + replaceindex] = fibercnt - 1;
            if (!isfirst) {
                PosOrig[_set * SETASSOC + replaceindex] = getOrig(firstaddr);
            } else {
                PosOrig[_set * SETASSOC + replaceindex] = 0;
            }
            initPracticalLFU(_set, replaceindex, 0);
            return;
        }

        // else, don't change the cache
        return;
    } else {
        // use virtual tag
        if (invirtualtag) {
            // has invalid slot, fill, put the virtual tag slot to invalid
            if (replacelfu == -1) {
                // put current slot into cache
                Valid[_set * SETASSOC + replaceindex] = 1;
                Tag[_set * SETASSOC + replaceindex] = _tag;
                Cnt[_set * SETASSOC + replaceindex] = fibercnt - 1;
                if (!isfirst) {
                    PosOrig[_set * SETASSOC + replaceindex] = getOrig(firstaddr);
                } else {
                    PosOrig[_set * SETASSOC + replaceindex] = 0;
                }
                initPracticalLFU(_set, replaceindex, virtuallfubit[_set * VIRTUALSETASSOC + virtualindex]);

                // put current virtual tag to invalid
                virtualValid[_set * VIRTUALSETASSOC + virtualindex] = 0;
                return;
            }

            // a slot in cache has lfu less then this in virtual. replace.
            if (replacelfu < virtuallfubit[_set * VIRTUALSETASSOC + virtualindex]) {
                // update metadata in cache (config to the current access)
                Valid[_set * SETASSOC + replaceindex] = 1;
                int oldtag = Tag[_set * SETASSOC + replaceindex];
                Tag[_set * SETASSOC + replaceindex] = _tag;
                Cnt[_set * SETASSOC + replaceindex] = fibercnt - 1;
                // int oldorig = PosOrig[_set * SETASSOC + replaceindex];
                if (!isfirst) {
                    PosOrig[_set * SETASSOC + replaceindex] = getOrig(firstaddr);
                } else {
                    PosOrig[_set * SETASSOC + replaceindex] = 0;
                }
                initPracticalLFU(_set, replaceindex, virtuallfubit[_set * VIRTUALSETASSOC + virtualindex]);

                // update metadata in virtual tag (config to the old slot in cache)
                virtualValid[_set * VIRTUALSETASSOC + virtualindex] = 1;
                virtualTag[_set * VIRTUALSETASSOC + virtualindex] = oldtag;
                virtuallfubit[_set * VIRTUALSETASSOC + virtualindex] = replacelfu;
            }
        } else { // not in cache; not in virtual tag

            // has invalid slot, fill
            if (replacelfu == -1) {
                Valid[_set * SETASSOC + replaceindex] = 1;
                Tag[_set * SETASSOC + replaceindex] = _tag;
                Cnt[_set * SETASSOC + replaceindex] = fibercnt - 1;
                if (!isfirst) {
                    PosOrig[_set * SETASSOC + replaceindex] = getOrig(firstaddr);
                } else {
                    PosOrig[_set * SETASSOC + replaceindex] = 0;
                }
                initPracticalLFU(_set, replaceindex, 0);
                return;
            }

            // has 0 slot, replace
            if (replacelfu == 0) {
                Valid[_set * SETASSOC + replaceindex] = 1;
                Tag[_set * SETASSOC + replaceindex] = _tag;
                Cnt[_set * SETASSOC + replaceindex] = fibercnt - 1;
                if (!isfirst) {
                    PosOrig[_set * SETASSOC + replaceindex] = getOrig(firstaddr);
                } else {
                    PosOrig[_set * SETASSOC + replaceindex] = 0;
                }

                initPracticalLFU(_set, replaceindex, 0);
                return;
            }

            // else, check whether can place into the virtual tag
            // first put into invalid slot, if there is no invalid slot, then put into
            // lfu=0 slot, if there is no lfu=0 slot, then do nothing
            for (int i = 0; i < VIRTUALSETASSOC; i++) {
                if (!virtualValid[_set * VIRTUALSETASSOC + i]) {
                    // has an invalid slot, put here and return (don't need to check other
                    // slots)
                    virtualValid[_set * VIRTUALSETASSOC + i] = 1;
                    virtualTag[_set * VIRTUALSETASSOC + i] = _tag;
                    virtuallfubit[_set * VIRTUALSETASSOC + i] = 0;
                    return;
                } else {
                }
            }
            for (int i = 0; i < VIRTUALSETASSOC; i++) {
                if (!virtualValid[_set * VIRTUALSETASSOC + i]) {
                } else {
                    // valid
                    if (virtuallfubit[_set * VIRTUALSETASSOC + i] == 0) {
                        // if the flfu bit is 0, replace it. (according to lru, the current
                        // is better)
                        virtualValid[_set * VIRTUALSETASSOC + i] = 1;
                        virtualTag[_set * VIRTUALSETASSOC + i] = _tag;
                        virtuallfubit[_set * VIRTUALSETASSOC + i] = 0;

                        return;
                    }
                    // else, can't do any operation
                }
            }
            return;
        }
    }
}

int hitcnt;
int misscnt;
long long totalhit;
long long totalaccess;


int sa_iteration_k = 0;
f64 current_prefetch_size;
f64 previous_prefetch_size;

f64 last_iteration_data_miss_rate = 1.0;
f64 best_data_miss_rate = 1.0;
long long elements_processed_since_last_adjustment = 0;
long long adjustment_interval;

long long prefetch_discards = 0;
long long prefetch_increments = 0;
long long data_access_hit = 0;
long long data_access_total = 0;

bool cacheRead(long long addr)
{
    totalaccess++;
    data_access_total++;

    if (cacheHit(addr)) {
        // cache hit!

        totalhit++;
        data_access_hit++;
        // sram read
        computeSramAccess += sramReadBandwidth(CACHE_BLOCK_NELEMS);
        hitcnt++;
        return 1;
    } else {
        // cache miss
        // dram load
        computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        // sram write
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);

        computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);

        // update cache status
        cacheReplace(addr);

        misscnt++;
        return 0;
    }
}

// need to define the OPT next access as negative;
// because the LRU choose the smallest one
#define ReplaceMAX -2147483647

// need a queue for each row to track all the nexposes in the prefetch window.
// the number track in this queue is the I number of A
queue<int> *nextposvector = nullptr;

int getNextpos(int rowid, int ii) {

    // pop the current and previous (not possible)
    while ((!nextposvector[rowid].empty()) &&
           (nextposvector[rowid].front() == -ii)) {
        nextposvector[rowid].pop();
    }

    // return the next access time
    if (!nextposvector[rowid].empty()) {
        return nextposvector[rowid].front();
    }

    // return replaceMAX when there isn't a next access in the current window
    return ReplaceMAX;
}

int getLFU(int rowid, int /* ii */) {
    int retlfu = LFUtag[rowid];
    LFUtag[rowid]--;
    return retlfu;
}

bool cacheReadOPT(long long addr, int nextpos) {

    totalaccess++;
    data_access_total++;

    // cache hit!
    if (cacheHitOPT(addr, nextpos)) {

        totalhit++;
        data_access_hit++;
        // sram read
        computeSramAccess += sramReadBandwidth(CACHE_BLOCK_NELEMS);
        return 1;
    }
    // cache miss
    else {
        // dram load
        computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        // sram write
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);

        computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);

        // update cache status
        cacheReplaceOPT(addr, nextpos);
        return 0;
    }
}

bool cacheReadOPTLFU(long long addr, int lfutime) {
    totalaccess++;
    data_access_total++;
    // cache hit!
    if (cacheHitOPTLFU(addr, lfutime)) {
        totalhit++;
        data_access_hit++;
        // sram read
        computeSramAccess += sramReadBandwidth(CACHE_BLOCK_NELEMS);
        return 1;
    }
    // cache miss
    else {
        // dram load
        computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        // sram write
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
        computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        // update cache status
        cacheReplaceOPTLFU(addr, lfutime);
        return 0;
    }
}

// no longer has the lfutime para (all in the lfubit)
// no longer any lfutime in this level (all lfu bit is calculated in the tag)
bool cacheReadPracticalLFU(long long addr, bool isfirst, long long firstaddr) {
    totalaccess++;
    data_access_total++;

    // cache hit!
    if (cacheHitPracticalLFU(addr, isfirst, firstaddr)) {
        totalhit++;
        data_access_hit++;
        // sram read
        computeSramAccess += sramReadBandwidth(CACHE_BLOCK_NELEMS);
        return 1;
    }
    // cache miss
    else {
        // dram load
        computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        // sram write
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
        computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        // update cache status
        cacheReplacePracticalLFU(addr, isfirst, firstaddr);
        return 0;
    }
}

void initializeCacheValid() {
    memset(Valid, 0, sizeof(bool) * CACHE_NSETS * SETASSOC);
    if (useVirtualTag) {
        memset(virtualValid, 0, sizeof(bool) * CACHE_NSETS * VIRTUALSETASSOC);
    }
    memset(PosOrig, 0, sizeof(short) * CACHE_NSETS * SETASSOC);
}

// ii here means the now access position for OPT policy
__attribute__((noinline)) void cacheAccessFiber(int jj, int fibersize, int ii) {

    // fiber + cut + whole
    // only cache the part within a cacheline (x-cache)
    if (cacheScheme == CACHE_SCHEME_BASE) {
        // if the whole size exceed the cacheline, then the rest part miss
        long long tmpaddr = getCacheAddr(jj, 0);

        bool tmphit = cacheRead(tmpaddr);

        // the exceed part will miss anyway
        if (fibersize > CACHE_BLOCK_NELEMS) {
            // int loadsize = fibersize - CACHEBLOCK;
            int loadsize =
                (1 + ((fibersize - CACHE_BLOCK_NELEMS - 1) / CACHE_BLOCK_NELEMS)) * CACHE_BLOCK_NELEMS;
            totalaccess += (1 + ((fibersize - CACHE_BLOCK_NELEMS - 1) / CACHE_BLOCK_NELEMS));
            // dram load
            computeDramAccess += memoryBandwidthPE(loadsize);
            // sram write
            computeSramAccess += sramWriteBandwidth(loadsize);
            computeB += memoryBandwidthPE(loadsize);
            tmphit = 0;
        }
        // someblock miss, need to access the dram metadata
        // need to know where to fetch the dram fiber first before the fetching
        if (!tmphit) {
            computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
            // sram write
            computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
            computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        }
    }

    // fiber + split + whole
    // split to multiple consective cachelines when exceed cacheline size
    if (cacheScheme == CACHE_SCHEME_MAPPING) {
        // for each BLOCK segment of the B fiber

        // will be set to 1 if any cacheblock is miss
        // (need extra dram access)
        bool anymiss = 0;
        for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHE_BLOCK_NELEMS) {

            // the address alters in different cache schemes
            long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHE_BLOCK_NELEMS);

            // the read granularity alters in different cache schemes

            bool tmphit = cacheRead(tmpaddr);

            if (!tmphit) {
                anymiss = 1;
            }
        }

        // someblock miss, need to access the dram metadata
        if (anymiss) {
            computeDramAccess += memoryBandwidthPE(16);
            computeSramAccess += sramWriteBandwidth(16);
            computeB += memoryBandwidthPE(16);
        }
    }

    // addr + split + whole
    if (cacheScheme == 4) {
        // the tmpaddr here is the address in dram.
        // the begin address of this row
        // need:
        // 1) minus the tag size at the beginning
        //     before each time the tiling size is fixed
        // 2) add the extra acecss each time acecss a line
        //      here at each single fiber access
        int tmpaddr = offsetarrayB[jj] * 3;
        // add the current bias of this row
        tmpaddr += sim.cursor.B.begins[jj - TJ] * 3;
        tmpaddr += sim.cfg.J;

        // need to read a whole line here
        // a for loop for each related cacheline. (may more then scehme1)

        // the begin block is the block which contains the start tmpaddr
        int beginaddr = tmpaddr - (tmpaddr % CACHE_BLOCK_NELEMS);
        // the start addr of the end block of the fiber
        // int endaddr = (tmpaddr+fibersize)-((tmpaddr+fibersize-1)%CACHEBLOCK+1);
        int endaddr = tmpaddr + fibersize;

        bool srammetahit = 0;

        // if not have a buffer or miss in the buffer
        if (!srammetahit) {
            srammetahit = cacheRead(jj);
        }

        bool anymiss = 0;

        for (int tmpcurr = beginaddr; tmpcurr < endaddr; tmpcurr += CACHE_BLOCK_NELEMS) {

            bool tmphit = cacheRead(tmpcurr);

            if (!tmphit) {
                anymiss = 1;
            }
        }

        // someblock miss, need to access the dram metadata
        if (anymiss && (!srammetahit)) {

            computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
            // sram write
            computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
            computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        }
    }

    // Sparch
    // scheme 1 + OPT
    if (cacheScheme == 6) {
        // for each BLOCK segment of the B fiber

        // should get the next pos here (same in each )
        // send the now I ii
        int nextpos = getNextpos(jj, ii);
        // access the head pointer
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
        bool anymiss = 0;

        for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHE_BLOCK_NELEMS) {

            // the address alters in different cache schemes
            long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHE_BLOCK_NELEMS);
            // the read granularity alters in different cache schemes
            bool tmphit = cacheReadOPT(tmpaddr, nextpos);
            if (!tmphit) {
                anymiss = 1;
            }
        }

        // someblock miss, need to access the dram metadata
        if (anymiss) {
            computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
            // sram write
            computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
            computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        }
    }

    // InnerSP
    // scheme0 + static OPT
    if (cacheScheme == CACHE_SCHEME_INNER_SP) {
        int nextpos = getNextpos(jj, ii);
        // access the head pointer
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
        // bool anymiss = 0;

        long long tmpaddr = getCacheAddr(jj, 0);
        bool tmphit = cacheReadOPT(tmpaddr, nextpos);
        // the exceed part will miss anyway
        if (fibersize > CACHE_BLOCK_NELEMS) {
            // int loadsize = fibersize - CACHEBLOCK;
            int loadsize =
                (1 + ((fibersize - CACHE_BLOCK_NELEMS - 1) / CACHE_BLOCK_NELEMS)) * CACHE_BLOCK_NELEMS;
            totalaccess += (1 + ((fibersize - CACHE_BLOCK_NELEMS - 1) / CACHE_BLOCK_NELEMS));
            // dram load
            computeDramAccess += memoryBandwidthPE(loadsize);
            // sram write
            computeSramAccess += sramWriteBandwidth(loadsize);
            computeB += memoryBandwidthPE(loadsize);
            tmphit = 0;
        }
        // someblock miss, need to access the dram metadata
        // need to know where to fetch the dram fiber first before the fetching
        if (!tmphit) {
            computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
            // sram write
            computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
            computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        }
    }

    // Sparch
    // scheme0 + dynamic OPT
    if (cacheScheme == CACHE_SCHEME_SPARCH) {
        int nextpos = getNextpos(jj, ii);
        // access the head pointer
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
        // bool anymiss = 0;

        long long tmpaddr = getCacheAddr(jj, 0);
        bool tmphit = cacheReadOPT(tmpaddr, nextpos);
        // the exceed part will miss anyway
        if (fibersize > CACHE_BLOCK_NELEMS) {
            // int loadsize = fibersize - CACHEBLOCK;
            int loadsize =
                (1 + ((fibersize - CACHE_BLOCK_NELEMS - 1) / CACHE_BLOCK_NELEMS)) * CACHE_BLOCK_NELEMS;
            totalaccess += (1 + ((fibersize - CACHE_BLOCK_NELEMS - 1) / CACHE_BLOCK_NELEMS));
            // dram load
            computeDramAccess += memoryBandwidthPE(loadsize);
            // sram write
            computeSramAccess += sramWriteBandwidth(loadsize);
            computeB += memoryBandwidthPE(loadsize);
            tmphit = 0;
        }
        // someblock miss, need to access the dram metadata
        // need to know where to fetch the dram fiber first before the fetching
        if (!tmphit) {
            computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
            // sram write
            computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
            computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        }
    }

    // 66 referes to 6 + LFU prefetch + hybrid bit (fewer hardware cost)
    if (cacheScheme == 66) {

        // use the getNextposLFU to get the LFU
        int lfutime = getLFU(jj, ii);
        // access the head pointer
        computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);
        bool anymiss = 0;
        for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHE_BLOCK_NELEMS) {
            long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHE_BLOCK_NELEMS);
            bool tmphit = cacheReadOPTLFU(tmpaddr, lfutime);
            if (!tmphit) {
                anymiss = 1;
            }
        }
        if (anymiss) {
            computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
            computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);

            computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        }
    }

    // 88 refers to the practical FLFU (enabling 4-bit, virtual tag)  (virtual
    // tag can be configured or not (baseline)) the flu information is no longer
    // kept in the LFUtag, but the extra lfubit
    if (cacheScheme == CACHE_SCHEME_FLFU) {
        bool anymiss = 0;
        fibersize = sim.cursor.B.sizes[jj - TJ] * 3;
        for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHE_BLOCK_NELEMS) {
            long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHE_BLOCK_NELEMS);
            bool tmphit = cacheReadPracticalLFU(tmpaddr, tmpcurr == 0, getCacheAddr(jj, 0));
            if (!tmphit) {
                anymiss = 1;
            }
        }
        if (anymiss) {
            computeDramAccess += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
            computeSramAccess += sramWriteBandwidth(CACHE_BLOCK_NELEMS);

            computeB += memoryBandwidthPE(CACHE_BLOCK_NELEMS);
        }
    }
}

// (re-)allocate memory dynamically
u64 last_cache_nsets = 0;
void initialize_cache()
{
    b32 need_realloc    = (CACHE_NSETS != last_cache_nsets);
    last_cache_nsets    = CACHE_NSETS;

    u64 nblocks         = CACHE_NSETS*SETASSOC;
    u64 virtual_nblocks = CACHE_NSETS*VIRTUALSETASSOC;

    if (need_realloc) {
        arena_clear(cache.backing);
        Valid          = (b8  *)arena_push(cache.backing, nblocks         * sizeof(*Valid),          __alignof__(*Valid),          1);
        Tag            = (i32 *)arena_push(cache.backing, nblocks         * sizeof(*Tag),            __alignof__(*Tag),            0);
        lrubit         = (i32 *)arena_push(cache.backing, nblocks         * sizeof(*lrubit),         __alignof__(*lrubit),         0);
        lfubit         = (i32 *)arena_push(cache.backing, nblocks         * sizeof(*lfubit),         __alignof__(*lfubit),         0);

        virtualValid   = (b8  *)arena_push(cache.backing, virtual_nblocks * sizeof(*virtualValid),   __alignof__(*virtualValid),   1);
        virtualTag     = (i32 *)arena_push(cache.backing, virtual_nblocks * sizeof(*virtualTag),     __alignof__(*virtualTag),     0);
        virtuallfubit  = (i32 *)arena_push(cache.backing, virtual_nblocks * sizeof(*virtuallfubit),  __alignof__(*virtuallfubit),  0);

        PosOrig        = (u16 *)arena_push(cache.backing, nblocks         * sizeof(*PosOrig),        __alignof__(*PosOrig),        1);

        Cnt            = (u8  *)arena_push(cache.backing, nblocks         * sizeof(*Cnt),            __alignof__(*Cnt),            1);
    } else {
        memset(Valid, 0, nblocks*sizeof(*Valid));
        memset(virtualValid, 0, nblocks*sizeof(*Valid));
    }
}
