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

// 地面に貼る印（信仰・敵の範囲・自分の範囲・罠）

// 地面に貼る印（信仰・敵の範囲・自分の範囲）。描く順番と、深度・加算の切り替えだけ
void BattleScene::DrawFloorMarks()
{
    // 地面に貼る印（信仰・敵の範囲・自分の範囲・罠）も深度を書かない。浮いたマスの印が奥の敵を隠さないように
    DrawFaithBase();
    DrawThreatStripes();
    m_renderer3D->SetDepthEnabled(false);
    DrawThreatOutlines();
    m_renderer3D->SetDepthEnabled(true);
    m_renderer3D->SetAdditive(true);
    m_renderer3D->SetDepthWrite(false);
    DrawFaithSigils();
    // （深度は書かないまま：プレイヤーの攻撃範囲マーカー・罠も地面の印）
    m_renderer3D->SetAdditive(false);
    DrawReachMarkers();
}

// 信仰のマスの金色の下地（敵の範囲より先に・低く描く＝重なっても範囲の模様が上に見える）
void BattleScene::DrawFaithBase()
{
    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            int f = m_gridMap->GetCell(col, row).faith;
            if (f <= 0) continue;
            float x = CellWX(col);
            float z = CellWZ(row);
            float wy = m_gridMap->GetCell(col, row).gameObject.worldY;
            float pulse = Faith::IsSanctuary(f) ? 0.5f + 0.5f * sinf(m_highlightTimer * 2.0f) : 0.0f;
            m_renderer3D->DrawTile(m_whiteTexture, x, z, 0.96f,
                XMFLOAT4(1.0f, 0.82f, 0.3f, 0.10f + 0.025f * f + 0.10f * pulse), wy + 0.008f);   // 下地（範囲の下）
        }
}

// 敵の攻撃範囲：所有敵の色で、敵ごとに違う模様のしましまを薄く塗る（どの敵の範囲か、色が近くても模様で分かる）
void BattleScene::DrawThreatStripes()
{
    //   絵：ui_hazard＝／の斜線、ui_hazard_line＝横じま。90度回すと＼・縦になる。×は斜線を2枚重ねる
    static const struct { const char* tex; float rot; bool cross; } STRIPES[5] = {
        { "ui_hazard",      XM_PIDIV2, false },   // 1体目 ＼
        { "ui_hazard",      0.0f,      false },   // 2体目 ／
        { "ui_hazard_line", 0.0f,      false },   // 3体目 ─
        { "ui_hazard_line", XM_PIDIV2, false },   // 4体目 │
        { "ui_hazard",      0.0f,      true  },   // 5体目 ×
    };
    for (auto& mk : m_highlighter.GetThreatMarks())
    {
        float x = CellWX(mk.col);
        float z = CellWZ(mk.row);
        float wy = m_gridMap->GetCell(mk.col, mk.row).gameObject.worldY + 0.02f;
        int layers = 0;
        for (int k = 0; k < 5; k++) if (mk.patterns & (1 << k)) layers++;
        XMFLOAT4 col = mk.color;
        if (layers >= 2) col.w *= 0.7f;   // 重なるマスは全員の模様を重ねる。濃くなりすぎないよう1枚ずつ少し薄く
        for (int k = 0; k < 5; k++)
        {
            if (!(mk.patterns & (1 << k))) continue;
            const auto& st = STRIPES[k];
            ID3D11ShaderResourceView* tex = TextureManager::Get(st.tex);
            if (st.cross)   // 格子：＼と／を少し薄くして重ねる
            {
                XMFLOAT4 c = col; c.w *= 0.6f;
                m_renderer3D->DrawTileEx(tex, x, z, 0.94f, 0.94f, 0.0f, c, wy);
                m_renderer3D->DrawTileEx(tex, x, z, 0.94f, 0.94f, XM_PIDIV2, c, wy);
            }
            else
                m_renderer3D->DrawTileEx(tex, x, z, 0.94f, 0.94f, st.rot, col, wy);
        }
    }
}

// 敵ごとの色で範囲の外周線（内側の線ほど短く。重なる辺はずらして両方見せる）
void BattleScene::DrawThreatOutlines()
{
    const float HALF = 0.5f, T = 0.07f;
    const float BASE = HALF - T * 0.5f;    // 一番外側の線の内寄せ量
    const float LEN = 1.0f - T;            // 一番外側の線の長さ
    std::map<int, int> edgeStack;          // 同じ辺に既に何本引いたか
    for (auto& e : m_highlighter.GetThreatEdges())
    {
        int key = (e.col * 64 + e.row) * 4 + e.dir;
        int k = edgeStack[key]++;           // この辺での重なり順（0,1,2...）
        float inset = BASE - k * T;         // 重なるごとに内側へ
        float len = LEN - 2.0f * T * k;     // 内側ほど短く（角が入れ子になる）
        if (len < T) len = T;               // 短くなりすぎ防止
        float cx = CellWX(e.col);
        float cz = CellWZ(e.row);
        if (e.dir == 0)      m_renderer3D->DrawTileEx(m_whiteTexture, cx, cz - inset, len, T, 0.0f, e.color);
        else if (e.dir == 1) m_renderer3D->DrawTileEx(m_whiteTexture, cx, cz + inset, len, T, 0.0f, e.color);
        else if (e.dir == 2) m_renderer3D->DrawTileEx(m_whiteTexture, cx - inset, cz, T, len, 0.0f, e.color);
        else                 m_renderer3D->DrawTileEx(m_whiteTexture, cx + inset, cz, T, len, 0.0f, e.color);
    }
}

// 信仰の紋章とオーラ：敵の範囲の上に加算合成で重ねる（範囲の模様を隠さず、上から光って見える）
void BattleScene::DrawFaithSigils()
{
    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            int f = m_gridMap->GetCell(col, row).faith;
            if (f <= 0) continue;
            float x = CellWX(col);
            float z = CellWZ(row);
            float wy = m_gridMap->GetCell(col, row).gameObject.worldY;
            float pulse = Faith::IsSanctuary(f) ? 0.5f + 0.5f * sinf(m_highlightTimer * 2.0f) : 0.0f;
            // オーラ：ゆっくり呼吸する（マスの位置でタイミングをずらし、光の波が盤面を伝わるように）
            float breath = 0.5f + 0.5f * sinf(m_highlightTimer * 1.6f - (col + row) * 0.6f);
            m_renderer3D->DrawTile(TextureManager::Get("faith_aura"), x, z, 1.18f + 0.12f * breath,
                XMFLOAT4(1.0f, 0.78f, 0.35f, (0.18f + 0.05f * f) * (0.6f + 0.4f * breath) + 0.15f * pulse), wy + 0.024f);
            char tex[16]; sprintf_s(tex, "faith_%d", f);
            m_renderer3D->DrawTile(TextureManager::Get(tex), x, z, 0.92f,
                XMFLOAT4(1.0f, 0.8f, 0.35f, 0.9f + 0.1f * pulse), wy + 0.025f);
        }
}

// 自分の攻撃範囲のマーカー（水色ブラケット＋クロスヘア）
void BattleScene::DrawReachMarkers()
{
    for (auto& pc : m_highlighter.GetReachCells())
    {
        float x = CellWX(pc.first);
        float z = CellWZ(pc.second);
        float wy = m_gridMap->GetCell(pc.first, pc.second).gameObject.worldY + 0.03f;
        m_renderer3D->DrawTile(TextureManager::Get("ui_reach"), x, z, 0.98f, XMFLOAT4(1, 1, 1, 1), wy);
    }
}

// 罠（設置者の下地・育ち・導火線）
void BattleScene::DrawTraps()
{
    for (int row = 0; row < m_gridMap->GetRows(); row++)
    {
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            auto& cell = m_gridMap->GetCell(col, row);
            if (cell.tileEffect.active)
            {
                float x = CellWX(col);
                float z = CellWZ(row);
                TileDropFx::Look drop = m_tileDrop.Get(col, row, cell.tileEffect.id);   // 置かれたばかりなら、上から落ちてくる途中
                if (drop.hidden) continue;                           // まだ落ち始めていない

                float t = cell.gameObject.worldY / 0.10f;

                // ハイライト中（起爆カード選択で浮いている）の罠を脈動させる
                float pulseScale = 1.0f, bright = 1.0f;
                if (cell.gameObject.worldY > 0.05f)
                {
                    float s = 0.5f + 0.5f * sinf(m_highlightTimer * 8.0f);   // 0..1
                    pulseScale = 1.0f + 0.18f * s;                           // サイズ 1.0..1.18
                    bright = 1.0f + 0.5f * s;                           // 明るさ 1.0..1.5
                }

                const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
                XMFLOAT4 terrainColor = def ? def->color : XMFLOAT4(1, 1, 1, 0.5f);
                float sz = (0.8f + 0.064f * t) * pulseScale * drop.scale;
                float lift = cell.gameObject.worldY + drop.lift;   // 床・罠を描く高さ（落ちてくる途中は上）
                if (drop.shadow > 0.0f)   // 落ちてくる間、マスに影（だんだん濃く大きく）
                    m_renderer3D->DrawTile(TextureManager::Get("particle"), x, z, sz * (0.5f + 0.5f * drop.shadow),
                        XMFLOAT4(0, 0, 0, 0.35f * drop.shadow), cell.gameObject.worldY + 0.03f);
                if (!cell.tileEffect.enemyOwned)   // 育った罠は少し大きく・明るく
                {
                    sz *= 1.0f + 0.05f * cell.tileEffect.grown;
                    bright += 0.12f * cell.tileEffect.grown;
                }

                // 導火線：たまった数ほど速く、ピッピッと点滅（1つ＝1秒ごと、2つ＝0.5秒、3つ＝0.25秒）。光る瞬間は罠の絵も赤く
                float beep = 0.0f;
                if (!cell.tileEffect.enemyOwned && cell.tileEffect.fuse > 0)
                {
                    float period = 1.0f / (float)(1 << (cell.tileEffect.fuse - 1));   // 1, 0.5, 0.25
                    beep = (fmodf(m_highlightTimer, period) / period < 0.35f) ? 1.0f : 0.0f;   // 1回の間隔の最初の35%だけ光る
                }

                if (def && !def->texture.empty())
                    m_renderer3D->DrawTileEx(TextureManager::Get(def->texture), x, z, sz, sz, XM_PI,   // カメラが逆向きなので回す
                        XMFLOAT4(bright, bright * (1.0f - 0.45f * beep), bright * (1.0f - 0.55f * beep), drop.alpha), lift + 0.04f);
                else
                    m_renderer3D->DrawTile(m_whiteTexture, x, z, sz,
                        XMFLOAT4(terrainColor.x * bright, terrainColor.y * bright, terrainColor.z * bright, terrainColor.w * drop.alpha),
                        lift + 0.04f);

                // 導火線：手前の縁に点を3つ。たまった数だけ赤橙に灯す（敵が隣にいる間だけ進む）
                if (!cell.tileEffect.enemyOwned && cell.tileEffect.fuse > 0)
                    for (int k = 0; k < 3; k++)
                    {
                        bool lit = k < cell.tileEffect.fuse;
                        float fl = lit ? (beep > 0.0f ? 1.3f : 0.55f) : 1.0f;   // 灯った点は、ピッで明るく・それ以外は暗めに
                        m_renderer3D->DrawTile(m_whiteTexture, x + (k - 1) * 0.16f, z + sz * 0.42f, 0.1f,
                            lit ? XMFLOAT4(1.0f * fl, 0.45f * fl, 0.1f, 1.0f) : XMFLOAT4(0.15f, 0.12f, 0.1f, 0.9f),
                            lift + 0.05f);
                    }
            }
        }
    }
}
