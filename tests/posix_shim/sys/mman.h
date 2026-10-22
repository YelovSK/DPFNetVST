#pragma once
/* Just enough POSIX for upstream's contract tests: guard pages via VirtualAlloc/VirtualProtect. */
#include <windows.h>
#include <stddef.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0
#define MAP_ANONYMOUS 0
#define MAP_FAILED ((void *)-1)
static inline DWORD shim_protect(int prot) {
    return (prot & PROT_WRITE) ? PAGE_READWRITE : (prot & PROT_READ) ? PAGE_READONLY : PAGE_NOACCESS;
}
static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)flags; (void)fd; (void)off;
    void *p = VirtualAlloc(NULL, len, MEM_RESERVE | MEM_COMMIT, shim_protect(prot));
    return p ? p : MAP_FAILED;
}
static inline int mprotect(void *addr, size_t len, int prot) {
    DWORD old; return VirtualProtect(addr, len, shim_protect(prot), &old) ? 0 : -1;
}
static inline int munmap(void *addr, size_t len) { (void)len; return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1; }
