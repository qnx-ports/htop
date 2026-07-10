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
#include <sys/memmsg.h>

#include "Machine.h"
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
   this->lastScanTime = 0;

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
      return BLOCKED;
   case STATE_NANOSLEEP:
      return SLEEPING;
   case STATE_WAITPAGE:
      return PAGING;
   default:
      return UNKNOWN;
   }
}

void ProcessTable_goThroughEntries(ProcessTable* super) {
   QNXProcessTable *this = (QNXProcessTable*) super;
   QNXMachine* qhost = (QNXMachine*) super->super.host;
   Machine* host  = super->super.host;

   struct timeval currentTime;
   gettimeofday(&currentTime, NULL);
   const uint64_t timeNowNs = (currentTime.tv_sec * 1e6 + currentTime.tv_usec) * 1e3;

   uint64_t elapsedTime = timeNowNs - this->lastScanTime;
   this->lastScanTime = timeNowNs;

   DIR* procDir = opendir("/proc");
   if (!procDir)
      return;


   uint32_t acc_fds = 0;
   struct dirent* ent;
   while ((ent = readdir(procDir)) != NULL) {
      if (!isdigit((unsigned char)ent->d_name[0])) continue;
      pid_t pid = atoi(ent->d_name);
      if (pid <= 0) continue;
      char ctlpath[64];
      xSnprintf(ctlpath, sizeof(ctlpath), "/proc/%d/ctl", (int)pid);

      int fd = open(ctlpath, O_RDONLY);
      if (fd < 0) continue;

      procfs_info info = {0};
      memset(&info, 0, sizeof(info));
      if (devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), NULL) != EOK) {
         close(fd);
         continue;
      }

      procfs_status status = {0};
      memset(&status, 0, sizeof(status));
      status.tid = 1;
      if (devctl(fd, DCMD_PROC_TIDSTATUS, &status, sizeof(status), NULL) != EOK) {
         close(fd);
         continue;
      }

      procfs_asinfo asinfo = {0};
      if (devctl(fd, DCMD_PROC_ASINFO, &asinfo, sizeof(asinfo), NULL) != EOK) {
         close(fd);
         continue;
      }

      // we need to define this struct since devctl fills the hdr field and then adds the path after.
      struct {
         procfs_debuginfo hdr;
         char             path[PATH_MAX];
      } mapdebug = {0};
      // dbg.hdr.vaddr = info.base_address;
      if (devctl(fd, DCMD_PROC_MAPDEBUG_BASE, &mapdebug, sizeof(mapdebug), NULL) != EOK) {
         continue;
         close(fd);
      }
      close(fd);

      bool preExisting;
      Process* proc = ProcessTable_getProcess(super, pid, &preExisting, QNXProcess_new);
      QNXProcess* qp = (QNXProcess*) proc;

      if (!preExisting) {
         // Initialise fields that don't change
         Process_setPid(proc, pid);
         Process_setParent(proc, info.parent);
         Process_setThreadGroup(proc, pid);

         proc->starttime_ctime = info.start_time / 1e9;
         Process_fillStarttimeBuffer(proc);
      }

      // Fields updated each scan
      proc->super.updated = true;
      proc->isKernelThread = (status.flags & _DEBUG_FLAG_ISSYS) > 0;
      proc->isUserlandThread = !proc->isKernelThread;
      proc->pgrp = info.pgrp;
      proc->session = info.sid;
      proc->nlwp = (int) info.num_threads;
      proc->st_uid = info.uid;
      proc->priority = (int) status.priority;
      proc->processor = (int) status.last_cpu;
      proc->m_virt = asinfo.as_used / 1024;
      proc->m_resident = asinfo.rss / 1024;
      qp->num_fds = info.num_fdcons;
      acc_fds += info.num_fdcons;

      // Process state - use the first thread's state
      proc->state = QNXProcessTable_threadStateToProcessState(status.state);
      if (info.flags & _NTO_PF_ZOMBIE) {
         proc->state = ZOMBIE;
      }

      // CPU time accounting
      uint64_t cpuTimeNs = info.utime + info.stime;
      uint64_t prevCpuTime = qp->utime + qp->stime;
      qp->utime = info.utime;
      qp->stime = info.stime;

      if (preExisting && elapsedTime > 0) {
         uint64_t deltaCpu = cpuTimeNs - prevCpuTime;
         float percent = ((float)deltaCpu / (float)elapsedTime) * 100.0f;
         proc->percent_cpu = percent;
      } else {
         proc->percent_cpu = 0;
      }

      Process_updateCPUFieldWidths(proc->percent_cpu);

      // Total accumulated time in centiseconds (for TIME column)
      proc->time = (unsigned long long)(cpuTimeNs / 1e6);

      // Memory
      if (host->totalMem > 0) proc->percent_mem = ((double)asinfo.rss / (double)host->totalMem) * 100.0;
      else proc->percent_mem = 0.0;

      // User name
      proc->user = UsersTable_getRef(host->usersTable, proc->st_uid);

      // Command line
      Process_updateCmdline(proc, mapdebug.hdr.path, 0, strlen(mapdebug.hdr.path));

      if (mapdebug.path[0]) {
         Process_updateComm(proc, mapdebug.path);
      }else {
         char comm[64];
         comm[63] = '\0';
         snprintf(comm, 63, "[%d]", (int)pid);
         Process_updateComm(proc, comm);
      }

      // Show / hide
      proc->super.show = true;
      if (proc->isKernelThread && host->settings->hideKernelThreads) proc->super.show = false;
      if (proc->isUserlandThread && host->settings->hideUserlandThreads) proc->super.show = false;

      if (proc->super.show) {
         if (proc->state == RUNNING) {
            super->runningTasks++;
         }
         super->totalTasks += proc->nlwp + 1;
         if (proc->isKernelThread) {
            super->kernelThreads += proc->nlwp;
         }
         if (proc->isUserlandThread) {
            super->userlandThreads += proc->nlwp;
         }
      }

      if (!preExisting) {
         ProcessTable_add(super, proc);
      }
   }
   __curr_num_fds = acc_fds;

   closedir(procDir);
}
