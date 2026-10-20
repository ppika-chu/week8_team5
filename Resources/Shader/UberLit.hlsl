#pragma pack_matrix(row_major)

#include "ColorSpace.hlsli"

#define LIGHT_TYPE_POINT 1   // C++ ELightType과 같아야 함
#define LIGHT_TYPE_SPOT  2
#define LIGHT_TYPE_RECT  3

// LightCullingCommon.hlsli의 MAX_LIGHTS_PER_CELL / CELL_STRIDE와 같아야 한다.
#define MAX_LIGHTS_PER_CELL 32
#define CELL_STRIDE (MAX_LIGHTS_PER_CELL + 1)

// LightGrid.LightDebugMode 값. 에디터 SettingsPanel의 "Light Debug" 콤보 순서와 같아야 한다.
#define LIGHT_DEBUG_MODE_NONE         0
#define LIGHT_DEBUG_MODE_CELL_HEATMAP 1
#define LIGHT_DEBUG_MODE_BRUTE_FORCE  2   // 컬링 결과를 무시하고 모든 로컬 라이트를 순회
#define LIGHT_DEBUG_MODE_SLICES       3

// 조명 모델은 C++(FRenderResourceManager::LoadUberLit)이 매크로로 하나만 넘긴다.
// 아무 매크로 없이 컴파일될 때(기존 머티리얼이 쓰는 기본 키)만 Lambert를 켠다.
#if !defined(LIGHTING_MODEL_UNLIT) && !defined(LIGHTING_MODEL_GOURAUD) && \
    !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_BLINN_PHONG) && \
    !defined(WORLD_NORMAL)
#define LIGHTING_MODEL_LAMBERT 1
#endif

#define MAX_DIRECTIONAL_LIGHTS 1

// 아래 구조체와 cbuffer Lighting은 Renderer.h의 FLightingConstants와 바이트 단위로 같아야 한다.
struct FAmbientLightInfo
{
    float3 Color;
    float Intensity;
};

struct FDirectionalLightInfo
{
    float3 Direction; // 빛이 향하는 방향
    float Intensity;
    float3 Color;
    float Padding;
};

// SceneManagement.h의 FLightRenderParameters와 바이트 단위로 같아야 한다.
struct FLightRenderParameters
{
    float3 Position;
    float Radius; // 영향 반경
    float3 Color;
    float Intensity;
    float3 Direction;
    float InnerConeCos;
    float3 Right;
    float FalloffExponent;
    float OuterConeCos;
    float HalfWidth;
    float HalfHeight;
    uint Type;
    float BarnDoorCos; // Rect 전용
    float BarnDoorSin;
    float BarnDoorLength;
    float SourceRadius; // 광원의 반지름(구 형태를 가정)
};



// Renderer.h의 FViewConstants와 바이트 단위로 같아야 한다. VS와 PS 둘 다 b0에 바인딩된다.
cbuffer ViewConstants : register(b0)
{
    matrix ViewProjection;
    float3 CameraWorldPos; // Specular의 V = normalize(CameraWorldPos - WorldPos)
    float ViewPadding;
};

cbuffer MaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVOffset;
    float bOpaque;
    float Shininess; // Blinn-Phong 광택 지수 (Material.h FStaticMeshMaterialParams::Shininess)
};

// Renderer.h의 FPerObjectConstants와 바이트 단위로 같아야 한다.
cbuffer PerObjectConstants : register(b2)
{
    matrix World;
    matrix WorldInverseTranspose; // (World^-1)^T

    uint bReceiveShadows;
    uint3 PerObjectPadding;
};

// b3는 FireBall이 쓰는 동안 비워 둔다.

cbuffer Lighting : register(b4)
{
    FAmbientLightInfo Ambient;
    FDirectionalLightInfo Directional;
    uint NumLocalLights;
    uint3 LightingPadding;
};


cbuffer LightGrid : register(b5)
{
    uint NumTilesX;
    uint NumTilesY;
    uint NumSlices;
    uint LightDebugMode; // LIGHT_DEBUG_MODE_*
    float NearZ;
    float FarZ;
    float SliceScale;
    float SliceBias;
    uint TileSize;
    uint bOrthographic;
    uint2 LightGridPadding;
};

/* Shadow */
/*cbuffer ShadowConstants : register(b6)
{
    matrix LightViewProj;
    float ShadowBias;
    uint bShadowEnabled;
    float2 ShadowPadding;
};*/

struct FShadowInfo
{
    matrix LightViewProjection;
    float2 AtlasOffset;
    float2 AtlasScale;
    float DepthBias;
    float PrescaledTangentTheta;
    uint bShadowActive;
    float Padding;
};

struct FLightShadowRange
{
    uint Offset;
    uint Count;
    float BlendWidth;
    uint bDebugCascades;
};

Texture2D<float> ShadowMapAtlas : register(t13); // 0 ~ 1 깊이
StructuredBuffer<FShadowInfo> ShadowInfos : register(t14);
StructuredBuffer<FLightShadowRange> LightShadowRanges : register(t15);
SamplerState PointSampler : register(s1);
SamplerComparisonState LinearCmpSampler : register(s2);

StructuredBuffer<FLightRenderParameters> LocalLights : register(t10);
// 셀마다 [개수][라이트 번호 x MAX_LIGHTS_PER_CELL] 형태로 CELL_STRIDE칸씩 이어져 있다.
StructuredBuffer<uint> CellLightIndices : register(t12);


#define CASTER_SEARCH_COUNT 16
#define CASTER_SAMPLE_COUNT 32
static const float2 PoissonDisk[32] =
{
    float2(0.03586579, -0.59705367),
    float2(-0.23044534, 0.87454343),
    float2(-0.66339939, 0.46301572),
    float2(-0.35899683, -0.33978122),
    float2(0.56808130, -0.61531311),
    float2(0.77020037, 0.45271065),
    float2(0.31040800, 0.20762513),
    float2(0.93493649, -0.03457706),
    float2(-0.40942175, 0.06790063),
    float2(-0.11239377, -0.91544888),
    float2(0.23295829, 0.94751021),
    float2(0.08970381, -0.17057817),
    float2(-0.13597787, 0.38841689),
    float2(0.52597640, -0.24154734),
    float2(-0.86081911, -0.10926739),
    float2(-0.58368362, -0.74310095),
    float2(-0.71432519, 0.14127773),
    float2(-0.37569550, -0.59557831),
    float2(0.30426721, -0.83233358),
    float2(-0.51594125, 0.71216759),
    float2(0.60539985, -0.00120244),
    float2(0.08039493, 0.71460868),
    float2(0.58907444, 0.73757767),
    float2(0.78857418, -0.43640529),
    float2(-0.70777108, -0.50342177),
    float2(-0.55752157, -0.15186224),
    float2(-0.04242211, 0.13067769),
    float2(-0.22741851, -0.11848174),
    float2(0.59583200, 0.24992236),
    float2(0.29563006, 0.47243108),
    float2(0.34038874, -0.43836685),
    float2(-0.39910329, 0.48008842)
};



Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState MaterialSampler : register(s0);


struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT; // w = 바이탄젠트 부호
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

// VS가 출력하고 PS가 입력으로 받는다.
struct VS_OUTPUT
{
    float4 SvPosition : SV_POSITION; // VS: 클립 공간 / PS: 픽셀 좌표(xy), 깊이(z), View 깊이(w)
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
#if LIGHTING_MODEL_GOURAUD
    float3 VertexDiffuse : COLOR1;   // 정점에서 미리 계산한 Diffuse 조명
#endif
};

// Diffuse와 Specular를 담는 구조체
struct FLightContribution
{
    float3 Diffuse;
    float3 Specular;
};

// 원뿔 감쇠량. 0(Outer 밖) ~ 1(Inner 안)
float GetSpotConeAttenuation(FLightRenderParameters Light, float3 L)
{
    return smoothstep(Light.OuterConeCos, Light.InnerConeCos, dot(-L, Light.Direction));
}

// 거리 감쇠량을 돌려주고, 표면에서 빛을 향하는 단위 벡터 L을 OutL로 내보낸다.
float GetDistanceAttenuation(float3 LightPos, float AttenuationRadius, float FalloffExponent, float3 WorldPos, out float3 OutL)
{
    const float3 ToLight = LightPos - WorldPos; // 픽셀의 월드 좌표에서 빛을 향하는 벡터
    const float Distance = length(ToLight);
    OutL = ToLight / max(Distance, 0.0001f);
    // 거리가 Radius에 가까울수록 0으로
    return pow(saturate(1.0f - Distance / max(AttenuationRadius, 0.001f)), max(FalloffExponent, 0.001f));
}



// 라이트 하나의 Diffuse와 Specular를 구한다. Radiance = Color * Intensity * Attenuation (* ConeAttenuation)
FLightContribution SurfaceShading(float3 N, float3 L, float3 V, float3 Radiance, float InShininess)
{
    const float NdotL = saturate(dot(normalize(N), normalize(L)));
    FLightContribution Contribution = (FLightContribution) 0;
    Contribution.Diffuse = NdotL;

#if LIGHTING_MODEL_BLINN_PHONG
    // Blinn Specular
    const float3 H = normalize(L + V);
    if (dot(N, L) > 0)
    {
        Contribution.Specular = pow(saturate(dot(normalize(N), H)), max(InShininess, 1.f));
    }
#endif
    Contribution.Diffuse *= Radiance;
    Contribution.Specular *= Radiance;
    return Contribution;
}

// 표면에 들어오는 빛의 양만 돌려준다. 표면색(albedo)은 호출한 쪽에서 곱한다.
float3 GetAmbientLighting()
{
    return Ambient.Color * Ambient.Intensity;
}

uint GetCascadeNo()
{
    return 0;
}

// return value : 통과율
// 월드 좌표 -> 맵 UV (0 ~ 1), uv 밖이거나 near/far 바깥이면 false
bool ProjectToShadowMap(float3 WorldPos, matrix LightVP, out float2 OutUV, out float OutDepth)
{
    const float4 Clip = mul(float4(WorldPos, 1.0f), LightVP);
    OutUV = 0.0f;
    OutDepth = 0.0f;
    if (Clip.w <= 0.0f)
        return false;
    const float3 Ndc = Clip.xyz / Clip.w;
    OutUV = float2(Ndc.x * 0.5f + 0.5f, -Ndc.y * 0.5f + 0.5f);
    OutDepth = Ndc.z;
    return all(OutUV >= 0.0f) && all(OutUV <= 1.0f);
}

// 아틀라스에서 해당 맵의 Tile 가져와서 비교. 1 = 빛을 받음 / 0 = 빛을 안 받음 (그림자)
float SampleShadowTile(FShadowInfo Info, float2 UV, float Depth, float3 WorldPosDDX, float3 WorldPosDDY)
{
    // 비활성 Cascade도 선택과 블렌딩에 참여하며 가시율은 1이다.
    if (Info.bShadowActive == 0)
    {
        return 1.0f;
    }

    const float3 ReceiverUV = float3(UV * Info.AtlasScale + Info.AtlasOffset, Depth);
    float2 SearchCasterRadius = ReceiverUV.z * Info.PrescaledTangentTheta * Info.AtlasScale.xy;
    float3 DuvDx = mul(float4(WorldPosDDX, 0.0f), Info.LightViewProjection).xyz * float3(0.5f, -0.5f, 1.0f) * float3(Info.AtlasScale, 1.0f);
    float3 DuvDy = mul(float4(WorldPosDDY, 0.0f), Info.LightViewProjection).xyz * float3(0.5f, -0.5f, 1.0f) * float3(Info.AtlasScale, 1.0f);
    float Det = DuvDx.x * DuvDy.y - DuvDx.y * DuvDy.x;
    float2x2 InvMatrix = float2x2(0, 0, 0, 0);
#if !LIGHTING_MODEL_GOURAUD
    //if (abs(Det) >= 1e-9f * Info.AtlasScale.x)
        if (Det != 0.0f)
            InvMatrix = float2x2(DuvDy.y, -DuvDx.y, -DuvDy.x, DuvDx.x) / Det;
#endif
    uint TexelCountX, TexelCountY;
    ShadowMapAtlas.GetDimensions(TexelCountX, TexelCountY);
    float2 TexelStep = 1.0f / float2(TexelCountX, TexelCountY);
    float2 MaxSampleRadius = 40.0f * TexelStep;
    SearchCasterRadius = min(SearchCasterRadius, MaxSampleRadius);
    float2 TileMinUV = Info.AtlasOffset;
    float2 TileMaxUV = Info.AtlasOffset + Info.AtlasScale;
    float2 MapDu = float2(TexelStep.x, 0.0f);
    float2 MapDv = float2(0.0f, TexelStep.y);
    float2 DxyDu = mul(MapDu, InvMatrix);
    float2 DxyDv = mul(MapDv, InvMatrix);
    float BlockAverage = 0.0f;
    uint DepthCount = 0;
    for (uint i = 0; i < CASTER_SEARCH_COUNT; ++i)
    {
        float2 OffsetUV = SearchCasterRadius * PoissonDisk[i];
        float2 SampleUV = ReceiverUV.xy + OffsetUV;
        if (any(SampleUV < TileMinUV) || any(SampleUV >= TileMaxUV))
            continue;
        float ReceiverDepth = OffsetUV.x * TexelCountX * (DxyDu.x * DuvDx.z + DxyDu.y * DuvDy.z) + OffsetUV.y * TexelCountY * (DxyDv.x * DuvDx.z + DxyDv.y * DuvDy.z) + ReceiverUV.z - Info.DepthBias;
        float DepthSampled = ShadowMapAtlas.SampleLevel(PointSampler, SampleUV, 0);
        if (DepthSampled < ReceiverDepth)
        {
            BlockAverage += DepthSampled;
            ++DepthCount;
        }
    }
    float PercentLit = 1.0f;
    if (DepthCount > 0)
    {
        BlockAverage /= DepthCount;
        float2 BlockerRadius = max(0.75f * min(Info.PrescaledTangentTheta * (ReceiverUV.z - BlockAverage) * Info.AtlasScale.xy, MaxSampleRadius), 1.5f * TexelStep);
        PercentLit = 0.0f;
        uint SampleCount = 0;
        for (uint j = 0; j < CASTER_SAMPLE_COUNT; ++j)
        {
            float2 OffsetUV = BlockerRadius * PoissonDisk[j];
            float2 SampleUV = ReceiverUV.xy + OffsetUV;
            if (any(SampleUV < TileMinUV + 0.5f * TexelStep) || any(SampleUV > TileMaxUV - 0.5f * TexelStep))
                continue;
            float ReceiverDepth = OffsetUV.x * TexelCountX * (DxyDu.x * DuvDx.z + DxyDu.y * DuvDy.z) + OffsetUV.y * TexelCountY * (DxyDv.x * DuvDx.z + DxyDv.y * DuvDy.z) + ReceiverUV.z - Info.DepthBias;
            PercentLit += ShadowMapAtlas.SampleCmpLevelZero(LinearCmpSampler, SampleUV, ReceiverDepth);
            ++SampleCount;
        }
        PercentLit = SampleCount > 0 ? PercentLit / SampleCount : 1.0f;
    }
    return PercentLit;
}

// 픽셀을 cascade index로 반환 (debug용)
uint FindCascadeIndex(float3 WorldPos)
{
    const FLightShadowRange Range = LightShadowRanges[0];
    for (uint i = 0; i < Range.Count; ++i)
    {
        float2 UV; float Depth;
        if (ProjectToShadowMap(WorldPos, ShadowInfos[Range.Offset + i].LightViewProjection, UV, Depth))
            return i;
    }
    // 모든 Cascade 밖이면 Count 반환
    return Range.Count;
}

float CalcDirectionalShadow(float3 WorldPos)
{
    if (bReceiveShadows == 0)
    {
        return 1.0f;
    }

    // Disabled shadow is not included in ShadowInfos.
    // if (!bShadowEnabled) return 1.0f;
    
    // Currently, we only support one directional light,
    // so we reserve the slot 0 for directional light shadow info.
    const FLightShadowRange Range = LightShadowRanges[0];
    if (Range.Count == 0)
        return 1.0f; // 그림자 없는 Directional Light

    // 캐스케이드 선택 전에 공통 월드 위치를 미분한다. Gouraud의 VS에서는 미분을 사용할 수 없다.
#if LIGHTING_MODEL_GOURAUD
    const float3 WorldPosDDX = 0.0f;
    const float3 WorldPosDDY = 0.0f;
#else
    const float3 WorldPosDDX = ddx(WorldPos);
    const float3 WorldPosDDY = ddy(WorldPos);
#endif

    // 앞쪽 Cascade부터 검색
    for (uint i = 0; i < Range.Count; ++i)
    {
        const FShadowInfo Info = ShadowInfos[Range.Offset + i];
        float2 UV; float Depth;

        // 1) 밖이면 다음 Cascade 검사
        if (!ProjectToShadowMap(WorldPos, Info.LightViewProjection, UV, Depth))
            continue;
        
        // 그림자면 0 빛이면 1
        const float Shadow = SampleShadowTile(Info, UV, Depth, WorldPosDDX, WorldPosDDY);

        // 현재 위치에서 가장자리 위치까지 거리
        const float EdgeDist = min(min(UV.x, UV.y), min(1.0f - UV.x, 1.0f - UV.y));

        // 2) 충분히 UV 안쪽이거나 다음 Cascade가 없으면 그대로 반환
        if (EdgeDist >= Range.BlendWidth || i + 1 >= Range.Count)
            return Shadow;

        // 3) Blend 하기 위해 다음 Cascade 구하기
        const FShadowInfo NextInfo = ShadowInfos[Range.Offset + i + 1];
        float2 NextUV; float NextDepth;
        if (!ProjectToShadowMap(WorldPos, NextInfo.LightViewProjection, NextUV, NextDepth))
            return Shadow;      // 혹시라도 이 점을 표현하지 않으면 그대로 반환
        
        // lerp(A, B, t) : t가 0 이면 A, 1이면 B
        return lerp(SampleShadowTile(NextInfo, NextUV, NextDepth, WorldPosDDX, WorldPosDDY), Shadow, EdgeDist / Range.BlendWidth);
    }

    // 모든 Cascade 밖이면 
    return 1.0;
}

float SampleShadowInfo(FShadowInfo Info, float3 WorldPos)
{
    if (Info.bShadowActive == 0)
    {
        return 1.0f;
    }

    const float4 LightClip =
        mul(float4(WorldPos, 1.0f), Info.LightViewProjection);
    
    if (LightClip.w <= 0.0f)
        return 1.0f; // 뒤쪽이면 그림자 없음
            
    const float3 Ndc = LightClip.xyz / LightClip.w;
    const float2 UV = float2((Ndc.x * 0.5 + 0.5), (-Ndc.y * 0.5 + 0.5));

    if (any(UV < 0.0f) || any(UV > 1.0f) || 
        Ndc.z < 0.0f || Ndc.z > 1.0f)
        return 1.0f; // Shadow Map 밖이면 빛을 받는 것으로 간주
    
    const float2 AtlasUV = UV * Info.AtlasScale + Info.AtlasOffset;
    
    uint Width, Height;
    ShadowMapAtlas.GetDimensions(Width, Height);
    
    const float2 AtlasSize = float2(Width, Height);

    const int2 TileMin = int2(round(Info.AtlasOffset * AtlasSize));
    const int2 TileSize = int2(round(Info.AtlasScale * AtlasSize));
    const int2 TileMax = TileMin + TileSize - int2(1, 1);

    const int2 Texel = clamp(int2(AtlasUV * AtlasSize), TileMin, TileMax);
    
    const float MapDepth = ShadowMapAtlas.Load(int3(Texel, 0));
    
    return Ndc.z - Info.DepthBias > MapDepth ? 0.0f : 1.0f;
}

uint GetPointShadowFace(float3 LightToSurface)
{
    const float3 A = abs(LightToSurface);
    
    if (A.x >= A.y && A.x >= A.z) // X축이 가장 크면 +X/-X 면
        return LightToSurface.x >= 0.0f ? 0u : 1u; // +X, -X
    
    if (A.y >= A.z) // Y축이 가장 크면 +Y/-Y 면
        return LightToSurface.y >= 0.0f ? 2u : 3u; // +Y, -Y
    
    // Z축이 가장 크면 +Z/-Z 면
    return LightToSurface.z >= 0.0f ? 4u : 5u; // +Z, -Z
}

float CalcLocalShadow(
    uint LightIndex,
    FLightRenderParameters Light,
    float3 WorldPos)
{
    if (bReceiveShadows == 0)
    {
        return 1.0f;
    }

    if (Light.Type != LIGHT_TYPE_POINT &&
        Light.Type != LIGHT_TYPE_SPOT)
    {
        return 1.0f;
    }
    
    const FLightShadowRange Range =
        LightShadowRanges[MAX_DIRECTIONAL_LIGHTS + LightIndex];
    
    if (Range.Count == 0)
        return 1.0f; // 그림자 없는 로컬 라이트
    
    uint FaceIndex = 0; // Spot
    
    if (Light.Type == LIGHT_TYPE_POINT)
    {
        FaceIndex = GetPointShadowFace(WorldPos - Light.Position);
    }
    
    if (FaceIndex >= Range.Count)
        return 1.0f; // 그림자 맵이 없는 면이면 빛을 받는 것으로 간주
    
    const FShadowInfo Info = ShadowInfos[Range.Offset + FaceIndex];
    return SampleShadowInfo(Info, WorldPos);
}

// Directional Light의 영향을 구한다
FLightContribution
    CalcDirectionalLight(
    float3 N, float3 V, float InShininess)
{
    const float3 Radiance = Directional.Color * Directional.Intensity;
    return SurfaceShading(N, -Directional.Direction, V, Radiance, InShininess);
}

// 해당 WorldPos에서 PointLight 하나의 영향(밝기)을 구한다
FLightContribution CalcPointLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    float3 L;
    const float Attenuation = GetDistanceAttenuation(Light.Position, Light.Radius, Light.FalloffExponent, WorldPos, L);
    return SurfaceShading(N, L, V, Light.Color * Light.Intensity * Attenuation, InShininess);
}

// Spot은 Point에서 원뿔 감쇠만 곱하면 된다
FLightContribution CalcSpotLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    float3 L;
    const float Attenuation = GetDistanceAttenuation(Light.Position, Light.Radius, Light.FalloffExponent, WorldPos, L);
    return SurfaceShading(N, L, V, Light.Color * Light.Intensity * Attenuation * GetSpotConeAttenuation(Light, L), InShininess);
}

FLightContribution CalcRectLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    const float3 Up = cross(Light.Direction, Light.Right); // Z축
    const float3 CenterToSurface = WorldPos - Light.Position;

    // 1) 사각형 위에서 표면과 가장 가까운 점 찾기
    const float RectX = clamp(dot(CenterToSurface, Light.Right), -Light.HalfWidth, Light.HalfWidth); // 사각형 가로 좌표
    const float RectY = clamp(dot(CenterToSurface, Up), -Light.HalfHeight, Light.HalfHeight); // 사각형 세로 좌표
    const float3 ClosestPoint = Light.Position + Light.Right * RectX + Up * RectY;

    // 2) 가장 가까운 점을 점광원처럼 취급
    float3 L; // 표면 -> 빛
    const float Attenuation = GetDistanceAttenuation(ClosestPoint, Light.Radius, Light.FalloffExponent, WorldPos, L);

    // 3) 면의 앞쪽만 비춘다 (뒤쪽이면 0)
    const float FrontFace = saturate(dot(-L, Light.Direction));

    // 4) 문 안쪽(프러스텀)만 통과
    float BarnDoorMask = 1.0f;

    // Door Length가 0이면 그냥 rect (전방향)
    if (Light.BarnDoorLength > 0.0f)
    {
        const float3 EmitterToSurface = WorldPos - ClosestPoint; // 발광면에서 표면으로
        const float Distance = max(length(EmitterToSurface), 0.0001f);
        const float ForwardDist = dot(EmitterToSurface, Light.Direction);
        const float LateralX = abs(dot(EmitterToSurface, Light.Right));
        const float LateralY = abs(dot(EmitterToSurface, Up));
        // 옆으로 벌어진 각도가 frustum 안인가? (양수면 안쪽)
        const float SlackX = (ForwardDist * Light.BarnDoorSin - LateralX * Light.BarnDoorCos) / Distance;
        const float SlackY = (ForwardDist * Light.BarnDoorSin - LateralY * Light.BarnDoorCos) / Distance;
        // Slack이 −0.05보다 작으면 0
        // x,y 모두 닿아야 빛이 닿으므로 곱으로 마스크 만들기
        BarnDoorMask = smoothstep(-0.05f, 0.0f, SlackX) * smoothstep(-0.05f, 0.0f, SlackY);
    }

    const float3 Radiance = Light.Color * Light.Intensity * Attenuation * FrontFace * BarnDoorMask;
    return SurfaceShading(N, L, V, Radiance, InShininess);
}

// Type에 따라 Point / Spot / Rect로 나눠 계산한다
FLightContribution CalcLocalLight(FLightRenderParameters Light, float3 N, float3 V, float3 WorldPos, float InShininess)
{
    FLightContribution Result;
    if (Light.Type == LIGHT_TYPE_SPOT)
        Result = CalcSpotLight(Light, N, V, WorldPos, InShininess);
    else if (Light.Type == LIGHT_TYPE_RECT)
        Result = CalcRectLight(Light, N, V, WorldPos, InShininess);
    else
        Result = CalcPointLight(Light, N, V, WorldPos, InShininess);
    return Result;
}

// 픽셀이 속한 깊이 슬라이스 번호
uint GetSliceIndex(float4 SvPosition)
{
    float Slice;
    if (bOrthographic)
    {
        // 직교: SV_Position.z(0~1)가 Near~Far에 선형으로 대응
        Slice = SvPosition.z * NumSlices;
    }
    else
    {
        // 원근: SV_Position.w = 클립 공간 w = View 깊이(X)
        Slice = log(SvPosition.w) * SliceScale + SliceBias;
    }
    return (uint) clamp(Slice, 0.0f, NumSlices - 1.0f);
}

// 픽셀이 속한 셀(타일 x 슬라이스)의 목록이 CellLightIndices에서 시작하는 위치
uint GetCellListOffset(float4 SvPosition)
{
    const uint2 Tile = (uint2) SvPosition.xy / TileSize;
    const uint Slice = GetSliceIndex(SvPosition);
    return ((Slice * NumTilesY + Tile.y) * NumTilesX + Tile.x) * CELL_STRIDE;
}

// 라이트 개수를 검정 → 파랑 → 초록 → 빨강 → 흰색 히트맵으로
float3 GetHeatmapColor(uint LightCount)
{
    if (LightCount == 0)
        return float3(0, 0, 0);
    if (LightCount >= MAX_LIGHTS_PER_CELL)
        return float3(1, 1, 1);

    const float t = LightCount / (float) MAX_LIGHTS_PER_CELL;
    return saturate(float3(t * 2 - 0.5, 1 - abs(t * 2 - 1), 1.5 - t * 2));
}

// Directional + 로컬 라이트의 Diffuse/Specular를 모두 더한다. Ambient는 포함하지 않는다.
// bUseLightGrid = true면 SvPosition이 속한 셀의 라이트만, false면 전체 로컬 라이트를 순회한다.
// VS(Gouraud)는 픽셀 위치가 없어 셀을 쓸 수 없으므로 false로 호출한다.
FLightContribution AccumulateDirectLighting(float3 N, float3 V, float3 WorldPos, float InShininess, float4 SvPosition, bool bUseLightGrid)
{
    // Directional Light는 처음에 한 번만 계산
    FLightContribution Total = CalcDirectionalLight(N, V, InShininess);
    const float DirectionalShadow = CalcDirectionalShadow(WorldPos);
    Total.Diffuse *= DirectionalShadow;
    Total.Specular *= DirectionalShadow;

    uint LightCount = NumLocalLights;
    uint CellOffset = 0;
    bool bUseCellList = false;
    if (bUseLightGrid && NumTilesX > 0 && LightDebugMode != LIGHT_DEBUG_MODE_BRUTE_FORCE)
    {
        CellOffset = GetCellListOffset(SvPosition);
        const uint CellLightCount = CellLightIndices[CellOffset];
        // 셀이 넘쳤으면(상한 이상) 목록이 잘렸으므로 전체 순회로 돌아간다
        if (CellLightCount < MAX_LIGHTS_PER_CELL)
        {
            LightCount = CellLightCount;
            bUseCellList = true;
        }
    }

    for (uint Index = 0; Index < LightCount; ++Index)
    {
        const uint LightIndex = bUseCellList ? CellLightIndices[CellOffset + 1 + Index] : Index;
        const FLightRenderParameters Light = LocalLights[LightIndex];
        const FLightContribution LightContribution = CalcLocalLight(Light, N, V, WorldPos, InShininess);
        const float Shadow = CalcLocalShadow(LightIndex, Light, WorldPos);
        Total.Diffuse += LightContribution.Diffuse * Shadow;
        Total.Specular += LightContribution.Specular * Shadow;
    }

    return Total;
}

VS_OUTPUT mainVS(VS_INPUT Input)
{
    VS_OUTPUT Output;

    const float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    Output.SvPosition = mul(WorldPos, ViewProjection);
    Output.WorldPos = WorldPos.xyz;

    // 노멀은 역전치로, 탄젠트는 표면을 따라 놓인 방향이라 World로 변환한다.
    // World가 뒤집혀 있으면(행렬식 < 0) 바이탄젠트 부호도 뒤집는다.
    Output.Normal = mul(float4(Input.Normal, 0.0f), WorldInverseTranspose).xyz;
    Output.Tangent = float4(mul(float4(Input.Tangent.xyz, 0.0f), World).xyz, Input.Tangent.w * (determinant((float3x3) World) < 0.0f ? -1.0f : 1.0f));

    Output.UV = Input.UV;
    Output.Color = Input.Color;
#if LIGHTING_MODEL_GOURAUD
    const float3 V = normalize(CameraWorldPos - Output.WorldPos);
    const FLightContribution Contribution = AccumulateDirectLighting(normalize(Output.Normal), V, Output.WorldPos, 1, Output.SvPosition, false);
    Output.VertexDiffuse = Contribution.Diffuse;
#endif
    return Output;
}

float4 mainPS(VS_OUTPUT Input) : SV_TARGET
{
    const float2 UV = Input.UV + UVOffset;
    const float4 TexColor = DiffuseTexture.Sample(MaterialSampler, UV);
    float4 Albedo = TexColor * BaseColor;
#if !defined(EDITOR_ARROW_SHADER)
    // 씬은 HDR 타깃에 선형으로 쌓이므로 sRGB로 인코딩된 디퓨즈 텍스처와 BaseColor를 각각 선형으로 바꾼다.
    // 에디터 화살표는 톤 매핑 이후 LDR 타깃에 그려지므로 sRGB 그대로 둔다.
    Albedo.rgb = SRGBToLinear(TexColor.rgb) * SRGBToLinear(BaseColor.rgb);

    if (LightDebugMode == LIGHT_DEBUG_MODE_CELL_HEATMAP && NumTilesX > 0)
    {
        const uint CellLightCount = CellLightIndices[GetCellListOffset(Input.SvPosition)];
        return float4(lerp(Albedo.rgb * 0.2f, GetHeatmapColor(CellLightCount), 0.7f), 1.0f);
    }
    // 슬라이스 시각화: 이웃 슬라이스끼리 색이 크게 달라지도록 황금비로 색상환을 돈다.
    if (LightDebugMode == LIGHT_DEBUG_MODE_SLICES && NumTilesX > 0)
    {
        const float Hue = frac(GetSliceIndex(Input.SvPosition) * 0.618034f);
        const float3 SliceColor = saturate(abs(frac(Hue + float3(0.0f, 2.0f, 1.0f) / 3.0f) * 6.0f - 3.0f) - 1.0f);
        return float4(lerp(Albedo.rgb * 0.2f, SliceColor, 0.7f), 1.0f);
    }
#endif

    const float3 VertexNormal = { normalize(Input.Normal) };
#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG) || defined(WORLD_NORMAL)
    // TBN: 노멀맵의 탄젠트 공간 노멀을 월드 공간으로 옮긴다.
    const float3 T = { normalize(Input.Tangent.xyz - VertexNormal * dot(VertexNormal, Input.Tangent.xyz)) };
    const float3 B = { cross(VertexNormal, T) * (Input.Tangent.w < 0.0f ? -1.0f : 1.0f) };
    const float3 TangentNormal = { NormalTexture.Sample(MaterialSampler, UV).xyz * 2.0f - 1.0f };
    const float3 N = { normalize(T * TangentNormal.x + B * -TangentNormal.y + VertexNormal * TangentNormal.z) };
#endif

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    const float Alpha = { bOpaque > 0.5f ? 1.0f : Albedo.a };

#if LIGHTING_MODEL_UNLIT
    return float4(Albedo.rgb, Alpha);
#elif WORLD_NORMAL
    return float4(N * 0.5f + 0.5f, 1.0f);
#else
    float3 DiffuseLighting = { GetAmbientLighting() };
    float3 SpecularLighting = { 0.0f, 0.0f, 0.0f };

#if defined(LIGHTING_MODEL_LAMBERT) || defined(LIGHTING_MODEL_BLINN_PHONG)
    const float3 V = { normalize(CameraWorldPos - Input.WorldPos) };
    const FLightContribution Contribution = { AccumulateDirectLighting(N, V, Input.WorldPos, Shininess, Input.SvPosition, true) };
    DiffuseLighting += Contribution.Diffuse;
    SpecularLighting = Contribution.Specular;
#elif LIGHTING_MODEL_GOURAUD
    DiffuseLighting += Input.VertexDiffuse;
#endif

    // Cascade 디버그
    if (bReceiveShadows != 0 &&
        LightShadowRanges[0].bDebugCascades != 0)
    {
        static const float3 CascadeColors[5] = {
            // 연한 빨강, 연한 초록, 연학 파랑, 연한 노랑, 흰색 (Cascade 없는 곳은 원래 색)
            float3(1.0f, 0.3f, 0.3f), float3(0.3f, 1.0f, 0.3f), float3(0.3f, 0.3f, 1.0f), float3(1.0f, 1.0f, 0.3f), float3(1.0f, 1.0f, 1.0f)
        };
        // 틴트 추가
        const uint Index = FindCascadeIndex(Input.WorldPos);
        // 모든 Cascade 밖(Index == Count)이면 Cascade 수와 상관없이 흰색
        const float3 Tint = (Index >= LightShadowRanges[0].Count) ? float3(1.0f, 1.0f, 1.0f) : CascadeColors[Index];
        Albedo.rgb *= Tint;
    }

    return float4(Albedo.rgb * DiffuseLighting + SpecularLighting, Alpha);
#endif
}
