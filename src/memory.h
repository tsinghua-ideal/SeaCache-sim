#ifndef MEMORY_H
#define MEMORY_H

#include <any>

extern int dramChannels;
extern long long dramLatency;

enum RequestType { A_ROW, B_FIBER, C_ROW };

struct DRAMRequest {
  RequestType type;
  int id; // row of A / C, or which B fiber
  double words;
  bool endOfTile;
  long long requestInitTime;
};

void initMemory();

// events the PEs schedule
void sendToDRAM(std::any any);   // a DRAMRequest
void requestFiber(std::any any); // a FiberArgs (pe.h)

// what the cache says about one fiber (in simulator.cpp)
struct FiberAccess {
  double dramWords; // 0 = hit
  long long sramCycles;
};
FiberAccess accessFiberGust(int fiber, int row);

void printMemoryStats(long long totalCycles);

#endif
