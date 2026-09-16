cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix Projection;
    float4 Color;
    float4 UVRect;
    float4 Fade;
};

Texture2D txDiffuse : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
    float WY : TEXCOORD1;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 c = txDiffuse.Sample(samLinear, input.Tex) * input.Color;
    if (Fade.z > 0.5f)
    {
        // ‹«ŠEü(Fade.x)‚æ‚è‰º(=Y‚ª‘å‚«‚¢)‚ğ“§–¾‚ÉA‚Ú‚©‚µ•(Fade.y)‚Å‚È‚ß‚ç‚©‚É
        float a = saturate((Fade.x - input.WY) / max(Fade.y, 1.0f));
        c.a *= a;
    }
    return c;
}