#include "queue.h"

std::queue<QueueEntry> LoopQueue;

std::vector<DRAMQueueEntry> DRAMQueue;
std::vector<PrefetchQueueEntry> PrefetchQueue;
std::vector<CacheMissQueueEntry> CacheMissQueue;
std::vector<OpsWaitingOnMemoryQueueEntry> OpsWaitingOnMemoryQueue;
std::vector<OpsWaitingOnPEsQueueEntry> OpsWaitingOnPEsQueue;
std::vector<WritebackQueueEntry> WritebackQueue;

