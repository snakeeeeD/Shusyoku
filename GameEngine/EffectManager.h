#pragma once
#include <string>
#include <vector>
#include <memory>
#include <DirectXMath.h>
#include "EffectPart.h"

using namespace DirectX;
class Renderer3D;

// 演出の呼び出し口（全部の種類で共通）。
// effects.json の名前で Play すると、その演出の部品（粒・コマ送り…）を、それぞれの種類のクラスに作らせて、まとめて動かす・描く・消す。
// どう動いてどう描くかは、種類ごとのファイル（ParticleBurst.cpp・SpriteSheet.cpp）に書く。種類の一覧は EffectKinds.cpp
class EffectManager
{
public:
    // 名前でエフェクトを再生（effects.json のプリセット）。delay 秒後に出す。tint＝色、scale＝大きさの倍率。戻り値は Stop 用の番号
    static int Play(const std::string& id, float x, float y, float z, float rot = 0.0f, float delay = 0.0f,
        XMFLOAT4 tint = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), float scale = 1.0f);
    static void Stop(int handle, float fadeSec = 0.3f);   // 指定番号の演出を止める（フェードできる種類はフェード）
    static void Update(float dt);
    static void Draw(Renderer3D* r, EffectLayer layer);   // layer の演出だけ描く（地面→キャラ→上→画面、の順に呼ばれる）
    static void Clear();   // 全部消す（時間差の予約も。次の戦闘へ持ち越さない）
private:
    struct PendingPlay { std::string id; float x, y, z, rot; XMFLOAT4 tint; float scale; float delay; };
    static inline std::vector<std::unique_ptr<EffectPart>> s_parts;   // 出ている演出（種類はばらばら）
    static inline std::vector<PendingPlay> s_pending;                 // 時間差で出す予約
    static inline SpawnAt s_cam;                                      // 最後に描いたときのカメラの向き（出すときに渡す）
    static inline int s_nextHandle = 1;
};
