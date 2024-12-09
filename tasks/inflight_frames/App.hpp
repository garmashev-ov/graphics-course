#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <chrono>


#include "etna/Buffer.hpp"
#include "etna/GraphicsPipeline.hpp"
#include "wsi/OsWindowingManager.hpp"


class App
{
public:
  App();
  ~App();

  void run();

  static constexpr size_t NUM_FRAMES_IN_FLIGHT = 2;

private:
  void drawFrame();

private:
  struct PushConstants
  {
    float time;
    glm::vec2 resolution;
    glm::vec2 mouse_pos;
  };

  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  etna::Image generatedTextureImage;
  etna::Image loadedTextureImage1;
  etna::Image loadedTextureImage2;

  etna::Sampler defaultSampler;

  etna::GraphicsPipeline graphicsPipeline;
  etna::GraphicsPipeline texturePipeline;

  std::chrono::time_point<std::chrono::system_clock> start_time = std::chrono::system_clock::now();
  glm::vec2 mouse_pos;

  // etna::GpuSharedResource<etna::Buffer> constants;
  std::array<etna::Buffer, NUM_FRAMES_IN_FLIGHT> constants;
  size_t constants_idx = 0;

  bool is_textures_loaded = false;
};