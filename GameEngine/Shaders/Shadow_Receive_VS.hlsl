// 落ち影 その2：影を受ける地面の頂点シェーダー
// 3D_VS と同じことをした上で、授業の VS_SpriteShadow と同じく「光のカメラから見た位置」も出す
cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    float4 Color;
    float4 UvRect;
    float4 Extra;
};

cbuffer ShadowBuffer : register(b1)
{
    matrix LightView;
    matrix LightProj;
    float4 ShadowParams;   // x=影の濃さ y=ずれ止め z=1/影の絵の大きさ
};

struct VS_INPUT
{
    float3 Pos : POSITION;
    float2 Tex : TEXCOORD;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;       // ゲームのカメラから見た位置
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
    float4 LightPos : POSITION1;    // 光のカメラから見た位置
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    float4 world = mul(float4(input.Pos, 1.0f), World);
    output.Pos = mul(mul(world, View), Projection);              // ゲームのカメラ
    output.LightPos = mul(mul(world, LightView), LightProj);     // 光のカメラ
    output.Tex = input.Tex * UvRect.zw + UvRect.xy;
    output.Color = Color;
    return output;
}
