#include "pe.h"
#include "headers.h"
#include "memory.h"
#include "queue.h"
#include "statistics.h"

#include <deque>

struct PE {
  int row;
  std::vector<int> fibers; // B rows this row needs
  int fibersLeft;          // not multiplied yet
  std::deque<int> arrived; // here, but not multiplied yet
  bool multiplying;
  long long waitStart;     // when it started waiting, for the stall stats
  double cDramWords;
};
std::vector<PE> pes;

int nextRow; // next row of A in this tile that a free PE will take
int pesDone; // PEs that found no rows left in this tile

std::vector<long long> peBusy, peStallA, peStallB, peIdle, peDoneAt;

void initPEs() {
  pes.assign(PEcnt, PE());
  peBusy.assign(PEcnt, 0);
  peStallA.assign(PEcnt, 0);
  peStallB.assign(PEcnt, 0);
  peIdle.assign(PEcnt, 0);
  peDoneAt.assign(PEcnt, 0);
}

// tile starts: every PE asks for a row at the same cycle
void startTileGust(std::any any) {
  nextRow = 0;
  pesDone = 0;
  for (int pe = 0; pe < PEcnt; pe++) {
    schedule(peTakeRow, pe);
  }
}

void peTakeRow(std::any any) {
  int pe = std::any_cast<int>(any);
  int ii = nextRow;

  // no rows left: wait at the tile barrier. the last PE ends the tile
  if (ii >= iii || TI + ii > I) {
    peDoneAt[pe] = simTime;
    pesDone++;
    if (pesDone == PEcnt) {
      for (int p = 0; p < PEcnt; p++) {
        peIdle[p] += simTime - peDoneAt[p];
      }
      schedule(finishCalculate);
    }
    return;
  }
  nextRow++;

  RowInfo info = startRowGust(TI + ii);
  slidePrefetchWindowGust(ii);

  PE &p = pes[pe];
  p.row = TI + ii;
  p.fibers = info.fibers;
  p.fibersLeft = info.fibers.size();
  p.arrived.clear();
  p.multiplying = false;
  p.cDramWords = info.cDramWords;
  p.waitStart = simTime;

  // read the A row: from DRAM if it's not on chip
  if (info.aDramWords > 0) {
    schedule(sendToDRAM, DRAMRequest{A_ROW, p.row, pe, info.aDramWords});
  } else {
    schedule(peAReady, pe, info.aSramCycles);
  }
}

// A row is here, so now we know which B rows to ask for
void peAReady(std::any any) {
  int pe = std::any_cast<int>(any);
  PE &p = pes[pe];
  peStallA[pe] += simTime - p.waitStart;
  p.waitStart = simTime;

  if (p.fibers.empty()) {
    schedule(peRowDone, pe);
    return;
  }
  for (int fiber : p.fibers) {
    schedule(requestFiber, FiberArgs{pe, p.row, fiber});
  }
}

void peFiberReady(std::any any) {
  FiberArgs args = std::any_cast<FiberArgs>(any);
  pes[args.pe].arrived.push_back(args.fiber);
  schedule(peMultiplyNext, args.pe);
}

// multiply the next fiber that has arrived. if none has, the PE stalls until one does
void peMultiplyNext(std::any any) {
  int pe = std::any_cast<int>(any);
  PE &p = pes[pe];
  if (p.multiplying || p.arrived.empty()) {
    return;
  }
  int fiber = p.arrived.front();
  p.arrived.pop_front();

  peStallB[pe] += simTime - p.waitStart;
  peBusy[pe] += currsizeB[fiber];
  perPEWork[pe] += currsizeB[fiber];
  p.multiplying = true;
  schedule(peMultiplyDone, pe, currsizeB[fiber]); // 1 multiply per cycle
}

void peMultiplyDone(std::any any) {
  int pe = std::any_cast<int>(any);
  PE &p = pes[pe];
  p.multiplying = false;
  p.fibersLeft--;
  p.waitStart = simTime;

  if (p.fibersLeft == 0) {
    schedule(peRowDone, pe);
  } else {
    schedule(peMultiplyNext, pe);
  }
}

// C goes to DRAM, the PE doesn't wait for it and takes the next row
void peRowDone(std::any any) {
  int pe = std::any_cast<int>(any);
  PE &p = pes[pe];
  if (p.cDramWords > 0) {
    schedule(sendToDRAM, DRAMRequest{C_ROW, p.row, -1, p.cDramWords});
  }
  schedule(peTakeRow, pe);
}

void printPEStats(long long totalCycles) {
  long long busy = 0, stallA = 0, stallB = 0, idle = 0;
  for (int pe = 0; pe < PEcnt; pe++) {
    busy += peBusy[pe];
    stallA += peStallA[pe];
    stallB += peStallB[pe];
    idle += peIdle[pe];
  }
  double peTime = (double)PEcnt * totalCycles;
  printf("PE busy = %.1f%%, waiting for A = %.1f%%, waiting for B = %.1f%%, idle at tile barrier = %.1f%%\n",
         100.0 * busy / peTime, 100.0 * stallA / peTime, 100.0 * stallB / peTime,
         100.0 * idle / peTime);
}
