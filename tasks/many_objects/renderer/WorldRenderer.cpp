#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <glm/ext.hpp>

#include <bitset>

const std::size_t INST_NUM = 4096; //max 4096

WorldRenderer::WorldRenderer()
    : sceneMgr{ std::make_unique<SceneManager>() }
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  instMatBuffer = ctx.createBuffer({
    .size = INST_NUM * sizeof(glm::mat4x4),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
  });
  instMatBuffer.map();
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectScene(path);
  //auto size = sceneMgr->getRenderElements().size();
  //instCount.assign(size, 0);
}

void WorldRenderer::loadShaders()
{
#if 0
  etna::create_program(
    "static_mesh_material",
    {MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
#else
  etna::create_program(
    "static_mesh_material",
    {MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh_baked.vert.spv"});
  etna::create_program("static_mesh", {MANY_OBJECTS_RENDERER_SHADERS_ROOT "static_mesh_baked.vert.spv"});
#endif
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  staticMeshPipeline = {};
  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
    "static_mesh_material",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
}

void WorldRenderer::debugInput(const Keyboard&) {}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
  }
}

static char sgn(bool isNeg) {
    return 1 - isNeg * 2;
}

bool isDraw(const std::pair<glm::vec3, glm::vec3>& bounds, const glm::mat4x4& globalTransform)
{
  glm::vec3 pos = bounds.first;

  for (int i = 0; i < 8; ++i)
  {
      glm::vec4 vertPos;
      std::bitset<3> search(i);
      vertPos = glm::vec4(pos + glm::vec3(bounds.second.x * sgn(search[0]), bounds.second.y * sgn(search[1]), bounds.second.z * sgn(search[2])), 1.f);

      vertPos = globalTransform * vertPos;

      float w = abs(vertPos.w * 0.75f); // mul 1.1
      if (abs(vertPos.x ) < w && abs(vertPos.y) < w)
        return true;
  }
  return false;
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout)
{
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  pushConst2M.projView = glob_tm;

  auto instanceMeshes = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();

  auto meshes = sceneMgr->getMeshes();
  auto relems = sceneMgr->getRenderElements();
  auto boundBoxes = sceneMgr->getBoundBoxes();

  glm::mat4x4* matrices = reinterpret_cast<glm::mat4x4*>(instMatBuffer.data());

  std::vector<uint32_t> instCount{ 0 };
  std::memset(instCount.data(), 0, instCount.size() * sizeof(instCount[0]));
  for (std::size_t instIdx = 0; instIdx < instanceMeshes.size(); ++instIdx)
  {
    pushConst2M.model = instanceMatrices[instIdx];

    const auto meshIdx = instanceMeshes[instIdx];
    const auto& instanceMatrix = instanceMatrices[instIdx ];
    for (std::size_t j = 0; j < meshes[meshIdx].relemCount; ++j)
    {
      const auto relemIdx = meshes[meshIdx].firstRelem + j;

      if (!isDraw(boundBoxes[relemIdx], glob_tm * instanceMatrix))
        continue;
      
      instCount[relemIdx]++;
      *matrices = instanceMatrix;
      ++matrices;
    }
  }

  {
      auto staticMesh = etna::get_shader_program("static_mesh");

      auto set = etna::create_descriptor_set(
        staticMesh.getDescriptorLayoutId(0),
        cmd_buf,
        {etna::Binding{0, instMatBuffer.genBinding()}}
      );

      vk::DescriptorSet vkSet = set.getVkSet();
      cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, 
          pipeline_layout, 
          0, 
          1, 
          &vkSet, 
          0, 
          nullptr
      );
  }
  

  std::uint32_t firstInstance = 0;
  for (std::size_t j = 0; j < relems.size(); ++j)
  {
    std::uint32_t instanceCount = instCount[j];
    if (instanceCount != 0)
    {
      const auto& relem = relems[j];
      cmd_buf.pushConstants<PushConstants>(
        pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {pushConst2M});
      cmd_buf.drawIndexed(relem.indexCount, instanceCount, relem.indexOffset, relem.vertexOffset, firstInstance);
      firstInstance += instanceCount;
    }
  }
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = target_image, .view = target_image_view}},
      {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
    renderScene(cmd_buf, worldViewProj, staticMeshPipeline.getVkPipelineLayout());
  }
}
