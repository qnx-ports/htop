/*
htop - qnx/QNXMachine.c
(C) 2024 htop dev team
(C) 2026 QNXe team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "qnx/QNXMachine.h"

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

uint32_t __curr_num_fds = 0;

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

   this->cpus = xCalloc(numCPUs + 1, sizeof(CPUData));
   for (unsigned int i = 0; i < numCPUs + 1; i++) {
      this->cpus[i].frequency = NAN;
      this->cpus[i].prevIdleNs = 0;
   }

   /* Total RAM (static) */
   super->totalMem = QNXMachine_totalRamKB();

   /* No swap support in base QNX */
   super->totalSwap  = 0;
   super->usedSwap   = 0;
   super->cachedSwap = 0;

   this->prevScanTime = 0;

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

void Machine_scan(Machine* super) {
   QNXMachine* this = (QNXMachine*) super;

   /* Memory */
   memory_t freeKB = QNXMachine_freeRamKB();
   this->usedMem = 0;
   if (super->totalMem > freeKB) {
      this->usedMem = super->totalMem - freeKB;
   }

   struct timeval currentTime;
   gettimeofday(&currentTime, NULL);
   const uint64_t timeNowNs = (currentTime.tv_sec * 1e6 + currentTime.tv_usec) * 1e3;
   uint64_t elapsedNs;
   if (this->prevScanTime == 0) {
      elapsedNs = Platform_getUptime() * 1e9;
   }else {
      elapsedNs = timeNowNs - this->prevScanTime;
   }
   this->prevScanTime = timeNowNs;

   struct timespec coreKernelTime;
   double totalPercent = 0.0;
   int cpusCounted = 0;

   // cpus is 1 indexed as 0 is used as the average
   for (unsigned int i = 1; i <= super->existingCPUs; i++) {
     // Tracking kernel busy time is close enough to system idle time since that
     // time is reaped by the scheduler
     clockid_t cid = ClockId(KPROCID, i);
     if (cid == -1)
       continue;
     clock_gettime(cid, &coreKernelTime);
     uint64_t idleNs = coreKernelTime.tv_sec * 1e9 + coreKernelTime.tv_nsec;

     double perCoreTime = 0.0;
     uint64_t idleDelta = idleNs - this->cpus[i].prevIdleNs;
     double busyFrac = 1.0 - (double)idleDelta / (double)elapsedNs;
     perCoreTime = CLAMP(busyFrac * 100.0, 0.0, 100.0);

     this->cpus[i].prevIdleNs = idleNs;

     this->cpus[i].userPercent = perCoreTime;
     this->cpus[i].systemPercent = 0.0;
     totalPercent += perCoreTime;
     cpusCounted += 1;
   }

   this->cpus[0].userPercent = totalPercent / cpusCounted;
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
