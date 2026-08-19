#include "BattleScene.h"
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
#include "Audio.h"

#include <algorithm>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// ナイフ＋投擲術なら遠距離化（Diamond=範囲内の任意の敵を狙える。単体パスのままなので研磨も効く）
static void ApplyKnifeThrow(CardData& d, const Player* p)
{
    if (p->GetBuffManager().HasBuff(BuffType::KnifeThrow)
        && std::find(d.tags.begin(), d.tags.end(), "Knife") != d.tags.end())
    {
        d.rangeType = RangeType::Diamond;
        d.range = p->GetBuffManager().GetBuffValue(BuffType::KnifeThrow);   // バフ値＝射程
    }
}

BattleScene::BattleScene()
    : m_battleUI(nullptr)
    , m_gridMap(nullptr)
    , m_whiteTexture(nullptr)
    , m_playerCol(0)
    , m_playerRow(0)
    , m_player(nullptr)
    , m_renderer3D(nullptr)
    , m_spriteRenderer(nullptr)
    , m_highlightTimer(0.0f)
{
}

BattleScene::~BattleScene()
{
    delete m_battleUI;
    delete m_gridMap;
    delete m_renderer3D;
    delete m_spriteRenderer;
    delete m_player;
    for (auto enemy : m_enemies)
        delete enemy;
    m_enemies.clear();
}

bool BattleScene::Init(ID3D11Device* device, ID3D11DeviceContext* context, 
                       int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_device = device;
    m_context = context;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_hWnd = hWnd;

    m_selectedCardIndex = -1;
    m_hoveredCardIndex  = -1;
    m_prevHoveredCardIndex = -1;

    m_showDrawPile = false;
    m_showDiscardPile = false;
    m_showExhaustPile = false;

    m_hoveredCell = { -1, -1 };
    m_rightClickDragged = false;

    m_cameraZoom = ZOOM_MAX;
    m_isDraggingCamera = false;
    m_dragStartPos = { 0, 0 };

    m_debugRank = 1;

    m_debugEncounterIndex = -1;  // -1 = ランダム

    m_battleResult = BattleResult::None;

    // PlayerDataManagerからデッキを取得
    auto& playerData = PlayerDataManager::GetData();
    for (auto& cardId : playerData.deck)
        m_deck.AddCard(cardId);
    m_hand.SetDeck(&m_deck);

    m_deck.ShuffleDrawPile();

    // 最初の手札を引く
    std::vector<std::string> moveCardIds;
    for (auto& cardId : m_deck.GetDrawPile())
    {
        const CardData* data = CardDataBase::Get(cardId);
        if (data && data->type == CardType::Move)
            moveCardIds.push_back(cardId);
    }

   /* if (!moveCardIds.empty())
    {
        int idx = rand() % (int)moveCardIds.size();
        std::string moveId = m_deck.DrawSpecificCard(moveCardIds[idx]);
        if (!moveId.empty()) m_hand.AddCard(moveId);
    }*/

    if (!playerData.tutorialBattle)
    {
        // 初戦チュートリアル：左から 移動・アタック・ブロック で固定
        for (const char* id : { "MOV_move", "ATK_strike", "SKL_defend" })
        {
            std::string c = m_deck.DrawSpecificCard(id);
            if (!c.empty()) m_hand.AddCard(c);
        }
        while ((int)m_hand.GetCards().size() < HAND_SIZE)
        {
            std::string id = m_deck.DrawCard();
            if (id.empty()) break;
            m_hand.AddCard(id);
        }
    }
    else
    {
        for (int i = 0; i < HAND_SIZE - 1; i++)
        {
            std::string id = m_deck.DrawCard();
            if (!id.empty()) m_hand.AddCard(id);
        }
    }

    m_renderer3D = new Renderer3D();
    if (!m_renderer3D->Init(device, context, screenWidth, screenHeight))
        return false;

    m_spriteRenderer = new SpriteRenderer();
    m_spriteRenderer->Init(device, context, screenWidth, screenHeight);

    m_battleUI = new BattleUI();
    if (!m_battleUI->Init(device, context, screenWidth, screenHeight, swapChain))
        return false;

    m_whiteTexture = TextureManager::Get("white");

    // グリッドマップ
    m_gridMap = new GridMap();
    m_gridMap->Init(9, 7, 72.0f);

    // 各マスに白いテクスチャをセット
    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int col = 0; col < m_gridMap->GetCols(); col++)
            m_gridMap->GetCell(col, row).gameObject.texture = m_whiteTexture;

    // プレイヤー初期位置
    m_playerCol = m_gridMap->GetCols() / 2;
    m_playerRow = m_gridMap->GetRows() / 2;
    m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Player);


    m_player = new Player();
    m_player->gridCol = m_playerCol;
    m_player->gridRow = m_playerRow;
    m_player->worldX = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
    m_player->worldZ = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;

    m_cameraOffsetX = m_player->worldX;
    m_cameraOffsetZ = m_player->worldZ;

    // PlayerDataManagerからHPを引き継ぐ
    m_player->SetMaxHp(playerData.maxHp);   // 最大HPをPlayerDataから同期
    m_player->SetHp(playerData.hp);
    m_player->SnapDisplayHp();   // 表示HPを即実HPに（入場時のアニメ防止）

    m_player->AddEnergy(RelicManager::SumValue("turnEnergy"));   // レリック

    // レリック：戦闘開始時
    if (int blk = RelicManager::SumValue("startBlock")) m_player->AddBlock(blk);
    if (int h = RelicManager::SumValue("startHeal")) m_player->Heal(h);
    if (int atk = RelicManager::SumValue("startBuffAtk"))
    {
        Buff b; b.type = BuffType::AttackUp; b.value = atk; b.duration = 999;
        b.name = L""; b.description = L"";
        m_player->GetBuffManager().AddBuff(b);
    }
    for (int i = 0; i < RelicManager::SumValue("startDraw"); i++)
    {
        std::string id = m_deck.DrawCard();
        if (!id.empty()) m_hand.AddCard(id);
    }

    // プレイヤーターン開始時にエネルギー回復
    m_turnManager.onPlayerTurnStart = [this]()
        {
            for (auto& id : PlayerDataManager::GetData().relics)
                if (auto def = RelicManager::Get(id); def && def->perTurn)
                    PlayerDataManager::GetData().relicCounters[id] = 0;

            TurnBanner::Show(TurnBannerType::Player);
            m_player->RestoreEnergy();
            m_player->AddEnergy(RelicManager::SumValue("turnEnergy"));   // レリック
            {
                int keep = m_player->GetBlock() * RelicManager::SumValue("blockRetain") / 100;
                m_player->ResetBlock();
                if (keep > 0) m_player->AddBlock(keep);
            }
            m_player->GetBuffManager().OnTurnEnd();

            // 攻撃力成長
            int grow = m_player->GetBuffManager().GetBuffValue(BuffType::AttackGrowth)
                + RelicManager::SumValue("turnBuffAtk");
            if (grow > 0)
            {
                Buff gb; gb.type = BuffType::AttackUp; gb.value = grow; gb.duration = -1;
                gb.name = L""; gb.description = L"";
                m_player->GetBuffManager().AddBuff(gb);
            }

            // 毒の瘴気：毎ターン開始時、全敵に毒を付与
            int nox = m_player->GetBuffManager().GetBuffValue(BuffType::NoxiousFumes);
            if (nox > 0)
                for (auto enemy : m_enemies)
                {
                    if (enemy->GetHp() <= 0) continue;
                    float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                    float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                    Buff pb; pb.type = BuffType::Poison;
                    pb.value = nox + RelicManager::SumValue("poisonAdd");   // ← +毒の心得
                    pb.duration = pb.value;
                    pb.name = BuffInfo::Get(BuffType::Poison).name; pb.description = L"";
                    enemy->GetBuffManager().AddBuff(pb);
                    EffectManager::Play("poison_apply", wx, 0.5f, wz);
                    FloatingTextManager::Spawn(wx, 0.7f, wz, std::to_wstring(pb.value),
                        BuffInfo::Get(BuffType::Poison).color, 32.0f);
                }

            // デバフダメージ
            auto dmg = m_player->GetBuffManager().GetTurnEndDamage();
            if (dmg.total() > 0)
                m_player->TakeDamage(dmg.total(), DamageFeel::Poison);

            m_turnCount++;

            for (auto enemy : m_enemies)
            {
                int tC = (m_decoyCol >= 0) ? m_decoyCol : m_playerCol;
                int tR = (m_decoyCol >= 0) ? m_decoyRow : m_playerRow;
                int allies = 0; for (auto o : m_enemies) if (o != enemy && o->GetHp() > 0) allies++;
                enemy->SetAllyCount(allies);
                enemy->DecideNextAction(tC, tR, m_turnCount);
            }

            m_arrowRevealTimer = 0.1f;   // 敵の動作直後に次矢印が出ないよう一拍おく

            // 山札と捨て札から移動カードを探す
            std::vector<std::string> moveCardIds;
            auto collectMoveCards = [&](const std::vector<std::string>& pile)
                {
                    for (auto& cardId : pile)
                    {
                        const CardData* data = CardDataBase::Get(cardId);
                        if (data && data->type == CardType::Move)
                            moveCardIds.push_back(cardId);
                    }
                };
            collectMoveCards(m_deck.GetDrawPile());
            collectMoveCards(m_deck.GetDiscardPile());

            //// 移動カードを1枚確定で引く
            //if (!moveCardIds.empty())
            //{
            //    int idx = rand() % (int)moveCardIds.size();
            //    std::string moveId = m_deck.DrawSpecificCard(moveCardIds[idx]);
            //    if (!moveId.empty()) m_hand.AddCard(moveId);
            //}

            // 残りを引く
            for (int i = 0; i < HAND_SIZE - 1; i++)
            {
                std::string id = m_deck.DrawCard();
                if (!id.empty())
                {
                    m_hand.AddCard(id);
                    m_battleUI->StartDrawCardEffect(id);
                }
            }

            // 毎ターン ナイフを生成（刃の心得）
            int kg = m_player->GetBuffManager().GetBuffValue(BuffType::KnifeGen);
            for (int i = 0; i < kg; i++)
            {
                m_hand.AddCard("ATK_knife");
                m_battleUI->StartDrawCardEffect("ATK_knife");
            }

            RunTurnCycle();
    
        };
    m_turnManager.onEnemyTurnStart = [this]()
        {

            // 時限床（ボスのやけど床など）の寿命：敵ターン開始時に1減らし、0で消滅
            for (int r = 0; r < m_gridMap->GetRows(); r++)
                for (int c = 0; c < m_gridMap->GetCols(); c++)
                {
                    auto& cell = m_gridMap->GetCell(c, r);
                    if (cell.tileEffect.active && cell.tileEffect.hazardTurns > 0)
                    {
                        cell.tileEffect.hazardTurns--;
                        if (cell.tileEffect.hazardTurns <= 0)
                            cell.tileEffect = TileEffect();
                    }
                }

            TurnBanner::Show(TurnBannerType::Enemy);

            for (auto enemy : m_enemies)
                enemy->ResetBlock();
            m_battleUI->StartDiscardEffects();
            for (auto card : m_hand.GetCards())
                m_deck.DiscardCard(card->GetId());
            m_hand.Clear();
            m_battleUI->ClearCardAnimations();

            m_enemyPhase = EnemyTurnPhase::WaitStart;
            m_currentEnemyIdx = 0;
            m_poisonIdx = 0;         
            m_poisonSubTicks = 0;
            m_enemyActionDelay = 0.8f;
        };

    int encCount = EncounterDataBase::GetCount();
    int layer = PlayerDataManager::GetData().layer;
    const EncounterData* encounter = EncounterDataBase::GetById(m_battleEnemyId);   // 指名戦（あれば）
    if (!encounter)
        encounter = EncounterDataBase::GetEncounter(layer, m_category, m_battleTier, m_battleSeed);
    if (encounter)
    {
        for (auto& ee : encounter->enemies)
            AddEnemy(ee.col, ee.row, ee.id);

        const auto& ladder = encounter->escalation.empty()
            ? EncounterDataBase::DefaultEscalation()
            : encounter->escalation;

        float hpMul = 1.0f, dmgMul = 1.0f;
        int bonusActions = 0;
        int level = m_overflow;
        for (int i = 0; i < level; i++)
        {
            if (i < (int)ladder.size())
            {
                const EscalationTier& t = ladder[i];
                switch (t.kind)
                {
                case EscalationKind::HpUp:      hpMul += t.value / 100.0f;   break;
                case EscalationKind::AtkUp:     dmgMul += t.value / 100.0f;   break;
                case EscalationKind::AddAction: bonusActions += 1;            break;
                case EscalationKind::AddEnemy:  AddEnemy(t.col, t.row, t.id); break;
                }
            }
            else   // 梯子超過分は緩やかに
            {
                hpMul += 0.10f;
                dmgMul += 0.10f;
            }
        }
        for (auto enemy : m_enemies)
            enemy->ApplyDifficulty(hpMul, dmgMul, bonusActions);
    }

    HitStop::Clear();   // 前の戦闘の停止を持ち越さない

    // 猛毒の小瓶：少し経ってから全敵に毒（Updateで発火）
    m_startPoisonAmount = RelicManager::SumValue("startPoison");
    m_startPoisonTimer = 0.6f;

    for (auto enemy : m_enemies)
    {
        int tC = (m_decoyCol >= 0) ? m_decoyCol : m_playerCol;
        int tR = (m_decoyCol >= 0) ? m_decoyRow : m_playerRow;
        int allies = 0; for (auto o : m_enemies) if (o != enemy && o->GetHp() > 0) allies++;
        enemy->SetAllyCount(allies);
        enemy->DecideNextAction(tC, tR, m_turnCount);
    }

     m_input.SetWindowHandle(hWnd);

     FloatingTextManager::Clear();
     UiNotice::Clear();
     EffectManager::Clear();

     RunTurnCycle();

     TurnBanner::ShowThen(TurnBannerType::BattleStart, TurnBannerType::Player);

    return true;
}

void BattleScene::AddEnemy(int col, int row, const std::string& id)
{
    auto enemy = new Enemy();
    enemy->Init(id);
    enemy->gridCol = col;
    enemy->gridRow = row;

    // 形の中心を計算してワールド座標を設定
    float sumCol = 0, sumRow = 0;
    for (auto& [dc, dr] : enemy->GetGridShape())
    {
        sumCol += col + dc;
        sumRow += row + dr;
    }
    float centerCol = sumCol / enemy->GetGridShape().size();
    float centerRow = sumRow / enemy->GetGridShape().size();
    enemy->worldX = (centerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
    enemy->worldZ = (centerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;

    // 占有マスをセット
    CellType cellType = enemy->IsBoss() ? CellType::Boss : CellType::Enemy;
    for (auto& [dc, dr] : enemy->GetGridShape())
        m_gridMap->SetCellType(col + dc, row + dr, cellType);

    m_enemies.push_back(enemy);
}

void BattleScene::SummonNear(Enemy* src, const std::string& id, int count)
{
    int placed = 0;
    // 召喚主の周囲を近い順（外周リング）に探索。敵/ボス/障害のマスは飛ばして隣の空きへ
    for (int r = 1; r <= 5 && placed < count; r++)
        for (int dr = -r; dr <= r && placed < count; dr++)
            for (int dc = -r; dc <= r && placed < count; dc++)
            {
                if (abs(dc) != r && abs(dr) != r) continue;   // 外周リングだけ
                int c = src->gridCol + dc, rr = src->gridRow + dr;
                if (c < 0 || c >= m_gridMap->GetCols() || rr < 0 || rr >= m_gridMap->GetRows()) continue;
                if (m_gridMap->GetCell(c, rr).type != CellType::Empty) continue;   // 敵がいるマス等は飛ばす
                if (c == m_player->gridCol && rr == m_player->gridRow) continue;    // プレイヤーの現在地も避ける
                AddEnemy(c, rr, id);
                m_enemies.back()->MarkJustSummoned();                              // 同ターンは動かない
                EffectManager::Play("summon", m_enemies.back()->worldX, 0.4f, m_enemies.back()->worldZ);  // 召喚エフェクト
                placed++;
            }
}

void BattleScene::OnPlayerMoved()
{
    m_player->AddBlock(RelicManager::SumValue("moveBlock"));
    if (m_player->GetBuffManager().HasBuff(BuffType::Frenzy))
        m_player->TakeDamage(1, DamageFeel::Hit);   // 狂乱中の移動は身を削る
    // 今後の「移動時○○」レリックはここに追記
}

void BattleScene::Update(float deltaTime)
{
    m_input.Update();
    if (m_freeLook) return;

#ifdef _DEBUG
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
#endif

    if (HitStop::IsActive()) { HitStop::Update(deltaTime); return; }   // ヒットストップ中はゲームを止める

    FloatingTextManager::Update(deltaTime);
    // マルチヒットの追撃を間隔をあけて処理
    if (m_multiHitRemain > 0 && !m_multiHitTargets.empty())
    {
        m_multiHitTimer -= deltaTime;
        if (m_multiHitTimer <= 0.0f)
        {
            bool anyAlive = false;
            for (auto t : m_multiHitTargets)
            {
                bool alive = false;
                for (auto e : m_enemies) if (e == t && e->GetHp() > 0) alive = true;
                if (alive) { t->TakeDamage(m_multiHitDamage); t->StartJump(0.5f, 0.2f); anyAlive = true; }
            }
            m_multiHitRemain--;
            m_multiHitTimer = MULTI_HIT_INTERVAL;
            if (!anyAlive || m_multiHitRemain <= 0)
            {
                m_multiHitRemain = 0;
                m_multiHitTargets.clear();
            }
        }
    }

    if (m_startPoisonAmount > 0)
    {
        m_startPoisonTimer -= deltaTime;
        if (m_startPoisonTimer <= 0.0f)
        {
            for (auto enemy : m_enemies)
            {
                if (enemy->GetHp() <= 0) continue;
                Buff b; b.type = BuffType::Poison;
                b.value = m_startPoisonAmount + RelicManager::SumValue("poisonAdd");
                b.duration = b.value;
                b.name = BuffInfo::Get(BuffType::Poison).name; b.description = L"";
                enemy->GetBuffManager().AddBuff(b);
                float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                EffectManager::Play("poison_apply", wx, 0.5f, wz);
            }
            m_startPoisonAmount = 0;   // 一度だけ
        }
    }

    if (!m_chainQueue.empty())
    {
        m_chainTimer -= deltaTime;
        if (m_chainTimer <= 0.0f)
        {
            auto wave = m_chainQueue;          // 今の波
            m_chainQueue.clear();
            for (auto& [c, r] : wave)
            {
                auto& cell = m_gridMap->GetCell(c, r);
                if (!cell.tileEffect.active || cell.tileEffect.enemyOwned) continue;   // 敵床は除外
                CardExecutor::DetonateTrap(cell, c, r, m_gridMap, m_enemies, m_chainFull, false);
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++)
                    {
                        if (dc == 0 && dr == 0) continue;
                        int nc = c + dc, nr = r + dr;
                        if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
                        auto& ncell = m_gridMap->GetCell(nc, nr);
                        if (ncell.tileEffect.active && !ncell.tileEffect.enemyOwned)   // 敵床に連鎖しない
                            m_chainQueue.push_back({ nc, nr });
                    }
            }
            ScreenShake::Add(0.3f);
            ProcessDeadEnemies();
            m_chainTimer = 0.12f;   // 波の間隔（大きいほどゆっくり連鎖）
        }
    }
    if (!m_detonateQueue.empty())
    {
        m_detonateTimer -= deltaTime;
        if (m_detonateTimer <= 0.0f)
        {
            auto [c, r] = m_detonateQueue.front();
            m_detonateQueue.erase(m_detonateQueue.begin());
            auto& cell = m_gridMap->GetCell(c, r);
            if (cell.tileEffect.active)
            {
                CardExecutor::DetonateTrap(cell, c, r, m_gridMap, m_enemies, m_detonateFull, false);
                ScreenShake::Add(0.2f);
                ProcessDeadEnemies();
            }
            m_detonateTimer = 0.08f;   // 1個ずつの間隔（大きいほどゆっくり）
        }
    }

    UiNotice::Update(deltaTime);
    if (UiNotice::ConsumeTriggered())
        m_battleUI->StartOverflowDiscardEffect();
    ScreenShake::Update(deltaTime);
    TurnBanner::Update(deltaTime);
    EffectManager::Update(deltaTime);

    // HP0の敵を「塵化」させ、アニメが終わったら消す
    for (auto enemy : m_enemies)
    {
        if (enemy->GetHp() <= 0 && !enemy->IsDying())
        {
            for (auto& [dc, dr] : enemy->GetGridShape())      // マスを即解放
                m_gridMap->SetCellType(enemy->gridCol + dc, enemy->gridRow + dr, CellType::Empty);
            enemy->StartDeath();
        }
        enemy->UpdateDeath(deltaTime);
    }
    ProcessDeadEnemies();

    m_player->UpdateDisplayHp(deltaTime);
    for (auto enemy : m_enemies)
        enemy->UpdateDisplayHp(deltaTime);

    m_player->UpdateMove(deltaTime);
    m_player->UpdateHitFlash(deltaTime);

    for (auto enemy : m_enemies)
    {
        enemy->UpdateMove(deltaTime);
        enemy->UpdateLunge(deltaTime);
        enemy->UpdateJump(deltaTime);
        enemy->UpdateHitFlash(deltaTime);
    }

    // カメラズーム（マウスホイール）
    int wheelDelta = m_input.GetMouseWheelDelta();
    if (wheelDelta != 0)
    {
        m_cameraZoom -= wheelDelta > 0 ? ZOOM_SPEED : -ZOOM_SPEED;
        m_cameraZoom = max(ZOOM_MIN, min(ZOOM_MAX, m_cameraZoom));
    }

    // 右クリック（ボタン1）：経路キャンセル → カード選択キャンセル の二段階
    if (m_input.GetMouseButtonTrigger(1)
        && m_selectedCardIndex >= 0
        && m_selectedCardIndex < (int)m_hand.GetCards().size())
    {
        bool isMove = (m_hand.GetCards()[m_selectedCardIndex]->GetData()->type == CardType::Move);

        if (isMove && !m_movePath.empty())
        {
            // 経路がある → 経路だけキャンセル（カードは維持）
            m_movePath.clear();
            m_moveReleaseSuppress = true;
            m_suppressCell = m_hoveredCell;
        }
        else
        {
            // 経路が無い or 移動以外のカード → カード選択解除
            m_selectedCardIndex = -1;
            m_movePath.clear();
            m_moveReleaseSuppress = false;
        }
    }

    // カメラパン（右クリックドラッグ）
    if (m_input.GetMouseButtonPress(2))
    {
        POINT mousePos = m_input.GetMousePos();
        if (!m_isDraggingCamera)
        {
            m_isDraggingCamera = true;
            m_dragStartPos = mousePos;
            m_rightClickDragged = false;
        }
        else
        {
            float dx = (float)(mousePos.x - m_dragStartPos.x);
            float dy = (float)(mousePos.y - m_dragStartPos.y);

            if (abs(dx) > 1.0f || abs(dy) > 1.0f)
                m_rightClickDragged = true;

            m_cameraOffsetX += dx * 0.02f * m_cameraZoom;
            m_cameraOffsetZ -= dy * 0.02f * m_cameraZoom;

            // クランプはオフセット加算の後
            float gridHalfW = (m_gridMap->GetCols() / 2.0f) * 1.1f;
            float gridHalfH = (m_gridMap->GetRows() / 2.0f) * 1.1f;
            float zoomFactor = (m_cameraZoom > 1.0f) ? 1.0f / m_cameraZoom : 1.0f;  // ズームアウト時に制限が狭くなる
            m_cameraOffsetX = max((-gridHalfW + 2.0f) * zoomFactor, min((gridHalfW - 3.0f) * zoomFactor, m_cameraOffsetX));
            m_cameraOffsetZ = max((-gridHalfH + 1.0f) * zoomFactor, min((gridHalfH - 2.0f) * zoomFactor, m_cameraOffsetZ));

            m_dragStartPos = mousePos;
        }
    }
    else
    {
        if (m_isDraggingCamera && !m_rightClickDragged)
        {
            // ドラッグせずに離した＝単押し→カード選択解除（Moveカードは除く）
            if (m_selectedCardIndex >= 0
                && m_selectedCardIndex < (int)m_hand.GetCards().size()
                && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
                m_selectedCardIndex = -1;
        }
        m_isDraggingCamera = false;
    }

    // カメラリセット（ミドルクリック）
    if (m_input.GetMouseButtonTrigger(2))
    {
        m_cameraZoom = ZOOM_MAX;
        m_cameraOffsetX = m_player->worldX;
        m_cameraOffsetZ = m_player->worldZ;
    }

    // カメラ更新（ズーム or パンが変わったら毎フレーム適用）
    {
        float shakeX, shakeZ;
        ScreenShake::GetOffset(shakeX, shakeZ);

        XMFLOAT3 target(
            m_cameraOffsetX + shakeX,
            -2.0f,
            m_cameraOffsetZ + shakeZ
        );
        XMFLOAT3 zoomedPos(
            m_cameraOffsetX + shakeX,
            target.y + 17.0f * m_cameraZoom,
            m_cameraOffsetZ + shakeZ + 6.0f * m_cameraZoom
        );
        m_renderer3D->SetCamera(zoomedPos, target, XMFLOAT3(0.0f, 1.0f, 0.0f));

        int hi = m_battleUI->GetPanelHoveredEnemy();
        m_highlighter.SetSelectedEnemy(hi >= 0 ? hi : m_selectedEnemyRange);

        // ハイライト更新
        int highlightCardIndex = m_selectedCardIndex >= 0 ? m_selectedCardIndex : m_hoveredCardIndex;
        if (highlightCardIndex >= 0 && highlightCardIndex < (int)m_hand.GetCards().size())
        {
            CardData dataThrow = *m_hand.GetCards()[highlightCardIndex]->GetData();
            ApplyKnifeThrow(dataThrow, m_player);
            const CardData* data = &dataThrow;

            RECT cardArea = { 0, 0, 0, 0 };
            if (!m_hand.GetCards().empty())
            {
                int numCards = (int)m_hand.GetCards().size();
                float totalW = numCards * (CARD_WIDTH + 10.0f);
                float leftX = m_screenWidth / 2.0f - totalW / 2.0f;
                float rightX = leftX + totalW;
                float topY = m_screenHeight - CARD_HIDE_Y_OFFSET;
                cardArea = { (LONG)leftX, (LONG)topY, (LONG)rightX, (LONG)m_screenHeight };
            }

            m_highlighter.SetTravelPath(&m_movePath);

            m_highlighter.UpdatePlayerHighlight(
                m_playerCol, m_playerRow, data,
                m_enemies, m_gridMap, m_player,
                m_highlightTimer, m_hoveredCell,
                m_renderer3D, m_screenWidth, m_screenHeight,
                cardArea, m_player->GetBuffManager().HasBuff(BuffType::MoveLock));
        }
        else
        {
            m_highlighter.ClearPlayerHighlight(m_gridMap);
        }

        // ハイライト明滅タイマーを更新
        m_highlightTimer += deltaTime * 0.5f; // 点滅速度調整
        if (m_arrowRevealTimer > 0.0f) m_arrowRevealTimer -= deltaTime;
        if (m_highlightTimer > 3.14159f * 2.0f)
            m_highlightTimer = 0.0f;

        if (m_battleResult != BattleResult::None) return;   // 勝敗決定後は何もしない
        m_highlighter.UpdateEnemyHighlight(
            m_enemies, m_gridMap, m_player,
            m_playerCol, m_playerRow, m_highlightTimer,
            m_decoyCol, m_decoyRow);

        m_battleUI->UpdateDrawCardEffects(deltaTime);
        m_battleUI->UpdatePlayCardEffects(deltaTime);
        bool selectedNeedsTarget = false;
        if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size())
        {
            const CardData* sd = m_hand.GetCards()[m_selectedCardIndex]->GetData();
            CardType ct = sd->type;
            selectedNeedsTarget = (ct == CardType::Attack || ct == CardType::Move
                || sd->mainEffect.type == CardEffectType::PlaceTrap
                || sd->mainEffect.type == CardEffectType::DetonateAt
                || sd->mainEffect.type == CardEffectType::DetonateChain
                || sd->mainEffect.type == CardEffectType::PlaceDecoy);
        }
        m_battleUI->UpdateCardAnimations(deltaTime, (int)m_hand.GetCards().size(), m_hoveredCardIndex, 
            m_selectedCardIndex, m_input.GetMousePos(), selectedNeedsTarget,
            &m_discardSelected);
        m_battleUI->UpdateDiscardEffects(deltaTime);

        // 勝利判定
        if (m_enemies.empty())
        {
            m_battleResult = BattleResult::Win;

            m_player->Heal(RelicManager::SumValue("winHeal"));   // レリック（pd.hpに反映される）

            // HPを保存、現在のマスをクリア済みに
            auto& pd = PlayerDataManager::GetData();
            pd.hp = m_player->GetHp();

            if (m_category == EncCategory::Boss && pd.layer < 3)
            {
                std::string br = RelicManager::RandomUnowned("boss");
                if (br.empty()) br = RelicManager::RandomDrop();
                if (!br.empty()) { PlayerDataManager::AddRelic(br); m_rewardRelic = br; }
            }

            // 初勝利：クラフト導入（コア＋素材を確定ドロップ。
            // 勝利画面で行0=コア/行1=素材に固定し、チュートリアルのスポットライトを合わせる）
            if (!pd.tutorialCraft)
            {
                pd.materials["core_slash"] += 1;      m_dropResult.push_back({ "core_slash", 1, true });
                pd.materials["sword_fragment"] += 1;  m_dropResult.push_back({ "sword_fragment", 1, false });
            }


            // 素材ドロップ判定
            for (auto& eid : m_defeatedEnemyIds)
            {
                const EnemyData* ed = EnemyDataBase::Get(eid);
                if (!ed) continue;
                for (auto& d : ed->drops)
                {
                    if (rand() % 100 < d.chance)
                    {
                        int n = d.min + (d.max > d.min ? rand() % (d.max - d.min + 1) : 0);
                        pd.materials[d.id] += n;
                        m_dropResult.push_back({ d.id, n, d.rare });   // 表示用
                    }
                }
            }

            // カード報酬のレア出現をプール/カテゴリ別に抽選
            {
                int rareChance = 0;
                if (m_category == EncCategory::Elite)
                    rareChance = 30;                 // エリート
                else if (m_category == EncCategory::Normal)
                    rareChance = (m_battleTier <= 1) ? 0 : (m_battleTier == 2) ? 2 : 5;  // 弱/中/強
                pd.rewardRare = (rand() % 100 < rareChance);
            }

            int nodeIdx = pd.fieldPlayerCol * 7 + pd.fieldPlayerRow;
            if (nodeIdx >= 0 && nodeIdx < (int)pd.fieldNodeVisited.size())
                pd.fieldNodeVisited[nodeIdx] = true;
            pd.gold += 10 + rand() % 16;
            if (m_category == EncCategory::Elite)
            {
                pd.gold += 25;
                int em = RelicManager::SumValue("eliteMaterial");
                // エリート報酬：レリック
                std::string rr = RelicManager::RandomDrop();
                if (!rr.empty()) { PlayerDataManager::AddRelic(rr); m_rewardRelic = rr; }
                if (em > 0)
                {
                    std::vector<std::string> mids;
                    for (auto& kv : MaterialDataBase::AllMaterials()) mids.push_back(kv.first);
                    for (int k = 0; k < em && !mids.empty(); k++)
                    {
                        std::string mid = mids[rand() % mids.size()];
                        pd.materials[mid] += 1;
                        m_dropResult.push_back({ mid, 1, false });   // 勝利画面に表示
                    }
                }
            }

            PlayerDataManager::Save();
            return;
        }

        // 敗北判定
        if (m_player->GetHp() <= 0)
        {
            m_battleResult = BattleResult::Lose;
            // セーブデータを削除（ニューゲームからやり直し）
            std::remove("Assets/Data/playerdata.json");
            PlayerDataManager::Init();
            PlayerDataManager::Save();
            return;
        }

        if (m_turnManager.IsEnemyTurn())
        {
            m_highlighter.ClearPlayerHighlight(m_gridMap);
            m_highlighter.ClearEnemyHighlight(m_gridMap);

            switch (m_enemyPhase)
            {
            case EnemyTurnPhase::WaitStart:
            {
                m_enemyActionDelay -= deltaTime;
                if (m_enemyActionDelay <= 0)
                {
                    if (m_enemies.empty())
                        m_enemyPhase = EnemyTurnPhase::EndTurn;
                    else
                        m_enemyPhase = EnemyTurnPhase::PoisonTick;
                }
                break;
            }
            case EnemyTurnPhase::PoisonTick:
            {
                m_enemyActionDelay -= deltaTime;
                if (m_enemyActionDelay > 0) break;

                // 現在の敵にサブティックが残っていれば1回（連続攻撃風・少しずらす）
                if (m_poisonSubTicks > 0)
                {
                    Enemy* e = (m_poisonIdx < (int)m_enemies.size()) ? m_enemies[m_poisonIdx] : nullptr;
                    if (e && e->GetHp() > 0)
                    {
                        int pd = e->GetBuffManager().TickPoison();   // 現在の毒→ダメージ、毒-1
                        if (pd > 0)
                        {
                            e->TakeDamage(pd, DamageFeel::Poison);
                            ScreenShake::Add(0.15f);                 // 毒でも画面振動
                        }
                        if (e->GetHp() <= 0) m_poisonSubTicks = 1;   // 死んだら残り打ち切り
                    }
                    m_poisonSubTicks--;
                    m_enemyActionDelay = 0.12f;                       // 連続の間隔
                    if (m_poisonSubTicks <= 0) m_poisonIdx++;         // この敵完了→次へ
                    break;
                }

                // 次の「生存＆毒持ち」敵を探す
                while (m_poisonIdx < (int)m_enemies.size())
                {
                    Enemy* e = m_enemies[m_poisonIdx];
                    if (e->GetHp() > 0 && e->GetBuffManager().GetBuffValue(BuffType::Poison) > 0) break;
                    m_poisonIdx++;
                }

                if (m_poisonIdx >= (int)m_enemies.size())
                {
                    ProcessDeadEnemies();
                    m_currentEnemyIdx = 0;
                    m_enemyPhase = m_enemies.empty()
                        ? EnemyTurnPhase::EndTurn : EnemyTurnPhase::ProcessEnemy;
                    break;
                }

                // この敵のサブティック数をセット（1 + 脈動）
                m_poisonSubTicks = 1 + m_player->GetBuffManager().GetBuffValue(BuffType::ToxicRhythm);
                m_enemyActionDelay = 0.0f;   // すぐ最初のティックへ
                break;
            }


            case EnemyTurnPhase::ProcessEnemy:
            {
                while (m_currentEnemyIdx < (int)m_enemies.size()
                    && (m_enemies[m_currentEnemyIdx]->GetHp() <= 0
                        || m_enemies[m_currentEnemyIdx]->TakeJustSummoned()))
                    m_currentEnemyIdx++;

                if (m_currentEnemyIdx >= (int)m_enemies.size())
                {
                    m_enemyPhase = EnemyTurnPhase::EndTurn;
                    break;
                }

                Enemy* enemy = m_enemies[m_currentEnemyIdx];

                int ai = enemy->GetActionIndex();
                int tC = m_playerCol, tR = m_playerRow;
                if (m_decoyCol >= 0)
                {
                    if (enemy->IsActionUnavoidable(ai))
                    {
                        tC = m_decoyCol; tR = m_decoyRow;   // 必中はデコイ優先（近さ無視）
                    }
                    else
                    {
                        int dP = abs(enemy->gridCol - m_playerCol) + abs(enemy->gridRow - m_playerRow);
                        int dD = abs(enemy->gridCol - m_decoyCol) + abs(enemy->gridRow - m_decoyRow);
                        if (dD < dP) { tC = m_decoyCol; tR = m_decoyRow; }
                    }
                }

                // 現在の行動を実行
                bool targetedDecoy = (m_decoyCol >= 0 && tC == m_decoyCol && tR == m_decoyRow);
                bool atk = false;
                int damage = enemy->ExecuteAction(ai, m_playerCol, m_playerRow, m_gridMap, m_player, m_enemies, tC, tR, &atk);
                for (auto& sm : enemy->TakePendingSummons())
                    SummonNear(enemy, sm.first, sm.second);

                m_playerCol = m_player->gridCol;
                m_playerRow = m_player->gridRow;
                if (damage > 0)
                {
                    int hpBefore = m_player->GetHp();
                    m_player->TakeDamage(damage);
                    bool fullyBlocked = (m_player->GetHp() == hpBefore);   // HPが減ってない＝完全に受けきった

                    int th = m_player->GetBuffManager().GetBuffValue(BuffType::Thorns);
                    if (th > 0 && enemy->GetHp() > 0) enemy->TakeDamage(th);

                    int rip = m_player->GetBuffManager().GetBuffValue(BuffType::Riposte);
                    if (fullyBlocked && rip > 0 && enemy->GetHp() > 0)
                    {
                        enemy->TakeDamage(rip);
                        float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                        float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                        EffectManager::Play("hit", wx, 0.6f, wz);   // 反撃の一撃
                        ScreenShake::Add(0.2f);
                    }
                }
                enemy->SetActionIndex(ai + 1);

                // デコイを狙って攻撃した時だけ破壊（移動だけでは壊れない）
                if (targetedDecoy && atk)
                {
                    float x = (m_decoyCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                    float z = (m_decoyRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                    EffectManager::Play("decoy_break", x, 0.5f, z);
                    ScreenShake::Add(0.3f);
                    m_decoyCol = -1; m_decoyRow = -1;
                }

                // 突進：止まるマス（罠マス＋最終マス）を先に列挙し、間は滑らかに移動
                if (enemy->DidDash() && !enemy->GetMovePath().empty())
                {
                    const auto& path = enemy->GetMovePath();
                    m_dashEnemy = enemy;
                    m_dashStops.clear();
                    for (auto& c : path)
                        if (m_gridMap->GetCell(c.first, c.second).tileEffect.active)
                            m_dashStops.push_back(c);
                    if (m_dashStops.empty() || m_dashStops.back() != path.back())
                        m_dashStops.push_back(path.back());   // 最終マスは必ず終点

                    m_dashStopIdx = 0;
                    DashGlideTo(m_dashStops[0].first, m_dashStops[0].second);
                    m_enemyPhase = EnemyTurnPhase::DashStep;
                    break;
                }

                // 通過した各マスの罠を発火（複数マス移動で飛び越えないように）
                for (auto& mp : enemy->GetMovePath())
                {
                    auto& pcell = m_gridMap->GetCell(mp.first, mp.second);
                    if (pcell.tileEffect.active)
                        CardExecutor::TriggerTrap(pcell, enemy, mp.first, mp.second, m_gridMap, m_enemies);
                    if (enemy->GetHp() <= 0) break;
                }
                auto& cell = m_gridMap->GetCell(enemy->gridCol, enemy->gridRow);
                if (enemy->GetHp() > 0 && cell.tileEffect.active)
                    CardExecutor::TriggerTrap(cell, enemy, enemy->gridCol, enemy->gridRow, m_gridMap, m_enemies);

                m_enemyPhase = EnemyTurnPhase::WaitAction;
                m_enemyActionDelay = ENEMY_ACTION_PAUSE;
                break;
            }

            case EnemyTurnPhase::WaitAction:
            {
                m_enemyActionDelay -= deltaTime;

                bool anyMoving = false;
                for (auto enemy : m_enemies)
                    if (enemy->IsMoving() || enemy->IsLunging() || enemy->IsJumping()) anyMoving = true;

                if (m_enemyActionDelay <= 0 && !anyMoving)
                {
                    Enemy* enemy = (m_currentEnemyIdx < (int)m_enemies.size())
                        ? m_enemies[m_currentEnemyIdx] : nullptr;

                    // 同じ敵にまだ行動が残っていれば続けて実行
                    if (enemy && enemy->GetHp() > 0 && enemy->HasMoreActions())
                    {
                        m_enemyPhase = EnemyTurnPhase::ProcessEnemy;
                    }
                    else
                    {
                        m_enemyPhase = EnemyTurnPhase::NextEnemy;
                        m_enemyActionDelay = ENEMY_BETWEEN_PAUSE;
                    }
                }
                break;
            }

            case EnemyTurnPhase::NextEnemy:
                m_enemyActionDelay -= deltaTime;
                if (m_enemyActionDelay <= 0)
                {
                    m_currentEnemyIdx++;
                    if (m_currentEnemyIdx >= (int)m_enemies.size())
                        m_enemyPhase = EnemyTurnPhase::EndTurn;
                    else
                        m_enemyPhase = EnemyTurnPhase::ProcessEnemy;
                }
                break;

            case EnemyTurnPhase::EndTurn:
                ProcessDeadEnemies();
                for (auto enemy : m_enemies)
                    enemy->GetBuffManager().OnTurnEnd(false);  
                m_turnManager.EndTurn();
                break;
            case EnemyTurnPhase::DashStep:
            {
                if (!m_dashEnemy) { m_enemyPhase = EnemyTurnPhase::WaitAction; m_enemyActionDelay = ENEMY_ACTION_PAUSE; break; }
                if (m_dashEnemy->IsMoving()) break;   // グライド中は待つ

                // 到着した stop の罠を発動（罠でなければ no-op）
                if (m_dashStopIdx < (int)m_dashStops.size())
                {
                    auto s = m_dashStops[m_dashStopIdx];
                    auto& cell = m_gridMap->GetCell(s.first, s.second);
                    if (cell.tileEffect.active)
                        CardExecutor::TriggerTrap(cell, m_dashEnemy, s.first, s.second, m_gridMap, m_enemies);
                    m_dashStopIdx++;
                }

                // 罠で倒れたら終了
                if (m_dashEnemy->GetHp() <= 0)
                {
                    m_dashEnemy = nullptr;
                    m_enemyPhase = EnemyTurnPhase::WaitAction; m_enemyActionDelay = ENEMY_ACTION_PAUSE;
                    break;
                }

                // 次の stop へグライド、なければ終了
                if (m_dashStopIdx < (int)m_dashStops.size())
                    DashGlideTo(m_dashStops[m_dashStopIdx].first, m_dashStops[m_dashStopIdx].second);
                else
                {
                    m_dashEnemy = nullptr;
                    m_enemyPhase = EnemyTurnPhase::WaitAction; m_enemyActionDelay = ENEMY_ACTION_PAUSE;
                }
                break;
            }
            }
        }

        int highlightCardIndex2 = m_selectedCardIndex >= 0 ? m_selectedCardIndex : m_hoveredCardIndex;
        if (highlightCardIndex2 >= 0 && highlightCardIndex2 < (int)m_hand.GetCards().size())
        {
            CardData dataThrow2 = *m_hand.GetCards()[highlightCardIndex2]->GetData();
            ApplyKnifeThrow(dataThrow2, m_player);
            const CardData* data = &dataThrow2;

            // 手札エリアの範囲を計算
            RECT cardArea = { 0, 0, 0, 0 };
            if (!m_hand.GetCards().empty())
            {
                int numCards = (int)m_hand.GetCards().size();
                float totalW = numCards * (CARD_WIDTH + 10.0f);
                float leftX = m_screenWidth / 2.0f - totalW / 2.0f;
                float rightX = leftX + totalW;
                float topY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;
                cardArea = { (LONG)leftX, (LONG)topY, (LONG)rightX, (LONG)m_screenHeight };
            }

            m_highlighter.SetTravelPath(&m_movePath);

            m_highlighter.UpdatePlayerHighlight(
                m_playerCol, m_playerRow, data,
                m_enemies, m_gridMap, m_player,
                m_highlightTimer, m_hoveredCell,
                m_renderer3D, m_screenWidth, m_screenHeight,
                cardArea, m_player->GetBuffManager().HasBuff(BuffType::MoveLock));
        }
        else
        {
            m_highlighter.ClearPlayerHighlight(m_gridMap);
        }
    }
}

void BattleScene::Draw()
{
    // 背景
    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(
        TextureManager::Get("battle_bg"),
        0.0f, 0.0f,
        (float)m_screenWidth, (float)m_screenHeight,
        0.0f, XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f));
    m_spriteRenderer->End();

    // 3D描画
    m_renderer3D->Begin();

    // 選択中のマスを浮かせる（滑らかに）
    {
        std::set<std::pair<int, int>> raised;
        if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size())
        {
            CardData dThrow = *m_hand.GetCards()[m_selectedCardIndex]->GetData();
            ApplyKnifeThrow(dThrow, m_player);
            const CardData* d = &dThrow;
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
                if (m_hoveredCell.first >= 0)
                    RangeShape::CardinalAim(m_playerCol, m_playerRow,
                        m_hoveredCell.first, m_hoveredCell.second, aimDx, aimDy);
                if (aimDx == 0 && aimDy == 0) aimDy = -1;   // 初期は上向き

                for (auto& c : BattleHighlighter::GetCandidates(
                    m_playerCol, m_playerRow, d->rangeType, range, aimDx, aimDy))
                    raised.insert(c);
            }
            else if (d->mainEffect.type == CardEffectType::Detonate)
            {
                for (auto& cc : BattleHighlighter::GetCandidates(
                    m_playerCol, m_playerRow, d->rangeType, d->range))
                {
                    if (cc.first < 0 || cc.first >= m_gridMap->GetCols() ||
                        cc.second < 0 || cc.second >= m_gridMap->GetRows()) continue;
                    auto& hc = m_gridMap->GetCell(cc.first, cc.second);
                    if (hc.tileEffect.active && !hc.tileEffect.enemyOwned)   // 敵床は光らせない
                        raised.insert(cc);
                }
            }
            else if (d->mainEffect.type == CardEffectType::DetonateAt)
            {
                // 置いてある罠（範囲内）をハイライト
                for (auto& cc : BattleHighlighter::GetCandidates(
                    m_playerCol, m_playerRow, d->rangeType, d->range))
                {
                    if (cc.first < 0 || cc.first >= m_gridMap->GetCols() ||
                        cc.second < 0 || cc.second >= m_gridMap->GetRows()) continue;
                    auto& hc = m_gridMap->GetCell(cc.first, cc.second);
                    if (hc.tileEffect.active && !hc.tileEffect.enemyOwned)   // 敵床は光らせない
                        raised.insert(cc);
                }

                // 3x3プレビュー
                if (m_hoveredCell.first >= 0 &&
                    RangeShape::Contains(m_playerCol, m_playerRow,
                        m_hoveredCell.first, m_hoveredCell.second, d->rangeType, d->range) &&
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
                for (auto& cc : BattleHighlighter::GetCandidates(
                    m_playerCol, m_playerRow, d->rangeType, d->range))
                {
                    if (cc.first < 0 || cc.first >= m_gridMap->GetCols() ||
                        cc.second < 0 || cc.second >= m_gridMap->GetRows()) continue;
                    auto& hc = m_gridMap->GetCell(cc.first, cc.second);
                    if (hc.tileEffect.active && !hc.tileEffect.enemyOwned)   // 敵床は光らせない
                        raised.insert(cc);
                }

                // 連鎖の起点プレビュー（既存のまま）
                if (m_hoveredCell.first >= 0 &&
                    RangeShape::Contains(m_playerCol, m_playerRow,
                        m_hoveredCell.first, m_hoveredCell.second, d->rangeType, d->range) &&
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
                && d->mainEffect.type != CardEffectType::PlaceTrap
                && d->mainEffect.type != CardEffectType::Detonate
                && d->mainEffect.type != CardEffectType::DetonateAt)
            {
                raised.insert({ m_playerCol, m_playerRow });
                m_gridMap->GetCell(m_playerCol, m_playerRow).gameObject.color
                    = HighlightPalette::ForCard(d->type);   // 危険色よりスキル色を優先
            }
            else if (m_hoveredCell.first >= 0)
            {
                // 効果範囲内のマスだけ浮かせる
                int range = d->range;
                if (m_player->GetBuffManager().HasBuff(BuffType::Reposition))
                    range += m_player->GetBuffManager().GetBuffValue(BuffType::Reposition);

                if (RangeShape::Contains(m_playerCol, m_playerRow,
                    m_hoveredCell.first, m_hoveredCell.second, d->rangeType, range))
                    raised.insert({ m_hoveredCell.first, m_hoveredCell.second });
            }
        }

        for (int row = 0; row < m_gridMap->GetRows(); row++)
            for (int col = 0; col < m_gridMap->GetCols(); col++)
            {
                auto& cell = m_gridMap->GetCell(col, row);
                float target = raised.count({ col, row }) ? 0.30f : 0.0f;
                cell.gameObject.worldY += (target - cell.gameObject.worldY) * 0.2f;   // 補間
            }
    }

    for (int row = 0; row < m_gridMap->GetRows(); row++)
    {
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            float x = (col - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float z = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;
            auto& cell = m_gridMap->GetCell(col, row);
            float t = cell.gameObject.worldY / 0.10f;              // 0=通常, 1=浮ききった状態
            float size = 1.0f + 0.02f * t;                          // 少しだけ拡大
            m_renderer3D->DrawTile(cell.gameObject.texture, x, z, size, cell.gameObject.color, cell.gameObject.worldY);
        }
    }

    // 敵の攻撃範囲マーカー（半透明の四角＋四隅ブラケット）をマスの上に重ねる
    for (auto& mk : m_highlighter.GetThreatMarks())
    {
        float x = (mk.col - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float z = (mk.row - m_gridMap->GetRows() / 2.0f) * 1.1f;
        float wy = m_gridMap->GetCell(mk.col, mk.row).gameObject.worldY + 0.02f;
        m_renderer3D->DrawTile(TextureManager::Get("ui_threat"), x, z, 0.98f, mk.color, wy);
    }

    // プレイヤーの攻撃範囲マーカー（水色ブラケット＋クロスヘア）
    for (auto& pc : m_highlighter.GetReachCells())
    {
        float x = (pc.first - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float z = (pc.second - m_gridMap->GetRows() / 2.0f) * 1.1f;
        float wy = m_gridMap->GetCell(pc.first, pc.second).gameObject.worldY + 0.03f;
        m_renderer3D->DrawTile(TextureManager::Get("ui_reach"), x, z, 0.98f, XMFLOAT4(1, 1, 1, 1), wy);
    }

    // 罠の表示
    for (int row = 0; row < m_gridMap->GetRows(); row++)
    {
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            auto& cell = m_gridMap->GetCell(col, row);
            if (cell.tileEffect.active)
            {
                float x = (col - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float z = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;

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

                // 設置者を示す下地（自分=水色 / 敵=赤）。罠の少し下・少し大きめでフチのように出す
                XMFLOAT4 ownerCol = cell.tileEffect.enemyOwned
                    ? XMFLOAT4(1.0f, 0.30f, 0.20f, 0.55f)    // 敵＝赤
                    : XMFLOAT4(0.30f, 0.75f, 1.0f, 0.55f);   // 自分＝水色
                m_renderer3D->DrawTile(m_whiteTexture, x, z, sz * 1.14f, ownerCol,
                    cell.gameObject.worldY + 0.035f);

                if (def && !def->texture.empty())
                    m_renderer3D->DrawTile(TextureManager::Get(def->texture), x, z, sz,
                        XMFLOAT4(bright, bright, bright, 1.0f), cell.gameObject.worldY + 0.04f);
                else
                    m_renderer3D->DrawTile(m_whiteTexture, x, z, sz,
                        XMFLOAT4(terrainColor.x * bright, terrainColor.y * bright, terrainColor.z * bright, terrainColor.w),
                        cell.gameObject.worldY + 0.04f);
            }
        }
    }

    if (m_decoyCol >= 0)
    {
        float x = (m_decoyCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float z = (m_decoyRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
        m_renderer3D->DrawBillboard(TextureManager::Get("kakashi"), x, 0.05f, z , 0.8f, 0.8f, 0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // 敵は乗っているマスの高さに合わせる
    for (auto enemy : m_enemies)
        enemy->worldY = 0.05f + m_gridMap->GetCell(enemy->gridCol, enemy->gridRow).gameObject.worldY;

    // 移動不可マスの×マーク（深度テストOFF、敵より先に描画）
    auto& blockedCells = m_highlighter.GetOutOfRangeCells();
    if (!blockedCells.empty())
    {
        m_renderer3D->SetDepthEnabled(false);
        for (auto& [col, row] : blockedCells)
        {
            float cx = (col - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float cz = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;

            m_renderer3D->DrawTileEx(m_whiteTexture, cx, cz,
                0.7f, 0.06f, XM_PIDIV4,
                XMFLOAT4(0.8f, 0.2f, 0.2f, 0.7f));
            m_renderer3D->DrawTileEx(m_whiteTexture, cx, cz,
                0.7f, 0.06f, -XM_PIDIV4,
                XMFLOAT4(0.8f, 0.2f, 0.2f, 0.7f));
        }
        m_renderer3D->SetDepthEnabled(true);
    }

    // 選択/ホバー中の敵をコーナーマークで囲む
    {
        int hi = m_battleUI->GetPanelHoveredEnemy();
        int target = (hi >= 0) ? hi : m_selectedEnemyRange;
        if (target >= 0 && target < (int)m_enemies.size())
        {
            Enemy* e = m_enemies[target];
            m_renderer3D->SetDepthEnabled(false);
            XMFLOAT4 line(0.2f, 0.7f, 1.0f, 1.0f);
            const float H = 0.5f, L = 0.18f, T = 0.05f;   // 半径・角の長さ・太さ

            for (auto& [dc, dr] : e->GetGridShape())
            {
                float cx = (e->gridCol + dc - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float cz = (e->gridRow + dr - m_gridMap->GetRows() / 2.0f) * 1.1f;
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

    m_player->worldY = m_gridMap->GetCell(m_playerCol, m_playerRow).gameObject.worldY;
    // 背水：赤い残像（低HP＋火事場/決死 の間）
    bool berserk = (m_player->GetHp() * 2 <= m_player->GetMaxHp())
        && (m_player->GetBuffManager().HasBuff(BuffType::LastStand)
            || m_player->GetBuffManager().HasBuff(BuffType::DeepStand));
    int hp = m_player->GetHp(), mhp = m_player->GetMaxHp();
    bool hasLast = m_player->GetBuffManager().HasBuff(BuffType::LastStand);   // 1/2パワー
    bool hasDeep = m_player->GetBuffManager().HasBuff(BuffType::DeepStand);   // 1/4パワー
    float pulse = 0.5f + 0.5f * sinf(m_highlightTimer * 10.0f);               // 0..1 脈動

    auto ptex = TextureManager::Get("player");
    m_renderer3D->SetDepthEnabled(false);

    // 1/2以下：プレイヤーが赤く脈動（膨らむ赤オーラ）
    if (hp * 2 <= mhp && hasLast)
    {
        float sc = 1.0f + 0.15f * pulse;
        float a = 0.20f + 0.30f * pulse;
        m_renderer3D->DrawBillboard(ptex,
            m_player->worldX, m_player->worldY, m_player->worldZ,
            sc, sc, 0.0f, XMFLOAT4(1.0f, 0.2f, 0.15f, a));
    }

    // 1/4以下：赤い残像（1/2パワーがあれば残像自体が膨張脈動）
    if (hp * 4 <= mhp && hasDeep)
    {
        float sc = 1.0f + (hasLast ? 0.25f * pulse : 0.0f);   // 1/2パワーで膨らむ脈動
        for (int i = 1; i <= 3; i++)
        {
            float ph = m_highlightTimer * 14.0f + i * 1.2f;
            float ox = sinf(ph) * 0.10f * i;
            float oz = -0.09f * i;
            float a = 0.30f - (i - 1) * 0.08f;
            m_renderer3D->DrawBillboard(ptex,
                m_player->worldX + ox, m_player->worldY, m_player->worldZ + oz,
                sc, sc, 0.0f, XMFLOAT4(1.0f, 0.25f, 0.15f, a));   // ← サイズscで脈動
        }
    }

    m_renderer3D->SetDepthEnabled(true);
    m_player->Draw3D(m_renderer3D);   // 本体は前面

    // 敵：透明な四角が互いを隠さないよう、深度書き込みOFF＋奥→手前で描く
    m_renderer3D->SetDepthWrite(false);
    std::vector<Enemy*> drawOrder(m_enemies.begin(), m_enemies.end());
    std::sort(drawOrder.begin(), drawOrder.end(),
        [](Enemy* a, Enemy* b) { return a->worldZ < b->worldZ; });   // 奥(小Z)を先に、手前(大Z)を後に
    for (auto enemy : drawOrder)
        enemy->Draw3D(m_renderer3D);
    m_renderer3D->SetDepthWrite(true);

    m_renderer3D->SetDepthEnabled(false);

    m_renderer3D->SetDepthEnabled(false);
    EffectManager::Draw(m_renderer3D, TextureManager::Get("particle"));
    // 必中攻撃の対象にクロスヘア（デコイ優先）。対象の実座標に合わせて画面端でもズレないように
    bool showCross = false;
    for (auto enemy : m_enemies)
    {
        const EnemyAction* act = enemy->GetNextAction();
        if (act && act->target.unavoidable) { showCross = true; break; }
    }
    if (showCross)
    {
        float cx, cy, cz;
        if (m_decoyCol >= 0)
        {
            cx = (m_decoyCol - m_gridMap->GetCols() / 2.0f) * 1.1f;   // デコイはマス固定
            cz = (m_decoyRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
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
        float gx = (gc - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float gz = (gr - m_gridMap->GetRows() / 2.0f) * 1.1f;
        float gy = m_player->worldY + m_gridMap->GetCell(gc, gr).gameObject.worldY;   // ← マスの浮きぶん
        m_renderer3D->DrawBillboard(
            TextureManager::Get("player"),
            gx, gy, gz,
            m_player->width, m_player->height, 0.0f,
            XMFLOAT4(0.5f, 0.7f, 1.0f, 0.3f));
    }

    // 罠設置不可マーク（フラグだけ保存）
    float trapBlockX = -1, trapBlockY = -1;
    float trapAngle1 = 0, trapAngle2 = 0, trapLen = 0;

    if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size())
    {
        const CardData* selData = m_hand.GetCards()[m_selectedCardIndex]->GetData();
        if (selData && selData->mainEffect.type == CardEffectType::PlaceTrap)
        {
            auto& playerCell = m_gridMap->GetCell(m_playerCol, m_playerRow);
            if (playerCell.tileEffect.active)
            {
                float cx = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float cz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                float half = 0.4f;

                XMMATRIX vp = m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix();

                auto toScreen = [&](float wx, float wz, float& outX, float& outY) -> bool {
                    XMVECTOR w = XMVectorSet(wx, 0.01f, wz, 1.0f);
                    XMVECTOR c = XMVector4Transform(w, vp);
                    XMFLOAT4 cl;
                    XMStoreFloat4(&cl, c);
                    if (cl.w <= 0) return false;
                    outX = (cl.x / cl.w + 1.0f) * 0.5f * m_screenWidth;
                    outY = (1.0f - cl.y / cl.w) * 0.5f * m_screenHeight;
                    return true;
                    };

                float sx, sy, rx, ry, bx, by;
                if (toScreen(cx, cz, sx, sy) &&
                    toScreen(cx + half, cz, rx, ry) &&
                    toScreen(cx, cz + half, bx, by))
                {
                    trapBlockX = sx;
                    trapBlockY = sy;
                    float hw = rx - sx;
                    float hh = by - sy;
                    trapAngle1 = atan2f(hh, hw);
                    trapAngle2 = atan2f(hh, -hw);
                    trapLen = sqrtf(hw * hw + hh * hh) * 2.0f;
                }
            }
        }
    }
   
    // 罠設置不可×マーク描画
    if (trapBlockX >= 0)
    {
        m_spriteRenderer->Begin();
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            trapBlockX - trapLen / 2.0f, trapBlockY - 2.5f,
            trapLen, 5.0f, trapAngle1,
            XMFLOAT4(1.0f, 0.1f, 0.1f, 0.8f));
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            trapBlockX - trapLen / 2.0f, trapBlockY - 2.5f,
            trapLen, 5.0f, trapAngle2,
            XMFLOAT4(1.0f, 0.1f, 0.1f, 0.8f));
        m_spriteRenderer->End();
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
                float wx = (cc - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float wz = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;
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

    m_renderer3D->End();

    // 2D UI描画
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
    ctx.hoveredCell = m_hoveredCell;
    ctx.highlightTimer = m_highlightTimer;
    ctx.battleResult = m_battleResult;
    ctx.drops = &m_dropResult;
    ctx.showDrawPile = m_showDrawPile;
    ctx.cardSelecting = m_cardSelecting;
    ctx.showDiscardPile = m_showDiscardPile;
    ctx.showExhaustPile = m_showExhaustPile;
    ctx.screenWidth = m_screenWidth;
    ctx.screenHeight = m_screenHeight;
    ctx.cameraZoom = m_cameraZoom;
    ctx.isPlayerTurn = m_turnManager.IsPlayerTurn();
    ctx.showMoveArrows = !m_turnManager.IsPlayerTurn() || m_arrowRevealTimer <= 0.0f;
    ctx.mousePos = m_input.GetMousePos();
    ctx.outOfRangeCells = &m_highlighter.GetOutOfRangeCells();
    ctx.travelPath = &m_movePath;
    ctx.selectedEnemy = m_selectedEnemyRange;
    ctx.discardSelectCount = m_discardSelectCount;
    ctx.discardSelected = &m_discardSelected;
    ctx.discardViewMode = m_discardViewMode;
    ctx.rewardRelic = &m_rewardRelic;

    m_battleUI->Draw(ctx);
}

void BattleScene::HandleInput()
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
                else onChangeScene(SceneType::Result);   // 最終層クリア＝RUN CLEAR
            }
            else onChangeScene(SceneType::CardSelect);
        }
        return;
    }
    if (m_battleResult == BattleResult::Lose)
    {
        if (m_input.GetMouseButtonTrigger(0) && onChangeScene)
            onChangeScene(SceneType::Result);
        return;
    }

    if (!m_turnManager.IsPlayerTurn()) return; // プレイヤーターン以外は無視

    // 捨てるカードの選択中は他の操作を受け付けない
    if (m_discardSelectCount > 0)
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

        m_hoveredCardIndex = m_battleUI->GetCardAtScreenPos(mp);

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
                for (int idx : m_discardSelected)
                {
                    const CardData* dd = CardDataBase::Get(m_hand.GetCards()[idx]->GetId());
                    if (dd && dd->onDiscardEffect.hasEffect) effects.push_back(dd->onDiscardEffect);
                    m_battleUI->StartDiscardEffectAt(idx);
                    m_hand.DiscardAt(idx);
                    m_battleUI->OnCardRemoved(idx);
                }
                m_discardSelected.clear();
                m_discardSelectCount = 0;
                m_hoveredCardIndex = -1;
                for (auto& e : effects) ApplyDiscardEffect(e);   // ← 捨て終わってから発動
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
        return;
    }

    const float cardHideY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    const float cardHoverY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;

    const auto& cards = m_hand.GetCards();

    // 山札・捨て札ボタン判定
    float drawPileX = 20.0f;
    float drawPileY = m_screenHeight - 60.0f;
    float discardX = 80.0f;
    float discardY = m_screenHeight - 60.0f;

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
        else if (m_hoveredCell.first >= 0)
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
                        const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
                        for (int keep = (int)m_movePath.size() - 1; keep >= 0; keep--)
                        {
                            int b = moveRangeSel - keep;
                            int ptc = (keep == 0) ? m_playerCol : m_movePath[keep - 1].first;
                            int ptr = (keep == 0) ? m_playerRow : m_movePath[keep - 1].second;

                            std::set<std::pair<int, int>> blocked;
                            for (int i = 0; i < keep; i++) blocked.insert(m_movePath[i]);
                            blocked.insert({ m_playerCol, m_playerRow });
                            blocked.erase({ ptc, ptr });

                            std::queue<std::pair<int, int>> q;
                            std::map<std::pair<int, int>, std::pair<int, int>> par;
                            std::map<std::pair<int, int>, int> dist;
                            auto start = std::make_pair(ptc, ptr);
                            q.push(start); dist[start] = 0; par[start] = { -1, -1 };

                            bool found = false;
                            while (!q.empty())
                            {
                                auto [cc, rr] = q.front(); q.pop();
                                if (cc == hc && rr == hr) { found = true; break; }
                                if (dist[{cc, rr}] >= b) continue;
                                for (int d = 0; d < 4; d++)
                                {
                                    int nc = cc + dirs[d][0], nr = rr + dirs[d][1];
                                    if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
                                    auto np = std::make_pair(nc, nr);
                                    if (dist.count(np)) continue;
                                    if (blocked.count(np)) continue;
                                    if (m_gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
                                    dist[np] = dist[{cc, rr}] + 1;
                                    par[np] = { cc, rr };
                                    q.push(np);
                                }
                            }

                            if (found)
                            {
                                std::vector<std::pair<int, int>> conn;
                                auto cur = std::make_pair(hc, hr);
                                while (cur != start) { conn.push_back(cur); cur = par[cur]; }
                                std::reverse(conn.begin(), conn.end());

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
                    m_movePath.clear();

                    std::queue<std::pair<int, int>> q;
                    std::map<std::pair<int, int>, std::pair<int, int>> par;
                    std::map<std::pair<int, int>, int> dist;
                    auto start = std::make_pair(m_playerCol, m_playerRow);
                    q.push(start); dist[start] = 0; par[start] = { -1, -1 };

                    const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
                    bool found = false;

                    while (!q.empty())
                    {
                        auto [cc, rr] = q.front(); q.pop();
                        if (cc == hc && rr == hr) { found = true; break; }
                        if (dist[{cc, rr}] >= moveRangeSel) continue;

                        for (int d = 0; d < 4; d++)
                        {
                            int nc = cc + dirs[d][0], nr = rr + dirs[d][1];
                            if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
                            auto np = std::make_pair(nc, nr);
                            if (dist.count(np)) continue;
                            if (m_gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
                            dist[np] = dist[{cc, rr}] + 1;
                            par[np] = { cc, rr };
                            q.push(np);
                        }
                    }

                    if (found)
                    {
                        auto cur = std::make_pair(hc, hr);
                        while (cur != start) { m_movePath.push_back(cur); cur = par[cur]; }
                        std::reverse(m_movePath.begin(), m_movePath.end());
                    }
                }
            }
        }
    }
    else
    {
        m_pathBuilding = false;
        m_movePath.clear();
    }

    bool isOnCard = false;
    bool cardJustUsed = false;

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
                    usePath = &m_movePath;
                else if (m_pathBuilding && m_movePath.empty())
                    moveCanceled = true;
            }

            int targetCol = m_playerCol;
            int targetRow = m_playerRow;
            bool canTry = !moveCanceled;

            CardEffectType met = cards[m_selectedCardIndex]->GetData()->mainEffect.type;
            bool needCell = (met == CardEffectType::PlaceTrap || met == CardEffectType::DetonateAt 
                || met == CardEffectType::DetonateChain || met == CardEffectType::PlaceDecoy);
            if ((ct == CardType::Attack || ct == CardType::Move || needCell)
                && !usePath && !moveCanceled)
            {
                auto result = m_gridMap->GetClickedCell3D(
                    releasePos,
                    m_renderer3D->GetViewMatrix(),
                    m_renderer3D->GetProjectionMatrix(),
                    m_screenWidth,
                    m_screenHeight
                );
                if (result.cell)
                {
                    targetCol = result.col;
                    targetRow = result.row;
                }
                else
                {
                    canTry = false;
                }
            }

            if (canTry)
            {
                std::string cardId = card->GetId();
                int newPlayerCol = m_playerCol;
                int newPlayerRow = m_playerRow;

                float playCardX = CardVisual::HandSlotX(
                    m_selectedCardIndex, (int)cards.size(), (float)m_screenWidth);
                float playCardY = m_screenHeight - 30.0f;

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
                if (execResult.startChainDetonate)
                {
                    m_chainQueue.clear();
                    m_chainQueue.push_back({ execResult.chainCol, execResult.chainRow });
                    m_chainTimer = 0.0f;
                    m_chainFull = execResult.chainFull;
                }
                if (execResult.startSeqDetonate)
                {
                    m_detonateQueue = execResult.seqDetonateCells;
                    m_detonateFull = execResult.seqFull;
                    m_detonateTimer = 0.0f;   // すぐ1個目
                }

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
                    {
                        int oldCol = m_playerCol;
                        int oldRow = m_playerRow;
                        m_playerCol = newPlayerCol;
                        m_playerRow = newPlayerRow;
                        m_player->gridCol = m_playerCol;
                        m_player->gridRow = m_playerRow;
                        OnPlayerMoved();   // レリック：移動時
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
                            if (def && def->effect == "Slide")
                            {
                                isSlide = true;
                                int dx = m_playerCol - oldCol;
                                int dy = m_playerRow - oldRow;
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
                                float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                                float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                                m_player->StartMove(px, pz);

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
                        }
                    }

                    for (auto& drawnId : execResult.drawnCards)
                        m_battleUI->StartDrawCardEffect(drawnId);
                    m_battleUI->StartPlayCardEffect(dataPtr, m_selectedCardIndex);
                    ProcessDeadEnemies();
                    if (execResult.multiHitRemain > 0)
                    {
                        m_multiHitTargets = execResult.multiHitTargets;
                        m_multiHitRemain = execResult.multiHitRemain;
                        m_multiHitDamage = execResult.multiHitDamage;
                        m_multiHitTimer = MULTI_HIT_INTERVAL;
                    }
                    if (execResult.pendingSelection != CardEffectType::None)
                    {
                        m_cardSelecting = true;
                        m_selectingType = execResult.pendingSelection;
                        if (m_selectingType == CardEffectType::Search)
                            m_showDrawPile = true;
                        else if (m_selectingType == CardEffectType::Salvage)
                            m_showDiscardPile = true;
                    }

                    m_battleUI->OnCardRemoved(m_selectedCardIndex);
                    m_selectedCardIndex = -1;
                    cardJustUsed = true;
                    OnCardPlayed(dataPtr);

                    if (execResult.pendingDiscard > 0)
                    {
                        m_discardSelectCount = min(execResult.pendingDiscard, (int)m_hand.GetCards().size());
                        m_discardSelected.clear();
                        m_discardViewMode = false;
                    }
                }
            }
        }
    }

    if (m_input.GetMouseButtonRelease(0))
    {
        m_pathBuilding = false;
        m_movePath.clear();
        m_moveReleaseSuppress = false;
    }

    if (m_input.GetMouseButtonTrigger(0))
    {
        POINT mousePos = m_input.GetMousePos();

        // カードエリアにマウスがあるかチェック
        isOnCard = (m_battleUI->GetCardAtScreenPos(mousePos) >= 0);

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

        // ビューワー外クリックで閉じる
        else if (m_cardSelecting && (m_showDrawPile || m_showDiscardPile))
        {
            float bgX = m_screenWidth / 2.0f - 300.0f;
            float bgY = 50.0f;
            const float scale = 0.7f;
            const float cardW = CardVisual::CARD_W * scale;
            const float cardH = CardVisual::CARD_H * scale;
            const float gap = 12.0f;
            const int cols = 6;

            const auto& pile = m_showDrawPile
                ? m_deck.GetDrawPile()
                : m_deck.GetDiscardPile();

            std::vector<std::string> displayPile = pile;
            if (m_showDrawPile)
                std::sort(displayPile.begin(), displayPile.end());

            bool cardClicked = false;
            for (int i = 0; i < (int)displayPile.size(); i++)
            {
                int col = i % cols;
                int row = i / cols;
                float cx = bgX + 25.0f + col * (cardW + gap);
                float cy = bgY + 50.0f + row * (cardH + gap);

                if (mousePos.x >= cx && mousePos.x <= cx + cardW
                    && mousePos.y >= cy && mousePos.y <= cy + cardH)
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

                    cardClicked = true;
                    break;
                }
            }

            if (cardClicked)
            {
                m_cardSelecting = false;
                m_selectingType = CardEffectType::None;
                m_showDrawPile = false;
                m_showDiscardPile = false;
            }
            else
            {
                // ビューワー外クリックで選択キャンセル
                bool outsideBg = mousePos.x < bgX || mousePos.x > bgX + 600.0f
                    || mousePos.y < bgY || mousePos.y > bgY + 580.0f;
                if (outsideBg)
                {
                    m_cardSelecting = false;
                    m_selectingType = CardEffectType::None;
                    m_showDrawPile = false;
                    m_showDiscardPile = false;
                }
            }
        }
        else if (m_showDrawPile || m_showDiscardPile || m_showExhaustPile)
        {
            float bgX = m_screenWidth / 2.0f - 300.0f;
            float bgY = 50.0f;
            bool outsideBg = mousePos.x < bgX || mousePos.x > bgX + 600.0f
                || mousePos.y < bgY || mousePos.y > bgY + 580.0f;
            if (outsideBg)
            {
                m_showDrawPile = false;
                m_showDiscardPile = false;
                m_showExhaustPile = false;
            }
        }

    
        if (result.cell && !isOnCard)
        {
            if (m_selectedCardIndex >= 0
                && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
            {
                const Card* card = m_hand.GetCards()[m_selectedCardIndex];
                std::string cardId = card->GetId();
                CardData dataCopy = *card->GetData();
                ApplyKnifeThrow(dataCopy, m_player);

                int newPlayerCol = m_playerCol;
                int newPlayerRow = m_playerRow;

                float playCardX = CardVisual::HandSlotX(
                    m_selectedCardIndex, (int)cards.size(), (float)m_screenWidth);
                float playCardY = m_screenHeight - 30.0f;

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
                    result.col, result.row,
                    m_player, m_enemies, m_gridMap,
                    m_playerCol, m_playerRow,
                    m_hand, m_selectedCardIndex, m_deck,
                    newPlayerCol, newPlayerRow
                );
                if (execResult.startChainDetonate)
                {
                    m_chainQueue.clear();
                    m_chainQueue.push_back({ execResult.chainCol, execResult.chainRow });
                    m_chainTimer = 0.0f;
                    m_chainFull = execResult.chainFull;
                }
                if (execResult.startSeqDetonate)
                {
                    m_detonateQueue = execResult.seqDetonateCells;
                    m_detonateFull = execResult.seqFull;
                    m_detonateTimer = 0.0f;   // すぐ1個目
                }
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

                    // プレイヤー座標を更新
                        if (newPlayerCol != m_playerCol || newPlayerRow != m_playerRow)
                        {
                            int oldCol = m_playerCol;
                            int oldRow = m_playerRow;
                            m_playerCol = newPlayerCol;
                            m_playerRow = newPlayerRow;
                            m_player->gridCol = m_playerCol;
                            m_player->gridRow = m_playerRow;
                            OnPlayerMoved();   // レリック：移動時
                            float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                            float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                            m_player->StartMove(px, pz);

                            auto& movedCell = m_gridMap->GetCell(m_playerCol, m_playerRow);

                            bool isSlide = false;
                            int slideDx = 0, slideDy = 0;
                            if (movedCell.tileEffect.active)
                            {
                                const TerrainDef* def = TerrainDataBase::Get(movedCell.tileEffect.id);
                                if (def && def->effect == "Slide")
                                {
                                    isSlide = true;
                                    int dx = m_playerCol - oldCol;
                                    int dy = m_playerRow - oldRow;
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
                                    float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                                    float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                                    m_player->StartMove(px, pz);

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
                            }
                        }
                    


                    for (auto& drawnId : execResult.drawnCards)
                        m_battleUI->StartDrawCardEffect(drawnId);
                    m_battleUI->StartPlayCardEffect(dataPtr, m_selectedCardIndex);

                    ProcessDeadEnemies();

                    if (execResult.multiHitRemain > 0)
                    {
                        m_multiHitTargets = execResult.multiHitTargets;
                        m_multiHitRemain = execResult.multiHitRemain;
                        m_multiHitDamage = execResult.multiHitDamage;
                        m_multiHitTimer = MULTI_HIT_INTERVAL;
                    }

                    if (execResult.pendingSelection != CardEffectType::None)
                    {
                        m_cardSelecting = true;
                        m_selectingType = execResult.pendingSelection;
                        if (m_selectingType == CardEffectType::Search)
                            m_showDrawPile = true;
                        else if (m_selectingType == CardEffectType::Salvage)
                            m_showDiscardPile = true;
                    }

                    if (execResult.pendingDiscard > 0)
                    {
                        m_discardSelectCount = min(execResult.pendingDiscard, (int)m_hand.GetCards().size());
                        m_discardSelected.clear();
                        m_discardViewMode = false;
                    }

                    m_battleUI->OnCardRemoved(m_selectedCardIndex);
                    m_selectedCardIndex = -1;
                    cardJustUsed = true;
                    OnCardPlayed(dataPtr);
                }
            }
        }
            else if (m_selectedCardIndex >= 0 && !isOnCard
                && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
                {
                    m_selectedCardIndex = -1;
        }
    }

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
            int cost = m_hand.GetCards()[cardUnder]->GetData()->cost;
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
    if (m_hoveredCardIndex != m_prevHoveredCard)
    {
        if (m_hoveredCardIndex >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
        m_prevHoveredCard = m_hoveredCardIndex;
    }

        // ターンエンドボタン（右下）
        if (m_battleResult == BattleResult::None)
        {
            float btnX = m_screenWidth - 160.0f;
            float btnY = m_screenHeight - 60.0f;
            float btnW = 140.0f;
            float btnH = 40.0f;

            if (m_input.GetMouseButtonTrigger(0))
            {
                POINT mp = m_input.GetMousePos();
                if (mp.x >= btnX && mp.x <= btnX + btnW
                    && mp.y >= btnY && mp.y <= btnY + btnH)
                {
                    Audio::PlaySE("Assets/Sound/se/click.mp3");
                    m_turnManager.EndTurn();
                }
            }
        }

        // 敵パネルクリック → カメラ移動
        if (m_input.GetMouseButtonTrigger(0))
        {
            POINT mp = m_input.GetMousePos();
            float panelX = m_screenWidth - 250.0f;
            float panelY = 50.0f;
            float panelW = 240.0f;
            float entryH = 90.0f;

            for (int i = 0; i < (int)m_enemies.size(); i++)
            {
                float entryY = panelY + i * (entryH + 5.0f);
                if (mp.x >= panelX && mp.x <= panelX + panelW
                    && mp.y >= entryY && mp.y <= entryY + entryH)
                {
                    m_selectedEnemyRange = (m_selectedEnemyRange == i) ? -1 : i;
                    m_cameraOffsetX = m_enemies[i]->worldX;
                    m_cameraOffsetZ = m_enemies[i]->worldZ;
                    break;
                }
            }
        }
}

void BattleScene::ProcessDeadEnemies()
{
    std::vector<Enemy*> toDelete;
    for (auto enemy : m_enemies)
        if (enemy->IsDeathFinished()) 
            toDelete.push_back(enemy);

    if (toDelete.empty()) return;   // 何も死んでなければ何もしない

    // 選択中の敵を覚えておく（死んでいたら解除）
    Enemy* selected = nullptr;
    if (m_selectedEnemyRange >= 0 && m_selectedEnemyRange < (int)m_enemies.size())
    {
        selected = m_enemies[m_selectedEnemyRange];
        for (auto d : toDelete)
            if (d == selected) { selected = nullptr; break; }
    }


    for (auto enemy : toDelete)
    {
        for (auto& [dc, dr] : enemy->GetGridShape())
            m_gridMap->SetCellType(
                enemy->gridCol + dc,
                enemy->gridRow + dr,
                CellType::Empty);

        m_enemies.erase(
            std::remove(m_enemies.begin(), m_enemies.end(), enemy),
            m_enemies.end()
        );

        m_defeatedEnemyIds.push_back(enemy->GetId());

        PlayerDataManager::GetData().gold += RelicManager::SumValue("killGold");   // レリック
        m_player->Heal(RelicManager::SumValue("killHeal"));                        // レリック
        if (int ks = RelicManager::SumValue("killStrength"))                       // レリック（撃破で攻撃UP）
        {
            Buff b; b.type = BuffType::AttackUp; b.value = ks; b.duration = -1;
            b.name = L""; b.description = L"";
            m_player->GetBuffManager().AddBuff(b);
        }
        // 毒の伝染レリック：毒持ちが倒れたら周囲の敵へ同量の毒を分散
        if (RelicManager::HasKind("poisonSpread"))
        {
            int pz = enemy->GetBuffManager().GetBuffValue(BuffType::Poison);
            if (pz > 0)
            {
                for (auto other : m_enemies)   // 死んだ敵はerase済み＝生存のみ対象
                {
                    if (other->GetHp() <= 0) continue;   // 同時に死んだ敵は除外
                    int dc = abs(other->gridCol - enemy->gridCol);
                    int dr = abs(other->gridRow - enemy->gridRow);
                    if (dc > 1 || dr > 1) continue;      // 周囲8マスのみ

                    Buff pb; pb.type = BuffType::Poison; pb.value = pz; pb.duration = pz;
                    pb.name = BuffInfo::Get(BuffType::Poison).name; pb.description = L"";
                    other->GetBuffManager().AddBuff(pb);

                    float wx = (other->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                    float wz = (other->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                    EffectManager::Play("poison_apply", wx, 0.5f, wz);
                }
            }
        }

        delete enemy;
    }

    // 削除後に選択中の敵の新しいindexを探し直す
    m_selectedEnemyRange = -1;
    if (selected)
    {
        for (int i = 0; i < (int)m_enemies.size(); i++)
            if (m_enemies[i] == selected) { m_selectedEnemyRange = i; break; }
    }
}

void BattleScene::ApplyDiscardEffect(const CardEffectData& e)
{
    if (!e.hasEffect) return;
    switch (e.type)
    {
    case CardEffectType::Draw:
        for (int i = 0; i < e.value; i++)
        {
            std::string id = m_deck.DrawCard();
            if (!id.empty()) { m_hand.AddCard(id); m_battleUI->StartDrawCardEffect(id); }
        }
        break;
    case CardEffectType::AddEnergy: m_player->AddEnergy(e.value); break;
    case CardEffectType::Block:     m_player->AddBlock(m_player->GetBuffManager().GetFinalBlock(e.value)); break;
    case CardEffectType::CreateCard: for (int i = 0; i < e.value; i++) { m_hand.AddCard(e.cardId); m_battleUI->StartDrawCardEffect(e.cardId); } break;
    case CardEffectType::ApplyDebuff:   // 捨てたら全敵にデバフ（毒など）
    {
        BuffType bt = StringToBuffType(e.buffType);
        for (auto enemy : m_enemies)
        {
            if (enemy->GetHp() <= 0) continue;
            Buff b; b.type = bt; b.value = e.value; b.duration = e.value;
            b.name = BuffInfo::Get(bt).name; b.description = L"";
            enemy->GetBuffManager().AddBuff(b);
            float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
            EffectManager::Play("poison_apply", wx, 0.5f, wz);
        }
        break;
    }
    default: break;
    }
}

void BattleScene::AutoDiscardAll()
{
    std::vector<CardEffectData> effects;
    for (int i = (int)m_hand.GetCards().size() - 1; i >= 0; i--)   // 後ろから消す
    {
        const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
        if (dd && dd->onDiscardEffect.hasEffect) effects.push_back(dd->onDiscardEffect);
        m_battleUI->StartDiscardEffectAt(i);
        m_hand.DiscardAt(i);
        m_battleUI->OnCardRemoved(i);
    }
    for (auto& e : effects) ApplyDiscardEffect(e);   // 全部捨ててから発動
}

void BattleScene::RunTurnCycle()
{
    int n = RelicManager::SumValue("turnCycle");
    if (n <= 0) return;

    // 追加で n 枚引く（7→8枚）
    for (int i = 0; i < n; i++)
    {
        std::string id = m_deck.DrawCard();
        if (!id.empty()) { m_hand.AddCard(id); m_battleUI->StartDrawCardEffect(id); }
    }

    // n 枚を「選んで捨てる」既存UIを起動（8→7枚、選んだカードのonDiscardが発動）
    m_discardSelectCount = min(n, (int)m_hand.GetCards().size());
}

void BattleScene::DashGlideTo(int c, int r)
{
    if (!m_dashEnemy) return;
    float wx = (c - m_gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (r - m_gridMap->GetRows() / 2.0f) * 1.1f;
    float dist = fabsf(wx - m_dashEnemy->worldX) + fabsf(wz - m_dashEnemy->worldZ);
    float dur = (dist / 1.1f) * 0.07f;   // 1マスあたり0.07秒＝距離で時間が伸びる＝速度一定感を避けつつ滑らか
    if (dur < 0.06f) dur = 0.06f;
    m_dashEnemy->StartMove(wx, wz, dur, false);   // false = smoothstep（緩急つき）
}

void BattleScene::FreeLookStep(Input& in, float deltaTime)
{
    // ズーム
    int wd = in.GetMouseWheelDelta();
    if (wd != 0) { m_cameraZoom -= wd > 0 ? ZOOM_SPEED : -ZOOM_SPEED; m_cameraZoom = max(ZOOM_MIN, min(ZOOM_MAX, m_cameraZoom)); }

    // パン（中ボタンドラッグ）
    if (in.GetMouseButtonPress(2)) {
        POINT mp = in.GetMousePos();
        if (!m_isDraggingCamera) { m_isDraggingCamera = true; m_dragStartPos = mp; }
        else {
            float dx = (float)(mp.x - m_dragStartPos.x), dy = (float)(mp.y - m_dragStartPos.y);
            m_cameraOffsetX += dx * 0.02f * m_cameraZoom;
            m_cameraOffsetZ -= dy * 0.02f * m_cameraZoom;
            float gW = (m_gridMap->GetCols() / 2.0f) * 1.1f, gH = (m_gridMap->GetRows() / 2.0f) * 1.1f;
            float zf = (m_cameraZoom > 1.0f) ? 1.0f / m_cameraZoom : 1.0f;
            m_cameraOffsetX = max((-gW + 2.0f) * zf, min((gW - 3.0f) * zf, m_cameraOffsetX));
            m_cameraOffsetZ = max((-gH + 1.0f) * zf, min((gH - 2.0f) * zf, m_cameraOffsetZ));
            m_dragStartPos = mp;
        }
    }
    else m_isDraggingCamera = false;

    // リセット（中クリック）
    if (in.GetMouseButtonTrigger(2)) { m_cameraZoom = ZOOM_MAX; m_cameraOffsetX = m_player->worldX; m_cameraOffsetZ = m_player->worldZ; }

    // 敵クリック（グリッド）→ 範囲選択
    if (in.GetMouseButtonTrigger(0)
        && in.GetMousePos().x < m_screenWidth - 250.0f
        && in.GetMousePos().y < m_screenHeight - 150.0f) {
        auto rc = m_gridMap->GetClickedCell3D(in.GetMousePos(),
            m_renderer3D->GetViewMatrix(), m_renderer3D->GetProjectionMatrix(), m_screenWidth, m_screenHeight);
        int found = -1;
        if (rc.cell)
            for (int i = 0; i < (int)m_enemies.size(); i++)
                for (auto& [dc, dr] : m_enemies[i]->GetGridShape())
                    if (m_enemies[i]->gridCol + dc == rc.col && m_enemies[i]->gridRow + dr == rc.row) found = i;
        m_selectedEnemyRange = (found >= 0 && found != m_selectedEnemyRange) ? found : -1;
    }
    // 敵パネルクリック → 範囲選択＋その敵へカメラ
    if (in.GetMouseButtonTrigger(0)) {
        POINT mp = in.GetMousePos();
        float px = m_screenWidth - 250.0f, py = 50.0f, pw = 240.0f, eh = 90.0f;
        for (int i = 0; i < (int)m_enemies.size(); i++) {
            float ey = py + i * (eh + 5.0f);
            if (mp.x >= px && mp.x <= px + pw && mp.y >= ey && mp.y <= ey + eh) {
                m_selectedEnemyRange = (m_selectedEnemyRange == i) ? -1 : i;
                m_cameraOffsetX = m_enemies[i]->worldX; m_cameraOffsetZ = m_enemies[i]->worldZ; break;
            }
        }
    }

    // カメラ適用
    {
        float sx, sz; ScreenShake::GetOffset(sx, sz);
        XMFLOAT3 tgt(m_cameraOffsetX + sx, -2.0f, m_cameraOffsetZ + sz);
        XMFLOAT3 pos(m_cameraOffsetX + sx, tgt.y + 17.0f * m_cameraZoom, m_cameraOffsetZ + sz + 6.0f * m_cameraZoom);
        m_renderer3D->SetCamera(pos, tgt, XMFLOAT3(0, 1, 0));
    }

    // ハイライト（脅威範囲＋当たりマーカー）
    int hi = m_battleUI->GetPanelHoveredEnemy();
    m_highlighter.SetSelectedEnemy(hi >= 0 ? hi : m_selectedEnemyRange);
    m_highlighter.ClearPlayerHighlight(m_gridMap);
    m_highlightTimer += deltaTime * 0.5f;
    if (m_highlightTimer > 3.14159f * 2.0f) m_highlightTimer = 0.0f;
    m_highlighter.UpdateEnemyHighlight(m_enemies, m_gridMap, m_player,
        m_playerCol, m_playerRow, m_highlightTimer, m_decoyCol, m_decoyRow);
    if (m_forceHitmark && !m_enemies.empty()) m_enemies[0]->hitsPlayer = true;   // 学習用に強制表示
}

bool BattleScene::GetHitmarkRect(float& x, float& y, float& w, float& h) const
{
    if (m_enemies.empty() || !m_battleUI) return false;
    return m_battleUI->GetHitmarkRect(m_enemies[0], m_renderer3D, x, y, w, h);
}

void BattleScene::OnCardPlayed(const CardData* d)
{
    if (!d) return;
    auto& pd = PlayerDataManager::GetData();
    auto tick = [&](const char* kind)
        {
            for (auto* r : RelicManager::OwnedByKind(kind))
            {
                if (r->count <= 0) continue;
                int& c = pd.relicCounters[r->id];
                if (++c >= r->count)                 // 進んで、しきい値に達したら
                {
                    c = 0;                           // 0に戻す
                    if (r->effect == "energy") m_player->AddEnergy(r->value);
                    else                       m_player->AddBlock(r->value);   // 既定=ブロック
                }
            }
        };
    const char* k = (d->type == CardType::Attack) ? "attack" :
        (d->type == CardType::Skill) ? "skill" :
        (d->type == CardType::Move) ? "move" : "";
    if (k[0]) tick(k);   // その種別
    tick("card");        // 全カード共通
}