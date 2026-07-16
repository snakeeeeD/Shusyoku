#include "BattleScene.h"
#include "TextureLoader.h"
#include "CardExecutor.h"
#include "TerrainDataBase.h"
#include "RangeShape.h"
#include <algorithm>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

BattleScene::BattleScene()
    : m_battleUI(nullptr)
    , m_gridMap(nullptr)
    , m_whiteTexture(nullptr)
    , m_playerCol(0)
    , m_playerRow(0)
    , m_player(nullptr)
    , m_renderer3D(nullptr)
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

    m_deck.ShuffleDrawPile();

    // 最初の手札を引く
    std::vector<std::string> moveCardIds;
    for (auto& cardId : m_deck.GetDrawPile())
    {
        const CardData* data = CardDataBase::Get(cardId);
        if (data && data->type == CardType::Move)
            moveCardIds.push_back(cardId);
    }

    if (!moveCardIds.empty())
    {
        int idx = rand() % (int)moveCardIds.size();
        std::string moveId = m_deck.DrawSpecificCard(moveCardIds[idx]);
        if (!moveId.empty()) m_hand.AddCard(moveId);
    }

    for (int i = 0; i < HAND_SIZE - 1; i++)
    {
        std::string id = m_deck.DrawCard();
        if (!id.empty()) m_hand.AddCard(id);
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
    m_player->SetHp(playerData.hp);

    // プレイヤーターン開始時にエネルギー回復
    m_turnManager.onPlayerTurnStart = [this]()
        {
            m_player->RestoreEnergy();
            m_player->ResetBlock();
            m_player->GetBuffManager().OnTurnEnd();

            // デバフダメージ
            auto dmg = m_player->GetBuffManager().GetTurnEndDamage();
            if (dmg.total() > 0)
                m_player->TakeDamage(dmg.total());

            m_turnCount++;

            for (auto enemy : m_enemies)
                enemy->DecideNextAction(m_playerCol, m_playerRow, m_turnCount);

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

            // 移動カードを1枚確定で引く
            if (!moveCardIds.empty())
            {
                int idx = rand() % (int)moveCardIds.size();
                std::string moveId = m_deck.DrawSpecificCard(moveCardIds[idx]);
                if (!moveId.empty()) m_hand.AddCard(moveId);
            }

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
        };
    m_turnManager.onEnemyTurnStart = [this]()
        {
            for (auto enemy : m_enemies)
                enemy->ResetBlock();
            m_battleUI->StartDiscardEffects();
            for (auto card : m_hand.GetCards())
                m_deck.DiscardCard(card->GetId());
            m_hand.Clear();
            m_battleUI->ClearCardAnimations();

            m_enemyPhase = EnemyTurnPhase::WaitStart;
            m_currentEnemyIdx = 0;
            m_enemyActionDelay = 0.8f;
        };

    const EncounterData* encounter = EncounterDataBase::GetRandom(1);
    if (encounter)
    {
        for (auto& ee : encounter->enemies)
            AddEnemy(ee.col, ee.row, ee.id);
    }

    for (auto enemy : m_enemies)
        enemy->DecideNextAction(m_playerCol, m_playerRow, m_turnCount);

     m_input.SetWindowHandle(hWnd);

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

void BattleScene::Update(float deltaTime)
{
    m_input.Update();

#ifdef _DEBUG
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
#endif

    m_player->UpdateDisplayHp(deltaTime);
    for (auto enemy : m_enemies)
        enemy->UpdateDisplayHp(deltaTime);

    m_player->UpdateMove(deltaTime);

    // 経路を1マスずつ歩く
    if (!m_playerWalkPath.empty() && !m_player->IsMoving())
    {
        m_playerWalkIndex++;
        if (m_playerWalkIndex < (int)m_playerWalkPath.size())
        {
            auto& n = m_playerWalkPath[m_playerWalkIndex];
            float wx = (n.first - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float wz = (n.second - m_gridMap->GetRows() / 2.0f) * 1.1f;
            m_player->StartMove(wx, wz, 0.25f);
        }
        else
        {
            m_playerWalkPath.clear();   // 到着
            m_playerWalkIndex = 0;
        }
    }
    for (auto enemy : m_enemies)
    {
        enemy->UpdateMove(deltaTime);
        enemy->UpdateLunge(deltaTime);
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
        XMFLOAT3 target(
            m_cameraOffsetX,
            -2.0f,
            m_cameraOffsetZ
        );
        XMFLOAT3 zoomedPos(
            m_cameraOffsetX,
            target.y + 17.0f * m_cameraZoom,
            m_cameraOffsetZ + 6.0f * m_cameraZoom
        );
        m_renderer3D->SetCamera(zoomedPos, target, XMFLOAT3(0.0f, 1.0f, 0.0f));

        int hi = m_battleUI->GetPanelHoveredEnemy();
        m_highlighter.SetSelectedEnemy(hi >= 0 ? hi : m_selectedEnemyRange);

        // ハイライト更新
        int highlightCardIndex = m_selectedCardIndex >= 0 ? m_selectedCardIndex : m_hoveredCardIndex;
        if (highlightCardIndex >= 0 && highlightCardIndex < (int)m_hand.GetCards().size())
        {
            const CardData* data = m_hand.GetCards()[highlightCardIndex]->GetData();

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
                cardArea);
        }
        else
        {
            m_highlighter.ClearPlayerHighlight(m_gridMap);
        }

        // ハイライト明滅タイマーを更新
        m_highlightTimer += deltaTime * 0.1f; // 点滅速度調整
        if (m_highlightTimer > 3.14159f * 2.0f)
            m_highlightTimer = 0.0f;



        if (m_battleResult != BattleResult::None) return;   // 勝敗決定後は何もしない
        m_highlighter.UpdateEnemyHighlight(
            m_enemies, m_gridMap, m_player,
            m_playerCol, m_playerRow, m_highlightTimer);

        m_battleUI->UpdateDrawCardEffects(deltaTime);
        m_battleUI->UpdatePlayCardEffects(deltaTime);
        bool selectedNeedsTarget = false;
        if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size())
        {
            CardType ct = m_hand.GetCards()[m_selectedCardIndex]->GetData()->type;
            selectedNeedsTarget = (ct == CardType::Attack || ct == CardType::Move);
        }
        m_battleUI->UpdateCardAnimations(deltaTime, (int)m_hand.GetCards().size(), m_hoveredCardIndex, 
            m_selectedCardIndex, m_input.GetMousePos(), selectedNeedsTarget);
        m_battleUI->UpdateDiscardEffects(deltaTime);

        // 勝利判定
        if (m_enemies.empty())
        {
            m_battleResult = BattleResult::Win;
            // HPを保存、現在のマスをクリア済みに
            auto& pd = PlayerDataManager::GetData();
            pd.hp = m_player->GetHp();
            int nodeIdx = pd.fieldPlayerCol * 7 + pd.fieldPlayerRow;
            if (nodeIdx >= 0 && nodeIdx < (int)pd.fieldNodeVisited.size())
                pd.fieldNodeVisited[nodeIdx] = true;
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
                m_enemyActionDelay -= deltaTime;
                if (m_enemyActionDelay <= 0)
                {
                    if (m_enemies.empty())
                        m_enemyPhase = EnemyTurnPhase::EndTurn;
                    else
                        m_enemyPhase = EnemyTurnPhase::ProcessEnemy;
                }
                break;
            case EnemyTurnPhase::ProcessEnemy:
            {
                while (m_currentEnemyIdx < (int)m_enemies.size()
                    && m_enemies[m_currentEnemyIdx]->GetHp() <= 0)
                    m_currentEnemyIdx++;

                if (m_currentEnemyIdx >= (int)m_enemies.size())
                {
                    m_enemyPhase = EnemyTurnPhase::EndTurn;
                    break;
                }

                Enemy* enemy = m_enemies[m_currentEnemyIdx];

                // 毒ダメージはこの敵の最初の行動時のみ
                if (enemy->GetActionIndex() == 0)
                {
                    auto dmg = enemy->GetBuffManager().GetTurnEndDamage();
                    if (dmg.total() > 0)
                    {
                        enemy->TakeDamage(dmg.total());
                        if (enemy->GetHp() <= 0)
                        {
                            m_enemyPhase = EnemyTurnPhase::WaitAction;
                            m_enemyActionDelay = ENEMY_ACTION_PAUSE;
                            break;
                        }
                    }
                }

                // 現在の行動を実行
                int ai = enemy->GetActionIndex();
                int damage = enemy->ExecuteAction(ai, m_playerCol, m_playerRow, m_gridMap, m_player);
                if (damage > 0)
                    m_player->TakeDamage(damage);
                enemy->SetActionIndex(ai + 1);

                auto& cell = m_gridMap->GetCell(enemy->gridCol, enemy->gridRow);
                if (cell.tileEffect.active)
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
                    if (enemy->IsMoving() || enemy->IsLunging()) anyMoving = true;

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
                    enemy->GetBuffManager().OnTurnEnd();
                m_turnManager.EndTurn();
                break;
            }
        }

        int highlightCardIndex2 = m_selectedCardIndex >= 0 ? m_selectedCardIndex : m_hoveredCardIndex;
        if (highlightCardIndex2 >= 0 && highlightCardIndex2 < (int)m_hand.GetCards().size())
        {
            const CardData* data =
                m_hand.GetCards()[highlightCardIndex2]->GetData();

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
                cardArea);
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
            const CardData* d = m_hand.GetCards()[m_selectedCardIndex]->GetData();
            if (d->type == CardType::Move)
            {
                for (auto& p : m_movePath) raised.insert(p);
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
            m_renderer3D->DrawTile(m_whiteTexture, x, z, size, cell.gameObject.color, cell.gameObject.worldY);
        }
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

                const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
                XMFLOAT4 terrainColor = def ? def->color : XMFLOAT4(1, 1, 1, 0.5f);

                if (def && !def->texture.empty())
                    m_renderer3D->DrawTile(TextureManager::Get(def->texture), x, z, 0.8f + 0.064f * t, XMFLOAT4(1, 1, 1, 1), cell.gameObject.worldY + 0.01f);
                else
                    m_renderer3D->DrawTile(m_whiteTexture, x, z, 0.8f + 0.064f * t, terrainColor, cell.gameObject.worldY + 0.01f);
            }
        }
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

    m_player->Draw3D(m_renderer3D);
    for (auto enemy : m_enemies)
        enemy->Draw3D(m_renderer3D);

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
    ctx.showDrawPile = m_showDrawPile;
    ctx.cardSelecting = m_cardSelecting;
    ctx.showDiscardPile = m_showDiscardPile;
    ctx.showExhaustPile = m_showExhaustPile;
    ctx.screenWidth = m_screenWidth;
    ctx.screenHeight = m_screenHeight;
    ctx.cameraZoom = m_cameraZoom;
    ctx.isPlayerTurn = m_turnManager.IsPlayerTurn();
    ctx.mousePos = m_input.GetMousePos();
    ctx.outOfRangeCells = &m_highlighter.GetOutOfRangeCells();
    ctx.travelPath = &m_movePath;
    ctx.selectedEnemy = m_selectedEnemyRange;

    m_battleUI->Draw(ctx);
}

void BattleScene::HandleInput()
{
    if (!m_turnManager.IsPlayerTurn()) return; // プレイヤーターン以外は無視

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
            moveRangeSel = m_player->GetBuffManager().GetFinalMoveRange(sd->range);
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
        if (releasePos.y < m_screenHeight - 150)
        {
            const Card* card = cards[m_selectedCardIndex];
            CardData dataCopy = *card->GetData();
            CardType ct = dataCopy.type;

            const std::vector<std::pair<int, int>>* usePath = nullptr;
            bool moveCanceled = false;
            if (ct == CardType::Move)
            {
                if (m_moveReleaseSuppress)
                    moveCanceled = true;                        // 右クリックでキャンセル済み
                else if (m_pathBuilding && !m_movePath.empty())
                    usePath = &m_movePath;                       // 手動経路で移動
                else if (m_pathBuilding && m_movePath.empty())
                    moveCanceled = true;                         // プレイヤー上で離した＝キャンセル
            }

            int targetCol = m_playerCol;
            int targetRow = m_playerRow;
            bool canTry = !moveCanceled;

            if ((ct == CardType::Attack || ct == CardType::Move) && !usePath && !moveCanceled)
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

                float playCardX = m_screenWidth / 2.0f
                    - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
                    + m_selectedCardIndex * (CARD_WIDTH + 10.0f);
                float playCardY = m_screenHeight - 30.0f;

                const CardData* dataPtr = card->GetData();

                auto execResult = m_cardExecutor.Execute(
                    dataCopy, cardId,
                    targetCol, targetRow,
                    m_player, m_enemies, m_gridMap,
                    m_playerCol, m_playerRow,
                    m_hand, m_selectedCardIndex, m_deck,
                    newPlayerCol, newPlayerRow,
                    usePath
                );

                if (execResult.cardUsed)
                {
                    if (newPlayerCol != m_playerCol || newPlayerRow != m_playerRow)
                    {
                        int oldCol = m_playerCol;
                        int oldRow = m_playerRow;
                        m_playerCol = newPlayerCol;
                        m_playerRow = newPlayerRow;
                        m_player->gridCol = m_playerCol;
                        m_player->gridRow = m_playerRow;
                        float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                        float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                        if (usePath && !usePath->empty())
                        {
                            m_playerWalkPath = *usePath;      // 経路を保存
                            m_playerWalkIndex = 0;
                            // 最初の1マスへ
                            auto& f = m_playerWalkPath[0];
                            float wx = (f.first - m_gridMap->GetCols() / 2.0f) * 1.1f;
                            float wz = (f.second - m_gridMap->GetRows() / 2.0f) * 1.1f;
                            m_player->StartMove(wx, wz, 0.12f, true);   // 1マスぶんの時間
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
                                if (usePath && !usePath->empty())
                                {
                                    m_playerWalkPath = *usePath;      // 経路を保存
                                    m_playerWalkIndex = 0;
                                    // 最初の1マスへ
                                    auto& f = m_playerWalkPath[0];
                                    float wx = (f.first - m_gridMap->GetCols() / 2.0f) * 1.1f;
                                    float wz = (f.second - m_gridMap->GetRows() / 2.0f) * 1.1f;
                                    m_player->StartMove(wx, wz, 0.12f, true);   // 1マスぶんの時間
                                }
                                else
                                {
                                    m_player->StartMove(px, pz);
                                }

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
                    m_selectedCardIndex = -1;
                    cardJustUsed = true;



                    // ドロー効果のアニメーション
                    for (auto& drawnId : execResult.drawnCards)
                        m_battleUI->StartDrawCardEffect(drawnId);
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
        isOnCard = false;
        for (int i = 0; i < (int)cards.size(); i++)
        {
            if (i == m_selectedCardIndex) continue;
            float cardX = m_screenWidth / 2.0f
                - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
                + i * (CARD_WIDTH + 10.0f);

            float hitX, hitY, hitW, hitH;
            if (i == m_selectedCardIndex)
            {
                hitX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
                hitY = cardHoverY + 40.0f;
                hitW = CARD_HOVER_W;
                hitH = CARD_HOVER_H;
            }
            else if (i == m_prevHoveredCardIndex)
            {
                if (m_selectedCardIndex >= 0)
                {
                    hitX = cardX;
                    hitY = cardHideY - 40.0f;
                    hitW = CARD_WIDTH;
                    hitH = CARD_HEIGHT;
                }
                else
                {
                    hitX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
                    hitY = cardHoverY;
                    hitW = CARD_HOVER_W;
                    hitH = (float)m_screenHeight - cardHoverY;
                }
            }
            else
            {
                hitX = cardX;
                hitY = cardHideY;
                hitW = CARD_WIDTH;
                hitH = CARD_HEIGHT;
            }

            if (mousePos.x >= hitX && mousePos.x <= hitX + hitW
                && mousePos.y >= hitY && mousePos.y <= hitY + hitH)
            {
                isOnCard = true;
                break;
            }
        }

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
            m_showDiscardPile = false;
        }

        // 捨て札ボタン
        else if (mousePos.x >= discardX && mousePos.x <= discardX + 50.0f
            && mousePos.y >= discardY && mousePos.y <= discardY + 40.0f)
        {
            m_showDiscardPile = !m_showDiscardPile;
            m_showDrawPile = false;
        }

        // 廃棄札ボタン
        else if (m_deck.GetExhaustPileCount() > 0
            && mousePos.x >= 140.0f && mousePos.x <= 190.0f
            && mousePos.y >= discardY && mousePos.y <= discardY + 40.0f)
        {
            m_showExhaustPile = !m_showExhaustPile;
            m_showDrawPile = false;
            m_showDiscardPile = false;
        }

        // ビューワー外クリックで閉じる
        else if (m_cardSelecting && (m_showDrawPile || m_showDiscardPile))
        {
            float bgX = m_screenWidth / 2.0f - 300.0f;
            float bgY = 50.0f;
            const float cardW = 160.0f;
            const float cardH = 50.0f;
            const float padding = 10.0f;
            const int cols = 3;

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
                float cx = bgX + 20.0f + col * (cardW + padding);
                float cy = bgY + 50.0f + row * (cardH + padding);

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

                int newPlayerCol = m_playerCol;
                int newPlayerRow = m_playerRow;

                float playCardX = m_screenWidth / 2.0f
                    - (m_hand.GetCards().size() * (CARD_WIDTH + 10.0f)) / 2.0f
                    + m_selectedCardIndex * (CARD_WIDTH + 10.0f);
                float playCardY = m_screenHeight - 30.0f;

                const CardData* dataPtr = card->GetData();

                auto execResult = m_cardExecutor.Execute(
                    dataCopy, cardId,
                    result.col, result.row,
                    m_player, m_enemies, m_gridMap,
                    m_playerCol, m_playerRow,
                    m_hand, m_selectedCardIndex, m_deck,
                    newPlayerCol, newPlayerRow
                );

                if (execResult.cardUsed)
                {
                    // プレイヤー座標を更新
                        if (newPlayerCol != m_playerCol || newPlayerRow != m_playerRow)
                        {
                            int oldCol = m_playerCol;
                            int oldRow = m_playerRow;
                            m_playerCol = newPlayerCol;
                            m_playerRow = newPlayerRow;
                            m_player->gridCol = m_playerCol;
                            m_player->gridRow = m_playerRow;
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

                    if (execResult.pendingSelection != CardEffectType::None)
                    {
                        m_cardSelecting = true;
                        m_selectingType = execResult.pendingSelection;
                        if (m_selectingType == CardEffectType::Search)
                            m_showDrawPile = true;
                        else if (m_selectingType == CardEffectType::Salvage)
                            m_showDiscardPile = true;
                    }

                    m_selectedCardIndex = -1;
                    cardJustUsed = true;
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
    
    if (!cardJustUsed)
    {
        for (int i = 0; i < (int)cards.size(); i++)
        {
            float cardX = m_screenWidth / 2.0f
                - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
                + i * (CARD_WIDTH + 10.0f);

            float hitX, hitY, hitW, hitH;

            if (i == m_selectedCardIndex)
            {
                hitX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
                hitY = cardHoverY + 40.0f;
                hitW = CARD_HOVER_W;
                hitH = CARD_HOVER_H;
            }
            else if (i == m_prevHoveredCardIndex)
            {
                if (m_selectedCardIndex >= 0)
                {
                    hitX = cardX;
                    hitY = cardHideY - 40.0f;
                    hitW = CARD_WIDTH;
                    hitH = CARD_HEIGHT;
                }
                else
                {
                    hitX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
                    hitY = cardHoverY;
                    hitW = CARD_HOVER_W;
                    hitH = (float)m_screenHeight - cardHoverY;
                }
            }
            else
            {
                hitX = cardX;
                hitY = cardHideY;
                hitW = CARD_WIDTH;
                hitH = CARD_HEIGHT;
            }

            bool hover = mousePos.x >= hitX && mousePos.x <= hitX + hitW
                && mousePos.y >= hitY && mousePos.y <= hitY + hitH;

            if (hover && m_input.GetMouseButtonTrigger(0))
            {
                if (m_selectedCardIndex == i)
                    m_selectedCardIndex = -1;
                else
                    m_selectedCardIndex = i;
                break;
            }
        }
    }
    

    m_prevHoveredCardIndex = m_hoveredCardIndex;
    m_hoveredCardIndex = -1;

        for (int i = 0; i < (int)cards.size(); i++)
        {
            float cardX = m_screenWidth / 2.0f
                - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
                + i * (CARD_WIDTH + 10.0f);

            // 通常位置の判定
            bool hoverNormal = mousePos.x >= cardX && mousePos.x <= cardX + CARD_WIDTH
                && mousePos.y >= cardHideY && mousePos.y <= cardHideY + CARD_HEIGHT;

            // 大きいカードの位置の判定
            float hoverDrawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
            bool hoverBig = mousePos.x >= hoverDrawX && mousePos.x <= hoverDrawX + CARD_HOVER_W
                && mousePos.y >= cardHoverY && mousePos.y <= cardHoverY + CARD_HOVER_H;

            // 選択中の判定
            float selectedDrawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
            bool hoverSelected = (i == m_selectedCardIndex)
                && mousePos.x >= selectedDrawX && mousePos.x <= selectedDrawX + CARD_HOVER_W
                && mousePos.y >= cardHoverY + 40.0f && mousePos.y <= cardHoverY + 40.0f + CARD_HOVER_H;

            if (hoverNormal || hoverBig || hoverSelected)
            {
                m_hoveredCardIndex = i;
                break;
            }
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
                    m_turnManager.EndTurn();
                }
            }
        }

        // 敵パネルクリック → カメラ移動
        if (m_input.GetMouseButtonTrigger(0))
        {
            POINT mp = m_input.GetMousePos();
            float panelX = m_screenWidth - 250.0f;
            float panelY = 10.0f;
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

        // 勝敗後のクリック
        if (m_battleResult == BattleResult::Win)
        {
            if (m_input.GetMouseButtonTrigger(0))
            {
                if (onChangeScene)
                    onChangeScene(SceneType::CardSelect);
            }
            return;
        }
        if (m_battleResult == BattleResult::Lose)
        {
            if (m_input.GetMouseButtonTrigger(0))
            {
                if (onChangeScene)
                    onChangeScene(SceneType::Title);
            }
            return;
        }
}

void BattleScene::ProcessDeadEnemies()
{
    std::vector<Enemy*> toDelete;
    for (auto enemy : m_enemies)
        if (enemy->GetHp() <= 0)
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