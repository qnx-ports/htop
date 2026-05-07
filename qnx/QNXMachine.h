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

typedef struct CPUData_ {
   double userPercent;
   double systemPercent;
   double totalPercent;   /* combined for display */
   double frequency;      /* MHz, or NAN if unknown */
   uint64_t prevIdleNs;
} CPUData;

typedef struct QNXMachine_ {
   Machine super;

   memory_t usedMem;      /* used physical memory in kB */
   memory_t cachedMem;    /* (unused on QNX, kept for API compat) */

   /* Per-CPU accounting */
   CPUData* cpus;         /* [0] = aggregate, [1..N] = per-cpu */

   /* Load averages: 1, 5, and 15 minute exponential moving averages of
    * the runnable thread count, computed on each scan. */
   double loadAvg[3];
   uint64_t prevLoadAvgMs; /* monotonicMs at last load-average update */

   long pageSize;         /* system page size in bytes */

   uint64_t prevScanTime;
} QNXMachine;

#endif /* HEADER_QNXMachine */
