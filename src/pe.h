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
  int pe, row, fiber;
};

void initPEs();

// events
void startTileGust(std::any any);
void peTakeRow(std::any any);
void peAReady(std::any any);
void peFiberReady(std::any any);
void peMultiplyNext(std::any any);
void peMultiplyDone(std::any any);
void peRowDone(std::any any);

void printPEStats(long long totalCycles);

#endif
