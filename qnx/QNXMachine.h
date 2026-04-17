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

typedef struct CPUData_ {
   double userPercent;
   double systemPercent;
   double totalPercent;   /* combined for display */
   double frequency;      /* MHz, or NAN if unknown */
} CPUData;

typedef struct QNXMachine_ {
   Machine super;

   memory_t usedMem;      /* used physical memory in kB */
   memory_t cachedMem;    /* (unused on QNX, kept for API compat) */

   /* Per-CPU accounting */
   CPUData* cpus;         /* [0] = aggregate, [1..N] = per-cpu */

   /* Per-CPU idle-thread accumulated CPU time from last scan (ns).
    * Obtained via ClockId(1, cpu+1) / ClockTime() against procnto's
    * idle threads, mirroring what QNX top does. */
   uint64_t prevIdleNs[QNX_MAX_CPUS];

   /* Load averages: 1, 5, and 15 minute exponential moving averages of
    * the runnable thread count, computed on each scan. */
   double loadAvg[3];
   uint64_t prevLoadAvgMs; /* monotonicMs at last load-average update */

   long pageSize;         /* system page size in bytes */
} QNXMachine;

void QNXMachine_updateLoadAvg(QNXMachine* this, unsigned int runnable);

#endif /* HEADER_QNXMachine */
