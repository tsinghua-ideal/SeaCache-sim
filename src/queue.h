#ifndef QUEUE_H
#define QUEUE_H

#include <any>
#include <queue>
#include <vector>

struct QueueEntry {
    void (*queueFunc)(std::any);
    std::any args;
    long long time;   // cycle when this event happens
    long long order;  // events at the same cycle run in the order they were added
};

// earliest event first. if two events are at the same cycle, the one added first goes first
struct EventOrder {
    bool operator()(const QueueEntry &a, const QueueEntry &b) const {
        if (a.time != b.time) return a.time > b.time;
        return a.order > b.order;
    }
};

// Basic queue used for global execution, always gives back the earliest event
// TODO: Split into several queues representing the different elements
extern std::priority_queue<QueueEntry, std::vector<QueueEntry>, EventOrder> LoopQueue;

// current simulation time in cycles. it jumps to the time of each event as it runs
extern long long simTime;

// add an event that happens 'delay' cycles from now (0 = at the current cycle)
void schedule(void (*func)(std::any), std::any args = {}, long long delay = 0);

/*

The LoopQueue and QueueEntry is how queues will be defined. Each entry will have the necessary information to be executed
as well as update the simulation clock. Each queue (dram queue, PE queue, writeback queue,...) will also have additional
metadata which the queueing policy will use to determine which item to next execute.

The valid items for each queue are defined below.

*/

// Loop Generic Functions
void initialize(std::any any);

void inner_loop_before_calculate(std::any any);
void inner_loop_after_calculate(std::any any);

void iterate_inner_loop(std::any any);
void iterate_mid_loop(std::any any);

void reinitialize_inner(std::any any);
void iterate_outer_loop(std::any any);
void reinitialize_mid(std::any any);

void finishRun(std::any any);

// Calculate Generic Functions
void calculate(std::any any);
void finishCalculate(std::any any);

// Gust Dataflow Functions
void calculateGust(std::any any);
void prefetchPrefillWindowGust(std::any any);

// Inner Dataflow Functions - TODO: make more granular
void calculateInner(std::any any);

// Outer Dataflow Functions - TODO: make more granular
void calculateOuter(std::any any);

enum QueueEntryStatus {
    WAITING,
    RUNNING,
    COMPLETED,
    EMPTY
};

struct BaseQueueEntry {
    int id;
    QueueEntryStatus status;
    int requestID;
};

enum MemReadType {
    A_PREFETCH,
    B_GET,
    C_GET,
    WRITEBACK
};

struct DRAMQueueEntry : BaseQueueEntry { 
    long long address;
    MemReadType type;
};

// Prefill with all of A at start of program
struct PrefetchQueueEntry : BaseQueueEntry {
    long long aIndex;
    long long fiberID;
    long long address; 
};

struct CacheMissQueueEntry : BaseQueueEntry {
    long long address;
    long long fiberID;
};

struct OpsWaitingOnMemoryQueueEntry : BaseQueueEntry { 
    long long bAddress;
    long long bFiberID;
    long long cAddress;
    long long cFiberID;
};

struct OpsWaitingOnPEsQueueEntry : BaseQueueEntry {
};

struct WritebackQueueEntry : BaseQueueEntry { 
};

#endif // QUEUE_H
