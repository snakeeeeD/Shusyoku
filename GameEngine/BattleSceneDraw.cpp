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

// BattleScene の描画（Draw とその下の関数）。状態はシーンのものをそのまま読むので、クラスは分けずにファイルだけ分ける

static constexpr float RAISE_H = 0.12f;   // 狙ったマスが浮く高さ（上げすぎると奥のマスに食い込んで見える）

// 描く順番を並べるだけ（中身は下の関数）。奥・地面から順に重ねる
void BattleScene::Draw()
{
    RequestCellCursor();                 // 使えるマス・手札の上なら指差し
    bool blurScene = (m_awakenCinematic > 0.0f);
    if (blurScene) m_renderer3D->BeginOffscreen();   // 覚醒演出：ぼかすためにいったん別の絵に描く
    DrawBackground2D();

    m_renderer3D->Begin();
    UpdateCellHeights();                 // 狙うマスを浮かせる＋その上に立つキャラの高さ（描く前に）
    const BattleTheme th = CurrentTheme();
    DrawGround(th);                      // 地面のタイル・装飾と影
    DrawCells(th);                       // マス
    DrawFloorMarks();                    // 信仰・敵の範囲と外周線・自分の範囲
    DrawTraps();
    DrawSnakes();
    DrawUnits();                         // 身代わり・×印・注目の枠・背水のオーラ・脅威のグロー・キャラ・聖地の光
    DrawOverlays();                      // エフェクト・必中のクロスヘア・移動先のゴースト・L字枠
    m_renderer3D->End();

    if (blurScene) DrawAwakenCinematic();   // 集中線・ブラー・ボスだけくっきり・円
    BattleUIContext ctx = MakeUIContext();
    m_battleUI->Draw(ctx);
    m_battleUI->DrawAddedCardsTop(ctx);
}

// マウスカーソル：手札のカード／今離したら使えるマスの上なら指差し（ボタン類は UiHover・HoverLift 側で出る）
void BattleScene::RequestCellCursor()
{
    if (m_battleResult == BattleResult::None && m_turnManager.IsPlayerTurn())
    {
        bool hov = false;
        int sel = m_selectedCardIndex;
        if (sel >= 0 && sel < (int)m_hand.GetCards().size())
        {
            CardData d = *m_hand.GetCards()[sel]->GetData();
            ApplyKnifeThrow(d, m_player);
            if (d.type == CardType::Move)
                hov = !m_movePath.empty() && m_movePath.back() == m_hoveredCell;   // 経路の先＝離せばそこへ行ける
            else if (SelectedNeedsTarget())
                hov = m_hoveredCell.first >= 0 && CellInCardRange(d, m_hoveredCell.first, m_hoveredCell.second);
        }
        else hov = (m_hoveredCardIndex >= 0);   // 何も持っていない時は手札のカード
        if (hov) Cursor::Request(CursorKind::Hover);
    }
}

// 2Dの背景（3Dの地面の後ろ）
void BattleScene::DrawBackground2D()
{
    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(
        TextureManager::Get("battle_bg"),
        0.0f, 0.0f,
        (float)m_screenWidth, (float)m_screenHeight,
        0.0f, XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f));
    m_spriteRenderer->End();
}

// 選んだカードで狙うマスを浮かせる（滑らかに）。描画の前に毎フレーム
void BattleScene::UpdateCellHeights()
{
    std::set<std::pair<int, int>> raised;
    if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size())
    {
        CardData dThrow = *m_hand.GetCards()[m_selectedCardIndex]->GetData();
        ApplyKnifeThrow(dThrow, m_player);
        const CardData* d = &dThrow;
        auto raiseTrapsInRange = [&]() {   // 範囲内の自分の罠（起爆系3種で共通。敵床は光らせない）
            for (auto& cc : BattleHighlighter::GetCandidates(m_playerCol, m_playerRow, d->rangeType, d->range))
            {
                if (cc.first < 0 || cc.first >= m_gridMap->GetCols() ||
                    cc.second < 0 || cc.second >= m_gridMap->GetRows()) continue;
                auto& hc = m_gridMap->GetCell(cc.first, cc.second);
                if (hc.tileEffect.active && !hc.tileEffect.enemyOwned)
                    raised.insert(cc);
            }
        };
        // 敵1体・アタックは、ホバーしていなくても敵マスを狙い先にする（ホバー時と同じ浮き演出）
        std::pair<int, int> hov = m_hoveredCell;
        // 攻撃は「今離したら当たる敵」の占有マスを浮かせる（使用時と同じ判定）
        if (const Enemy* t = AttackTarget())
        {
            if (!CellInCardRange(*d, hov.first, hov.second))
                hov = { t->gridCol, t->gridRow };   // 射程内マス以外は狙う敵へ
            for (auto& c : t->OccupiedCells()) raised.insert(c);
        }
        if (d->type == CardType::Move)
        {
            for (auto& p : m_movePath) raised.insert(p);
        }
        else if (d->rangeType == RangeType::Cone)
        {
            int range = d->range;
            if (m_player->GetBuffManager().HasBuff(BuffType::Reposition))
                range += m_player->GetBuffManager().GetBuffValue(BuffType::Reposition);

            int aimDx = 0, aimDy = 0;
            if (hov.first >= 0)
                RangeShape::CardinalAim(m_playerCol, m_playerRow,
                    hov.first, hov.second, aimDx, aimDy);
            if (aimDx == 0 && aimDy == 0) aimDy = -1;   // 初期は上向き

            for (auto& c : BattleHighlighter::GetCandidates(
                m_playerCol, m_playerRow, d->rangeType, range, aimDx, aimDy))
                raised.insert(c);
        }
        else if (d->mainEffect.type == CardEffectType::Detonate)
        {
            raiseTrapsInRange();
        }
        else if (d->mainEffect.type == CardEffectType::DetonateAt)
        {
            // 置いてある罠（範囲内）をハイライト
            raiseTrapsInRange();

            // 3x3プレビュー
            if (m_hoveredCell.first >= 0 &&
                CellInCardRange(*d, m_hoveredCell.first, m_hoveredCell.second) &&
                m_gridMap->GetCell(m_hoveredCell.first, m_hoveredCell.second).tileEffect.active)
            {
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++)
                        raised.insert({ m_hoveredCell.first + dc, m_hoveredCell.second + dr });
            }
        }
        else if (d->mainEffect.type == CardEffectType::DetonateChain)
        {
            // 置いてある罠（範囲内）をハイライト
            raiseTrapsInRange();

            // 連鎖の起点プレビュー（既存のまま）
            if (m_hoveredCell.first >= 0 &&
                CellInCardRange(*d, m_hoveredCell.first, m_hoveredCell.second) &&
                m_gridMap->GetCell(m_hoveredCell.first, m_hoveredCell.second).tileEffect.active)
            {
                raised.insert({ m_hoveredCell.first, m_hoveredCell.second });
            }
        }
        else if (d->mainEffect.type == CardEffectType::PlaceTrapArea)
        {
            for (auto& cc : BattleHighlighter::GetCandidates(
                m_playerCol, m_playerRow, d->rangeType, d->range))
                raised.insert(cc);
        }
        else if ((d->type == CardType::Skill || d->type == CardType::Power)
            && !PicksCell(*d)
            && d->mainEffect.type != CardEffectType::Detonate)
        {
            raised.insert({ m_playerCol, m_playerRow });
            m_gridMap->GetCell(m_playerCol, m_playerRow).gameObject.color
                = HighlightPalette::ForCard(d->type);   // 危険色よりスキル色を優先
        }
        else if (hov.first >= 0)
        {
            if (CellInCardRange(*d, hov.first, hov.second))   // 射程の判定は使用時と同じ（足元も上がる）
                raised.insert({ hov.first, hov.second });
        }
        // 起爆系：足元の罠も、ほかの罠と同じように上げる
        if (d->mainEffect.type == CardEffectType::Detonate
            || d->mainEffect.type == CardEffectType::DetonateAt
            || d->mainEffect.type == CardEffectType::DetonateChain)
        {
            auto& own = m_gridMap->GetCell(m_playerCol, m_playerRow);
            if (own.tileEffect.active && !own.tileEffect.enemyOwned)
                raised.insert({ m_playerCol, m_playerRow });
        }

        // 全敵効果カード：全敵マスも範囲としてハイライト
        if (d->allEnemyEffect.hasEffect)
            for (auto en : m_enemies)
                if (en && en->GetHp() > 0)
                    for (auto& [dc, dr] : en->GetGridShape())
                        raised.insert({ en->gridCol + dc, en->gridRow + dr });
    }


    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            auto& cell = m_gridMap->GetCell(col, row);
            float target = raised.count({ col, row }) ? RAISE_H : 0.0f;
            cell.gameObject.worldY += (target - cell.gameObject.worldY) * 0.2f;   // 補間
        }

    // その上に立つキャラの高さ（敵は占有マスの一番高い浮きに合わせる）
    for (auto enemy : m_enemies)
    {
        auto safeY = [&](int c, int r) -> float {
            if (c < 0 || c >= m_gridMap->GetCols() || r < 0 || r >= m_gridMap->GetRows()) return 0.0f;
            return m_gridMap->GetCell(c, r).gameObject.worldY;
            };
        float maxY = safeY(enemy->gridCol, enemy->gridRow);
        if (enemy->IsSnake())
            for (auto& b : enemy->GetBodyCells())
                maxY = max(maxY, safeY(b.first, b.second));
        else
            for (auto& [dc, dr] : enemy->GetGridShape())
                maxY = max(maxY, safeY(enemy->gridCol + dc, enemy->gridRow + dr));
        enemy->worldY = 0.05f + maxY;
    }

    m_player->worldY = m_gridMap->GetCell(m_playerCol, m_playerRow).gameObject.worldY;
}

// 背景テーマ（層＋ボスで地面・装飾・マスの色を決める）。層やボスを足す時はここに1つ
BattleScene::BattleTheme BattleScene::CurrentTheme() const
{
    int themeLayer = PlayerDataManager::GetData().layer;
    BattleTheme th;
    if (m_category == EncCategory::Boss && m_battleEnemyId == "l3boss")
    {
        if (m_bossAwakened)
            th = { "ground_void_awake", { "deco_rift","deco_debris","deco_throne" }, { 2.6f,2.4f,3.4f }, { 3.2f,2.2f,4.2f },
                   "cell_stone", 4, { 0.60f, 0.42f, 0.52f, 0.92f } };
        else
            th = { "ground_void",       { "deco_rift","deco_debris","deco_throne" }, { 2.6f,2.4f,3.4f }, { 3.2f,2.2f,4.2f },
                   "cell_stone", 4, { 0.52f, 0.48f, 0.62f, 0.92f } };
    }
    else if (m_category == EncCategory::Boss && m_battleEnemyId == "l2boss")
        th = { "ground_snakepit", { "deco_darkspike","deco_bones","deco_egg" }, { 2.6f,2.4f,2.6f }, { 3.8f,3.4f,2.1f },
               "cell_stone", 4, { 0.50f, 0.56f, 0.44f, 0.92f } };
    else if (m_category == EncCategory::Boss)
        th = { "ground_scorched", { "deco_deadtree","deco_ember","deco_darkrock" }, { 2.8f,2.6f,2.6f }, { 3.8f,2.1f,2.1f },   // dragon(1層)
               "cell_stone", 4, { 0.55f, 0.47f, 0.43f, 0.92f } };
    else if (themeLayer <= 1)
        th = { "ground_grass",    { "deco_tree","deco_bush","deco_rock" },          { 3.0f,2.4f,2.6f }, { 3.8f,1.8f,2.1f },
               "cell_stone", 4, { 0.80f, 0.76f, 0.66f, 0.92f } };
    else if (themeLayer == 2)
        th = { "ground_cave",     { "deco_stalagmite","deco_crystal","deco_caverock" }, { 2.0f,2.4f,2.6f }, { 3.8f,3.4f,2.1f },
               "cell_stone", 4, { 0.60f, 0.66f, 0.78f, 0.92f } };
    else
        th = { "ground_future",   { "deco_pillar","deco_terminal","deco_techblock" }, { 1.9f,2.6f,2.6f }, { 3.8f,3.0f,2.2f },
               "cell_panel", 1, { 0.62f, 0.70f, 0.78f, 0.92f } };
    return th;
}

// 地面のタイルと背景の装飾（影つき）
void BattleScene::DrawGround(const BattleTheme& th)
{
    // === 接地用の地面（見下ろし草地をタイル状に敷く） ===
    {
        const float TILE = 6.0f;   // 1枚のワールドサイズ（テクスチャ1回分）
        const int   N = 6;         // 中心から±N枚（(2N+1)^2枚）
        for (int gz = -N; gz <= N; gz++)
            for (int gx = -N; gx <= N; gx++)
                m_renderer3D->DrawTile(TextureManager::Get(th.ground),
                    gx * TILE, gz * TILE, TILE * 1.02f, XMFLOAT4(1, 1, 1, 1), -0.06f);
    }

    // 背景装飾（透明quadがdepthを書いて四角化するのを防ぐため depth-write OFF）
    m_renderer3D->SetDepthWrite(false);
    for (auto& dc : m_decos)
    {
        const char* tex = th.tex[dc.type];
        float w = th.w[dc.type] * dc.scale;
        float h = th.h[dc.type] * dc.scale;
        // 光を浴びて斜めに伸びる影（本体シルエットをシアー投影。足元固定・頭が倒れる）
        float lenX = -h * 0.45f, lenZ = -h * 0.7f;  // 斜めの向き×長さ
        float dl = sqrtf(lenX * lenX + lenZ * lenZ);
        float tuck = -0.1f;                          // 固定の食い込み量（全種共通・ワールド単位）
        float bx = dc.x ;
        float bz = dc.z - (lenZ / dl) * tuck;
        m_renderer3D->DrawShadow(TextureManager::Get(tex),
            bx, bz, w, lenX, lenZ, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.30f));

        // 本体
        m_renderer3D->DrawBillboard(TextureManager::Get(tex),
            dc.x, -0.10f, dc.z, w, h, 0.0f, XMFLOAT4(1, 1, 1, 1));  // y=中心ではなく足元(地面)
    }
}

// マス（浮いたマスは暗い台を残して厚みに見せる）
void BattleScene::DrawCells(const BattleTheme& th)
{
    // マスの地面は深度を書かない：手前のマスが浮いても、奥に立つ敵やプレイヤーを隠さない（めり込み防止）。
    // 奥の行から順に描くので、マス同士の重なりは描く順で正しくなる（depth-write は装飾から OFF のまま）

    // 明るくするのは「何も表示していない空マス」だけ（範囲・ハイライトは素の色のまま）
    {
        int cols = m_gridMap->GetCols(), rows = m_gridMap->GetRows();
        std::vector<char> shown(cols * rows, 0);
        for (auto& mk : m_highlighter.GetThreatMarks())
            if (mk.col >= 0 && mk.col < cols && mk.row >= 0 && mk.row < rows)
                shown[mk.row * cols + mk.col] = 1;
        for (auto& p : m_highlighter.GetPlayerHighlightCells())
            if (p.first >= 0 && p.first < cols && p.second >= 0 && p.second < rows)
                shown[p.second * cols + p.first] = 1;

        for (int row = 0; row < rows; row++)
            for (int col = 0; col < cols; col++)
            {
                float x = (col - cols / 2.0f) * 1.1f;
                float z = (row - rows / 2.0f) * 1.1f;
                auto& cell = m_gridMap->GetCell(col, row);
                float t = cell.gameObject.worldY / 0.10f;
                float size = 1.0f + 0.02f * t;

                XMFLOAT4 cc = cell.gameObject.color;
                if (cell.type == CellType::Empty && !shown[row * cols + col])
                    cc = th.cellCol;                                    // 空マスは層の地面に合った色
                const char* ctex = th.cellTex;                          // 石板は数種をマスごとに使い分け（模様の繰り返しを消す）
                char vname[24];
                if (th.cellVariants > 1) { sprintf_s(vname, "%s_%d", th.cellTex, (col * 7 + row * 3) % th.cellVariants); ctex = vname; }
                if (cell.gameObject.worldY > 0.01f)   // 浮いたマス：元の位置に暗い台を残して、厚み（側面）に見せる
                    m_renderer3D->DrawTile(m_whiteTexture, x, z, size,
                        XMFLOAT4(cc.x * 0.4f, cc.y * 0.4f, cc.z * 0.4f, 1.0f), 0.005f);
                m_renderer3D->DrawTile(TextureManager::Get(ctex), x, z, size, cc, cell.gameObject.worldY);
            }
    }
}

// 地面に貼る印：信仰の下地・敵の範囲と外周線・信仰の紋章・自分の範囲
void BattleScene::DrawFloorMarks()
{
    // 地面に貼る印（信仰・敵の範囲・自分の範囲・罠）も深度を書かない。浮いたマスの印が奥の敵を隠さないように

    // 信仰のマス（敵の範囲より先に・低く描く＝重なっても範囲の模様が上に見える）
    // 金色の下地＋紋章（外周10区切りのうち信仰の数だけ光る。聖域は全部）
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

    // 敵の攻撃範囲：所有敵の色でハザード模様を薄く塗る
    ID3D11ShaderResourceView* rangeTex = TextureManager::Get("ui_hazard");
    for (auto& mk : m_highlighter.GetThreatMarks())
    {
        float x = CellWX(mk.col);
        float z = CellWZ(mk.row);
        float wy = m_gridMap->GetCell(mk.col, mk.row).gameObject.worldY + 0.02f;
        m_renderer3D->DrawTile(rangeTex, x, z, 0.94f, mk.color, wy);
    }

    // 敵ごとの色で範囲の外周線（内側の線ほど短く。重なる辺はずらして両方見せる）
    {
        m_renderer3D->SetDepthEnabled(false);
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
        m_renderer3D->SetDepthEnabled(true);
    }

    // 信仰の紋章：敵の範囲の上に加算合成で重ねる（範囲の模様を隠さず、上から光って見える）
    m_renderer3D->SetAdditive(true);
    m_renderer3D->SetDepthWrite(false);
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
    // （深度は書かないまま：プレイヤーの攻撃範囲マーカー・罠も地面の印）
    m_renderer3D->SetAdditive(false);

    // プレイヤーの攻撃範囲マーカー（水色ブラケット＋クロスヘア）
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
                float sz = (0.8f + 0.064f * t) * pulseScale;
                if (!cell.tileEffect.enemyOwned)   // 育った罠は少し大きく・明るく
                {
                    sz *= 1.0f + 0.05f * cell.tileEffect.grown;
                    bright += 0.12f * cell.tileEffect.grown;
                }

                // 設置者を示す下地（自分=水色 / 敵=赤）。罠の少し下・少し大きめでフチのように出す
                XMFLOAT4 ownerCol = cell.tileEffect.enemyOwned
                    ? XMFLOAT4(1.0f, 0.30f, 0.20f, 0.55f)    // 敵＝赤
                    : XMFLOAT4(0.30f, 0.75f, 1.0f, 0.55f);   // 自分＝水色
                m_renderer3D->DrawTile(m_whiteTexture, x, z, sz * 1.14f, ownerCol,
                    cell.gameObject.worldY + 0.035f);

                if (def && !def->texture.empty())
                    m_renderer3D->DrawTileEx(TextureManager::Get(def->texture), x, z, sz, sz, XM_PI,   // カメラが逆向きなので回す
                        XMFLOAT4(bright, bright, bright, 1.0f), cell.gameObject.worldY + 0.04f);
                else
                    m_renderer3D->DrawTile(m_whiteTexture, x, z, sz,
                        XMFLOAT4(terrainColor.x * bright, terrainColor.y * bright, terrainColor.z * bright, terrainColor.w),
                        cell.gameObject.worldY + 0.04f);

                // 導火線：手前の縁に点を3つ。たまった数だけ赤橙に灯す（敵が隣にいる間だけ進む）
                if (!cell.tileEffect.enemyOwned && cell.tileEffect.fuse > 0)
                    for (int k = 0; k < 3; k++)
                    {
                        bool lit = k < cell.tileEffect.fuse;
                        float fl = lit ? 0.75f + 0.25f * sinf(m_highlightTimer * 10.0f) : 1.0f;
                        m_renderer3D->DrawTile(m_whiteTexture, x + (k - 1) * 0.16f, z + sz * 0.42f, 0.1f,
                            lit ? XMFLOAT4(1.0f * fl, 0.45f * fl, 0.1f, 1.0f) : XMFLOAT4(0.15f, 0.12f, 0.1f, 0.9f),
                            cell.gameObject.worldY + 0.05f);
                    }
            }
        }
    }
}

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

// 身代わり・移動不可の×印・注目の枠・背水のオーラ・脅威のグロー・キャラ（奥から順に）・聖地の光
void BattleScene::DrawUnits()
{
    if (m_decoyCol >= 0)
    {
        float x = CellWX(m_decoyCol);
        float z = CellWZ(m_decoyRow);
        m_renderer3D->DrawBillboard(TextureManager::Get("kakashi"), x, 0.05f, z , 0.8f, 0.8f, 0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // 移動不可マスの×マーク（深度テストOFF、敵より先に描画）
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

    // 注目中の敵を識別色のコーナーマークで囲む
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

    // 背水：赤い残像（低HP＋火事場/決死 の間）
    int hp = m_player->GetHp(), mhp = m_player->GetMaxHp();
    bool hasLast = m_player->GetBuffManager().HasBuff(BuffType::LastStand);   // 1/2パワー
    bool hasDeep = m_player->GetBuffManager().HasBuff(BuffType::DeepStand);   // 1/4パワー
    float pulse = 0.5f + 0.5f * sinf(m_highlightTimer * 10.0f);               // 0..1 脈動

    m_renderer3D->SetDepthEnabled(false);

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

    // 脅威範囲にプレイヤーが入っている敵を、脈動グロー（疑似ぼかし）で強調（自ターン中のみ）
    // 深度テストを切った区間で、プレイヤー・敵より先に描く（拡大した縁が床にもぐって途切れないように）
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


    m_renderer3D->SetDepthEnabled(true);

    // プレイヤーと敵：透明な四角が互いを隠さないよう、深度書き込みOFF＋奥→手前で描く
    // （プレイヤーだけ深度を書くと、透明な部分が奥の敵の足元を隠して埋まって見える）
    m_renderer3D->SetDepthWrite(false);
    std::vector<Enemy*> drawOrder(m_enemies.begin(), m_enemies.end());
    std::sort(drawOrder.begin(), drawOrder.end(),
        [](Enemy* a, Enemy* b) { return a->worldZ < b->worldZ; });   // 奥(小Z)を先に、手前(大Z)を後に

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

    // 聖地に立っている敵：手前に立ちのぼる光（敵の絵にかぶせて「焼かれる」と一目で分かるように）
    m_renderer3D->SetDepthEnabled(false);
    m_renderer3D->SetAdditive(true);
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
    m_renderer3D->SetAdditive(false);
    m_renderer3D->SetDepthEnabled(true);
    m_renderer3D->SetDepthWrite(true);
}

// エフェクト・必中のクロスヘア・移動先のゴースト・浮いたマスのL字枠
void BattleScene::DrawOverlays()
{
    m_renderer3D->SetDepthEnabled(false);
    EffectManager::Draw(m_renderer3D, TextureManager::Get("particle"));
    // 必中攻撃の対象にクロスヘア（デコイ優先）。対象の実座標に合わせて画面端でもズレないように
    bool showCross = false;
    for (auto enemy : m_enemies)
    {
        const EnemyAction* act = enemy->GetNextAction();
        if (act && act->target.unavoidable && !EnemyIntentVisual::Hidden()) { showCross = true; break; }
    }
    if (showCross)
    {
        float cx, cy, cz;
        if (m_decoyCol >= 0)
        {
            cx = CellWX(m_decoyCol);   // デコイはマス固定
            cz = CellWZ(m_decoyRow);
            cy = 0.4f;
        }
        else
        {
            cx = m_player->worldX;              // プレイヤーは実際の描画位置に合わせる
            cz = m_player->worldZ;
            cy = m_player->worldY + 0.35f;
        }

        m_renderer3D->SetDepthEnabled(false);
        float pulse = 0.85f + 0.15f * sinf(m_highlightTimer * 6.0f);
        m_renderer3D->DrawBillboard(TextureManager::Get("crosshair"),
            cx, cy, cz, 0.9f * pulse, 0.9f * pulse, 0.0f, XMFLOAT4(1.0f, 0.3f, 0.2f, 0.95f));
        m_renderer3D->SetDepthEnabled(true);
    }
    m_renderer3D->SetDepthEnabled(true);

    // 移動経路の終点に半透明プレイヤー
    if (m_pathBuilding && !m_movePath.empty())
    {
        int gc = m_movePath.back().first;
        int gr = m_movePath.back().second;
        float gx = CellWX(gc);
        float gz = CellWZ(gr);
        float gy = m_player->worldY + m_gridMap->GetCell(gc, gr).gameObject.worldY;   // ← マスの浮きぶん
        m_renderer3D->DrawBillboard(
            TextureManager::Get("player"),
            gx, gy, gz,
            m_player->width, m_player->height, 0.0f,
            XMFLOAT4(0.5f, 0.7f, 1.0f, 0.3f));
    }

    // 対象マス（上がっているマス）の四隅にL字枠
    {
        XMMATRIX vp = m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix();
        auto toScreen = [&](float wx, float wy, float wz, float& ox, float& oy) -> bool {
            XMVECTOR w = XMVectorSet(wx, wy, wz, 1.0f);
            XMVECTOR c = XMVector4Transform(w, vp);
            XMFLOAT4 cl; XMStoreFloat4(&cl, c);
            if (cl.w <= 0.0f) return false;
            ox = (cl.x / cl.w + 1.0f) * 0.5f * m_screenWidth;
            oy = (1.0f - cl.y / cl.w) * 0.5f * m_screenHeight;
            return true;
            };
        auto drawLine = [&](float ax, float ay, float bx, float by, float th, const XMFLOAT4& c) {
            float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;
            float dx = bx - ax, dy = by - ay;
            float len = sqrtf(dx * dx + dy * dy);
            float ang = atan2f(dy, dx);
            m_spriteRenderer->DrawSprite(m_whiteTexture, mx - len * 0.5f, my - th * 0.5f, len, th, ang, c);
            };

        m_spriteRenderer->Begin();
        const float h = 0.5f;       // セル半径（枠の位置、要調整）
        const float t = 0.32f;      // 角の長さ比
        const XMFLOAT4 col(1.0f, 0.9f, 0.3f, 0.95f);
        for (int row = 0; row < m_gridMap->GetRows(); row++)
            for (int cc = 0; cc < m_gridMap->GetCols(); cc++)
            {
                auto& cell = m_gridMap->GetCell(cc, row);
                if (cell.gameObject.worldY < 0.05f) continue;   // 上がってるマスだけ
                float wx = CellWX(cc);
                float wz = CellWZ(row);
                float wy = cell.gameObject.worldY + 0.02f;
                float sx[4], sy[4];
                bool ok = toScreen(wx - h, wy, wz - h, sx[0], sy[0])
                    && toScreen(wx + h, wy, wz - h, sx[1], sy[1])
                    && toScreen(wx + h, wy, wz + h, sx[2], sy[2])
                    && toScreen(wx - h, wy, wz + h, sx[3], sy[3]);
                if (!ok) continue;
                for (int k = 0; k < 4; k++)
                {
                    int n = (k + 1) % 4, p = (k + 3) % 4;
                    drawLine(sx[k], sy[k], sx[k] + (sx[n] - sx[k]) * t, sy[k] + (sy[n] - sy[k]) * t, 3.0f, col);
                    drawLine(sx[k], sy[k], sx[k] + (sx[p] - sx[k]) * t, sy[k] + (sy[p] - sy[k]) * t, 3.0f, col);
                }
            }
        m_spriteRenderer->End();
    }
}

// 戦闘UIに渡す情報
BattleUIContext BattleScene::MakeUIContext()
{
    BattleUIContext ctx;
    ctx.player = m_player;
    ctx.enemies = &m_enemies;
    ctx.hand = &m_hand;
    ctx.deck = &m_deck;
    ctx.turnManager = &m_turnManager;
    ctx.renderer3D = m_renderer3D;
    ctx.gridMap = m_gridMap;
    ctx.playerCol = m_playerCol;
    ctx.playerRow = m_playerRow;
    ctx.selectedCardIndex = m_selectedCardIndex;
    ctx.hoveredCardIndex = m_hoveredCardIndex;
    ctx.hoveredCell = m_uiBlocked ? std::pair<int, int>(-1, -1) : m_hoveredCell;   // 窓の後ろでマスの説明を出さない
    ctx.highlightTimer = m_highlightTimer;
    ctx.battleResult = m_battleResult;
    ctx.drops = &m_dropResult;
    ctx.showDrawPile = m_showDrawPile;
    ctx.cardSelecting = m_cardSelecting;
    ctx.showDiscardPile = m_showDiscardPile;
    ctx.showExhaustPile = m_showExhaustPile;
    ctx.screenWidth = m_screenWidth;
    ctx.screenHeight = m_screenHeight;
    ctx.cameraZoom = m_camera.Zoom();
    ctx.isPlayerTurn = m_turnManager.IsPlayerTurn();
    ctx.showMoveArrows = (!m_turnManager.IsPlayerTurn() || m_arrowRevealTimer <= 0.0f)
        && !EnemyIntentVisual::Hidden();   // 目隠し中は移動予定も見せない
    ctx.mousePos = m_uiBlocked ? POINT{ -10000, -10000 } : m_input.GetMousePos();   // 窓の後ろのボタンにホバーさせない
    ctx.outOfRangeCells = &m_highlighter.GetOutOfRangeCells();
    ctx.travelPath = &m_movePath;
    ctx.selectedEnemy = FocusEnemy();
    ctx.discardSelectCount = m_discardSelectCount;
    ctx.discardSelected = &m_discardSelected;
    ctx.discardViewMode = m_discardViewMode;
    ctx.rewardRelic = &m_rewardRelic;
    return ctx;
}

// 3層ボスの覚醒演出：集中線＋ブラー、ボスだけくっきり、広がる円
void BattleScene::DrawAwakenCinematic()
{
    // 中心＝ボスの画面位置（ブラー前に計算：カメラは同じ）
    float cx = m_screenWidth * 0.5f, cy = m_screenHeight * 0.5f;
    if (m_awakenBoss)
    {
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, XMVector4Transform(
            XMVectorSet(m_awakenBoss->worldX, 0.0f, m_awakenBoss->worldZ, 1.0f),
            m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix()));
        if (clip.w > 0.0f)
        {
            cx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
            cy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
        }
    }
    float maxR = 0.0f;
    maxR = max(maxR, hypotf(cx, cy));
    maxR = max(maxR, hypotf(m_screenWidth - cx, cy));
    maxR = max(maxR, hypotf(cx, m_screenHeight - cy));
    maxR = max(maxR, hypotf(m_screenWidth - cx, m_screenHeight - cy));
    maxR *= 1.1f;
    float ph = m_awakenCinematic;

    // 集中線をオフスクリーン（ブラー前）に描く → ブラー対象に
    auto white = TextureManager::Get("white");
    const int LINES = 84;
    m_spriteRenderer->Begin();
    for (int i = 0; i < LINES; i++)
    {
        float ang = (float)i / LINES * 6.2831853f + sinf(ph * 30.0f + i * 12.9898f) * 0.02f;
        float Ri = maxR * (0.34f + 0.06f * sinf(i * 7.13f));
        float Ro = maxR * 1.4f;
        float len = Ro - Ri;
        float mx = cx + cosf(ang) * (Ri + Ro) * 0.5f;
        float my = cy + sinf(ang) * (Ri + Ro) * 0.5f;
        float th = 2.0f + 5.0f * (0.5f + 0.5f * sinf(i * 3.7f));
        m_spriteRenderer->DrawSprite(white, mx - len * 0.5f, my - th * 0.5f, len, th,
            ang, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f));
    }
    m_spriteRenderer->End();

    // ブラー合成（シーン＋集中線をまとめてぼかす）
    m_renderer3D->EndOffscreenBlur(2.0f);

    // ボスだけシャープ再描画
    if (m_awakenBoss)
    {
        m_renderer3D->Begin();
        m_renderer3D->SetDepthEnabled(false);
        m_awakenBoss->Draw3D(m_renderer3D);
        m_renderer3D->SetDepthEnabled(true);
        m_renderer3D->End();
    }

    // 円はシャープ（ブラー後）
    m_spriteRenderer->Begin();
    auto ring = TextureManager::Get("ui_hitring");
    for (float a : m_awakenRings)
    {
        float t = a / 1.1f;
        float R = t * maxR;
        float alpha = (1.0f - t) * 0.85f;
        m_spriteRenderer->DrawSprite(ring, cx - R, cy - R, R * 2.0f, R * 2.0f,
            0.0f, XMFLOAT4(1.0f, 0.35f, 0.2f, alpha));
    }
    m_spriteRenderer->End();
}
