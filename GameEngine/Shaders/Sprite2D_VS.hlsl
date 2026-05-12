cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix Projection;
    float4 Color;
};

struct VS_INPUT
{
    float3 Pos : POSITION;
    float2 Tex : TEXCOORD;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    output.Pos = mul(float4(input.Pos, 1.0f), World);
    output.Pos = mul(output.Pos, Projection);
    output.Tex = input.Tex;
    output.Color = Color;
    return output;
}