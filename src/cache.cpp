#include "cache.h"
#include "headers.h"
#include "statistics.h"
#include "util.h"

int cachesize = 262144;

int CACHEBLOCK = 16;
int CACHEBLOCKLOG = 4;

double longpart = 0;
double shortpart = 0;

double indpart = 0;
double datapart = 0;

int inputcachesize;

int SET = cachesize / (CACHEBLOCK * SETASSOC);
int SETLOG = getlog(SET);

// Params: cachesize, cacheblock
void setSET() {
  SET = (cachesize) / (CACHEBLOCK * SETASSOC);
  SETLOG = getlog(SET);
  initialize_cache();
}

bool *Valid = nullptr;
int *Tag = nullptr;
int *lrubit = nullptr;

int *lfubit = nullptr;

bool *virtualValid = nullptr;
int *virtualTag = nullptr;
int *virtuallfubit = nullptr;

bool Valid4[256];
int Tag4[256];
int lrubit4[256];

int LFUbit = 4;
int LFUmax = (1 << LFUbit) - 1;
int *LFUtag = nullptr;

// split into 4 parts.  witin 16: 0000, 0001, 0010,,,,  1111
// short *partialValid = nullptr;

// for the pack&split
const int N_TAG_L_BITS = 0; // Tag-L bits

unsigned char *Cnt = nullptr;
bool *Next = nullptr;
unsigned short *PosOrig = nullptr;

unsigned short *vPosOrig = nullptr;

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
//                  start addr     exceed part      not full part
// cache Scheme 0       fiber       cut                 whole
// cache Scheme 1       fiber       split               whole
int cacheScheme;

long long getCacheAddr(int fiberid, int relative) {
  long long ret;

  ret = (((long long)fiberid) << CACHEBLOCKLOG);
  if (relative) {
    ret += (((long long)relative) << (CACHEBLOCKLOG + BIAS));
  }

  return ret;
}

// mapping: tag | set index | offset within cacheblock
unsigned int getSet(long long addr) { return (addr >> (CACHEBLOCKLOG)) % SET; }

unsigned int getTag(long long addr) {
  return (addr >> (CACHEBLOCKLOG + SETLOG));
}

// mapping: tag-H | set index | tag-L | offset within cacheblock
unsigned int getSet2(long long addr) {
  return (addr >> (CACHEBLOCKLOG + N_TAG_L_BITS)) % SET;
}

unsigned int getTag2(long long addr) {
  long long fiberId = addr >> CACHEBLOCKLOG;
  long long tag_h = fiberId >> (N_TAG_L_BITS + SETLOG);
  long long tag_l = fiberId & ((1 << N_TAG_L_BITS) - 1);
  int _tag = (tag_h << N_TAG_L_BITS) | tag_l;
  return _tag;
}

int getSetPS(long long fiberId) {
  return (fiberId >> N_TAG_L_BITS) & ((1 << SETLOG) - 1);
}

long long getTagPS(long long fiberId) {
  long long tag_h = fiberId >> (N_TAG_L_BITS + SETLOG);
  long long tag_l = fiberId & ((1 << N_TAG_L_BITS) - 1);
  return (tag_h << N_TAG_L_BITS) | tag_l;
}

unsigned short getOrig(long long addr) {
  return (addr >> CACHEBLOCKLOG) & 0xFFFF;
}

// = 0 when don't use virtual tag
// = 1 when use virtual tag
bool useVirtualTag = 1;

int getLRU(int _set, int _index) { return lrubit[_set * SETASSOC + _index]; }
int getlfubit(int _set, int _index) { return lfubit[_set * SETASSOC + _index]; }

void updateLRU(int _set, int _index) {
  cachecycle++;
  lrubit[_set * SETASSOC + _index] = cachecycle;
}

void updateLRUOPT(int _set, int _index, int nextpos) {
  lrubit[_set * SETASSOC + _index] = nextpos;
}

void updateLRUOPTLFU(int _set, int _index, int lfutime) {
  lrubit[_set * SETASSOC + _index] = lfutime;
}

void updatePracticalLFU(int _set, int _index) {
  if (lfubit[_set * SETASSOC + _index]) {
    lfubit[_set * SETASSOC + _index]--;
  }
}

void initLRU(int _set, int _index) {
  // play the same as updateLRU in LRU policy
  cachecycle++;
  lrubit[_set * SETASSOC + _index] = cachecycle;
}

void initLRUOPT(int _set, int _index, int nextpos) {
  lrubit[_set * SETASSOC + _index] = nextpos;
}

void initLRUOPTLFU(int _set, int _index, int LFUtime) {
  lrubit[_set * SETASSOC + _index] = LFUtime;
}

void initPracticalLFU(int _set, int _index, int LFUtime) {
  lfubit[_set * SETASSOC + _index] = LFUtime;
}

bool cacheHit(long long addr) {
  int _set = getSet(addr);
  int _tag = getTag(addr);

  for (int i = 0; i < SETASSOC; i++) {
    if (Valid[_set * SETASSOC + i] && (Tag[_set * SETASSOC + i] == _tag)) {
      // hit !!

      // update lru bit
      updateLRU(_set, i);
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
      // hit !!
      updateLRUOPT(_set, i, nextpos);
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
      // hit !!
      updateLRUOPTLFU(_set, i, lfutime);
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
        // update without lfutime
        updatePracticalLFU(_set, i);
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

  initLRU(_set, replaceindex);
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

  initLRUOPT(_set, replaceindex, nextpos);
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

  initLRUOPTLFU(_set, replaceindex, LFUtime);
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

    int fiberid = addr >> CACHEBLOCKLOG;
    int tmpblocksize = CACHEBLOCK;
    tmpblocksize -= currsizeB[fiberid] * 3;
    while (tmpblocksize > 0 && (fiberid + fibercnt < TJ + jjj)) {
      if (currsizeB[fiberid + fibercnt] * 3 <= tmpblocksize) {
        tmpblocksize -= currsizeB[fiberid + fibercnt] * 3;
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
        vPosOrig[_set * SETASSOC + virtualindex] = 0;
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

const int SA_MAX_ITERATIONS = 500;
const double SA_INITIAL_TEMP = 0.02;
const double SA_COOLING_RATE = 0.99;
const double RATE_THRESHOLD = 0.20;

int sa_iteration_k = 0;
double current_prefetch_size;
double previous_prefetch_size;
double best_prefetch_size;

double last_iteration_data_miss_rate = 1.0;
double best_data_miss_rate = 1.0;
long long elements_processed_since_last_adjustment = 0;
long long adjustment_interval;

long long prefetch_discards = 0;
long long prefetch_increments = 0;
long long data_access_misses = 0;
long long data_access_hit = 0;
long long data_access_total = 0;

bool cacheRead(long long addr) {

  totalaccess++;
  data_access_total++;

  // cache hit!
  if (cacheHit(addr)) {

    totalhit++;
    data_access_hit++;
    // sram read
    computeSramAccess += sramReadBandwidth(CACHEBLOCK);
    hitcnt++;
    return 1;
  }
  // cache miss
  else {
    // dram load
    computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
    // sram write
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);

    computeB += memoryBandwidthPE(CACHEBLOCK);

    // update cache status
    cacheReplace(addr);

    misscnt++;
    return 0;
  }
}

void cacheRead(long long addr, int length) {

  totalaccess++;
  data_access_total++;

  // the begin of this cacheblock
  addr = addr - (addr % CACHEBLOCK);

  // cache hit!
  if (cacheHit(addr)) {

    totalhit++;
    data_access_hit++;
    // sram read
    computeSramAccess += sramReadBandwidth(length);
    hitcnt++;
  }
  // cache miss
  else {
    // dram load
    computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
    // sram write
    computeSramAccess += sramWriteBandwidth(length);
    totalB += memoryBandwidthPE(CACHEBLOCK);
    // update cache status
    cacheReplace(addr);
    misscnt++;
  }
}

void cacheEvict(long long addr) {
  // cache hit! evict
  if (cacheHit(addr)) {
    int _set = getSet(addr);
    int _tag = getTag(addr);

    // set the valid to 0
    for (int i = 0; i < SETASSOC; i++) {
      if (Valid[_set * SETASSOC + i] && (Tag[_set * SETASSOC + i] == _tag)) {
        Valid[_set * SETASSOC + i] = 0;
      }
    }
  }
  // cache miss, don't need to evict
  else {
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
    computeSramAccess += sramReadBandwidth(CACHEBLOCK);
    return 1;
  }
  // cache miss
  else {
    // dram load
    computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
    // sram write
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);

    computeB += memoryBandwidthPE(CACHEBLOCK);

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
    computeSramAccess += sramReadBandwidth(CACHEBLOCK);
    return 1;
  }
  // cache miss
  else {
    // dram load
    computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
    // sram write
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
    computeB += memoryBandwidthPE(CACHEBLOCK);
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
    computeSramAccess += sramReadBandwidth(CACHEBLOCK);
    return 1;
  }
  // cache miss
  else {
    // dram load
    computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
    // sram write
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
    computeB += memoryBandwidthPE(CACHEBLOCK);
    // update cache status
    cacheReplacePracticalLFU(addr, isfirst, firstaddr);
    return 0;
  }
}

void initializeCacheValid() {
  memset(Valid, 0, sizeof(bool) * SET * SETASSOC);
  if (useVirtualTag) {
    memset(virtualValid, 0, sizeof(bool) * SET * VIRTUALSETASSOC);
  }
  memset(PosOrig, 0, sizeof(short) * SET * SETASSOC);
  memset(vPosOrig, 0, sizeof(short) * SET * SETASSOC);
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
    if (fibersize > CACHEBLOCK) {
      // int loadsize = fibersize - CACHEBLOCK;
      int loadsize =
          (1 + ((fibersize - CACHEBLOCK - 1) / CACHEBLOCK)) * CACHEBLOCK;
      totalaccess += (1 + ((fibersize - CACHEBLOCK - 1) / CACHEBLOCK));
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
      computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
      // sram write
      computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
      computeB += memoryBandwidthPE(CACHEBLOCK);
    }
  }

  // fiber + split + whole
  // split to multiple consective cachelines when exceed cacheline size
  if (cacheScheme == CACHE_SCHEME_MAPPING) {
    // for each BLOCK segment of the B fiber

    // will be set to 1 if any cacheblock is miss
    // (need extra dram access)
    bool anymiss = 0;
    for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHEBLOCK) {

      // the address alters in different cache schemes
      long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHEBLOCK);

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
    tmpaddr += beginB[jj] * 3;
    tmpaddr += J;

    // need to read a whole line here
    // a for loop for each related cacheline. (may more then scehme1)

    // the begin block is the block which contains the start tmpaddr
    int beginaddr = tmpaddr - (tmpaddr % CACHEBLOCK);
    // the start addr of the end block of the fiber
    // int endaddr = (tmpaddr+fibersize)-((tmpaddr+fibersize-1)%CACHEBLOCK+1);
    int endaddr = tmpaddr + fibersize;

    bool srammetahit = 0;

    // if not have a buffer or miss in the buffer
    if (!srammetahit) {
      srammetahit = cacheRead(jj);
    }

    bool anymiss = 0;

    for (int tmpcurr = beginaddr; tmpcurr < endaddr; tmpcurr += CACHEBLOCK) {

      bool tmphit = cacheRead(tmpcurr);

      if (!tmphit) {
        anymiss = 1;
      }
    }

    // someblock miss, need to access the dram metadata
    if (anymiss && (!srammetahit)) {

      computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
      // sram write
      computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
      computeB += memoryBandwidthPE(CACHEBLOCK);
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
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
    bool anymiss = 0;

    for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHEBLOCK) {

      // the address alters in different cache schemes
      long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHEBLOCK);
      // the read granularity alters in different cache schemes
      bool tmphit = cacheReadOPT(tmpaddr, nextpos);
      if (!tmphit) {
        anymiss = 1;
      }
    }

    // someblock miss, need to access the dram metadata
    if (anymiss) {
      computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
      // sram write
      computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
      computeB += memoryBandwidthPE(CACHEBLOCK);
    }
  }

  // InnerSP
  // scheme0 + static OPT
  if (cacheScheme == CACHE_SCHEME_INNER_SP) {
    int nextpos = getNextpos(jj, ii);
    // access the head pointer
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
    // bool anymiss = 0;

    long long tmpaddr = getCacheAddr(jj, 0);
    bool tmphit = cacheReadOPT(tmpaddr, nextpos);
    // the exceed part will miss anyway
    if (fibersize > CACHEBLOCK) {
      // int loadsize = fibersize - CACHEBLOCK;
      int loadsize =
          (1 + ((fibersize - CACHEBLOCK - 1) / CACHEBLOCK)) * CACHEBLOCK;
      totalaccess += (1 + ((fibersize - CACHEBLOCK - 1) / CACHEBLOCK));
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
      computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
      // sram write
      computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
      computeB += memoryBandwidthPE(CACHEBLOCK);
    }
  }

  // Sparch
  // scheme0 + dynamic OPT
  if (cacheScheme == CACHE_SCHEME_SPARCH) {
    int nextpos = getNextpos(jj, ii);
    // access the head pointer
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
    // bool anymiss = 0;

    long long tmpaddr = getCacheAddr(jj, 0);
    bool tmphit = cacheReadOPT(tmpaddr, nextpos);
    // the exceed part will miss anyway
    if (fibersize > CACHEBLOCK) {
      // int loadsize = fibersize - CACHEBLOCK;
      int loadsize =
          (1 + ((fibersize - CACHEBLOCK - 1) / CACHEBLOCK)) * CACHEBLOCK;
      totalaccess += (1 + ((fibersize - CACHEBLOCK - 1) / CACHEBLOCK));
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
      computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
      // sram write
      computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
      computeB += memoryBandwidthPE(CACHEBLOCK);
    }
  }

  // 66 referes to 6 + LFU prefetch + hybrid bit (fewer hardware cost)
  if (cacheScheme == 66) {

    // use the getNextposLFU to get the LFU
    int lfutime = getLFU(jj, ii);
    // access the head pointer
    computeSramAccess += sramWriteBandwidth(CACHEBLOCK);
    bool anymiss = 0;
    for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHEBLOCK) {
      long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHEBLOCK);
      bool tmphit = cacheReadOPTLFU(tmpaddr, lfutime);
      if (!tmphit) {
        anymiss = 1;
      }
    }
    if (anymiss) {
      computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
      computeSramAccess += sramWriteBandwidth(CACHEBLOCK);

      computeB += memoryBandwidthPE(CACHEBLOCK);
    }
  }

  // 88 refers to the practical FLFU (enabling 4-bit, virtual tag)  (virtual
  // tag can be configured or not (baseline)) the flu information is no longer
  // kept in the LFUtag, but the extra lfubit
  if (cacheScheme == CACHE_SCHEME_FLFU) {
    bool anymiss = 0;
    fibersize = currsizeB[jj] * 3;
    for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHEBLOCK) {
      long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHEBLOCK);
      bool tmphit = cacheReadPracticalLFU(tmpaddr, (tmpcurr == 0) ? 1 : 0,
                                          getCacheAddr(jj, 0));
      if (!tmphit) {
        anymiss = 1;
      }
    }
    if (anymiss) {
      computeDramAccess += memoryBandwidthPE(CACHEBLOCK);
      computeSramAccess += sramWriteBandwidth(CACHEBLOCK);

      computeB += memoryBandwidthPE(CACHEBLOCK);
    }
  }
}

// (re-)allocate memory dynamically
int last_cache_set = 0;
void initialize_cache() {
  if(SET != last_cache_set) {
    deinitialize_cache();
    last_cache_set = SET;
  }
  try {
    Valid = new bool[SET * SETASSOC]();
    Tag = new int[SET * SETASSOC]();
    lrubit = new int[SET * SETASSOC]();
    lfubit = new int[SET * SETASSOC]();

    virtualValid = new bool[SET * VIRTUALSETASSOC]();
    virtualTag = new int[SET * VIRTUALSETASSOC]();
    virtuallfubit = new int[SET * VIRTUALSETASSOC]();

    PosOrig = new unsigned short[SET * SETASSOC]();
    vPosOrig = new unsigned short[SET * SETASSOC]();

    Cnt = new unsigned char[SET * SETASSOC]();
    Next = new bool[SET * SETASSOC]();
  } catch (const std::bad_alloc &e) {
    std::cerr << "Error allocating memory for " << e.what() << std::endl;
    std::exit(1);
  }
}

void deinitialize_cache() {
    if(Valid != nullptr) delete[] Valid;
    if(Tag != nullptr) delete[] Tag;
    if(lrubit != nullptr) delete[] lrubit;
    if(lfubit != nullptr) delete[] lfubit;

    if(virtualValid != nullptr) delete[] virtualValid;
    if(virtualTag != nullptr) delete[] virtualTag;
    if(virtuallfubit != nullptr) delete[] virtuallfubit;

    if(PosOrig != nullptr) delete[] PosOrig;
    if(vPosOrig != nullptr) delete[] vPosOrig;

    if(Cnt != nullptr) delete[] Cnt;
    if(Next != nullptr) delete[] Next;

    // (re)set all the pointers to nullptr
    Valid = nullptr;
    Tag = nullptr;
    lrubit = nullptr;
    lfubit = nullptr;

    virtualValid = nullptr;
    virtualTag = nullptr;
    virtuallfubit = nullptr;

    PosOrig = nullptr;
    vPosOrig = nullptr;

    Cnt = nullptr;
    Next = nullptr;
}

