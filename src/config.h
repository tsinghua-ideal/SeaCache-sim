/*
Keep all hardware configurations.
*/

#ifndef CONFIG_H
#define CONFIG_H
#include "headers.h"

// global bandwdith & SRAM configuration
extern f64 HBMbandwidth;
extern int PEcnt, mergecnt;
extern f64 HBMbandwidthperPE;
extern int sramBank;

// DRAM ↔ SRAM bandwidth calculations
__attribute__((always_inline)) static
f64 memoryBandwidthWhole(u64 ss){return div_rup(ss, HBMbandwidth);}

// bandwidth per PE
__attribute__((always_inline)) static
f64 memoryBandwidthPE(u64 ss)   {return ss / HBMbandwidthperPE; }

// SRAM cycle
__attribute__((always_inline)) static
u64 sramReadBandwidth(u64 ss)   {return ss / 2; }

__attribute__((always_inline)) static
u64 sramWriteBandwidth(u64 ss)  {return ss; }

extern bool ISCACHE;

#endif