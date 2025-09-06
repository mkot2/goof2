# OS-backed memory expansion

The virtual machine can grow its tape using operating system primitives when
using the OS-backed memory model.

- **Linux**: existing mappings are extended with [`mremap(2)`](https://man7.org/linux/man-pages/man2/mremap.2.html).
- **Windows**: additional pages are committed with `VirtualAlloc` and
  `MEM_COMMIT`.

If these platform APIs fail, goof2 falls back to allocating a new region and
copying the previous contents.
