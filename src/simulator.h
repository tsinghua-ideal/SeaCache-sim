#ifndef SIMULATOR_H
#define SIMULATOR_H

extern int cachesize;
extern int prefetchSize;
extern bool adaptive_prefetch;

extern int minBlock;

extern bool ISDYNAMICJ, ISDYNAMICK, ISDYNAMICI;
extern int PartialConfig;

extern int cacheScheme;

void configPartial(float partialA, float partialB, float partialC);
void reinitialize();
void runTile(bool isest, int iii, int jjj, int kkk, long long tti,
             long long ttj, long long ttk, long long SmallestTile);
void run();

int getkbound();
int getjbound();
int getibound();

extern int *currsizeB;
extern int *currsizeBc;
extern int *beginB;

extern int TI, TJ, TK;

void initialize_simulator();
void deinitialize_simulator();

#endif
