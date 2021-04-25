#pragma once

#define VALIDATION 1

#include "framework_vulkan\framework_vulkan.h"

struct regular_cell_vertices
{
    u32 Edges[12];
};

struct regular_cell_data
{
    u32 GeometryCounts;
    u32 VertexIndex[15];
};

struct indirect_args
{
    u32 NumVerticesPerInstance;
    u32 NumInstances;
    u32 StartVertexIndex;
    u32 StartInstanceIndex;
};

struct terrain_globals
{
    v3 Center;
    u32 Pad0;
    v3 Radius;
    u32 Pad1;
    v3 Resolution;
    u32 Pad2;
};

struct scene_globals
{
    v3 CameraPos;
    u32 Pad;
    m4 WVPTransform;
    m4 WTransform;
};

struct demo_state
{
    linear_arena Arena;
    linear_arena TempArena;

    // NOTE: Samplers
    VkSampler PointSampler;
    VkSampler LinearSampler;
    VkSampler AnisoSampler;

    // NOTE: Swap Chain Targets
    b32 WindowResized;
    u32 RenderWidth;
    u32 RenderHeight;
    VkFormat RenderFormat;
    render_target_entry SwapChainEntry;
    render_target CopyToSwapTarget;
    VkDescriptorSet CopyToSwapDesc;
    vk_pipeline* CopyToSwapPipeline;
    
    camera Camera;

    // NOTE: Procedural Terrain Data
    u32 TerrainResX;
    u32 TerrainResY;
    u32 TerrainResZ;
    v3 TerrainPos;
    v3 TerrainRadius;
    
    VkDescriptorSetLayout TerrainDescLayout;
    VkDescriptorSet TerrainDescriptor;
    vk_pipeline* GenerateTerrainPso;
    vk_pipeline* GenerateTrianglesPso;
    VkBuffer TerrainGlobals;
    vk_image TerrainDensity;
    VkBuffer CellClasses;
    VkBuffer RegularCells;
    VkBuffer RegularCellVertices;
    VkBuffer IndirectArgBuffer;
    VkBuffer TerrainTriangles;

    u32 NoiseDim;
    VkSampler NoiseSampler;
    vk_image NoiseTextures[4];

    // NOTE: Render Data
    vk_linear_arena RenderTargetArena;
    VkImage ColorImage;
    render_target_entry ColorEntry;
    VkImage DepthImage;
    render_target_entry DepthEntry;
    render_target RenderTarget;

    // NOTE: Forward Data
    VkDescriptorSetLayout ForwardDescLayout;
    VkDescriptorSet ForwardDescriptor;
    vk_pipeline* ForwardPipeline;
    VkBuffer SceneUniforms;
    
    ui_state UiState;
};

global demo_state* DemoState;
