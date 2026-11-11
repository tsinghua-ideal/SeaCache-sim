#ifndef PREFETCH_H
#define PREFETCH_H

#include <any>
#include <queue>
#include "pe.h"

extern int currentII;
extern int cFiberCount;
extern long long aFetchCount, bFetchCount;
extern std::queue<FiberArgs> readyFibers;

void initPrefetch();

// events
void prefetchA(std::any any);
void fiberAReady(std::any any);
void fiberBReady(std::any any);
void fiberCDone(std::any any);

#endif//PREFETCH_H
