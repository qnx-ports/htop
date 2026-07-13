/*
htop - qnx/Platform.c
(C) 2024 htop dev team
Copyright (c) 2026, BlackBerry Limited. All rights reserved.
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "qnx/Platform.h"

#include <math.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <sys/syspage.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#if defined(HAVE_SYS_FS_STATS_H) && defined(HAVE_GPT_H)
#include <sys/dcmd_blk.h>
#include <sys/fs_stats.h>
#endif
#ifdef HAVE_IFADDRS_H
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include "qnx/QNXMachine.h"

#include "CPUMeter.h"
#include "ClockMeter.h"
#include "DateTimeMeter.h"
#include "FileDescriptorMeter.h"
#include "HostnameMeter.h"
#include "Macros.h"
#include "MemoryMeter.h"
#include "MemorySwapMeter.h"
#include "Meter.h"
#include "SwapMeter.h"
#include "SysArchMeter.h"
#include "TasksMeter.h"
#include "UptimeMeter.h"
#include "generic/hostname.h"
#include "generic/uname.h"


const ScreenDefaults Platform_defaultScreens[] = {
   {
      .name    = "Main",
      .columns = "PID USER PRIORITY M_VIRT M_RESIDENT STATE PERCENT_CPU PERCENT_MEM Command",
      .sortKey = "PERCENT_CPU",
   }
};

const unsigned int Platform_numberOfDefaultScreens = ARRAYSIZE(Platform_defaultScreens);

/*
 * QNX signal table.
 * Signal numbers match /usr/include/signal.h on QNX 8.
 */
const SignalItem Platform_signals[] = {
   { .name = " 0 Cancel",    .number =  0 },
   { .name = " 1 SIGHUP",    .number =  1 },
   { .name = " 2 SIGINT",    .number =  2 },
   { .name = " 3 SIGQUIT",   .number =  3 },
   { .name = " 4 SIGILL",    .number =  4 },
   { .name = " 5 SIGTRAP",   .number =  5 },
   { .name = " 6 SIGABRT",   .number =  6 },
   { .name = " 7 SIGEMT",    .number =  7 },
   { .name = " 8 SIGFPE",    .number =  8 },
   { .name = " 9 SIGKILL",   .number =  9 },
   { .name = "10 SIGBUS",    .number = 10 },
   { .name = "11 SIGSEGV",   .number = 11 },
   { .name = "12 SIGSYS",    .number = 12 },
   { .name = "13 SIGPIPE",   .number = 13 },
   { .name = "14 SIGALRM",   .number = 14 },
   { .name = "15 SIGTERM",   .number = 15 },
   { .name = "16 SIGUSR1",   .number = 16 },
   { .name = "17 SIGUSR2",   .number = 17 },
   { .name = "18 SIGCHLD",   .number = 18 },
   { .name = "19 SIGPWR",    .number = 19 },
   { .name = "20 SIGWINCH",  .number = 20 },
   { .name = "21 SIGURG",    .number = 21 },
   { .name = "22 SIGPOLL",   .number = 22 },
   { .name = "23 SIGSTOP",   .number = 23 },
   { .name = "24 SIGTSTP",   .number = 24 },
   { .name = "25 SIGCONT",   .number = 25 },
   { .name = "26 SIGTTIN",   .number = 26 },
   { .name = "27 SIGTTOU",   .number = 27 },
   { .name = "28 SIGVTALRM", .number = 28 },
   { .name = "29 SIGPROF",   .number = 29 },
   { .name = "30 SIGXCPU",   .number = 30 },
   { .name = "31 SIGXFSZ",   .number = 31 },
   { .name = "32 SIGDOOM",   .number = 32 },
};

const unsigned int Platform_numberOfSignals = ARRAYSIZE(Platform_signals);

/*
 * Memory display classes.
 */
enum {
   MEMORY_CLASS_USED = 0,
};

const MemoryClass Platform_memoryClasses[] = {
   [MEMORY_CLASS_USED] = {
      .label       = "used",
      .countsAsUsed  = true,
      .countsAsCache = false,
      .color       = MEMORY_1,
   },
};

const unsigned int Platform_numberOfMemoryClasses = ARRAYSIZE(Platform_memoryClasses);

const MeterClass* const Platform_meterTypes[] = {
   &CPUMeter_class,
   &ClockMeter_class,
   &DateMeter_class,
   &DateTimeMeter_class,
   &MemoryMeter_class,
   &TasksMeter_class,
   &UptimeMeter_class,
   &SecondsUptimeMeter_class,
   &HostnameMeter_class,
#if defined(HAVE_IFADDRS_H)
   &NetworkIOMeter_class,
#endif
#if defined(HAVE_SYS_FS_STATS_H) && defined(HAVE_GPT_H)
   &DiskIORateMeter_class,
#endif
   &SysArchMeter_class,
   &AllCPUsMeter_class,
   &AllCPUs2Meter_class,
   &AllCPUs4Meter_class,
   &AllCPUs8Meter_class,
   &LeftCPUsMeter_class,
   &RightCPUsMeter_class,
   &LeftCPUs2Meter_class,
   &RightCPUs2Meter_class,
   &LeftCPUs4Meter_class,
   &RightCPUs4Meter_class,
   &LeftCPUs8Meter_class,
   &RightCPUs8Meter_class,
   &FileDescriptorMeter_class,
   &BlankMeter_class,
   NULL
};

bool Platform_init(void) {
   return true;
}

void Platform_done(void) {
}

void Platform_setBindings(Htop_Action* keys) {
   (void) keys;
}

int Platform_getUptime(void) {
   struct timespec tp;
   if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
      return -1;

   return (int)tp.tv_sec;
}

// QNX doesn't have a getloadavg api and we can't calculate that value accurately without building a resource manager for it
// Approximations within htop will be innacurate so it's better to have no data than invalid data
void Platform_setLoadAvg(double one, double five, double fifteen) {
   (void)one; (void)five; (void)fifteen;
}

void Platform_getLoadAverage(double* one, double* five, double* fifteen) {
   *one     = -1.0;
   *five    = -1.0;
   *fifteen = -1.0;
}

pid_t Platform_getMaxPid(void) {
   return INT_MAX;
}

double Platform_setCPUValues(Meter* this, unsigned int cpu) {
   const Machine* host = this->host;
   const QNXMachine* qhost = (const QNXMachine*) host;

   const CPUData* data = &qhost->cpus[cpu];

   double* v = this->values;
   v[CPU_METER_NICE]      = 0.0;
   v[CPU_METER_NORMAL]    = data->userPercent;
   v[CPU_METER_KERNEL]    = data->systemPercent;
   v[CPU_METER_FREQUENCY] = data->frequency;
   v[CPU_METER_TEMPERATURE] = NAN;
   this->curItems = 3;

   double percent = v[CPU_METER_NORMAL] + v[CPU_METER_KERNEL];
   return CLAMP(percent, 0.0, 100.0);
}

void Platform_setMemoryValues(Meter* this) {
   const Machine* host = this->host;
   const QNXMachine* qhost = (const QNXMachine*) host;

   this->total = host->totalMem;
   this->values[MEMORY_CLASS_USED] = (double) qhost->usedMem;
   this->curItems = 1;
}

void Platform_setSwapValues(Meter* this) {
   const Machine* host = this->host;
   this->total = host->totalSwap;
   this->values[SWAP_METER_USED] = host->usedSwap;
}

char* Platform_getProcessEnv(pid_t pid) {
   /* QNX does not expose process environment via /proc easily */
   (void) pid;
   return NULL;
}

FileLocks_ProcessData* Platform_getProcessLocks(pid_t pid) {
   (void) pid;
   return NULL;
}

void Platform_getFileDescriptors(double* used, double* max) {
   // We can technically get one processes open connections by enumerating ConnectServerInfo(), however we need the total number of system fds
   // We might also have issues with double-counting
   // There is sysctl kern.openfiles, however that number is smaller than it should be, indicating that there are somethings it's not counting
   *used = __curr_num_fds;

   struct rlimit rlp;
   if (getrlimit(RLIMIT_NOFILE, &rlp) == -1) *max = NAN;
   else *max = (double) rlp.rlim_cur;
}

bool Platform_getDiskIO(DiskIOData* data) {
#if defined(HAVE_SYS_FS_STATS_H) && defined(HAVE_GPT_H)
   int disk=0;
   char device_path[PATH_MAX];
   int fd;
   data->totalBytesRead = 0;
   data->totalBytesWritten = 0;
   data->numDisks = 0;
   data->totalMsTimeSpend = 0;
   while ((sprintf(device_path, "/dev/hd%d", disk)), (fd = open(device_path, O_ACCMODE)), disk++, fd != -1) {
      struct fs_stats fst;
      if (devctl(fd, DCMD_FSYS_STATISTICS, &fst,  sizeof(fst), NULL) != EOK) {
         continue;
      }

      data->totalBytesRead += fst.s_buf_rphys_bytes;
      data->totalBytesWritten += fst.s_buf_wphys_bytes;
      data->numDisks++;
      // I have no idea how to get that information, it will result in usage% not rendering
      data->totalMsTimeSpend = 0;

      close(fd);
   }
   return true;

#else
   (void) data;
   return false;
#endif
}

bool Platform_getNetworkIO(NetworkIOData* data) {
#ifdef HAVE_IFADDRS_H
   // netbsd implementation
   struct ifaddrs* ifaddrs = NULL;

   if (getifaddrs(&ifaddrs) != 0)
      return false;

   for (const struct ifaddrs* ifa = ifaddrs; ifa; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr)
         continue;
      if (ifa->ifa_addr->sa_family != AF_LINK)
         continue;
      if (ifa->ifa_flags & IFF_LOOPBACK)
         continue;

      const struct if_data* ifd = (const struct if_data*)ifa->ifa_data;

      data->bytesReceived += ifd->ifi_ibytes;
      data->packetsReceived += ifd->ifi_ipackets;
      data->bytesTransmitted += ifd->ifi_obytes;
      data->packetsTransmitted += ifd->ifi_opackets;
   }

   freeifaddrs(ifaddrs);
   return true;
#else
   (void) data;
   return false;
#endif
}

void Platform_getBattery(double* percent, ACPresence* isOnAC) {
   *percent  = NAN;
   *isOnAC   = AC_ERROR;
}

