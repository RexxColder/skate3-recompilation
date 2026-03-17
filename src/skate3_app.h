// skate3 - ReXGlue Recompiled Project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/logging.h>
#include "gpu_hooks.h"

class Skate3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Skate3App>(new Skate3App(ctx, "skate3",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    REXLOG_INFO("Skate3App: OnPreSetup - Configuring runtime...");
    // Initial GPU hooks setup - currently empty
  }

  void OnPostSetup() override {
    REXLOG_INFO("Skate3App: OnPostSetup - Runtime initialized, window created");
    
    // Initialize video hooks now that the function table is ready
    skate3::gpu::VideoHooks::Initialize();
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    REXLOG_INFO("Skate3App: OnConfigurePaths - Setting up paths...");
    REXLOG_INFO("  game_data_root: {}", paths.game_data_root.string());
    REXLOG_INFO("  user_data_root: {}", paths.user_data_root.string());
  }

  void OnShutdown() override {
    REXLOG_INFO("Skate3App: OnShutdown - Cleaning up...");
  }

  // Override virtual hooks for customization:
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
};
