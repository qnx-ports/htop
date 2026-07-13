#ifndef HEADER_QNXProcessTable
#define HEADER_QNXProcessTable
/*
htop - qnx/QNXProcessTable.h
(C) 2024 htop dev team
Copyright (c) 2026, BlackBerry Limited. All rights reserved.
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "ProcessTable.h"


typedef struct QNXProcessTable_ {
   ProcessTable super;

   uint64_t lastScanTime;
} QNXProcessTable;

#endif /* HEADER_QNXProcessTable */
