#include "cache.h"
#include "dynamic.h"
#include "estimation.h"
#include "headers.h"
#include "statistics.h"

// store all the buffered C now
set<int> *bufferedC = nullptr;
// record length of buffered C
// equals bufferedC[i].size()
int *bufferedClen = nullptr;

int BLOCKSIZE = 16;

int *beginA = nullptr;
int *beginB = nullptr;

int *beginAc = nullptr;
int *beginBc = nullptr;

// int *begin = nullptr;

/*
The current fiber size of each array
stored according to the dataflow order
(not the storage format order)

update currsize each time the block of the array changes
(when inter-iterate)
*/
int *currsizeA = nullptr;
int *currsizeAc = nullptr;
int *currsizeB = nullptr;
int *currsizeBc = nullptr;
// int *currsizeC = nullptr;

/*
The currently buffered size of each array
(part of cuursizeA/B/C)

update bufferedsize each time
*/
int *bufferedsizeA = nullptr;
int *bufferedsizeB = nullptr;
// int *bufferedsizeC = nullptr;

int *tmpC = nullptr;

// start of current block
int TI, TJ, TK;

bool ISDYNAMICJ = 0;
// dynamic j, play same as jjj
int dynj;

bool ISDYNAMICK = 0;
// dynamic k, play same as kkk
int dynk;

bool ISDYNAMICI = 0;
int dyni;

int PartialConfig;

bool check_outer_loop() {
  if ((interorder == KIJ) || (interorder == KJI)) {
    return TK < K;
  } else if ((interorder == JIK) || (interorder == JKI)) {
    return TJ < J;
  } else if ((interorder == IJK) || (interorder == IKJ)) {
    return TI < I;
  }
  return 0;
}

bool check_inner_loop() {

  if ((interorder == IJK) || (interorder == JIK)) {
    return (TK < K);
  } else if ((interorder == IKJ) || (interorder == KIJ)) {
    return (TJ < J);
  } else if ((interorder == JKI) || (interorder == KJI)) {
    return (TI < I);
  }
  return 0;
}

bool check_mid_loop() {

  if ((interorder == IKJ) || (interorder == JKI)) {
    return (TK < K);
  } else if ((interorder == IJK) || (interorder == KJI)) {
    return (TJ < J);
  } else if ((interorder == JIK) || (interorder == KIJ)) {
    return (TI < I);
  }
  return 0;
}

// STAR: call this
// when: 1) start time 2) each time update I/J
// can over called by call each time
/*
update currsizeA/B/C here!
(each time the block iterate)
currsize is consistent to dataflow order
*/
void updateBlockA() {

  // Row-majored
  if (dataflow == Inner || dataflow == Gust) {

    for (int ti = TI; ti < TI + iii; ti++) {
      if (ti > I)
        break;

      int startj = beginA[ti], tmpj = beginA[ti];
      int maxj = offsetarrayA[ti + 1] - offsetarrayA[ti];

      // jjj -> ((ISDYNAMICJ)?dynj:jjj)

      // wrong!  this function is use to calculate the new currsizeA
      // can't simply change to dynj
      // solution: add a update fuction after pre-load B

      // check: will pre_load use this currsizeA and currsizeB ? -> cause cycle
      while (tmpj < maxj && A[ti][tmpj] < ((ISDYNAMICJ) ? dynj : jjj) + TJ) {
        tmpj++;
      }

      currsizeA[ti] = tmpj - startj;
    }
  }

  // Col-majored
  if (dataflow == Outer) {

    for (int tj = TJ; tj < TJ + ((ISDYNAMICJ) ? dynj : jjj); tj++) {
      if (tj > J)
        break;

      int starti = beginAc[tj], tmpi = beginAc[tj];
      int maxi = offsetarrayAc[tj + 1] = offsetarrayAc[tj];

      while (tmpi < maxi && Ac[tj][tmpi] < iii + TI) {
        tmpi++;
      }

      currsizeAc[tj] = tmpi - starti;
    }
  }
}

void updateBlockB() {

  // Row-majored
  if (dataflow == Gust || dataflow == Outer) {

    for (int tj = TJ; tj < TJ + ((ISDYNAMICJ) ? dynj : jjj); tj++) {
      if (tj > J)
        break;

      int startk = beginB[tj], tmpk = beginB[tj],
          maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

      while (tmpk < maxk && B[tj][tmpk] < ((ISDYNAMICK) ? dynk : kkk) + TK) {
        tmpk++;
      }

      currsizeB[tj] = tmpk - startk;
    }
  }

  // Col-majored
  if (dataflow == Inner) {

    for (int tk = TK; tk < TK + ((ISDYNAMICK) ? dynk : kkk); tk++) {
      if (tk > K)
        break;

      int startj = beginBc[tk], tmpj = beginBc[tk],
          maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

      while (tmpj < maxj && Bc[tk][tmpj] < ((ISDYNAMICJ) ? dynj : jjj) + TJ) {
        tmpj++;
      }

      currsizeBc[tk] = tmpj - startj;
    }
  }
}

void updateBlockC() {

  // Don't have determined beginC and currsizeC
}

// beginA only related to TJ -> call every time update TJ
void forcebeginA() {
  for (int i = 0; i < I; i++) {

    // int startj = 0;
    int tmpj = 0;
    int maxj = offsetarrayA[i + 1] - offsetarrayA[i];

    // here is TJ because TJ have added jjj before call the func
    while (tmpj < maxj && A[i][tmpj] < TJ) {
      tmpj++;
    }

    beginA[i] = tmpj;
  }

  for (int tj = 0; tj < J; tj++) {
    // int starti = 0;
    int tmpi = 0;
    int maxi = offsetarrayAc[tj + 1] - offsetarrayAc[tj];

    while (tmpi < maxi && Ac[tj][tmpi] < TI) {
      tmpi++;
    }
    beginAc[tj] = tmpi;
  }
}

void forcebeginB() {

  for (int tj = 0; tj < J; tj++) {

    // int startk = 0;
    int tmpk = 0;
    int maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

    while (tmpk < maxk && B[tj][tmpk] < TK) {
      tmpk++;
    }

    beginB[tj] = tmpk;
  }

  for (int tk = 0; tk < K; tk++) {

    // int startj = 0;
    int tmpj = 0;
    int maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

    while (tmpj < maxj && Bc[tk][tmpj] < TJ) {
      tmpj++;
    }

    beginBc[tk] = tmpj;
  }
}

// each time after update TJ
void updateBeginA() {
  for (int ti = TI; ti < TI + iii; ti++) {
    if (ti > I)
      break;

    // int startj = beginA[ti];
    int tmpj = beginA[ti];
    int maxj = offsetarrayA[ti + 1] - offsetarrayA[ti];

    // here is TJ because TJ have added jjj before call the func
    while (tmpj < maxj && A[ti][tmpj] < TJ) {
      tmpj++;
    }

    beginA[ti] = tmpj;
  }
}

void ALLupdateBeginAc() {
  for (int tj = 0; tj < J; tj++) {
    if (tj > J)
      break;

    // int starti = beginAc[tj];
    int tmpi = beginAc[tj];
    int maxi = offsetarrayAc[tj + 1] - offsetarrayAc[tj];

    while (tmpi < maxi && Ac[tj][tmpi] < TI) {
      tmpi++;
    }

    beginAc[tj] = tmpi;
  }
}

void AllupdateBeginA() {
  for (int ti = 0; ti < I; ti++) {
    if (ti > I)
      break;

    // int startj = beginA[ti];
    int tmpj = beginA[ti];
    int maxj = offsetarrayA[ti + 1] - offsetarrayA[ti];
    while (tmpj < maxj && A[ti][tmpj] < TJ) {
      tmpj++;
    }

    beginA[ti] = tmpj;
    // printf("%d %d %d %d   ", startj, tmpj, maxj, beginA[ti]);
  }
}

// each time update TI
void updateBeginAc() {
  for (int tj = TJ; tj < TJ + ((ISDYNAMICJ) ? dynj : jjj); tj++) {
    if (tj > J)
      break;

    // int starti = beginAc[tj];
    int tmpi = beginAc[tj];
    int maxi = offsetarrayAc[tj + 1] - offsetarrayAc[tj];

    while (tmpi < maxi && Ac[tj][tmpi] < TI) {
      tmpi++;
    }

    beginAc[tj] = tmpi;
  }
}

void AllupdateBeginB() {

  // update beginB
  for (int tj = 0; tj < J; tj++) {
    if (tj > J)
      break;

    // int startk = beginB[tj];
    int tmpk = beginB[tj];
    int maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

    while (tmpk < maxk && B[tj][tmpk] < TK) {
      tmpk++;
    }

    beginB[tj] = tmpk;
  }
}

void AllupdateBeginBc() {

  // update beginBc
  for (int tk = 0; tk < K; tk++) {
    if (tk > K)
      break;

    // int startj = beginBc[tk]; 
    int tmpj = beginBc[tk];
    int maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

    while (tmpj < maxj && Bc[tk][tmpj] < TJ) {
      tmpj++;
    }

    beginBc[tk] = tmpj;
  }
}

// each time update Tk
void updateBeginB() {

  // update beginB
  for (int tj = TJ; tj < TJ + ((ISDYNAMICJ) ? dynj : jjj); tj++) {
    if (tj > J)
      break;

    // int startk = beginB[tj];
    int tmpk = beginB[tj];
    int maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

    while (tmpk < maxk && B[tj][tmpk] < TK) {
      tmpk++;
    }

    beginB[tj] = tmpk;
  }
}

// eachtime update TJ
void updateBeginBc() {
  // update beginBc
  for (int tk = TK; tk < TK + ((ISDYNAMICK) ? dynk : kkk); tk++) {
    if (tk > K)
      break;

    // int startj = beginBc[tk];
    int tmpj = beginBc[tk];
    int maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

    while (tmpj < maxj && Bc[tk][tmpj] < TJ) {
      tmpj++;
    }

    beginBc[tk] = tmpj;
  }
}

void updateBeginC() {}

void reinitialize_beginA() {
  for (int ti = 0; ti < I; ti++) {
    beginA[ti] = 0;
  }
}
void reinitialize_beginAc() {
  for (int tj = 0; tj < J; tj++) {
    beginAc[tj] = 0;
  }
}
void reinitialize_beginB() {
  for (int tj = 0; tj < J; tj++) {
    beginB[tj] = 0;
  }
}
void reinitialize_beginBc() {
  for (int tk = 0; tk < K; tk++) {
    beginBc[tk] = 0;
  }
}
void reinitialize_beginC() {
  /* for(int ti = 0; ti < I; ti ++){
       beginC[ti] = 0;
   }*/
}

// return 1 if I is before J in the interorder
// return 0 if J -> I
bool isIJ() {
  if (interorder == IJK || interorder == IKJ || interorder == KIJ)
    return 1;
  return 0;
}

// return 1 if J is before K in the interorder
// return 0 if K -> J
bool isJK() {
  if (interorder == JKI || interorder == JIK || interorder == IJK)
    return 1;
  return 0;
}

void updateTI() {
  TI += iii;

  if (isIJ()) {
    ALLupdateBeginAc();
  } else {
    updateBeginAc();
  }
}

void updateTJ() {
  TJ += ((ISDYNAMICJ) ? dynj : jjj);

  if (isIJ()) {
    updateBeginA();
  } else {
    AllupdateBeginA();
  }

  if (isJK()) {
    AllupdateBeginBc();
  } else {
    updateBeginBc();
  }
}

void updateTK() {
  TK += ((ISDYNAMICK) ? dynk : kkk);

  if (isJK()) {
    updateBeginB();
  } else {
    AllupdateBeginB();
  }
}

bool iterate_inner_loop() {
  if ((interorder == IJK) || (interorder == JIK)) {
    // adddyn
    // dynamicupdatek();
    if (TK + ((ISDYNAMICK) ? dynk : kkk) < K) {
      updateTK();
      if (ISDYNAMICK) {
        dynamicupdatek();
      }
      return 1;
    } else {
      TK += ((ISDYNAMICK) ? dynk : kkk);
      if (ISDYNAMICK) {
        dynamicupdatek();
      }
      return 0;
    }
  } else if ((interorder == IKJ) || (interorder == KIJ)) {
    // dynamicupdatej();
    if (TJ + ((ISDYNAMICJ) ? dynj : jjj) < J) {
      updateTJ();
      if (ISDYNAMICJ) {
        dynamicupdatej();
      }
      return 1;
    } else {
      TJ += ((ISDYNAMICJ) ? dynj : jjj);
      if (ISDYNAMICJ) {
        dynamicupdatej();
      }
      return 0;
    }
  } else if ((interorder == JKI) || (interorder == KJI)) {
    //  printf("####  %d %d %d\n", TI, iii, I);
    // dynamicupdatei();
    if (TI + iii < I) {
      updateTI();
      if (ISDYNAMICI) {
        dynamicupdatei();
      }
      return 1;

    } else {
      TI += iii;
      if (ISDYNAMICI) {
        dynamicupdatei();
      }
      return 0;
    }
  }

  return 0;
}

bool iterate_mid_loop() {
  if ((interorder == IKJ) || (interorder == JKI)) {
    // dynamicupdatek();
    if (TK + ((ISDYNAMICK) ? dynk : kkk) < K) {
      updateTK();
      if (ISDYNAMICK) {
        dynamicupdatek();
      }
      return 1;
    } else {
      TK += ((ISDYNAMICK) ? dynk : kkk);
      if (ISDYNAMICK) {
        dynamicupdatek();
      }
      return 0;
    }
  } else if ((interorder == IJK) || (interorder == KJI)) {
    // dynamicupdatej();
    if (TJ + ((ISDYNAMICJ) ? dynj : jjj) < J) {
      updateTJ();
      if (ISDYNAMICJ) {
        dynamicupdatej();
      }
      return 1;
    } else {
      TJ += ((ISDYNAMICJ) ? dynj : jjj);
      if (ISDYNAMICJ) {
        dynamicupdatej();
      }
      return 0;
    }
  } else if ((interorder == JIK) || (interorder == KIJ)) {
    // dynamicupdatei();
    if (TI + iii < I) {
      updateTI();
      if (ISDYNAMICI) {
        dynamicupdatei();
      }
      return 1;
    } else {
      TI += iii;
      if (ISDYNAMICI) {
        dynamicupdatei();
      }
      return 0;
    }
  }

  return 0;
}

bool iterate_outer_loop() {
  if ((interorder == KIJ) || (interorder == KJI)) {
    // dynamicupdatek();
    if (TK + ((ISDYNAMICK) ? dynk : kkk) < K) {
      updateTK();
      if (ISDYNAMICK) {
        dynamicupdatek();
      }
      return 1;
    } else {
      TK += ((ISDYNAMICK) ? dynk : kkk);
      if (ISDYNAMICK) {
        dynamicupdatek();
      }
      return 0;
    }
  } else if ((interorder == JIK) || (interorder == JKI)) {
    // dynamicupdatej();
    if (TJ + ((ISDYNAMICJ) ? dynj : jjj) < J) {
      updateTJ();
      if (ISDYNAMICJ) {
        dynamicupdatej();
      }
      return 1;
    } else {
      TJ += ((ISDYNAMICJ) ? dynj : jjj);
      if (ISDYNAMICJ) {
        dynamicupdatej();
      }
      return 0;
    }
  } else if ((interorder == IJK) || (interorder == IKJ)) {
    // dynamicupdatei();
    if (TI + iii < I) {
      updateTI();
      if (ISDYNAMICI) {
        dynamicupdatei();
      }
      return 1;
    } else {
      TI += iii;
      if (ISDYNAMICI) {
        dynamicupdatei();
      }
      return 0;
    }
  }

  return 0;
}

void reverse_I() {
  TI = 0;

  // reinitialize A
  // if((format == CR) || (format == CC)){
  if (isIJ()) {
    for (int tmpj = 0; tmpj < J; tmpj++) {
      beginAc[tmpj] = 0;
    }
  } else {
    for (int tmpj = TJ; tmpj < TJ + jjj; tmpj++) {
      beginAc[tmpj] = 0;
    }
  }
  //}
}

void reverse_J() {

  TJ = 0;
  // reinitialize A
  // if((format == RR) || (format == RC)){

  if (isIJ()) {
    for (int tmpi = TI; tmpi < TI + iii; tmpi++) {
      beginA[tmpi] = 0;
    }
  } else {
    for (int tmpi = 0; tmpi < I; tmpi++) {
      beginA[tmpi] = 0;
    }
  }
  // }

  // reinitialize Bc
  // if((format == RC) || (format == CC)){

  if (isJK()) {
    for (int tmpk = 0; tmpk < K; tmpk++) {
      beginBc[tmpk] = 0;
    }
  } else {
    for (int tmpk = TK; tmpk < TK + kkk; tmpk++) {
      beginBc[tmpk] = 0;
    }
  }

  //}
}

void reverse_K() {
  TK = 0;
  // reinitialize B
  // if((format == RR) || (format == CR)){

  if (isJK()) {

    for (int tmpj = TJ; tmpj < TJ + jjj; tmpj++) {
      beginB[tmpj] = 0;
    }
  } else {
    for (int tmpj = 0; tmpj < J; tmpj++) {
      beginB[tmpj] = 0;
    }
  }

  // }
}

void reverse_inner() {
  if ((interorder == IJK) || (interorder == JIK)) {
    reverse_K();
  } else if ((interorder == IKJ) || (interorder == KIJ)) {
    reverse_J();
  } else if ((interorder == JKI) || (interorder == KJI)) {
    reverse_I();
  }
}

void reverse_mid() {
  if ((interorder == IKJ) || (interorder == JKI)) {
    reverse_K();
  } else if ((interorder == IJK) || (interorder == KJI)) {
    reverse_J();
  } else if ((interorder == JIK) || (interorder == KIJ)) {
    reverse_I();
  }
}

void reinitialize_inner() {
  if ((interorder == IJK) || (interorder == JIK)) {
    reverse_K();
  } else if ((interorder == IKJ) || (interorder == KIJ)) {
    reverse_J();
  } else if ((interorder == JKI) || (interorder == KJI)) {
    reverse_I();
  }
}

void reinitialize_mid() {
  if ((interorder == IKJ) || (interorder == JKI)) {
    reverse_K();
  } else if ((interorder == IJK) || (interorder == KJI)) {
    reverse_J();
  } else if ((interorder == JIK) || (interorder == KIJ)) {
    reverse_I();
  }
}

void reinitialize_outer() {
  if ((interorder == KIJ) || (interorder == KJI)) {
    reverse_K();
  } else if ((interorder == JIK) || (interorder == JKI)) {
    reverse_J();
  } else if ((interorder == IJK) || (interorder == IKJ)) {
    reverse_I();
  }
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
int fullA, fullB, fullC;

bool checkAndLoadReuseA() {
  if ((interorder == IJK || interorder == JIK)) {
    // load inter-reuse A
    // the TK == 0 should get a ratio of inter-level cache hit (how many in the
    // buffer)

    // need to reaccess if the buffer can't hold the full A
    // int restDram = 0;
    // int restSram = 0;

    if (TK == 0) {

      Asizenow = 0;
      fulltagA = 0;
      if (format == RR || format == RC) {

        // on-chip fiber start

        for (int ti = TI; ti < TI + iii; ti++) {
          if (ti > I)
            break;
          Asizenow++;

          int startj = beginA[ti], tmpj = beginA[ti],
              maxj = offsetarrayA[ti + 1] - offsetarrayA[ti];

          while (tmpj < maxj && A[ti][tmpj] < jjj + TJ) {
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

      // haven't consider inconsistent format now
      if (format == CR || format == CC) {
      }

      return 1;
    } else {
      return 1;
    }
  } else {

    // if interorder not IJK or JIK, can not buffer A whatever the buffersize
    // so just set the fullA to TI-1 (the first place)
    fulltagA = 1;
    fullA = TI - 1;
  }

  return 0;

  // need: the reusable inter loop + not the first tile (need to load at the
  // first time)
  if ((interorder == IJK || interorder == JIK) && (TK != 0))
    return 1;
  return 0;
}

bool checkReuseB() {
  if ((interorder == JKI || interorder == KJI) && (TI != 0))
    return 1;
  return 0;
}

bool checkReuseC() {
  if ((interorder == IKJ || interorder == KIJ) && (TJ != 0))
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

  // we suppose a consistent format now, don't need the following
  return;

  // When need load A:
  // 2 scenario: 1) When A storage format mismatch with dataflow. (Otherwise
  // don't need to buffer A in 3 dataflow) 2) inter reuse A (load A at the first
  // loop, then don't need to load again)
  // -> the second scenario is not free!! need to alloc buffer for it; and can
  // only reuse the partition inside the buffer

  // mismatch of Gust

  Asizenow = 0;
  fulltagA = 0;

  if ((dataflow == Gust) && ((format == CC) || (format == CR))) {
    // implicit transform while loading

    // estimated fiberlet fragment waste
    Asize += ((fiberletlength + 1) / 2) * iii;

    // on-chip fiber current
    // This is why very bad before!!!
    Asize += iii;

    // Initialize
    for (int ti = TI; ti < TI + iii; ti++) {
      bufferedsizeA[ti] = 0;
    }

    for (int tj = TJ; tj < TJ + jjj; tj++) {
      if (tj > J)
        break;

      int starti = beginAc[tj], tmpi = beginAc[tj],
          maxi = offsetarrayAc[tj + 1] - offsetarrayAc[tj];

      while (tmpi < maxi) {
        int tmpindex = Ac[tj][tmpi];
        if (tmpindex >= TI + iii) {
          break;
        }
        tmpi++;

        bufferedsizeA[tmpindex]++;
      }

      int tmpsize = (tmpi - starti);

      if (Asizenow + tmpsize * 3 >= Asize) {
        if (!fulltagA) {
          fulltagA = 1;
          fullA = tj;
        }
        // cache the csc size of each col block
        // currsizeAc[tj] = tmpsize;
      } else {
        // currsizeAc[tj] = tmpsize;
        preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
        preA += memoryBandwidthWhole(tmpsize * 3 + 2);
        AccessByte += tmpsize * 2 + 2;

        // for each element need:
        // 1) get pos: one read (current position)
        // 2) add to chain: 1 data write; 1/block chain index write
        preSramAccess += sramWriteBandwidth(tmpsize);
        preSramAccess +=
            sramReadBandwidth(tmpsize + tmpsize / fiberletlength) * 3;
      }
    }
  }

  // mismatch of IP

  if ((dataflow == Inner) && ((format == CC) || (format == CR))) {
    // implicit transform while loading

    // estimated fiberlet fragment waste
    Asize += ((fiberletlength + 1) / 2) * iii;

    // on-chip fiber current
    // This is why very bad before!!!
    Asize += iii;

    for (int ti = TI; ti < TI + iii; ti++) {
      bufferedsizeA[ti] = 0;
    }

    for (int tj = TJ; tj < TJ + jjj; tj++) {
      if (tj > J)
        break;

      int starti = beginAc[tj], tmpi = beginAc[tj],
          maxi = offsetarrayAc[tj + 1] - offsetarrayAc[tj];

      while (tmpi < maxi) {
        int tmpindex = Ac[tj][tmpi];
        if (tmpindex >= TI + iii) {
          break;
        }
        tmpi++;

        bufferedsizeA[tmpindex]++;
      }

      int tmpsize = (tmpi - starti);

      if (Asizenow + tmpsize * 3 >= Asize) {
        if (!fulltagA) {
          fulltagA = 1;
          fullA = tj;
        }
        // cache the csc size of each col block
        // currsizeAc[tj] = tmpsize;
      } else {
        // currsizeAc[tj] = tmpsize;
        preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
        preA += memoryBandwidthWhole(tmpsize * 3 + 2);
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

  // mismatch of OP (different)

  if ((dataflow == Outer) && ((format == RC) || (format == RR))) {
    // implicit transform while loading

    // estimated fiberlet fragment waste
    Asize += ((fiberletlength + 1) / 2) * jjj;

    // on-chip fiber current

    Asize += jjj;

    // Initialize
    for (int tj = TJ; tj < TJ + jjj; tj++) {
      bufferedsizeA[tj] = 0;
    }

    for (int ti = TI; ti < TI + iii; ti++) {
      if (ti > I)
        break;

      int startj = beginA[ti], tmpj = beginA[ti],
          maxj = offsetarrayA[ti + 1] - offsetarrayA[ti];

      while (tmpj < maxj) {
        int tmpindex = A[ti][tmpj];
        if (tmpindex >= TJ + jjj) {
          break;
        }
        tmpj++;

        bufferedsizeA[tmpindex]++;
      }

      int tmpsize = (tmpj - startj);

      if (Asizenow + tmpsize * 3 >= Asize) {
        if (!fulltagA) {
          fulltagA = 1;
          fullA = ti;
        }
        // cache the csc size of each col block
        // currsizeAc[tj] = tmpsize;
      } else {
        // currsizeAc[tj] = tmpsize;
        preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
        preA += memoryBandwidthWhole(tmpsize * 3 + 2);
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
      int _TJ;
      // int _TK;

      for (tj = TJ; tj < TJ + jjj; tj++) {
        if (tj > J)
          break;

        if ((tj - TJ) < (jjj / 2)) {
          _TJ = 0;
        } else {
          _TJ = 1;
        }

        // on-chip fiber start
        Bsizenow++;

        int startk = beginB[tj], tmpk = beginB[tj],
            maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

        int halfk = beginB[tj];

        while (halfk < maxk && B[tj][halfk] < (kkk / 2) + TK) {
          halfk++;
        }
        tmpk = halfk;

        while (tmpk < maxk && B[tj][tmpk] < kkk + TK) {
          tmpk++;
        }

        int tmpsize = (tmpk - startk);

        updateDynamicTile(_TJ, (halfk - startk), tmpk - halfk);
        // Tcnt[_TJ][0] += (halfk-startk);
        // Tcnt[_TJ][1] += (tmpk-halfk);

        if (Bsizenow + tmpsize * 3 >= Bsize) {
          if (!fulltagB) {
            fulltagB = 1;
            fullB = tj;
          }
          //  currsizeB[tj] = tmpsize;
        } else {

          Bsizenow += tmpsize * 3;

          preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
          preB += memoryBandwidthWhole(tmpsize * 3 + 2);
          preSramAccess += sramWriteBandwidth(tmpsize * 3 + 2);
          AccessByte += tmpsize * 3 + 2;
        }
      }

      if (ISDYNAMICJ) {

        // dynamic growing if the buffer is not full!
        if (fulltagB == 0) {

          // start to grow from the last tj
          for (; tj < J; tj++) {

            // on-chip fiber start
            Bsizenow++;

            int startk = beginB[tj], tmpk = beginB[tj],
                maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

            while (tmpk < maxk && B[tj][tmpk] < kkk + TK) {
              tmpk++;
            }

            int tmpsize = (tmpk - startk);

            if (Bsizenow + tmpsize * 3 >= Bsize) {
              dynj = tj - TJ;
              break;
            } else {

              Bsizenow += tmpsize * 3;

              preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
              preB += memoryBandwidthWhole(tmpsize * 3 + 2);
              preSramAccess += sramWriteBandwidth(tmpsize * 3 + 2);
              AccessByte += tmpsize * 3 + 2;
            }
          }

          dynj = tj - TJ;

        } else {
          // the buffer is already full. don't need to increase. (will it
          // shrink? ) we can try both the two mode : shrink or not.

          dynj = jjj;
        }
      }
    }

    if ((dataflow == Gust) && ((format == CC) || (format == RC))) {
      // inconsistent format
      // implicit transform while loading

      // Unlike the consistent version, inconsistent version
      // can't do this

      // estimated fiberlet fragment waste
      Bsizenow += ((fiberletlength + 1) / 2) * jjj;

      // on-chip fiber current
      Bsizenow += jjj;

      // Initialize
      for (int tj = TJ; tj < TJ + jjj; tj++) {
        bufferedsizeB[tj] = 0;
      }

      for (int tk = TK; tk < TK + kkk; tk++) {
        if (tk > K)
          break;

        int startj = beginBc[tk], tmpj = beginBc[tk],
            maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

        while (tmpj < maxj) {
          int tmpindex = Bc[tk][tmpj];
          if (tmpindex >= TJ + jjj) {
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
          //   currsizeBc[tk] = tmpsize;
        } else {
          //   currsizeBc[tk] = tmpsize;
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
      for (tk = TK; tk < TK + kkk; tk++) {
        if (tk > K)
          break;

        // on-chip fiber start
        Bsizenow++;

        int startj = beginBc[tk], tmpj = beginBc[tk],
            maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

        while (tmpj < maxj && Bc[tk][tmpj] < jjj + TJ) {
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
          //  currsizeB[tj] = tmpsize;
        } else {

          Bsizenow += tmpsize * 3;

          preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
          preB += memoryBandwidthWhole(tmpsize * 3 + 2);
          preSramAccess += sramWriteBandwidth(tmpsize * 3 + 2);
          AccessByte += tmpsize * 3 + 2;
        }
      }

      // update the IP dynamic here
      if (ISDYNAMICK) {

        // dynamic growing if the buffer is not full!
        if (fulltagB == 0) {

          // start to grow from the last tk
          for (; tk < K; tk++) {

            // on-chip fiber start
            Bsizenow++;

            int startj = beginBc[tk], tmpj = beginBc[tk],
                maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

            while (tmpj < maxj && Bc[tk][tmpj] < jjj + TJ) {
              tmpj++;
            }

            int tmpsize = (tmpj - startj);

            if (Bsizenow + tmpsize * 3 >= Bsize) {
              dynk = tk - TK;
              break;
            } else {

              Bsizenow += tmpsize * 3;

              preDramAccess += memoryBandwidthWhole(tmpsize * 3 + 2);
              preB += memoryBandwidthWhole(tmpsize * 3 + 2);
              preSramAccess += sramWriteBandwidth(tmpsize * 3 + 2);
              AccessByte += tmpsize * 3 + 2;
            }
          }

          dynk = tk - TK;

        } else {
          // the buffer is already full. don't need to increase. (will it
          // shrink? ) we can try both the two mode : shrink or not.

          dynk = kkk;
        }
      }
    }

    if ((dataflow == Inner) && (((format == RR) || (format == CR)))) {
      // inconsistent format

      // estimated fiberlet fragment waste
      Bsizenow += ((fiberletlength + 1) / 2) * kkk;

      // on-chip fiber current
      Bsizenow += kkk;

      // initialize
      for (int tk = TK; tk < TK + ((ISDYNAMICK) ? dynk : kkk); tk++) {
        bufferedsizeB[tk] = 0;
      }

      for (int tj = TJ; tj < TJ + jjj; tj++) {
        if (tj > J)
          break;

        int startk = beginB[tj], tmpk = beginB[tj],
            maxk = offsetarrayB[tj + 1] - offsetarrayB[tj];

        while (tmpk < maxk) {
          int tmpindex = B[tj][tmpk];
          if (tmpindex >= TK + ((ISDYNAMICK) ? dynk : kkk)) {
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
          //   currsizeBc[tk] = tmpsize;
        } else {
          //   currsizeBc[tk] = tmpsize;
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

  // mismatch of the Gust and Inner have been considered in scenario 1
  if ((dataflow == Outer)) {

    Bsizenow = 0;
    fulltagB = 0;
    fullB = 0;

    // match: don't need buffer
    if ((format == RR) || (format == CR)) {
      // don't need pre load
      return;
    }

    // mismatch: buffered; same as Gust mismatch!
    if ((format == RC) || (format == CC)) {
      // inconsistent format
      // implicit transform while loading

      // estimated fiberlet fragment waste
      Bsizenow += ((fiberletlength + 1) / 2) * jjj;

      // on-chip fiber current
      Bsizenow += jjj;

      // Initialize
      for (int tj = TJ; tj < TJ + jjj; tj++) {
        bufferedsizeB[tj] = 0;
      }

      for (int tk = TK; tk < TK + kkk; tk++) {
        if (tk > K)
          break;

        int startj = beginBc[tk], tmpj = beginBc[tk],
            maxj = offsetarrayBc[tk + 1] - offsetarrayBc[tk];

        while (tmpj < maxj) {
          int tmpindex = Bc[tk][tmpj];
          if (tmpindex >= TJ + jjj) {
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
          //   currsizeBc[tk] = tmpsize;
        } else {
          //   currsizeBc[tk] = tmpsize;
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
void get_B_fiber(int jj, int ii) {

  // In Blocking Mode
  if (!ISCACHE) {

    // two decisions: 1) consistent or not; 2) buffer or not (may bypass)

    if (consistent_B()) {
      // B[jj] is on the buffer
      if (fulltagB == 0 || jj < fullB) {
        // hit!
        // different access with B format:
        // continuous or chained
        computeSramAccess += sramReadBandwidth(currsizeB[jj] * 3 + 2);

      } else {
        // B[jj] is not on the buffer, need to access dram
        // different access with B format
        // access one dram fiber all check all

        computeDramAccess += memoryBandwidthPE(currsizeB[jj] * 3 + 2);

        computeB += memoryBandwidthPE(currsizeB[jj] * 3 + 2);
        AccessByte += currsizeB[jj] * 3 + 2;
      }
    } else {
      // hit part (chained)
      computeSramAccess +=
          sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeB[jj] + 3) / 4);

      // miss part (need to check every uncached)

      if (fulltagB) {
        computeDramAccess +=
            (memoryBandwidthPE(3)) * ((long long)TK + kkk - fullB);
        computeB +=
            (memoryBandwidthPE(3)) * (long long)((long long)TK + kkk - fullB);
        AccessByte += 3 * (long long)((long long)TK + kkk - fullB);
      }
    }

  }
  // In cache Mode
  // address in cache mode is : fiberid + (relative << bias)  where relative =
  // (relative coordinate in fiber)/CACHEBLOCK
  else {
    int fibersize = currsizeB[jj] * 3 + 1;
    cacheAccessFiber(jj, fibersize, ii);
  }
}

// in IP
void get_B_fiber_col_iii(int kk, int iii) {

  // B[jj] is on the buffer
  if (fulltagB == 0 || kk < fullB) {
    // hit!
    // different access with B format:
    // continuous or chained
    computeSramAccess += sramReadBandwidth(currsizeBc[kk] * 3 + 2) * iii;

  } else {
    // B[jj] is not on the buffer, need to access dram
    // different access with B format
    // access one dram fiber all check all

    computeDramAccess += memoryBandwidthPE(currsizeBc[kk] * 3 + 2) * iii;

    computeB += memoryBandwidthPE(currsizeBc[kk] * 3 + 2) * iii;
    AccessByte += (currsizeBc[kk] * 3 + 2) * iii;
  }
}
void get_B_fiber_col(int kk) {
  if (consistent_B()) {
    // B[jj] is on the buffer
    if (fulltagB == 0 || kk < fullB) {
      // hit!
      // different access with B format:
      // continuous or chained
      computeSramAccess += sramReadBandwidth(currsizeBc[kk] * 3 + 2);

    } else {
      // B[jj] is not on the buffer, need to access dram
      // different access with B format
      // access one dram fiber all check all

      computeDramAccess += memoryBandwidthPE(currsizeBc[kk] * 3 + 2);

      computeB += memoryBandwidthPE(currsizeBc[kk] * 3 + 2);
      AccessByte += currsizeBc[kk] * 3 + 2;
    }
  } else {
    // hit part (chained)
    computeSramAccess +=
        sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeB[kk] + 3) / 4);

    // miss part (need to check every uncached)

    if (fulltagB) {
      computeDramAccess +=
          (memoryBandwidthPE(3)) * ((long long)TJ + jjj - fullB);
      computeB +=
          (memoryBandwidthPE(3)) * (long long)((long long)TJ + jjj - fullB);
      AccessByte += 3 * ((long long)TJ + jjj - fullB);
    }
  }
}

void get_A_fiber_col(int jj) {

  if (consistent_A()) {

    // A[ii] is on the buffer
    if (fulltagA == 0 || jj < fullA) {
      // hit
      computeSramAccess += sramReadBandwidth(currsizeAc[jj] * 3 + 2);
    } else {

      computeDramAccess += memoryBandwidthPE(currsizeAc[jj] * 3 + 2);
      computeA += memoryBandwidthPE(currsizeAc[jj] * 3 + 2);
      AccessByte += currsizeAc[jj] * 3 + 2;

      computeSramAccess += sramReadBandwidth(currsizeAc[jj] * 3 + 2) +
                           sramWriteBandwidth(currsizeAc[jj] * 3 + 2);

      if (cacheScheme == CACHE_SCHEME_INNER_SP) {
        // double A access in static FLRU scheme
        computeDramAccess += memoryBandwidthPE(currsizeAc[jj] * 3 + 2);
        computeA += memoryBandwidthPE(currsizeAc[jj] * 3 + 2);

        computeSramAccess += sramReadBandwidth(currsizeAc[jj] * 3 + 2) +
                             sramWriteBandwidth(currsizeAc[jj] * 3 + 2);
      }
    }
  } else {

    // hit part (chained)
    computeSramAccess +=
        sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeA[jj] + 3) / 4);

    // miss part (need to check every uncached)

    if (fulltagA) {
      computeDramAccess +=
          (memoryBandwidthPE(3)) * ((long long)TI + iii - fullA);
      computeB +=
          (memoryBandwidthPE(3)) * (long long)((long long)TI + iii - fullA);
      AccessByte += 3 * ((long long)TI + iii - fullA);
    }
  }
}

void get_A_fiber(int ii) {

  if (consistent_A()) {

    // A[ii] is on the buffer
    if (fulltagA == 0 || ii < fullA) {
      // hit
      computeSramAccess += sramReadBandwidth(currsizeA[ii] * 3 + 2);

      if (cacheScheme == CACHE_SCHEME_INNER_SP) {
        // double A access in static FLRU scheme
        computeSramAccess += sramReadBandwidth(currsizeA[ii] * 3 + 2);
      }
    } else {
      computeDramAccess += memoryBandwidthPE(currsizeA[ii] * 3 + 2);
      computeA += memoryBandwidthPE(currsizeA[ii] * 3 + 2);
      AccessByte += currsizeA[ii] * 3 + 2;

      computeSramAccess += sramReadBandwidth(currsizeA[ii] * 3 + 2) +
                           sramWriteBandwidth(currsizeA[ii] * 3 + 2);

      if (cacheScheme == CACHE_SCHEME_INNER_SP) {
        // double A access in static FLRU scheme
        computeDramAccess += memoryBandwidthPE(currsizeA[ii] * 3 + 2);
        computeA += memoryBandwidthPE(currsizeA[ii] * 3 + 2);

        computeSramAccess += sramReadBandwidth(currsizeA[ii] * 3 + 2) +
                             sramWriteBandwidth(currsizeA[ii] * 3 + 2);
      }
    }
  } else {

    // hit part (chained)
    computeSramAccess +=
        sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeA[ii] + 3) / 4);

    // miss part (need to check every uncached)

    if (fulltagA) {
      computeDramAccess +=
          (memoryBandwidthPE(3)) * ((long long)TJ + jjj - fullA);
      computeA +=
          (memoryBandwidthPE(3)) * (long long)((long long)TJ + jjj - fullA);
      AccessByte += 3 * ((long long)TJ + jjj - fullA);
    }
  }
}

void update_c_fiber(int jj) {

  for (int k1 = beginB[jj]; k1 < beginB[jj] + currsizeB[jj]; k1++) {
    tmpC[B[jj][k1]] = 1;
  }
}

// need to store: all the stored C coords,
// in order to calculate the current and next state occuppied buffer size
void updateCAccess(int ii) {

  // for buffered C:
  if ((Csize >= 100.0) && ((interorder == IKJ) || (interorder == KIJ))) {

    // check the delta buffer: how many new C elements (indicate how many
    // increase)
    int deltaC = 0;
    // int oldsize = bufferedClen[ii];
    for (int k1 = TK; k1 < TK + ((ISDYNAMICK) ? dynk : kkk); k1++) {
      if (tmpC[k1]) {
        // the k1 is a new element
        if (bufferedC[ii].find(k1) == bufferedC[ii].end()) {
          deltaC++;

          bufferedC[ii].insert(k1);
        }
      }
    }
    // update the size
    bufferedClen[ii] += deltaC;

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

      for (int i = TI; i < TI + iii; i++) {
        bufferedC[i].clear();
        bufferedC[i] = std::set<int>();
        bufferedClen[i] = 0;
      }
    }
  } else {
    // following is for the stream C
    // update with compute
    int cntc = 0;

    for (int k1 = TK; k1 < TK + ((ISDYNAMICK) ? dynk : kkk); k1++) {
      if (tmpC[k1]) {
        cntc++;
      }
    }
    // write into DRAM during the computation
    computeDramAccess += memoryBandwidthPE(cntc * 3);
    computeC += memoryBandwidthPE(cntc * 3);
    AccessByte += cntc * 3;

    if (jjj != J) {
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

void get_B_fibers(int ii) {
  if (dataflow == Gust) {
    int tmpj = beginA[ii];
    int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];

    // tmpc = 0
    for (int k1 = TK; k1 < TK + kkk; k1++) {
      tmpC[k1] = 0;
    }

    while (tmpj < maxj && A[ii][tmpj] < TJ + ((ISDYNAMICJ) ? dynj : jjj)) {
      // coordinate of required B fiber
      int jj = A[ii][tmpj];

      get_B_fiber(jj, ii);

      computePE += currsizeB[jj];
      elements_processed_since_last_adjustment += currsizeB[jj];

      update_c_fiber(jj);

      tmpj++;

      if (offsetarrayA[ii] + tmpj >= offsetarrayA[ii + 1]) {
        break;
      }
    }

    // update A access
    if (consistent_A()) {

      // A[ii] is on the buffer

      // updated: add the interorder judge.
      // totally can't reuse if not **K
      if ((interorder == IJK || interorder == JIK)) {
        if (fulltagA == 0 || ii < fullA) {
          // hit
          computeSramAccess += sramReadBandwidth((tmpj - beginA[ii]) * 3);
          if (cacheScheme == CACHE_SCHEME_INNER_SP) {
            computeSramAccess += sramReadBandwidth((tmpj - beginA[ii]) * 3);
          }
        } else {
          computeDramAccess += memoryBandwidthPE((tmpj - beginA[ii]) * 3);
          computeA += memoryBandwidthPE((tmpj - beginA[ii]) * 3);
          AccessByte += (tmpj - beginA[ii]) * 3;

          computeSramAccess += sramReadBandwidth((tmpj - beginA[ii]) * 3) +
                               sramWriteBandwidth((tmpj - beginA[ii]) * 3);

          if (cacheScheme == CACHE_SCHEME_INNER_SP) {
            computeDramAccess += memoryBandwidthPE((tmpj - beginA[ii]) * 3);
            computeA += memoryBandwidthPE((tmpj - beginA[ii]) * 3);
            computeSramAccess += sramReadBandwidth((tmpj - beginA[ii]) * 3) +
                                 sramWriteBandwidth((tmpj - beginA[ii]) * 3);
          }
        }
      } else {
        computeDramAccess += memoryBandwidthPE((tmpj - beginA[ii]) * 3);
        computeA += memoryBandwidthPE((tmpj - beginA[ii]) * 3);
        AccessByte += (tmpj - beginA[ii]) * 3;

        computeSramAccess += sramReadBandwidth((tmpj - beginA[ii]) * 3) +
                             sramWriteBandwidth((tmpj - beginA[ii]) * 3);

        if (cacheScheme == CACHE_SCHEME_INNER_SP) {
          computeDramAccess += memoryBandwidthPE((tmpj - beginA[ii]) * 3);
          computeA += memoryBandwidthPE((tmpj - beginA[ii]) * 3);

          computeSramAccess += sramReadBandwidth((tmpj - beginA[ii]) * 3) +
                               sramWriteBandwidth((tmpj - beginA[ii]) * 3);
        }
      }

    } else {

      // hit part (chained)
      computeSramAccess +=
          sramReadBandwidth(fiberletlength * 3) * ((bufferedsizeB[ii] + 3) / 4);

      // miss part (need to check every uncached)

      if (fulltagA) {
        computeDramAccess +=
            (memoryBandwidthPE(3)) * ((long long)TJ + jjj - fullA);
        computeA +=
            (memoryBandwidthPE(3)) * (long long)((long long)TJ + jjj - fullA);
        AccessByte += 3 * ((long long)TJ + jjj - fullA);
      }
    }

    // update C access
    updateCAccess(ii);
  }
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
    needsize = currsizeA[ii] * 4 + 1;
  }
  // FLFU mode; don't need next pointer (*3)
  else if (cacheScheme == 66 || cacheScheme == CACHE_SCHEME_FLFU) {
    needsize = currsizeA[ii] * 3;
  }

  // can't prefetch this row now
  if (prefetchNow + needsize >= prefetchSize) {
    return 0;
  }

  prefetchNow += needsize;

  int tmpj = beginA[ii];
  int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];

  while (tmpj < maxj && A[ii][tmpj] < TJ + ((ISDYNAMICJ) ? dynj : jjj)) {
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
      int fibersize = currsizeB[jj] * 3;
      for (int tmpcurr = 0; tmpcurr < fibersize; tmpcurr += CACHEBLOCK) {
        long long tmpaddr = getCacheAddr(jj, tmpcurr / CACHEBLOCK);

        int _set = getSet2(tmpaddr);
        int _tag = getTag2(tmpaddr);

        bool needhalf = 0;
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
              needhalf = 1;
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
                    needhalf = 1;
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
        if (needhalf) {
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

    if (offsetarrayA[ii] + tmpj >= offsetarrayA[ii + 1]) {
      break;
    }
  }

  return 1;
}

// void initialize_adaptive_prefetch(long long nnzA, long long nnzB, int K, int J,
//                                   int T_J) {
void initialize_adaptive_prefetch(long long, long long, int, int, int) {
  // --- Offline Phase ---
  // double avg_nonzero_length_B;
  // if (K > 0 && T_J > 0) {
  //   avg_nonzero_length_B = static_cast<double>(nnzB) / K;
  // } else {
  //   avg_nonzero_length_B = 1.0;
  // }

  current_prefetch_size = 1.0 / 128.0;

  prefetchSize = current_prefetch_size * inputcachesize;
  cachesize = inputcachesize - prefetchSize;
  setSET();

  sa_iteration_k = 0;
  previous_prefetch_size = current_prefetch_size;
  best_prefetch_size = current_prefetch_size;
  last_iteration_data_miss_rate = 1.0;
  best_data_miss_rate = 1.0;

  adjustment_interval = estEffMAC / 500;
  if (adjustment_interval == 0)
    adjustment_interval = 1000;

  elements_processed_since_last_adjustment = 0;

  prefetch_discards = 0;
  data_access_misses = 0;
  data_access_hit = 0;
  data_access_total = 0;
}

int get_num_samples(double current_temperature) {

  if (current_temperature > SA_INITIAL_TEMP * 0.5) {
    return 4;
  } else if (current_temperature > SA_INITIAL_TEMP * 0.1) {
    return 8;
  } else {
    return 16;
  }
}

bool lastaccept = 1;

double SA_FINAL_TEMP = 0.000001;

bool SAstage = 0;

void update_prefetch_size() {
  double temperature = SA_INITIAL_TEMP * pow(SA_COOLING_RATE, sa_iteration_k);

  int num_samples = get_num_samples(temperature);

  sa_iteration_k++;

  // printf("---%d %d %lf\n", sa_iteration_k, num_samples, temperature);

  if ((sa_iteration_k % num_samples) != 0) {
    return;
  }

  if (lastaccept == 0) {
    double current_data_miss_rate =
        1.0 - ((double)(data_access_hit) / data_access_total);
    last_iteration_data_miss_rate = current_data_miss_rate;
    lastaccept = 1;

    double current_discard_rate;
    if (prefetch_increments == 0) {
      current_discard_rate = 0;
    } else {
      current_discard_rate = ((double)prefetch_discards) / prefetch_increments;
    }
    // double current_no_counter_miss_rate = static_cast<double>(data_access_misses) / data_access_total;
    // printf("Discard Rate: %lf, %d %d \n", current_discard_rate,
    //        prefetch_discards, prefetch_increments);

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

          SAstage = 1;
          double perturbation =
              ((static_cast<double>(rand()) / RAND_MAX) - 0.5) * 0.2;
          current_prefetch_size *= (1.0 + perturbation);
        }
      }
    }

    current_prefetch_size = std::max(current_prefetch_size, 1.0 / 256.0);
    current_prefetch_size = std::min(current_prefetch_size, 0.10);

    prefetchSize = current_prefetch_size * inputcachesize;
    cachesize = inputcachesize - prefetchSize;

    elements_processed_since_last_adjustment = 0;
    prefetch_discards = 0;
    prefetch_increments = 0;
    data_access_misses = 0;
    data_access_hit = 0;
    data_access_total = 0;

    return;
  }

  double current_data_miss_rate =
      1.0 - ((double)(data_access_hit) / data_access_total);

  double delta_M = (double)((1.0 - last_iteration_data_miss_rate) -
                            (1.0 - current_data_miss_rate)) /
                   (1.0 - last_iteration_data_miss_rate);

  bool accept_change = false;
  if (delta_M < 0) {
    accept_change = true;
  } else {
    double acceptance_prob = exp(-delta_M / temperature);
    double random_val = static_cast<double>(rand()) / RAND_MAX;
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
      best_prefetch_size = current_prefetch_size;
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
    data_access_misses = 0;
    data_access_hit = 0;
    data_access_total = 0;

    return;
  }

  double current_discard_rate;
  if (prefetch_increments == 0) {
    current_discard_rate = 0;
  } else {
    current_discard_rate = ((double)prefetch_discards) / prefetch_increments;
  }
  // double current_no_counter_miss_rate = static_cast<double>(data_access_misses) / data_access_total;
  // printf("Discard Rate: %lf, %d %d \n", current_discard_rate,
  // prefetch_discards,
  //       prefetch_increments);

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

        SAstage = 1;
        double perturbation =
            ((static_cast<double>(rand()) / RAND_MAX) - 0.5) * 1.0;
        current_prefetch_size *= (1.0 + perturbation);
      }
    }
  }

  current_prefetch_size = std::max(current_prefetch_size, 1.0 / 256.0);
  current_prefetch_size = std::min(current_prefetch_size, 0.1);

  prefetchSize = current_prefetch_size * inputcachesize;
  cachesize = inputcachesize - prefetchSize;

  elements_processed_since_last_adjustment = 0;
  prefetch_discards = 0;
  prefetch_increments = 0;
  data_access_misses = 0;
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

  if ((dataflow == Inner) || (dataflow == Gust)) {

    if (dataflow == Gust) {

      prefetchNow = 0;

      // all prefetch scheme
      if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == 66 ||
          cacheScheme == CACHE_SCHEME_FLFU || cacheScheme == CACHE_SCHEME_INNER_SP || cacheScheme == CACHE_SCHEME_SPARCH) {
        // reinitialize the next pointer for FLRU
        if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == CACHE_SCHEME_INNER_SP ||
            cacheScheme == CACHE_SCHEME_SPARCH) {
          for (int j1 = TJ; j1 < TJ + jjj; j1++) {
            if (j1 > J)
              break;
            while (!nextposvector[j1].empty()) {
              nextposvector[j1].pop();
            }
          }
        }
        // reinitialize the LFU tag for FLFU
        if (cacheScheme == 66) {
          for (int j1 = TJ; j1 < TJ + jjj; j1++) {
            LFUtag[j1] = 0;
          }
        }

        // first prefill the prefetch window
        for (int ii = 0; prefetchNow < prefetchSize && ii < iii; ii++) {
          if (TI + ii > I)
            break;

          prefetchRowNow = TI + ii;
          // return 0 if can't prefetch that row now
          if (!prefetchrow(TI + ii)) {
            break;
          }
        }
      }

      for (int ii = 0; ii < iii; ii++) {
        if (TI + ii > I)
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
            needsize = currsizeA[TI + ii] * 4 + 1;
          }
          // FLFU mode; don't need next pointer (*3)
          if (cacheScheme == 66 || cacheScheme == CACHE_SCHEME_FLFU) {
            needsize = currsizeA[TI + ii] * 3;
          }

          if (prefetchNow > needsize) {
            prefetchNow -= needsize;
          }

          // need to prefetch the next ii+prefetchsize+1
          if (prefetchNow >= prefetchSize)
            continue;

          while (prefetchRowNow < TI + iii) {
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

    if (dataflow == Inner) {

      // update B here
      for (int k = TK; k < TK + ((ISDYNAMICK) ? dynk : kkk); k++) {
        get_B_fiber_col_iii(k, iii);
      }

      for (int ii = 0; ii < iii; ii++) {

        // int cnew = 0;
  int cnow = 0;

        // update A
        // get A
        get_A_fiber(TI + ii);

        // update C

        int tmpj = beginA[TI + ii];
        int maxj = offsetarrayA[TI + ii + 1] - offsetarrayA[TI + ii];

        // tmpc = 0
        for (int k1 = TK; k1 < TK + ((ISDYNAMICK) ? dynk : kkk); k1++) {
          tmpC[k1] = 0;
        }

        while (tmpj < maxj && A[TI + ii][tmpj] < TJ + jjj) {
          // coordinate of required B fiber
          int jj = A[TI + ii][tmpj];

          update_c_fiber(jj);

          tmpj++;

          if (offsetarrayA[TI + ii] + tmpj >= offsetarrayA[TI + ii + 1]) {
            break;
          }
        }

        updateCAccess(TI + ii);

        continue;

        for (int kk = 0; kk < kkk; kk++) {
          // get B
          get_B_fiber_col(TK + kk);

          // calculate if there is an intersect between A & B
          int tmpja = beginA[TI + ii],
              maxja = offsetarrayA[TI + ii + 1] - offsetarrayA[TI + ii];
          int tmpjb = beginBc[TK + kk],
              maxjb = offsetarrayBc[TK + kk + 1] - offsetarrayBc[TK + kk];

          bool findflag = 0;

          while (tmpja < maxja && A[TI + ii][tmpja] < TJ + jjj) {

            int findj = A[TI + ii][tmpja];

            while (tmpjb < maxjb && Bc[TK + kk][tmpjb] < TJ + jjj) {

              if (Bc[TK + kk][tmpjb] >= findj) {
                break;
              }
              tmpjb++;
            }

            // need to assure that b exit first!!
            // find the same index!
            if (tmpjb < maxjb && Bc[TK + kk][tmpjb] == findj) {
              findflag = 1;
              break;
            }

            tmpja++;
          }

          // get a C[TI+ii][TK+kk]
          if (findflag) {
            // if is the new index all update the old index
            cnow++;

          } else {
            // zero C; don't need extra operation
          }
        }

        computeDramAccess += memoryBandwidthPE(cnow * 3);
        computeC += memoryBandwidthPE(cnow * 3);
        AccessByte += cnow * 3;
      }
    }
  }
  if ((dataflow == Outer)) {

    for (int jj = 0; jj < jjj; jj++) {
      get_A_fiber_col(TJ + jj);

      // use a dumb jj
      get_B_fiber(TJ + jj, jj);
    }

    fulltagC = 0;
    Csizenow = 0;

    for (int ii = TI; ii < TI + iii; ii++) {
      int tmpj = beginA[ii];
      int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];

      // tmpc = 0
      for (int k1 = TK; k1 < TK + kkk; k1++) {
        tmpC[k1] = 0;
      }

      while (tmpj < maxj && A[ii][tmpj] < TJ + jjj) {
        // coordinate of required B fiber
        int jj = A[ii][tmpj];

        computePE += currsizeB[jj];

        update_c_fiber(jj);

        tmpj++;

        if (offsetarrayA[ii] + tmpj >= offsetarrayA[ii + 1]) {
          break;
        }
      }

      int cntc = 0;
      for (int k1 = TK; k1 < TK + kkk; k1++) {
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
              sramReadBandwidth(cntc * 3 + 2) * (jjj / mergecnt);
        }
      } else {
        computeDramAccess += memoryBandwidthPE(cntc * 3 + 2) * (jjj / mergecnt);
        computeC += memoryBandwidthPE(cntc * 3 + 2) * (jjj / mergecnt);
        AccessByte += (cntc * 3 + 2) * (jjj / mergecnt);
      }
    }
  }

  totalCycle += max(computePE / PEcnt, max(computeDramAccess / PEcnt,
                                           computeSramAccess / sramBank));
  calCycle += max(computePE / PEcnt,
                  max(computeDramAccess / PEcnt, computeSramAccess / sramBank));

  totalSram += computeSramAccess / sramBank;
  totalDram += computeDramAccess / PEcnt;
  totalPE += computePE / PEcnt;
}

void post_calculate_store() {

  if (checkReuseC()) {
    return;
  }
}

int minBlock = 2000;

int getkbound() {
  return minBlock;
  return max(23, min(minBlock, K / minBlock));
}

int getjbound() {
  return minBlock;
  return max(23, min(minBlock, J / minBlock));
}

int getibound() {
  return minBlock;
  return max(23, min(minBlock, I / minBlock));
}

void configPartial(float partialA, float partialB, float partialC) {

  Asize = cachesize * partialA;
  Bsize = cachesize * partialB;
  Csize = cachesize * partialC;

  // no C reuse -> only A/B
  if ((interorder != JIK) && (interorder != JKI)) {
  }
}

void initialize_simulator() {
  // alloacte memory
  try {
    if(bufferedC == nullptr) bufferedC = new set<int>[I]();
    if(bufferedClen == nullptr) bufferedClen = new int[I]();
    if(beginA == nullptr) beginA = new int[I]();
    if(beginB == nullptr) beginB = new int[J]();

    if(beginAc == nullptr) beginAc = new int[J]();
    if(beginBc == nullptr) beginBc = new int[K]();

    // if(begin == nullptr) new int[];

    if(currsizeA == nullptr) currsizeA = new int[I]();
    if(currsizeAc == nullptr) currsizeAc = new int[J]();
    if(currsizeB == nullptr) currsizeB = new int[J]();
    if(currsizeBc == nullptr) currsizeBc = new int[J]();
    // if(currsizeC == nullptr) new int[K];

    if(bufferedsizeA == nullptr) bufferedsizeA = new int[I]();
    if(bufferedsizeB == nullptr) bufferedsizeB = new int[J]();
    // if(bufferedsizeC == nullptr) new int[K];

    if(tmpC == nullptr) tmpC = new int[K]();

    if(LFUtag == nullptr) LFUtag = new int[J]();
    if(nextposvector == nullptr) nextposvector = new queue<int>[J]();
  } catch (const std::bad_alloc &e) {
    std::cerr << "Error allocating memory for " << e.what() << std::endl;
    std::exit(1);
  }
  
}

void deinitialize_simulator() {
  if(bufferedC != nullptr) delete[] bufferedC;
  if(bufferedClen != nullptr) delete[] bufferedClen;
  if(beginA != nullptr) delete[] beginA;
  if(beginB != nullptr) delete[] beginB;

  if(beginAc != nullptr) delete[] beginAc;
  if(beginBc != nullptr) delete[] beginBc;

  // if(begin != nullptr) delete[] int[];

  if(currsizeA != nullptr) delete[] currsizeA;
  if(currsizeAc != nullptr) delete[] currsizeAc;
  if(currsizeB != nullptr) delete[] currsizeB;
  if(currsizeBc != nullptr) delete[] currsizeBc;
  // if(currsizeC != nullptr) delete[] currsizeC;

  if(bufferedsizeA != nullptr) delete[] bufferedsizeA;
  if(bufferedsizeB != nullptr) delete[] bufferedsizeB;
  // if(bufferedsizeC != nullptr) delete[] bufferedsizeC;

  if(tmpC != nullptr) delete[] tmpC;
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
      memset(virtualValid, 0, sizeof(bool) * SET * VIRTUALSETASSOC);
    }

    memset(PosOrig, 0, sizeof(short) * SET * SETASSOC);
    memset(vPosOrig, 0, sizeof(short) * SET * SETASSOC);
  }

  // reinitialize buffer c
  Csizenow = 0;
  for (int i = 0; i < I; i++) {
    bufferedC[i].clear();
    bufferedC[i] = std::set<int>();
    bufferedClen[i] = 0;
  }

  // reinitialize management dtaa
  reinitialize_beginA();
  reinitialize_beginAc();
  reinitialize_beginB();
  reinitialize_beginBc();
  reinitialize_beginC();
}

int getcntc(int ii) {
  int tmpj = beginA[ii];
  int maxj = offsetarrayA[ii + 1] - offsetarrayA[ii];

  for (int k1 = 0; k1 < K; k1++) {
    tmpC[k1] = 0;
  }

  while (tmpj < maxj && A[ii][tmpj] < J) {
    // coordinate of required B fiber
    int jj = A[ii][tmpj];

    update_c_fiber(jj);

    tmpj++;

    if (offsetarrayA[ii] + tmpj >= offsetarrayA[ii + 1]) {
      break;
    }
  }

  int cntc = 0;

  for (int k1 = 0; k1 < K; k1++) {
    if (tmpC[k1]) {
      cntc++;
    }
  }

  return cntc;
}

// merge the partial C generated at each tile
// Read DRAM: amount of sum of all partialC offload
// Write DRAM: amount of nnzC
void postTileMerge() {
  // another way to realize this is to add once read each time when we write C
  // already added in the C writing position for gust and IP

  if (dataflow == Gust) {
    if (jjj != J) {

      for (int ii = 0; ii < I; ii++) {

        int cntc = getcntc(ii);

        computeDramAccess += memoryBandwidthPE(cntc * 3);
        postC += memoryBandwidthPE(cntc * 3);
        AccessByte += cntc * 3;
      }
    }
  }

  // calculate the inter-cost of outer
  if ((dataflow == Outer)) {

    for (int ii = 0; ii < I; ii++) {

      int cntc = getcntc(ii);

      postDramAccess += memoryBandwidthPE(cntc * 3) * (ttj);
      postC += memoryBandwidthPE(cntc * 3) * (ttj);
      AccessByte += cntc * 3 * ttj;
    }
    //   analyze_statistics();
  }

  postSramAccess /= sramBank;
  postDramAccess /= PEcnt;

  totalCycle += max(postDramAccess, postSramAccess);
  postCycle += max(postDramAccess, postSramAccess);
}

void run() {
  reinitialize();

  if (adaptive_prefetch) {
    initialize_adaptive_prefetch(nzA, nzB, K, J, jjj);
  }

  if (iii > I)
    iii = I;
  if (jjj > J)
    jjj = J;
  if (kkk > K)
    kkk = K;

  // initialize the finetune tile to the selected tile
  initialTileSize();

  // initialize Tcnt before each tile running
  reinitializecnt();

  // reinitialize**: TI/TJ/TK to 0;  put beginA/B to 0
  reinitialize_outer();
  do {

    reinitialize_mid();

    do {

      reinitialize_inner();
      do {

        initialDynamicTile();

        // need to initialize the cache each time change the tile
        if (ISCACHE) {
          initializeCacheValid();
        }

        // force for just test
        //  ensure the begin is right (with TI,TJ,TK)
        // forcebeginA();
        // forcebeginB();

        pre_calculate_load();

        updateBlockA();
        updateBlockB();

        calculate();

        post_calculate_store();

        // adddyn
        // dynamically update the estimate tile here.
        // don't change the actual TI/TJ/TK(iii, jjj, kkk in the code) here,
        // only change the _TI/_TJ/_TK(_iii, _jjj, _kkk in the code) here update
        // the _TI/_TJ/_TK to TI/TJ/TK when actually updating TI/TJ/TK (decide
        // by the interorder) this means will change immediately when the update
        // dimension is the inner-most dimension. here we use Tcnt00, Tcnt01,
        // Tcnt10, Tcnt11 to udpate _TI/_TJ/_TK

        update_T();

      } // update TI/TJ/TK; update beginA/B/C if TI/J/K don't overflow
      while (iterate_inner_loop());

    } while (iterate_mid_loop());

  } while (iterate_outer_loop());

  postTileMerge();

  analyze_statistics();
}

void runTile(bool isest, int /* iii */, int jjj, int kkk, long long tti,
             long long ttj, long long ttk, long long SmallestTile) {

  // only prunning in the estimation mode
  if (isest) {
    long long mosttotalonchip = (nzB / (tti * ttj * ttk)) * 3LL;

    if (mosttotalonchip * 100LL < cachesize) {
      return;
    }

    prefetchNow = 0;
    prefetchRowNow = 0;

    if (cacheScheme == 6 || cacheScheme == 7 || cacheScheme == CACHE_SCHEME_INNER_SP ||
        cacheScheme == CACHE_SCHEME_SPARCH) {
      for (int j = 0; j < J; j++) {
        while (!nextposvector[j].empty()) {
          nextposvector[j].pop();
        }
      }
    }
    if (cacheScheme == 66) {
      for (int j = 0; j < J; j++) {
        LFUtag[j] = 0;
      }
    }

    // pruning
    if (((long long)jjj * kkk) * 4 < SmallestTile) {
      return;
    }
  }

  // deal with the opt metadata
  if (ISCACHE && (cacheScheme == 6 || cacheScheme == 7)) {

    cachesize = inputcachesize - prefetchSize;
    cachesize -= kkk * 2;

    if (cachesize < 0) {
      puts("!!!!!! metadata out of range!!!!!!!!!!");
      fflush(stdout);
      return;
    }

    setSET();
  }

  if (ISCACHE && (cacheScheme == 66)) {

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

  if (ISCACHE && (cacheScheme == CACHE_SCHEME_FLFU)) {

    cachesize = inputcachesize - prefetchSize;

    setSET();
  }

  if (!ISCACHE) {
    if (PartialConfig == 1) {
      // 100% B
      configPartial(0, 1, 0);
      interorder = InterOrder(2); // JKI
    }
    if (PartialConfig == 2) {
      // 50%B + 50%A
      configPartial(0.5, 0.5, 0);
      interorder = InterOrder(0); // IJK
    }
    if (PartialConfig == 3) {
      // 50%B + 50%C
      configPartial(0, 0.5, 0.5);
      interorder = InterOrder(1); // IKJ
    }
    // ********* Two Dyanmic Config ***********
    if (PartialConfig == 4) {
      // dynamic : 100%B
      ISDYNAMICJ = 1;
      configPartial(0, 1, 0);
      interorder = InterOrder(1); // IKJ
    }
    if (PartialConfig == 5) {
      // dynamic: 50%B 50%C
      ISDYNAMICJ = 1;
      configPartial(0, 0.5, 0.5);
      interorder = InterOrder(1); // IKJ
    }
    if (PartialConfig == 6) {
      configPartial(0, 1, 0);
      interorder = InterOrder(1); // IKJ
    }
  }

  if (ISCACHE == 1) {
    // need to allocate extra tag space in address mode
    // the address space is depends on the tiling size (equal to jjj)
    // need to update: cachesize (actually Bsize?) + SET + SETLOG
    if ((cacheScheme == 4) || (cacheScheme == 5) || (cacheScheme == 7)) {
      // need to add back after this calculation
      cachesize = inputcachesize;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);
    }

    hitcnt = 0;
    misscnt = 0;
  }

  if (!isest) {
    //  cout <<  printInterOrder[interorder];
    //  printf("   %d %d %d %d    !!  %d %d %d\n", iii, jjj, kkk, PartialConfig,
    //  cachesize, SET, SETLOG);
  }
  fflush(stdout);

  // run();
  if (isest) {
    gustest(0);
  } else {
    run();
  }

  // }
}
