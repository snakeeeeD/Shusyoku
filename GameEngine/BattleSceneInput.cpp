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

// BattleScene の入力（HandleInput とその下の関数）。状態はシーンのものをそのまま使うので、クラスは分けずにファイルだけ分ける

// 戦闘の入力。どの操作をどこで受けるかの振り分けだけ（中身は下の関数）
void BattleScene::HandleInput()
{
    if (m_camera.DemoActive())   // カメラのデモ中は操作を止める（左クリックで飛ばす）
    {
        if (m_input.GetMouseButtonTrigger(0)) m_camera.EndDemo();
        return;
    }
    if (HandleBattleEndInput()) return;                                 // 勝敗後：クリックで次へ
    if (!m_turnManager.IsPlayerTurn()) return;                          // プレイヤーターン以外は無視
    if (m_drawSeqRemaining > 0 || m_reshuffleFxTimer > 0.0f) return;    // 時間差ドロー中／リシャッフル演出中は操作不可
    if (m_discardSelectCount > 0) { HandleDiscardSelectInput(); return; }   // 捨てるカードの選択中

    PickEnemyOnGrid();          // 盤面の敵クリックで範囲を見る
    UpdateHoveredCell();        // 指しているマス・狙い確定のタイマー
    UpdateMovePath();           // 移動カードの経路づくり
    HandleReleaseToPlay();      // 左ボタンを離したら使う
    HandleClickInput();         // 山札・捨て札ボタン、一覧から選ぶ、クリックで使う
    HandleHandInput();          // 手札を選ぶ・ホバー
    HandleTurnEndAndPanel();    // ターン終了ボタン、敵パネル
}

// 勝敗後：クリックで次へ（層を進める／報酬／結果画面）。勝敗が付いていたら true
bool BattleScene::HandleBattleEndInput()
{
    // 勝敗後はターンに関係なくクリックで進める
    if (m_battleResult == BattleResult::Win)
    {
        if (m_input.GetMouseButtonTrigger(0) && onChangeScene)
        {
            if (m_category == EncCategory::Boss)
            {
                auto& pd = PlayerDataManager::GetData();
                if (pd.layer < 3)
                {
                    pd.layer++;                       // 次の層へ
                    pd.fieldNodeTypes.clear();        // マップを作り直させる
                    pd.fieldNodeEnemyIds.clear();
                    pd.fieldNodeVisited.clear();
                    PlayerDataManager::Save();
                    onChangeScene(SceneType::Field);
                }
                else
                {
                    PlayerDataManager::DeleteSave();   // クリア＝セーブ削除
                    onChangeScene(SceneType::Result);
                }
            }
            else onChangeScene(SceneType::CardSelect);
        }
        return true;
    }
    if (m_battleResult == BattleResult::Lose)
    {
        if (m_input.GetMouseButtonTrigger(0) && onChangeScene)
        {
            PlayerDataManager::DeleteSave();   // ゲームオーバー＝セーブ削除
            onChangeScene(SceneType::Result);
        }
        return true;
    }
    return false;
}

// 捨てるカードの選択中（他の操作は受け付けない）
void BattleScene::HandleDiscardSelectInput()
{
    m_highlighter.ClearPlayerHighlight(m_gridMap);      // グリッドの範囲表示を出さない

    POINT mp = m_input.GetMousePos();
    bool click = m_input.GetMouseButtonTrigger(0);

    if (click && m_battleUI->IsOnDiscardView(mp))       // 閲覧モード切替
    {
        m_discardViewMode = !m_discardViewMode;
        return;
    }

    if (m_discardViewMode)
    {
        if (click)
        {
            bool onPileBtn = (mp.y >= m_screenHeight - 60 && mp.y <= m_screenHeight - 20)
                && ((mp.x >= 20 && mp.x <= 70) || (mp.x >= 80 && mp.x <= 130)
                    || (mp.x >= 140 && mp.x <= 190));

            if (onPileBtn)
            {
                if (mp.x >= 20 && mp.x <= 70)   m_showDrawPile = !m_showDrawPile;
                if (mp.x >= 80 && mp.x <= 130)  m_showDiscardPile = !m_showDiscardPile;
                if (mp.x >= 140 && mp.x <= 190) m_showExhaustPile = !m_showExhaustPile;
            }
            else if (m_showDrawPile || m_showDiscardPile || m_showExhaustPile)
            {
                // ビューアの外をクリックで閉じる
                float bgX = m_screenWidth / 2.0f - 300.0f, bgY = 50.0f;
                if (mp.x < bgX || mp.x > bgX + 600.0f || mp.y < bgY || mp.y > bgY + 580.0f)
                {
                    m_showDrawPile = false;
                    m_showDiscardPile = false;
                    m_showExhaustPile = false;
                }
            }
        }
        m_hoveredCardIndex = -1;
        return;
    }

    // 「捨てる」「盤面を見る」の上では説明を消す（説明の窓がボタンに重なって押せなくなるため）
    bool onBtn = m_battleUI->IsOnDiscardConfirm(mp) || m_battleUI->IsOnDiscardView(mp);
    if (onBtn) CardTooltip::ClearHeld();
    m_hoveredCardIndex = onBtn ? -1 : m_battleUI->GetCardAtScreenPos(mp);
    if (m_hoveredCardIndex < 0 && !onBtn && CardTooltip::MouseInHold())   // 説明へ移る間はカードを浮かせたまま
        m_hoveredCardIndex = m_prevHoveredCard;

    if (m_hoveredCardIndex != m_prevHoveredCard)
    {
        if (m_hoveredCardIndex >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
        m_prevHoveredCard = m_hoveredCardIndex;
    }

    if (click)
    {
        // 確定
        if (m_battleUI->IsOnDiscardConfirm(mp)
            && (int)m_discardSelected.size() == m_discardSelectCount)
        {
            std::sort(m_discardSelected.rbegin(), m_discardSelected.rend());
            std::vector<CardEffectData> effects;
            float playDelay = 0.0f;
            for (int idx : m_discardSelected)
            {
                const CardData* dd = CardDataBase::Get(m_hand.GetCards()[idx]->GetId());
                if (dd && dd->onDiscardEffect.hasEffect)
                {
                    effects.push_back(dd->onDiscardEffect);
                    m_battleUI->StartPlayCardEffectFromHand(dd, idx, playDelay, false); // 中央へ移動＝使用演出
                    playDelay += 0.15f;                                                 // 複数枚は少しずらす
                }
                else
                {
                    m_battleUI->StartDiscardEffectAt(idx);   // 効果なしは普通の捨てアニメ
                }
                m_hand.DiscardAt(idx);
                m_battleUI->OnCardRemoved(idx);
            }
            m_discardSelected.clear();
            m_discardSelectCount = 0;
            m_hoveredCardIndex = -1;
            for (auto& e : effects) ApplyDiscardEffect(e);   // 捨て終わってから発動
            return;
        }

        // 手札クリックで選択トグル
        int idx = m_hoveredCardIndex;
        if (idx >= 0 && idx < (int)m_hand.GetCards().size())
        {
            auto it = std::find(m_discardSelected.begin(), m_discardSelected.end(), idx);
            if (it != m_discardSelected.end())
            {
                m_discardSelected.erase(it);                  // 選択済み → 解除
            }
            else
            {
                if ((int)m_discardSelected.size() >= m_discardSelectCount)
                    m_discardSelected.erase(m_discardSelected.begin());   // 上限なら一番古いのを外す
                m_discardSelected.push_back(idx);
            }
        }
    }
}

// 盤面の敵をクリック → その敵の範囲を見る（もう一度で解除）
void BattleScene::PickEnemyOnGrid()
{
    if (m_input.GetMouseButtonTrigger(0) && m_selectedCardIndex < 0
        && m_input.GetMousePos().x < m_screenWidth - 250.0f      // パネル上は無視
        && m_input.GetMousePos().y < m_screenHeight - 150.0f)     // 手札エリアも無視
    {
        auto rc = m_gridMap->GetClickedCell3D(m_input.GetMousePos(),
            m_renderer3D->GetViewMatrix(), m_renderer3D->GetProjectionMatrix(),
            m_screenWidth, m_screenHeight);
        int found = -1;
        if (rc.cell)
            for (int i = 0; i < (int)m_enemies.size(); i++)
                for (auto& [dc, dr] : m_enemies[i]->GetGridShape())
                    if (m_enemies[i]->gridCol + dc == rc.col && m_enemies[i]->gridRow + dr == rc.row)
                        found = i;
        m_selectedEnemyRange = (found >= 0 && found != m_selectedEnemyRange) ? found : -1;
    }
}

// 指しているマス（m_hoveredCell）と、同じマスをいつから指しているか（狙い確定用）
void BattleScene::UpdateHoveredCell()
{
    // カード選択中はマウスのマスにハイライト
    if (m_selectedCardIndex >= 0)
    {
        POINT mousePos = m_input.GetMousePos();
        POINT gridMousePos = mousePos;
        if (gridMousePos.y > m_screenHeight + 200)
            gridMousePos.y = m_screenHeight + 200;
        auto result = m_gridMap->GetClickedCell3D(
            gridMousePos,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

        if (result.cell)
            m_hoveredCell = { result.col, result.row };
        else
            m_hoveredCell = { -1, -1 };   // マス外に出たら自動ホバー（敵）に戻す
    }
    else
    {
        POINT mousePos = m_input.GetMousePos();
        auto result = m_gridMap->GetClickedCell3D(
            mousePos,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

        if (result.cell)
            m_hoveredCell = { result.col, result.row };
        else
            m_hoveredCell = { -1, -1 };

        m_highlighter.ClearPlayerHighlight(m_gridMap);
    }

    // 狙っているマスがいつから同じか
    if (m_hoveredCell != m_aimCell)
    {
        m_aimCell = m_hoveredCell;
        m_aimSince = GetTickCount64();
    }
}

// 移動カード：指しているマスへ経路を伸ばす／戻す（手動）。離れたマスは最短経路
void BattleScene::UpdateMovePath()
{
    const auto& cards = m_hand.GetCards();
    // カメラが自動で動いて、マウスの下のマスが変わっただけ（マウスは止まっている）なら、経路は変えない
    POINT mpNow = m_input.GetMousePos();
    bool mouseMoved = (mpNow.x != m_prevPathMouse.x || mpNow.y != m_prevPathMouse.y);
    m_prevPathMouse = mpNow;
    bool cameraDriven = m_camera.AutoMoving() && !mouseMoved;

    // === 移動カード：手動経路構築 ===
    bool selectedIsMove = false;
    int moveRangeSel = 0;
    if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)cards.size())
    {
        const CardData* sd = cards[m_selectedCardIndex]->GetData();
        if (sd && sd->type == CardType::Move)
        {
            selectedIsMove = true;
            moveRangeSel = m_player->GetMoveRange(sd->range);
        }
    }

    if (selectedIsMove)
    {
        if (!m_pathBuilding) m_movePath.clear();   // 選択直後にリセット
        m_pathBuilding = true;

        // 右クリックキャンセル中：カーソルを別マスに動かすまで経路を出さない
        if (m_moveReleaseSuppress
            && m_hoveredCell.first == m_suppressCell.first
            && m_hoveredCell.second == m_suppressCell.second)
        {
            m_movePath.clear();
        }
        else if (m_hoveredCell.first >= 0 && !cameraDriven)
        {
            m_moveReleaseSuppress = false;   // カーソルが動いたら再開
            int hc = m_hoveredCell.first, hr = m_hoveredCell.second;

            int tipCol = m_movePath.empty() ? m_playerCol : m_movePath.back().first;
            int tipRow = m_movePath.empty() ? m_playerRow : m_movePath.back().second;

            int idx = -1;
            for (int i = 0; i < (int)m_movePath.size(); i++)
                if (m_movePath[i].first == hc && m_movePath[i].second == hr) { idx = i; break; }

            bool onTip = (hc == tipCol && hr == tipRow);
            bool onPlayer = (hc == m_playerCol && hr == m_playerRow);
            bool isRetract = onPlayer || (idx >= 0 && idx < (int)m_movePath.size() - 1);

            if (onTip)
            {
                // 先端の上：何もしない
                m_backtrackFrames = 0;
            }
            else if (isRetract)
            {
                // 戻す：数フレーム安定してから（縦の一瞬のブレで誤爆させない）
                if (hc == m_backtrackCell.first && hr == m_backtrackCell.second)
                    m_backtrackFrames++;
                else { m_backtrackCell = { hc, hr }; m_backtrackFrames = 1; }

                if (m_backtrackFrames >= 4)
                {
                    if (onPlayer) m_movePath.clear();
                    else m_movePath.resize(idx + 1);
                }
            }
            else
            {
                m_backtrackFrames = 0;
                int distTip = abs(hc - tipCol) + abs(hr - tipRow);

                if (distTip == 1)
                {
                    int budget = moveRangeSel - (int)m_movePath.size();
                    if (budget > 0 && m_gridMap->GetCell(hc, hr).type == CellType::Empty)
                    {
                        // 余裕あり：そのまま1マス追加
                        m_movePath.push_back({ hc, hr });
                    }
                    else if (m_gridMap->GetCell(hc, hr).type == CellType::Empty)
                    {
                        // 最大到達中：後ろから1マスずつ戻して、届く接頭辞を探す
                        for (int keep = (int)m_movePath.size() - 1; keep >= 0; keep--)
                        {
                            int b = moveRangeSel - keep;
                            int ptc = (keep == 0) ? m_playerCol : m_movePath[keep - 1].first;
                            int ptr = (keep == 0) ? m_playerRow : m_movePath[keep - 1].second;

                            std::set<std::pair<int, int>> blocked(m_movePath.begin(), m_movePath.begin() + keep);
                            blocked.insert({ m_playerCol, m_playerRow });
                            blocked.erase({ ptc, ptr });
                            auto conn = ShortestPath({ ptc, ptr }, { hc, hr }, b, blocked);
                            if (!conn.empty())
                            {
                                m_movePath.resize(keep);
                                for (auto& c : conn) m_movePath.push_back(c);
                                break;
                            }
                        }
                    }
                }
                else
                {
                    // 離れたマス：プレイヤーから自動最短経路（手動経路を破棄）
                    m_movePath = ShortestPath({ m_playerCol, m_playerRow }, { hc, hr }, moveRangeSel);
                }
            }
        }
    }
    else
    {
        m_pathBuilding = false;
        m_movePath.clear();
    }
}

// from から to への最短経路（空きマスだけ・budget 歩以内・blocked は通らない）。from は含まず to を含む。見つからなければ空
std::vector<std::pair<int, int>> BattleScene::ShortestPath(std::pair<int, int> from, std::pair<int, int> to, int budget,
    const std::set<std::pair<int, int>>& blocked) const
{
    static const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
    std::queue<std::pair<int, int>> q;
    std::map<std::pair<int, int>, std::pair<int, int>> par;
    std::map<std::pair<int, int>, int> dist;
    q.push(from); dist[from] = 0; par[from] = { -1, -1 };
    bool found = false;
    while (!q.empty())
    {
        auto [cc, rr] = q.front(); q.pop();
        if (cc == to.first && rr == to.second) { found = true; break; }
        if (dist[{cc, rr}] >= budget) continue;
        for (auto& d : dirs)
        {
            int nc = cc + d[0], nr = rr + d[1];
            if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
            auto np = std::make_pair(nc, nr);
            if (dist.count(np) || blocked.count(np)) continue;
            if (m_gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
            dist[np] = dist[{cc, rr}] + 1;
            par[np] = { cc, rr };
            q.push(np);
        }
    }
    std::vector<std::pair<int, int>> path;
    if (!found) return path;
    for (auto cur = to; cur != from; cur = par[cur]) path.push_back(cur);
    std::reverse(path.begin(), path.end());
    return path;
}

// 左ボタンを離したら使う：狙うマスを決めて PlayCard（通り過ぎる途中で離した時などは選択したまま）
void BattleScene::HandleReleaseToPlay()
{
    const auto& cards = m_hand.GetCards();
    // 全カード: グリッドエリア上で左ボタンを離したら使用
    if (m_input.GetMouseButtonRelease(0) && m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)cards.size())
    {
        POINT releasePos = m_input.GetMousePos();
        if (releasePos.y < m_screenHeight - 190)
        {
            const Card* card = cards[m_selectedCardIndex];
            CardData dataCopy = *card->GetData();
            ApplyKnifeThrow(dataCopy, m_player);
            CardType ct = dataCopy.type;

            const std::vector<std::pair<int, int>>* usePath = nullptr;
            bool moveCanceled = false;
            if (ct == CardType::Move)
            {
                if (m_player->GetBuffManager().HasBuff(BuffType::MoveLock))
                    moveCanceled = true;                        // 踏ん張り中は移動不可
                else if (m_moveReleaseSuppress)
                    moveCanceled = true;
                else if (m_pathBuilding && !m_movePath.empty())
                {
                    // 指すマスが変わった直後に離した＝通り過ぎる途中 → 使わずに選択したまま（もう一度押せば使える）
                    if (GetTickCount64() - m_aimSince >= AIM_SETTLE_MS) usePath = &m_movePath;
                    else moveCanceled = true;
                }
                else if (m_pathBuilding && m_movePath.empty())
                    moveCanceled = true;
            }

            int targetCol = m_playerCol;
            int targetRow = m_playerRow;
            bool canTry = !moveCanceled;

            auto cellInRng = [&](int ec, int er) { return CellInCardRange(dataCopy, ec, er); };
            auto enemyCells = [](Enemy* e) { return e->OccupiedCells(); };

            // 射程内の敵を数える。1体だけなら自動ターゲット
            Enemy* soleEnemy = nullptr; int inRangeCount = 0;
            if (ct == CardType::Attack)
                for (auto e : m_enemies)
                {
                    if (!e || e->GetHp() <= 0) continue;
                    bool inR = false;
                    for (auto& [ec, er] : enemyCells(e)) if (cellInRng(ec, er)) { inR = true; break; }
                    if (inR) { inRangeCount++; soleEnemy = e; }
                }
            bool autoAttack = (ct == CardType::Attack && inRangeCount == 1 && soleEnemy);

            bool needCell = PicksCell(*cards[m_selectedCardIndex]->GetData());
            if ((ct == CardType::Attack || ct == CardType::Move || needCell)
                && !usePath && !moveCanceled)
            {
                auto result = m_gridMap->GetClickedCell3D(
                    releasePos, m_renderer3D->GetViewMatrix(), m_renderer3D->GetProjectionMatrix(),
                    m_screenWidth, m_screenHeight);
                bool cellInRange = (result.cell && cellInRng(result.col, result.row));
                // 射程内の敵が1体だけなら、空きマスで離してもその敵を狙う（予測 AttackTargetEnemy と同じ判定）
                bool hoverEmpty = !result.cell || EnemyIndexAt(result.col, result.row) < 0;
                bool forceAuto = autoAttack && hoverEmpty && !AimsFreely(dataCopy);
                if (result.cell && !forceAuto && (cellInRange || !autoAttack))
                {
                    // 離したマスに止まっていた時間が短い＝通り過ぎる途中で離した → 使わずに選択したまま（もう一度押せば使える）
                    bool settled = (result.col == m_aimCell.first && result.row == m_aimCell.second)
                        && (GetTickCount64() - m_aimSince >= AIM_SETTLE_MS);
                    if (settled)
                    {
                        targetCol = result.col;
                        targetRow = result.row;
                    }
                    else canTry = false;
                }
                else if (autoAttack)
                {
                    // 頭優先：頭(gridCol/Row)が射程内ならそこ、無ければ射程内マスの最寄り
                    int tc = soleEnemy->gridCol, tr = soleEnemy->gridRow;
                    if (!cellInRng(tc, tr))
                    {
                        int best = 1 << 30;
                        for (auto& [ec, er] : enemyCells(soleEnemy))
                        {
                            if (!cellInRng(ec, er)) continue;
                            int d = abs(ec - m_playerCol) + abs(er - m_playerRow);
                            if (d < best) { best = d; tc = ec; tr = er; }
                        }
                    }
                    targetCol = tc; targetRow = tr;
                }
                else
                {
                    if (!dataCopy.allEnemyEffect.hasEffect)
                        canTry = false;
                }
            }

            if (canTry) PlayCard(targetCol, targetRow, usePath);
        }
    }

    if (m_input.GetMouseButtonRelease(0))
    {
        m_pathBuilding = false;
        m_movePath.clear();
        m_moveReleaseSuppress = false;
    }
}

// カードを使う（離して使う／クリックで使う の共通）。使えたら true
bool BattleScene::PlayCard(int targetCol, int targetRow, const std::vector<std::pair<int, int>>* usePath)
{
    const Card* card = m_hand.GetCards()[m_selectedCardIndex];
    CardData dataCopy = *card->GetData();
    ApplyKnifeThrow(dataCopy, m_player);   // ナイフ＋投擲術なら遠距離化
    std::string cardId = card->GetId();
    int newPlayerCol = m_playerCol;
    int newPlayerRow = m_playerRow;

    const CardData* dataPtr = card->GetData();

    int faBonus = (!m_firstAttackDone && dataPtr->type == CardType::Attack)
        ? RelicManager::SumValue("firstAttack") : 0;
    if (faBonus > 0) 
    {
        Buff fb; fb.type = BuffType::AttackUp; 
        fb.value = faBonus; 
        fb.duration = 1; fb.name = L"";
        fb.description = L"";
        m_player->GetBuffManager().AddBuff(fb); 
    }

    auto execResult = m_cardExecutor.Execute(
        dataCopy, cardId,
        targetCol, targetRow,
        m_player, m_enemies, m_gridMap,
        m_playerCol, m_playerRow,
        m_hand, m_selectedCardIndex, m_deck,
        newPlayerCol, newPlayerRow,
        usePath
    );
    if (execResult.cardUsed)                                   // 実際に使えた時だけ
        m_player->OnCardUsed(dataCopy.type, targetCol - m_playerCol, targetRow - m_playerRow);
    StartCardSequences(execResult);   // 連鎖起爆・一斉起爆・連撃の追撃（Update で1つずつ）

    if (execResult.placeDecoy)
    {
        m_decoyCol = execResult.decoyCol;
        m_decoyRow = execResult.decoyRow;
    }

    if (faBonus > 0)
    {
        Buff fb; fb.type = BuffType::AttackUp; fb.value = -faBonus; fb.duration = 1; fb.name = L""; fb.description = L""; m_player->GetBuffManager().AddBuff(fb);
        if (m_player->GetBuffManager().GetBuffValue(BuffType::AttackUp) == 0)
            m_player->GetBuffManager().RemoveBuff(BuffType::AttackUp);   // 0残り掃除
        if (execResult.cardUsed) m_firstAttackDone = true;
    }

    if (execResult.cardUsed)
    {
        Audio::PlaySE("Assets/Sound/se/card.mp3");

        // カードを使うたびブロック（刃の守り）
        if (int cb = m_player->GetBuffManager().GetBuffValue(BuffType::CardBlock))
        {
            m_player->AddBlock(cb);
            EffectManager::Play("block_gain", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);
        }

        if (newPlayerCol != m_playerCol || newPlayerRow != m_playerRow)
            MovePlayerAfterCard(newPlayerCol, newPlayerRow, execResult.moveDist, usePath);   // 歩き・氷の滑り・地形

        m_battleUI->StartEffectDraws(execResult.drawnCards);   // 使ったカードが出てから1枚ずつ引く
        m_battleUI->StartPlayCardEffect(dataPtr, m_selectedCardIndex);
        ProcessDeadEnemies();
        if (execResult.pendingSelection != CardEffectType::None)
        {
            m_cardSelecting = true;
            m_selectingType = execResult.pendingSelection;
            if (m_selectingType == CardEffectType::Search)
                m_showDrawPile = true;
            else if (m_selectingType == CardEffectType::Salvage)
                m_showDiscardPile = true;
        }

        m_camera.SetAimKeep(dataPtr->type == CardType::Move);   // 移動したらカメラは寄せた位置のまま
        m_battleUI->OnCardRemoved(m_selectedCardIndex);
        m_selectedCardIndex = -1;
        if (execResult.purgeHandCurses) PurgeHandCurses();   // 浄化：使ったカードのアニメを外してから（先にやると選択が-1になり、アニメの並びがずれる）
        OnCardPlayed(dataPtr);

        if (execResult.pendingDiscard > 0)
            OpenDiscardSelect(execResult.pendingDiscard);
    }
    return execResult.cardUsed;
}

// カードで動いた後のプレイヤー：歩く（経路があれば経路どおり）・氷で滑る・着いたマスの地形
void BattleScene::MovePlayerAfterCard(int newCol, int newRow, int moveDist, const std::vector<std::pair<int, int>>* usePath)
{
    int oldCol = m_playerCol;
    int oldRow = m_playerRow;
    m_playerCol = newCol;
    m_playerRow = newRow;
    m_player->gridCol = m_playerCol;
    m_player->gridRow = m_playerRow;
    OnPlayerMoved(oldCol, oldRow, moveDist);   // レリック：移動時
    float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
    float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
    if (usePath && !usePath->empty())
    {
        std::vector<std::pair<float, float>> pts;
        for (auto& p : *usePath)
            pts.push_back({ (p.first - m_gridMap->GetCols() / 2.0f) * 1.1f,
                            (p.second - m_gridMap->GetRows() / 2.0f) * 1.1f });
        m_player->StartWalk(pts, 0.08f);
    }
    else
    {
        m_player->StartMove(px, pz);
    }

    auto& movedCell = m_gridMap->GetCell(m_playerCol, m_playerRow);

    // 氷チェック（TriggerTerrainでdurationが消える前に）
    bool isSlide = false;
    int slideDx = 0, slideDy = 0;
    if (movedCell.tileEffect.active)
    {
        const TerrainDef* def = TerrainDataBase::Get(movedCell.tileEffect.id);
        if (def && def->effect == "Slide" && !RelicManager::HasKind("floorImmune"))
        {
            isSlide = true;
            // 向きは「最後の1歩」で決める（回り込んだ時に全体のずれで決めると斜め扱い→ランダムになる）
            int fromCol = oldCol, fromRow = oldRow;
            if (usePath && usePath->size() >= 2)
            {
                fromCol = (*usePath)[usePath->size() - 2].first;
                fromRow = (*usePath)[usePath->size() - 2].second;
            }
            int dx = m_playerCol - fromCol;
            int dy = m_playerRow - fromRow;
            if (dx != 0 && dy != 0)
            {
                if (rand() % 2 == 0)
                    slideDx = dx > 0 ? 1 : -1;
                else
                    slideDy = dy > 0 ? 1 : -1;
            }
            else
            {
                slideDx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
                slideDy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
            }
        }
    }

    CardExecutor::TriggerTerrain(movedCell, m_player);

    if (isSlide && (slideDx != 0 || slideDy != 0))
    {
        bool walked = (usePath && !usePath->empty());
        std::vector<std::pair<float, float>> slidePts;   // すべった先のマス（歩きの続きにつなぐ）
        if (!walked) slidePts.push_back({ px, pz });     // 経路なしの移動は、まず着地点へ
        while (true)
        {
            int nextCol = m_playerCol + slideDx;
            int nextRow = m_playerRow + slideDy;

            if (nextCol < 0 || nextCol >= m_gridMap->GetCols() ||
                nextRow < 0 || nextRow >= m_gridMap->GetRows())
                break;

            auto& nextCell = m_gridMap->GetCell(nextCol, nextRow);
            if (nextCell.type != CellType::Empty)
                break;

            m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Empty);
            m_playerCol = nextCol;
            m_playerRow = nextRow;
            m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Player);
            m_player->gridCol = m_playerCol;
            m_player->gridRow = m_playerRow;
            float sx = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float sz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
            slidePts.push_back({ sx, sz });

            auto& slideCell = m_gridMap->GetCell(m_playerCol, m_playerRow);
            if (slideCell.tileEffect.active)
            {
                const TerrainDef* slideDef = TerrainDataBase::Get(slideCell.tileEffect.id);
                if (slideDef && slideDef->effect == "Slide")
                    continue;

                CardExecutor::TriggerTerrain(slideCell, m_player);
            }
            break;
        }
        if (slidePts.size() > (walked ? 0u : 1u))         // 1マスでもすべった時だけ
            m_player->AppendWalk(slidePts, 0.08f);        // 歩き終わってから、同じ速さでスーッと続ける
    }
}

// クリック：山札・捨て札・廃棄札ボタン、一覧から選ぶ（探す・回収）、盤面クリックで使う／外して解除
void BattleScene::HandleClickInput()
{
    if (!m_input.GetMouseButtonTrigger(0)) return;

    // 山札・捨て札ボタンの位置
    float drawPileX = 20.0f;
    float drawPileY = m_screenHeight - 60.0f;
    float discardX = 80.0f;
    float discardY = m_screenHeight - 60.0f;

    POINT mousePos = m_input.GetMousePos();

    // カードエリアにマウスがあるかチェック
    bool isOnCard = (m_battleUI->GetCardAtScreenPos(mousePos) >= 0);

    POINT gridMousePos2 = mousePos;
    if (gridMousePos2.y > m_screenHeight - 200)
        gridMousePos2.y = m_screenHeight - 200;
    auto result = m_gridMap->GetClickedCell3D(
        gridMousePos2,
        m_renderer3D->GetViewMatrix(),
        m_renderer3D->GetProjectionMatrix(),
        m_screenWidth,
        m_screenHeight
    );

    // 山札ボタン
    if (mousePos.x >= drawPileX && mousePos.x <= drawPileX + 50.0f
        && mousePos.y >= drawPileY && mousePos.y <= drawPileY + 40.0f)
    {
        m_showDrawPile = !m_showDrawPile;
        Audio::PlaySE("Assets/Sound/se/click.mp3");
        m_showDiscardPile = false;
    }

    // 捨て札ボタン
    else if (mousePos.x >= discardX && mousePos.x <= discardX + 50.0f
        && mousePos.y >= discardY && mousePos.y <= discardY + 40.0f)
    {
        m_showDiscardPile = !m_showDiscardPile;
        Audio::PlaySE("Assets/Sound/se/click.mp3");
        m_showDrawPile = false;
    }

    // 廃棄札ボタン
    else if (m_deck.GetExhaustPileCount() > 0
        && mousePos.x >= 140.0f && mousePos.x <= 190.0f
        && mousePos.y >= discardY && mousePos.y <= discardY + 40.0f)
    {
        m_showExhaustPile = !m_showExhaustPile;
        Audio::PlaySE("Assets/Sound/se/click.mp3");
        m_showDrawPile = false;
        m_showDiscardPile = false;
    }

    // 一覧から選ぶ（山札から探す・捨て札から回収）
    else if (m_cardSelecting && (m_showDrawPile || m_showDiscardPile))
    {
        auto displayPile = BattleUI::PileForView(&m_deck, m_showDrawPile, m_showDiscardPile);
        int i = m_battleUI->PileCardAt(mousePos, (int)displayPile.size());   // 描画と同じ矩形で判定
        if (i >= 0)
        {
            std::string selectedId = displayPile[i];
            std::string result;
            if (m_selectingType == CardEffectType::Search)
                result = m_deck.DrawSpecificCard(selectedId);
            else if (m_selectingType == CardEffectType::Salvage)
                result = m_deck.SalvageCard(selectedId);

            if (!result.empty())
            {
                m_hand.AddCard(result);
                m_battleUI->StartDrawCardEffect(result);
            }
        }
        if (i >= 0 || !m_battleUI->PileViewerRect().has(mousePos))   // 選んだ／枠の外＝取りやめ
        {
            m_cardSelecting = false;
            m_selectingType = CardEffectType::None;
            m_showDrawPile = false;
            m_showDiscardPile = false;
        }
    }
    // 見るだけの一覧：枠の外で閉じる
    else if (m_showDrawPile || m_showDiscardPile || m_showExhaustPile)
    {
        if (!m_battleUI->PileViewerRect().has(mousePos))
        {
            m_showDrawPile = false;
            m_showDiscardPile = false;
            m_showExhaustPile = false;
        }
    }

    if (result.cell && !isOnCard)
    {
        if (m_selectedCardIndex >= 0
            && m_selectedCardIndex < (int)m_hand.GetCards().size()
            && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
            PlayCard(result.col, result.row, nullptr);   // クリックで使う
    }
    else if (m_selectedCardIndex >= 0 && !isOnCard
        && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
        m_selectedCardIndex = -1;
}

// 手札：クリックで選ぶ／同じカードで解除（エナジー不足は知らせて解除）、ホバー
void BattleScene::HandleHandInput()
{
    POINT mousePos = m_input.GetMousePos();
    int cardUnder = m_battleUI->GetCardAtScreenPos(mousePos);
    if (cardUnder >= 0 && m_input.GetMouseButtonTrigger(0))
    {
        if (m_selectedCardIndex == cardUnder)
        {
            m_selectedCardIndex = -1;                       // 同じカード再クリック→解除
        }
        else
        {
            int cost = m_player->CostOf(*m_hand.GetCards()[cardUnder]->GetData());
            if (m_player->GetEnergy() < cost)
            {
                // エナジー不足：アウトライン付き浮遊テキストで通知し、自動で選択解除
                float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                FloatingTextManager::Spawn(px, 1.3f, pz, L"エナジーがたりない",
                    XMFLOAT4(1.0f, 0.45f, 0.2f, 1.0f), 30.0f);
                m_selectedCardIndex = -1;
            }
            else
            {
                m_selectedCardIndex = cardUnder;
            }
        }
    }
    
    m_hoveredCardIndex = m_battleUI->GetCardAtScreenPos(mousePos);
    if (m_selectedCardIndex >= 0) CardTooltip::ClearHeld();   // 選んでいる間は、残っている説明も消す（窓の上のクリックを盤面に通す）
    if (m_hoveredCardIndex != m_prevHoveredCard)
    {
        if (m_hoveredCardIndex >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
        m_prevHoveredCard = m_hoveredCardIndex;
    }
}

// ターン終了ボタン、敵パネルのクリック（範囲を見る＋その敵へカメラ）
void BattleScene::HandleTurnEndAndPanel()
{
    // ターンエンドボタン（右下）
    if (m_battleResult == BattleResult::None)
    {
        float btnW = 180.0f;
        float btnH = 56.0f;
        float btnX = m_screenWidth - btnW - 20.0f;
        float btnY = m_screenHeight - btnH - 20.0f;

        if (m_input.GetMouseButtonTrigger(0))
        {
            POINT mp = m_input.GetMousePos();
            if (mp.x >= btnX && mp.x <= btnX + btnW
                && mp.y >= btnY && mp.y <= btnY + btnH)
            {
                Audio::PlaySE("Assets/Sound/se/click.mp3");
                m_selectedCardIndex = -1;   // 選択解除してからターン終了
                m_cardSelecting = false;    // ドロー/捨札選択も解除
                m_turnManager.EndTurn();
            }
        }
    }

    // 敵パネルクリック → 範囲選択＋その敵へカメラ
    if (m_input.GetMouseButtonTrigger(0))
    {
        int i = BattleUI::EnemyEntryAt(m_input.GetMousePos(), m_enemies, m_screenWidth);
        if (i >= 0)
        {
            m_selectedEnemyRange = (m_selectedEnemyRange == i) ? -1 : i;
            m_camera.LookAt(m_enemies[i]->worldX, m_enemies[i]->worldZ);
        }
    }
}
