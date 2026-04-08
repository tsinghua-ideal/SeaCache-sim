#include <assert.h>

#include "cache.h"
#include "estimation.h"
#include "headers.h"
#include "statistics.h"

// store all the buffered C now
set<int> *bufferedC = nullptr;
// record length of buffered C
// equals bufferedC[i].size()
Coord *dirtyC = NULL;

/*
The currently buffered size of each array
(part of cuursizeA/B/C)

update bufferedsize each time
*/
int *bufferedsizeB = nullptr;
// int *bufferedsizeC = nullptr;

u8 *tmpC = nullptr;

// start of current block (NOTE(ejs): block == tile?)
int TI, TJ, TK;

// STAR: call this
// when: 1) start time 2) each time update I/J
// can over called by call each time
void compute_tile_luts(struct tile *t, b16 reset_begins)
{
    Coord   major_dim, minor_dim, major_idx, minor_idx;
    const Coord *offsets, **map;
    Coord *begins, *sizes;
    major_dim   = t->major_dim;
    minor_dim   = t->minor_dim;
    major_idx   = *t->major_tile_idx * t->major_dim;
    minor_idx   = *t->minor_tile_idx * t->minor_dim;
    map         = t->map;
    offsets     = t->offsets;
    begins      = t->begins;
    sizes       = t->sizes;

    memset(sizes, 0, major_dim*sizeof(*sizes));
    if (reset_begins)
        memset(begins, 0, major_dim*sizeof(*sizes));

    Coord abs_rj_lim= minor_idx + minor_dim;
    for (Coord ri = 0; ri < major_dim; ++ri) {
        Coord abs_ri        = major_idx + ri;
        Coord nzero_elem_cnt= offsets[abs_ri+1] - offsets[abs_ri];

        Coord rj        = begins[ri];
        while (rj < nzero_elem_cnt && map[abs_ri][rj] < minor_idx)
            ++rj;
        begins[ri] = rj;
        while (rj < nzero_elem_cnt && map[abs_ri][rj] < abs_rj_lim) {
            ++sizes[ri];
            ++rj;
        }
    }
}

void advance_cursor() // / tile pair
{
    // 35479 35479 35479
    struct cursor *c= &sim.cursor;
    if (c->first) {
        // printf("\n%u %u %u [first]\n", c->ti, c->tj, c->tk);
        c->first = 0;
        compute_tile_luts(&c->A, 1);
        compute_tile_luts(&c->B, 1);
        return;
    }

    // increment tile indices and check if either tile needs recomputing
    b16 Atile_need_recompute, A_reset_begins;
    b16 Btile_need_recompute, B_reset_begins;
    
    {
        Coord old_A_major_tile_idx = *c->A.major_tile_idx;
        Coord old_A_minor_tile_idx = *c->A.minor_tile_idx;
        Coord old_B_major_tile_idx = *c->B.major_tile_idx;
        Coord old_B_minor_tile_idx = *c->B.minor_tile_idx;
        {
            Coord   inner_p1    = *c->inner_tile_idx + 1;
            Coord   middle_p1   = *c->middle_tile_idx + 1;
            b16 inner_at_lim    = inner_p1 == c->inner_ntiles;
            b16 middle_at_lim   = middle_p1 == c->middle_ntiles;
            *c->inner_tile_idx  = inner_at_lim ? 0 : inner_p1;
            *c->middle_tile_idx = inner_at_lim ? (middle_at_lim ? 0 : middle_p1) : *c->middle_tile_idx;
            *c->outer_tile_idx  += (inner_at_lim && middle_at_lim);

            *c->inner_wrap      = inner_at_lim;
            *c->middle_wrap     = inner_at_lim && middle_at_lim;
            *c->outer_wrap      = 0; // outer never wraps
        }
        Atile_need_recompute= (old_A_major_tile_idx != *c->A.major_tile_idx) || (old_A_minor_tile_idx != *c->A.minor_tile_idx);
        Btile_need_recompute= (old_B_major_tile_idx != *c->B.major_tile_idx) || (old_B_minor_tile_idx != *c->B.minor_tile_idx);
        A_reset_begins = *c->A.minor_wrap || (old_A_major_tile_idx != *c->A.major_tile_idx);
        B_reset_begins = *c->B.minor_wrap || (old_B_major_tile_idx != *c->B.major_tile_idx);
    }
    

    if (Atile_need_recompute)
        compute_tile_luts(&c->A, A_reset_begins);
    if (Btile_need_recompute)
        compute_tile_luts(&c->B, B_reset_begins);
    // printf("%u %u %u (recompute, reset): A=(%d, %d), B=(%d, %d)\n", c->ti, c->tj, c->tk, Atile_need_recompute,A_reset_begins,Btile_need_recompute,B_reset_begins);
}

// then will take length+1 place for the next fiberlet pointer
const int fiberletlength = 4;

/*
The allocated buffer to an array (A/B/Csize)
(accordind to "partial" parameter and dataflow)
and the currently used part (A/B/Csizenow)
*/
int Asizenow;
int Bsizenow;
int Csizenow;

bool fulltagA, fulltagB, fulltagC;
int fullA, fullB;

bool checkAndLoadReuseA() {
    if ((interorder == IJK || interorder == JIK)) {
        // load inter-reuse A
        // the TK == 0 should get a ratio of inter-level cache hit (how many in the
        // buffer)

        // need to reaccess if the buffer can't hold the full A
        if (TK == 0) {

            Asizenow = 0;
            fulltagA = 0;
            if (format == RR || format == RC) {

                // on-chip fiber start

                for (int ti = TI; ti < min(TI + sim.cfg.iii, sim.cfg.I); ti++) {
                    Asizenow++;

                    int startj = sim.cursor.A.begins[ti - TI], tmpj = sim.cursor.A.begins[ti - TI],
                        maxj = offsetarrayA[ti + 1] - offsetarrayA[ti];

                    while (tmpj < maxj && A[ti][tmpj] < sim.cfg.jjj + TJ) {
                        tmpj++;
                    }

                    int tmpsize = (tmpj - startj);

                    // overflow the buffer size
                    // need to load again in rest tiles
                    if (Asizenow + tmpsize * 3 >= Asize) {
                        // outside the buffer, need reload every following tiles
                        if (!fulltagA) {
                            fulltagA = 1;
                            fullA = ti;
                        }
                    } else {
                        // inside the buffer, don't need to reload in following tiles
                        Asizenow += tmpsize * 3;
                        preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
                        preA += memoryBandwidthWhole(tmpsize * 3 + 2);
                        preSramAccess += sramWriteBandwidth(tmpsize * 3 + 2);
                        AccessByte += tmpsize * 3 + 2;
                    }
                }
            }

        }

        return 1;
    }

    // if interorder not IJK or JIK, can not buffer A whatever the buffersize
    // so just set the fullA to TI-1 (the first place)
    fulltagA = 1;
    fullA = TI - 1;

    return 0;
}

bool checkReuseB() {
    if ((interorder == JKI || interorder == KJI) && (TI != 0))
        return 1;
    return 0;
}

void pre_load_A() {

    if (dataflow == Outer) {
        fulltagA = 1;
        fullA = TJ;

        return;
    }

    // if reuse A, don't need to load again
    if (checkAndLoadReuseA()) {
        return;
    }

}

void pre_load_B() {

    // if reuse B, don't need to load again
    if (checkReuseB()) {
        return;
    }

    if (dataflow == Outer) {
        fulltagB = 1;
        fullB = TJ;

        return;
    }

    // When need load B:
    // 2 scenarios: 1) IP & Gust (need to buffer B)  2) When B storage format
    // mismatch with dataflow

    // need to consider with inter-block
    Bsizenow = 0;
    fulltagB = 0;
    fullB = 0;

    // scenario 1
    if ((dataflow == Inner) || (dataflow == Gust)) {

        // Gust
        Bsizenow = 0;

        // check fulltagA
        fulltagB = 0;
        fullB = 0;

        if ((dataflow == Gust) && ((format == CR) || (format == RR))) {
            // consistent format

            int tj;

            // equals to 0(when jjj in the first half) or 1(when jjj in the second
            // half);

            for (tj = TJ; tj < min(TJ + sim.cfg.jjj, sim.cfg.J); tj++) {
                // on-chip fiber start
                Bsizenow++;

                int startk = sim.cursor.B.begins[tj - TJ], tmpk = sim.cursor.B.begins[tj - TJ],
                    maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

                int halfk = sim.cursor.B.begins[tj - TJ];

                while (halfk < maxk && B[tj][halfk] < (sim.cfg.kkk / 2) + TK) {
                    halfk++;
                }
                tmpk = halfk;

                while (tmpk < maxk && B[tj][tmpk] < sim.cfg.kkk + TK) {
                    tmpk++;
                }

                int tmpsize = (tmpk - startk);

                if (Bsizenow + tmpsize * 3 >= Bsize) {
                    if (!fulltagB) {
                        fulltagB = 1;
                        fullB = tj;
                    }
                    //  sim.cursor.B.sizes[tj] = tmpsize;
                } else {

                    Bsizenow += tmpsize * 3;

                    preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
                    preB += memoryBandwidthWhole(tmpsize * 3 + 2);
                    preSramAccess += sramWriteBandwidth(tmpsize * 3 + 2);
                    AccessByte += tmpsize * 3 + 2;
                }
            }
        }

        if ((dataflow == Gust) && ((format == CC) || (format == RC))) {
            // inconsistent format
            // implicit transform while loading

            // Unlike the consistent version, inconsistent version
            // can't do this

            // estimated fiberlet fragment waste
            Bsizenow += ((fiberletlength + 1) / 2) * sim.cfg.jjj;

            // on-chip fiber current
            Bsizenow += sim.cfg.jjj;

            // Initialize
            for (int tj = TJ; tj < TJ + sim.cfg.jjj; tj++) {
                bufferedsizeB[tj] = 0;
            }

            for (int tk = TK; tk < min(TK + sim.cfg.kkk, sim.cfg.K); tk++) {
                int startj = sim.cursor.B.begins[tk - TK], tmpj = sim.cursor.B.begins[tk - TK],
                    maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

                while (tmpj < maxj) {
                    int tmpindex = Bc[tk][tmpj];
                    if (tmpindex >= TJ + sim.cfg.jjj) {
                        break;
                    }
                    tmpj++;

                    bufferedsizeB[tmpindex]++;
                }

                int tmpsize = (tmpj - startj);

                if (Bsizenow + tmpsize * 3 >= Bsize) {
                    if (!fulltagB) {
                        fulltagB = 1;
                        fullB = tk;
                    }
                    // cache the csc size of each col block
                    //   sim.cursor.B.sizes[tk] = tmpsize;
                } else {
                    //   sim.cursor.B.sizes[tk] = tmpsize;
                    preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
                    preB += memoryBandwidthWhole(tmpsize * 3 + 2);
                    AccessByte += tmpsize * 3 + 2;

                    // for each element need:
                    // 1) get pos: one read (current position)
                    // 2) add to chain: 1 data write; 1/block chain index write
                    preSramAccess += sramWriteBandwidth(tmpsize);
                    preSramAccess +=
                        sramReadBandwidth(tmpsize + tmpsize / fiberletlength) * 3;
                }
            }
        }

        // Inner

        if ((dataflow == Inner) && (((format == RC) || (format == CC)))) {
            // consistent format

            Bsizenow = 0;
            fulltagB = 0;
            fullB = 0;

            int tk;
            // 这里preload就是为了确定dynamic的
            // for(tk = TK; tk < TK+((ISDYNAMICK)?dynk:kkk); tk ++){
            for (tk = TK; tk < min(TK + sim.cfg.kkk, sim.cfg.K); tk++) {
                // on-chip fiber start
                Bsizenow++;

                int startj = sim.cursor.B.begins[tk - TK], tmpj = sim.cursor.B.begins[tk - TK],
                    maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

                while (tmpj < maxj && Bc[tk][tmpj] < sim.cfg.jjj + TJ) {
                    tmpj++;
                }

                int tmpsize = (tmpj - startj);

                if (Bsizenow + tmpsize * 3 >= Bsize) {
                    if (!fulltagB) {
                        fulltagB = 1;
                        fullB = tk;
                        // printf("!!!! %d %d %d %d %d %d %d\n", TI, TJ, TK, iii, jjj, kkk,
                        // tk);
                    }
                    //  sim.cursor.B.sizes[tj] = tmpsize;
                } else {

                    Bsizenow += tmpsize * 3;

                    preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
                    preB += memoryBandwidthWhole(tmpsize * 3 + 2);
                    preSramAccess += sramWriteBandwidth(tmpsize * 3 + 2);
                    AccessByte += tmpsize * 3 + 2;
                }
            }
        }

        if ((dataflow == Inner) && (((format == RR) || (format == CR)))) {
            // inconsistent format

            // estimated fiberlet fragment waste
            Bsizenow += ((fiberletlength + 1) / 2) * sim.cfg.kkk;

            // on-chip fiber current
            Bsizenow += sim.cfg.kkk;

            // initialize
            for (int tk = TK; tk < TK + sim.cfg.kkk; tk++) {
                bufferedsizeB[tk] = 0;
            }

            for (int tj = TJ; tj < min(TJ + sim.cfg.jjj, sim.cfg.J); tj++) {
                int startk = sim.cursor.B.begins[tj - TJ], tmpk = sim.cursor.B.begins[tj - TJ],
                    maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

                while (tmpk < maxk) {
                    int tmpindex = B[tj][tmpk];
                    if (tmpindex >= TK + sim.cfg.kkk) {
                        break;
                    }
                    tmpk++;

                    bufferedsizeB[tmpindex]++;
                }

                int tmpsize = (tmpk - startk);

                if (Bsizenow + tmpsize * 3 >= Bsize) {
                    if (!fulltagB) {
                        fulltagB = 1;
                        fullB = tj;
                    }
                    // cache the csc size of each col block
                    //   sim.cursor.B.sizes[tk] = tmpsize;
                } else {
                    //   sim.cursor.B.sizes[tk] = tmpsize;
                    preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
                    preB += memoryBandwidthWhole(tmpsize * 3 + 2);
                    AccessByte += tmpsize * 3 + 2;

                    // for each element need:
                    // 1) get pos: one read (current position)
                    // 2) add to chain: 1 data write; 1/block chain index write
                    preSramAccess += sramWriteBandwidth(tmpsize);
                    preSramAccess +=
                        sramReadBandwidth(tmpsize + tmpsize / fiberletlength) * 3;
                }
            }
        }
    }

    // scenario 2: mismatch
}

/*
Load streams(A/B) into buffer before calculation.
*/
void pre_calculate_load() {

    preSramAccess = preDramAccess = 0;

    if (ISCACHE) {
        initializeCacheValid();
    }

    // only will preload in blocking mode
    // cache mode don't need preload
    if (!ISCACHE) {
        pre_load_B();
    }

    pre_load_A();

    preSramAccess /= sramBank;

    totalCycle += max(preSramAccess, preDramAccess);
    preCycle += max(preSramAccess, preDramAccess);
}

bool consistent_B() {
    if (dataflow == Gust) {
        if ((format == RR) || (format == CR))
            return 1;
        if ((format == RC) || (format == CC))
            return 0;
    }
    if (dataflow == Inner) {
        if ((format == RC) || (format == CC))
            return 1;
        if ((format == RR) || (format == CR))
            return 0;
    }
    if (dataflow == Outer) {
        if ((format == RR) || (format == CR))
            return 1;
        if ((format == RC) || (format == CC))
            return 0;
    }
    return 0;
}

bool consistent_A() {
    if (dataflow == Gust) {
        if ((format == RR) || (format == RC))
            return 1;
        if ((format == CR) || (format == CC))
            return 0;
    }
    if (dataflow == Inner) {
        if ((format == RR) || (format == RC))
            return 1;
        if ((format == CR) || (format == CC))
            return 0;
    }
    if (dataflow == Outer) {
        if ((format == CR) || (format == CC))
            return 1;
        if ((format == RR) || (format == RC))
            return 0;
    }
    return 0;
}

// ii here means the now access position for OPT policy
void get_B_fiber(int jj, int ii)
{
    // In Blocking Mode
    if (!ISCACHE) {

        // two decisions: 1) consistent or not; 2) buffer or not (may bypass)
        u64 cost = sim.cursor.B.sizes[jj - TJ] * 3 + 2;

        if (consistent_B()) {
            // B[jj] is on the buffer
            if (fulltagB == 0 || jj < fullB) {
                // hit!
                // different access with B format:
                // continuous or chained
                computeSramAccess   += sramReadBandwidth(cost);
            } else {
                // B[jj] is not on the buffer, need to access dram
                // different access with B format
                // access one dram fiber all check all
                computeDramAccess   += memoryBandwidthPE(cost);
                computeB            += memoryBandwidthPE(cost);
                AccessByte          += cost;
            }
        } else {
            // hit part (chained)
            computeSramAccess +=
                sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeB[jj] + 3) / 4);

            // miss part (need to check every uncached)

            if (fulltagB) {
                computeDramAccess +=
                    (memoryBandwidthPE(3)) * ((long long)TK + sim.cfg.kkk - fullB);
                computeB +=
                    (memoryBandwidthPE(3)) * (long long)((long long)TK + sim.cfg.kkk - fullB);
                AccessByte += 3 * (long long)((long long)TK + sim.cfg.kkk - fullB);
            }
        }

    }
    // In cache Mode
    // address in cache mode is : fiberid + (relative << bias)  where relative =
    // (relative coordinate in fiber)/CACHEBLOCK
    else {
        int fibersize = sim.cursor.B.sizes[jj - TJ] * 3 + 1;
        cacheAccessFiber(jj, fibersize, ii);
    }
}

// in IP
void get_B_fiber_col_iii(int kk, int iii)
{
    u64 cost = sim.cursor.B.sizes[kk - TK] * 3 + 2;
    // B[jj] is on the buffer
    if (fulltagB == 0 || kk < fullB) {
        // hit!
        // different access with B format:
        // continuous or chained
        computeSramAccess   += sramReadBandwidth(cost) * iii;

    } else {
        // B[jj] is not on the buffer, need to access dram
        // different access with B format
        // access one dram fiber all check all
        computeDramAccess   += memoryBandwidthPE(cost) * iii;
        computeB            += memoryBandwidthPE(cost) * iii;
        AccessByte          += (cost) * iii;
    }
}

void get_A_fiber_col(int jj)
{
    assert(consistent_A());
    u64 cost = sim.cursor.A.sizes[jj - TJ] * 3 + 2;

    // A[ii] is on the buffer
    if (fulltagA == 0 || jj < fullA) {
        // hit
        computeSramAccess += sramReadBandwidth(cost);
    } else {

        computeDramAccess   += memoryBandwidthPE(cost);
        computeA            += memoryBandwidthPE(cost);
        AccessByte          += cost;
        computeSramAccess   += sramReadBandwidth(cost) + sramWriteBandwidth(cost);

        if (cacheScheme == CACHE_SCHEME_INNER_SP) {
            // f64 A access in static FLRU scheme
            computeDramAccess   += memoryBandwidthPE(cost);
            computeA            += memoryBandwidthPE(cost);
            computeSramAccess   += sramReadBandwidth(cost) + sramWriteBandwidth(cost);
        }
    }
}

void get_A_fiber(int ii) {
    assert(consistent_A());
    u64 cost = sim.cursor.A.sizes[ii - TI] * 3 + 2;

    // A[ii] is on the buffer
    if (fulltagA == 0 || ii < fullA) {
        // hit
        computeSramAccess += sramReadBandwidth(cost);
        if (cacheScheme == CACHE_SCHEME_INNER_SP) {
            // f64 A access in static FLRU scheme
            computeSramAccess += sramReadBandwidth(cost);
        }
    } else {
        computeDramAccess   += memoryBandwidthPE(cost);
        computeA            += memoryBandwidthPE(cost);
        AccessByte          += cost;
        computeSramAccess   += sramReadBandwidth(cost) + sramWriteBandwidth(cost);

        if (cacheScheme == CACHE_SCHEME_INNER_SP) {
            // f64 A access in static FLRU scheme
            computeDramAccess   += memoryBandwidthPE(cost);
            computeA            += memoryBandwidthPE(cost);
            computeSramAccess   += sramReadBandwidth(cost) + sramWriteBandwidth(cost);
        }
    }
}

void update_c_fiber(int jj) {

    for (int k1 = sim.cursor.B.begins[jj - TJ]; k1 < sim.cursor.B.begins[jj - TJ] + sim.cursor.B.sizes[jj - TJ]; k1++) {
        tmpC[B[jj][k1]] = 1;
    }
}

// need to store: all the stored C coords,
// in order to calculate the current and next state occuppied buffer size
void updateCAccess(int ii)
{
    // for buffered C:
    if ((Csize >= 100.0) && ((interorder == IKJ) || (interorder == KIJ))) {

        // check the delta buffer: how many new C elements (indicate how many
        // increase)
        int deltaC = 0;
        for (int k1 = TK; k1 < TK + sim.cfg.kkk; k1++) {
            if (tmpC[k1]) { // FIXME: use bit vectors
                // the k1 is a new element
                if (bufferedC[ii].find(k1) == bufferedC[ii].end()) {
                    deltaC++;

                    bufferedC[ii].insert(k1);
                }
            }
        }

        Csizenow += deltaC;

        // overflow! need to offload
        if (Csizenow > Csize) {

            // *2 because
            // 1 is to write to dram
            // 1 is to read from dram at the final merge stage
            computeDramAccess += memoryBandwidthPE(Csizenow * 3);
            postDramAccess += memoryBandwidthPE(Csizenow * 3);
            // write C at compute stage; read C at post merge stage
            computeC += memoryBandwidthPE(Csizenow * 3);
            postC += memoryBandwidthPE(Csizenow * 3);
            AccessByte += Csizenow * 3;
            AccessByte += Csizenow * 3;

            computeSramAccess +=
                sramReadBandwidth(Csizenow * 3) + sramWriteBandwidth(Csizenow * 3);

            Csizenow = 0;

            for (int i = TI; i < TI + sim.cfg.iii; i++) {
                bufferedC[i]    = std::set<int>();
            }
        }
    } else {
        // following is for the stream C
        // update with compute
        int cntc = 0;

        for (int k1 = TK; k1 < TK + sim.cfg.kkk; k1++) {
            if (tmpC[k1]) {
                cntc++;
            }
        }
        // write into DRAM during the computation
        computeDramAccess += memoryBandwidthPE(cntc * 3);
        computeC += memoryBandwidthPE(cntc * 3);
        AccessByte += cntc * 3;

        if (sim.cfg.jjj != sim.cfg.J) {
            // multiply 2 here if kkk != K
            // because need a extra inter-tile C merge and thus need an extra load
            postDramAccess += memoryBandwidthPE(cntc * 3);
            postC += memoryBandwidthPE(cntc * 3);
            AccessByte += cntc * 3;
        }

        computeSramAccess +=
            sramReadBandwidth(cntc * 3) + sramWriteBandwidth(cntc * 3);
    }
}

void get_B_fibers(int ii)
{
    assert(LIKELY(dataflow == Gust));
    int tmpj = sim.cursor.A.begins[ii - TI];
    int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];
    Coord ndirty = 0;

    while (tmpj < maxj && A[ii][tmpj] < TJ + sim.cfg.jjj) {
        // coordinate of required B fiber
        int jj = A[ii][tmpj];

        Coord bsize   = sim.cursor.B.sizes[jj - TJ];

        // >> get_B_fiber() inlined
        // In Blocking Mode
        if (!ISCACHE) {

            // two decisions: 1) consistent or not; 2) buffer or not (may bypass)

            if (consistent_B()) {
                i64 cost = bsize * 3 + 2;
                // B[jj] is on the buffer
                if (fulltagB == 0 || jj < fullB) {
                    // hit!
                    // different access with B format:
                    // continuous or chained
                    computeSramAccess   += sramReadBandwidth(cost);

                } else {
                    // B[jj] is not on the buffer, need to access dram
                    // different access with B format
                    // access one dram fiber all check all
                    computeDramAccess   += memoryBandwidthPE(cost);
                    computeB            += memoryBandwidthPE(cost);
                    AccessByte          += cost;
                }
            } else {
                // hit part (chained)
                computeSramAccess += sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeB[jj] + 3) / 4);

                // miss part (need to check every uncached)

                if (fulltagB) {
                    computeDramAccess   += (memoryBandwidthPE(3)) * ((long long)TK + sim.cfg.kkk - fullB);
                    computeB            += (memoryBandwidthPE(3)) * (long long)((long long)TK + sim.cfg.kkk - fullB);
                    AccessByte          += 3 * (long long)((long long)TK + sim.cfg.kkk - fullB);
                }
            }

        } else {
            // In cache Mode
            // address in cache mode is : fiberid + (relative << bias)  where relative =
            // (relative coordinate in fiber)/CACHEBLOCK
            int fibersize = bsize * 3 + 1;
            cacheAccessFiber(jj, fibersize, ii);
        }
        // << get_B_fiber inlined

        computePE += bsize;
        elements_processed_since_last_adjustment += bsize;

        // >> update_c_fiber() inlined
        for (int k1 = sim.cursor.B.begins[jj - TJ]; k1 < sim.cursor.B.begins[jj - TJ] + bsize; k1++) {
            Coord index = B[jj][k1];
            if (!tmpC[index]) {
                dirtyC[ndirty++]= index;
                tmpC[index]     = 1;
            }
        }
        // << update_c_fiber() inlined

        tmpj++;
    }


    // update A access
    if (consistent_A()) {
        i64 cost = (tmpj - sim.cursor.A.begins[ii - TI]) * 3;
        b32 hitA = (interorder == IJK || interorder == JIK) && (fulltagA == 0 || ii < fullA);

        // A[ii] is on the buffer

        // updated: add the interorder judge.
        // totally can't reuse if not **K
        if (hitA) {
            // hit
            computeSramAccess += sramReadBandwidth(cost);
            if (cacheScheme == CACHE_SCHEME_INNER_SP)
                computeSramAccess += sramReadBandwidth(cost);
        } else {
            computeDramAccess   += memoryBandwidthPE(cost);
            computeA            += memoryBandwidthPE(cost);
            AccessByte          += cost;
            computeSramAccess   += sramReadBandwidth(cost) + sramWriteBandwidth(cost);

            if (cacheScheme == CACHE_SCHEME_INNER_SP) {
                computeDramAccess   += memoryBandwidthPE(cost);
                computeA            += memoryBandwidthPE(cost);
                computeSramAccess   += sramReadBandwidth(cost) + sramWriteBandwidth(cost);
            }
        }

    } else {
        // hit part (chained)
        computeSramAccess   += sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeB[ii] + 3) / 4);

        // miss part (need to check every uncached)
        if (fulltagA) {
            computeDramAccess   += (memoryBandwidthPE(3)) * ((long long)TJ + sim.cfg.jjj - fullA);
            computeA            += (memoryBandwidthPE(3)) * (long long)((long long)TJ + sim.cfg.jjj - fullA);
            AccessByte          += 3 * ((long long)TJ + sim.cfg.jjj - fullA);
        }
    }

    // >> updateCaccess() inlined
    // for buffered C:
    if ((Csize >= 100.0) && ((interorder == IKJ) || (interorder == KIJ))) {
        int deltaC = 0;
        for (Coord d = 0; d < ndirty; ++d) {
            Coord k1 = dirtyC[d];
            if (tmpC[k1]) {
                if (bufferedC[ii].find(k1) == bufferedC[ii].end()) {
                    ++deltaC;
                    bufferedC[ii].insert(k1);
                }
            }
        }
        Csizenow        += deltaC;

        // overflow! need to offload
        if (Csizenow > Csize) {

            // *2 because
            // 1 is to write to dram
            // 1 is to read from dram at the final merge stage
            computeDramAccess   += memoryBandwidthPE(Csizenow * 3);
            postDramAccess      += memoryBandwidthPE(Csizenow * 3);
            // write C at compute stage; read C at post merge stage
            computeC            += memoryBandwidthPE(Csizenow * 3);
            postC               += memoryBandwidthPE(Csizenow * 3);
            AccessByte          += Csizenow * 6;
            computeSramAccess   += sramReadBandwidth(Csizenow * 3) + sramWriteBandwidth(Csizenow * 3);
            Csizenow = 0;

            for (int i = TI; i < TI + sim.cfg.iii; i++) {
                bufferedC[i]    = std::set<int>();
            }
        }
    } else {
        // following is for the stream C
        // update with compute

        // write into DRAM during the computation
        computeDramAccess += memoryBandwidthPE(ndirty * 3);
        computeC += memoryBandwidthPE(ndirty * 3);
        AccessByte += ndirty * 3;

        if (sim.cfg.jjj != sim.cfg.J) {
            // multiply 2 here if kkk != K
            // because need a extra inter-tile C merge and thus need an extra load
            postDramAccess += memoryBandwidthPE(ndirty * 3);
            postC += memoryBandwidthPE(ndirty * 3);
            AccessByte += ndirty * 3;
        }

        computeSramAccess +=
            sramReadBandwidth(ndirty * 3) + sramWriteBandwidth(ndirty * 3);
    }
    // << updateCaccess() inlined

    for (Coord d = 0; d < ndirty; d++)
        tmpC[dirtyC[d]] = 0;
}

// prefetchSize: the size of the buffer allocated for prefetch
// prefetchNow: the current prefetch size (must < prefetchSize)
// prefetchRowNow: row of the current(next) prefetch position
int prefetchSize = 500;
int prefetchNow = 0;
int prefetchRowNow = 0;

bool prefetchrow(int ii) {

    int needsize = 0;
    // FLRU mode; need 2data+1coord+1next pointer (*4)
    if (cacheScheme == 6 || cacheScheme == 7) {
        needsize = sim.cursor.A.sizes[ii - TI] * 4 + 1;
    }
    // FLFU mode; don't need next pointer (*3)
    // NOTE(ejs): 2 data (words) + 1 coord (word) = 3 words
    else if (cacheScheme == 66 || cacheScheme == CACHE_SCHEME_FLFU) {
        needsize = sim.cursor.A.sizes[ii - TI] * 3;
    }

    // can't prefetch this row now
    if (prefetchNow + needsize >= prefetchSize) {
        return 0;
    }

    prefetchNow += needsize;

    int tmpj = sim.cursor.A.begins[ii - TI];
    int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];

    // QUESTION(ejs): Observation: if column c in the current A row fiber is non-zero,
    // it will multiply against row c of B. Hence, for each non-zero A col we know
    // we will eventually need the corresponding B row.
    //
    // This loop iterates over *all* columns of the current A row fiber to compute the
    // perfect cache metadata (LFU counts) of the corresponding B rows which shall be needed.
    // However, this is problematic:
    // - summing the lfu counts *per cycle* across the entire fiber does not seem remotely feasible
    // - how can they POSSIBLY guarantee that all the needed A columns are in cache? This is
    // essentially oracle access/cheating
    // - they are not charging for these accesses into metadata (e.g. increment computeSramAccess or something)
    while (tmpj < maxj && A[ii][tmpj] < TJ + sim.cfg.jjj) {
        // coordinate of required B fiber
        // in this prefetch: push the next access queue of jj a ii
        int jj = A[ii][tmpj];

        if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == CACHE_SCHEME_INNER_SP ||
            cacheScheme == CACHE_SCHEME_SPARCH) {
            nextposvector[jj].push(-ii);
        }
        if (cacheScheme == 66) {
            LFUtag[jj]++;
        }

        // practical flfu. update in the flubit
        if (cacheScheme == CACHE_SCHEME_FLFU) {

            long long firstaddr = getCacheAddr(jj, 0);
            int fibersize = sim.cursor.B.sizes[jj - TJ] * 3;
            for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHE_BLOCK_NELEMS) {
                long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHE_BLOCK_NELEMS);

                int _set = getSet2(tmpaddr);
                int _tag = getTag2(tmpaddr);

                bool need_halve_lfu = 0;
                bool incache = 0;

                prefetch_increments++;

                for (int i = 0; i < SETASSOC; i++) {
                    if (Valid[_set * SETASSOC + i] && (Tag[_set * SETASSOC + i] == _tag)) {

                        // not the first, need to check orig
                        if (tmpcurr != 0) {
                            if (PosOrig[_set * SETASSOC + i] != getOrig(firstaddr)) {
                                // not the same orig
                                continue;
                            }
                        } else {
                            if (PosOrig[_set * SETASSOC + i] != 0) {
                                continue;
                            }
                        }
                        // hit
                        incache = 1;
                        lfubit[_set * SETASSOC + i]++;
                        // if the updated flfu bit overflow
                        if (lfubit[_set * SETASSOC + i] > LFUmax) {
                            // NOTE(ejs): when one lfu counter saturates, the entire array of lfu's
                            // will be halved (renormalized) so the counts remain meaningful.
                            need_halve_lfu = 1;
                        }
                        break;
                    }
                }

                if (useVirtualTag) {
                    // not in cache, considering the virtual tag
                    if (!incache) {
                        bool invirtualtag = 0;
                        for (int i = 0; i < VIRTUALSETASSOC; i++) {
                            if (virtualValid[_set * VIRTUALSETASSOC + i]) {
                                if (virtualTag[_set * VIRTUALSETASSOC + i] == _tag) {
                                    // in virtual
                                    invirtualtag = 1;
                                    virtuallfubit[_set * VIRTUALSETASSOC + i]++;
                                    if (virtuallfubit[_set * VIRTUALSETASSOC + i] > LFUmax) {
                                        need_halve_lfu = 1;
                                    }
                                    // if find a matched, don't need to check others
                                    break;
                                }

                            } else {
                            }
                        }
                        if (!invirtualtag) {

                            bool hasinvalid = 0;
                            for (int i = 0; i < VIRTUALSETASSOC; i++) {
                                if (virtualValid[_set * VIRTUALSETASSOC + i] == 0) {
                                    // has invalide!
                                    hasinvalid = 1;
                                    // put the slot here
                                    virtualValid[_set * VIRTUALSETASSOC + i] = 1;
                                    virtualTag[_set * VIRTUALSETASSOC + i] = _tag;
                                    virtuallfubit[_set * VIRTUALSETASSOC + i] = 1;
                                    break;
                                }
                            }
                            bool haszero = 0;
                            if (!hasinvalid) {

                                for (int i = 0; i < VIRTUALSETASSOC; i++) {
                                    if (virtuallfubit[_set * VIRTUALSETASSOC + i] == 0) {
                                        // find a slot = 0, replace it to the current fiber
                                        haszero = 1;
                                        virtualValid[_set * VIRTUALSETASSOC + i] = 1;
                                        virtualTag[_set * VIRTUALSETASSOC + i] = _tag;
                                        virtuallfubit[_set * VIRTUALSETASSOC + i] = 1;

                                        break;
                                    }
                                }
                            }

                            if (hasinvalid == 0 && haszero == 0) {
                                prefetch_discards++;
                            }
                        }
                    }
                }

                // if the updated flfu overlow, half the flfubit of the whole set!
                // both update in cache or virtual tag will cause the half
                if (need_halve_lfu) {
                    for (int i = 0; i < SETASSOC; i++) {
                        if (Valid[_set * SETASSOC + i]) {
                            lfubit[_set * SETASSOC + i] /= 2;
                        }
                    }

                    // if use virtual tag, also need to half the virtual tag!
                    // 1 problem: sometimes may not 0 (halfed from 1),
                    // but is 0 now, then will be replace, but actually better
                    if (useVirtualTag) {
                        for (int i = 0; i < VIRTUALSETASSOC; i++) {
                            if (virtualValid[_set * VIRTUALSETASSOC + i]) {
                                virtuallfubit[_set * VIRTUALSETASSOC + i] /= 2;
                            }
                        }
                    }
                }
            }
        }

        tmpj++;
    }

    return 1;
}

int get_num_samples(f64 current_temperature) {

    if (current_temperature > SA_INITIAL_TEMP * 0.5) {
        return 4;
    } else if (current_temperature > SA_INITIAL_TEMP * 0.1) {
        return 8;
    } else {
        return 16;
    }
}

bool lastaccept = 1;

void update_prefetch_size() {
    f64 temperature = SA_INITIAL_TEMP * pow(SA_COOLING_RATE, sa_iteration_k);

    int num_samples = get_num_samples(temperature);

    sa_iteration_k++;

    // printf("---%d %d %lf\n", sa_iteration_k, num_samples, temperature);

    if ((sa_iteration_k % num_samples) != 0) {
        return;
    }

    if (lastaccept == 0) {
        f64 current_data_miss_rate =
            1.0 - ((f64)(data_access_hit) / data_access_total);
        last_iteration_data_miss_rate = current_data_miss_rate;
        lastaccept = 1;

        f64 current_discard_rate;
        if (prefetch_increments == 0) {
            current_discard_rate = 0;
        } else {
            current_discard_rate = ((f64)prefetch_discards) / prefetch_increments;
        }

        previous_prefetch_size = current_prefetch_size;

        if (current_discard_rate > RATE_THRESHOLD && current_data_miss_rate < 0.3) {
            current_prefetch_size *= 0.8;
        } else {
            if (current_data_miss_rate >= 0.3 &&
                current_discard_rate <= RATE_THRESHOLD) {
                current_prefetch_size *= 1.2;
            } else {
                if (current_data_miss_rate >= 0.3 &&
                    current_discard_rate > RATE_THRESHOLD) {

                    f64 perturbation =
                        ((static_cast<f64>(rand()) / RAND_MAX) - 0.5) * 0.2;
                    current_prefetch_size *= (1.0 + perturbation);
                }
            }
        }

        current_prefetch_size = max(current_prefetch_size, 1.0 / 256.0);
        current_prefetch_size = min(current_prefetch_size, 0.10);

        prefetchSize = current_prefetch_size * inputcachesize;
        cachesize = inputcachesize - prefetchSize;

        elements_processed_since_last_adjustment = 0;
        prefetch_discards = 0;
        prefetch_increments = 0;
        data_access_hit = 0;
        data_access_total = 0;

        return;
    }

    f64 current_data_miss_rate =
        1.0 - ((f64)(data_access_hit) / data_access_total);

    f64 delta_M = (f64)((1.0 - last_iteration_data_miss_rate) -
                              (1.0 - current_data_miss_rate)) /
                     (1.0 - last_iteration_data_miss_rate);

    bool accept_change = false;
    if (delta_M < 0) {
        accept_change = true;
    } else {
        f64 acceptance_prob = exp(-delta_M / temperature);
        f64 random_val = static_cast<f64>(rand()) / RAND_MAX;
        // printf("%lf %lf %lf\n", temperature, acceptance_prob, random_val);
        if (acceptance_prob > random_val) {
            accept_change = true;
        }
    }

    // printf("!!!!!! %d %d %lf last size: %lf  current size: %lf  last hit "
    //        "rate:%lf    current hit rate:%lf  change:%d \n",
    //        data_access_hit, data_access_total, current_data_miss_rate,
    //        previous_prefetch_size, current_prefetch_size,
    //        1.0 - last_iteration_data_miss_rate, 1.0 - current_data_miss_rate,
    //        accept_change);

    if (accept_change) {
        last_iteration_data_miss_rate = current_data_miss_rate;
        if (current_data_miss_rate < best_data_miss_rate) {
            best_data_miss_rate = current_data_miss_rate;
        }
        lastaccept = 1;
    } else {
        current_prefetch_size = previous_prefetch_size;

        lastaccept = 0;

        prefetchSize = current_prefetch_size * inputcachesize;
        cachesize = inputcachesize - prefetchSize;
        elements_processed_since_last_adjustment = 0;
        prefetch_discards = 0;
        prefetch_increments = 0;
        data_access_hit = 0;
        data_access_total = 0;

        return;
    }

    f64 current_discard_rate;
    if (prefetch_increments == 0) {
        current_discard_rate = 0;
    } else {
        current_discard_rate = ((f64)prefetch_discards) / prefetch_increments;
    }

    previous_prefetch_size = current_prefetch_size;

    if (current_discard_rate > RATE_THRESHOLD && current_data_miss_rate < 0.3) {
        current_prefetch_size *= 0.8;
    } else {
        if (current_data_miss_rate >= 0.3 &&
            current_discard_rate <= RATE_THRESHOLD) {
            current_prefetch_size *= 1.2;
        } else {

            if (current_data_miss_rate >= 0.3 &&
                current_discard_rate > RATE_THRESHOLD) {

                f64 perturbation =
                    ((static_cast<f64>(rand()) / RAND_MAX) - 0.5) * 1.0;
                current_prefetch_size *= (1.0 + perturbation);
            }
        }
    }

    current_prefetch_size = max(current_prefetch_size, 1.0 / 256.0);
    current_prefetch_size = min(current_prefetch_size, 0.1);

    prefetchSize = current_prefetch_size * inputcachesize;
    cachesize = inputcachesize - prefetchSize;

    elements_processed_since_last_adjustment = 0;
    prefetch_discards = 0;
    prefetch_increments = 0;
    data_access_hit = 0;
    data_access_total = 0;
}

bool adaptive_prefetch = 0;

/*
Perform calculation.
stream A/B/C: need to load/store from dram
buffered A/B/C: only load/store sram

3 bandwidth constraints:
dram; sram; compute

The inconsistent format has transformed while loading
So will not influence the calculate very much
Only in two place :
1) The iterate mode (compact or chained) (but the format is same)
2) The unbuffered (missed) access
*/
void calculate() {

    computePE = computeDramAccess = computeSramAccess = 0;

    switch (dataflow) {
    case Gust: {

        prefetchNow = 0;

        // all prefetch scheme
        if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == 66 ||
            cacheScheme == CACHE_SCHEME_FLFU || cacheScheme == CACHE_SCHEME_INNER_SP || cacheScheme == CACHE_SCHEME_SPARCH) {
            // reinitialize the next pointer for FLRU
            if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == CACHE_SCHEME_INNER_SP ||
                cacheScheme == CACHE_SCHEME_SPARCH) {
                for (int j1 = TJ; j1 < min(TJ + sim.cfg.jjj, sim.cfg.J); j1++) {
                    while (!nextposvector[j1].empty()) {
                        nextposvector[j1].pop();
                    }
                }
            }
            // reinitialize the LFU tag for FLFU
            if (cacheScheme == 66) {
                // QUESTION(ejs): Note that len(LFUtag) = J (i.e. num cols in A / num rows in B).
                // Why the hell is the LFUtag array being cleared to 0 every iteration?
                // How can this be tracking the corresponding B row frequency accurately?
                for (int j1 = TJ; j1 < TJ + sim.cfg.jjj; j1++) {
                    LFUtag[j1] = 0;
                }
            }

            // first prefill the prefetch window
            for (int ii = 0; prefetchNow < prefetchSize && ii < sim.cfg.iii; ii++) {
                if (TI + ii >= sim.cfg.I)
                    break;

                prefetchRowNow = TI + ii;
                // return 0 if can't prefetch that row now
                if (!prefetchrow(TI + ii)) {
                    break;
                }
            }
        }

        for (int ii = 0; ii < sim.cfg.iii; ii++) {
            if (TI + ii >= sim.cfg.I)
                break;

            // get O(J) corresponding B (different from Gust and Inner)
            // different from other dataflow (is A-dependent)
            get_B_fibers(TI + ii);

            // update the prefetch window after each row
            // don't need to update prefetch window in static flru
            if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == 66 ||
                cacheScheme == CACHE_SCHEME_FLFU || cacheScheme == CACHE_SCHEME_INNER_SP || cacheScheme == CACHE_SCHEME_SPARCH) {

                // first minus this row's overhead
                int needsize = 0;
                if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == CACHE_SCHEME_SPARCH) {
                    needsize = sim.cursor.A.sizes[TI + ii] * 4 + 1;
                }
                // FLFU mode; don't need next pointer (*3)
                if (cacheScheme == 66 || cacheScheme == CACHE_SCHEME_FLFU) {
                    needsize = sim.cursor.A.sizes[TI + ii] * 3;
                }

                if (prefetchNow > needsize) {
                    prefetchNow -= needsize;
                }

                // need to prefetch the next ii+prefetchsize+1
                if (prefetchNow >= prefetchSize)
                    continue;

                // QUESTION(ejs): Why are we updating prefetchRowNow / calling prefetchrow again?
                while (prefetchRowNow < TI + sim.cfg.iii) {
                    if (!prefetchrow(prefetchRowNow)) {
                        break;
                    }
                    prefetchRowNow++;
                }
            }

            if (adaptive_prefetch) {
                if (elements_processed_since_last_adjustment >= adjustment_interval) {
                    update_prefetch_size();
                    elements_processed_since_last_adjustment = 0;
                }
            }
        }
    }
        break;

    case Inner: {

        // update B here
        for (int k = TK; k < TK + sim.cfg.kkk; k++) {
            get_B_fiber_col_iii(k, sim.cfg.iii);
        }

        for (int ii = 0; ii < sim.cfg.iii; ii++) {

            // update A
            // get A
            get_A_fiber(TI + ii);

            // update C

            int tmpj = sim.cursor.A.begins[ii];
            int maxj = offsetarrayA[TI + ii + 1] - offsetarrayA[TI + ii];

            // tmpc = 0
            for (int k1 = TK; k1 < TK + sim.cfg.kkk; k1++) {
                tmpC[k1] = 0;
            }

            while (tmpj < maxj && A[TI + ii][tmpj] < TJ + sim.cfg.jjj) {
                // coordinate of required B fiber
                int jj = A[TI + ii][tmpj];

                update_c_fiber(jj);

                tmpj++;
            }

            updateCAccess(TI + ii);
        }
    }
        break;

    case Outer: {

        for (int jj = 0; jj < sim.cfg.jjj; jj++) {
            get_A_fiber_col(TJ + jj);

            // use a dumb jj
            get_B_fiber(TJ + jj, jj);
        }

        fulltagC = 0;
        Csizenow = 0;

        for (int ii = TI; ii < TI + sim.cfg.iii; ii++) {
            int tmpj = sim.cursor.A.begins[ii - TI];
            int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];

            // tmpc = 0
            for (int k1 = TK; k1 < TK + sim.cfg.kkk; k1++) {
                tmpC[k1] = 0;
            }

            while (tmpj < maxj && A[ii][tmpj] < TJ + sim.cfg.jjj) {
                // coordinate of required B fiber
                int jj = A[ii][tmpj];

                computePE += sim.cursor.B.sizes[jj - TJ];

                update_c_fiber(jj);

                tmpj++;
            }

            int cntc = 0;
            for (int k1 = TK; k1 < TK + sim.cfg.kkk; k1++) {
                if (tmpC[k1]) {
                    cntc++;
                }
            }

            if (!fulltagC) {
                if (Csizenow + cntc * 3 >= Csize) {
                    fulltagC = 1;
                    Csizenow += cntc * 3;
                } else {
                    Csizenow += cntc * 3;
                    // can be stored in sram!
                    computeSramAccess +=
                        sramReadBandwidth(cntc * 3 + 2) * (sim.cfg.jjj / mergecnt);
                }
            } else {
                computeDramAccess += memoryBandwidthPE(cntc * 3 + 2) * (sim.cfg.jjj / mergecnt);
                computeC += memoryBandwidthPE(cntc * 3 + 2) * (sim.cfg.jjj / mergecnt);
                AccessByte += (cntc * 3 + 2) * (sim.cfg.jjj / mergecnt);
            }
        }
    }
        break;
    }

    totalCycle += max(computePE / PEcnt, max(computeDramAccess / PEcnt,
                                             computeSramAccess / sramBank));
    calCycle += max(computePE / PEcnt,
                    max(computeDramAccess / PEcnt, computeSramAccess / sramBank));

    totalSram += computeSramAccess / sramBank;
    totalDram += computeDramAccess / PEcnt;
    totalPE += computePE / PEcnt;
}

void configPartial(f32 partialA, f32 partialB, f32 partialC) {
    Asize = cachesize * partialA;
    Bsize = cachesize * partialB;
    Csize = cachesize * partialC;
}

struct simulator_state initialize_simulator(const struct config *cfg)
{
    // alloacte memory
    try {
        if (bufferedC == nullptr)
            bufferedC = new set<int>[cfg->I]();
        if (bufferedsizeB == nullptr)
            bufferedsizeB = new int[cfg->J]();
        if (tmpC == nullptr)
            tmpC    = new u8[cfg->K]();
        if (dirtyC == nullptr)
            dirtyC  = new Coord[cfg->K]();
        if (LFUtag == nullptr)
            LFUtag = new int[cfg->J]();
        if (nextposvector == nullptr)
            nextposvector = new queue<int>[cfg->J]();
    } catch (const std::bad_alloc &e) {
        std::cerr << "Error allocating memory for " << e.what() << std::endl;
        std::exit(1);
    }

    return (struct simulator_state) {
        .cfg    = *cfg
    };
}

void reinitialize() {
    // reinitialize statistics
    totalCycle = 0;
    preCycle = calCycle = postCycle = 0;
    computeA = computeB = computeC = 0;
    totalA = totalB = totalC = 0;
    preA = preB = postC = 0;
    totalSram = totalDram = totalPE = 0;
    TI = TJ = TK = 0;
    AccessByte = 0;

    totalhit = 0;
    data_access_hit = 0;
    totalaccess = 0;
    data_access_total = 0;

    postDramAccess = postSramAccess = 0;

    if (ISCACHE) {
        initializeCacheValid();

        if (useVirtualTag) {
            memset(virtualValid, 0, sizeof(bool) * CACHE_NSETS * VIRTUALSETASSOC);
        }

        memset(PosOrig, 0, sizeof(short) * CACHE_NSETS * SETASSOC);
    }

    // reinitialize buffer c
    Csizenow = 0;
    for (int i = 0; i < sim.cfg.I; i++) {
        bufferedC[i]    = std::set<int>();
    }
}

int getcntc(int ii) 
{
    int tmpj = sim.cursor.A.begins[ii - TI];
    int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];
    Coord ndirty = 0;

    for (int k1 = 0; k1 < sim.cfg.K; k1++) // FIXME: better tmpC clearing invariants so before clear is not necessary? currently we do both before and after
        tmpC[k1] = 0;

    while (tmpj < maxj && A[ii][tmpj] < sim.cfg.J) {
        // coordinate of required B fiber
        int jj = A[ii][tmpj];

        // >> update_c_fiber() inlined
        for (int k1 = sim.cursor.B.begins[jj - TJ]; k1 < sim.cursor.B.begins[jj - TJ] + sim.cursor.B.sizes[jj - TJ]; k1++) {
            Coord index = B[jj][k1];
            if (!tmpC[index]) {
                dirtyC[ndirty++]= index;
                tmpC[index]     = 1;
            }
        }
        // << update_c_fiber() inlined

        tmpj++;
    }

    Coord cntc = 0;

    for (Coord d = 0; d < ndirty; ++d) {
        Coord k1 = dirtyC[d];
        cntc    += tmpC[k1];
        tmpC[k1] = 0;
    }

    return cntc;
}

// merge the partial C generated at each tile
// Read DRAM: amount of sum of all partialC offload
// Write DRAM: amount of nnzC
void postTileMerge() {
    // another way to realize this is to add once read each time when we write C
    // already added in the C writing position for gust and IP

    // if ((dataflow == Gust) && (sim.cfg.jjj != sim.cfg.J)) {
    //     for (int ii = 0; ii < sim.cfg.I; ii++) {
    //         int cntc = getcntc(ii);

    //         computeDramAccess += memoryBandwidthPE(cntc * 3);
    //         postC += memoryBandwidthPE(cntc * 3);
    //         AccessByte += cntc * 3;
    //     }
    // }

    // calculate the inter-cost of outer
    if (dataflow == Outer) {

        for (int ii = 0; ii < sim.cfg.I; ii++) {

            int cntc = getcntc(ii);

            postDramAccess += memoryBandwidthPE(cntc * 3) * (sim.cfg.ttj);
            postC += memoryBandwidthPE(cntc * 3) * (sim.cfg.ttj);
            AccessByte += cntc * 3 * sim.cfg.ttj;
        }
        //   analyze_statistics();
    }

    postSramAccess /= sramBank;
    postDramAccess /= PEcnt;

    totalCycle  += max(postDramAccess, postSramAccess);
    postCycle   += max(postDramAccess, postSramAccess);
}


void run()
{
    reinitialize();

    if (adaptive_prefetch) {
        // initialize adaptive prefetch
        // --- Offline Phase ---
        // f64 avg_nonzero_length_B;
        // if (K > 0 && T_J > 0) {
        //   avg_nonzero_length_B = static_cast<f64>(nnzB) / K;
        // } else {
        //   avg_nonzero_length_B = 1.0;
        // }

        current_prefetch_size = 1.0 / 128.0;

        prefetchSize = current_prefetch_size * inputcachesize;
        cachesize = inputcachesize - prefetchSize;
        setSET();

        sa_iteration_k = 0;
        previous_prefetch_size = current_prefetch_size;
        last_iteration_data_miss_rate = 1.0;
        best_data_miss_rate = 1.0;

        adjustment_interval = estEffMAC / 500;
        if (adjustment_interval == 0)
            adjustment_interval = 1000;

        elements_processed_since_last_adjustment = 0;

        prefetch_discards = 0;
        data_access_hit = 0;
        data_access_total = 0;
    }

    if (sim.cfg.iii > sim.cfg.I)
        sim.cfg.iii = sim.cfg.I;
    if (sim.cfg.jjj > sim.cfg.J)
        sim.cfg.jjj = sim.cfg.J;
    if (sim.cfg.kkk > sim.cfg.K)
        sim.cfg.kkk = sim.cfg.K;

    Coord total_ntiles = sim.cfg.tti*sim.cfg.ttj*sim.cfg.ttk;
    while (total_ntiles--) {
        // need to initialize the cache each time change the tile
        if (ISCACHE) { // FIXME(ejs): seems redundant and slow
            initializeCacheValid();
        }

        advance_cursor();
        // sync tile origins
        TI = sim.cursor.ti * sim.cfg.iii;
        TJ = sim.cursor.tj * sim.cfg.jjj;
        TK = sim.cursor.tk * sim.cfg.kkk;
        pre_calculate_load();
        calculate();
    }

    postTileMerge();

    analyze_statistics();
}

void runTile(int kkk)
{
    assert(ISCACHE);

    // deal with the opt metadata
    if (cacheScheme == 6 || cacheScheme == 7) {

        cachesize = inputcachesize - prefetchSize;
        cachesize -= kkk * 2;

        if (cachesize < 0) {
            puts("!!!!!! metadata out of range!!!!!!!!!!");
            fflush(stdout);
            return;
        }

        setSET();
    }

    if (cacheScheme == 66) {

        cachesize = inputcachesize - prefetchSize;

        // LFU tag size
        cachesize -= kkk;

        if (cachesize < 0) {
            puts("!!!!!! metadata out of range!!!!!!!!!!");
            fflush(stdout);
            return;
        }

        setSET();
    }

    if (cacheScheme == CACHE_SCHEME_FLFU) {

        cachesize = inputcachesize - prefetchSize;

        setSET();
    }

    // need to allocate extra tag space in address mode
    // the address space is depends on the tiling size (equal to jjj)
    // need to update: cachesize (actually Bsize?) + SET + SETLOG
    if ((cacheScheme == 4) || (cacheScheme == 5) || (cacheScheme == 7)) {
        // need to add back after this calculation
        cachesize = inputcachesize;
        CACHE_NSETS = cachesize / (CACHE_BLOCK_NELEMS * SETASSOC);
        CACHE_NSETS_LOG2 = getlog(CACHE_NSETS);
    }

    hitcnt = 0;
    misscnt = 0;

    fflush(stdout);

    run();
}
