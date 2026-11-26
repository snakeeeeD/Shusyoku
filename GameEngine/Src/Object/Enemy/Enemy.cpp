#include "Enemy.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "TerrainDataBase.h"
#include "Player.h"
#include "BuffInfo.h"
#include "GameUtils.h"
#include "RangeShape.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "Audio.h"
#include "EffectDataBase.h"   // 溶ける見た目の材質（fx_dissolve）
#include "CardExecutor.h"     // 床の効果（TriggerTerrain）
#include "RelicManager.h"     // 床の効果を受けないレリック
#include <algorithm>
#include <map>
#include <queue>

// 敵（初期化・更新・描画・絵の位置・占有マス）。ほかは機能ごとのファイル：EnemyHealth / AI / Threat / Move / PushPlayer
//  / Snake / Action / EnemyEffect*（効果の種類ごと）.cpp。どれも同じ Enemy クラスのメンバー

Enemy::Enemy()
    : m_HP(30), m_maxHP(30)
    , m_block(0)
{
    width = 1.0f;
    height = 1.0f;
    worldY = 0.05f;
}

void Enemy::Init(const std::string& id)
{
	const EnemyData* data = EnemyDataBase::Get(id);
	if (!data) return;

    m_id = id;
	m_HP = data->hp;
	m_maxHP = data->hp;
    m_displayHp = (float)m_HP;
	width = data->width;
	height = data->height;
    m_isBoss = data->isBoss;
    m_immovable = data->immovable;
	m_textureName = data->textureName;
    m_gridShape = data->gridShape;
    m_drawZ = data->drawZ;
    m_mouthX = data->mouthX; m_mouthY = data->mouthY;
    m_isSnake = data->snake;
    m_feel.SetStyle(MoveFeel::StyleFromName(data->moveStyle));
}

void Enemy::Update(float delteTime)
{

}

void Enemy::Draw3D(Renderer3D* renderer)
{
    if (!isActive) return;
    if (m_isSnake) return;

    if (m_dying) { DrawDeath(renderer); return; }   // 倒れていく途中は、溶ける絵だけ

    XMFLOAT4 drawColor = GetDrawColor();

    float y = worldY;
    float s = GetJumpScale();

    // 残像（2マス以上続けて動いた時。少し前の位置に、薄く）
    for (auto& g : m_feel.Ghosts())
    {
        XMFLOAT4 gc = drawColor; gc.w *= g.alpha;
        renderer->DrawBillboard(TextureManager::Get(m_textureName),
            g.x, y, g.z + m_drawZ, width * s, height * s, 0.0f, gc, FacingUV());
    }

    // 移動の手応え：跳ねて浮く高さと、伸び・つぶれ（板の基準は足元なので、足は地面についたまま）
    y += m_feel.Lift(height);
    float w = width * s * m_feel.ScaleX(), h = height * s * m_feel.ScaleY();

    // 突進のブラー：少しでも伸びている間は、ブラーのシェーダーで描く（重ねる絵も同じだけ伸ばす）
    const float BLUR_STRENGTH = MoveFeel::BLUR_STRENGTH;   // 数字は MoveFeel にまとめた（プレイヤーと共通）
    float bx = m_feel.BlurX(), bz = m_feel.BlurZ();
    bool blur = (bx * bx + bz * bz) > MoveFeel::BLUR_MIN * MoveFeel::BLUR_MIN;
    auto drawBody = [&](const std::string& tex) {
        if (blur)
            renderer->DrawMotionBlurBillboard(TextureManager::Get(tex), worldX, y, worldZ + m_drawZ,
                w, h, 0.0f, drawColor, FacingUV(), bx, bz, BLUR_STRENGTH);
        else
            renderer->DrawBillboard(TextureManager::Get(tex), worldX, y, worldZ + m_drawZ,
                w, h, 0.0f, drawColor, FacingUV());
    };
    drawBody(m_textureName);
    for (auto& o : m_overlays) drawBody(o);   // 重ねる絵は本体とまったく同じ位置・大きさ（絵の中で配置済み）
}

// 落ち影：本体と同じ大きさ・跳ねた高さで、地面に立てた板を描く（跳ねると影が足元から離れる）
void Enemy::DrawShadowCaster(Renderer3D* renderer)
{
    if (!isActive || m_isSnake || m_dying) return;
    float s = GetJumpScale();
    renderer->DrawShadowCaster(TextureManager::Get(m_textureName), worldX, worldY + m_feel.Lift(height), worldZ + m_drawZ,
        width * s * m_feel.ScaleX(), height * s * m_feel.ScaleY(), FacingUV());
}

bool Enemy::RemoveOverlay(const std::string& t)
{
    auto it = std::find(m_overlays.begin(), m_overlays.end(), t);
    if (it == m_overlays.end()) return false;
    m_overlays.erase(it);
    return true;
}

// プレイヤーの方を向く。画像は左向きが基準なので、右にいる時だけ反転する
// 絵の中の点のワールド座標。絵の板は足元が基準で、上方向は (0, cos, sin)（カメラの方へ傾けてある）
XMFLOAT3 Enemy::SpritePoint(float lx, float ly) const
{
    const float pitch = XMConvertToRadians(Renderer3D::GetBillboardPitch());
    return XMFLOAT3(worldX + lx * width, worldY + ly * height * cosf(pitch), worldZ + m_drawZ + ly * height * sinf(pitch));
}

// 口の位置。絵は左向き（-x）が基準なので、右を向いている時は反対側
XMFLOAT3 Enemy::MouthWorld() const
{
    return SpritePoint((m_faceRight ? 1.0f : -1.0f) * m_mouthX, m_mouthY);
}

void Enemy::FaceToward(float targetX)
{
    if (m_isBoss) return;                           // ボスは向き固定
    float dx = targetX - worldX;
    if (dx > 0.05f) m_faceRight = true;            // 真上・真下にいる時は前の向きを保つ（ちらつき防止）
    if (dx < -0.05f) m_faceRight = false;
}

// 形の中心のワールド座標（2*2 ならマスの間に来る）
void Enemy::CenterWorld(int col, int row, GridMap* gridMap, float& x, float& z) const
{
    float sc = 0, sr = 0;
    for (auto& [dc, dr] : m_gridShape) { sc += dc; sr += dr; }
    float n = (float)m_gridShape.size();
    x = (col + sc / n - gridMap->GetCols() / 2.0f) * 1.1f;
    z = (row + sr / n - gridMap->GetRows() / 2.0f) * 1.1f;
}

void Enemy::ClearFootprint(GridMap* gridMap)
{
    for (auto& [dc, dr] : GetGridShape())
        gridMap->SetCellType(gridCol + dc, gridRow + dr, CellType::Empty);
}

void Enemy::MarkFootprint(GridMap* gridMap)
{
    CellType t = m_isBoss ? CellType::Boss : CellType::Enemy;
    for (auto& [dc, dr] : GetGridShape())
        gridMap->SetCellType(gridCol + dc, gridRow + dr, t);
}

void Enemy::ApplyDifficulty(float hpMul, float dmgMul, int bonusActions)
{
    m_maxHP = (int)(m_maxHP * hpMul);
    m_HP = m_maxHP;
    m_displayHp = (float)m_HP;
    m_dmgScale = dmgMul;
    m_bonusActions = bonusActions;
}

bool Enemy::OccupiesCell(int col, int row) const
{
    for (auto& [dc, dr] : m_gridShape)
        if (gridCol + dc == col && gridRow + dr == row) return true;
    if (m_isSnake)
        for (auto& b : m_bodyCells)
            if (b.first == col && b.second == row) return true;
    return false;
}
