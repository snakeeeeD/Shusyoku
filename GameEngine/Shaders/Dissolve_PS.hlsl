// 溶けて消えるシェーダー：絵の色はそのまま、ノイズの低い所から順に消える。消える境目だけ、色の帯の色で光らせる
//   順番   = ノイズ×(1－Bias) ＋ 絵の縦の位置×Bias（Bias が大きいほど、上から順に溶ける）
//   境目   = 順番が「今のしきい値」のすぐ上の所。Edge の幅だけ、色の帯の左（熱い）→ 右の色にする
//   White  = 白を混ぜる量（とどめの一瞬、白く光らせる）
Texture2D texSprite : register(t0);     // 溶かす絵（キャラの絵、カードを1枚にまとめた絵など）
Texture2D texNoise  : register(t1);     // ノイズ（くり返して使う）
Texture2D texRamp   : register(t2);     // 色の帯（境目の光る色。左＝熱い）
SamplerState samClamp : register(s0);
SamplerState samWrap  : register(s1);

cbuffer ConstantBuffer : register(b0)   // いつもの板の数字（uv の範囲 UvRect を使う）
{
    matrix World;
    matrix View;
    matrix Projection;
    float4 Color;
    float4 UvRect;   // xy＝始まり、zw＝大きさ（絵の一部だけ・左右反転を描くとき）
    float4 Extra;
};

struct PS_INPUT
{
    float4 Pos   : SV_POSITION;
    float2 Tex   : TEXCOORD;
    float4 Color : COLOR;
};

cbuffer DissolveParams : register(b1)
{
    float Progress;     // 溶けた割合（0→1）
    float Edge;         // 光る境目の幅
    float NoiseScale;   // ノイズの細かさ
    float Glow;         // 境目の明るさ
    float Bias;         // 上から溶ける強さ（0＝ノイズだけ）
    float Seed;         // 模様をずらす乱数
    float White;        // 白を混ぜる量
    float Premultiplied;   // 1＝絵の色に透明度がもう掛かっている（オフスクリーンにまとめた絵）
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 col = texSprite.Sample(samClamp, input.Tex);
    if (Premultiplied > 0.5) col.rgb /= max(col.a, 0.001);       // 透明度で割って、ふつうの色に戻す
    col *= input.Color;
    float2 local = (input.Tex - UvRect.xy) / UvRect.zw;        // 描いている範囲の中での位置（0～1）。反転・一部でも模様は同じ
    float noise = texNoise.Sample(samWrap, local * NoiseScale + Seed * float2(0.37, 0.71)).r;
    float order = noise * (1.0 - Bias) + local.y * Bias;      // 溶ける順番（小さい所から先に消える）

    float thr = Progress * 1.15 - 0.05;                        // 今のしきい値（最後は全部消えきる）
    float e = order - thr;                                     // しきい値からの距離（マイナス＝もう消えた）
    float visible = smoothstep(0.0, 0.015, e);
    float rim = (1.0 - smoothstep(0.0, Edge, e)) * step(-0.01, e);   // 光る境目
    float3 ember = texRamp.Sample(samClamp, float2(saturate(e / max(Edge, 0.001)) * 0.6, 0.5)).rgb * Glow;

    col.rgb = lerp(col.rgb, float3(1, 1, 1), White);           // とどめの一瞬は白く
    col.rgb *= 1.0 - 0.45 * Progress;                          // 溶けながら焦げて暗くなる
    col.rgb = lerp(col.rgb, ember, rim);
    col.a *= saturate(visible + rim);
    return col;
}
