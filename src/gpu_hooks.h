#pragma once

#include <rex/ppc/context.h>

namespace skate3::gpu {

class VideoHooks {
 public:
  static void Initialize();

  // D3D-equivalent Hooks
  static void Guest_Swap(PPCContext& ctx, uint8_t* base);
  static void Guest_CreateDevice_Stub(PPCContext& ctx, uint8_t* base);
  
 private:
  static void HookFunction(uint32_t guest_addr, PPCFunc* hook_func);
};

}  // namespace skate3::gpu
