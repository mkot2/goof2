#include "vm/memory.hxx"

#include "vm.hxx"

#if GOOF2_HAS_OS_VM
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace goof2 {
#if defined(_WIN32)
void* defaultOsAlloc(size_t bytes) {
    const size_t maxReserve = static_cast<size_t>(GOOF2_TAPE_MAX_BYTES);
    void* base = VirtualAlloc(nullptr, maxReserve, MEM_RESERVE, PAGE_READWRITE);
    if (base) {
        void* commit = VirtualAlloc(base, bytes, MEM_COMMIT, PAGE_READWRITE);
        if (commit) return base;
        VirtualFree(base, 0, MEM_RELEASE);
    }
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
void defaultOsFree(void* ptr, size_t) { VirtualFree(ptr, 0, MEM_RELEASE); }
#else
void* defaultOsAlloc(size_t bytes) {
    return mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
void defaultOsFree(void* ptr, size_t bytes) { munmap(ptr, bytes); }
#endif

void* (*os_alloc)(size_t) = defaultOsAlloc;
void (*os_free)(void*, size_t) = defaultOsFree;
}  // namespace goof2
#endif
