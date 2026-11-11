#include "pe.h"
#include "headers.h"
#include "memory.h"
#include "prefetch.h"
#include "queue.h"
#include "statistics.h"

#include <deque>

struct PE {
  bool multiplying;
  long long waitStart;     // when it started waiting, for the stall stats
};
std::vector<PE> pes;

std::vector<long long> peBusy, peStallA, peStallB, peIdle, peDoneAt;

long long stallA = 0, stallB = 0, stallC = 0;

void initPEs() {
  pes.assign(PEcnt, PE());
  peBusy.assign(PEcnt, 0);
  peStallA.assign(PEcnt, 0);
  peStallB.assign(PEcnt, 0);
  peIdle.assign(PEcnt, 0);
  peDoneAt.assign(PEcnt, 0);
}

bool peReady(int pe) {
  return !pes[pe].multiplying;
}

// check if any fibers are waiting in queue. If not, go idle
void peTakeRow(std::any any) {
  int pe = std::any_cast<int>(any);

  if (readyFibers.empty()) {
    pes[pe].multiplying = false;
    return;
  }

  peFiberReady(PEArgs{readyFibers.front(), pe});
  readyFibers.pop();
}

// Start processing ready fiber
void peFiberReady(std::any any) {
  PEArgs args = std::any_cast<PEArgs>(any);
  int pe = args.pe;
  int fiber = args.fiberArgs.fiber;

  if (!pes[pe].multiplying) peIdle[pe] += simTime - pes[pe].waitStart;

  pes[pe].waitStart = simTime;
  pes[pe].multiplying = true;
  peBusy[pe] += currsizeB[fiber];
  perPEWork[pe] += currsizeB[fiber];

  schedule(peRowDone, args, currsizeB[fiber]); // 1 multiply per cycle
}

// C goes to DRAM, the PE doesn't wait for it and takes the next row
void peRowDone(std::any any) {
  PEArgs args = std::any_cast<PEArgs>(any);
  int pe = args.pe;
  double cDramWords = args.fiberArgs.cDramWords;
  PE &p = pes[pe];
  bool endOfTile = TI + currentII >= I;
  if (cDramWords > 0) {
    cFiberCount++;
    schedule(sendToDRAM, DRAMRequest{C_ROW, args.fiberArgs.row, cDramWords, endOfTile, simTime});
  }

  p.waitStart = simTime;
  // If at end of tile and no new fibers then go idle and posssibly stop calculation
  if (readyFibers.empty() && !aFetchCount && !bFetchCount) {
    p.multiplying = false;
    bool anyMultiplying = false;
    for (int i = 0; i < PEcnt; i++) {
      peIdle[i] += simTime - pes[i].waitStart;
      pes[i].waitStart = simTime;
      anyMultiplying |= pes[i].multiplying;
    }
    if (!anyMultiplying) schedule(finishCalculate);
    return;
  }

  // Try to get new row
  schedule(peTakeRow, pe);
}

void printPEStats(long long totalCycles) {
  long long busy = 0, idle = 0;
  for (int pe = 0; pe < PEcnt; pe++) {
    busy += peBusy[pe];
    idle += peIdle[pe];
  }
  double peTime = (double)PEcnt * totalCycles;
  printf("PE busy = %.1f%%, waiting for A = %.1f%%, waiting for B = %.1f%%, waiting for C = %.1f%%, idle at tile barrier = %.1f%%\n",
         100.0 * busy / peTime, 100.0 * stallA / peTime, 100.0 * stallB / peTime, 100.0 * stallC / peTime,
         100.0 * idle / peTime);
  printf("StallA = %lld, StallB = %lld, StallC = %lld\n", stallA, stallB, stallC);
  printf("Per PE Busy:");
  for(int pe = 0; pe  < PEcnt; pe++) {
    printf(" pe%dBusy: %.1f%% ", pe, 100.0 * peBusy[pe] / totalCycles);
  }
  printf("\n");
}
