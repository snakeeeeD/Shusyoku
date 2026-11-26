#include "CardEffect.h"
#include "BattleScene.h"
#include "CardTooltip.h"
#include "TextureLoader.h"
#include "EffectManager.h"
#include "RelicManager.h"
#include "CardExecutor.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "HighlightPalette.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "TurnBanner.h"
#include "DamageFeedback.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "Cursor.h"
#include "Audio.h"
#include "Hint.h"
#include "Telemetry.h"
#include "EnemyIntentVisual.h"
#include "Faith.h"

#include <algorithm>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// キャラまわりの描画（デコイ・注目の枠・背水・本体・聖地の光・大蛇）

// 大蛇の体（尾〜頭をつないで描く）と光るウロコ
void BattleScene::DrawSnakes()
{
    m_renderer3D->SetDepthWrite(false);
    for (auto enemy : m_enemies)
    {
        if (!enemy->IsSnake()) continue;
        std::vector<std::pair<int, int>> chain = enemy->VisualChain();    // 尾(0)…頭(last)。歩きに合わせて伸び縮みする
        int n = (int)chain.size();
        auto WX = [&](int c) { return CellWX(c); };
        auto WZ = [&](int r) { return CellWZ(r); };
        auto dirRot = [](int dx, int dy)->float {
            if (dy > 0) return 0.0f;      // 北
            if (dx > 0) return 1.5708f;  // 東
            if (dy < 0) return 3.1416f;   // 南
            return       4.7124f;         // 西
            };
        for (int i = 0; i < n; i++)
        {
            float x = WX(chain[i].first), z = WZ(chain[i].second);
            if (i == n - 1) { x = enemy->worldX; z = enemy->worldZ; }     // 頭は歩きの位置に描く
            int bdx = 0, bdy = 0, fdx = 0, fdy = 0;
            if (i > 0) { bdx = chain[i].first - chain[i - 1].first; bdy = chain[i].second - chain[i - 1].second; }
            if (i < n - 1) { fdx = chain[i + 1].first - chain[i].first; fdy = chain[i + 1].second - chain[i].second; }
            if (i == 0) { bdx = fdx; bdy = fdy; }

            const char* tex; float rot; float size = 1.12f;
            if (i == n - 1)   // 頭
            {
                tex = "snake_head"; rot = dirRot(-bdx, -bdy);
                size = 1.7f;                                  // 大きめ
                float off = (size - 1.12f) * 0.5f;            // 首を体の接合面に合わせ、頭は前方へ
                x += bdx * off;                                // 進行方向へずらす
                z += bdy * off;
            }
            else if (bdx == fdx && bdy == fdy) { tex = "snake_body"; rot = (bdx != 0) ? 1.5708f : 0.0f; } // 直線
            else   // 角
            {
                tex = "snake_corner";
                auto has = [&](int x, int y) { return (-bdx == x && -bdy == y) || (fdx == x && fdy == y); };
                rot = (has(-1, 0) && has(0, 1)) ? 3.1416f
                    : (has(0, -1) && has(-1, 0)) ? 1.5708f
                    : (has(1, 0) && has(0, -1)) ? 0.0f
                    : 4.7124f;
            }
            float cyLift = m_gridMap->GetCell(chain[i].first, chain[i].second).gameObject.worldY + 0.12f;

            // 頭が脅威中：頭と同じ回転・位置で後ろにグローを敷く（立て看板グローの平置き版）
            if (i == n - 1 && enemy->hitsPlayer && !m_turnManager.IsEnemyTurn())
            {
                float thPulse = 0.5f + 0.5f * sinf(m_highlightTimer * 6.0f);
                float gsc = size * (1.18f + 0.12f * thPulse);   // 少し大きく
                float rr = 0.05f + 0.02f * thPulse;            // 円状のにじみ半径
                float ga = (0.55f + 0.35f * thPulse) / 8.0f * 2.2f;
                XMFLOAT4 gcol(enemy->hueColor.x * 1.3f, enemy->hueColor.y * 1.3f,
                    enemy->hueColor.z * 1.3f, ga);
                auto htex = TextureManager::Get("snake_head");
                for (int k = 0; k < 8; k++)                     // 8枚を円状にズラして重ねる
                {
                    float ang = (float)k / 8.0f * 6.2831853f;
                    m_renderer3D->DrawTileEx(htex,
                        x + cosf(ang) * rr, z + sinf(ang) * rr, gsc, gsc, rot,
                        gcol, cyLift - 0.01f);                  // 頭より少し下＝後ろ
                }
            }

            m_renderer3D->SetWhiteMix(min(0.6f, 0.2f * enemy->ShedCount()));   // 脱皮するほど白く（1回20%、最大60%）
            m_renderer3D->DrawTileEx(TextureManager::Get(tex), x, z, size, size, rot,
                XMFLOAT4(1, 1, 1, 1), cyLift);
            m_renderer3D->SetWhiteMix(0.0f);
        }

        // 光るウロコ：殴ると2倍になる場所（脈動する緑の光）
        if (enemy->GetWeakCell().first >= 0)
        {
            auto [wc, wr] = enemy->GetWeakCell();
            float pulse = 0.65f + 0.35f * sinf(m_highlightTimer * 6.0f);
            m_renderer3D->SetAdditive(true);
            m_renderer3D->DrawTile(TextureManager::Get("faith_aura"), WX(wc), WZ(wr), 1.25f,
                XMFLOAT4(0.4f, 1.0f, 0.3f, pulse), m_gridMap->GetCell(wc, wr).gameObject.worldY + 0.15f);
            m_renderer3D->SetAdditive(false);
        }
    }
    m_renderer3D->SetDepthWrite(true);
}

// キャラまわり（デコイ・×・注目の枠・背水・狙われている光・本体・聖地の光）。描く順番と、深度・加算の切り替えだけ
void BattleScene::DrawUnits()
{
    DrawDecoy();
    DrawBlockedCells();
    DrawFocusCorners();

    m_renderer3D->SetDepthEnabled(false);
    DrawLowHpAura();
    DrawThreatGlow();
    m_renderer3D->SetDepthEnabled(true);

    // プレイヤーと敵：透明な四角が互いを隠さないよう、深度書き込みOFF＋奥→手前で描く
    // （プレイヤーだけ深度を書くと、透明な部分が奥の敵の足元を隠して埋まって見える）
    m_renderer3D->SetDepthWrite(false);
    std::vector<Enemy*> drawOrder(m_enemies.begin(), m_enemies.end());
    std::sort(drawOrder.begin(), drawOrder.end(),
        [](Enemy* a, Enemy* b) { return a->worldZ < b->worldZ; });   // 奥(小Z)を先に、手前(大Z)を後に
    DrawCharacters(drawOrder);

    // 聖地に立っている敵：手前に立ちのぼる光（敵の絵にかぶせて「焼かれる」と一目で分かるように）
    m_renderer3D->SetDepthEnabled(false);
    m_renderer3D->SetAdditive(true);
    DrawHolyGlow(drawOrder);
    m_renderer3D->SetAdditive(false);
    m_renderer3D->SetDepthEnabled(true);
    m_renderer3D->SetDepthWrite(true);
}

// デコイ（かかし）
void BattleScene::DrawDecoy()
{
    if (m_decoyCol < 0) return;
    float x = CellWX(m_decoyCol);
    float z = CellWZ(m_decoyRow);
    m_renderer3D->DrawBillboard(TextureManager::Get("kakashi"), x, 0.05f, z , 0.8f, 0.8f, 0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
}

// 移動できないマスの×マーク（深度テストOFF、敵より先に描く）
void BattleScene::DrawBlockedCells()
{
    auto& blockedCells = m_highlighter.GetOutOfRangeCells();
    if (!blockedCells.empty())
    {
        m_renderer3D->SetDepthEnabled(false);
        for (auto& [col, row] : blockedCells)
        {
            float cx = CellWX(col);
            float cz = CellWZ(row);

            m_renderer3D->DrawTileEx(m_whiteTexture, cx, cz,
                0.7f, 0.06f, XM_PIDIV4,
                XMFLOAT4(0.8f, 0.2f, 0.2f, 0.7f));
            m_renderer3D->DrawTileEx(m_whiteTexture, cx, cz,
                0.7f, 0.06f, -XM_PIDIV4,
                XMFLOAT4(0.8f, 0.2f, 0.2f, 0.7f));
        }
        m_renderer3D->SetDepthEnabled(true);
    }
}

// 注目中の敵を識別色のコーナーマークで囲む
void BattleScene::DrawFocusCorners()
{
    int target = FocusEnemy();
    if (target >= 0 && target < (int)m_enemies.size())
    {
        Enemy* e = m_enemies[target];
        m_renderer3D->SetDepthEnabled(false);
        XMFLOAT4 line(e->hueColor.x, e->hueColor.y, e->hueColor.z, 1.0f);
        const float H = 0.5f, L = 0.18f, T = 0.05f;   // 半径・角の長さ・太さ

        for (auto& [c, r] : e->OccupiedCells())
        {
            float cx = CellWX(c);
            float cz = CellWZ(r);
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sz = -1; sz <= 1; sz += 2)
                {
                    m_renderer3D->DrawTileEx(m_whiteTexture,
                        cx + sx * (H - L / 2), cz + sz * H, L, T, 0.0f, line);
                    m_renderer3D->DrawTileEx(m_whiteTexture,
                        cx + sx * H, cz + sz * (H - L / 2), T, L, 0.0f, line);
                }
        }
        m_renderer3D->SetDepthEnabled(true);
    }
}

// 背水：HPが減ると赤く脈動・赤い残像（火事場＝1/2以下、決死＝1/4以下の間）
void BattleScene::DrawLowHpAura()
{
    int hp = m_player->GetHp(), mhp = m_player->GetMaxHp();
    bool hasLast = m_player->GetBuffManager().HasBuff(BuffType::LastStand);   // 1/2パワー
    bool hasDeep = m_player->GetBuffManager().HasBuff(BuffType::DeepStand);   // 1/4パワー
    float pulse = 0.5f + 0.5f * sinf(m_highlightTimer * 10.0f);               // 0..1 脈動

    // 1/2以下：プレイヤーが赤く脈動（膨らむ赤オーラ）
    if (hp * 2 <= mhp && hasLast)
    {
        float sc = 1.0f + 0.15f * pulse;
        float a = 0.20f + 0.30f * pulse;
        Player::SpriteFrame f = m_player->GetSprite(sc);   // 本体と同じ向き・コマ
        m_renderer3D->DrawBillboard(f.tex, f.x, f.y, f.z, f.w, f.h, f.rot,
            XMFLOAT4(1.0f, 0.2f, 0.15f, a), f.uv);
    }

    // 1/4以下：赤い残像（1/2パワーがあれば残像自体が膨張脈動）
    if (hp * 4 <= mhp && hasDeep)
    {
        float sc = 1.0f + (hasLast ? 0.25f * pulse : 0.0f);   // 1/2パワーで膨らむ脈動
        Player::SpriteFrame f = m_player->GetSprite(sc);   // 本体と同じ向き・コマ（残像の3枚で共通）
        for (int i = 1; i <= 3; i++)
        {
            float ph = m_highlightTimer * 14.0f + i * 1.2f;
            float ox = sinf(ph) * 0.10f * i;
            float oz = -0.09f * i;
            float a = 0.30f - (i - 1) * 0.08f;
            m_renderer3D->DrawBillboard(f.tex, f.x + ox, f.y, f.z + oz, f.w, f.h, f.rot,
                XMFLOAT4(1.0f, 0.25f, 0.15f, a), f.uv);   // ← サイズscで脈動
        }
    }
}

// 攻撃範囲にプレイヤーが入っている敵を、脈動グロー（疑似ぼかし）で強調（自分のターンだけ）。プレイヤー・敵より先に描く
void BattleScene::DrawThreatGlow()
{
    if (!m_turnManager.IsEnemyTurn())
    {
        float thPulse = 0.5f + 0.5f * sinf(m_highlightTimer * 6.0f);   // 0..1
        const int N = 8;                                               // 重ね枚数＝滑らかさ
        for (auto enemy : m_enemies)
        {
            if (!enemy->hitsPlayer || enemy->GetHp() <= 0) continue;
            if (enemy->IsSnake()) continue;   // 蛇は頭タイル側で回転付きグローを出す
            auto tex = TextureManager::Get(enemy->GetTextureName());
            float sc = 1.18f + 0.12f * thPulse;
            float r = 0.05f + 0.02f * thPulse;                        // ぼかし半径
            float a = (0.55f + 0.35f * thPulse) / N * 2.2f;           // 重ね用に薄める
            float cy = enemy->worldY - enemy->height * (sc - 1.0f) * 0.5f;
            for (int k = 0; k < N; k++)                                // 円状にズラして重ねる
            {
                float ang = (float)k / N * 6.2831853f;
                float ox = cosf(ang) * r, oy = sinf(ang) * r;
                m_renderer3D->DrawBillboard(tex,
                    enemy->worldX + ox, cy + oy, enemy->worldZ + 0.10f,
                    enemy->width* sc, enemy->height* sc, 0.0f,
                    XMFLOAT4(enemy->hueColor.x * 1.3f,
                        enemy->hueColor.y * 1.3f,
                        enemy->hueColor.z * 1.3f, a),
                    enemy->FacingUV());                 // ← 本体と同じ向きで光らせる
            }
        }
    }
}

// プレイヤーと敵の本体を、奥から手前へ（プレイヤーより手前の敵の前にプレイヤーを描く）
void BattleScene::DrawCharacters(const std::vector<Enemy*>& drawOrder)
{
    bool playerDrawn = false;
    for (auto enemy : drawOrder)
    {
        if (!playerDrawn && enemy->worldZ > m_player->worldZ)   // プレイヤーより手前の敵の前に、プレイヤーを描く
        {
            m_player->Draw3D(m_renderer3D);
            playerDrawn = true;
        }
        enemy->Draw3D(m_renderer3D);
    }
    if (!playerDrawn) m_player->Draw3D(m_renderer3D);
}

// 聖地に立っている敵：手前に立ちのぼる光（敵の絵にかぶせて「焼かれる」と一目で分かるように）
void BattleScene::DrawHolyGlow(const std::vector<Enemy*>& drawOrder)
{
    for (auto enemy : drawOrder)
    {
        if (!enemy || enemy->GetHp() <= 0) continue;
        int best = 0;
        for (auto& c : enemy->OccupiedCells())
            if (int f = Faith::At(m_gridMap, c.first, c.second); f > best) best = f;
        if (best <= 0) continue;
        float flick = 0.85f + 0.15f * sinf(m_highlightTimer * 3.0f + enemy->worldX);   // 敵ごとに少しずらして明滅
        float h = 1.2f + enemy->height * 0.6f;
        m_renderer3D->DrawBillboard(TextureManager::Get("faith_pillar"),
            enemy->worldX, h * 0.5f, enemy->worldZ, enemy->width * 0.9f, h, 0.0f,
            XMFLOAT4(1.0f, 0.82f, 0.4f, (0.25f + 0.05f * best) * flick));
    }
}
