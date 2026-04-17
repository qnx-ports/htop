/*
htop - qnx/QNXProcessTable.c
(C) 2024 htop dev team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "qnx/QNXProcessTable.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <devctl.h>
#include <sys/procfs.h>
#include <sys/neutrino.h>
#include <sys/states.h>

#include "Machine.h"
#include "Macros.h"
#include "Object.h"
#include "Process.h"
#include "ProcessTable.h"
#include "Row.h"
#include "Settings.h"
#include "XUtils.h"
#include "qnx/QNXMachine.h"
#include "qnx/QNXProcess.h"


ProcessTable* ProcessTable_new(Machine* host, Hashtable* pidMatchList) {
   QNXProcessTable* this = xCalloc(1, sizeof(QNXProcessTable));
   Object_setClass(this, Class(ProcessTable));

   ProcessTable* super = &this->super;
   ProcessTable_init(super, Class(QNXProcess), host, pidMatchList);

   return super;
}

void ProcessTable_delete(Object* cast) {
   QNXProcessTable* this = (QNXProcessTable*) cast;
   ProcessTable_done(&this->super);
   free(this);
}

/* Map a QNX thread state to an htop ProcessState */
static ProcessState QNXProcessTable_threadStateToProcessState(unsigned int state) {
   switch (state) {
   case STATE_RUNNING:
      return RUNNING;
   case STATE_READY:
      return RUNNABLE;
   case STATE_STOPPED:
      return STOPPED;
   case STATE_DEAD:
      return ZOMBIE;
   case STATE_SEND:
   case STATE_RECEIVE:
   case STATE_REPLY:
   case STATE_MQ_SEND:
   case STATE_MQ_RECEIVE:
   case STATE_SIGSUSPEND:
   case STATE_SIGWAITINFO:
   case STATE_WAITCTX:
   case STATE_SEM:
   case STATE_JOIN:
   case STATE_INTR:
   case STATE_MUTEX:
   case STATE_CONDVAR:
   case STATE_BARRIER:
   case STATE_PIPE:
   case STATE_RWLOCK_READ:
   case STATE_RWLOCK_WRITE:
      return SLEEPING;
   case STATE_NANOSLEEP:
      return SLEEPING;
   case STATE_WAITPAGE:
      return PAGING;
   default:
      return UNKNOWN;
   }
}

/*
 * Parse /proc/<pid>/vmstat to get m_virt (map_size) and m_resident (rss).
 * Values in the file are in units of system pages; we convert to kB.
 */
static void QNXProcessTable_readVmstat(pid_t pid, long pageSize, memory_t* virt_kb, memory_t* rss_kb) {
   char path[64];
   snprintf(path, sizeof(path), "/proc/%d/vmstat", (int)pid);

   FILE* f = fopen(path, "r");
   if (!f)
      return;

   /* Default: unknown */
   *virt_kb = 0;
   *rss_kb  = 0;

   char line[128];
   while (fgets(line, sizeof(line), f)) {
      unsigned long long val_hex = 0;
      char key[64];
      /* Lines look like: as_stats.map_size=0x963 (9.386MB) */
      if (sscanf(line, "%63[^=]=%lli", key, (long long*)&val_hex) == 2 ||
          sscanf(line, "%63[^=]=0x%llx", key, &val_hex) == 2) {
         /* re-parse correctly */
         if (sscanf(line, "%63[^=]=0x%llx", key, &val_hex) == 2 ||
             sscanf(line, "%63[^=]=%lli", key, (long long*)&val_hex) == 2) {
            if (strcmp(key, "as_stats.map_size") == 0) {
               /* value is in pages */
               *virt_kb = (memory_t)(val_hex * (unsigned long long)pageSize / 1024);
            } else if (strcmp(key, "as_stats.rss") == 0) {
               *rss_kb = (memory_t)(val_hex * (unsigned long long)pageSize / 1024);
            }
         }
      }
   }

   fclose(f);
}

/*
 * Read /proc/<pid>/cmdline (null-terminated string on QNX).
 */
static void QNXProcessTable_readCmdline(pid_t pid, char* buf, size_t buflen) {
   char path[64];
   snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);

   FILE* f = fopen(path, "r");
   if (!f) {
      buf[0] = '\0';
      return;
   }

   size_t n = fread(buf, 1, buflen - 1, f);
   buf[n] = '\0';

   fclose(f);
}

/*
 * Read /proc/<pid>/exefile (the path to the executable).
 */
static bool QNXProcessTable_readExefile(pid_t pid, char* buf, size_t buflen) {
   char path[64];
   snprintf(path, sizeof(path), "/proc/%d/exefile", (int)pid);

   FILE* f = fopen(path, "r");
   if (!f)
      return false;

   size_t n = fread(buf, 1, buflen - 1, f);
   buf[n] = '\0';
   /* strip trailing whitespace / newline */
   while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == ' ' || buf[n-1] == '\0'))
      buf[--n] = '\0';

   fclose(f);
   return n > 0;
}

/*
 * Scan a single process identified by its numeric PID directory entry.
 * Returns false if the process could not be read (e.g. permission denied).
 */
static bool QNXProcessTable_scanProcess(ProcessTable* super, pid_t pid,
                                          unsigned int* runnableThreads) {
   QNXMachine* qhost = (QNXMachine*) super->super.host;
   Machine*     host  = super->super.host;

   /* Open /proc/<pid>/as for devctl queries */
   char aspath[64];
   snprintf(aspath, sizeof(aspath), "/proc/%d/as", (int)pid);

   int fd = open(aspath, O_RDONLY);
   if (fd < 0)
      return false;

   /* Get process info */
   procfs_info info;
   memset(&info, 0, sizeof(info));
   if (devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), NULL) != EOK) {
      close(fd);
      return false;
   }

   /* Get first-thread status for state, priority and last_cpu */
   procfs_status status;
   memset(&status, 0, sizeof(status));
   status.tid = 1;
   devctl(fd, DCMD_PROC_TIDSTATUS, &status, sizeof(status), NULL);

   /* Count runnable threads (RUNNING or READY) across all threads.
    * TIDs start at 1 and may not be contiguous; iterate with a generous
    * upper bound and skip over any gaps that return an error. */
   unsigned int found = 0;
   unsigned int runnableInProcess = 0;
   for (unsigned int tid = 1; tid <= info.num_threads * 4 + 8 && found < info.num_threads; tid++) {
      procfs_status ts;
      ts.tid = (pthread_t)tid;
      if (devctl(fd, DCMD_PROC_TIDSTATUS, &ts, sizeof(ts), NULL) != EOK)
         continue;
      found++;
      if (ts.state == STATE_RUNNING || ts.state == STATE_READY)
         runnableInProcess++;
   }
   *runnableThreads += runnableInProcess;

   close(fd);

   /* Look up or create the process in our table */
   bool preExisting;
   Process* proc = ProcessTable_getProcess(super, pid, &preExisting, QNXProcess_new);
   QNXProcess* qp = (QNXProcess*) proc;

   if (!preExisting) {
      /* Initialise fields that don't change */
      Process_setPid(proc, pid);
      Process_setParent(proc, info.parent);
      Process_setThreadGroup(proc, pid);

      proc->starttime_ctime = (time_t)(info.start_time / 1000000000ULL);
      Process_fillStarttimeBuffer(proc);
   }

   /* Fields updated each scan */
   proc->super.updated = true;
   proc->isKernelThread  = false;
   proc->isUserlandThread = false;
   proc->pgrp    = info.pgrp;
   proc->session = info.sid;
   proc->tpgid   = 0;
   proc->tty_nr  = 0;
   proc->tty_name = NULL;
   proc->nlwp    = (int) info.num_threads;
   proc->st_uid  = info.uid;
   proc->priority = (int) status.priority;
   proc->nice    = PROCESS_NICE_UNKNOWN;
   proc->processor = (int) status.last_cpu;
   proc->minflt  = 0;
   proc->majflt  = 0;

   /* Map QNX scheduling policy to the values htop's Scheduling.c expects.
    * QNX: SCHED_NOCHANGE=0 SCHED_FIFO=1 SCHED_RR=2 SCHED_OTHER=3
    * htop/Linux: SCHED_OTHER=0 SCHED_FIFO=1 SCHED_RR=2 */
   switch (status.policy) {
   case 1:  proc->scheduling_policy = SCHED_FIFO;  break;
   case 2:  proc->scheduling_policy = SCHED_RR;    break;
   default: proc->scheduling_policy = SCHED_OTHER; break;
   }

   /* Process state - use the first thread's state */
   proc->state = QNXProcessTable_threadStateToProcessState(status.state);

   /* CPU time accounting */
   uint64_t cpuTimeNs   = info.utime + info.stime;
   uint64_t prevCpuTime = qp->utime + qp->stime;
   qp->utime = info.utime;
   qp->stime = info.stime;

   /* Compute per-process CPU% */
   uint64_t elapsedNs = host->monotonicMs > host->prevMonotonicMs
      ? (host->monotonicMs - host->prevMonotonicMs) * 1000000ULL
      : 0;

   if (preExisting && elapsedNs > 0 && cpuTimeNs >= prevCpuTime) {
      uint64_t deltaCpu = cpuTimeNs - prevCpuTime;
      float pct = (float)deltaCpu / (float)elapsedNs * 100.0f;
      proc->percent_cpu = MINIMUM(pct, (float)host->activeCPUs * 100.0f);
   } else {
      proc->percent_cpu = preExisting ? proc->percent_cpu : 0.0f;
   }
   Process_updateCPUFieldWidths(proc->percent_cpu);

   /* Total accumulated time in centiseconds (for TIME column) */
   proc->time = (unsigned long long)(cpuTimeNs / 10000000ULL); /* ns -> 1/100 s */

   /* Memory */
   memory_t virt_kb = 0, rss_kb = 0;
   QNXProcessTable_readVmstat(pid, qhost->pageSize, &virt_kb, &rss_kb);
   proc->m_virt     = virt_kb;
   proc->m_resident = rss_kb;

   if (host->totalMem > 0)
      proc->percent_mem = (double)rss_kb / (double)host->totalMem * 100.0;
   else
      proc->percent_mem = 0.0;

   /* User name */
   proc->user = UsersTable_getRef(host->usersTable, proc->st_uid);

   /* Command line */
   char cmdline[4096] = {'\0'};
   QNXProcessTable_readCmdline(pid, cmdline, sizeof(cmdline));

   char exefile[PATH_MAX] = {'\0'};
   bool hasExe = QNXProcessTable_readExefile(pid, exefile, sizeof(exefile));

   if (cmdline[0]) {
      /* Find where argv[0] ends (first '\0' or space in original) */
      int exe_end = (int)strlen(cmdline);
      Process_updateCmdline(proc, cmdline, 0, exe_end);
   } else if (hasExe) {
      Process_updateCmdline(proc, exefile, 0, (int)strlen(exefile));
   } else {
      char comm[32];
      snprintf(comm, sizeof(comm), "<%d>", (int)pid);
      Process_updateCmdline(proc, comm, 0, (int)strlen(comm));
   }

   if (hasExe)
      Process_updateExe(proc, exefile);

   /* comm = basename of executable or first token of cmdline */
   if (exefile[0]) {
      const char* base = strrchr(exefile, '/');
      Process_updateComm(proc, base ? base + 1 : exefile);
   } else if (cmdline[0]) {
      /* Use up to first space */
      char comm2[64];
      snprintf(comm2, sizeof(comm2), "%s", cmdline);
      char* sp = strchr(comm2, ' ');
      if (sp) *sp = '\0';
      const char* base = strrchr(comm2, '/');
      Process_updateComm(proc, base ? base + 1 : comm2);
   }

   /* Show / hide */
   proc->super.show = !(proc->isKernelThread && host->settings->hideKernelThreads)
                   && !(proc->isUserlandThread && host->settings->hideUserlandThreads);

   /* Update task counters.
    * totalTasks counts every thread (processes + extra threads), so that
    * TasksMeter's formula  processes = totalTasks - kernelThreads - userlandThreads
    * stays non-negative. */
   super->totalTasks++;                          /* the process itself */
   if (info.num_threads > 1) {
      unsigned int extraThreads = info.num_threads - 1;
      super->userlandThreads += extraThreads;   /* extra threads */
      super->totalTasks      += extraThreads;   /* include them in total */
   }
   super->runningTasks += runnableInProcess;

   if (!preExisting)
      ProcessTable_add(super, proc);

   return true;
}

void ProcessTable_goThroughEntries(ProcessTable* super) {
   QNXMachine* qhost = (QNXMachine*) super->super.host;

   unsigned int runnableThreads = 0;

   DIR* procDir = opendir("/proc");
   if (!procDir)
      return;

   struct dirent* ent;
   while ((ent = readdir(procDir)) != NULL) {
      if (!isdigit((unsigned char)ent->d_name[0]))
         continue;
      pid_t pid = (pid_t)atoi(ent->d_name);
      if (pid <= 0)
         continue;
      QNXProcessTable_scanProcess(super, pid, &runnableThreads);
   }

   closedir(procDir);

   QNXMachine_updateLoadAvg(qhost, runnableThreads);
}
