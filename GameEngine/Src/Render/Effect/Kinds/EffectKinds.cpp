#include "EffectKinds.h"
#include "ParticleBurst.h"
#include "SpriteSheet.h"
#include "GroundRing.h"
#include "ShockWall.h"
#include "ScreenOverlay.h"
#include "ScreenImage.h"
#include "Helix.h"
#include "FlashSprite.h"
#include "Projectile.h"
#include "Tether.h"
#include <unordered_map>
#include <Windows.h>

std::unique_ptr<EffectPartDef> EffectKinds::Load(const std::string& type, const nlohmann::json& j)
{
    // 種類の一覧。新しい種類は、その種類のファイルを作って、ここに1行足す
    static const std::unordered_map<std::string, LoadFn> KINDS = {
        { "burst", &BurstDef::Load },   // 粒をばらまく（ParticleBurst.cpp）
        { "sheet", &SheetDef::Load },   // コマ送りの絵（SpriteSheet.cpp）
        { "ring",  &GroundRingDef::Load },   // 地面に広がる輪（GroundRing.cpp）
        { "wall",  &ShockWallDef::Load },    // 輪から立ち上がる衝撃波の壁（ShockWall.cpp）
        { "screen", &ScreenOverlayDef::Load },   // 画面いっぱいの演出（ScreenOverlay.cpp）
        { "screenImage", &ScreenImageDef::Load },   // 画面に貼った絵を動かして止める（ScreenImage.cpp）
        { "helix", &HelixDef::Load },              // らせんが回りながら昇る柱（Helix.cpp）
        { "flash", &FlashSpriteDef::Load },      // カメラに向いた絵1枚（FlashSprite.cpp）
        { "projectile", &ProjectileDef::Load },  // 飛び道具（Projectile.cpp）
        { "tether", &TetherDef::Load },          // 敵とプレイヤーをつなぐ物（Tether.cpp）
    };
    auto it = KINDS.find(type);
    if (it == KINDS.end())
    {
        OutputDebugStringA(("[effects.json] unknown type: " + type + "\n").c_str());
        return nullptr;
    }
    return it->second(j);   // その種類の「読み方」の関数を呼ぶ
}
