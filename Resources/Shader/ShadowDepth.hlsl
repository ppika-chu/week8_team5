#pragma pack_matrix(row_major)

cbuffer ShadowViewConstants : register(b0)
{
    float4x4 LightViewProjectionMatrix;
};

cbuffer ShadowObjectConstants : register(b2)
{
    float4x4 WorldMatrix;
};

float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    const float4 WorldPosition = mul(float4(Position, 1.0f), WorldMatrix);
    
    return mul(WorldPosition, LightViewProjectionMatrix);
}

float4 mainPS() : SV_TARGET
{
    return float4(0.0f, 0.0f, 0.0f, 0.0f);
}