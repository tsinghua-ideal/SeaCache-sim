#include "queue.h"

std::priority_queue<QueueEntry, std::vector<QueueEntry>, EventOrder> LoopQueue;

long long simTime = 0;

// counts up for every event added, so same-cycle events keep their order
long long eventOrder = 0;

void schedule(void (*func)(std::any), std::any args, long long delay) {
  LoopQueue.push({func, args, simTime + delay, eventOrder++});
}

std::vector<DRAMQueueEntry> DRAMQueue;
std::vector<PrefetchQueueEntry> PrefetchQueue;
std::vector<CacheMissQueueEntry> CacheMissQueue;
std::vector<OpsWaitingOnMemoryQueueEntry> OpsWaitingOnMemoryQueue;
std::vector<OpsWaitingOnPEsQueueEntry> OpsWaitingOnPEsQueue;
std::vector<WritebackQueueEntry> WritebackQueue;
