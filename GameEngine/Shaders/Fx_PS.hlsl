// エフェクト用のピクセルシェーダー：形・ノイズ・色の帯の3枚と「寿命の進み具合」から、色と透明度を決める
//   温度 = 形 × ノイズ。進むほど冷えて、色の帯の熱い側（左）から冷たい側（右）へ移る
//   消え方 = 進み具合が ErodeStart～ErodeEnd の間で、ノイズの低い所からちぎれて消える
//   出方   = Appear > 0 なら、進み具合が Appear に届くまでに、形の画像の G（出てくる順番）が小さい所から順に出てくる
Texture2D texShape : register(t0);      // 形（R＝中心ほど白い。G＝出てくる順番。白黒の画像なら R と同じ）
Texture2D texNoise : register(t1);      // ノイズ（くり返して使う）
Texture2D texRamp  : register(t2);      // 色の帯（左＝熱い、右＝冷たい）
SamplerState samClamp : register(s0);   // 端で止まる（形・色の帯）
SamplerState samWrap  : register(s1);   // 端でくり返す（ノイズを流す）

struct PS_INPUT
{
    float4 Pos   : SV_POSITION;
    float2 Tex   : TEXCOORD;
    float4 Color : COLOR;
};

cbuffer FxParams : register(b1)
{
    float  Life;        // 寿命の進み具合（0→1）
    float  ErodeStart;  // この進み具合から消え始める
    float  ErodeEnd;    // ここで消えきる
    float  Glow;        // 色を何倍に光らせるか
    float2 Scroll;      // ノイズを流す向きと速さ（1秒あたりのUV）
    float  NoiseScale;  // ノイズの細かさ
    float  Time;        // 経過時間（秒）
    float  Seed;        // 粒ごとの乱数（どの粒も同じ模様にならないように）
    float  Appear;      // 出てくるのにかかる進み具合（0なら最初から全部出ている）
    float2 Pad;
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 shapeTex = texShape.Sample(samClamp, input.Tex);
    float shape = shapeTex.r;
    float2 nuv  = input.Tex * NoiseScale + Scroll * Time + Seed * float2(0.37, 0.71);
    float noise = texNoise.Sample(samWrap, nuv).r;

    // 温度：形が濃くて模様が白い所ほど熱い。進み具合とともに冷える
    float heat = saturate(shape * (0.4 + 0.9 * noise) * 1.6 - Life * 1.1);

    // 消え方：形×模様が「消えるしきい値」より低い所から消える（境目は少しぼかす）
    float cut   = saturate((Life - ErodeStart) / max(ErodeEnd - ErodeStart, 0.001));
    float mask  = shape * (0.35 + 0.65 * noise);
    float alpha = smoothstep(cut, cut + 0.12, mask);

    // 出方：G（出てくる順番）が小さい所から順に出てくる。境目はノイズで少しぎざぎざに
    if (Appear > 0.0)
    {
        float reveal = saturate(Life / Appear);
        alpha *= saturate((reveal * 1.25 - shapeTex.g + (noise - 0.5) * 0.15) / 0.15);
    }

    float4 col = texRamp.Sample(samClamp, float2(1.0 - heat, 0.5));   // 温度 → 色（熱い＝左）
    col.rgb *= Glow;
    col *= input.Color;   // いつもの板と同じ「色」（粒の色の変化・全体の色合い）
    col.a *= alpha;
    return col;
}
