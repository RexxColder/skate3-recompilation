# Skate 3 Recompilation Project

## Current Status: DEADLOCK (PRE-RENDER)

### Latest Working Test

```
✅ Runtime initialized
✅ Vulkan GPU working (AMD Radeon RX 580)
✅ XEX loaded and parsed
✅ Kernel launching (XAM / IO functions)
✅ Swapchains created (1280x720 and 1440x872)
✅ Phase C/B: GPU Hooks Implementation
```

### Key Findings (Update Mar 15, 2026)

1. **XEXP issue** - Continues to fail the hash validation; disabled.
2. **GPU Hooks (Phase D.3/D.4 Resolved)** - Successfully identified memory address mismatch (`0x830286B0`) and stubbed D3D VTable (AddRef, Release, Allocate).
3. **Primary Present Wrapper** - Found at `0x82B5A648`; ready for testing once the engine proceeds past initialization.
4. **Initialization Breakthrough** - The engine no longer segfaults during device setup. Current focus shifted to unblocking the main frame loop.

### Working Configuration

- XEX: `/home/rexx/Escritorio/Xbox/skate3_extracted/default.xex` (6.6MB)
- NO XEXP (due to mismatch)
- `src/gpu_hooks.cpp` properly hooking `0x82B5A648` (VdSwap Call)
- ReXGlue automatically loading Vulkan 1.4

### What's Needed

1. **Investigate Sync Lock**: The game appears stuck on `VdSetGraphicsInterruptCallback` or a similar VSync lock/Yield.
2. We must determine the active execution threads using a Host Debugger (`gdb` or `lldb`).

### Project Location
```
/home/rexx/Escritorio/Xbox/skate3_project/
```

### Next Steps (Phase D)

1. **GDB Profiling**: Run the game attached to `gdb` and dump backtraces on the main executable thread upon hang.
2. **Handle the WaitObject / Interrupt**: Find out if the game is waiting for a certain `KeSetEvent` from the graphics layer!
3. **Bypass or Patch Deadlock**: Provide a shim if a kernel callback is missing.
