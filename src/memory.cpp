#include "memory.h"
#include "config.h"
#include "pe.h"
#include "queue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <unordered_map>
#include <vector>

int dramChannels = 4;
long long dramLatency = 100; // our guess, not from the paper

// one queue per channel
std::vector<std::deque<DRAMRequest>> dramQueue;
std::vector<bool> channelBusy;
std::vector<long long> channelBusyCycles;

// fiber -> requests waiting for it, while it's on its way from DRAM
std::unordered_map<int, std::vector<FiberArgs>> waitingForFiber;

long long hits, misses, merged;

void channelServe(std::any any);
void channelFree(std::any any);
void dramArrive(std::any any);

void initMemory() {
  dramQueue.assign(dramChannels, std::deque<DRAMRequest>());
  channelBusy.assign(dramChannels, false);
  channelBusyCycles.assign(dramChannels, 0);
  waitingForFiber.clear();
  hits = misses = merged = 0;
}

void sendToDRAM(std::any any) {
  DRAMRequest req = std::any_cast<DRAMRequest>(any);
  int ch = req.id % dramChannels;
  dramQueue[ch].push_back(req);
  schedule(channelServe, ch);
}

// the channel takes the next request from its queue, if it's free
void channelServe(std::any any) {
  int ch = std::any_cast<int>(any);
  if (channelBusy[ch] || dramQueue[ch].empty()) {
    return;
  }
  DRAMRequest req = dramQueue[ch].front();
  dramQueue[ch].pop_front();

  // total DRAM bandwidth (words per cycle) is split equally between channels
  double wordsPerCycle = HBMbandwidth / dramChannels;
  long long transfer = std::max(1LL, (long long)std::ceil(req.words / wordsPerCycle));
  channelBusy[ch] = true;
  channelBusyCycles[ch] += transfer;

  // channel is busy for the transfer, the data arrives dramLatency after that
  schedule(channelFree, ch, transfer);
  schedule(dramArrive, req, transfer + dramLatency);
}

void channelFree(std::any any) {
  int ch = std::any_cast<int>(any);
  channelBusy[ch] = false;
  schedule(channelServe, ch);
}

void dramArrive(std::any any) {
  DRAMRequest req = std::any_cast<DRAMRequest>(any);
  if (req.type == A_ROW) {
    schedule(peAReady, req.pe);
  } else if (req.type == B_FIBER) {
    for (FiberArgs waiting : waitingForFiber[req.id]) {
      schedule(peFiberReady, waiting);
    }
    waitingForFiber.erase(req.id);
  }
}

void requestFiber(std::any any) {
  FiberArgs args = std::any_cast<FiberArgs>(any);

  // the cache decides hit/miss now, when the PE asks for it
  FiberAccess access = accessFiberGust(args.fiber, args.row);

  if (waitingForFiber.count(args.fiber)) {
    // already on its way from DRAM, wait for that one
    waitingForFiber[args.fiber].push_back(args);
    merged++;
  } else if (access.dramWords > 0) {
    waitingForFiber[args.fiber] = {args};
    misses++;
    schedule(sendToDRAM, DRAMRequest{B_FIBER, args.fiber, args.pe, access.dramWords});
  } else {
    hits++;
    schedule(peFiberReady, args, access.sramCycles);
  }
}

void printMemoryStats(long long totalCycles) {
  printf("DRAM channel busy:");
  for (int c = 0; c < dramChannels; c++) {
    printf(" ch%d = %.1f%%", c, 100.0 * channelBusyCycles[c] / totalCycles);
  }
  printf("\n");
  printf("B fibers: %lld hits, %lld misses, %lld waited for a miss already on its way\n", hits,
         misses, merged);
}
