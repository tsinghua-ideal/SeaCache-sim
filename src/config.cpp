#include "config.h"

f64 HBMbandwidth;
int PEcnt;
int mergecnt;
f64 HBMbandwidthperPE;
int sramBank;

// // DRAM bandwidth
// __attribute__((always_inline))
// f64 memoryBandwidthWhole(long long ss) { return (ss + HBMbandwidth - 1) / HBMbandwidth;}

// // bandwidth per PE
// __attribute__((always_inline))
// f64 memoryBandwidthPE(long long ss) { return ss / HBMbandwidthperPE; }

// /*
// Return the single port cycle. (Otherwise will lead to fractional)
// Just / banknumber at last

// 2 read port and 1 write port
// */
// // SRAM read bandwidth
// __attribute__((always_inline))
// u64 sramReadBandwidth(long long ss) { return ss / 2; }

// __attribute__((always_inline))
// u64 sramWriteBandwidth(long long ss) { return ss; }

bool ISCACHE;