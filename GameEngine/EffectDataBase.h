#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <DirectXMath.h>

using namespace DirectX;

struct BurstDef
{
    int count = 10;
    float speed = 1.5f, life = 0.5f, scale = 0.1f;
    float gravity = 4.0f, drag = 0.02f;
    XMFLOAT4 colorStart = { 1,1,1,1 };
    XMFLOAT4 colorEnd = { 1,1,1,0 };
    std::string texture;
};

struct SheetAnim
{
    std::string texture;              // シートのテクスチャID
    int cols = 1, rows = 1;           // 縦横のコマ数
    int frames = 0;                   // 総コマ数（0ならcols*rows）
    float fps = 20.0f;                // 再生速度
    float scale = 1.0f;               // 表示サイズ
    float yOffset = 0.0f;             // 発生位置のy補正
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool loop = false;                // falseで1周して消滅
    XMFLOAT3 offset = { 0, 0, 0 };    // 発生位置のオフセット（画面外から出す用）
    XMFLOAT3 vel = { 0, 0, 0 };       // ワールド速度(毎秒)＝3D空間を飛ぶ
    int   startFrame = 0;             // シート内の開始コマ
    float delay = 0.0f;               // このシートだけの遅延
    bool  autoFlipX = false;          // 発生位置(x<0)なら左右反転して反対側から出す
    float rotDeg = 0.0f;              // このシート自体の回転(度)
    XMFLOAT3 fromCamera = { 0, 0, 0 };   // カメラ基準の発生位置(右, 上, 前)
    bool  useCamera = false;             // fromCamera を使うか
    float travel = 0.0f;                 // 着弾までの秒数(>0で速度を自動計算)
};

struct EffectDef
{
    std::string id;
    std::vector<BurstDef> bursts;
    std::vector<SheetAnim> sheets;
    std::string then;                 // 着弾後などに続けて鳴らすエフェクトid
    float thenDelay = 0.0f;           // その遅延(秒)
    float shake = 0.0f;               // 再生時の画面振動の強さ(0~1)
};

class EffectDataBase
{
public:
    static void Load(const std::string& path);
    static const EffectDef* Get(const std::string& id);
private:
    static std::unordered_map<std::string, EffectDef> s_effects;
};