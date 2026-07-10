#ifndef HEADER_QNXProcessTable
#define HEADER_QNXProcessTable
/*
htop - qnx/QNXProcessTable.h
(C) 2024 htop dev team
(C) 2026 QNXe team
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "ProcessTable.h"


typedef struct QNXProcessTable_ {
   ProcessTable super;

   uint64_t lastScanTime;
} QNXProcessTable;

#endif /* HEADER_QNXProcessTable */
