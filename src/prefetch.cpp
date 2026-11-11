#include "prefetch.h"
#include "headers.h"
#include "memory.h"
#include "queue.h"
#include <stdexcept>

std::queue<FiberArgs> readyFibers;
int currentII = 0;
int cFiberCount = 0;
long long aFetchCount = 0;
long long bFetchCount = 0;

// reset prefetch values at start of new tile
void initPrefetch() {
    currentII = 0;
    cFiberCount = 0;
    schedule(prefetchA);
}

void prefetchA(std::any any) {
    int initialII = currentII;

    // need to prefetch the next ii+prefetchsize+1
    if (prefetchNow >= prefetchSize) return;

    int prefetchCount = 0;
    // schedule prefetch for up to dramChannels
    for (; prefetchNow < prefetchSize && currentII < iii && currentII < (initialII + dramChannels); currentII++) {
        if (TI + currentII > I)
        break;

        prefetchRowNow = TI + currentII;
        // return 0 if can't prefetch that row now
        if (!prefetchrow(prefetchRowNow)) {
        break;
        }
        prefetchCount++; aFetchCount++;
        schedule(sendToDRAM, DRAMRequest{A_ROW, prefetchRowNow, currsizeA[prefetchRowNow] * 3.0, false, simTime});
    }
    
    // Confirm there was space before prefetching again
    if (prefetchCount) schedule(prefetchA, NULL, prefetchCount); // prefetchCount cycles to prefetch prefetchCount As
}

// dram returned A fiber so now bet B fibers for A
void fiberAReady(std::any any) {
    FiberArgs args = std::any_cast<FiberArgs>(any);
    RowInfo info = startRowGust(args.row);
    stallA += simTime - args.requestInitTime;
    for (int fiber : info.fibers) {
        bFetchCount++;
        schedule(requestFiber, FiberArgs{args.row, fiber, info.cDramWords, simTime});
    }
    aFetchCount--;
    // Update prefetch window size now that A is being used
    prefetchNow -= currsizeA[args.row] * 3;
    // Prefetch more As now that prefetch buffer is less full
    schedule(prefetchA);
}

// dram returned B fiber so try to multiply
void fiberBReady(std::any any) {
    FiberArgs args = std::any_cast<FiberArgs>(any);
    stallB += simTime - args.requestInitTime;
    bool found = false;
    for (int pe = 0; pe < PEcnt; pe++) {
        // Find PE ready to execute multiply
        if (peReady(pe)) {
            peFiberReady(PEArgs{args, pe});
            found = true;
            break;
        }
    }
    bFetchCount--;
    // If no pe found put in queue of waiting pes
    if (!found) readyFibers.push(args);
}

void fiberCDone(std::any any) {
    DRAMRequest req = std::any_cast<DRAMRequest>(any);
    stallC += simTime - req.requestInitTime;
    cFiberCount--;
    // if last C fiber at end of tile then call finish calculate
    // if (req.endOfTile && !cFiberCount) schedule(finishCalculate);
}
