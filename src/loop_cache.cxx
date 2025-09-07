#include <unordered_map>
#include <vector>

#include "vm.hxx"

namespace goof2 {
static LoopCache loopCacheInstance;

LoopCache& getLoopCache() { return loopCacheInstance; }

void clearLoopCache() { loopCacheInstance.clear(); }
}  // namespace goof2
