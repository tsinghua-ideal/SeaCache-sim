#ifndef SIMULATOR_H
#define SIMULATOR_H

extern int cachesize;
extern int prefetchSize;
extern int prefetchNow, prefetchRowNow;
bool prefetchrow(int ii);
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

extern int currsizeB[MAXN];
extern int currsizeA[MAXN];
extern int currsizeBc[MAXN];
extern int beginB[MAXN];

extern int TI, TJ, TK;

#endif
