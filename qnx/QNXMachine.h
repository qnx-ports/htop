#ifndef HEADER_QNXMachine
#define HEADER_QNXMachine
/*
htop - qnx/QNXMachine.h
(C) 2024 htop dev team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include <stdbool.h>
#include <stdint.h>

#include "Machine.h"
#include "UsersTable.h"

#define QNX_MAX_CPUS 64

#define KPROCID 1

// Scuffed counter for the number of system fds, since we have to enumerate every proc, we'll just accumulate them here
// This is to be read by Process_getFileDescriptors()
extern uint32_t __curr_num_fds;

typedef struct CPUData_ {
   double userPercent;
   double systemPercent;
   double totalPercent;
   double frequency;
   uint64_t prevIdleNs;
} CPUData;

typedef struct QNXMachine_ {
   Machine super;

   memory_t usedMem;
   memory_t cachedMem;

   CPUData* cpus;

   double loadAvg[3];
   uint64_t prevLoadAvgMs;

   long pageSize;

   uint64_t prevScanTime;
} QNXMachine;

#endif
