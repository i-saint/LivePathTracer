#pragma once
#include "MeshUtils/MeshUtils.h"
#include "lptSettings.h"
#include "lptInterface.h"

#define lptDeclRefPtr(T) using T##Ptr = ref_ptr<T, InternalReleaser<T>>
#define lptMaxLights 32

namespace lpt {

enum class DebugFlag : uint32_t
{
    Timestamp       = 0x01,
    ForceUpdateAS   = 0x02,
    PowerStableState= 0x04,
};

enum class GlobalFlag : uint32_t
{
};

enum class RenderFlag : uint32_t
{
    CullBackFaces           = 0x00000001,
    FlipCasterFaces         = 0x00000002,
    IgnoreSelfShadow        = 0x00000004,
    KeepSelfDropShadow      = 0x00000008,
    AlphaTest               = 0x00000010,
    Transparent             = 0x00000020,
    AdaptiveSampling        = 0x00000100,
    Antialiasing            = 0x00000200,
    GPUSkinning             = 0x00010000,
    ClampBlendShapeWights   = 0x00020000,
};

enum class InstanceFlag : uint32_t
{
    ReceiveShadows  = 0x01,
    ShadowsOnly     = 0x02,
    CastShadows     = 0x04,
    CullFront       = 0x10,
    CullBack        = 0x20,
    CullFrontShadow = 0x40,
    CullBackShadow  = 0x80,

    Default = ReceiveShadows | CastShadows | CullBack,
};

enum class DirtyFlag : uint32_t
{
    None        = 0x00000000,
    Transform   = 0x00000001,
    Blendshape  = 0x00000002,
    Joints      = 0x00000004,

    Indices     = 0x00000010,
    Points      = 0x00000020,
    Normals     = 0x00000040,
    Tangents    = 0x00000080,
    UV          = 0x00000100,

    Camera      = 0x01000000,
    Light       = 0x02000000,
    RenderTarget= 0x04000000,
    Texture     = 0x08000000,
    TextureData = 0x10000000,
    Material    = 0x20000000,
    Mesh        = 0x40000000,
    Instance    = 0x80000000,

    Deform = Transform | Blendshape | Joints,
    Shape = Indices | Points,
    Vertices = Indices | Points | Normals | Tangents | UV,
    SceneEntities = Camera | Light | RenderTarget | Instance,
    Any = 0xffffffff,
};

int GetTexelSize(TextureFormat v);

#define DefCompare(T)\
    bool operator==(const T& v) const { return std::memcmp(this, &v, sizeof(*this)) == 0; }\
    bool operator!=(const T& v) const { return !(*this == v); }\

// these structs are directly uploaded to GPU buffers

struct CameraData
{
    float4x4 view{};
    float4x4 proj{};
    union {
        float3 position;
        float4 position4;
    };
    quatf rotation{};

    float near_plane{};
    float far_plane{};
    float fov{};
    float pad{};

    DefCompare(CameraData);
};

struct LightData
{
    LightType light_type{};
    uint32_t pad1[3];

    float3 position{};
    float range{};
    float3 direction{};
    float spot_angle{}; // radian
    float3 color{};
    float pad2{};

    DefCompare(LightData);
};

struct MaterialData
{
    float3 diffuse{};
    float roughness{};
    float3 emissive{};
    float opacity{};

    int diffuse_tex{};
    int emissive_tex{};
    int2 pad_tex{};

    DefCompare(MaterialData);
};

struct InstanceData
{
    float4x4 local_to_world;
    float4x4 world_to_local;
    uint32_t mesh_index;
    uint32_t material_index;
    uint32_t instance_flags; // combination of InstanceFlags
    uint32_t layer_mask;

    DefCompare(InstanceData);
};

struct SceneData
{
    uint32_t render_flags; // combination of RenderFlag
    uint32_t light_count;
    float shadow_ray_offset;
    float self_shadow_threshold;

    CameraData camera;
    LightData lights[lptMaxLights];

    DefCompare(SceneData);
    template<class Body>
    void eachLight(const Body& body)
    {
        for (uint32_t li = 0; li < light_count; ++li)
            body(lights[li]);
    }
};
#undef DefCompare


template<class T>
class RefCount : public T
{
public:
    int addRef() override
    {
        return ++m_ref_external;
    }
    int release() override
    {
        // deleting entities is responsible for Context.
        // (ContextDXR::frameBegin() etc.)
        return --m_ref_external;
    }
    int getRef() const override
    {
        return m_ref_external;
    }

    int addRefInternal()
    {
        return ++m_ref_internal;
    }
    int releaseInternal()
    {
        int ret = --m_ref_internal;
        if (ret == 0)
            delete this;
        return ret;
    }
    int getRefInternal() const
    {
        return m_ref_internal;
    }

    void setName(const char* v) override
    {
        m_name = v;
    }

    const char* getName() const override
    {
        return m_name.c_str();
    }


public:
    std::atomic_int m_ref_external{ 0 };
    std::atomic_int m_ref_internal{ 0 };
    std::string m_name;
    uint32_t m_dirty_flags = 0;
};

template<class T>
class InternalReleaser
{
public:
    static void addRef(T* v) { v->addRefInternal(); }
    static void release(T* v) { v->releaseInternal(); }
};


template<class T>
class EntityBase : public RefCount<T>
{
public:
    bool isDirty(DirtyFlag v = DirtyFlag::Any) const
    {
        return (m_dirty_flags & (uint32_t)v) != 0;
    }

    void markDirty(DirtyFlag v)
    {
        m_dirty_flags |= (uint32_t)v;
    }

    void clearDirty()
    {
        m_dirty_flags = 0;
    }

public:
    uint32_t m_dirty_flags = 0;
};



class Camera : public EntityBase<ICamera>
{
public:
    void setPosition(float3 v) override;
    void setDirection(float3 v, float3 up) override;
    void setFOV(float v) override;
    void setNear(float v) override;
    void setFar(float v) override;

public:
    CameraData m_data;
};
lptDeclRefPtr(Camera);


class Light : public EntityBase<ILight>
{
public:
    void setType(LightType v) override;
    void setPosition(float3 v) override;
    void setDirection(float3 v) override;
    void setRange(float v) override;
    void setSpotAngle(float v) override;
    void setColor(float3 v) override;

public:
    LightData m_data;
};
lptDeclRefPtr(Light);


class Texture : public EntityBase<ITexture>
{
public:
    Texture(TextureFormat format, int width, int height);
    void upload(const void* src) override;

public:
    int m_index = 0;
    TextureFormat m_format = TextureFormat::Unknown;
    int m_width = 0;
    int m_height = 0;
    RawVector<char> m_data;
};
lptDeclRefPtr(Texture);


class RenderTarget : public EntityBase<IRenderTarget>
{
public:
    RenderTarget(TextureFormat format, int width, int height);
    void enableReadback(bool v) override;

public:
    TextureFormat m_format = TextureFormat::Unknown;
    int m_width = 0;
    int m_height = 0;
    bool m_readback_enabled = false;
};
lptDeclRefPtr(RenderTarget);


class Material : public EntityBase<IMaterial>
{
public:
    void setDiffuse(float3 v) override;
    void setRoughness(float v) override;
    void setEmissive(float3 v) override;
    void setDiffuseTexture(ITexture* v) override;
    void setEmissiveTexture(ITexture* v) override;

public:
    int m_index = 0;
    MaterialData m_data;
    TexturePtr m_tex_diffuse;
    TexturePtr m_tex_emissive;
};
lptDeclRefPtr(Material);


struct BlendshapeData
{
    struct FrameData
    {
        RawVector<float3> delta;
        float weight = 0.0f;
    };
    std::vector<FrameData> frames;
};

class Mesh : public EntityBase<IMesh>
{
public:
    void setIndices(const int* v, size_t n) override;
    void setPoints(const float3* v, size_t n) override;
    void setNormals(const float3* v, size_t n) override;
    void setTangents(const float3* v, size_t n) override;
    void setUV(const float2* v, size_t n) override;

    void setJointBindposes(const float4x4* v, size_t n) override;
    void setJointWeights(const JointWeight* v, size_t n) override;
    void setJointCounts(const uint8_t* v, size_t n) override;

    void markDynamic() override;

public:
    int m_index = 0;
    RawVector<int>    m_indices;
    RawVector<float3> m_points;
    RawVector<float3> m_normals;
    RawVector<float3> m_tangents;
    RawVector<float2> m_uv;

    RawVector<float4x4>    m_joint_bindposes;
    RawVector<uint8_t>     m_joint_counts;
    RawVector<JointWeight> m_joint_weights;

    std::vector<BlendshapeData> blendshapes;

    bool m_dynamic = false;
};
lptDeclRefPtr(Mesh);


class MeshInstance : public EntityBase<IMeshInstance>
{
public:
    MeshInstance(IMesh* v = nullptr);
    void setMaterial(IMaterial* v) override;
    void setTransform(const float4x4& v) override;
    void setJointMatrices(const float4x4* v) override;
    bool hasFlag(InstanceFlag flag) const;

public:
    InstanceData m_data;
    MeshPtr m_mesh;
    MaterialPtr m_material;
    RawVector<float4x4> m_joint_matrices;
    RawVector<float> m_blendshape_weights;

    uint32_t m_instance_flags = (uint32_t)InstanceFlag::Default;
};
lptDeclRefPtr(MeshInstance);



class Scene : public EntityBase<IScene>
{
public:
    void setRenderTarget(IRenderTarget* v) override;
    void setCamera(ICamera* v) override;
    void addLight(ILight* v) override;
    void removeLight(ILight* v) override;
    void addMesh(IMeshInstance* v) override;
    void removeMesh(IMeshInstance* v) override;
    void clear() override;

public:
    SceneData m_data;
    CameraPtr m_camera;
    RenderTargetPtr m_render_target;
    std::vector<LightPtr> m_lights;
    std::vector<MeshInstancePtr> m_instances;
};
lptDeclRefPtr(Scene);


class Context : public RefCount<IContext>
{
public:
public:
};
lptDeclRefPtr(Context);


#define lptDefBaseT(T, I)\
    class T;\
    inline T* base_t(I* v) { return static_cast<T*>(v); }\
    inline T& base_t(I& v) { return static_cast<T&>(v); }

lptDefBaseT(Camera, ICamera)
lptDefBaseT(Light, ILight)
lptDefBaseT(Texture, ITexture)
lptDefBaseT(RenderTarget, IRenderTarget)
lptDefBaseT(Material, IMaterial)
lptDefBaseT(Mesh, IMesh)
lptDefBaseT(MeshInstance, IMeshInstance)
lptDefBaseT(Scene, IScene)
lptDefBaseT(Context, IContext)

#undef lptDefBaseT


struct GlobalSettings
{
    std::atomic_uint32_t debug_flags{ 0 }; // combination of DebugFlag
    std::atomic_uint32_t flags{ 0 }; // combination of GlobalFlag

    void enableDebugFlag(DebugFlag flag);
    void disableDebugFlag(DebugFlag flag);
    bool hasDebugFlag(DebugFlag flag) const;

    bool hasFlag(GlobalFlag v) const;
};

GlobalSettings& GetGlobals();

} // namespace lpt
