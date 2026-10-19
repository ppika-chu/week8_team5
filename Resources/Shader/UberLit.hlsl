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
    float BarnDoorCos;     // Rect 전용
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



StructuredBuffer<FLightRenderParameters> LocalLights : register(t10);
// 셀마다 [개수][라이트 번호 x MAX_LIGHTS_PER_CELL] 형태로 CELL_STRIDE칸씩 이어져 있다.
StructuredBuffer<uint> CellLightIndices : register(t12);


cbuffer ShadowConstants : register(b6)
{
    matrix DirectionalLightRotation;
    float ShadowBias;
    uint bShadowEnabled;
    float CascadeShadowMapReferenceHeight; // 월드 원점과 레퍼런스 광원 평면의 거리로 정의. 낮을수록 좋지만 웬만하면 모든 리시버/캐스터의 광원 뷰 시점 높이보다 높아야 함(자연스럽게, 모든 캐스케이드들의 높이는 웬만하면 레퍼런스보다 낮게 될 것). 값 변동 시 그 순간에 그림자 아티팩트 발생 가능성 있음.
// 따라서 이전 프레임에 비해 '디렉셔널 라이트 방향이 그대로여도 이 값을 바꿀 당위성이 있는 상황(ex: 월드 컨텍스트가 격변함-핵폭탄이 터져 월드의 물체들의 전체적인 높이가 감소. 폭탄이 터져 빛나는 순간에 이 값을 감소시킬 수 있을것임)'이 아니라면, 바꾸지 않아야 함 
// 위 예시로부터 생각해볼 수 있는 변화 가능 조건으로, 월드 액터 혹은 그들의 렌더링 데이터에 대한 쿼리 구조를 사용해 일련의 높이 정보를 얻고 프러스텀 내에 눈에 띄는 그림자를 만들어내는 리시버가 존재하지 않으면 이용자가 별도의 수동 조작 함수를 호출하지 않고도 변화시킬 수 있을 것임
// 한편 디렉셔널 라이트의 방향이 변하는 경우의 값 변경 정책 설계는 자유. 정책의 예시로, 그림자의 품질을 최대한 일정하게 유지하고 싶은 월드위치가 있으면 그 위치로부터 레퍼런스 광원 평면의 거리가 일정하도록 설계할 수도 있을 것(다만 방향이 변하는 경우는 리시버/캐스터의 광원 뷰 시점 높이가 변화무쌍할 것은 염두에 두어야 함). 이 경우 매니저 정책 함수에 특정 월드 위치가 영향을 줌
// 단순하게는 고정 레퍼런스 높이로 시작해볼 수 있을 것
    
    float ShadowPadding;
};

struct FCascadeData
{
    matrix LightViewProj;
    float Size; // xy
    float Depth;
    float Bias;
    float PrescaledTangentTheta; // tan(각반지름) / Length
    float2 TopLeftUV;
    float UVScale;
};

Texture2D<float> CascadeShadowMap : register(t13);     // 0 ~ 1 깊이
StructuredBuffer<FCascadeData> CascadeDatas : register(t14);
SamplerState PointSampler : register(s1);

#define CASTER_SEARCH_COUNT 16
#define CASTER_SAMPLE_COUNT 32
const float2 PoissonDisk[32] = (
    float2(-0.94201624, -0.39906216),
    float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870),
    float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432),
    float2(-0.36885672, -0.59391560),
    float2(-0.21444538,  0.58169124),
    float2( 0.21396971,  0.71632737),
    float2( 0.14517782, -0.89163759),
    float2( 0.77199182, -0.54098940),
    float2( 0.74344331,  0.47479805),
    float2(-0.52329040, -0.40813896),
    float2(-0.54061840, -0.84076041),
    float2( 0.11587961, -0.47395784),
    float2( 0.73387539,  0.13610810),
    float2(-0.16568006,  0.28951076),
    float2( 0.29347372, -0.25450444),
    float2(-0.47137415,  0.84261900),
    float2( 0.04688114,  0.11199968),
    float2(-0.64670003,  0.41320398),
    float2(-0.01723472, -0.14091361),
    float2( 0.47144005, -0.64096510),
    float2( 0.46977828,  0.91652441),
    float2(-0.40773016,  0.13437238),
    float2(-0.11189914,  0.88775164),
    float2( 0.44046342,  0.57549924),
    float2(-0.84226143, -0.11475760),
    float2( 0.29200342, -0.05948252),
    float2(-0.41221237, -0.00743513),
    float2( 0.50645125, -0.15044230),
    float2( 0.09115263,  0.41349834),
    float2(-0.15655446, -0.73037607)
);



Texture2D DiffuseTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState MaterialSampler : register(s0);


struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float4 Tangent : TANGENT;   // w = 바이탄젠트 부호
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

// return 1 -> 빛을 받음. 0 -> 그림자 안 (빛을 안 받음)
float CalcDirectionalShadow(float3 WorldPos)
{
    if (!bShadowEnabled) return 1.0f;

    uint CascadeNo = GetCascadeNo();
    
    FCascadeData CascadeData = CascadeDatas[CascadeNo];
    
    float4 ReceiverUV = mul(float4(WorldPos, 1), CascadeData.LightViewProj) * float4(0.5f, 0.5f, 0.5f, 1.0f) + float4(0.5f, 0.5f, 0.5f, 0.0f);
    
    float StandardizedViewDepth = ReceiverUV.w + CascadeData.Bias;
    
    float SearchCasterRadius = StandardizedViewDepth * CascadeData.PrescaledTangentTheta;

    float3 DuvDx = ddx(ReceiverUV);
    float3 DuvDy = ddy(ReceiverUV);
    
    float Det = DuvDx.x * DuvDy.y - DuvDx.y * DuvDy.x;
    float2x2 InvMatrix;
    if (abs(Det) < 1e-6f)
    {
        DuvDx.z = 0.0f;
        DuvDy.z = 0.0f;
        InvMatrix = float2x2(1, 0, 0, 1);
    }
    else
    {
        InvMatrix = float2x2(DuvDy.y, -DuvDy.x, -DuvDx.y, DuvDx.x) / Det;
    }
    
    
    
    for (uint i = 0; i < CASTER_SEARCH_COUNT; ++i)
    {
        CascadeShadowMap.Sample(PointSampler, ReceiverUV.xy + SearchCasterRadius * PoissonDisk[i]);
    }
    
    uint Width, Height;

    ShadowMap.GetDimensions(Width, Height);   // 텍스처 크기(텍셀 개수) 돌려주는 내장함수
    const uint2 Texel = min(uint2(UV * float2(Width, Height)), uint2(Width-1, Height-1));    // UV = 1.0 이면 텍셀 밖이므로 clamp
    const float MapDepth = ShadowMap.Load(int3(Texel, 0));
    return (Ndc.z - ShadowBias > MapDepth)? 0.0f : 1.0f;
}

// Directional Light의 영향을 구한다
FLightContribution CalcDirectionalLight(float3 N, float3 V, float InShininess)
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
    const float3 Up = cross(Light.Direction, Light.Right);  // Z축
    const float3 CenterToSurface = WorldPos - Light.Position;

    // 1) 사각형 위에서 표면과 가장 가까운 점 찾기
    const float RectX = clamp(dot(CenterToSurface, Light.Right), -Light.HalfWidth,  Light.HalfWidth);   // 사각형 가로 좌표
    const float RectY = clamp(dot(CenterToSurface, Up),          -Light.HalfHeight, Light.HalfHeight);  // 사각형 세로 좌표
    const float3 ClosestPoint = Light.Position + Light.Right * RectX + Up * RectY;

    // 2) 가장 가까운 점을 점광원처럼 취급
    float3 L;   // 표면 -> 빛
    const float Attenuation = GetDistanceAttenuation(ClosestPoint, Light.Radius, Light.FalloffExponent, WorldPos, L);

    // 3) 면의 앞쪽만 비춘다 (뒤쪽이면 0)
    const float FrontFace = saturate(dot(-L, Light.Direction));

    // 4) 문 안쪽(프러스텀)만 통과
    float BarnDoorMask = 1.0f;

    // Door Length가 0이면 그냥 rect (전방향)
    if (Light.BarnDoorLength > 0.0f)
    {
        const float3 EmitterToSurface = WorldPos - ClosestPoint;     // 발광면에서 표면으로
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
        const FLightContribution LightContribution = CalcLocalLight(LocalLights[LightIndex], N, V, WorldPos, InShininess);
        Total.Diffuse += LightContribution.Diffuse;
        Total.Specular += LightContribution.Specular;
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

    return float4(Albedo.rgb * DiffuseLighting + SpecularLighting, Alpha);
#endif
}
