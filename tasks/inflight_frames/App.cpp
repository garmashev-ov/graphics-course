#include "App.hpp"
#include "etna/Buffer.hpp"
#include "etna/DescriptorSet.hpp"
#include "etna/Window.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <etna/BlockingTransferHelper.hpp>

#include <utility>
#include <vulkan/vulkan_enums.hpp>
#include <stb_image.h>


App::App()
  : resolution{1280, 720}
  , useVsync{false}
{

  {
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = NUM_FRAMES_IN_FLIGHT,
    });
  }

  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  {
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    resolution = {w, h};
  }

  commandManager = etna::get_context().createPerFrameCmdMgr();

  generatedTextureImage = etna::get_context().createImage(
    {.extent = vk::Extent3D{resolution.x, resolution.y, 1},
     .name = "texture",
     .format = vk::Format::eB8G8R8A8Srgb,
     .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});


  defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{.name = "default_sampler"});

  etna::create_program(
    "inflight_frames",
    {INFLIGHT_FRAMES_SHADERS_ROOT "toy.frag.spv", INFLIGHT_FRAMES_SHADERS_ROOT "toy.vert.spv"});
  graphicsPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "inflight_frames",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {.colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb}}});

  etna::create_program(
    "texture",
    {INFLIGHT_FRAMES_SHADERS_ROOT "texture.frag.spv", INFLIGHT_FRAMES_SHADERS_ROOT "toy.vert.spv"});
  texturePipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "texture",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {.colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb}}});

  for (size_t i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
  {
    constants[i] = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(PushConstants),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
      .name = "constants" + std::to_string(i)});
  }

  {
    int height, width, channels;
    unsigned char* textureData = stbi_load(
      GRAPHICS_COURSE_RESOURCES_ROOT "/textures/test_tex_1.png", &width, &height, &channels, 4);
    assert(textureData != nullptr);

    loadedTextureImage1 = etna::get_context().createImage({
      .extent = vk::Extent3D{(uint32_t)width, (uint32_t)height, 1},
      .name = "loaded_texture",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
    });

    etna::BlockingTransferHelper transferHelper({.stagingSize = VkDeviceSize(width * height * 4)});

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = etna::get_context().createOneShotCmdMgr();

    transferHelper.uploadImage(
      *oneShotCmdMgr,
      loadedTextureImage1,
      0,
      0,
      std::span<std::byte>(reinterpret_cast<std::byte*>(textureData), width * height * 4));

    stbi_image_free(textureData);
  }

  {
    int height, width, channels;
    unsigned char* textureData = stbi_load(
      GRAPHICS_COURSE_RESOURCES_ROOT "/textures/texture1.bmp", &width, &height, &channels, 4);
    assert(textureData != nullptr);

    loadedTextureImage2 = etna::get_context().createImage({
      .extent = vk::Extent3D{(uint32_t)width, (uint32_t)height, 1},
      .name = "loaded_texture2",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
    });

    etna::BlockingTransferHelper transferHelper({.stagingSize = VkDeviceSize(width * height * 4)});

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = etna::get_context().createOneShotCmdMgr();

    transferHelper.uploadImage(
      *oneShotCmdMgr,
      loadedTextureImage2,
      0,
      0,
      std::span<std::byte>(reinterpret_cast<std::byte*>(textureData), width * height * 4));

    stbi_image_free(textureData);
  }

  mouse_pos = glm::vec2(resolution / 2u);
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    ZoneScopedN("frame");

    {
      ZoneScopedN("windowing poll");
      windowing.poll();
    }

    drawFrame();

    FrameMark;
  }

  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  auto currentCmdBuf = commandManager->acquireNext();

  etna::begin_frame();

  auto nextSwapchainImage = vkWindow->acquireNext();

  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      ETNA_PROFILE_GPU(currentCmdBuf, "frame_render");

      {
        ZoneScopedN("sleep");
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
      }

      etna::set_state(
        currentCmdBuf,
        generatedTextureImage.get(),
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);

      if (!is_textures_loaded)
      {
        etna::set_state(
          currentCmdBuf,
          generatedTextureImage.get(),
          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          vk::AccessFlagBits2::eColorAttachmentWrite,
          vk::ImageLayout::eColorAttachmentOptimal,
          vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(currentCmdBuf);

        {
          etna::RenderTargetState state{
            currentCmdBuf,
            {{}, {resolution.x, resolution.y}},
            {{generatedTextureImage.get(), generatedTextureImage.getView({})}},
            {}};

          currentCmdBuf.bindPipeline(
            vk::PipelineBindPoint::eGraphics, texturePipeline.getVkPipeline());

          currentCmdBuf.pushConstants(
            texturePipeline.getVkPipelineLayout(),
            vk::ShaderStageFlagBits::eFragment,
            0,
            std::max(sizeof(resolution), 16ul),
            &resolution);

          currentCmdBuf.draw(3, 1, 0, 0);
        }
        is_textures_loaded = true;
      }

      etna::set_state(
        currentCmdBuf,
        generatedTextureImage.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);

      {
        etna::RenderTargetState state{
          currentCmdBuf, {{}, {resolution.x, resolution.y}}, {{backbuffer, backbufferView}}, {}};

        auto shaderInfo = etna::get_shader_program("inflight_frames");

        auto set = etna::create_descriptor_set(
          shaderInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {
            etna::Binding{
              0,
              generatedTextureImage.genBinding(
                defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{
              1,
              loadedTextureImage1.genBinding(
                defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{
              2,
              loadedTextureImage2.genBinding(
                defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{3, constants[constants_idx].genBinding()},
          });

        currentCmdBuf.bindPipeline(
          vk::PipelineBindPoint::eGraphics, graphicsPipeline.getVkPipeline());

        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          graphicsPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});


        PushConstants pushConstants;

        pushConstants.time = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now() - start_time)
                               .count() /
          1000.f;

        pushConstants.resolution = resolution;

        if (osWindow.get()->mouse[MouseButton::mbLeft] == ButtonState::High)
        {
          mouse_pos = osWindow.get()->mouse.freePos;
        }
        pushConstants.mouse_pos = mouse_pos;

        std::byte* constantsData = constants[constants_idx].map();
        std::memcpy(constantsData, &pushConstants, sizeof(pushConstants));
        constants[constants_idx].unmap();
        constants_idx = (constants_idx + 1) % NUM_FRAMES_IN_FLIGHT;

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
    }

    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();


  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}