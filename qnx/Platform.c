/*
htop - qnx/Platform.c
(C) 2024 htop dev team
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

#include "CPUMeter.h"
#include "ClockMeter.h"
#include "DateTimeMeter.h"
#include "FileDescriptorMeter.h"
#include "HostnameMeter.h"
#include "LoadAverageMeter.h"
#include "Macros.h"
#include "MemoryMeter.h"
#include "MemorySwapMeter.h"
#include "Meter.h"
#include "SwapMeter.h"
#include "SysArchMeter.h"
#include "TasksMeter.h"
#include "UptimeMeter.h"
#include "XUtils.h"
#include "generic/hostname.h"
#include "generic/uname.h"
#include "qnx/QNXMachine.h"


const ScreenDefaults Platform_defaultScreens[] = {
   {
      .name    = "Main",
      .columns = "PID USER PRIORITY NICE M_VIRT M_RESIDENT STATE PERCENT_CPU PERCENT_MEM TIME Command",
      .sortKey = "PERCENT_CPU",
   },
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
   &SwapMeter_class,
   &MemorySwapMeter_class,
   &TasksMeter_class,
   &UptimeMeter_class,
   &SecondsUptimeMeter_class,
   &BatteryMeter_class,
   &HostnameMeter_class,
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
   struct qtime_entry* qtime = SYSPAGE_ENTRY(qtime);
   if (!qtime)
      return -1;

   time_t now = time(NULL);
   return (int)(now - (time_t) qtime->boot_time);
}

// QNX doesn't have a getloadavg api and we can't calculate that value accurately without building a resource manager for it
// Approximations within htop will be innacurate so it's better to have no data than invalid data
void Platform_setLoadAvg(double one, double five, double fifteen) { }

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

   /* cpu == 0 means aggregate; otherwise 1-indexed per-cpu */
   const CPUData* data = (cpu == 0) ? &qhost->cpus[0] : &qhost->cpus[cpu];

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
   *used = NAN;
   *max  = NAN;
}

bool Platform_getDiskIO(DiskIOData* data) {
   (void) data;
   return false;
}

bool Platform_getNetworkIO(NetworkIOData* data) {
   (void) data;
   return false;
}

void Platform_getBattery(double* percent, ACPresence* isOnAC) {
   *percent  = NAN;
   *isOnAC   = AC_ERROR;
}

void Platform_getHostname(char* buffer, size_t size) {
   Generic_hostname(buffer, size);
}

static void QNX_fetchRelease(char* buffer, size_t size) {
   struct utsname u;
   if (uname(&u) == 0)
      snprintf(buffer, size, "QNX %s", u.release);
   else
      snprintf(buffer, size, "QNX");
}

const char* Platform_getRelease(void) {
   return Generic_unameRelease(QNX_fetchRelease);
}
