#pragma once
#include <windows.h>
#define _SC_PAGESIZE 30
static inline long sysconf(int name) {
    SYSTEM_INFO si; GetSystemInfo(&si); return name == _SC_PAGESIZE ? (long)si.dwPageSize : -1;
}
