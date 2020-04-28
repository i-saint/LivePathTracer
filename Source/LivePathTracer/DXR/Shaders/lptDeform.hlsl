#include "lptCommon.h"

#define kThreadBlockSize 4

RWStructuredBuffer<vertex_t>        g_dst_vertices  : register(u0);
StructuredBuffer<vertex_t>          g_src_vertices  : register(t0);

// blendshape data
StructuredBuffer<float4>            g_bs_delta      : register(t1);
StructuredBuffer<BlendshapeFrame>   g_bs_frames     : register(t2);
StructuredBuffer<BlendshapeInfo>    g_bs_info       : register(t3);
StructuredBuffer<float>             g_bs_weights    : register(t4);

// skinning data
StructuredBuffer<JointCount>        g_joint_counts   : register(t5);
StructuredBuffer<JointWeight>       g_joint_weights  : register(t6);
StructuredBuffer<float4x4>          g_joint_matrices : register(t7);

ConstantBuffer<MeshData>            g_mesh : register(b0);


uint VertexCount()
{
    uint n, s;
    g_dst_vertices.GetDimensions(n, s);
    return n;
}

uint BlendshapeCount()
{
    uint n, s;
    g_bs_info.GetDimensions(n, s);
    return n;
}

uint DeformFlags()
{
    return g_mesh.flags;
}


float GetBlendshapeWeight(uint bsi)
{
    return g_bs_weights[bsi];
}

uint GetBlendshapeFrameCount(uint bsi)
{
    return g_bs_info[bsi].frame_count;
}

float GetBlendshapeFrameWeight(uint bsi, uint fi)
{
    uint offset = g_bs_info[bsi].frame_offset;
    return g_bs_frames[offset + fi].weight;
}

float3 GetBlendshapeDelta(uint bsi, uint fi, uint vi)
{
    uint offset = g_bs_frames[g_bs_info[bsi].frame_offset + fi].delta_offset;
    return g_bs_delta[offset + vi].xyz;
}


uint GetVertexJointCount(uint vi)
{
    return g_joint_counts[vi].weight_count;
}

float GetVertexJointWeight(uint vi, uint bi)
{
    return g_joint_weights[g_joint_counts[vi].weight_offset + bi].weight;
}

float4x4 GetVertexJointMatrix(uint vi, uint bi)
{
    uint i = g_joint_weights[g_joint_counts[vi].weight_offset + bi].joint_index;
    return g_joint_matrices[i];
}


void ApplyBlendshape(uint vi, inout vertex_t v)
{
    float3 pos_deformed = v.position;

    uint blendshape_count = BlendshapeCount();
    for (uint bsi = 0; bsi < blendshape_count; ++bsi) {
        float weight = GetBlendshapeWeight(bsi);
        if (weight == 0.0f)
            continue;

        uint frame_count = GetBlendshapeFrameCount(bsi);
        float last_weight = GetBlendshapeFrameWeight(bsi, frame_count - 1);

        if (weight < 0.0f) {
            float3 delta = GetBlendshapeDelta(bsi, 0, vi);
            float s = weight / GetBlendshapeFrameWeight(bsi, 0);
            pos_deformed += delta * s;
        }
        else if (weight > last_weight) {
            float3 delta = GetBlendshapeDelta(bsi, frame_count - 1, vi);
            float s = 0.0f;
            if (frame_count >= 2) {
                float prev_weight = GetBlendshapeFrameWeight(bsi, frame_count - 2);
                s = (weight - prev_weight) / (last_weight - prev_weight);
            }
            else {
                s = weight / last_weight;
            }
            pos_deformed += delta * s;
        }
        else {
            float3 p1 = 0.0f, p2 = 0.0f;
            float w1 = 0.0f, w2 = 0.0f;

            for (uint fi = 0; fi < frame_count; ++fi) {
                float frame_weight = GetBlendshapeFrameWeight(bsi, fi);
                if (weight <= frame_weight) {
                    p2 = GetBlendshapeDelta(bsi, fi, vi);
                    w2 = frame_weight;
                    break;
                }
                else {
                    p1 = GetBlendshapeDelta(bsi, fi, vi);
                    w1 = frame_weight;
                }
            }
            float s = (weight - w1) / (w2 - w1);
            pos_deformed += lerp(p1, p2, s);
        }
    }
    v.position = pos_deformed;
}

void ApplySkinning(uint vi, inout vertex_t v)
{
    float4 pos_base = float4(v.position, 1.0f);
    float3 pos_deformedeformed = 0.0f;

    uint joint_count = GetVertexJointCount(vi);
    for (uint bi = 0; bi < joint_count; ++bi) {
        float w = GetVertexJointWeight(vi, bi);
        float4x4 m = GetVertexJointMatrix(vi, bi);
        pos_deformedeformed += mul(m, pos_base).xyz * w;
    }
    v.position = pos_deformedeformed;
}

[numthreads(kThreadBlockSize, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    uint vi = tid.x;

    vertex_t v = g_src_vertices[vi];
    uint deform_flags = DeformFlags();
    if (deform_flags & DF_APPLY_BLENDSHAPE)
        ApplyBlendshape(vi, v);
    if (deform_flags & DF_APPLY_SKINNING)
        ApplySkinning(vi, v);

    g_dst_vertices[vi] = v;
}
