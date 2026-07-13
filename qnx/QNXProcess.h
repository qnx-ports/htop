#ifndef HEADER_QNXProcess
#define HEADER_QNXProcess
/*
htop - qnx/QNXProcess.h
(C) 2024 htop dev team
Copyright (c) 2026, BlackBerry Limited. All rights reserved.
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "Machine.h"
#include "Object.h"
#include "Process.h"


typedef struct QNXProcess_ {
   Process super;

   /* Accumulated CPU time in nanoseconds (utime + stime from debug_process_t) */
   uint64_t utime;
   uint64_t stime;

   uint32_t num_fds;
} QNXProcess;


extern const ProcessFieldData Process_fields[LAST_PROCESSFIELD];

Process* QNXProcess_new(const Machine* host);

void Process_delete(Object* cast);

extern const ProcessClass QNXProcess_class;

#endif /* HEADER_QNXProcess */
