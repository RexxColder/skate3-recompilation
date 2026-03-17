# Skate 3 Recompilation Project

This project aims to recompile and port **Skate 3** to modern platforms using the **ReXGlue** runtime and **Vulkan** graphics API.

## Project Goal
The objective is to achieve a functional, high-performance recompilation of the original Xbox 360 title, bypassing traditional emulation overhead by leveraging native execution and modern graphics hooks.

## Current Development Status: Phase D.4 Resolved
We have successfully navigated the initial Direct3D initialization sequence.

### Major Achievements:
- **Runtime Stability**: ReXGlue runtime and Vulkan GPU layer (AMD/NVIDIA/Intel) are functional.
- **D3D Crash Resolution**:
  - Fixed a critical memory crash in `sub_826B57D0` caused by an address mismatch during global pointer initialization.
  - Resolved a virtual method crash in `sub_82A60B90` by implementing a dummy D3D interface and VTable stub system.
- **Memory Management**: Implemented custom guest memory allocation stubs (`Guest_D3D_Allocate`).

### Current Blocker:
The engine is currently experiencing a **deadlock post-initialization**. The main iteration loop enters a silent halt state after successful device creation. Ongoing work focuses on tracing kernel synchronization primitives to unblock the rendering loop.

## How to Build
This project uses CMake.

1. **Prerequisites**:
   - CMake 3.20+
   - Vulkan SDK
   - C++20 Compatible Compiler

2. **Clone and Build**:
   ```bash
   mkdir build && cd build
   cmake ..
   make -j$(nproc)
   ```

3. **Run**:
   ```bash
   ./skate3
   ```

## Project Structure
- `src/gpu_hooks.cpp`: The Direct3D/GPU interception and stubbing layer.
- `generated/`: Recompiled C++ source code derived from the original binary.
- `assets/`: Symbolic link to extracted game resources.

---
*For technical details and historical debugging notes, see `PROGRESS.md`, `STATUS.md`, and the internal documentation.*
