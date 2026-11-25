#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// コマ送りの絵の種類のデータ（effects.json の "sheets" の1つ）
struct SheetDef : EffectPartDef
{
    std::string texture;              // シートのテクスチャID
    int cols = 1, rows = 1;           // 縦横のコマ数
    int frames = 0;                   // 総コマ数（0ならcols*rows）
    float fps = 20.0f;                // 再生速度
    float scale = 1.0f;               // 表示サイズ（幅）
    float aspect = 1.0f;              // 高さ÷幅（1で正方形。裂け目からプレイヤーまで横に伸ばす時など）
    float yOffset = 0.0f;             // 発生位置のy補正
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool loop = false;                // falseで1周して消滅
    XMFLOAT3 offset = { 0, 0, 0 };    // 発生位置のオフセット（画面外から出す用）
    XMFLOAT3 vel = { 0, 0, 0 };       // ワールド速度(毎秒)＝3D空間を飛ぶ
    int   startFrame = 0;             // シート内の開始コマ（敵エディタのプレビューだけが使う）
    float delay = 0.0f;               // このシートだけの遅延
    bool  autoFlipX = false;          // 発生位置(x<0)なら左右反転して反対側から出す
    bool  flipX = false;              // 常に左右反転（autoFlipX とは独立）
    float rotDeg = 0.0f;              // このシート自体の回転(度)
    float rollJitter = 0.0f;          // 0より大きいと、出すたびに画面の中で ±この角度(度) のランダムに傾ける（噛みつきが何発も重なる時など）
    XMFLOAT3 fromCamera = { 0, 0, 0 };   // カメラ基準の発生位置(右, 上, 前)
    bool  useCamera = false;             // fromCamera を使うか
    float travel = 0.0f;                 // 着弾までの秒数(>0で速度を自動計算)

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ているコマ送りの絵1つ。決まった速さでコマを進め、1周したら消える（loop なら Stop されるまで続く）
class SpriteSheet : public EffectPart
{
public:
    SpriteSheet(const SheetDef& def, const SpawnAt& at);
    void Update(float dt) override;
    void Draw(Renderer3D* r, EffectLayer layer) override;
    bool IsDone() const override;
    void Stop(float fadeSec) override;   // フェードして消す（fadeSec<=0で即消し）
private:
    const SheetDef& m_def;            // コマ数・速さなどはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    XMFLOAT3 m_pos = { 0, 0, 0 }, m_vel = { 0, 0, 0 };
    XMFLOAT4 m_color = { 1, 1, 1, 1 };   // データの色×呼んだ側の色
    float m_rot = 0.0f;
    bool m_rolled = false;               // 画面の中で傾けて描くか（rollJitter）
    XMFLOAT3 m_right = { 1, 0, 0 }, m_up = { 0, 1, 0 };   // 傾けて描く時の、絵の右と上（カメラの右と上を回したもの）
    bool m_flipX = false;
    int m_frames = 1;
    float m_elapsed = 0.0f;           // マイナスの間は待ち（このシートだけの遅延）
    bool m_fading = false;
    float m_fade = 0.0f, m_fadeDur = 0.3f;
};
