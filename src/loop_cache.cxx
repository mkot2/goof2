#include <mutex>
#include <unordered_map>
#include <vector>

#include "vm.hxx"

namespace goof2 {
static LoopCache loopCacheInstance;
static std::mutex loopCacheMutex;

LoopCache& getLoopCache() { return loopCacheInstance; }

std::mutex& getLoopCacheMutex() { return loopCacheMutex; }

void clearLoopCache() {
    std::lock_guard<std::mutex> lock(loopCacheMutex);
    loopCacheInstance.clear();
}
}  // namespace goof2
