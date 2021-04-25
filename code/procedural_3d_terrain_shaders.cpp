#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_gpu_shader_int64 : enable

#define PACK_VERTICES 1

// NOTE: The RegularCellData structure holds information about the triangulation
// used for a single equivalence class in the modified Marching Cubes algorithm,
// described in Section 3.2.

struct regular_cell_vertices
{
    uint Edges[12];
};

struct regular_cell_data
{
    uint GeometryCounts;     // NOTE: High nibble is vertex count, low nibble is triangle count.
    uint VertexIndex[15];    // NOTE: Groups of 3 indexes giving the triangulation.
};
    
uint GetVertexCount(regular_cell_data Data)
{
    return (Data.GeometryCounts >> 4);
}
    
uint GetTriangleCount(regular_cell_data Data)
{
    return (Data.GeometryCounts & 0x0F);
}

struct indirect_args
{
    uint NumVerticesPerInstance;
    uint NumInstances;
    uint StartVertexIndex;
    uint StartInstanceIndex;
};

layout(set = 0, binding = 0) uniform terrain_globals
{
    vec3 Center;
    vec3 Radius;
    vec3 Resolution;
} TerrainGlobals;

layout(set = 0, binding = 1, r16f) uniform image3D TerrainDensity;

// TODO: This can be compressed since each element is 16bits
layout(set = 0, binding = 2, std430) buffer cell_classes
{
    uint CellClasses[256];
};

layout(set = 0, binding = 3, std430) buffer regular_cells
{
    regular_cell_data RegularCells[16];
};

layout(set = 0, binding = 4, std430) buffer regular_cell_vertices_buffer
{
    regular_cell_vertices RegularCellVertices[256];
};

layout(set = 0, binding = 5) buffer indirect_arg_buffer
{
    indirect_args IndirectArgs;
};

layout(set = 0, binding = 6) buffer triangle_list
{
    vec4 TerrainTriangleList[];
};

layout(set = 0, binding = 7) uniform sampler3D NoiseTextures[4];

//=========================================================================================================================================
// NOTE: Generate 3d Terrain
//=========================================================================================================================================

#if GENERATE_3D_TERRAIN

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
void main()
{
    if (gl_GlobalInvocationID.x < TerrainGlobals.Resolution.x &&
        gl_GlobalInvocationID.y < TerrainGlobals.Resolution.y &&
        gl_GlobalInvocationID.z < TerrainGlobals.Resolution.z)
    {
        // NOTE: Remap our thread id to world space position
        vec3 Uv = 2.0f * (gl_GlobalInvocationID / TerrainGlobals.Resolution) - vec3(1.0f);
        vec3 WorldSpacePos = TerrainGlobals.Center + Uv * TerrainGlobals.Radius;

        // NOTE: Generate a density value
        float Density = -WorldSpacePos.y;

        // NOTE: Add noise
#if 1
        //Density += texture(NoiseTextures[0], WorldSpacePos).x;
        Density += texture(NoiseTextures[0], Uv*9.53).x*0.07;
        Density += texture(NoiseTextures[1], Uv*6.03).x*0.13; 
        Density += texture(NoiseTextures[0], Uv*4.03).x*0.25;
        Density += texture(NoiseTextures[1], Uv*1.96).x*0.50;
        Density += texture(NoiseTextures[2], Uv*1.01).x*1.00; 
        Density += texture(NoiseTextures[3], Uv*0.87).x*1.00;
        Density += texture(NoiseTextures[0], Uv*0.54).x*1.00;
        Density += texture(NoiseTextures[1], Uv*0.32).x*1.55; 
#endif

        Density -= 3.5f;
        
        // NOTE: Create floors
        //float HardFloor = 0.7;
        //Density += clamp((HardFloor - Uv.y)*3, 0, 1)*40; 
        
        // NOTE: Write out the density
        imageStore(TerrainDensity, ivec3(gl_GlobalInvocationID), vec4(Density, 0, 0, 0));
    }
}

#endif

//=========================================================================================================================================
// NOTE: Generate Triangles
//=========================================================================================================================================

#if GENERATE_TRIANGLES

vec3 GenerateNormals(ivec3 Origin)
{
    vec3 Gradient;
    Gradient.x = imageLoad(TerrainDensity, Origin + ivec3(1, 0, 0)).x - imageLoad(TerrainDensity, Origin - ivec3(1, 0, 0)).x;
    Gradient.y = imageLoad(TerrainDensity, Origin + ivec3(0, 1, 0)).x - imageLoad(TerrainDensity, Origin - ivec3(0, 1, 0)).x;
    Gradient.z = imageLoad(TerrainDensity, Origin + ivec3(0, 0, 1)).x - imageLoad(TerrainDensity, Origin - ivec3(0, 0, 1)).x;

    vec3 Result = -normalize(Gradient);
    return Result;
}

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
void main()
{
    if (gl_GlobalInvocationID.x < TerrainGlobals.Resolution.x - 1 &&
        gl_GlobalInvocationID.y < TerrainGlobals.Resolution.y - 1 &&
        gl_GlobalInvocationID.z < TerrainGlobals.Resolution.z - 1)
    {
        // NOTE: Find the 8 corner uvs
        ivec3 Corners[8];
        Corners[0] = ivec3(gl_GlobalInvocationID) + ivec3(0, 0, 0);
        Corners[1] = ivec3(gl_GlobalInvocationID) + ivec3(1, 0, 0);
        Corners[2] = ivec3(gl_GlobalInvocationID) + ivec3(0, 1, 0);
        Corners[3] = ivec3(gl_GlobalInvocationID) + ivec3(1, 1, 0);
        Corners[4] = ivec3(gl_GlobalInvocationID) + ivec3(0, 0, 1);
        Corners[5] = ivec3(gl_GlobalInvocationID) + ivec3(1, 0, 1);
        Corners[6] = ivec3(gl_GlobalInvocationID) + ivec3(0, 1, 1);
        Corners[7] = ivec3(gl_GlobalInvocationID) + ivec3(1, 1, 1);

        // NOTE: Sample the density at each corner
        float Densities[8];
        Densities[0] = imageLoad(TerrainDensity, Corners[0]).x;
        Densities[1] = imageLoad(TerrainDensity, Corners[1]).x;
        Densities[2] = imageLoad(TerrainDensity, Corners[2]).x;
        Densities[3] = imageLoad(TerrainDensity, Corners[3]).x;
        Densities[4] = imageLoad(TerrainDensity, Corners[4]).x;
        Densities[5] = imageLoad(TerrainDensity, Corners[5]).x;
        Densities[6] = imageLoad(TerrainDensity, Corners[6]).x;
        Densities[7] = imageLoad(TerrainDensity, Corners[7]).x;

        vec3 CornerNormals[8];
        CornerNormals[0] = GenerateNormals(Corners[0]);
        CornerNormals[1] = GenerateNormals(Corners[1]);
        CornerNormals[2] = GenerateNormals(Corners[2]);
        CornerNormals[3] = GenerateNormals(Corners[3]);
        CornerNormals[4] = GenerateNormals(Corners[4]);
        CornerNormals[5] = GenerateNormals(Corners[5]);
        CornerNormals[6] = GenerateNormals(Corners[6]);
        CornerNormals[7] = GenerateNormals(Corners[7]);
        
        // NOTE: Generate case byte
        uint CaseBit0 = Densities[0] >= 0 ? 0x1 : 0x0;
        uint CaseBit1 = Densities[1] >= 0 ? 0x1 : 0x0;
        uint CaseBit2 = Densities[2] >= 0 ? 0x1 : 0x0;
        uint CaseBit3 = Densities[3] >= 0 ? 0x1 : 0x0;
        uint CaseBit4 = Densities[4] >= 0 ? 0x1 : 0x0;
        uint CaseBit5 = Densities[5] >= 0 ? 0x1 : 0x0;
        uint CaseBit6 = Densities[6] >= 0 ? 0x1 : 0x0;
        uint CaseBit7 = Densities[7] >= 0 ? 0x1 : 0x0;
        uint CaseByte = ((CaseBit0 << 0) | (CaseBit1 << 1) | (CaseBit2 << 2) | (CaseBit3 << 3) |
                         (CaseBit4 << 4) | (CaseBit5 << 5) | (CaseBit6 << 6) | (CaseBit7 << 7));

        // NOTE: Skip cases 0 and 255 since they are empty
        if ((CaseByte ^ ((CaseBit7 >> 7) & 0xFF)) != 0)
        {
            uint RegularCellId = CellClasses[CaseByte];
            regular_cell_data RegularCell = RegularCells[RegularCellId];
            regular_cell_vertices PackedVertices = RegularCellVertices[CaseByte];

            // NOTE: Generate triangle list from loaded cell data
            // TODO: Right now I don't do any vertex reuse just to make sure I generate valid data
#if PACK_VERTICES
            vec2 Vertices[12];
            vec2 Normals[12];
#else
            vec3 Vertices[12];
#endif
            for (uint VertexId = 0; VertexId < GetVertexCount(RegularCell); ++VertexId)
            {
                uint Edge = PackedVertices.Edges[VertexId];
                uint FirstVertexId = Edge & 0xF;
                uint SecondVertexId = (Edge >> 4) & 0xF;

                ivec3 FirstCorner = Corners[FirstVertexId];
                ivec3 SecondCorner = Corners[SecondVertexId];

                // NOTE: Interpolate according to density value
                float FirstDensity = Densities[FirstVertexId];
                float SecondDensity = Densities[SecondVertexId];
                float T = SecondDensity / (SecondDensity - FirstDensity);
                
                vec3 Vertex = mix(vec3(FirstCorner), vec3(SecondCorner), T);
                Vertex = (2.0f * Vertex / TerrainGlobals.Resolution) - vec3(1);

#if PACK_VERTICES
                // NOTE: Pack to fixed point
                {
                    // NOTE: Convert to 32bit fixed point
#define I32_MIN -2147483648
                    Vertex *= -I32_MIN;
                    int PosX = int(Vertex.x + 0.5f);
                    int PosY = int(Vertex.y + 0.5f);
                    int PosZ = int(Vertex.z + 0.5f);

                    // NOTE: Only keep top 21:21:20 bits
                    int64_t PosX64 = PosX;
                    int64_t PosY64 = PosY;
                    int64_t PosZ64 = PosZ;
                    PosX64 = (PosX64 >> 11u) & 0x1FFFFF;
                    PosY64 = (PosY64 >> 11u) & 0x1FFFFF;
                    PosZ64 = (PosZ64 >> 12u) & 0x0FFFFF;
            
                    // NOTE: Pack as a U64
                    int64_t PackedVertex = (PosX64 << 0u) | (PosY64 << 21u) | (PosZ64 << 42u);
                    Vertices[VertexId] = vec2(intBitsToFloat(unpackInt2x32(PackedVertex)));
                }

                // NOTE: Pack Normals
                {
                    vec3 Normal = normalize(mix(CornerNormals[FirstVertexId], CornerNormals[SecondVertexId], T));
                    Normals[VertexId] = Normal.xy;
                }
                
#else
                Vertices[VertexId] = Vertex;
#endif
            }
            
            // NOTE: Write out our triangle list
            uint StartVertexId = atomicAdd(IndirectArgs.NumVerticesPerInstance, GetTriangleCount(RegularCell)*3);
            for (uint TriangleId = 0; TriangleId < GetTriangleCount(RegularCell); ++TriangleId)
            {
#if PACK_VERTICES
                uint VertexId0 = RegularCell.VertexIndex[3*TriangleId + 0];
                uint VertexId1 = RegularCell.VertexIndex[3*TriangleId + 1];
                uint VertexId2 = RegularCell.VertexIndex[3*TriangleId + 2];
                TerrainTriangleList[StartVertexId + 3*TriangleId + 0] = vec4(Vertices[VertexId0], Normals[VertexId0]);
                TerrainTriangleList[StartVertexId + 3*TriangleId + 1] = vec4(Vertices[VertexId1], Normals[VertexId1]);
                TerrainTriangleList[StartVertexId + 3*TriangleId + 2] = vec4(Vertices[VertexId2], Normals[VertexId2]);
#else
                TerrainTriangleList[StartVertexId + 3*TriangleId + 0] = vec4(Vertices[RegularCell.VertexIndex[3*TriangleId + 0]], 1);
                TerrainTriangleList[StartVertexId + 3*TriangleId + 1] = vec4(Vertices[RegularCell.VertexIndex[3*TriangleId + 1]], 1);
                TerrainTriangleList[StartVertexId + 3*TriangleId + 2] = vec4(Vertices[RegularCell.VertexIndex[3*TriangleId + 2]], 1);
#endif
            }
        }
    }
}

#endif

// NOTE: Junk for debugging
// TODO: REMOVE
/*

#if 0
            if (GetTriangleCount(RegularCell) > 0)
            {
                uint StartVertexId = atomicAdd(IndirectArgs.NumVerticesPerInstance, 15);
                TerrainTriangleList[StartVertexId + 0].x = GetVertexCount(RegularCell);
                TerrainTriangleList[StartVertexId + 0].y = GetTriangleCount(RegularCell);
                TerrainTriangleList[StartVertexId + 0].z = RegularCellId;
                TerrainTriangleList[StartVertexId + 0].w = CaseByte;

#if 0
                TerrainTriangleList[StartVertexId + 1].x = CaseBit0;
                TerrainTriangleList[StartVertexId + 1].y = CaseBit1;
                TerrainTriangleList[StartVertexId + 1].z = CaseBit2;
                TerrainTriangleList[StartVertexId + 1].w = CaseBit3;
                TerrainTriangleList[StartVertexId + 2].x = CaseBit4;
                TerrainTriangleList[StartVertexId + 2].y = CaseBit5;
                TerrainTriangleList[StartVertexId + 2].z = CaseBit6;
                TerrainTriangleList[StartVertexId + 2].w = CaseBit7;
#endif                
                
#if 1
                TerrainTriangleList[StartVertexId + 1].x = Densities[0];
                TerrainTriangleList[StartVertexId + 2].x = Densities[1];
                TerrainTriangleList[StartVertexId + 3].x = Densities[2];
                TerrainTriangleList[StartVertexId + 4].x = Densities[3];
                TerrainTriangleList[StartVertexId + 5].x = Densities[4];
                TerrainTriangleList[StartVertexId + 6].x = Densities[5];
                TerrainTriangleList[StartVertexId + 7].x = Densities[6];
                TerrainTriangleList[StartVertexId + 8].x = Densities[7];
                TerrainTriangleList[StartVertexId + 9].xyz = gl_GlobalInvocationID;
#endif
                
#if 0
                TerrainTriangleList[StartVertexId + 1].x = PackedVertices.Edges[0] & 0xF;
                TerrainTriangleList[StartVertexId + 1].y = (PackedVertices.Edges[0] >> 4) & 0xF;
                TerrainTriangleList[StartVertexId + 2].x = PackedVertices.Edges[1] & 0xF;
                TerrainTriangleList[StartVertexId + 2].y = (PackedVertices.Edges[1] >> 4) & 0xF;
                TerrainTriangleList[StartVertexId + 3].x = PackedVertices.Edges[2] & 0xF;
                TerrainTriangleList[StartVertexId + 3].y = (PackedVertices.Edges[2] >> 4) & 0xF;

                TerrainTriangleList[StartVertexId + 4].xyz = Corners[PackedVertices.Edges[0] & 0xF];
                TerrainTriangleList[StartVertexId + 5].xyz = Corners[(PackedVertices.Edges[0] >> 4) & 0xF];
                TerrainTriangleList[StartVertexId + 6].xyz = Corners[PackedVertices.Edges[1] & 0xF];
                TerrainTriangleList[StartVertexId + 7].xyz = Corners[(PackedVertices.Edges[1] >> 4) & 0xF];
                TerrainTriangleList[StartVertexId + 8].xyz = Corners[PackedVertices.Edges[2] & 0xF];
                TerrainTriangleList[StartVertexId + 9].xyz = Corners[(PackedVertices.Edges[2] >> 4) & 0xF];

                TerrainTriangleList[StartVertexId + 10].xyz = Vertices[0];
                TerrainTriangleList[StartVertexId + 11].xyz = Vertices[1];
                TerrainTriangleList[StartVertexId + 12].xyz = Vertices[2];

                TerrainTriangleList[StartVertexId + 13].x = RegularCell.VertexIndex[0];
                TerrainTriangleList[StartVertexId + 13].y = RegularCell.VertexIndex[1];
                TerrainTriangleList[StartVertexId + 13].z = RegularCell.VertexIndex[2];
#endif
                
                TerrainTriangleList[StartVertexId + 14] = vec4(0xFFFFFFFF);
            }
#endif

 */
