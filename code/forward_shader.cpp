#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_gpu_shader_int64 : enable

#define PACK_VERTICES 1

#include "shader_light_types.cpp"
#include "shader_blinn_phong_lighting.cpp"

layout(set = 0, binding = 0) uniform scene_buffer
{
    vec3 CameraPos;
    uint Pad;
    mat4 WVPTransform;
    mat4 WTransform;
} SceneBuffer;

#if VERTEX_SHADER

layout(location = 0) in vec4 InPackedPosNormal;

layout(location = 0) out vec3 OutWorldPos;
layout(location = 1) out vec3 OutWorldNormal;
layout(location = 2) out vec2 OutUv;

void main()
{
#if PACK_VERTICES
    // NOTE: Unpack the position
    vec3 Pos;
    {
        int64_t PackedPos = packInt2x32(floatBitsToInt(InPackedPosNormal.xy));
        uint PosX = uint(PackedPos >> 0u) & 0x1FFFFF;
        uint PosY = uint(PackedPos >> 21u) & 0x1FFFFF;
        uint PosZ = uint(PackedPos >> 42u) & 0x0FFFFF;

        PosX = PosX << 11u;
        PosY = PosY << 11u;
        PosZ = PosZ << 12u;

        int PosXI32 = int(PosX);
        int PosYI32 = int(PosY);
        int PosZI32 = int(PosZ);

#define I32_MIN -2147483648
        Pos = vec3(PosXI32, PosYI32, PosZI32) / (-I32_MIN);
    }

    // NOTE: Unpack Normal
    vec3 Normal = vec3(InPackedPosNormal.z, InPackedPosNormal.w, 0);
    Normal.z = sqrt(1.0f - clamp(Normal.x*Normal.x - Normal.y*Normal.y, 0, 1));
#else
    vec3 Pos = InPackedPosNormal.xyz;
    vec3 Normal = vec3(0);
#endif
    
    gl_Position = SceneBuffer.WVPTransform * vec4(Pos, 1);
    OutWorldPos = (SceneBuffer.WTransform * vec4(Pos, 1)).xyz;
    OutWorldNormal = (SceneBuffer.WTransform * vec4(Normal, 0)).xyz;
    OutUv = vec2(0);
}

#endif

#if FRAGMENT_SHADER

layout(location = 0) in vec3 InWorldPos;
layout(location = 1) in vec3 InWorldNormal;
layout(location = 2) in vec2 InUv;

layout(location = 0) out vec4 OutColor;

void main()
{
    vec3 CameraPos = vec3(0, 0, 0);

    // TODO: Proper texture mapping
#if PACK_VERTICES
    vec4 TexelColor = vec4(1);
#else
    vec4 TexelColor = vec4(1);
#endif
    
    vec3 SurfacePos = InWorldPos;
    vec3 SurfaceNormal = normalize(InWorldNormal);
    vec3 SurfaceColor = TexelColor.rgb;
    vec3 View = normalize(CameraPos - SurfacePos);
    vec3 Color = vec3(0);

    // NOTE: Calculate lighting for point lights
    //for (int i = 0; i < SceneBuffer.NumPointLights; ++i)
    {
        //point_light CurrLight = PointLights[i];
        //vec3 LightDir = normalize(SurfacePos - CurrLight.Pos);
        //Color += BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, 32, LightDir, PointLightAttenuate(SurfacePos, CurrLight));
    }

    // NOTE: Calculate lighting for directional lights
    {
        directional_light DirLight;
        DirLight.Color = vec3(1);
        DirLight.Dir = normalize(vec3(1, 1, 0));
        DirLight.AmbientLight = vec3(0.4);
        Color += BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, 32, DirLight.Dir, DirLight.Color);
        Color += DirLight.AmbientLight * SurfaceColor;
    }

    OutColor = vec4(Color, 1);
}

#endif
