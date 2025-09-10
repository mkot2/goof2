#pragma once

#include <cstddef>

namespace goof2 {
#if GOOF2_HAS_OS_VM
void* defaultOsAlloc(size_t bytes);
void defaultOsFree(void* ptr, size_t bytes);
extern void* (*os_alloc)(size_t);
extern void (*os_free)(void*, size_t);
#endif
}  // namespace goof2
