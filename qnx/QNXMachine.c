/*
htop - qnx/QNXMachine.c
(C) 2024 htop dev team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "qnx/QNXMachine.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <unistd.h>

#include "Machine.h"
#include "XUtils.h"
#include "qnx/Platform.h"


/*
 * Compute total physical RAM by summing "ram" entries in syspage asinfo.
 * Returns value in kB.
 */
static memory_t QNXMachine_totalRamKB(void) {
   struct asinfo_entry* as = SYSPAGE_ENTRY(asinfo);
   int n = (int)(SYSPAGE_ENTRY_SIZE(asinfo) / sizeof(*as));
   const char* strings = SYSPAGE_ENTRY(strings)->data;

   unsigned long long total = 0;
   for (int i = 0; i < n; i++) {
      if (strcmp(strings + as[i].name, "ram") == 0)
         total += as[i].end - as[i].start + 1;
   }
   return (memory_t)(total / 1024);
}

/*
 * Compute free physical RAM using the POSIX typed-memory interface.
 * POSIX_TYPED_MEM_ALLOCATE reports allocatable (free) bytes.
 * Returns value in kB; returns 0 on error.
 */
static memory_t QNXMachine_freeRamKB(void) {
   int fd = posix_typed_mem_open("ram", O_RDONLY, POSIX_TYPED_MEM_ALLOCATE);
   if (fd < 0)
      return 0;

   struct posix_typed_mem_info info;
   memory_t free_kb = 0;
   if (posix_typed_mem_get_info(fd, &info) == 0)
      free_kb = (memory_t)(info.posix_tmi_length / 1024);

   close(fd);
   return free_kb;
}

Machine* Machine_new(UsersTable* usersTable, uid_t userId) {
   QNXMachine* this = xCalloc(1, sizeof(QNXMachine));
   Machine* super = &this->super;

   Machine_init(super, usersTable, userId);

   this->pageSize = sysconf(_SC_PAGESIZE);
   if (this->pageSize <= 0)
      this->pageSize = 4096;

   /* Determine CPU count */
   unsigned int numCPUs = (unsigned int)_syspage_ptr->num_cpu;
   if (numCPUs < 1)
      numCPUs = 1;

   super->existingCPUs = numCPUs;
   super->activeCPUs   = numCPUs;

   /* Allocate CPUData: [0] = aggregate, [1..N] = per-cpu */
   this->cpus = xCalloc(numCPUs + 1, sizeof(CPUData));
   for (unsigned int i = 0; i <= numCPUs; i++)
      this->cpus[i].frequency = NAN;

   /* Total RAM (static) */
   super->totalMem = QNXMachine_totalRamKB();

   /* No swap support in base QNX */
   super->totalSwap  = 0;
   super->usedSwap   = 0;
   super->cachedSwap = 0;

   return super;
}

void Machine_delete(Machine* super) {
   QNXMachine* this = (QNXMachine*) super;
   Machine_done(super);
   free(this->cpus);
   free(this);
}

bool Machine_isCPUonline(const Machine* host, unsigned int id) {
   assert(id < host->existingCPUs);
   (void) host; (void) id;
   return true;
}

/*
 * Machine_scan is called each htop refresh cycle.
 * Updates memory stats and per-CPU usage via procnto idle-thread clocks.
 *
 * QNX's idle threads live in PID 1 (procnto) with TID = cpu + 1.
 * ClockId(1, tid) returns the POSIX clockid for that thread's CPU time.
 * ClockTime() reads accumulated nanoseconds.  The delta between scans
 * is the idle time; subtracting from elapsed wall time gives busy time.
 * This is the same method used by the QNX top utility.
 */
void Machine_scan(Machine* super) {
   QNXMachine* this = (QNXMachine*) super;

   /* Memory */
   memory_t freeKB = QNXMachine_freeRamKB();
   this->usedMem = (super->totalMem > freeKB) ? super->totalMem - freeKB : 0;

   /* Per-CPU usage via idle-thread clock deltas */
   uint64_t elapsedNs = (super->monotonicMs > super->prevMonotonicMs)
      ? (super->monotonicMs - super->prevMonotonicMs) * 1000000ULL
      : 0;

   double totalBusy = 0.0;

   for (unsigned int i = 0; i < super->existingCPUs; i++) {
      /* tid = i + 1 for 0-indexed CPU i */
      clockid_t cid = ClockId(1, (int)(i + 1));
      uint64_t idleNs = 0;
      if (cid != -1)
         ClockTime(cid, NULL, &idleNs);

      double pct = 0.0;
      if (elapsedNs > 0 && this->prevIdleNs[i] > 0) {
         uint64_t idleDelta = (idleNs >= this->prevIdleNs[i])
            ? idleNs - this->prevIdleNs[i] : 0;
         double busyFrac = 1.0 - (double)idleDelta / (double)elapsedNs;
         pct = CLAMP(busyFrac * 100.0, 0.0, 100.0);
      }

      this->prevIdleNs[i] = idleNs;

      /* cpus[0] = aggregate (filled after loop), cpus[i+1] = per-cpu */
      this->cpus[i + 1].userPercent   = pct;
      this->cpus[i + 1].systemPercent = 0.0;
      totalBusy += pct;
   }

   /* Aggregate = average across all CPUs */
   this->cpus[0].userPercent   = totalBusy / (double)super->existingCPUs;
   this->cpus[0].systemPercent = 0.0;
}

/*
 * Update the 1/5/15-minute exponentially-weighted moving averages of
 * runnable thread count.  Called from QNXProcessTable after each scan.
 *
 * The standard decay constants are:
 *   1-min:  T = 60 s
 *   5-min:  T = 300 s
 *   15-min: T = 900 s
 *
 * Formula: load = load * e^(-dt/T) + n * (1 - e^(-dt/T))
 */
void QNXMachine_updateLoadAvg(QNXMachine* this, unsigned int runnable) {
   Machine* super = &this->super;

   if (this->prevLoadAvgMs == 0) {
      /* First sample: seed all three averages with the current runnable count */
      this->loadAvg[0] = this->loadAvg[1] = this->loadAvg[2] = (double)runnable;
      this->prevLoadAvgMs = super->monotonicMs;
      Platform_setLoadAvg(this->loadAvg[0], this->loadAvg[1], this->loadAvg[2]);
      return;
   }

   double dt = (double)(super->monotonicMs - this->prevLoadAvgMs) / 1000.0;
   if (dt <= 0.0)
      return;

   static const double T[3] = { 60.0, 300.0, 900.0 };
   for (int i = 0; i < 3; i++) {
      double decay = exp(-dt / T[i]);
      this->loadAvg[i] = this->loadAvg[i] * decay + (double)runnable * (1.0 - decay);
   }

   this->prevLoadAvgMs = super->monotonicMs;
   Platform_setLoadAvg(this->loadAvg[0], this->loadAvg[1], this->loadAvg[2]);
}

int Machine_getCPUPhysicalCoreID(const Machine* host, unsigned int id) {
   assert(id < host->existingCPUs);
   (void) host;
   return (int)id;
}

int Machine_getCPUThreadIndex(const Machine* host, unsigned int id) {
   assert(id < host->existingCPUs);
   (void) host; (void) id;
   return 0;
}
