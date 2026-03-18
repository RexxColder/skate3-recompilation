#include "gpu_hooks.h"
#include <rex/logging.h>
#include <rex/runtime.h>
#include <unordered_map>

extern "C" {

static std::unordered_map<uint32_t, PPCFunc*> g_originalFunctions;

static uint32_t g_guestDeviceAddress = 0;
static uint32_t g_guestVTableAddress = 0;
static uint32_t g_guestMethodThunksBase = 0;
static uint32_t g_guestD3DInterfaceAddress = 0;
static uint32_t g_guestDummyStringBuffer = 0;
static uint32_t g_guestDummyPathBuffer = 0;
static uint32_t g_gfxInterruptCallback = 0;
static uint32_t g_gfxInterruptContext = 0;

// Forward Declarations
void sub_82A60208(PPCContext& ctx, uint8_t* base);

// Generic stub for D3D vtable methods
static uint32_t g_stubCallCount = 0;
void Guest_D3D_STUB(PPCContext& ctx, uint8_t* base) {
  ++g_stubCallCount;
  uint32_t lr = (uint32_t)ctx.lr;
  uint32_t this_ptr = ctx.r3.u32;
  
  if (g_stubCallCount <= 50 || (g_stubCallCount % 5000) == 0) {
    REXLOG_INFO("Guest_D3D_STUB #{}: this=0x{:08X}, lr=0x{:08X}", 
                g_stubCallCount, this_ptr, lr);
  }
  if (g_stubCallCount == 100) {
    REXLOG_WARN("Guest_D3D_STUB called 100+ times - possible infinite loop! Last lr=0x{:08X}", lr);
  }
  ctx.r3.u64 = 0; // S_OK
}

void Guest_D3D_QueryInterface(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO("Guest_D3D_QueryInterface called.");
  ctx.r3.u64 = 0x80004001; // E_NOTIMPL
}

void Guest_D3D_AddRef(PPCContext& ctx, uint8_t* base) {
  // REXLOG_INFO("Guest_D3D_AddRef called.");
  ctx.r3.u64 = (uint64_t)ctx.r3.u32; // Just return 'this' for now
}

void Guest_D3D_Release(PPCContext& ctx, uint8_t* base) {
  // REXLOG_INFO("Guest_D3D_Release called.");
  ctx.r3.u64 = (uint64_t)ctx.r3.u32;
}

// vtable[1] = Allocate(size)
void Guest_D3D_Allocate(PPCContext& ctx, uint8_t* base) {
  uint32_t size = ctx.r4.u32;
  uint32_t addr = rex::Runtime::instance()->memory()->SystemHeapAlloc(size);
  rex::Runtime::instance()->memory()->Zero(addr, size);
  ctx.r3.u64 = (uint64_t)addr;
  REXLOG_INFO("Guest_D3D_Allocate: allocated {} bytes at 0x{:08X}", size, addr);
}

void VdSwap_Hook(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO("VdSwap_Hook: Presenting frame!");
  auto it = g_originalFunctions.find(0x82F7214C);
  if (it != g_originalFunctions.end()) {
    it->second(ctx, base);
  }
}

// Hook sub_82A60208 (D3D CreateDevice equivalent)
// Original creates a 152-byte struct, copies fields from the caller, 
// initializes a linked list, and stores the global device pointer.
// The CALLER (sub_8293F460) reads from r3+8 onwards after this returns.
void sub_82A60208(PPCContext& ctx, uint8_t* base) {
  uint32_t caller_struct = ctx.r3.u32;  // r30 in original = caller's "this"
  REXLOG_INFO("Guest_CreateDevice_Stub called. caller_struct=0x{:08X}", caller_struct);
  
  auto runtime = rex::Runtime::instance();
  if (runtime && g_guestDeviceAddress == 0) {
      // Allocate the main 152-byte struct (matches original allocation size)
      g_guestDeviceAddress = runtime->memory()->SystemHeapAlloc(256);
      g_guestVTableAddress = runtime->memory()->SystemHeapAlloc(1024);
      // Allocate thunks in the function table thunk reserve (past the end of the code segment)
      // skate3 code ends at ~0x82F734D4, and the reserve adds 64KB (0x10000) up to 0x82F834D4.
      // We pick a safe address inside the reserve that won't conflict with real code.
      g_guestMethodThunksBase = 0x82F80000;
      g_guestDummyStringBuffer = runtime->memory()->SystemHeapAlloc(2048);
      g_guestDummyPathBuffer = runtime->memory()->SystemHeapAlloc(2048);
      
      REXLOG_INFO("Allocated DeviceStruct=0x{:08X}, VTable=0x{:08X}, Thunks=0x{:08X}",
                  g_guestDeviceAddress, g_guestVTableAddress, g_guestMethodThunksBase);
      
      // Zero everything first
      runtime->memory()->Zero(g_guestDeviceAddress, 256);
      runtime->memory()->Zero(g_guestVTableAddress, 1024);
      runtime->memory()->Zero(g_guestDummyStringBuffer, 2048);
      runtime->memory()->Zero(g_guestDummyPathBuffer, 2048);
      
      // Initialize VTable with stub functions
      REXLOG_INFO("Initializing VTable thunks...");
      for (int i = 0; i < 256; ++i) {
        uint32_t method_addr = g_guestMethodThunksBase + (i * 4);
        *(rex::be<uint32_t>*)(base + g_guestVTableAddress + (i * 4)) = method_addr;
        
        if (i == 0) runtime->memory()->SetFunction(method_addr, Guest_D3D_QueryInterface);
        else if (i == 1) runtime->memory()->SetFunction(method_addr, Guest_D3D_Allocate);
        else if (i == 2) runtime->memory()->SetFunction(method_addr, Guest_D3D_AddRef);
        else if (i == 3) runtime->memory()->SetFunction(method_addr, Guest_D3D_Release);
        else runtime->memory()->SetFunction(method_addr, Guest_D3D_STUB);
      }
      
      // Allocate the D3D interface object (just a single vtable pointer)
      g_guestD3DInterfaceAddress = runtime->memory()->SystemHeapAlloc(4);
      *(rex::be<uint32_t>*)(base + g_guestD3DInterfaceAddress) = g_guestVTableAddress;
      REXLOG_INFO("Allocated D3D Interface=0x{:08X} (points to VTable=0x{:08X})",
                  g_guestD3DInterfaceAddress, g_guestVTableAddress);
  }
  
  uint32_t dev = g_guestDeviceAddress;
  
  // --- Match original sub_82A60208 struct layout ---
  
  // Self-referencing linked list pointers (original lines 88940-88945)
  // stw r3,4(r3) and stw r3,0(r3)
  *(rex::be<uint32_t>*)(base + dev + 0) = dev;  // next pointer = self
  *(rex::be<uint32_t>*)(base + dev + 4) = dev;  // prev pointer = self
  
  // Copy 6 dwords from caller_struct+12..+36 into dev+8..+32
  // This matches the original copy loop at lines 88950-88961
  // (lwzu r9,4(r11) from r11=r30+8, stwu r9,4(r10) to r10=r3+4)
  for (int i = 0; i < 6; ++i) {
      uint32_t src_offset = 12 + (i * 4);  // caller+12, +16, +20, +24, +28, +32
      uint32_t dst_offset = 8 + (i * 4);   // dev+8, +12, +16, +20, +24, +28
      uint32_t val = *(rex::be<uint32_t>*)(base + caller_struct + src_offset);
      *(rex::be<uint32_t>*)(base + dev + dst_offset) = val;
      REXLOG_INFO("  Copy caller+{} -> dev+{}: 0x{:08X}", src_offset, dst_offset, val);
  }
  
  // Set fields as in original (lines 88968-88996)
  *(rex::be<uint32_t>*)(base + dev + 32) = 0xFFFFFFFF; // -1 (linked list sentinel)
  *(rex::be<uint32_t>*)(base + dev + 36) = 0;
  *(rex::be<uint32_t>*)(base + dev + 40) = 0;
  *(rex::be<uint32_t>*)(base + dev + 44) = 0;
  
  // +48 = caller[1] (r7 = *(r30+4)), +52 = caller[0] (r6 = *(r30+0))
  uint32_t caller_val0 = *(rex::be<uint32_t>*)(base + caller_struct + 0);
  uint32_t caller_val4 = *(rex::be<uint32_t>*)(base + caller_struct + 4);
  *(rex::be<uint32_t>*)(base + dev + 48) = caller_val4;
  *(rex::be<uint32_t>*)(base + dev + 52) = caller_val0;
  REXLOG_INFO("  dev+48 (from caller+4)=0x{:08X}, dev+52 (from caller+0)=0x{:08X}", 
              caller_val4, caller_val0);
  
  *(rex::be<uint32_t>*)(base + dev + 56) = 0;
  *(rex::be<uint32_t>*)(base + dev + 60) = 0;
  // +64 = vtable pointer: original is "addi r8,r9,25624" where r9=-2107768832
  // = 0x82780000 + 25624 = 0x82786418
  *(rex::be<uint32_t>*)(base + dev + 64) = 0x82786418;
  *(rex::be<uint32_t>*)(base + dev + 68) = 0;
  
  // Initialize critical sections at +72 and +112 (original calls sub_82A17A38)
  // We must call the original PPC functions to properly init these
  {
    auto memory = runtime->memory();
    
    // sub_82A17A38(dev+72, 1) — init critical section
    PPCFunc* initCS = memory->GetFunction(0x82A17A38);
    if (initCS) {
      ctx.r3.u64 = (uint64_t)(dev + 72);
      ctx.r4.s64 = 1;
      initCS(ctx, base);
      REXLOG_INFO("  Initialized CriticalSection at dev+72");
      
      // sub_82A17A38(dev+112, 1) — init second critical section
      ctx.r3.u64 = (uint64_t)(dev + 112);
      ctx.r4.s64 = 1;
      initCS(ctx, base);
      REXLOG_INFO("  Initialized CriticalSection at dev+112");
    } else {
      REXLOG_WARN("  Could not find sub_82A17A38 for CriticalSection init!");
    }
  }
  
  // Store device at the global location the engine expects
  // Original: stw r31, -31056(r11) where r11 = 0x83070000
  // = store at 0x83068690
  uint32_t global_addr = 0x83068690;
  *(rex::be<uint32_t>*)(base + global_addr) = dev;
  REXLOG_INFO("Stored DeviceStruct 0x{:08X} at global 0x{:08X}", dev, global_addr);
  
  // PHASE D.3 - Fix SIGSEGV in sub_826B57D0:
  // Store device struct at 0x830186B0 as well!
  // In the original sub_82A60208, it does: stw r31, -31056(r11) where r11=0x83020000
  uint32_t global_addr2 = 0x830286B0;
  *(rex::be<uint32_t>*)(base + global_addr2) = dev;
  REXLOG_INFO("Stored DeviceStruct 0x{:08X} at global 0x{:08X}", dev, global_addr2);

  // Also store D3D device pointer at -31052 = 0x83068694 AND 0x830286B4
  // Original: stw r3, -31052(r11) — stores the D3D interface 
  uint32_t devInterface = g_guestD3DInterfaceAddress;
  *(rex::be<uint32_t>*)(base + 0x83068694) = devInterface;
  *(rex::be<uint32_t>*)(base + 0x830286B4) = devInterface;
  REXLOG_INFO("Stored D3D interface 0x{:08X} at 0x83068694 and 0x830286B4", devInterface);
  
  // Call PostInit
  REXLOG_INFO("Calling sub_82A60970 (PostInit) naturally before returning from CreateDevice...");
  auto fn970 = runtime->memory()->GetFunction(0x82A60970);
  if (fn970) fn970(ctx, base);
  
  // CRITICAL: Return the device struct pointer in r3!
  // The caller (sub_8293F460) reads r3+8 onwards after we return.
  ctx.r3.u64 = (uint64_t)dev;
  REXLOG_INFO("Returning DeviceStruct 0x{:08X} in r3", dev);
}

void sub_82B5A648_Hook(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO("sub_82B5A648 (Present Wrapper) called!");
  auto it = g_originalFunctions.find(0x82B5A648);
  if (it != g_originalFunctions.end()) {
    it->second(ctx, base);
  }
}

// PHASE D.3: Complete replacement for sub_8293F460
// This function calls sub_82A60208 (CreateDevice) then sub_82A60600 (path parser).
// Since the hook system can't intercept direct BL calls in generated PPC code,
// we must replace the entire function to skip the path parser.
void sub_8293F460_Hook(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO(">>> sub_8293F460_Hook: FULL REPLACEMENT (skipping path parser)");
  
  // The caller provides r3 = the PresentationParameters struct on the stack
  // We need to call sub_82A60208 with the right setup
  
  // Load singleton pointer from global (r31 = *(0x82EC3000 + 13836))
  // 0x82EC3000 = -2096693248(lis) = 0x82EC3000
  // 13836 = 0x360C
  uint32_t singleton = *(rex::be<uint32_t>*)(base + 0x82EC360C);
  if (singleton == 0) {
    // Call sub_82883C70 to initialize if needed
    auto initFn = rex::Runtime::instance()->memory()->GetFunction(0x82883C70);
    if (initFn) {
      initFn(ctx, base);
    }
    singleton = *(rex::be<uint32_t>*)(base + 0x82EC360C);
  }
  REXLOG_INFO("  singleton=0x{:08X}", singleton);
  
  // Call sub_82966598 (some init function) with r3=*(singleton+4)
  uint32_t val_plus4 = *(rex::be<uint32_t>*)(base + singleton + 4);
  ctx.r3.u64 = (uint64_t)val_plus4;
  auto fn966598 = rex::Runtime::instance()->memory()->GetFunction(0x82966598);
  if (fn966598) {
    fn966598(ctx, base);
  }
  
  // Set up the presentation parameters struct on the stack
  // We don't need to emulate the full stack setup because our sub_82A60208 hook
  // doesn't read the stack params — it uses ctx.r3 as the caller_struct
  
  // Set r3 = stack struct pointer (we'll use the caller's original r3)
  // The original code builds a struct at r1+80 with format, resolution, etc.
  // Our sub_82A60208 hook reads from ctx.r3.u32 (caller_struct)
  // We need to prepare a minimal struct in memory
  
  auto runtime = rex::Runtime::instance();
  uint32_t paramStruct = runtime->memory()->SystemHeapAlloc(256);
  runtime->memory()->Zero(paramStruct, 256);
  
  // Fill in the fields the original sub_8293F460 would set at the stack struct:
  // +0 = 6 (format), +4 = 256 (backbufferWidth?), +8 = D3D interface
  // +12..+36 = params that get copied into DeviceStruct
  *(rex::be<uint32_t>*)(base + paramStruct + 0) = 6;     // Format
  *(rex::be<uint32_t>*)(base + paramStruct + 4) = 256;   // BackbufferWidth/count
  *(rex::be<uint32_t>*)(base + paramStruct + 8) = *(rex::be<uint32_t>*)(base + singleton + 4); // D3D device
  
  // +12..+36 = the data that gets copied to DeviceStruct+8..+28
  // Read from the original game memory for sane defaults
  // +20 = 2 (multiSample?), +24 = -1 (swap interval), +32 = path string ptr
  *(rex::be<uint32_t>*)(base + paramStruct + 12) = 0;
  *(rex::be<uint32_t>*)(base + paramStruct + 16) = 0;
  *(rex::be<uint32_t>*)(base + paramStruct + 20) = 2;           // MultiSampleType
  *(rex::be<uint32_t>*)(base + paramStruct + 24) = 0xFFFFFFFF;  // SwapInterval
  *(rex::be<uint32_t>*)(base + paramStruct + 28) = 0;
  *(rex::be<uint32_t>*)(base + paramStruct + 32) = 0x82060A2B;  // Resource path pointer
  
  ctx.r3.u64 = (uint64_t)paramStruct;
  
  // Call our hooked sub_82A60208
  REXLOG_INFO("  Calling sub_82A60208 with paramStruct=0x{:08X}", paramStruct);
  sub_82A60208(ctx, base);
  uint32_t deviceStruct = ctx.r3.u32;
  REXLOG_INFO("  sub_82A60208 returned deviceStruct=0x{:08X}", deviceStruct);
  
  // The original does two copy loops here, but since we set up the DeviceStruct
  // properly in sub_82A60208, and we're skipping sub_82A60600 anyway, 
  // we just need to make sure the data is consistent.
  
  // Set DeviceStruct+44 to a search path pointer (original: addi r11,r4,-4)
  // r4 in the original was a fixed data address: -2112487424 + 13024 = 0x82930000 + 0x32E0 = 0x829332E0
  uint32_t searchPathData = 0x829332E0;
  *(rex::be<uint32_t>*)(base + deviceStruct + 44) = searchPathData - 4;
  
  // SKIP sub_82A60600 (path parser) - this is the whole point of this hook!
  REXLOG_INFO("  SKIPPING sub_82A60600 (path parser) to prevent crash/infinite loop");
  
  // Return r3=0 (success)
  ctx.r3.u64 = 0;
  REXLOG_INFO("<<< sub_8293F460_Hook DONE");
}

// PHASE D.3: Stub the path parser that crashes on our fake device data
void sub_82A60600_Hook(PPCContext& ctx, uint8_t* base) {
  REXLOG_INFO("sub_82A60600 (PathParser) STUBBED - skipping path parsing to avoid crash");
  // Don't call original - the path parser crashes because our fake device
  // has fabricated string pointers that don't contain valid semicolon-delimited paths.
  // The game's resource loading works via VFS, so these search paths aren't needed.
}


// PHASE D.5: Native C++ replacement for sub_82A603F0
// Creates a 192-byte resource registration object, initializes critical sections,
// semaphores, linked lists, and links into the device struct.
// Replaces VTable[1] Allocate(192) call with direct SystemHeapAlloc.
void sub_82A603F0(PPCContext& ctx, uint8_t* base) {
  uint32_t dev = ctx.r3.u32;    // device struct
  uint32_t owner = ctx.r4.u32;  // owner/parent pointer
  uint32_t flags = ctx.r5.u32;  // flags (bit 0 = async mode)
  
  REXLOG_INFO("sub_82A603F0: dev=0x{:08X}, owner=0x{:08X}, flags={}", dev, owner, flags);
  
  auto runtime = rex::Runtime::instance();
  auto memory = runtime->memory();
  
  // Enter critical section at dev+112
  uint32_t cs_addr = dev + 112;
  auto enterCS = memory->GetFunction(0x82A17AA0);
  if (enterCS) {
    ctx.r3.u64 = (uint64_t)cs_addr;
    ctx.r4.s64 = -2111504384 + 2360;  // caller ID
    enterCS(ctx, base);
  }
  
  // Allocate 192 bytes
  uint32_t obj = memory->SystemHeapAlloc(192);
  memory->Zero(obj, 192);
  
  REXLOG_INFO("  Allocated resource obj=0x{:08X} (192 bytes)", obj);
  
  if (obj == 0) {
    ctx.r3.u64 = (uint64_t)cs_addr;
    auto leaveCS = memory->GetFunction(0x82F71EBC);
    if (leaveCS) leaveCS(ctx, base);
    ctx.r3.u64 = 0;
    return;
  }
  
  // Initialize fields
  *(uint8_t*)(base + obj + 8) = 0;
  *(uint8_t*)(base + obj + 9) = 0;
  *(uint8_t*)(base + obj + 10) = (flags & 1);
  
  // Self-referencing linked list at +12/+16
  uint32_t list_node = obj + 12;
  *(rex::be<uint32_t>*)(base + obj + 16) = list_node;
  *(rex::be<uint32_t>*)(base + obj + 12) = list_node;
  
  // Init critical section at obj+24
  auto initCS = memory->GetFunction(0x82A17A38);
  if (initCS) {
    ctx.r3.u64 = (uint64_t)(obj + 24);
    ctx.r4.s64 = 1;
    initCS(ctx, base);
  }
  
  // Init semaphore at obj+64
  auto initSem = memory->GetFunction(0x82A17520);
  if (initSem) {
    ctx.r3.u64 = (uint64_t)(obj + 64);
    ctx.r4.s64 = 1;
    initSem(ctx, base);
  }
  
  *(rex::be<uint32_t>*)(base + obj + 152) = 0;
  
  // Init structure at obj+156
  auto initFn3 = memory->GetFunction(0x82A17F38);
  if (initFn3) {
    ctx.r3.u64 = (uint64_t)(obj + 156);
    ctx.r4.s64 = 1;
    initFn3(ctx, base);
  }
  
  // Store owner and zero fields
  *(rex::be<uint32_t>*)(base + obj + 176) = owner;
  *(rex::be<uint32_t>*)(base + obj + 172) = 0;
  *(rex::be<uint64_t>*)(base + obj + 184) = 0;
  *(rex::be<uint32_t>*)(base + obj + 0) = 0;
  *(rex::be<uint32_t>*)(base + obj + 4) = 0;
  
  // If async, start resource thread
  if (flags & 1) {
    auto startThread = memory->GetFunction(0x82A60D70);
    if (startThread) {
      ctx.r3.u64 = (uint64_t)obj;
      startThread(ctx, base);
    }
  }
  
  // Link into device struct linked list
  uint32_t dev_global = *(rex::be<uint32_t>*)(base + 0x830286B0);
  uint32_t dev_next = *(rex::be<uint32_t>*)(base + dev_global + 4);
  
  *(rex::be<uint32_t>*)(base + obj + 0) = dev_global;
  *(rex::be<uint32_t>*)(base + obj + 4) = dev_next;
  *(rex::be<uint32_t>*)(base + dev_global + 4) = obj;
  
  if (dev_next != 0) {
    *(rex::be<uint32_t>*)(base + dev_next + 0) = obj;
  }
  
  // Leave critical section
  ctx.r3.u64 = (uint64_t)cs_addr;
  auto leaveCS = memory->GetFunction(0x82F71EBC);
  if (leaveCS) leaveCS(ctx, base);
  
  REXLOG_INFO("  sub_82A603F0 returning obj=0x{:08X}", obj);
  ctx.r3.u64 = (uint64_t)obj;
}


// PHASE D.3: Hook VdSetGraphicsInterruptCallback to trace/store the callback
void VdSetGraphicsInterruptCallback_Hook(PPCContext& ctx, uint8_t* base) {
  g_gfxInterruptCallback = ctx.r3.u32;
  g_gfxInterruptContext = ctx.r4.u32;
  REXLOG_INFO("VdSetGraphicsInterruptCallback: callback=0x{:08X}, context=0x{:08X}",
              g_gfxInterruptCallback, g_gfxInterruptContext);
  // Call original so ReXGlue's GPU system can handle it
  auto it = g_originalFunctions.find(0x82F721CC);
  if (it != g_originalFunctions.end()) {
    it->second(ctx, base);
  }
}

void sub_82F273F0(PPCContext& ctx, uint8_t* base) {
  uint32_t dest = ctx.r3.u32;
  uint32_t val = ctx.r4.u8;
  uint32_t size = ctx.r5.u32;
  
  if (size > 128 * 1024 * 1024) { // 128MB fail-safe
      REXLOG_ERROR("CRITICAL: Suppressed catastrophic memset (size={}) at 0x{:08X}", size, dest);
      ctx.r3.u64 = (uint64_t)dest; // memset returns dest
      return;
  }
  
  if (dest < 0x20000000 || dest >= 0xA0000000) {
      uint32_t lr = (uint32_t)ctx.lr;
      REXLOG_ERROR("CRITICAL: memset to invalid address 0x{:08X} (size={}), caller LR=0x{:08X}!", dest, size, lr);
      ctx.r3.u64 = (uint64_t)dest;
      return; 
  }

  // Only log memsets larger than 1KB to reduce noise
  if (size > 1024) {
      REXLOG_INFO("memset: dest=0x{:08X}, val={}, size={}", dest, val, size);
  }

  auto it = g_originalFunctions.find(0x82F273F0);
  if (it != g_originalFunctions.end()) {
      it->second(ctx, base);
  } else {
      // Fallback: manually implement simple memset if original not found yet
      for (uint32_t i = 0; i < size; ++i) {
          *(base + dest + i) = (uint8_t)val;
      }
  }
}

void NtWaitForSingleObjectEx_Hook(PPCContext& ctx, uint8_t* base) {
  uint32_t handle = ctx.r3.u32;
  REXLOG_INFO("NtWaitForSingleObjectEx: handle=0x{:08X}", handle);
  auto it = g_originalFunctions.find(0x82F7249C);
  if (it != g_originalFunctions.end()) it->second(ctx, base);
}

void KeWaitForSingleObject_Hook(PPCContext& ctx, uint8_t* base) {
  uint32_t object_ptr = ctx.r3.u32;
  REXLOG_INFO("KeWaitForSingleObject: object_ptr=0x{:08X}", object_ptr);
  auto it = g_originalFunctions.find(0x82F722CC);
  if (it != g_originalFunctions.end()) it->second(ctx, base);
}

void KeWaitForMultipleObjects_Hook(PPCContext& ctx, uint8_t* base) {
  uint32_t count = ctx.r3.u32;
  REXLOG_INFO("KeWaitForMultipleObjects: count={}", count);
  auto it = g_originalFunctions.find(0x82F72C5C);
  if (it != g_originalFunctions.end()) it->second(ctx, base);
}

// PHASE D.3: Hook sub_826B57D0 just to see if 0x830186B0 is null at entry
void sub_826B57D0_Hook(PPCContext& ctx, uint8_t* base) {
  uint32_t val = *(rex::be<uint32_t>*)(base + 0x830186B0);
  REXLOG_INFO(">>> sub_826B57D0 ENTRANCE. *(0x830186B0) = 0x{:08X}", val);
  
  // Call original
  auto it = g_originalFunctions.find(0x826B57D0);
  if (it != g_originalFunctions.end()) {
    it->second(ctx, base);
  }
}

}

namespace skate3::gpu {

void VideoHooks::Initialize() {
  REXLOG_INFO("skate3::gpu: Video hooks initializing...");
  
  // Kernel sync tracing
  HookFunction(0x82F7249C, &::NtWaitForSingleObjectEx_Hook);
  HookFunction(0x82F722CC, &::KeWaitForSingleObject_Hook);
  HookFunction(0x82F72C5C, &::KeWaitForMultipleObjects_Hook);
  
  // GPU/Present hooks
  HookFunction(0x82F7214C, &::VdSwap_Hook);
  HookFunction(0x82B5A648, &::sub_82B5A648_Hook);
  HookFunction(0x82F721CC, &::VdSetGraphicsInterruptCallback_Hook);
  
  // D3D Device creation + path parser stubs
  HookFunction(0x82A60208, &::sub_82A60208);
  HookFunction(0x82A603F0, &::sub_82A603F0);  // PHASE D.5: resource registration (strong symbol)
  HookFunction(0x82A60600, &::sub_82A60600_Hook);  // PHASE D.3: stub path parser
  HookFunction(0x8293F460, &::sub_8293F460_Hook);  // PHASE D.3: full caller replacement
  HookFunction(0x826B57D0, &::sub_826B57D0_Hook);  // PHASE D.3: trace pointer at entry
  
  // Memory safety
  HookFunction(0x82F273F0, &::sub_82F273F0);
  
  REXLOG_INFO("skate3::gpu: Video hooks fully initialized ({} hooks active).",
              g_originalFunctions.size());
}

void VideoHooks::HookFunction(uint32_t guest_addr, PPCFunc* hook_func) {
  auto runtime = rex::Runtime::instance();
  if (!runtime) return;

  auto memory = runtime->memory();
  if (memory->HasFunctionTable()) {
    PPCFunc* original = memory->GetFunction(guest_addr);
    if (original && original != hook_func) {
      g_originalFunctions[guest_addr] = original;
    }
    memory->SetFunction(guest_addr, hook_func);
    REXLOG_INFO("Hooked function at 0x{:08X}", guest_addr);
  } else {
    REXLOG_WARN("Cannot hook 0x{:08X}: Function table not initialized", guest_addr);
  }
}


void VideoHooks::Guest_Swap(PPCContext& ctx, uint8_t* base) {
  // Moved to extern "C" block above
}

void VideoHooks::Guest_CreateDevice_Stub(PPCContext& ctx, uint8_t* base) {
  // Moved to extern "C" block above
}

}  // namespace skate3::gpu
