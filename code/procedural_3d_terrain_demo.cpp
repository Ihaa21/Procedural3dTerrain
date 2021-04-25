
#include "procedural_3d_terrain_demo.h"
#include "transvoxel.cpp"

inline void DemoWindowResize(u32 Width, u32 Height)
{
    b32 ReCreate = DemoState->RenderTargetArena.Used != 0;
    DemoState->WindowResized = true;
    VkArenaClear(&DemoState->RenderTargetArena);

    if (ReCreate)
    {
        RenderTargetDestroy(&DemoState->RenderTarget);
    }

    RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                              &DemoState->ColorImage, &DemoState->ColorEntry);
    RenderTargetEntryReCreate(&DemoState->RenderTargetArena, Width, Height, VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                              &DemoState->DepthImage, &DemoState->DepthEntry);

    if (ReCreate)
    {
        RenderTargetUpdateEntries(&DemoState->TempArena, &DemoState->RenderTarget);
    }

    VkDescriptorImageWrite(&RenderState->DescriptorManager, DemoState->CopyToSwapDesc, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           DemoState->ColorEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
}

//
// NOTE: Demo Code
//

inline void DemoAllocGlobals(linear_arena* Arena)
{
    // IMPORTANT: These are always the top of the program memory
    DemoState = PushStruct(Arena, demo_state);
    RenderState = PushStruct(Arena, render_state);
}

DEMO_INIT(Init)
{
    // NOTE: Init Memory
    {
        linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
        DemoAllocGlobals(&Arena);
        *DemoState = {};
        *RenderState = {};
        DemoState->Arena = Arena;
        DemoState->TempArena = LinearSubArena(&DemoState->Arena, MegaBytes(10));
    }

    // NOTE: Init Vulkan
    {
        {
            const char* DeviceExtensions[] =
            {
                "VK_EXT_shader_viewport_index_layer",
                "VK_KHR_shader_atomic_int64",
                "VK_EXT_shader_subgroup_ballot",
            };
            
            render_init_params InitParams = {};
            InitParams.ValidationEnabled = true;
            //InitParams.PresentMode = VK_PRESENT_MODE_FIFO_KHR;
            InitParams.WindowWidth = WindowWidth;
            InitParams.WindowHeight = WindowHeight;
            InitParams.GpuLocalSize = MegaBytes(3000);
            InitParams.DeviceExtensionCount = ArrayCount(DeviceExtensions);
            InitParams.DeviceExtensions = DeviceExtensions;
            VkInit(VulkanLib, hInstance, WindowHandle, &DemoState->Arena, &DemoState->TempArena, InitParams);
        }
    }
    
    // NOTE: Create samplers
    DemoState->PointSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->LinearSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
    DemoState->AnisoSampler = VkSamplerMipMapCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 16.0f,
                                                    VK_SAMPLER_MIPMAP_MODE_LINEAR, 0, 0, 5);    
        
    // NOTE: Init render target entries
    DemoState->SwapChainEntry = RenderTargetSwapChainEntryCreate(RenderState->WindowWidth, RenderState->WindowHeight,
                                                                 RenderState->SwapChainFormat);

    // NOTE: Copy To Swap RT
    {
        render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, RenderState->WindowWidth,
                                                                 RenderState->WindowHeight);
        RenderTargetAddTarget(&Builder, &DemoState->SwapChainEntry, VkClearColorCreate(0, 0, 0, 1));
                            
        vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

        u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, RenderState->SwapChainFormat, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
        VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderPassSubPassEnd(&RpBuilder);

        DemoState->CopyToSwapTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
        DemoState->CopyToSwapDesc = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, RenderState->CopyImageDescLayout);
        DemoState->CopyToSwapPipeline = FullScreenCopyImageCreate(DemoState->CopyToSwapTarget.RenderPass, 0);
    }

    // NOTE: Init camera
    {
        DemoState->Camera = CameraFpsCreate(V3(0, 5, -6), V3(0, 0, 1), true, 1.0f, 0.015f);
        CameraSetPersp(&DemoState->Camera, f32(RenderState->WindowWidth / RenderState->WindowHeight), 69.375f, 0.01f, 1000.0f);
    }

    DemoState->RenderTargetArena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, MegaBytes(32));

    // NOTE: Terrain Data
    {
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->TerrainDescLayout);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        // NOTE: Create PSOs
        VkDescriptorSetLayout Layouts[] =
        {
            DemoState->TerrainDescLayout,
        };
        DemoState->GenerateTerrainPso = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                "shader_generate_3d_terrain.spv", "main", Layouts, ArrayCount(Layouts));
        DemoState->GenerateTrianglesPso = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                  "shader_generate_triangles.spv", "main", Layouts, ArrayCount(Layouts));

        // NOTE: Create Resources
        DemoState->TerrainResX = 256;
        DemoState->TerrainResY = 256;
        DemoState->TerrainResZ = 256;
        DemoState->TerrainPos = V3(0);
        DemoState->TerrainRadius = V3(5.0f);
        
        DemoState->TerrainGlobals = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                   sizeof(terrain_globals));
        DemoState->TerrainDensity = VkImageCreate(RenderState->Device, &RenderState->GpuArena, DemoState->TerrainResX,
                                                  DemoState->TerrainResY, DemoState->TerrainResZ, VK_FORMAT_R16_SFLOAT,
                                                  VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        DemoState->CellClasses = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                sizeof(u32)*256);
        DemoState->RegularCells = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                 sizeof(regular_cell_data)*16);
        DemoState->RegularCellVertices = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                        sizeof(regular_cell_vertices)*256);
        DemoState->IndirectArgBuffer = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                      sizeof(indirect_args));
        DemoState->TerrainTriangles = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                     //sizeof(v4)*15*DemoState->TerrainResX*DemoState->TerrainResY*DemoState->TerrainResZ);
                                                     sizeof(v4)*5*DemoState->TerrainResX*DemoState->TerrainResY*DemoState->TerrainResZ);
        
        DemoState->TerrainDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->TerrainDescLayout);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DemoState->TerrainGlobals);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               DemoState->TerrainDensity.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_GENERAL);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->CellClasses);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RegularCells);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->RegularCellVertices);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->IndirectArgBuffer);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, DemoState->TerrainTriangles);
        
        DemoState->NoiseDim = 16;
        DemoState->NoiseSampler = VkSamplerCreate(RenderState->Device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, 0.0f);
        for (u32 NoiseTextureId = 0; NoiseTextureId < ArrayCount(DemoState->NoiseTextures); ++NoiseTextureId)
        {
            DemoState->NoiseTextures[NoiseTextureId] = VkImageCreate(RenderState->Device, &RenderState->GpuArena, DemoState->NoiseDim,
                                                                     DemoState->NoiseDim, DemoState->NoiseDim, VK_FORMAT_R32_SFLOAT,
                                                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                                                     VK_IMAGE_ASPECT_COLOR_BIT);
            VkDescriptorImageWrite(&RenderState->DescriptorManager, DemoState->TerrainDescriptor, 7, NoiseTextureId,
                                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DemoState->NoiseTextures[NoiseTextureId].View,
                                   DemoState->NoiseSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

    }
    
    // NOTE: Forward Data
    {
        DemoState->RenderWidth = WindowWidth;
        DemoState->RenderHeight = WindowHeight;
        DemoWindowResize(WindowWidth, WindowHeight);

        // NOTE: Create Forward Render Target
        {
            render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, DemoState->RenderWidth, DemoState->RenderHeight);
            RenderTargetAddTarget(&Builder, &DemoState->ColorEntry, VkClearColorCreate(0, 0, 0, 1));
            RenderTargetAddTarget(&Builder, &DemoState->DepthEntry, VkClearDepthStencilCreate(0, 0));
                            
            vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);
            u32 ColorId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->ColorEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, DemoState->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                    VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

            VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
            VkRenderPassColorRefAdd(&RpBuilder, ColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            VkRenderPassSubPassEnd(&RpBuilder);

            DemoState->RenderTarget = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
        }

        // NOTE: Forward Descriptor Data
        {
            {
                vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&DemoState->ForwardDescLayout);
                VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
                VkDescriptorLayoutEnd(RenderState->Device, &Builder);
            }
            
            DemoState->SceneUniforms = VkBufferCreate(RenderState->Device, &RenderState->GpuArena,
                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                      sizeof(scene_globals));
            
            DemoState->ForwardDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, DemoState->ForwardDescLayout);
            VkDescriptorBufferWrite(&RenderState->DescriptorManager, DemoState->ForwardDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DemoState->SceneUniforms);
        }
        
        // NOTE: Create PSO
        {
            vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

            // NOTE: Shaders
            VkPipelineShaderAdd(&Builder, "shader_forward_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
            VkPipelineShaderAdd(&Builder, "shader_forward_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
            // NOTE: Specify input vertex data format
            VkPipelineVertexBindingBegin(&Builder);
            VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(v4));
            VkPipelineVertexBindingEnd(&Builder);

            VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
            VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);
            VkPipelineMsaaStateSet(&Builder, VK_SAMPLE_COUNT_1_BIT, VK_FALSE);
            
            // NOTE: Set the blending state
            VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO,
                                         VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

            VkDescriptorSetLayout DescriptorLayouts[] =
                {
                    DemoState->ForwardDescLayout,
                };
            
            DemoState->ForwardPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                              DemoState->RenderTarget.RenderPass, 0, DescriptorLayouts, ArrayCount(DescriptorLayouts));
        }
    }
    
    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);

    // NOTE: Upload assets
    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);
    {
        // NOTE: Create UI
        UiStateCreate(RenderState->Device, &DemoState->Arena, &DemoState->TempArena, RenderState->LocalMemoryId,
                      &RenderState->DescriptorManager, &RenderState->PipelineManager, &RenderState->Commands,
                      RenderState->SwapChainFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &DemoState->UiState);

        // NOTE: Upload Terrain Globals
        {
            terrain_globals* GpuPtr = VkCommandsPushWriteStruct(&RenderState->Commands, DemoState->TerrainGlobals, terrain_globals,
                                                                BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            
            *GpuPtr = {};
            GpuPtr->Center = DemoState->TerrainPos;
            GpuPtr->Radius = DemoState->TerrainRadius;
            GpuPtr->Resolution = V3(DemoState->TerrainResX, DemoState->TerrainResY, DemoState->TerrainResZ);
        }

        // NOTE: Upload Cell Classes
        {
            u32* GpuPtr = VkCommandsPushWriteArray(&RenderState->Commands, DemoState->CellClasses, u32, 256,
                                                   BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                   BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            for (u32 CellClassId = 0; CellClassId < 256; ++CellClassId)
            {
                GpuPtr[CellClassId] = GlobalRegularCellClasses[CellClassId];
            }
        }

        // NOTE: Upload Regular Cells
        {
            regular_cell_data* GpuPtr = VkCommandsPushWriteArray(&RenderState->Commands, DemoState->RegularCells, regular_cell_data, 16,
                                                                 BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                                 BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            for (u32 RegularCellId = 0; RegularCellId < 16; ++RegularCellId)
            {
                GpuPtr[RegularCellId] = GlobalRegularCellData[RegularCellId];
            }
        }

        // NOTE: Upload Regular Cell Vertices
        {
            u32* GpuPtr = VkCommandsPushWriteArray(&RenderState->Commands, DemoState->RegularCellVertices, u32, 256*12,
                                                   BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                   BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            u32* CurrElement = GpuPtr;
            for (u32 CellClassId = 0; CellClassId < 256; ++CellClassId)
            {
                *CurrElement++ = GlobalRegularVertexData[CellClassId][0];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][1];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][2];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][3];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][4];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][5];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][6];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][7];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][8];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][9];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][10];
                *CurrElement++ = GlobalRegularVertexData[CellClassId][11];
            }
        }

        // NOTE: Upload Noise Textures
        for (u32 NoiseTextureId = 0; NoiseTextureId < ArrayCount(DemoState->NoiseTextures); ++NoiseTextureId)
        {
            f32* GpuPtr = (f32*)VkCommandsPushWriteImage(&RenderState->Commands, DemoState->NoiseTextures[NoiseTextureId].Image,
                                                         DemoState->NoiseDim, DemoState->NoiseDim, DemoState->NoiseDim, sizeof(f32),
                                                         VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                         BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                         BarrierMask(VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));

            for (u32 PixelId = 0; PixelId < DemoState->NoiseDim * DemoState->NoiseDim * DemoState->NoiseDim; ++PixelId)
            {
                // TODO: Write a diff random generator?
                GpuPtr[PixelId] = f32(rand()) / f32(RAND_MAX);
            }
        }
        
        VkCommandsTransferFlush(Commands, RenderState->Device);

        VkBarrierImageAdd(&RenderState->Commands, DemoState->TerrainDensity.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
        VkCommandsBarrierFlush(&RenderState->Commands);
    }
    
    VkCommandsSubmit(Commands, RenderState->Device, RenderState->GraphicsQueue);
}

DEMO_DESTROY(Destroy)
{
}

DEMO_SWAPCHAIN_CHANGE(SwapChainChange)
{
    VkCheckResult(vkDeviceWaitIdle(RenderState->Device));
    VkSwapChainReCreate(&DemoState->TempArena, WindowWidth, WindowHeight, RenderState->PresentMode);

    DemoState->SwapChainEntry.Width = RenderState->WindowWidth;
    DemoState->SwapChainEntry.Height = RenderState->WindowHeight;

    DemoState->Camera.PerspAspectRatio = f32(RenderState->WindowWidth / RenderState->WindowHeight);

    DemoState->RenderWidth = RenderState->WindowWidth;
    DemoState->RenderHeight = RenderState->WindowHeight;
    DemoWindowResize(WindowWidth, WindowHeight);
}

DEMO_CODE_RELOAD(CodeReload)
{
    linear_arena Arena = LinearArenaCreate(ProgramMemory, ProgramMemorySize);
    // IMPORTANT: We are relying on the memory being the same here since we have the same base ptr with the VirtualAlloc so we just need
    // to patch our global pointers here
    DemoAllocGlobals(&Arena);

    VkGetGlobalFunctionPointers(VulkanLib);
    VkGetInstanceFunctionPointers();
    VkGetDeviceFunctionPointers();
}

DEMO_MAIN_LOOP(MainLoop)
{
    u32 ImageIndex;
    VkCheckResult(vkAcquireNextImageKHR(RenderState->Device, RenderState->SwapChain, UINT64_MAX, RenderState->ImageAvailableSemaphore,
                                        VK_NULL_HANDLE, &ImageIndex));
    DemoState->SwapChainEntry.View = RenderState->SwapChainViews[ImageIndex];

    vk_commands* Commands = &RenderState->Commands;
    VkCommandsBegin(Commands, RenderState->Device);

    // NOTE: Update pipelines
    VkPipelineUpdateShaders(RenderState->Device, &RenderState->CpuArena, &RenderState->PipelineManager);

    RenderTargetUpdateEntries(&DemoState->TempArena, &DemoState->CopyToSwapTarget);
    
    // NOTE: Update Ui State
    {
        ui_state* UiState = &DemoState->UiState;
        
        ui_frame_input UiCurrInput = {};
        UiCurrInput.MouseDown = CurrInput->MouseDown;
        UiCurrInput.MousePixelPos = V2(CurrInput->MousePixelPos);
        UiCurrInput.MouseScroll = CurrInput->MouseScroll;
        Copy(CurrInput->KeysDown, UiCurrInput.KeysDown, sizeof(UiCurrInput.KeysDown));
        UiStateBegin(UiState, FrameTime, RenderState->WindowWidth, RenderState->WindowHeight, UiCurrInput);
        local_global v2 PanelPos = V2(100, 800);
        
        UiStateEnd(UiState, &RenderState->DescriptorManager);
    }

    // NOTE: Upload scene data
    {
        if (!(DemoState->UiState.MouseTouchingUi || DemoState->UiState.ProcessedInteraction))
        {
            CameraUpdate(&DemoState->Camera, CurrInput, PrevInput);
        }
        
        // NOTE: Push Scene Globals
        {
            scene_globals* GpuPtr = VkCommandsPushWriteStruct(&RenderState->Commands, DemoState->SceneUniforms, scene_globals,
                                                              BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                              BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            
            *GpuPtr = {};
            GpuPtr->CameraPos = DemoState->Camera.Pos;
            GpuPtr->WTransform = M4Pos(DemoState->TerrainPos) * M4Scale(2.0f*DemoState->TerrainRadius);
            GpuPtr->WVPTransform = CameraGetVP(&DemoState->Camera) * GpuPtr->WTransform;
        }
        
        // NOTE: Upload Indirect Arg Buffer
        {
            indirect_args* GpuPtr = VkCommandsPushWriteStruct(&RenderState->Commands, DemoState->IndirectArgBuffer, indirect_args,
                                                              BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT),
                                                              BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
            
            *GpuPtr = {};
            GpuPtr->NumInstances = 1;
        }

        VkCommandsTransferFlush(&RenderState->Commands, RenderState->Device);
    }
    
    {
        // NOTE: Generate proedural terrain 
        {            
            vk_pipeline* Pipeline = DemoState->GenerateTerrainPso;
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Handle);
            VkDescriptorSet DescriptorSets[] =
                {
                    DemoState->TerrainDescriptor,
                };
            vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Layout, 0,
                                    ArrayCount(DescriptorSets), DescriptorSets, 0, 0);

            u32 DispatchX = CeilU32(f32(DemoState->TerrainResX) / 4.0f);
            u32 DispatchY = CeilU32(f32(DemoState->TerrainResY) / 4.0f);
            u32 DispatchZ = CeilU32(f32(DemoState->TerrainResZ) / 4.0f);
            vkCmdDispatch(Commands->Buffer, DispatchX, DispatchY, DispatchZ);
        }
        
        VkBarrierBufferAdd(&RenderState->Commands, DemoState->IndirectArgBuffer,
                           VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        VkBarrierBufferAdd(&RenderState->Commands, DemoState->TerrainTriangles,
                           VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        VkBarrierImageAdd(&RenderState->Commands, DemoState->TerrainDensity.Image, VK_IMAGE_ASPECT_COLOR_BIT,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL,
                          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_IMAGE_LAYOUT_GENERAL);
        VkCommandsBarrierFlush(&RenderState->Commands);
        
        // NOTE: Generate triangles for terrain
        {
            vk_pipeline* Pipeline = DemoState->GenerateTrianglesPso;
            vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Handle);
            VkDescriptorSet DescriptorSets[] =
                {
                    DemoState->TerrainDescriptor,
                };
            vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Layout, 0,
                                    ArrayCount(DescriptorSets), DescriptorSets, 0, 0);

            u32 DispatchX = CeilU32(f32(DemoState->TerrainResX - 1) / 4.0f);
            u32 DispatchY = CeilU32(f32(DemoState->TerrainResY - 1) / 4.0f);
            u32 DispatchZ = CeilU32(f32(DemoState->TerrainResZ - 1) / 4.0f);
            vkCmdDispatch(Commands->Buffer, DispatchX, DispatchY, DispatchZ);
        }

        VkBarrierBufferAdd(&RenderState->Commands, DemoState->IndirectArgBuffer,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
        VkBarrierBufferAdd(&RenderState->Commands, DemoState->TerrainTriangles,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
        VkCommandsBarrierFlush(&RenderState->Commands);
    }

    // NOTE: Draw Terrain
    RenderTargetPassBegin(&DemoState->RenderTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    {
        vkCmdBindPipeline(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->ForwardPipeline->Handle);
        {
            VkDescriptorSet DescriptorSets[] =
                {
                    DemoState->ForwardDescriptor,
                };
            vkCmdBindDescriptorSets(Commands->Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, DemoState->ForwardPipeline->Layout, 0,
                                    ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
        }

        VkDeviceSize Offset = 0;
        vkCmdBindVertexBuffers(Commands->Buffer, 0, 1, &DemoState->TerrainTriangles, &Offset);
        vkCmdDrawIndirect(Commands->Buffer, DemoState->IndirectArgBuffer, 0, 1, 0);
    }
    RenderTargetPassEnd(Commands);        
    
    RenderTargetPassBegin(&DemoState->CopyToSwapTarget, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    FullScreenPassRender(Commands, DemoState->CopyToSwapPipeline, 1, &DemoState->CopyToSwapDesc);
    RenderTargetPassEnd(Commands);
    UiStateRender(&DemoState->UiState, RenderState->Device, Commands, DemoState->SwapChainEntry.View);

    VkCommandsEnd(Commands, RenderState->Device);
                    
    // NOTE: Render to our window surface
    // NOTE: Tell queue where we render to surface to wait
    VkPipelineStageFlags WaitDstMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo SubmitInfo = {};
    SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitInfo.waitSemaphoreCount = 1;
    SubmitInfo.pWaitSemaphores = &RenderState->ImageAvailableSemaphore;
    SubmitInfo.pWaitDstStageMask = &WaitDstMask;
    SubmitInfo.commandBufferCount = 1;
    SubmitInfo.pCommandBuffers = &Commands->Buffer;
    SubmitInfo.signalSemaphoreCount = 1;
    SubmitInfo.pSignalSemaphores = &RenderState->FinishedRenderingSemaphore;
    VkCheckResult(vkQueueSubmit(RenderState->GraphicsQueue, 1, &SubmitInfo, Commands->Fence));
    
    VkPresentInfoKHR PresentInfo = {};
    PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    PresentInfo.waitSemaphoreCount = 1;
    PresentInfo.pWaitSemaphores = &RenderState->FinishedRenderingSemaphore;
    PresentInfo.swapchainCount = 1;
    PresentInfo.pSwapchains = &RenderState->SwapChain;
    PresentInfo.pImageIndices = &ImageIndex;
    VkResult Result = vkQueuePresentKHR(RenderState->PresentQueue, &PresentInfo);

    switch (Result)
    {
        case VK_SUCCESS:
        {
        } break;

        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_SUBOPTIMAL_KHR:
        {
            // NOTE: Window size changed
            InvalidCodePath;
        } break;

        default:
        {
            InvalidCodePath;
        } break;
    }

    DemoState->WindowResized = false;
}
