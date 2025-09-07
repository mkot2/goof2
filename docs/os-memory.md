# OS-backed memory expansion

The virtual machine can grow its tape using operating system primitives when
using the OS-backed memory model.

- Linux: existing mappings are extended with mremap(2).
- Windows: a large virtual region is reserved up-front and pages are committed
  on demand with VirtualAlloc + MEM_COMMIT. This keeps the working set low for
  long-running programs while allowing cheap growth.

If these platform APIs fail, goof2 falls back to allocating a new region and
copying the previous contents.

Heuristics: the engine selects OS-backed memory once the predicted tape size
exceeds roughly 64 MiB (based on cell width). Medium-size tapes (> 8 MiB) use a
paged allocator to avoid large contiguous reallocations. Smaller tapes use
Fibonacci or contiguous growth.

