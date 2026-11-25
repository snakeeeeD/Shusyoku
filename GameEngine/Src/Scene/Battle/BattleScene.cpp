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
#include "BuffPopup.h"
#include "PileFx.h"
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
#include <random>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// BattleScene は機能ごとにファイルを分けている（どれも同じ BattleScene クラスのメンバー）
//   BattleScene.cpp            初期化と毎フレームの更新
//   BattleSceneInput.cpp       入力
//   BattleSceneDraw.cpp        描画
//   BattleSceneDebug.cpp       デバッグ用の窓
//   BattleSceneEnemyFx.cpp     敵の攻撃の演出
//   BattleSceneEnemies.cpp     敵の出入り
//   BattleSceneResult.cpp      勝ち負け
//   BattleSceneHand.cpp        手札
//   BattleSceneBurn.cpp        廃棄の軸
//   BattleSceneRelic.cpp       レリックの呼び出し口
//   BattleSceneSequence.cpp    時間を置いて1つずつ進める処理
//   BattleSceneTrap.cpp        罠
//   BattleSceneBoss.cpp        ボスの仕掛け
//   BattleSceneCameraLink.cpp  シーン側のカメラ
//   BattleSceneFx.cpp          盤面の空気とバフの演出
//   BattleSceneQuery.cpp       調べるだけの関数
//   BattleSceneTurn.cpp        ターンの流れ

// 敵が (col,row) に立った時、体の下の罠を全部踏む（大きい敵は形の全マス）
static void TriggerTrapsUnder(Enemy* e, int col, int row, GridMap* g, std::vector<Enemy*>& enemies)
{
    for (auto& [dc, dr] : e->GetGridShape())
    {
        int c = col + dc, r = row + dr;
        if (c < 0 || r < 0 || c >= g->GetCols() || r >= g->GetRows()) continue;
        auto& cell = g->GetCell(c, r);
        if (cell.tileEffect.active) CardExecutor::TriggerTrap(cell, e, c, r, g, enemies);
        if (e->GetHp() <= 0) return;
    }
}

// ナイフ＋投擲術なら遠距離化（Diamond=範囲内の任意の敵を狙える。単体パスのままなので研磨も効く）
void BattleScene::ApplyKnifeThrow(CardData& d, const Player* p)
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
    EnemyIntentVisual::SetPlayerBuffs(nullptr);
    CardExecutor::SetBattlePlayer(nullptr);
    EnemyIntentVisual::SetHidden(false);   // 目隠しを戦闘の外に持ち越さない
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

    m_showDrawPile = false;
    m_showDiscardPile = false;
    m_showExhaustPile = false;

    m_hoveredCell = { -1, -1 };

#ifdef _DEBUG
    m_debugRank = 1;
    m_debugEncounterIndex = -1;
#endif

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

    m_holdForTutorial = !playerData.tutorialBattle;   // この戦闘で初戦チュートリアルが出る

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

    m_renderer3D->InitPostProcess();

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
    CardExecutor::SetBattlePlayer(m_player);   // 罠のダメージに罠術バフを足すため
    m_player->gridCol = m_playerCol;
    m_player->gridRow = m_playerRow;
    m_player->PlaceAt((m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f,
        (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f);   // 正面を向いて立つ

    m_camera.Home(m_player->worldX, m_player->worldZ);

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
        Buff b; b.type = BuffType::AttackUp; b.value = atk; b.duration = -1;
        b.name = L""; b.description = L"";
        m_player->GetBuffManager().AddBuff(b);
    }
    if (int atk = RelicManager::SumValue("startBuffAtkTurn"))   // 最初のターンだけ攻撃UP（鍛錬の鉢巻）
    {
        Buff b; b.type = BuffType::AttackUpTurn; b.value = atk; b.duration = 1;
        b.name = L""; b.description = L"";
        m_player->GetBuffManager().AddBuff(b);
    }
    for (int i = 0; i < RelicManager::SumValue("startDraw"); i++)
    {
        std::string id = m_deck.DrawCard();
        if (!id.empty()) m_hand.AddCard(id);
    }
    for (auto* r : RelicManager::OwnedByKind("startCard"))   // 種火の灯：戦闘開始時、決まったカードを手札に
        for (int i = 0; i < r->value; i++) m_hand.AddCard(r->param);

    // 戦闘開始時：1ターン用(perTurn)カウントレリックを0に
    // ※リセットはStartPlayerTurn（ターン2以降）でしか走らないため、
    //   前の戦闘から持ち越した値が1ターン目に残るのを防ぐ
    for (auto& id : PlayerDataManager::GetData().relics)
        if (auto def = RelicManager::Get(id); def && def->perTurn)
            PlayerDataManager::GetData().relicCounters[id] = 0;

    // プレイヤーターン開始時にエネルギー回復
    m_turnManager.onPlayerTurnStart = [this]()
        {
            m_player->ResetMoved();
            m_burnedThisTurn = 0;              // このターンに燃やした枚数（煤けた火打石）
            m_burnStatusEnergyUsed = false;    // 煤けた手袋（1ターンに1回）
            for (auto* c : m_hand.GetCards()) c->SetCostThisTurn(-1);   // 「このターンだけのコスト」を戻す
            for (auto& id : PlayerDataManager::GetData().relics)
                if (auto def = RelicManager::Get(id); def && def->perTurn)
                    PlayerDataManager::GetData().relicCounters[id] = 0;

            TurnBanner::Show(TurnBannerType::Player);
            UpdateTrapsAtTurnStart();   // 罠が育つ・導火線
            m_player->RestoreEnergy();
            m_player->AddEnergy(RelicManager::SumValue("turnEnergy"));   // レリック
            {
                // 持ち越し：一番多く残る手段だけを使う（重ねて増えないように）
                int block = m_player->GetBlock();
                int keep = block * min(100, RelicManager::SumValue("blockRetain")) / 100;   // 割合（鋼の意志）
                if (RelicManager::HasKind("blockCarry"))                                    // 一定量だけ減る（不落の城壁）
                    keep = max(keep, block - RelicManager::SumValue("blockCarry"));
                if (m_player->GetBuffManager().HasBuff(BuffType::HalfCarry)) keep = max(keep, block / 2);   // 残心は半分
                if (m_player->GetBuffManager().HasBuff(BuffType::Barricade)) keep = block;  // 土手・要塞は全部
                m_player->SetBlock(keep);
            }
            m_player->GetBuffManager().TickDurations(false);   // プレイヤーのバフ：ターンの始めに減る（敵のターンの間も残る）

            // 攻撃力成長：闘気（戦士の誓い・成長石）と巨人の篭手（turnBuffAtk）は、どちらも2ターンに1回（偶数ターンの終わり）
            {
                int grow = 0;
                if ((m_turnCount % 2) == 0)
                    grow = m_player->GetBuffManager().GetBuffValue(BuffType::AttackGrowth) + RelicManager::SumValue("turnBuffAtk");
                if (grow > 0)
                {
                    Buff gb; gb.type = BuffType::AttackUp; gb.value = grow; gb.duration = -1;
                    gb.name = L""; gb.description = L"";
                    m_player->GetBuffManager().AddBuff(gb);
                }
            }

            // 毒の瘴気：2ターンに1回、全敵に毒を付与
            int nox = ((m_turnCount % 2) == 0)
                ? m_player->GetBuffManager().GetBuffValue(BuffType::NoxiousFumes) : 0;
            if (nox > 0) EffectManager::Play("miasma_screen", 0.0f, 0.0f, 0.0f);   // カードを使ったときと同じ画面の瘴気（画面の演出なので、場所は使わない）
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

            // 大聖堂：ターン開始時、聖地に立っていれば周り8マスに信仰を刻む（自分の足元は増えない）
            if (int cat = m_player->GetBuffManager().GetBuffValue(BuffType::Cathedral);
                cat > 0 && Faith::At(m_gridMap, m_playerCol, m_playerRow) > 0)
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++)
                        if (dc != 0 || dr != 0)
                            Faith::Engrave(m_gridMap, m_playerCol + dc, m_playerRow + dr, cat);

            // 毒：今の値だけダメージを受けてから、1減る（敵と同じ TickPoison。HPバーの予告の数字と同じになる）
            int poisonDmg = m_player->GetBuffManager().TickPoison();
            if (poisonDmg > 0)
                m_player->TakeDamage(poisonDmg, DamageFeel::Poison);

            m_turnCount++;

            // 3層ボス：4ターン毎に楔が位置交換
            if ((m_turnCount % 4) == 0 && !m_bossAwakened)
                SwapWedges();

            for (auto enemy : m_enemies)
            {
                int tC = (m_decoyCol >= 0) ? m_decoyCol : m_playerCol;
                int tR = (m_decoyCol >= 0) ? m_decoyRow : m_playerRow;
                int allies = 0; for (auto o : m_enemies) if (o != enemy && o->GetHp() > 0) allies++;
                enemy->SetAllyCount(allies);
                enemy->DecideNextAction(tC, tR, m_turnCount);
            }

            // 覚醒ターンのボスは予告を「覚醒」に差し替える
            if (m_bossAwakenAtTurn >= 0 && m_turnCount >= m_bossAwakenAtTurn && !m_bossAwakened)
                for (auto enemy : m_enemies)
                    if (enemy->GetId() == "l3_sovereign") enemy->SetAwakenIntent();

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

            // 残りを時間差で引く（引く→山札が尽きたらリシャッフル演出→残りを引く）
            int extraDraw = m_player->GetBuffManager().GetBuffValue(BuffType::DrawPerTurn);
            int keep = m_hand.GetCardCount() - m_enemyAddedToHand;   // 保留で残っている枚数（敵に入れられたお邪魔は数えない）
            if (keep < 0) keep = 0;
            m_enemyAddedToHand = 0;
            int drawN = HAND_SIZE - 1 + extraDraw - keep;
            if (drawN < 0) drawN = 0;
            StartDrawSequence(drawN);
    
        };
    m_turnManager.onEnemyTurnStart = [this]()
        {
            // 残りターン：プレイヤーのデバフはプレイヤーのターンの終わり、敵のバフは敵のターンの始めに減る
            m_player->GetBuffManager().TickDurations(true);
            for (auto enemy : m_enemies)
                if (enemy->GetHp() > 0) enemy->GetBuffManager().TickDurations(false);

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

            // 守護の誓い：ターン終了時、聖地に立っていればブロック（敵ターンの攻撃に備える）
            if (int gv = m_player->GetBuffManager().GetBuffValue(BuffType::GuardianVow);
                gv > 0 && Faith::At(m_gridMap, m_playerCol, m_playerRow) > 0)
            {
                m_player->AddBlock(gv);
                EffectManager::Play("block_gain", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);
            }

            // 不退転：ターン終了時、隣（上下左右）の敵全員に、残っているブロックの value% のダメージ。
            // 隣にしか効かない＝前に出て受け止める軸の核（遠くでブロックを積んで待つだけでは削れない）
            if (int sf = m_player->GetBuffManager().GetBuffValue(BuffType::StandFirm); sf > 0)
                if (int dmg = m_player->GetBlock() * sf / 100; dmg > 0)
                    for (auto enemy : m_enemies)
                    {
                        if (!enemy || enemy->GetHp() <= 0 || enemy->IsDying()) continue;
                        bool adj = false;   // 大きい敵・大蛇の体は、占めているマスのどれかが隣なら
                        for (auto& c : enemy->OccupiedCells())
                            if (abs(c.first - m_playerCol) + abs(c.second - m_playerRow) == 1) { adj = true; break; }
                        if (!adj) continue;
                        enemy->TakeDamage(dmg);
                        EffectManager::Play("hit", enemy->worldX, enemy->worldY + 0.6f, enemy->worldZ);
                    }

            // 聖地：プレイヤーのターン終了時、聖地に立つ敵は信仰と同じダメージ。聖域でなければそのマスの信仰は半分に
            for (auto enemy : m_enemies)
            {
                if (!enemy || enemy->GetHp() <= 0) continue;
                int best = 0, bc = -1, br = -1;   // 大きな敵は、占めているマスのうち信仰が一番高いマスで判定
                for (auto& c : enemy->OccupiedCells())
                    if (int f = Faith::At(m_gridMap, c.first, c.second); f > best) { best = f; bc = c.first; br = c.second; }
                if (best <= 0) continue;
                enemy->TakeDamage(best, DamageFeel::Holy);   // 表示どおりの固定ダメージ
                EffectManager::Play(Faith::IsSanctuary(best) ? "faith_smite_big" : "faith_smite",      // 聖域は大きく・画面を揺らす
                    enemy->worldX, enemy->worldY + 0.5f, enemy->worldZ);
                m_gridMap->GetCell(bc, br).faith = best / 2;   // 焼いたら、聖域も含めて信仰は半分に（踏ませると居場所が削れる）
            }

            TurnBanner::Show(TurnBannerType::Enemy);

            for (auto enemy : m_enemies)
                enemy->ResetBlock();
            float seq = 0.0f;
            float burnEnd = 0.0f;
            m_burnClock = 0.0f;
            const float BURN_GAP = 0.5f;   // 連続時は詰めて一連を速く
            for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
            {
                const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
                if (dd && dd->endTurnDamage > 0)
                {
                    if (dd->retain)
                        m_battleUI->StartHandCardPeek(i, seq);                     // 保留：中央→手札へ戻る
                    else
                        m_battleUI->StartPlayCardEffectFromHand(dd, i, seq, true); // 非保留：中央→退場
                    m_burnHits.push_back({ seq + 0.36f * 0.75f, dd->endTurnDamage,
                       dd->type == CardType::Curse });
                    burnEnd = seq + 0.75f;
                    seq += BURN_GAP;
                }
            }

            // 残り（過負荷以外）は通常の捨てアニメ（保留はアニメ無し）
            for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
            {
                const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
                if (dd && dd->endTurnDamage > 0) continue;
                if (dd && dd->retain) continue;
                bool ex = (dd && dd->ethereal);
                m_battleUI->StartDiscardEffectAt(i, dd, seq, ex);   // エセリアル=その場で溶ける
            }
           
            // 呪い・増殖など：手札にあると捨て札にカードを加える（敵がお邪魔カードを足す時と同じ演出で見せる）
            for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
            {
                const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
                if (!dd || dd->endTurnAddCard.empty()) continue;
                m_deck.DiscardCard(dd->endTurnAddCard);
                m_battleUI->StartAddedCardEffect(CardDataBase::Get(dd->endTurnAddCard), "discard", 0, 1);
            }

            // 非保留カードだけ手札から取り除く（保留はスロット/アニメ状態を維持）
            std::vector<const CardData*> etherealBurned;   // ターン終わりに燃えたエセリアル（全部片付けてから OnCardBurned）
            for (int i = (int)m_hand.GetCards().size() - 1; i >= 0; i--)
            {
                const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
                if (dd && dd->retain) continue;                               // 保留：残す
                if (dd && dd->ethereal) { m_deck.ExhaustCard(m_hand.GetCards()[i]->GetId()); etherealBurned.push_back(dd); }
                else                    m_deck.DiscardCard(m_hand.GetCards()[i]->GetId());
                m_hand.RemoveCard(i);
                m_battleUI->OnCardRemoved(i);                                 // アニメ側も同indexを除去
            }
            m_selectedCardIndex = -1;   // 手札が消えたので選択も無効化
            for (auto* dd : etherealBurned) OnCardBurned(dd);
            m_hoveredCardIndex = -1;    // ホバーも無効化
            m_cardSelecting = false;
           // m_battleUI->ClearCardAnimations();

            m_enemyPhase = EnemyTurnPhase::WaitStart;
            m_currentEnemyIdx = 0;
            m_poisonIdx = 0;         
            m_poisonSubTicks = 0;
            m_enemyActionDelay = max(0.8f, burnEnd + 0.25f);   // 過負荷が全て終わるまで敵は動かない
        };

    Se::SetFloor(PlayerDataManager::GetData().layer);   // 足音をこの層の地面に
    int encCount = EncounterDataBase::GetCount();
    int layer = PlayerDataManager::GetData().layer;
    const EncounterData* encounter = EncounterDataBase::GetById(m_battleEnemyId);   // 指名戦（あれば）
    if (!encounter)
        encounter = EncounterDataBase::GetEncounter(layer, m_category, m_battleTier, m_battleSeed);
    if (encounter)
    {
        for (auto& ee : encounter->enemies)
            AddEnemy(ee.col, ee.row, ee.id);

        // 3層ボス：楔の四隅スロットを記録
        for (auto e : m_enemies)
            if (e->GetId().rfind("wedge_", 0) == 0)
                m_wedgeCorners.push_back({ e->gridCol, e->gridRow });

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
    if (int sp = RelicManager::SumValue("startPoison"); sp > 0)
        m_startPoison.Start({ sp }, 0.6f);   // 少し遅らせて全敵へ毒

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
     BuffPopupManager::Clear();
     PileFx::Clear();
     UiNotice::Clear();
     EffectManager::Clear();
     ScreenShake::Clear();   // 前の戦闘の揺れを持ち越さない

     RunTurnCycle();

     TurnBanner::ShowThen(TurnBannerType::BattleStart, TurnBannerType::Player);
     // 背景装飾（盤面の外側・後方と左右だけに少数。手前には置かずグリッドに被らせない）
     m_decos.clear();
     unsigned s = 20240820u;
     auto rnd = [&s]() { s = s * 1103515245u + 12345u; return (int)((s >> 16) & 0x7fff); };
     float gw = m_gridMap->GetCols() * 1.1f * 0.5f;   // 盤面半幅 ~6.6
     float gh = m_gridMap->GetRows() * 1.1f * 0.5f;   // 盤面半奥 ~3.85
     int placed = 0;
     for (int i = 0; i < 200 && placed < 14; i++)     // 最大14個まで（新しい絵は細かく目立つので控えめに）
     {
         float ang = ((rnd() % 1000) / 1000.0f) * 6.2832f;
         float m = 2.0f + ((rnd() % 1000) / 1000.0f) * 4.0f;   // フチから 2..6 外側
         float x = cosf(ang) * (gw + m);
         float z = sinf(ang) * (gh + m);
         if (z > gh - 0.5f) continue;                 // ★手前(カメラ側)には置かない＝グリッドに被らない
         Deco d; d.x = x; d.z = z;
         int r = rnd() % 100;
         d.type = (r < 45) ? 0 : (r < 75) ? 1 : 2;
         d.scale = 0.8f + (rnd() % 100) / 100.0f * 0.6f;
         m_decos.push_back(d); placed++;
     }
     // 描く順：奥（z が小さい＝画面の上）から手前へ。足元が画面の下にある物ほど後に描いて、手前に見せる
     std::sort(m_decos.begin(), m_decos.end(), [](const Deco& a, const Deco& b) { return a.z < b.z; });

     // テレメトリ: 戦闘開始（敵構成・開始HP）
     m_hpStart = m_player ? m_player->GetHp() : 0;
     {
         std::string foes;
         for (auto* e : m_enemies) { if (!foes.empty()) foes += ","; foes += e->GetId(); }
         Telemetry::Instance().Log("battle_start", {
             {"enc",   m_battleEnemyId},
             {"cat",   (int)m_category},
             {"tier",  m_battleTier},
             {"count", (int)m_enemies.size()},
             {"foes",  foes},
             {"hp",    m_hpStart},
             });
     }

     return true;

    return true;
}

void BattleScene::Update(float deltaTime)
{
    m_input.Update();
    if (m_player) EnemyIntentVisual::SetPlayerBuffs(&m_player->GetBuffManager());   // 予告表示に被ダメ補正を反映
    {
        // 呪い・目隠しが手札にある間は、敵の予告を隠す
        auto isBlind = [](const std::string& id) { const CardData* d = CardDataBase::Get(id); return d && d->hideIntent; };
        bool blind = false;
        for (auto* c : m_hand.GetCards())
            if (isBlind(c->GetId())) { blind = true; break; }
        // 配っている途中（ターン開始〜配り終わり）は、目隠しを引く可能性があれば伏せておく
        // （敵の次の行動はターン開始時に決まるので、引く前の一瞬に新しい予告が見えてしまうのを防ぐ）
        if (!blind && (m_drawSeqRemaining > 0 || m_reshuffleFxTimer > 0.0f))
        {
            for (auto& id : m_deck.GetDrawPile())    if (isBlind(id)) { blind = true; break; }
            if (!blind)
                for (auto& id : m_deck.GetDiscardPile()) if (isBlind(id)) { blind = true; break; }
        }
        EnemyIntentVisual::SetHidden(blind);
    }
    SyncFaithBuffs();
    m_screenFx.Update(deltaTime, { m_player->GetHp(), m_player->GetMaxHp(),
        m_player->GetBuffManager().HasBuff(BuffType::Poison),
        m_player->GetBuffManager().HasBuff(BuffType::Burn) });   // 画面の演出（演出中・ヒットストップ中も止めない）
    // 信仰のマスから、ときどき光の粒が昇る（信仰が多いほど多い。見た目は effects.json の faith_mote）
    for (int r = 0; r < m_gridMap->GetRows(); r++)
        for (int c = 0; c < m_gridMap->GetCols(); c++)
        {
            int f = m_gridMap->GetCell(c, r).faith;
            if (f <= 0) continue;
            float chance = deltaTime * (0.4f + 0.12f * f);            // 1秒あたり 信仰1→約0.5回、10→約1.6回
            if ((float)rand() / RAND_MAX >= chance) continue;
            float ox = ((float)rand() / RAND_MAX - 0.5f) * 0.8f;      // マスの中のどこか
            float oz = ((float)rand() / RAND_MAX - 0.5f) * 0.8f;
            EffectManager::Play("faith_mote",
                (c - m_gridMap->GetCols() / 2.0f) * 1.1f + ox, 0.05f,
                (r - m_gridMap->GetRows() / 2.0f) * 1.1f + oz);
        }
    // 背景テーマの空気に舞う粒（ドラゴンのステージの火の粉など）。盤面より少し広い範囲の地面から、1秒に ambientRate 個くらい
    {
        const BattleTheme th = CurrentTheme();
        if (th.ambientFx && (float)rand() / RAND_MAX < deltaTime * th.ambientRate)
        {
            float hx = m_gridMap->GetCols() * 1.1f * 0.5f + 1.5f, hz = m_gridMap->GetRows() * 1.1f * 0.5f + 1.0f;
            float x = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * hx;
            float z = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * hz;
            EffectManager::Play(th.ambientFx, x, 0.05f, z);
        }
    }
    UpdateBossGimmick();   // 3層ボス：楔の生存で無敵/覚醒を制御
    if (m_awakenCinematic > 0.0f)
    {
        m_awakenCinematic -= deltaTime;

        // 画面全体の円を一定間隔で追加し、経過時間を進める
        m_awakenRingTimer -= deltaTime;
        if (m_awakenRingTimer <= 0.0f) { m_awakenRings.push_back(0.0f); m_awakenRingTimer = 0.32f; }
        for (auto& a : m_awakenRings) a += deltaTime;
        m_awakenRings.erase(
            std::remove_if(m_awakenRings.begin(), m_awakenRings.end(),
                [](float a) { return a > 1.1f; }),   // 寿命1.1秒
            m_awakenRings.end());

        ScreenShake::Add(0.6f);              // ずっと揺らす（グワングワン）

        // 演出だけ更新（操作・ターン遷移・時間経過は止める）
        ScreenShake::Update(deltaTime);
        EffectManager::Update(deltaTime);
        FloatingTextManager::Update(deltaTime);
        BuffPopupManager::Update(deltaTime);
        // 演出中：ボス付近にズームイン（揺れも乗せる。通常のカメラ更新はこの下でreturnされて通らない）
        if (m_awakenBoss)   // ズーム0.4（小さいほど寄る）
            BattleCamera::ApplyAt(m_renderer3D, m_awakenBoss->worldX, m_awakenBoss->worldZ - 1.5f, 0.4f);
        return;
    }
    if (m_freeLook) return;

#ifdef _DEBUG
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
#endif

    if (HitStop::IsActive()) { HitStop::Update(deltaTime); return; }   // ヒットストップ中はゲームを止める

    FloatingTextManager::Update(deltaTime);
    BuffPopupManager::Update(deltaTime);
    // 時間を置いて1つずつ進める処理（どれも同じ形：1件処理して、返した秒数だけ待つ。中身は Step〜）
    m_playerHits.Update(deltaTime,  [&](PlayerHit& h) { return StepPlayerHit(h); });
    m_enemyHits.Update(deltaTime,   [&](EnemyHit& h)  { return StepEnemyHit(h); });
    m_coilTraps.Update(deltaTime,   [&](CoilTrap& c)  { return StepCoilTrap(c); });
    m_startPoison.Update(deltaTime, [&](int& amount)  { return StepStartPoison(amount); });
    m_chain.Update(deltaTime,       [&](ChainWave& w) { return StepChainWave(w); });
    m_detonate.Update(deltaTime,    [&](TrapFire& f)  { return StepDetonate(f); });

    UiNotice::Update(deltaTime);
    if (UiNotice::ConsumeTriggered())
        m_battleUI->StartOverflowDiscardEffect();
    ScreenShake::Update(deltaTime);
    TurnBanner::Update(deltaTime);
    EffectManager::Update(deltaTime);
    UpdatePullTether();   // 引き寄せの「つなぐ物」をプレイヤーに付いていかせる
    UpdateFloorStepFx();  // 敵の床を踏んだら、足が着いた瞬間に床の演出
    m_tileDrop.Update(deltaTime, m_gridMap);   // 床・罠が新しく置かれたら、上から落とす
    if (m_fuseBeep.left > 0 && m_highlightTimer >= m_fuseBeep.next)   // 導火線の「ピッ」：光る瞬間に1回ずつ
    {
        Se::Play(Se::FuseBeep, 0.03f, 0.7f);
        m_fuseBeep.left--;
        m_fuseBeep.next += m_fuseBeep.period;
    }
    UpdateBuffGainFx();   // バフが付いた／増えたらエフェクト

    // オロチの脱皮：ターンは使わず、その瞬間に演出だけ出す
    for (auto enemy : m_enemies)
        if (enemy->IsSnake() && enemy->ConsumeShed())
        {
            auto [hc, hr] = enemy->HitCell();                              // 殴った場所に出す
            float wx = (hc - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float wz = (hr - m_gridMap->GetRows() / 2.0f) * 1.1f;
            EffectManager::Play("chain_break_venom", wx, 0.6f, wz);        // 緑の破片
            ScreenShake::Add(0.5f);
            FloatingTextManager::Spawn(wx, 1.0f, wz, L"脱皮！ 巻きつき・攻撃力アップ",
                XMFLOAT4(0.5f, 1.0f, 0.4f, 1.0f), 40.0f);
        }

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
    m_player->Update(deltaTime);
    m_player->UpdateHitFlash(deltaTime);

    for (auto enemy : m_enemies)
    {
        enemy->UpdateMove(deltaTime);
        enemy->UpdateLunge(deltaTime);
        enemy->UpdateJump(deltaTime);
        enemy->UpdateHitFlash(deltaTime);
        enemy->FaceToward(m_player->worldX);
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

    // カメラの手での操作（ホイール・右ドラッグ・ホイールクリック）。操作のデモ中は動かせない
    if (!m_camera.DemoActive()
        && m_camera.HandleInput(m_input, m_gridMap->GetCols(), m_gridMap->GetRows(), m_player->worldX, m_player->worldZ))
    {
        // ドラッグせずに離した＝単押し→カード選択解除（Moveカードは除く）
        if (m_selectedCardIndex >= 0
            && m_selectedCardIndex < (int)m_hand.GetCards().size()
            && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
            m_selectedCardIndex = -1;
    }

    // ===== 狙いカメラ =====
    // ・マスを選ぶカードを選んでいる間、狙えるマスが手札に隠れていたら、隠れなくなるまで奥へ寄せる
    // ・移動カードで経路を引いている間は、移動予定のマス（ゴースト）を画面の中央へ寄せる（カーソルも一緒に動かす）
    // ・選択が終わったら：移動カードで移動した → そのまま／キャンセルなど → 寄せる前の位置へ戻す
    // シーンは「どこへ寄せたいか」を決めるだけ。寄せ方・戻し方は BattleCamera
    if (m_camera.UserPanning())
    {
        m_camera.AimCancel();
        Cursor::Request(CursorKind::Camera);   // 右ドラッグ中＝カメラ操作のカーソル
    }

    bool carryCursor = false;                 // このフレーム、カーソルをゴーストと一緒に動かすか
    std::pair<int, int> carryCell = { -1, -1 };
    float carryBX = 0.0f, carryBY = 0.0f;     // カメラを動かす前のゴーストの画面位置

    m_camera.BeginAimFrame();
    if (SelectedNeedsTarget() && Settings::Get().aimCamera)
    {
        if (!m_camera.Dragging())
        {
            const CardData* sd = m_hand.GetCards()[m_selectedCardIndex]->GetData();
            bool moved;
            if (sd->type == CardType::Move && !m_movePath.empty())
            {
                // 移動予定のマス（ゴースト）を中央へ。カメラの注視点を少し奥に置くと中央に映る（初期カメラと同じ -0.7）
                std::pair<int, int> tip = m_movePath.back();
                float goalX = (tip.first - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float goalZ = (tip.second - m_gridMap->GetRows() / 2.0f) * 1.1f - 0.7f;

                // カーソルは「カーソルの下のマス」と一緒に動かす（ゴーストではない）
                if (m_hoveredCell.first >= 0)
                {
                    carryCell = m_hoveredCell;
                    carryCursor = CellToScreen(carryCell.first, carryCell.second, carryBX, carryBY);
                }
                moved = m_camera.AimFollow(goalX, goalZ, deltaTime);
            }
            else
                moved = m_camera.AimPan(0.0f, HiddenTargetsPanZ(*sd), 8.0f, deltaTime);   // 経路がまだ無い：手札に隠れるマスを出す
            if (!moved) carryCursor = false;
        }
    }
    else m_camera.AimEnd();

    MaybeStartCamDemo();   // カメラ操作の自動デモ（初回の1ターン目から。どのヒントよりも先）
    m_camera.Update(deltaTime, m_gridMap->GetCols(), m_gridMap->GetRows(), m_player->worldX, m_player->worldZ);

    // カメラ更新
    {
        m_camera.Apply(m_renderer3D);

        // ゴーストを中央へ寄せている間は、カーソルの下のマスがずれた分だけカーソルもずらす（指している場所を変えない）
        if (carryCursor)
        {
            float ax, ay;
            if (CellToScreen(carryCell.first, carryCell.second, ax, ay))   // 新しいカメラでの位置
                m_input.MoveMouseBy(ax - carryBX, ay - carryBY);
        }

        m_highlighter.SetSelectedEnemy(FocusEnemy());

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
                float totalW = numCards * (CardVisual::CARD_W + 10.0f);
                float leftX = m_screenWidth / 2.0f - totalW / 2.0f;
                float rightX = leftX + totalW;
                float topY = m_screenHeight - CardVisual::CARD_HIDE_Y_OFFSET;
                cardArea = { (LONG)leftX, (LONG)topY, (LONG)rightX, (LONG)m_screenHeight };
            }

            m_highlighter.SetTravelPath(&m_movePath);

            m_highlighter.UpdatePlayerHighlight(
                m_playerCol, m_playerRow, data,
                m_enemies, m_gridMap, m_player,
                m_highlightTimer, m_hoveredCell,
                m_renderer3D, m_screenWidth, m_screenHeight,
                cardArea, m_player->GetBuffManager().HasBuff(BuffType::MoveLock), AttackTarget());
        }
        else
        {
            m_highlighter.ClearPlayerHighlight(m_gridMap);
        }

        // ハイライト明滅タイマーを更新
        m_highlightTimer += deltaTime * 0.5f; // 点滅速度調整（0に戻さない：戻すと整数倍でない点滅・色の入れ替わりがワープする）
        if (m_arrowRevealTimer > 0.0f) m_arrowRevealTimer -= deltaTime;
        if (m_battleResult != BattleResult::None)
        {
            Hint::Clear();   // 勝利/敗北画面にヒントを残さない
            // 勝敗決定後：操作は止めるが、飛んでいる途中のカード演出は最後まで動かす（止めると画面に残る）
            m_battleUI->UpdateDrawCardEffects(deltaTime);
            m_battleUI->UpdatePlayCardEffects(deltaTime);
            m_battleUI->UpdateDiscardEffects(deltaTime);
            m_battleUI->UpdateAddedCardEffects(deltaTime);
            PileFx::Update(deltaTime);   // 山札・捨て札の光の帯と弾み
            return;
        }

        m_highlighter.UpdateEnemyHighlight(
            m_enemies, m_gridMap, m_player,
            m_playerCol, m_playerRow, m_highlightTimer,
            m_decoyCol, m_decoyRow);

        // その場ヒント（初めてその状況になった時だけ）
        if (!m_turnManager.IsEnemyTurn())
        {
            bool threatened = std::any_of(m_enemies.begin(), m_enemies.end(),
                [](const Enemy* e) { return e->hitsPlayer; });
            if (threatened) Hint::Request("threat"); else Hint::Resolve("threat");
            if (m_player->GetEnergy() == 0) Hint::Request("endTurn");
            for (auto e : m_enemies)
                if (e->IsSnake())   // オロチの見どころ（出会った時点で）
                {
                    Hint::Request("snakeWeak");
                    Hint::Request("molt");
                }
            if (!m_camera.DemoActive() && PlayerDataManager::GetData().tutorialCamera)   // カメラのデモを見た後に（デモの後ろに重ねない）
            { Hint::Request("focus"); Hint::Request("camera"); }
        }
        else Hint::Resolve("endTurn");

        if (int n = m_deck.ConsumeReshuffled())   // 手番中のドローで山札を戻した時：戻す演出 → 終わってから引いたカードを出す
        {
            m_battleUI->StartReshuffleEffect(m_deck.GetDrawPileCount() + n);   // 戻した枚数（今の山札＋引いた分）
            m_battleUI->HoldNewestDraws(n, CardVisual::RESHUFFLE_FX_DUR);
            Audio::PlaySE("Assets/Sound/se/card.mp3");
        }
        m_battleUI->UpdateDrawCardEffects(deltaTime);
        // 時間差ドローの進行
        if (m_reshuffleFxTimer > 0.0f)
        {
            m_reshuffleFxTimer -= deltaTime;               // 演出中はドロー停止
        }
        else if (m_drawSeqRemaining > 0)
        {
            m_drawSeqTimer -= deltaTime;
            if (m_drawSeqTimer <= 0.0f)
            {
                if (m_deck.GetDrawPileCount() == 0 && m_deck.GetDiscardPileCount() > 0)
                {
                    // 山札が尽きた → 捨て札を山札へ戻す演出を挟む
                    int back = m_deck.GetDiscardPileCount();   // 山札へ戻す枚数
                    m_deck.Reset();
                    m_battleUI->StartReshuffleEffect(back);
                    Audio::PlaySE("Assets/Sound/se/card.mp3");
                    m_reshuffleFxTimer = CardVisual::RESHUFFLE_FX_DUR;
                }
                else
                {
                    std::string id = m_deck.DrawCard();
                    if (!id.empty()) { m_hand.AddCard(id); m_battleUI->StartDrawCardEffect(id); }
                    m_drawSeqRemaining--;
                    m_drawSeqTimer = DRAW_SEQ_INTERVAL;
                    if (m_drawSeqRemaining == 0) OnDrawSequenceComplete();
                }
            }
        }
        PileFx::Update(deltaTime);   // 山札・捨て札の光の帯と弾み
        m_battleUI->UpdatePlayCardEffects(deltaTime);
        if (!m_burnHits.empty())
        {
            m_burnClock += deltaTime;
            for (auto it = m_burnHits.begin(); it != m_burnHits.end(); )
            {
                if (m_burnClock >= it->atTime)
                {
                    m_player->TakeDamage(it->dmg);
                    if (it->curse)
                        EffectManager::Play("curse_skull",
                            m_player->worldX, m_player->worldY + 0.7f, m_player->worldZ);
                    it = m_burnHits.erase(it);
                }
                else ++it;
            }
        }
        // 攻撃カードを選んでいる間は、剣を振りかぶって構える
        {
            bool ready = m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size()
                && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type == CardType::Attack;
            m_player->SetReady(ready);
            if (ready)   // 狙っている先（今離したら当たる敵／射程内でカーソルを乗せているマス）の方を向く
            {
                const CardData* d = m_hand.GetCards()[m_selectedCardIndex]->GetData();
                int tc = -1, tr = -1;
                if (const Enemy* t = AttackTarget())
                {
                    int best = 9999;   // 大きい敵は、自分に一番近いマス
                    for (auto& [c, r] : t->OccupiedCells())
                    {
                        int dd = abs(c - m_playerCol) + abs(r - m_playerRow);
                        if (dd < best) { best = dd; tc = c; tr = r; }
                    }
                }
                else if (m_hoveredCell.first >= 0 && CellInCardRange(*d, m_hoveredCell.first, m_hoveredCell.second))
                {
                    tc = m_hoveredCell.first; tr = m_hoveredCell.second;
                }
                if (tc >= 0) m_player->FaceToward(tc - m_playerCol, tr - m_playerRow);
            }
        }
        bool selectedNeedsTarget = SelectedNeedsTarget();
        m_battleUI->UpdateCardAnimations(deltaTime, (int)m_hand.GetCards().size(), m_hoveredCardIndex, 
            m_selectedCardIndex, m_input.GetMousePos(), selectedNeedsTarget,
            &m_discardSelected);
        m_battleUI->UpdateDiscardEffects(deltaTime);
        m_battleUI->UpdateAddedCardEffects(deltaTime);

        // 勝利判定
        if (m_enemies.empty())
        {
            CancelDiscardSelect();   // 捨てる画面と勝利バナーを重ねない
            m_battleResult = BattleResult::Win;
            Telemetry::Instance().Log("battle_end", {
                {"result", "win"},
                {"turns",  m_turnCount},
                {"hpStart",m_hpStart},
                {"hpLeft", m_player->GetHp()},
                {"enc",    m_battleEnemyId},
                });

            m_player->Heal(RelicManager::SumValue("winHeal"));   // レリック（pd.hpに反映される）
            // 止血帯：この戦闘で自傷で失ったHPの value% を戻す（端数は切り上げ）
            if (int pct = RelicManager::SumValue("selfHealBack"); pct > 0 && m_player->GetSelfLost() > 0)
                m_player->Heal((m_player->GetSelfLost() * pct + 99) / 100);

            // HPを保存、現在のマスをクリア済みに
            auto& pd = PlayerDataManager::GetData();
            pd.hp = m_player->GetHp();

            if (m_category == EncCategory::Boss)   // ボス撃破で全回復
            {
                m_player->Heal(m_player->GetMaxHp());
                pd.hp = m_player->GetHp();
            }

            if (m_category == EncCategory::Boss && pd.layer < 3)
            {
                // ボスのレリックは3つの候補から選ばせる（次の層のフィールドで選択画面）。候補が無ければ従来どおりランダム付与
                pd.bossRelicChoices = RelicManager::RandomUnownedMany("boss", 3);
                if (pd.bossRelicChoices.empty())
                {
                    std::string br = RelicManager::RandomDrop();
                    if (!br.empty()) { PlayerDataManager::AddRelic(br); m_rewardRelic = br; }
                }
            }

            // 初勝利：クラフト導入（コア＋素材を確定ドロップ。
            // 勝利画面で行0=コア/行1=素材に固定し、チュートリアルのスポットライトを合わせる）
            int matDrops = 0;   // コア込みのドロップ数（最大3）
            if (!pd.tutorialCraft)
            {
                pd.materials["core_slash"] += 1;      m_dropResult.push_back({ "core_slash", 1, true });  matDrops++;
                pd.materials["sword_fragment"] += 1;  m_dropResult.push_back({ "sword_fragment", 1, false }); matDrops++;
            }

            if (m_category == EncCategory::Elite)   // エリートはコアを1つ必ず落とす（今の層までのコアからランダム）
            {
                std::vector<std::string> cores;
                for (auto& kv : MaterialDataBase::AllBases())
                    if (kv.second.layer == 0 || kv.second.layer <= pd.layer) cores.push_back(kv.first);
                if (!cores.empty() && matDrops < 3)
                {
                    std::string cid = cores[rand() % cores.size()];
                    pd.materials[cid] += 1; m_dropResult.push_back({ cid, 1, true }); matDrops++;
                }
            }

            // 素材ドロップ判定
            for (auto& eid : m_defeatedEnemyIds)
            {
                if (matDrops >= 3) break;                 // 上限
                const EnemyData* ed = EnemyDataBase::Get(eid);
                if (!ed) continue;
                for (auto& d : ed->drops)
                {
                    if (matDrops >= 3) break;
                    const MaterialDef* md = MaterialDataBase::GetMaterial(d.id);
                    if (md && md->layer != 0 && md->layer != PlayerDataManager::GetData().layer) continue;  // 層外素材は落とさない
                    if (rand() % 100 < d.chance)
                    {
                        int n = d.min + (d.max > d.min ? rand() % (d.max - d.min + 1) : 0);
                        pd.materials[d.id] += n;
                        m_dropResult.push_back({ d.id, n, d.rare });
                        matDrops++;                        // 加算
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
            PlayerDataManager::AddGold(8 + rand() % 8);
            if (m_category == EncCategory::Elite)
            {
                PlayerDataManager::AddGold(25);
                int em = RelicManager::SumValue("eliteMaterial");
                // エリート報酬：レリック
                std::string rr = RelicManager::RandomDrop();
                if (!rr.empty()) { PlayerDataManager::AddRelic(rr); m_rewardRelic = rr; }
                if (em > 0)
                {
                    std::vector<std::string> mids;
                    for (auto& kv : MaterialDataBase::AllMaterials()) mids.push_back(kv.first);
                    for (int k = 0; k < em && !mids.empty() && matDrops < 3; k++)
                    {
                        std::string mid = mids[rand() % mids.size()];
                        pd.materials[mid] += 1;
                        m_dropResult.push_back({ mid, 1, false });
                        matDrops++;                        // 加算
                    }
                }
            }

            for (auto& dr : m_dropResult)
                Telemetry::Instance().Log("material_drop", {
                    {"id", dr.id}, {"n", dr.count}, {"rare", dr.rare}
                    });
            PlayerDataManager::Save();
            return;
        }

        if (m_player->GetHp() <= 0) TryPhoenixRevive();   // 不死鳥の尾羽
        // 敗北判定
        if (m_player->GetHp() <= 0)
        {
            CancelDiscardSelect();
            m_battleResult = BattleResult::Lose;
            Telemetry::Instance().Log("battle_end", {
                {"result", "lose"},
                {"turns",  m_turnCount},
                {"hpStart",m_hpStart},
                {"hpLeft", 0},
                {"enc",    m_battleEnemyId},
                });
            return;
            // セーブデータを削除（ニューゲームからやり直し）
            
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
                if (m_windupTimer > 0.0f)            // 落下中は行動を保留
                {
                    m_windupTimer -= deltaTime;
                    if (m_windupTimer > 0.0f) break;
                }

                while (m_currentEnemyIdx < (int)m_enemies.size()
                    && (m_enemies[m_currentEnemyIdx]->GetHp() <= 0
                        || m_enemies[m_currentEnemyIdx]->TakeJustSummoned()))
                {
                    m_currentEnemyIdx++; m_windupPlayed = false;
                }

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

                // 予備動作：落下演出を出して、着弾まで実行を待つ
                const EnemyAction* wa = enemy->GetNextAction();
                if (wa && !wa->windupVfx.empty() && wa->windup > 0.0f && !m_windupPlayed
                    && (!wa->HasEffect(EffectKind::PullPlayer) || wa->target.unavoidable
                        || enemy->IsThreateningCell(m_playerCol, m_playerRow, *wa)))   // 引き寄せは、当たる時だけ伸ばす
                {
                    int ac, ar; enemy->GetAnchorCell(*wa, tC, tR, ac, ar);
                    float wx = (ac - m_gridMap->GetCols() / 2.0f) * 1.1f;
                    float wz = (ar - m_gridMap->GetRows() / 2.0f) * 1.1f;
                    // 飛び道具は敵の体の真ん中から飛ばす（飛び始めを使わない演出は、今までどおり wx,0.5,wz に出る）
                    XMFLOAT3 from(enemy->worldX, enemy->worldY + enemy->height * 0.5f, enemy->worldZ);
                    XMFLOAT3 mouth, ln, lf; float mrot;
                    if (wa->windupAt == WindupAt::Mouth && EnemyMouth(enemy, *wa, tC, tR, mouth, mrot))   // 口に集める（滅びの息吹のタメ）
                        m_windupHandle = EffectManager::Play(wa->windupVfx, mouth.x, mouth.y, mouth.z, mrot);
                    else if (wa->windupAt == WindupAt::Line)   // 列の端から端へ（次元十字の裂け目とビーム）。縦・横の列ごとに1本
                    {
                        auto playLine = [&](const TargetSpec& tg) {
                            if (LineEnds(enemy, tg, tC, tR, enemy->gridCol, enemy->gridRow, ln, lf))
                                m_windupHandle = EffectManager::PlayFrom(wa->windupVfx, ln, lf);
                        };
                        playLine(wa->target);
                        for (auto& tg : wa->extraTargets) playLine(tg);
                    }
                    else if (wa->windupAt == WindupAt::Self)   // 敵自身（次元崩しの吸い込み）
                        m_windupHandle = EffectManager::Play(wa->windupVfx, from.x, from.y, from.z);
                    else
                        m_windupHandle = EffectManager::PlayFrom(wa->windupVfx, from, XMFLOAT3(wx, 0.5f, wz));
                    m_windupPlayed = true;
                    m_windupTimer = wa->windup;
                    break;
                }
                m_windupPlayed = false;
                int windupHandle = m_windupHandle; m_windupHandle = 0;   // この行動の予備動作の演出（引き寄せならこの後も使う）

                int preCol = enemy->gridCol, preRow = enemy->gridRow;   // 実行前の位置（予告の範囲の中心。移動する攻撃でも演出は予告どおりの所に出す）
                // 実行前の攻撃範囲を控える（移動を伴う攻撃でも予告と一致させる）
                std::vector<std::pair<int, int>> preCells;
                if (wa && !wa->target.unavoidable && wa->target.approach != ApproachType::Dash)
                {
                    for (auto& ef : wa->effects)
                        if (ef.kind == EffectKind::Damage || (ef.kind == EffectKind::Debuff && ef.applyTo == ApplyTo::Player))   // デバフだけの行動も範囲に演出を出す
                        {
                            preCells = enemy->GetThreatCells(*wa, m_gridMap);
                            break;
                        }
                }

                // 現在の行動を実行
                bool targetedDecoy = (m_decoyCol >= 0 && tC == m_decoyCol && tR == m_decoyRow);
                bool atk = false;
                int damage = 0;
                if (enemy->GetId() == "l3_sovereign" && !m_bossAwakened
                    && m_bossAwakenAtTurn >= 0 && m_turnCount >= m_bossAwakenAtTurn)
                {
                    // 覚醒ターン：何もせず演出＋無敵解除
                    m_bossAwakened = true;
                    m_bossAwakenAtTurn = -1;
                    enemy->SetInvulnerable(false);
                    enemy->SetTexture("darkload_awaken");
                    m_awakenCinematic = 2.5f;
                    m_awakenRingTimer = 0.0f;
                    m_awakenRings.clear();
                    m_awakenBoss = enemy;
                    ScreenShake::Add(1.0f);
                }
                else
                {
                    damage = enemy->ExecuteAction(ai, m_playerCol, m_playerRow, m_gridMap, m_player, m_enemies, tC, tR, &atk);
                    if (wa && windupHandle != 0 && wa->HasEffect(EffectKind::PullPlayer))   // 引き寄せ：伸ばした物を、引っぱる間くっつけておく
                    { m_tetherHandle = windupHandle; m_tetherEnemy = enemy; }
                }
                // 着弾点の演出（避けられても出す）
                if (wa && !wa->impactVfx.empty())
                {
                    int ac, ar; enemy->GetAnchorCell(*wa, tC, tR, ac, ar);
                    float ix = (ac - m_gridMap->GetCols() / 2.0f) * 1.1f;
                    float iz = (ar - m_gridMap->GetRows() / 2.0f) * 1.1f;
                    EffectManager::Play(wa->impactVfx, ix, 0.5f, iz);
                    ScreenShake::Add(0.35f);
                }

                if (wa && !wa->originVfx.empty()) PlayEnemyOriginFx(enemy, *wa, tC, tR);   // 口から吐く（当たり外れに関係なし）
                if (wa && !wa->allyVfx.empty())   // 味方に向けて飛ばす（楔を強化する）。体の真ん中から体の真ん中へ
                    for (auto* o : m_enemies)
                        if (o != enemy && o->GetHp() > 0 && !o->IsDying())
                            EffectManager::PlayFrom(wa->allyVfx, enemy->SpritePoint(0.0f, 0.5f), o->SpritePoint(0.0f, 0.5f));
                // 攻撃範囲に演出（当たり外れに関わらず出す）。hitVfx がある攻撃は、当たったら命中の演出（1発ごと）に任せる
                if (wa && (!wa->aroundVfx.empty() || !(damage > 0 && !wa->hitVfx.empty()))) PlayEnemyAreaFx(enemy, *wa, tC, tR, preCol, preRow, preCells);

                for (auto& pc : enemy->PendingCurses())
                {
                    const CardData* cd = CardDataBase::Get(pc.cardId);
                    for (int k = 0; k < pc.count; k++)
                    {
                        if (pc.target == "hand")   // 手札の一番左へ：真ん中で見せてから、すっと入る（何枚もなら1枚ずつ）
                        {
                            if (m_hand.AddCardFront(pc.cardId))
                            {
                                m_battleUI->OnCardInserted(true, k * 0.25f);
                                m_enemyAddedToHand++;
                                m_hoveredCardIndex = -1;                            // 左に入って番号がずれるので、マウスの所は選び直す
                                if (m_selectedCardIndex >= 0) m_selectedCardIndex++;   // 選んでいたカードは同じカードのまま
                            }
                        }
                        else if (pc.target == "discard")  m_deck.DiscardCard(pc.cardId);
                        else                              m_deck.AddCardRandom(pc.cardId);
                        if (pc.target != "hand")          // 山札/捨て札は見せてから飛ばす
                            m_battleUI->StartAddedCardEffect(cd, pc.target, k, pc.count);
                    }
                }
                enemy->PendingCurses().clear();

                for (auto& sm : enemy->TakePendingSummons())
                    SummonNear(enemy, sm.first, sm.second);

                m_playerCol = m_player->gridCol;
                m_playerRow = m_player->gridRow;
                bool fullyBlocked = false;
                bool multiHit = false;   // この攻撃が多段か（多段は Update の順番待ちで1発ずつ）
                if (damage > 0)
                {
                    float waveDelay = (wa && wa->waveTime > 0.0f) ? WaveArrival(enemy, *wa, tC, tR, preCol, preRow, CellWX(m_playerCol), CellWZ(m_playerRow)) : 0.0f;
                    if (enemy->GetLastHitCount() > 1 || waveDelay > 0.0f)
                    {
                        // 多段：1発ずつ時間差で適用／広がる波：波がプレイヤーに届いた時に（状態異常は最後の1発の後）
                        int n = (std::max)(1, enemy->GetLastHitCount());
                        int each = (n > 1) ? enemy->GetLastHitDamage() : damage;
                        std::vector<EnemyHit> hits(n, EnemyHit{ enemy, each, false, wa ? wa->hitVfx : std::string() });
                        hits.back().last = true;
                        m_enemyHits.Start(std::move(hits), waveDelay);
                        multiHit = true;
                    }
                    else
                    {
                        int hpBefore = m_player->GetHp(), blockBefore = m_player->GetBlock();
                        XMFLOAT3 from(enemy->worldX, 0.0f, enemy->worldZ);   // ブロックの盾を、この敵の方に向ける
                        m_player->TakeDamage(damage, DamageFeel::Hit, &from);
                        if (wa && !wa->hitVfx.empty()) PlayEnemyHitFx(wa->hitVfx);
                        fullyBlocked = (m_player->GetHp() == hpBefore);
                        int blocked = blockBefore - m_player->GetBlock();   // ブロックで防いだ量
                        int th = m_player->GetBuffManager().GetBuffValue(BuffType::Thorns);
                        if (th > 0 && enemy->GetHp() > 0) enemy->TakeDamage(th);
                        int rip = m_player->GetBuffManager().GetBuffValue(BuffType::Riposte);
                        if (blocked > 0 && rip > 0 && enemy->GetHp() > 0)   // 受け返し：防いだダメージの rip% を返す
                        {
                            enemy->TakeDamage(blocked * rip / 100);
                            float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                            float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                            EffectManager::Play("hit", wx, 0.6f, wz);
                            ScreenShake::Add(0.2f);
                        }
                    }
                }

                // 状態異常はダメージの後に付与（多段は最後の1発の後＝StepEnemyHit で）
                if (!multiHit)
                {
                    if (!fullyBlocked)
                        for (auto& b : enemy->PendingPlayerBuffs())
                            m_player->GetBuffManager().AddBuff(b);
                    enemy->PendingPlayerBuffs().clear();
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
                        for (auto& [dc, dr] : enemy->GetGridShape())   // 体のどこかが罠に乗るマスで止まる
                        {
                            int tc = c.first + dc, tr = c.second + dr;
                            if (tc >= 0 && tr >= 0 && tc < m_gridMap->GetCols() && tr < m_gridMap->GetRows()
                                && m_gridMap->GetCell(tc, tr).tileEffect.active)
                            {
                                m_dashStops.push_back(c);
                                break;
                            }
                        }
                    if (m_dashStops.empty() || m_dashStops.back() != path.back())
                        m_dashStops.push_back(path.back());   // 最終マスは必ず終点

                    m_dashStopIdx = 0;
                    m_dashEnemy->m_feel.SetDash(true);   // 土煙を出し続け、ブラーを伸ばす
                    DashGlideTo(m_dashStops[0].first, m_dashStops[0].second);
                    m_enemyPhase = EnemyTurnPhase::DashStep;
                    break;
                }

                // 通過した各マスの罠
                if (enemy->IsSnake())
                {
                    // とぐろ：経路の罠を段階発火キューへ（実際の発火はUpdateのシーケンサ）
                    for (auto& mp : enemy->GetMovePath())
                        if (m_gridMap->GetCell(mp.first, mp.second).tileEffect.active)
                            m_coilTraps.Push({ mp.first, mp.second, enemy });
                    if (!m_coilTraps.Empty()) m_coilTraps.Delay(0.0f);   // すぐ1つ目
                }
                else
                {
                    for (auto& mp : enemy->GetMovePath())   // 通ったマスごとに、体の下の罠を全部踏む
                    {
                        TriggerTrapsUnder(enemy, mp.first, mp.second, m_gridMap, m_enemies);
                        if (enemy->GetHp() <= 0) break;
                    }
                    if (enemy->GetHp() > 0)
                        TriggerTrapsUnder(enemy, enemy->gridCol, enemy->gridRow, m_gridMap, m_enemies);
                }

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

                if (m_enemyActionDelay <= 0 && !anyMoving && m_coilTraps.Empty() && m_enemyHits.Empty())   // 連撃・波のダメージが全部入ってから次へ
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
                    enemy->GetBuffManager().TickDurations(true);   // 敵のデバフ：敵のターンの終わりに減る（今と同じ）
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
                    TriggerTrapsUnder(m_dashEnemy, s.first, s.second, m_gridMap, m_enemies);   // 体の下の罠を全部
                    m_dashStopIdx++;
                }

                // 罠で倒れたら終了
                if (m_dashEnemy->GetHp() <= 0)
                {
                    m_dashEnemy->m_feel.SetDash(false);   // 突進おわり：止まった所に土煙、ブラーは縮んで消える
                    m_dashEnemy = nullptr;
                    m_enemyPhase = EnemyTurnPhase::WaitAction; m_enemyActionDelay = ENEMY_ACTION_PAUSE;
                    break;
                }

                // 次の stop へグライド、なければ終了
                if (m_dashStopIdx < (int)m_dashStops.size())
                    DashGlideTo(m_dashStops[m_dashStopIdx].first, m_dashStops[m_dashStopIdx].second);
                else
                {
                    m_dashEnemy->m_feel.SetDash(false);   // 突進おわり：止まった所に土煙、ブラーは縮んで消える
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
                float totalW = numCards * (CardVisual::CARD_W + 10.0f);
                float leftX = m_screenWidth / 2.0f - totalW / 2.0f;
                float rightX = leftX + totalW;
                float topY = m_screenHeight - CardVisual::CARD_H - CardVisual::CARD_HOVER_Y_OFFSET;
                cardArea = { (LONG)leftX, (LONG)topY, (LONG)rightX, (LONG)m_screenHeight };
            }

            m_highlighter.SetTravelPath(&m_movePath);

            m_highlighter.UpdatePlayerHighlight(
                m_playerCol, m_playerRow, data,
                m_enemies, m_gridMap, m_player,
                m_highlightTimer, m_hoveredCell,
                m_renderer3D, m_screenWidth, m_screenHeight,
                cardArea, m_player->GetBuffManager().HasBuff(BuffType::MoveLock), AttackTarget());
        }
        else
        {
            m_highlighter.ClearPlayerHighlight(m_gridMap);
        }
    }
}


void BattleScene::DashGlideTo(int c, int r)
{
    if (!m_dashEnemy) return;
    float wx, wz; m_dashEnemy->CenterWorld(c, r, m_gridMap, wx, wz);   // 形の中心へ
    float dist = fabsf(wx - m_dashEnemy->worldX) + fabsf(wz - m_dashEnemy->worldZ);
    float dur = (dist / 1.1f) * 0.07f;   // 1マスあたり0.07秒＝距離で時間が伸びる＝速度一定感を避けつつ滑らか
    if (dur < 0.06f) dur = 0.06f;
    m_dashEnemy->StartMove(wx, wz, dur, false);   // false = smoothstep（緩急つき）
}

