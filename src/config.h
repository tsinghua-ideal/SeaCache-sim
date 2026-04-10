/*
Keep all hardware configurations.
*/

#ifndef CONFIG_H
#define CONFIG_H

// NOTE(ejs): I only enum-ified the cachescheme codes used/described in main.cpp.
// some undecipherable magic slop remains in simulator.cpp. CACHE_SCHEME_BASE is
// set to a high number so these enums (hopefully) do not conflict with the magic slop.
enum cache_scheme {
    CACHE_SCHEME_BASE=1000000,  // formerly magic 0
    CACHE_SCHEME_MAPPING,       // formerly magic 1
    CACHE_SCHEME_FLFU,          // formerly magic 88
    CACHE_SCHEME_INNER_SP,      // formerly magic 11100
    CACHE_SCHEME_SPARCH         // formerly magic 11101
};

// global bandwdith & SRAM configuration
extern double HBMbandwidth;
extern int PEcnt, mergecnt;
extern double HBMbandwidthperPE;
extern int sramBank, sramReadPort, sramWritePort;

// DRAM ↔ SRAM bandwidth calculations
double memoryBandwidthWhole(long long ss);
double memoryBandwidthPE(long long ss);

// SRAM cycle
long long sramReadBandwidth(long long ss);
long long sramWriteBandwidth(long long ss);

extern bool ISCACHE;

#endif