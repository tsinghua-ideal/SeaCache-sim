#ifndef PE_H
#define PE_H

#include <any>
#include <vector>

// what one row of A needs (in simulator.cpp)
struct RowInfo {
  std::vector<int> fibers; // B rows it needs
  double aDramWords, cDramWords;
  long long aSramCycles;
};
RowInfo startRowGust(int row);
void slidePrefetchWindowGust(int ii);

// one B fiber a PE asks for
struct FiberArgs {
  int row, fiber; double cDramWords; long long requestInitTime;
};
struct PEArgs {
  FiberArgs fiberArgs; int pe;
};

void initPEs();
bool peReady(int pe);

extern long long stallA, stallB, stallC;

// events
void peTakeRow(std::any any);
void peFiberReady(std::any any);
void peRowDone(std::any any);

void printPEStats(long long totalCycles);

#endif
