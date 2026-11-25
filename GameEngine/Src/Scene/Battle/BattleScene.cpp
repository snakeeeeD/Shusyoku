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

// ナイフ＋投擲術なら遠距離化（Diamond=範囲内の任意の敵を狙える。単体パスのままなので研磨も効く）
// 3層ボスの鎖：柱（楔）ごとの鎖の絵と、外れた時の破片
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


static const struct { const char* wedge; const char* chain; const char* fx; } BOSS_CHAINS[] = {
    { "wedge_sword",  "boss_chain_sword",  "chain_break_sword"  },
    { "wedge_venom",  "boss_chain_venom",  "chain_break_venom"  },
    { "wedge_shield", "boss_chain_shield", "chain_break_shield" },
    { "wedge_hex",    "boss_chain_hex",    "chain_break_hex"    },
};

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

void BattleScene::AddEnemy(int col, int row, const std::string& id)
{
    auto enemy = new Enemy();
    enemy->Init(id);
    enemy->gridCol = col;
    enemy->gridRow = row;
    enemy->BindGrid(m_gridMap, m_gridMap->GetCols(), m_gridMap->GetRows());

    enemy->CenterWorld(col, row, m_gridMap, enemy->worldX, enemy->worldZ);   // 形の中心に置く

    // 占有マスをセット
    CellType cellType = enemy->IsBoss() ? CellType::Boss : CellType::Enemy;
    for (auto& [dc, dr] : enemy->GetGridShape())
        m_gridMap->SetCellType(col + dc, row + dr, cellType);

    if (enemy->IsSnake())
    {
        enemy->InitSnake(m_gridMap);
        enemy->GetBuffManager().AddBuff({ BuffType::HeadWeak, 2, -1 });   // 頭弱点マーク（表示用・永続）
        enemy->GetBuffManager().AddBuff({ BuffType::Molt, Enemy::SHED_DAMAGE, -1 });   // 脱皮までの残り（表示用）
    }
    if (enemy->IsImmovable())
        enemy->GetBuffManager().AddBuff({ BuffType::Immovable, 0, -1 });   // 移動不可マーク（表示用・永続）

    if (id == "l3_sovereign")
    {
        enemy->SetInvulnerable(true);   // 初手決定前から無敵に
        for (auto& c : BOSS_CHAINS) enemy->AddOverlay(c.chain);
    }

    // 識別色：今いる敵が使っていない一番小さい番号（誰かが倒れても他の敵の色は変わらない）
    int ci = 0;
    while (std::any_of(m_enemies.begin(), m_enemies.end(),
        [ci](const Enemy* e) { return e->colorIndex == ci; }))
        ci++;
    enemy->colorIndex = ci;
    enemy->hueColor = HighlightPalette::EnemyHue(ci);

    m_enemies.push_back(enemy);
}

void BattleScene::SummonNear(Enemy* src, const std::string& id, int count)
{
    if (!src->SummonPlan().empty())                                        // 予告した場所に出す
    {
        for (auto& [c, r] : src->SummonPlan())
        {
            if (m_gridMap->GetCell(c, r).type != CellType::Empty) continue;   // 塞がれたら出ない
            AddEnemy(c, r, id);
            m_enemies.back()->MarkJustSummoned();
            EffectManager::Play("summon", m_enemies.back()->worldX, 0.4f, m_enemies.back()->worldZ);
        }
        src->ClearSummonPlan();
        return;
    }

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

void BattleScene::OnPlayerMoved(int fromCol, int fromRow, int moveDist)
{
    BuffManager& bm = m_player->GetBuffManager();
    m_player->AddMoved(moveDist);                                        // このターンの移動マス数
    m_player->AddBlock(bm.GetBuffValue(BuffType::Momentum) * moveDist);
    if (int charge = bm.GetBuffValue(BuffType::Charge) * moveDist)       // このターンの攻撃力
    {
        Buff b; b.type = BuffType::AttackUpTurn; b.value = charge; b.duration = 1;
        b.name = BuffInfo::Get(BuffType::AttackUpTurn).name; b.description = L"";
        bm.AddBuff(b);
    }
    if (int hit = bm.GetBuffValue(BuffType::HitAndRun))                  // 隣の敵（体も含む）へ
        for (auto e : m_enemies)
        {
            if (e->GetHp() <= 0) continue;
            bool adj = e->OccupiesCell(m_playerCol + 1, m_playerRow) || e->OccupiesCell(m_playerCol - 1, m_playerRow)
                || e->OccupiesCell(m_playerCol, m_playerRow + 1) || e->OccupiesCell(m_playerCol, m_playerRow - 1);
            if (adj) e->TakeDamage(hit);
        }
    m_player->AddBlock(RelicManager::SumValue("moveBlock"));
    Faith::Engrave(m_gridMap, fromCol, fromRow, RelicManager::SumValue("moveEngrave"));   // 巡礼者の笠：元のマスに刻む
    if (m_player->GetBuffManager().HasBuff(BuffType::Frenzy))
        m_player->TakeDamage(1, DamageFeel::Hit);   // 狂乱中の移動は身を削る
    // 今後の「移動時○○」レリックはここに追記
}

void BattleScene::OpenDiscardSelect(int n)
{
    // とどめを刺した時は開かない（倒れる演出の後の勝利バナーと被るため）
    if (std::all_of(m_enemies.begin(), m_enemies.end(),
        [](const Enemy* e) { return !e || e->GetHp() <= 0; })) return;
    m_discardSelectCount = min(n, (int)m_hand.GetCards().size());
    m_discardSelected.clear();
    m_discardViewMode = false;
}

void BattleScene::CancelDiscardSelect()
{
    m_discardSelectCount = 0;
    m_discardSelected.clear();
    m_discardViewMode = false;
    m_selectBurn = false;
}

// 手札を燃やす（index は何個でも、順番は問わない）。燃える演出で消して廃棄置き場へ。全部燃やしてから効果を発動（途中で引くと index がずれる）
std::vector<const CardData*> BattleScene::BurnHandCards(std::vector<int> idx)
{
    std::sort(idx.rbegin(), idx.rend());
    std::vector<const CardData*> burned;
    for (int i : idx)
    {
        if (i < 0 || i >= (int)m_hand.GetCards().size()) continue;
        const CardData* dd = m_hand.GetCards()[i]->GetData();
        m_battleUI->StartBurnFromHand(dd, i, (int)burned.size());   // 真ん中へ飛んで燃える（どのカードが燃えたか見せる）
        m_deck.ExhaustCard(m_hand.GetCards()[i]->GetId());
        m_hand.RemoveCard(i);
        m_battleUI->OnCardRemoved(i);
        burned.push_back(dd);
    }
    m_selectedCardIndex = -1;   // 手札が減ったので、選択とホバーは無効に（index のずれ対策）
    m_hoveredCardIndex = -1;
    for (int k = 0; k < (int)burned.size(); k++) OnCardBurned(burned[k], k);   // k＝燃やした順番（火の玉を少しずつずらす）
    return burned;
}

// カードを1枚燃やした時（燃やす経路は全部ここを通る）。燃やされた時の効果と、燃やした数。燃やすと得をするパワー・レリックはここに足す
// 効果を1つ作る（燃やした時のパワー・レリックの「引く」「ブロック」を、捨てた時の効果の処理に渡す）
static CardEffectData MakeEffect(CardEffectType t, int v)
{
    CardEffectData e; e.hasEffect = true; e.type = t; e.value = v;
    return e;
}

void BattleScene::OnCardBurned(const CardData* dd, int slot)
{
    m_burnedThisCombat++;
    m_burnedThisTurn++;
    if (dd && dd->onBurnEffect.hasEffect) ApplyDiscardEffect(dd->onBurnEffect);   // 効果の中身は「捨てた時」と同じ物を使う

    // パワー：炉心（ランダムな敵へ火の玉）・残り火の盾（ブロック）
    auto& bm = m_player->GetBuffManager();
    if (int v = bm.GetBuffValue(BuffType::BurnStrike); v > 0)
        if (Enemy* en = RandomAliveEnemy()) FireBolt(en, v, slot * 0.1f);
    if (int v = bm.GetBuffValue(BuffType::BurnGuard); v > 0) ApplyDiscardEffect(MakeEffect(CardEffectType::Block, v));

    // レリック（光らせるのは、発動したレリックだけ）
    auto fired = [](const char* kind) { for (auto* r : RelicManager::OwnedByKind(kind)) RelicManager::NotifyFired(r->id); };
    if (int v = RelicManager::SumValue("burnBlock"); v > 0)   // 燃え残りの指輪
    {
        ApplyDiscardEffect(MakeEffect(CardEffectType::Block, v)); fired("burnBlock");
    }
    if (m_burnedThisTurn == 1)                                 // 煤けた火打石：このターン初めて燃やした時
        if (int v = RelicManager::SumValue("burnFirstDraw"); v > 0)
        {
            ApplyDiscardEffect(MakeEffect(CardEffectType::Draw, v)); fired("burnFirstDraw");
        }
    if (dd && (dd->type == CardType::Status || dd->type == CardType::Curse) && !m_burnStatusEnergyUsed)   // 煤けた手袋：1ターンに1回
        if (int v = RelicManager::SumValue("burnStatusEnergy"); v > 0)
        {
            m_player->AddEnergy(v); m_burnStatusEnergyUsed = true; fired("burnStatusEnergy");
        }
    TickCountRelics("burn");   // 灰の砂時計（カウント系：count 枚燃やすごとに effect）
}

// 焼き直し：燃やしたカードの種類 → 効果（攻撃＝このターンの攻撃+v／スキル・移動＝ブロックv／パワー＝エナジー+2／状態異常・呪い＝1枚引く）
static CardEffectData BurnTypeEffect(CardType t, int v)
{
    CardEffectData e; e.hasEffect = true;
    switch (t)
    {
    case CardType::Attack: e.type = CardEffectType::ApplyBuff; e.buffType = "AttackUpTurn"; e.value = v; e.duration = 1; break;
    case CardType::Power:  e.type = CardEffectType::AddEnergy; e.value = 2; break;
    case CardType::Status: case CardType::Curse: e.type = CardEffectType::Draw; e.value = 1; break;
    default:               e.type = CardEffectType::Block; e.value = v; break;   // スキル・移動
    }
    return e;
}

// このカードで燃やした1枚ごとの効果（燃やした後）。焚き付け＝引く、焼却＝ブロック、生贄の刃＝コスト×N を狙った敵へ、焼き直し＝種類で分かれる
void BattleScene::ApplyBurnEffect(const CardData* src, const std::vector<const CardData*>& burned, Enemy* target)
{
    if (!src || !src->burnEffect.hasEffect) return;
    const CardEffectData& e = src->burnEffect;
    bool targetAlive = false;   // 選んでいる間に倒れて消えた敵には触らない
    for (auto* en : m_enemies) if (en == target && en->GetHp() > 0) targetAlive = true;
    float delay = 0.0f;
    for (auto* b : burned)
    {
        if (!b) continue;
        if (e.type == CardEffectType::DamageByCost)   // 燃えたカードの火の玉が飛んで、コスト×value の追撃
        {
            int dmg = (std::max)(0, b->cost) * e.value;
            if (!targetAlive || dmg <= 0) continue;
            FireBolt(target, dmg, delay);
            delay += 0.1f;
        }
        else if (e.type == CardEffectType::Transform)    // 変容：ランダムなカード（種類も問わない）に入れ替える（value>0 なら強化済み）
        {
            std::string id = CardDataBase::RandomPoolCard(b->id);
            if (!id.empty() && e.value > 0 && CardDataBase::Get(id + "+")) id += "+";
            if (!id.empty()) AddToHand(id, -1, BattleUI::BurnShowTime() + 0.15f);   // 廃棄したカードが燃え上がってから見せる
        }
        else if (e.type == CardEffectType::ByCardType)   // 燃やしたカードの種類で分かれる
            ApplyDiscardEffect(BurnTypeEffect(b->type, e.value));
        else
            ApplyDiscardEffect(e);   // 引く・ブロック・エナジーなど（「捨てた時」と同じ物を使う）
    }
}

// 燃えたカードの火の玉を敵へ飛ばし、着いたら dmg の追撃（カードが真ん中で燃え上がってから）。生贄の刃・炉心で共通
void BattleScene::FireBolt(Enemy* target, int dmg, float extraDelay)
{
    if (!target || dmg <= 0) return;
    const float FLY = 0.25f;   // 火の玉が着くまで（effects.json の fire_bolt の life と同じ）
    float at = BattleUI::BurnShowTime() + extraDelay;
    EffectManager::PlayFrom("fire_bolt", XMFLOAT3(m_player->worldX, m_player->worldY + m_player->height * 0.5f, m_player->worldZ),
        target->SpritePoint(0.0f, 0.5f), at);
    PlayerHit h{ { target }, dmg };
    if (m_playerHits.Empty()) m_playerHits.Start({ h }, at + FLY); else m_playerHits.Push(h);
}

// 生きている敵からランダムに1体（炉心の的）
Enemy* BattleScene::RandomAliveEnemy()
{
    std::vector<Enemy*> alive;
    for (auto* en : m_enemies) if (en && en->GetHp() > 0) alive.push_back(en);
    if (alive.empty()) return nullptr;
    static std::mt19937 rng(std::random_device{}());
    return alive[std::uniform_int_distribution<int>(0, (int)alive.size() - 1)(rng)];
}

// 手札にカードを加える（山札からではなく、真ん中で見せてから右端へ。状態異常を入れられた時と同じ見せ方）
// costThisTurn>=0 ならこのターンだけそのコスト（灰からの再生など）。delay 秒は隠して待つ。満杯で捨て札へ行ったら nullptr
Card* BattleScene::AddToHand(const std::string& id, int costThisTurn, float delay)
{
    Card* c = m_hand.AddCard(id);
    if (!c) return nullptr;
    m_battleUI->OnCardInserted(false, delay);
    if (costThisTurn >= 0) c->SetCostThisTurn(costThisTurn);
    return c;
}

// 不死鳥の尾羽：HPが0になったら1度だけ復活（HP＝廃棄札1枚につき value、最大HPの半分まで。最低1）。使ったかは relicCounters に1（セーブされる）
bool BattleScene::TryPhoenixRevive()
{
    auto& pd = PlayerDataManager::GetData();
    for (auto* r : RelicManager::OwnedByKind("revive"))
    {
        if (pd.relicCounters[r->id] > 0) continue;   // 使用済み
        int hp = std::clamp(m_deck.GetExhaustPileCount() * r->value, 1, (std::max)(1, m_player->GetMaxHp() / 2));
        m_player->SetHp(hp);
        pd.relicCounters[r->id] = 1;
        RelicManager::NotifyFired(r->id);
        EffectManager::Play("nova_fire", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);   // 炎が広がって
        EffectManager::Play("heal", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);        // 回復
        return true;
    }
    return false;
}

// 手札をランダムに n 枚燃やす
std::vector<const CardData*> BattleScene::BurnRandomCards(int n)
{
    std::vector<int> all;
    for (int i = 0; i < (int)m_hand.GetCards().size(); i++) all.push_back(i);
    std::shuffle(all.begin(), all.end(), std::mt19937(std::random_device{}()));
    if ((int)all.size() > n) all.resize(n);
    return BurnHandCards(all);
}

// 燃やすカードを選ぶ画面（捨てる画面を使い回す）。手札が n 枚以下なら、選ばずに全部燃やす
void BattleScene::OpenBurnSelect(int n)
{
    if ((int)m_hand.GetCards().size() <= n) { ApplyBurnEffect(m_burnSource, BurnRandomCards(n), m_burnTarget); return; }   // 選ぶまでもない時は全部
    OpenDiscardSelect(n);
    m_selectBurn = true;
}

void BattleScene::UpdateBossGimmick()
{
    Enemy* boss = nullptr;
    int wedgesAlive = 0;
    for (auto e : m_enemies)
    {
        if (!e || e->IsDying()) continue;
        const std::string& id = e->GetId();
        if (id.rfind("wedge_", 0) == 0) wedgesAlive++;
        else if (id == "l3_sovereign") boss = e;
    }
    if (!boss) return;   // このバトルに3層ボスがいなければ何もしない

    if (wedgesAlive > 0) m_bossHadWedges = true;

    // 倒れた柱の鎖を外す（破片＋画面の揺れ）
    if (m_bossHadWedges)
        for (auto& c : BOSS_CHAINS)
        {
            bool alive = std::any_of(m_enemies.begin(), m_enemies.end(), [&](Enemy* e) {
                return e && !e->IsDying() && e->GetId() == c.wedge; });
            if (!alive && boss->RemoveOverlay(c.chain))
            {
                EffectManager::Play(c.fx, boss->worldX, 1.4f, boss->worldZ);
                ScreenShake::Add(0.35f);
            }
        }

    if (!m_bossAwakened)
    {
        if (wedgesAlive > 0) m_bossHadWedges = true;

        if (!m_bossAwakened)
        {
            boss->SetInvulnerable(true);   // 覚醒するまでは無敵維持
            if (m_bossHadWedges && wedgesAlive == 0 && m_bossAwakenAtTurn < 0)
                m_bossAwakenAtTurn = m_turnCount + 1;   // 楔全滅→次の敵ターンで覚醒
        }
        else
        {
            boss->SetInvulnerable(true);   // 覚醒前は常に無敵
        }
    }
}

void BattleScene::SwapWedges()
{
    if (m_wedgeCorners.empty()) return;

    // プレイヤーが四隅にいるターンは重なり防止でスキップ
    for (auto& s : m_wedgeCorners)
        if (s.first == m_playerCol && s.second == m_playerRow) return;

    std::vector<Enemy*> wedges;
    for (auto e : m_enemies)
        if (e && !e->IsDying() && e->GetHp() > 0 && e->GetId().rfind("wedge_", 0) == 0)
            wedges.push_back(e);
    int n = (int)wedges.size();
    if (n < 2) return;   // 1体以下は交換なし

    // 使う四隅をシャッフルして先頭n個（4=四隅/3=三角/2=ランダム2）
    std::vector<std::pair<int, int>> slots = m_wedgeCorners;
    for (int i = (int)slots.size() - 1; i > 0; i--) std::swap(slots[i], slots[rand() % (i + 1)]);
    if ((int)slots.size() > n) slots.resize(n);

    // 楔の並びもシャッフル（位置が入れ替わる）
    for (int i = n - 1; i > 0; i--) std::swap(wedges[i], wedges[rand() % (i + 1)]);

    // 全楔のセルをクリア → 新座標へ一括配置（同時交換の衝突回避）
    for (auto w : wedges)
        for (auto& [dc, dr] : w->GetGridShape())
        {
            int c = w->gridCol + dc, r = w->gridRow + dr;
            if (c >= 0 && c < m_gridMap->GetCols() && r >= 0 && r < m_gridMap->GetRows())
                m_gridMap->SetCellType(c, r, CellType::Empty);
        }

    for (int i = 0; i < n; i++)
    {
        Enemy* w = wedges[i];
        int nc = slots[i].first, nr = slots[i].second;
        w->gridCol = nc; w->gridRow = nr;
        for (auto& [dc, dr] : w->GetGridShape())
        {
            int c = nc + dc, r = nr + dr;
            if (c >= 0 && c < m_gridMap->GetCols() && r >= 0 && r < m_gridMap->GetRows())
                m_gridMap->SetCellType(c, r, CellType::Enemy);
        }
        float wx = (nc - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (nr - m_gridMap->GetRows() / 2.0f) * 1.1f;
        w->StartMove(wx, wz, 0.35f);   // グライドで移動
    }
}

void BattleScene::BossInvulnAction(Enemy* boss)
{
    if (m_turnCount % 2 != 0)
        return;   // 溜めターン（何もしない）

    if (((m_turnCount / 2) % 2) == 0)
    {
        // 楔の攻撃力を永続アップ
        for (auto e : m_enemies)
        {
            if (!e || e->IsDying()) continue;
            if (e->GetId().rfind("wedge_", 0) == 0)
                e->GetBuffManager().AddBuff({ BuffType::AttackUp, 2, -1 });   // +2・永続
        }
        EffectManager::Play("explosion_all",
            boss->worldX, boss->worldY + boss->height * 0.5f, boss->worldZ);
    }
    else
    {
        // お邪魔カードを山札に挿入
        const char* junk = "status_static";
        m_deck.AddCard(junk);
        m_battleUI->StartAddedCardEffect(CardDataBase::Get(junk), "deck", 0, 1);
    }
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

// ダメージの無い、プレイヤーへのデバフだけの行動：範囲のマスに出す「しみこむ」演出（色は画面のふちのデバフと同じ）
static const struct { BuffType type; const char* fx; } DEBUFF_AREA_FX[] = {
    { BuffType::Poison, "debuff_area_poison" },
    { BuffType::Slow, "debuff_area_bind" },       { BuffType::Root, "debuff_area_bind" },
    { BuffType::Vulnerable, "debuff_area_vuln" }, { BuffType::Frail, "debuff_area_vuln" }, { BuffType::DefenseDown, "debuff_area_vuln" },
    { BuffType::Weak, "debuff_area_weak" },       { BuffType::AttackDown, "debuff_area_weak" },
    { BuffType::Burn, "debuff_area_burn" },
};

// 敵の攻撃が当たった範囲の演出（当たり外れに関わらず出す）
//  中心から広がる形（周り・正方形・ひし形）：衝撃波を範囲のマス数ぶん、外へ重ねて出す（敵の識別色）
//  それ以外（十字・直線・突進・市松など）：マスごとに、敵から近い順に時間差で出す
void BattleScene::PlayEnemyAreaFx(Enemy* enemy, const EnemyAction& a, int tC, int tR, int preCol, int preRow,
    const std::vector<std::pair<int, int>>& preCells)
{
    if (a.target.unavoidable) return;
    bool isAtkAction = false;
    const char* debuffFx = nullptr;   // ダメージが無くデバフだけの行動なら、そのデバフの「しみこむ」演出
    for (auto& ef : a.effects)
    {
        if (ef.kind == EffectKind::Damage) isAtkAction = true;
        else if (ef.kind == EffectKind::Debuff && ef.applyTo == ApplyTo::Player && !debuffFx)
            for (auto& d : DEBUFF_AREA_FX)
                if (d.type == StringToBuffType(ef.buff)) { debuffFx = d.fx; break; }
    }
    if (!isAtkAction && debuffFx)   // デバフだけ：範囲のマスごとに、敵から近い順に（輪がしみこみ、粒が降る。粒を上から降らせるので高さ1で出す）
    {
        for (auto& [c, r] : preCells)
            EffectManager::Play(debuffFx, CellWX(c), 1.0f, CellWZ(r), 0.0f, 0.04f * (abs(c - preCol) + abs(r - preRow)));
        return;
    }
    if (!isAtkAction) return;

    bool isDash = (a.target.approach == ApproachType::Dash);
    RangeType rt = a.target.rangeType;
    const int SHOCK_MIN_RANGE = 1;   // この範囲以上の攻撃を衝撃波にする（1＝周り1マスの普通の攻撃も含めて全部）
    int range = a.target.range + enemy->GetBuffManager().GetBuffValue(BuffType::RangeUp);
    bool radial = (rt == RangeType::Area || rt == RangeType::Square || rt == RangeType::Diamond);
    if (radial && range >= SHOCK_MIN_RANGE && !isDash && a.impactVfx.empty())   // 着弾の演出（隕石の爆発など）がある攻撃は、そちらに任せる
    {
        float cx, cz, body;
        AroundCenter(enemy, a, tC, tR, preCol, preRow, cx, cz, body);
        if (!a.aroundVfx.empty())              // なぎ払いなど：衝撃波の代わりに、範囲の外の縁まで届く大きさで1つ（回り始めはランダム）
        {
            EffectManager::PlayAround(a.aroundVfx, cx, 0.5f, cz, range + 0.5f + body, (float)rand() / RAND_MAX * XM_2PI);
            if (a.waveTime > 0.0f && !a.areaVfx.empty())   // 広がる波：範囲のマスが、波の届いた順に（滅びの息吹の炎）
                for (auto& [c, r] : preCells)
                    EffectManager::Play(a.areaVfx, CellWX(c), 0.15f, CellWZ(r), 0.0f,
                        WaveArrival(enemy, a, tC, tR, preCol, preRow, CellWX(c), CellWZ(r)));
            return;
        }
        const float WAVE_GAP = 0.12f;          // 波と波の間の秒数
        for (int k = 1; k <= range; k++)       // 範囲1マスごとに波を1つ。k マス目まで届く大きさで、少しずつ遅らせて外へ重ねる
            EffectManager::PlayAround("enemy_shock", cx, 0.5f, cz, k + 0.5f + body, 0.0f, WAVE_GAP * (k - 1), enemy->hueColor);
        return;
    }

    const std::string afx = a.areaVfx.empty() ? "area_burst" : a.areaVfx;
    // 突進は「実際に走った経路」、それ以外は実行前の攻撃範囲
    std::vector<std::pair<int, int>> cells = isDash ? enemy->GetMovePath() : preCells;
    float step = isDash ? 0.10f : 0.04f;   // 突進はグライドに合わせて遅め
    for (int k = 0; k < (int)cells.size(); k++)
    {
        int c = cells[k].first, r = cells[k].second;
        int d = isDash ? k                                     // 走った順に尾を引く
            : abs(c - preCol) + abs(r - preRow);
        float delay = (a.waveTime > 0.0f && !isDash) ? WaveArrival(enemy, a, tC, tR, preCol, preRow, CellWX(c), CellWZ(r))   // 波：ダメージと同じ時間
                                                      : step * d;
        EffectManager::Play(afx, CellWX(c), 0.15f, CellWZ(r), 0.0f, delay);
    }
}

// 敵の口の位置と向き。位置は絵の口（enemies.json の mouth）、向きは扇なら扇の向き、ほかは口から狙ったマスの方。向きが無ければ false
bool BattleScene::EnemyMouth(Enemy* enemy, const EnemyAction& a, int tC, int tR, XMFLOAT3& pos, float& rot)
{
    pos = enemy->MouthWorld();
    float ux, uz;
    if (a.target.rangeType == RangeType::Cone) { ux = (float)enemy->GetAimDx(); uz = (float)enemy->GetAimDy(); }
    else { ux = CellWX(tC) - pos.x; uz = CellWZ(tR) - pos.z; }
    if (ux * ux + uz * uz < 0.000001f) return false;   // 向きが無い
    rot = atan2f(ux, uz);
    return true;
}

// 敵の口から攻撃の向きへ出す演出（originVfx）
void BattleScene::PlayEnemyOriginFx(Enemy* enemy, const EnemyAction& a, int tC, int tR)
{
    XMFLOAT3 p; float rot;
    if (EnemyMouth(enemy, a, tC, tR, p, rot)) EffectManager::Play(a.originVfx, p.x, p.y, p.z, rot);
}

// 周り全体の攻撃の中心と、体の大きさ（マス数の半分）。範囲の外の縁＝中心から (範囲 + 0.5 + body) マス
void BattleScene::AroundCenter(Enemy* enemy, const EnemyAction& a, int tC, int tR, int preCol, int preRow,
    float& cx, float& cz, float& body)
{
    body = 0.0f;
    if (a.target.anchor != AnchorType::Self)   // 範囲の中心が、狙ったマスや決まったマス
    {
        int ac, ar; enemy->GetAnchorCell(a, tC, tR, ac, ar);
        cx = CellWX(ac); cz = CellWZ(ar);
    }
    else                                       // 範囲の中心が、敵自身（2*2 なら体の中心。体の幅のぶん範囲も広い）
    {
        enemy->CenterWorld(preCol, preRow, m_gridMap, cx, cz);   // 動く前の位置（予告の範囲の中心）
        int lo = 0, hi = 0;
        for (auto& [dc, dr] : enemy->GetGridShape()) { lo = (std::min)(lo, dc); hi = (std::max)(hi, dc); }
        body = (hi - lo) * 0.5f;
    }
}

// 縦・横1列の攻撃の、敵に近い端と遠い端（ワールド。盤の外へ少しはみ出す）。列の攻撃でなければ false
bool BattleScene::LineEnds(Enemy* enemy, const TargetSpec& tg, int tC, int tR, int preCol, int preRow, XMFLOAT3& nearEnd, XMFLOAT3& farEnd)
{
    RangeType rt = tg.rangeType;
    if (rt != RangeType::Column && rt != RangeType::Row) return false;
    int ac, ar; enemy->GetAnchorCell(tg, tC, tR, ac, ar);
    if (tg.anchor == AnchorType::Self) { ac = preCol; ar = preRow; }   // 敵自身の列
    float cx, cz; enemy->CenterWorld(preCol, preRow, m_gridMap, cx, cz);
    const float EDGE_OUT = 0.6f * 1.1f;   // 盤の端から外へ（裂け目を盤の外に開く）。OUT は Windows のマクロなので使わない
    XMFLOAT3 e0, e1;
    if (rt == RangeType::Column)
    {
        e0 = XMFLOAT3(CellWX(ac), 0.5f, CellWZ(0) - EDGE_OUT);
        e1 = XMFLOAT3(CellWX(ac), 0.5f, CellWZ(m_gridMap->GetRows() - 1) + EDGE_OUT);
    }
    else
    {
        e0 = XMFLOAT3(CellWX(0) - EDGE_OUT, 0.5f, CellWZ(ar));
        e1 = XMFLOAT3(CellWX(m_gridMap->GetCols() - 1) + EDGE_OUT, 0.5f, CellWZ(ar));
    }
    float d0 = (e0.x - cx) * (e0.x - cx) + (e0.z - cz) * (e0.z - cz);
    float d1 = (e1.x - cx) * (e1.x - cx) + (e1.z - cz) * (e1.z - cz);
    nearEnd = (d0 <= d1) ? e0 : e1;
    farEnd = (d0 <= d1) ? e1 : e0;
    return true;
}

// 周りに広がる波（aroundVfx）が (x, z) に届くまでの秒数（プレイヤーへのダメージ・マスの炎で共通）。波は waveTime 秒で、範囲の外の縁まで一定の速さで広がる
float BattleScene::WaveArrival(Enemy* enemy, const EnemyAction& a, int tC, int tR, int preCol, int preRow, float x, float z)
{
    // 列の攻撃：(x, z) を通る列ごとに、敵に近い端からの割合×waveTime。いちばん早く届く列の時間（十字の交わる所は早い方）
    float onLine = -1.0f, nearest = -1.0f;
    auto lineT = [&](const TargetSpec& tg) {
        XMFLOAT3 ne, fe;
        if (!LineEnds(enemy, tg, tC, tR, preCol, preRow, ne, fe)) return;
        float lx = fe.x - ne.x, lz = fe.z - ne.z;
        float u = ((x - ne.x) * lx + (z - ne.z) * lz) / (lx * lx + lz * lz);
        float d = a.waveTime * (std::max)(0.0f, (std::min)(1.0f, u));
        float off = (tg.rangeType == RangeType::Column) ? fabsf(x - ne.x) : fabsf(z - ne.z);   // 列からの横のずれ
        if (off < 0.55f && (onLine < 0.0f || d < onLine)) onLine = d;
        if (nearest < 0.0f || d < nearest) nearest = d;
    };
    lineT(a.target);
    for (auto& tg : a.extraTargets) lineT(tg);
    if (nearest >= 0.0f) return (onLine >= 0.0f) ? onLine : nearest;
    float cx, cz, body;
    AroundCenter(enemy, a, tC, tR, preCol, preRow, cx, cz, body);
    int range = a.target.range + enemy->GetBuffManager().GetBuffValue(BuffType::RangeUp);
    float radius = (range + 0.5f + body) * 1.1f;
    float dx = x - cx, dz = z - cz;
    return a.waveTime * (std::min)(1.0f, sqrtf(dx * dx + dz * dz) / radius);
}

// 敵の攻撃が1発当たるたびの演出（hitVfx）。同じ所に重ならないよう、横の位置と傾きを少しずつ変える
void BattleScene::PlayEnemyHitFx(const std::string& id)
{
    auto rnd = []() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; };   // -1〜1
    const float POS_JITTER = 0.15f;   // 横にずらす幅（ワールド）
    const float ROT_JITTER = 0.14f;   // 傾ける幅（ラジアン。約8度。flash の絵は rot の分だけ画面の中で回る）
    EffectManager::Play(id, m_player->worldX + rnd() * POS_JITTER, m_player->worldY + m_player->height * 0.5f,
        m_player->worldZ, rnd() * ROT_JITTER);
}

// 引き寄せの「つなぐ物」：プレイヤーが引っぱられて動いている間は、両端を敵とプレイヤーに合わせる。止まったら敵の方へ戻す
// （踏ん張りで動かなかった時は、すぐ戻る＝耐えたのが見える）
void BattleScene::UpdatePullTether()
{
    if (m_tetherHandle == 0) return;
    if (m_tetherEnemy && m_player->IsMoving())
    {
        EffectManager::SetEnds(m_tetherHandle,
            XMFLOAT3(m_tetherEnemy->worldX, m_tetherEnemy->worldY + m_tetherEnemy->height * 0.5f, m_tetherEnemy->worldZ),
            XMFLOAT3(m_player->worldX, m_player->worldY + 0.5f, m_player->worldZ));   // 着く高さは予備動作と同じ 0.5
        return;
    }
    EffectManager::Stop(m_tetherHandle, 0.15f);
    m_tetherHandle = 0; m_tetherEnemy = nullptr;
}

// 敵の床を踏んだ演出：プレイヤーの見た目が新しいマスの真ん中に着いた時、そのマスの床の vfx を出す
// （歩く・突進・氷で滑る・引き寄せ、のどれでも、足が着いた瞬間に合う。床の効果そのものは TriggerTerrain のまま）
void BattleScene::UpdateFloorStepFx()
{
    if (!m_player || !m_gridMap) return;
    const int cols = m_gridMap->GetCols(), rows = m_gridMap->GetRows();
    const float px = m_player->worldX, pz = m_player->worldZ;
    int c = (int)floorf(px / 1.1f + cols / 2.0f + 0.5f);   // 見た目の位置に一番近いマス
    int r = (int)floorf(pz / 1.1f + rows / 2.0f + 0.5f);
    if (c < 0 || r < 0 || c >= cols || r >= rows || (c == m_floorFxCol && r == m_floorFxRow)) return;
    float cx = (c - cols / 2.0f) * 1.1f, cz = (r - rows / 2.0f) * 1.1f;
    if (fabsf(px - cx) >= 0.25f || fabsf(pz - cz) >= 0.25f) return;   // マスの真ん中まで来たら（すれ違うだけでは出さない）
    m_floorFxCol = c; m_floorFxRow = r;
    const auto& tile = m_gridMap->GetCell(c, r).tileEffect;
    const TerrainDef* def = (tile.active && tile.persistent) ? TerrainDataBase::Get(tile.id) : nullptr;
    if (def && !def->vfx.empty() && !RelicManager::HasKind("floorImmune"))
        EffectManager::Play(def->vfx, cx, 0.05f, cz);
}

// Draw（描画）は BattleSceneDraw.cpp

// HandleInput（入力）は BattleSceneInput.cpp

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


    for (auto d : toDelete)   // 引き寄せの「つなぐ物」の根元の敵が消えるなら、先に戻しておく
        if (d == m_tetherEnemy) { EffectManager::Stop(m_tetherHandle, 0.15f); m_tetherHandle = 0; m_tetherEnemy = nullptr; }

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

        PlayerDataManager::AddGold(RelicManager::SumValue("killGold"));   // レリック
        m_player->Heal(RelicManager::SumValue("killHeal") + m_player->GetBuffManager().GetBuffValue(BuffType::BloodThirst));   // 血の渇望も                        // レリック
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
    case CardEffectType::ApplyBuff: CardEffect::ApplyEffectToPlayer(e, m_player); break;   // 自分へのバフ（焼き直しの「このターンの攻撃+」）
    case CardEffectType::Block:
        m_player->AddBlock(m_player->GetBuffManager().GetFinalBlock(e.value));
        EffectManager::Play("block_gain", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);   // 青い盾の光
        break;
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
        if (dd && dd->endTurnDamage > 0) m_player->TakeDamage(dd->endTurnDamage);
        m_hand.DiscardAt(i);
        m_battleUI->OnCardRemoved(i);
    }
    for (auto& e : effects) ApplyDiscardEffect(e);   // 全部捨ててから発動
}

void BattleScene::RunTurnCycle()
{
    int n = RelicManager::SumValue("turnCycle");
    if (n <= 0) return;

    if (m_holdForTutorial) { m_turnCyclePending = true; return; }   // 説明中は出さない（閉じたら発動）

    // 追加で n 枚引く（7→8枚）
    for (int i = 0; i < n; i++)
    {
        std::string id = m_deck.DrawCard();
        if (!id.empty()) { m_hand.AddCard(id); m_battleUI->StartDrawCardEffect(id); }
    }

    // n 枚を「選んで捨てる」既存UIを起動（8→7枚、選んだカードのonDiscardが発動）
    m_discardSelectCount = min(n, (int)m_hand.GetCards().size());
}

void BattleScene::OnTutorialClosed()
{
    if (!m_holdForTutorial) return;
    m_holdForTutorial = false;
    if (m_turnCyclePending) { m_turnCyclePending = false; RunTurnCycle(); }
}

void BattleScene::StartDrawSequence(int count)
{
    m_drawSeqRemaining = count;
    m_drawSeqTimer = 0.0f;   // 最初の1枚はすぐ
    m_reshuffleFxTimer = 0.0f;
}

void BattleScene::OnDrawSequenceComplete()
{
    // 刃の心得：ナイフ生成（ドロー完了後に手札へ）
    int kg = m_player->GetBuffManager().GetBuffValue(BuffType::KnifeGen);
    for (int i = 0; i < kg; i++)
    {
        m_hand.AddCard("ATK_knife");
        m_battleUI->StartDrawCardEffect("ATK_knife");
    }
    // 灰の王冠（代償）：引き終わったら、手札をランダムに n 枚燃やす（燃やすと得をする物もここで発動）
    if (int n = RelicManager::SumValue("turnBurn"); n > 0) BurnRandomCards(n);
    RunTurnCycle();
}

bool BattleScene::CellInCardRange(const CardData& d, int c, int r) const
{
    if (c < 0 || r < 0) return false;
    if (CanPickOwnCell(d) && c == m_playerCol && r == m_playerRow) return true;   // 足元も選べる
    int rng = d.range;
    if (m_player->GetBuffManager().HasBuff(BuffType::Reposition))
        rng += m_player->GetBuffManager().GetBuffValue(BuffType::Reposition);
    int adx = 0, ady = 0;
    if (d.rangeType == RangeType::Cone)   // 扇は指したマスの方向へ向ける
        RangeShape::CardinalAim(m_playerCol, m_playerRow, c, r, adx, ady);
    return RangeShape::Contains(m_playerCol, m_playerRow, c, r, d.rangeType, rng, 0, adx, ady);
}

int BattleScene::EnemyIndexAt(int c, int r) const
{
    if (c < 0 || r < 0) return -1;
    for (int i = 0; i < (int)m_enemies.size(); i++)
        for (auto& cell : m_enemies[i]->OccupiedCells())
            if (cell.first == c && cell.second == r) return i;
    return -1;
}

int BattleScene::AttackTargetEnemy() const
{
    if (m_selectedCardIndex < 0 || m_selectedCardIndex >= (int)m_hand.GetCards().size()) return -1;
    CardData d = *m_hand.GetCards()[m_selectedCardIndex]->GetData();
    ApplyKnifeThrow(d, m_player);
    if (d.type != CardType::Attack) return -1;

    auto inRange = [&](const Enemy* e) {
        for (auto& [c, r] : e->OccupiedCells()) if (CellInCardRange(d, c, r)) return true;
        return false;
        };
    // 射程内の敵が1体だけなら自動ターゲット（使用時と同じ判定）
    int sole = -1, count = 0;
    for (int i = 0; i < (int)m_enemies.size(); i++)
        if (m_enemies[i]->GetHp() > 0 && inRange(m_enemies[i])) { sole = i; count++; }
    bool autoAttack = (count == 1);

    bool hoverInRange = CellInCardRange(d, m_hoveredCell.first, m_hoveredCell.second);
    int hoverEnemy = EnemyIndexAt(m_hoveredCell.first, m_hoveredCell.second);

    // 射程内が1体だけなら、空きマスを指していてもその敵（突進先・扇の向きなど指したマスに意味があるカードは除く）
    if (autoAttack && hoverEnemy < 0 && !AimsFreely(d)) return sole;

    if (m_hoveredCell.first >= 0 && (hoverInRange || !autoAttack))
        return hoverInRange ? hoverEnemy : -1;   // 指したマスの敵（射程外・空きマス＝当たらない）
    return autoAttack ? sole : -1;
}

// 優先：攻撃の対象予測 > パネルのホバー > マウスの下の敵 > クリック選択
int BattleScene::FocusEnemy() const
{
    int t = AttackTargetEnemy();
    if (t >= 0) return t;
    int hi = m_battleUI ? m_battleUI->GetPanelHoveredEnemy() : -1;
    if (hi >= 0 && hi < (int)m_enemies.size()) return hi;
    if (m_selectedCardIndex < 0 && m_hoveredCardIndex < 0)   // カードを触っていない時だけ
        for (int i = 0; i < (int)m_enemies.size(); i++)
            for (auto& cell : m_enemies[i]->OccupiedCells())
                if (cell == m_hoveredCell) return i;
    return m_selectedEnemyRange;
}

// カメラ操作の自動デモ（初回の2ターン目に1回）を始めるか。手順・動きは BattleCamera
void BattleScene::MaybeStartCamDemo()
{
    auto& pd = PlayerDataManager::GetData();
    if (pd.tutorialCamera) { Hint::Hold(false); return; }   // 見終わった：ヒントを出してよい
    Hint::Hold(true);                                        // まだ見ていない：デモを先に見せるため、新しいヒントは待たせる
    if (m_camera.DemoActive() || m_camera.Homing()) return;
    // 始める条件：自分の番（1ターン目から）、ターン表示が消えて、カードを持っていない、出ているヒントが閉じた
    if (m_battleResult != BattleResult::None || !m_turnManager.IsPlayerTurn()
        || TurnBanner::IsActive() || m_selectedCardIndex >= 0 || Hint::Current()) return;
    pd.tutorialCamera = true;
    pd.seenHints.insert("camera");   // 同じことをヒントでもう一度言わない
    PlayerDataManager::Save();
    m_camera.StartDemo();
}

bool BattleScene::CamDemoCaption(const wchar_t*& text, const char*& icon, float& alpha) const
{
    return m_camera.DemoCaption(text, icon, alpha);
}

// ===== 時間を置いて1つずつ進める処理（TimedQueue の1件分）。戻り値＝次までの秒数（負なら残りを捨てて終わり） =====

// カードを使った後の順番待ちを始める（カードを使う経路が2つあるので共通に）
void BattleScene::StartCardSequences(const CardExecutor::ExecuteResult& r)
{
    if (r.startChainDetonate)
        m_chain.Start({ ChainWave{ { { r.chainCol, r.chainRow } }, r.chainFull } });
    if (r.startSeqDetonate)
    {
        std::vector<TrapFire> fires;
        for (auto& [c, rr] : r.seqDetonateCells) fires.push_back({ c, rr, r.seqFull });
        m_detonate.Start(std::move(fires));   // すぐ1個目
    }
    if (r.cardUsed && r.multiHitRemain > 0)
        m_playerHits.Start(std::vector<PlayerHit>(r.multiHitRemain,
            PlayerHit{ r.multiHitTargets, r.multiHitDamage, r.multiHitOnHit, r.multiHitOnHit2 }), MULTI_HIT_INTERVAL);
}

// 自分の連撃の追撃1発
float BattleScene::StepPlayerHit(PlayerHit& h)
{
    bool anyAlive = false;
    for (auto t : h.targets)
    {
        bool alive = false;
        for (auto e : m_enemies) if (e == t && e->GetHp() > 0) alive = true;   // もう消えた敵には触らない
        if (!alive) continue;
        t->ReuseHitPart(); t->TakeDamage(h.damage); t->StartJump(0.5f, 0.2f); anyAlive = true;
        CardEffect::ApplyOnHitEffect(h.onHit, t->GetBuffManager());    // 命中時の効果もヒットごとに（弱体1×2回＝弱体2）
        CardEffect::ApplyOnHitEffect(h.onHit2, t->GetBuffManager());
    }
    return anyAlive ? MULTI_HIT_INTERVAL : -1.0f;   // 全員倒れたら残りは打たない
}

// 敵の多段攻撃の1発
float BattleScene::StepEnemyHit(EnemyHit& h)
{
    int hpBefore = m_player->GetHp(), blockBefore = m_player->GetBlock();
    XMFLOAT3 from = h.enemy ? XMFLOAT3(h.enemy->worldX, 0.0f, h.enemy->worldZ) : XMFLOAT3(m_player->worldX, 0.0f, m_player->worldZ + 1.0f);
    m_player->TakeDamage(h.damage, DamageFeel::Hit, &from);   // 1発（数字はTakeDamage内で出る）。盾は攻撃してきた敵の方へ
    if (!h.vfx.empty()) PlayEnemyHitFx(h.vfx);
    bool fullyBlocked = (m_player->GetHp() == hpBefore);
    int blocked = blockBefore - m_player->GetBlock();   // ブロックで防いだ量
    if (h.enemy && h.enemy->GetHp() > 0)
    {
        int th = m_player->GetBuffManager().GetBuffValue(BuffType::Thorns);
        if (th > 0) h.enemy->TakeDamage(th);
        int rip = m_player->GetBuffManager().GetBuffValue(BuffType::Riposte);
        if (blocked > 0 && rip > 0) h.enemy->TakeDamage(blocked * rip / 100);   // 受け返し：防いだダメージの rip% を返す
    }
    // 最後の1発の後に状態異常（単発と同じく、完全に防いだら付かない。ブロックは前の発から減るので＝1発でも通ったら付く）
    if (h.last && h.enemy)
    {
        if (!fullyBlocked)
            for (auto& b : h.enemy->PendingPlayerBuffs())
                m_player->GetBuffManager().AddBuff(b);
        h.enemy->PendingPlayerBuffs().clear();
    }
    return MULTI_HIT_INTERVAL;
}

// 大蛇のとぐろ：通ったマスの罠を1つ
float BattleScene::StepCoilTrap(CoilTrap& t)
{
    auto& cell = m_gridMap->GetCell(t.c, t.r);
    if (cell.tileEffect.active && t.enemy->GetHp() > 0)
        CardExecutor::TriggerTrap(cell, t.enemy, t.c, t.r, m_gridMap, m_enemies);
    return 0.15f;
}

// レリック：戦闘開始時に全敵へ毒（1回だけ）
float BattleScene::StepStartPoison(int& amount)
{
    for (auto enemy : m_enemies)
    {
        if (enemy->GetHp() <= 0) continue;
        Buff b; b.type = BuffType::Poison;
        b.value = amount + RelicManager::SumValue("poisonAdd");
        b.duration = b.value;
        b.name = BuffInfo::Get(BuffType::Poison).name; b.description = L"";
        enemy->GetBuffManager().AddBuff(b);
        float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
        EffectManager::Play("poison_apply", wx, 0.5f, wz);
    }
    return -1.0f;
}

// 連鎖起爆：今の波を起爆し、隣の罠を次の波に
float BattleScene::StepChainWave(ChainWave& w)
{
    ChainWave next{ {}, w.full };
    for (auto& [c, r] : w.cells)
    {
        auto& cell = m_gridMap->GetCell(c, r);
        if (!cell.tileEffect.active || cell.tileEffect.enemyOwned) continue;   // 敵床は除外
        CardExecutor::DetonateTrap(cell, c, r, m_gridMap, m_enemies, w.full, false);
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
            {
                if (dc == 0 && dr == 0) continue;
                int nc = c + dc, nr = r + dr;
                if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
                auto& ncell = m_gridMap->GetCell(nc, nr);
                if (ncell.tileEffect.active && !ncell.tileEffect.enemyOwned)   // 敵床に連鎖しない
                    next.cells.push_back({ nc, nr });
            }
    }
    if (!next.cells.empty()) m_chain.Push(next);
    ScreenShake::Add(0.3f);
    ProcessDeadEnemies();
    return 0.12f;   // 波の間隔（大きいほどゆっくり連鎖）
}

// 一斉起爆・導火線：罠を1つ
float BattleScene::StepDetonate(TrapFire& f)
{
    auto& cell = m_gridMap->GetCell(f.c, f.r);
    if (cell.tileEffect.active)
    {
        CardExecutor::DetonateTrap(cell, f.c, f.r, m_gridMap, m_enemies, f.full, false);
        ScreenShake::Add(0.2f);
        ProcessDeadEnemies();
    }
    return 0.08f;   // 1個ずつの間隔（大きいほどゆっくり）
}

// 自分の罠：自分のターンの始め（＝敵のターンが終わった所）に育つ（上限3回）。
// 周り1マスに敵がいる間だけ導火線が進み、3たまった次のターン、まだ敵が隣にいたら自分で起爆する（周りは半分）。敵がいない罠はそのまま残る（回収できる）
static constexpr int TRAP_GROW_MAX = 3, TRAP_FUSE = 3;
void BattleScene::UpdateTrapsAtTurnStart()
{
    int maxFuse = 0;   // このターンに進んだ導火線の、一番大きい数（「ピッ」の回数を決める）
    for (int r = 0; r < m_gridMap->GetRows(); r++)
        for (int c = 0; c < m_gridMap->GetCols(); c++)
        {
            auto& t = m_gridMap->GetCell(c, r).tileEffect;
            if (!t.active || t.enemyOwned || t.persistent) continue;   // 自分の罠だけ（床・ハザードは除く）
            const TerrainDef* def = TerrainDataBase::Get(t.id);
            if (!def || def->growPct <= 0) continue;
            if (t.grown < TRAP_GROW_MAX) { t.value += (std::max)(1, (t.value * def->growPct + 50) / 100); t.grown++; }
            bool nearEnemy = false;   // 周り1マスに敵
            for (int dr = -1; dr <= 1 && !nearEnemy; dr++)
                for (int dc = -1; dc <= 1 && !nearEnemy; dc++)
                {
                    int nc = c + dc, nr = r + dr;
                    if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
                    int ei = EnemyIndexAt(nc, nr);   // 大きい敵・蛇の体も含む
                    nearEnemy = (ei >= 0 && m_enemies[ei]->GetHp() > 0);
                }
            if (nearEnemy)
            {
                if (t.fuse < TRAP_FUSE)
                {
                    t.fuse++;   // 導火線が進む（3になってもすぐには爆発しない＝3の間に敵に踏ませることもできる）
                    maxFuse = (std::max)(maxFuse, t.fuse);
                    // 進んだのが分かるように、罠の上に数を出す（3は「次のターンに爆発」）。初めての時はヒントで仕組みを説明
                    bool last = (t.fuse >= TRAP_FUSE);
                    FloatingTextManager::Spawn(CellWX(c), 0.6f, CellWZ(r),
                        last ? std::wstring(L"次のターンに爆発！") : L"導火線 " + std::to_wstring(t.fuse) + L"/3",
                        last ? XMFLOAT4(1.0f, 0.3f, 0.2f, 1.0f) : XMFLOAT4(1.0f, 0.6f, 0.2f, 1.0f), 24.0f);
                    Hint::Request("fuse");
                }
                else if (!m_detonate.Any([&](const TrapFire& f) { return f.c == c && f.r == r; }))
                    m_detonate.Push({ c, r, false });   // 3たまった次のターン、まだ敵が隣にいたら自分で起爆（起爆カードと同じ順番待ち。周りは半分）
            }
        }
    if (!m_detonate.Empty()) m_detonate.Delay(0.3f);   // 少し間をおいて

    // 導火線が進んだら、点滅に合わせて「ピッ」を数回だけ（低HPの鼓動と同じく、鳴り続けて考える邪魔をしないように）
    if (maxFuse > 0)
    {
        static const int BEEPS[TRAP_FUSE + 1] = { 0, 2, 3, 4 };   // 導火線1は点滅が遅いので2回、2は3回、3は4回
        m_fuseBeep.period = 1.0f / (float)(1 << (maxFuse - 1));   // 点滅と同じ間隔（BattleSceneDraw）
        m_fuseBeep.left = BEEPS[maxFuse];
        m_fuseBeep.next = ceilf(m_highlightTimer / m_fuseBeep.period) * m_fuseBeep.period;   // 次に光る瞬間にそろえる
    }
}

// バフ・デバフが付いた／増えた時の演出。増やす時は1行。ここに無いもの（表示用など）は何も出さない
//   fx     ＝体の所の演出（プレイヤーも敵も。nullptr なら無し）
//   screen ＝画面のふちのオーラ（プレイヤーだけ。バフは左右から立ちのぼる・デバフは上からアーチ状に落ちる）
//   popup  ＝頭の上にアイコンを出すか（聖地のように、歩くたびに付いたり消えたりするものは false）
static const struct { BuffType type; const char* fx; const char* screen; bool popup = true; } BUFF_GAIN_FX[] = {
    // 攻撃が上がる（ふち：赤）
    { BuffType::AttackUp, "buff_attack", "aura_buff_attack" },     { BuffType::AttackUpTurn, "buff_attack", "aura_buff_attack" },
    { BuffType::AttackGrowth, "buff_attack", "aura_buff_attack" }, { BuffType::Frenzy, "buff_attack", "aura_buff_attack" },
    { BuffType::LastStand, "buff_attack", "aura_buff_attack" },    { BuffType::DeepStand, "buff_attack", "aura_buff_attack" },
    { BuffType::KnifePower, "buff_attack", "aura_buff_attack" },   { BuffType::HolyGround, "buff_attack", nullptr, false },
    { BuffType::Charge, "buff_attack", "aura_buff_attack" },       { BuffType::TrapPower, "buff_attack", "aura_buff_attack" },
    { BuffType::Berserker, "buff_attack", "aura_buff_attack" },
    // 守りが上がる（ふち：青）
    { BuffType::DefenseUp, "buff_defense", "aura_buff_defense" },  { BuffType::Barricade, "buff_defense", "aura_buff_defense" },
    { BuffType::Thorns, "buff_defense", "aura_buff_defense" },     { BuffType::Momentum, "buff_defense", "aura_buff_defense" },
    { BuffType::Riposte, "buff_defense", "aura_buff_defense" },    { BuffType::CardBlock, "buff_defense", "aura_buff_defense" },
    { BuffType::Sanctuary, "buff_defense", "aura_buff_defense" },  { BuffType::GuardianVow, "buff_defense", "aura_buff_defense" },
    { BuffType::Invincible, "buff_defense", "aura_buff_defense" }, { BuffType::HalfCarry, "buff_defense", "aura_buff_defense" },
    { BuffType::StandFirm, "buff_defense", "aura_buff_defense" },  { BuffType::BurnGuard, "buff_defense", "aura_buff_defense" },
    // そのほかの強化（ふち：金）
    { BuffType::RangeUp, "buff_up", "aura_buff_up" },      { BuffType::MoveUp, "buff_up", "aura_buff_up" },
    { BuffType::HitAndRun, "buff_up", "aura_buff_up" },    { BuffType::Reposition, "buff_up", "aura_buff_up" },
    { BuffType::NoxiousFumes, "buff_up", "aura_buff_up" }, { BuffType::ToxicRhythm, "buff_up", "aura_buff_up" },
    { BuffType::KnifeThrow, "buff_up", "aura_buff_up" },   { BuffType::KnifeGen, "buff_up", "aura_buff_up" },
    { BuffType::EmberTrail, "buff_up", "aura_buff_up" },   { BuffType::DrawPerTurn, "buff_up", "aura_buff_up" },
    { BuffType::Cathedral, "buff_up", "aura_buff_up" },    { BuffType::BloodThirst, "buff_up", "aura_buff_up" },
    { BuffType::BloodFlash, "buff_up", "aura_buff_up" },   { BuffType::BurnStrike, "buff_up", "aura_buff_up" },
    // デバフ（体の所の演出は、付けた側のカード・攻撃が出しているので無し）
    { BuffType::Weak, nullptr, "aura_debuff_weak" },       { BuffType::AttackDown, nullptr, "aura_debuff_weak" },
    { BuffType::Vulnerable, nullptr, "aura_debuff_vuln" }, { BuffType::Frail, nullptr, "aura_debuff_vuln" },
    { BuffType::DefenseDown, nullptr, "aura_debuff_vuln" },
    { BuffType::Root, nullptr, "aura_debuff_bind" },       { BuffType::Slow, nullptr, "aura_debuff_bind" },
    { BuffType::Poison, nullptr, "aura_debuff_poison" },   { BuffType::Burn, nullptr, "aura_debuff_burn" },
};

// 毎フレーム、前のフレームのバフの値と比べて、付いた／増えたものにエフェクト（どこで付いても拾える）
void BattleScene::UpdateBuffGainFx()
{
    std::unordered_map<const void*, std::map<int, BuffSnap>> next;
    // y＝体の真ん中（体の所の演出）、headY＝頭の上（アイコン）。画面のふちはプレイヤーだけ
    auto check = [&](const void* key, const BuffManager& bm, float x, float y, float z, float headY, bool isPlayer) {
        std::map<int, BuffSnap> cur;
        for (auto& b : bm.GetBuffs())
        {
            BuffSnap& s = cur[(int)b.type];
            s.value += (b.value > 0) ? b.value : 1;           // 値の無いバフも「付いた」で拾う
            if (!b.isPermanent()) s.turns += b.duration;      // 重ねがけで残りターンだけ延びた時も拾う
        }
        auto get = [](const std::map<int, BuffSnap>& m, BuffType t) { auto f = m.find((int)t); return f == m.end() ? BuffSnap{} : f->second; };
        auto it = m_buffSnap.find(key);
        if (it != m_buffSnap.end())   // 初めて見た時（戦闘開始・召喚直後）は出さない
        {
            std::vector<const char*> played;   // 同じフレームに同じ演出は1回
            auto playOnce = [&](const char* id, float px, float py, float pz) {
                if (!id || std::find(played.begin(), played.end(), id) != played.end()) return;
                EffectManager::Play(id, px, py, pz);
                played.push_back(id);
            };
            for (auto& e : BUFF_GAIN_FX)
            {
                BuffSnap now = get(cur, e.type), before = get(it->second, e.type);
                bool more = now.value > before.value;         // 値が増えた（毒・攻撃UPなど）
                bool longer = now.turns > before.turns;       // 残りターンが延びた（脱力・弱体の重ねがけなど）
                if (!more && !longer) continue;
                playOnce(e.fx, x, y, z);
                if (isPlayer) playOnce(e.screen, 0.0f, 0.0f, 0.0f);   // 画面の演出なので、場所は使わない
                if (e.popup)   // 頭の上にアイコン。値のあるもの（毒・攻撃UPなど）は増えた数も
                    BuffPopupManager::Spawn(e.type, (more && bm.GetBuffValue(e.type) > 0) ? now.value - before.value : 0, x, headY, z);
            }
        }
        next[key] = std::move(cur);
    };
    check(m_player, m_player->GetBuffManager(), m_player->worldX, m_player->worldY + 0.5f, m_player->worldZ,
        m_player->worldY + m_player->height + 0.15f, true);
    for (auto e : m_enemies)
        if (e && e->GetHp() > 0)
            check(e, e->GetBuffManager(), e->worldX, e->worldY + e->height * 0.5f, e->worldZ,
                e->worldY + e->height + 0.15f, false);
    m_buffSnap.swap(next);   // いなくなった敵の分は自然に消える
}

bool BattleScene::SelectedNeedsTarget() const
{
    if (m_selectedCardIndex < 0 || m_selectedCardIndex >= (int)m_hand.GetCards().size()) return false;
    const CardData* sd = m_hand.GetCards()[m_selectedCardIndex]->GetData();
    CardType ct = sd->type;
    return ct == CardType::Attack || ct == CardType::Move || PicksCell(*sd);
}

float BattleScene::HiddenTargetsPanZ(const CardData& d) const
{
    XMMATRIX vp = m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix();
    float handTop = m_screenHeight - CardVisual::PLAY_LINE_OFFSET;   // 手札の上端（使用ラインと同じ線）
    int rng = (d.type == CardType::Move) ? m_player->GetMoveRange(d.range) : d.range;

    // ワールド座標 → 画面のy（下ほど大きい）
    auto screenY = [&](float wx, float wz) {
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(wx, 0.0f, wz, 1.0f), vp));
        return (clip.w > 0.0f) ? (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight : -1.0f;
        };

    // 狙えるマスのうち、画面で一番下に映っているもの
    float lowest = 0.0f, lowX = 0.0f, lowZ = 0.0f;
    for (auto& [c, r] : BattleHighlighter::GetCandidates(m_playerCol, m_playerRow, d.rangeType, rng, 0, -1))
    {
        if (c < 0 || c >= m_gridMap->GetCols() || r < 0 || r >= m_gridMap->GetRows()) continue;   // 盤面外も返るので除く
        float wx = (c - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (r - m_gridMap->GetRows() / 2.0f) * 1.1f;
        float sy = screenY(wx, wz);
        if (sy > lowest) { lowest = sy; lowX = wx; lowZ = wz; }
    }
    const float MARGIN = 30.0f;              // 手札の上端より、これだけ上に出す
    float limit = handTop - MARGIN;
    if (lowest <= limit) return 0.0f;        // 余白込みで隠れていない → 動かさない

    // そのマスの近くで「カメラを奥へ1ずらすと、画面で何px上がるか」を測って、必要な量に直す
    float pxPerUnit = screenY(lowX, lowZ) - screenY(lowX, lowZ - 1.0f);
    if (pxPerUnit <= 1.0f) return 0.0f;
    return (lowest - limit) / pxPerUnit;
}

bool BattleScene::CellToScreen(int col, int row, float& sx, float& sy) const
{
    float wx = (col - m_gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;
    XMFLOAT4 clip;
    XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(wx, 0.0f, wz, 1.0f),
        m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix()));
    if (clip.w <= 0.0f) return false;
    sx = (1.0f + clip.x / clip.w) * 0.5f * m_screenWidth;
    sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
    return true;
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

void BattleScene::FreeLookStep(Input& in, float deltaTime)
{
    // ズーム・パン・初期位置（本番の戦闘と同じ操作）
    m_camera.HandleInput(in, m_gridMap->GetCols(), m_gridMap->GetRows(), m_player->worldX, m_player->worldZ);

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
        int i = BattleUI::EnemyEntryAt(in.GetMousePos(), m_enemies, m_screenWidth);
        if (i >= 0) {
            m_selectedEnemyRange = (m_selectedEnemyRange == i) ? -1 : i;
            m_camera.LookAt(m_enemies[i]->worldX, m_enemies[i]->worldZ);
        }
    }

    m_camera.Apply(m_renderer3D);   // カメラ適用

    // ハイライト（脅威範囲＋当たりマーカー）
    m_highlighter.SetSelectedEnemy(FocusEnemy());
    m_highlighter.ClearPlayerHighlight(m_gridMap);
    m_highlightTimer += deltaTime * 0.5f;
    m_highlighter.UpdateEnemyHighlight(m_enemies, m_gridMap, m_player,
        m_playerCol, m_playerRow, m_highlightTimer, m_decoyCol, m_decoyRow);
    if (m_forceHitmark && !m_enemies.empty()) m_enemies[0]->hitsPlayer = true;   // 学習用に強制表示
}

bool BattleScene::GetHitmarkRect(float& x, float& y, float& w, float& h) const
{
    if (m_enemies.empty() || !m_battleUI) return false;
    return m_battleUI->GetHitmarkRect(m_enemies[0], m_renderer3D, x, y, w, h);
}

bool BattleScene::GetEnemyPanelRect(float& x, float& y, float& w, float& h) const
{
    if (m_enemies.empty()) return false;
    float ex, ey, ew, eh, lx, ly;
    BattleUI::GetEnemyEntryRect(0, m_enemies, m_screenWidth, ex, ey, ew, eh);
    BattleUI::GetEnemyEntryRect((int)m_enemies.size() - 1, m_enemies, m_screenWidth, lx, ly, ew, eh);
    x = ex - 6.0f; y = ey - 6.0f;
    w = ew + 12.0f; h = (ly + eh) - ey + 12.0f;   // 先頭〜末尾の敵まで
    return true;
}

// カウント系レリックを1つ進める（kind＝attack/skill/move/card/burn）。しきい値に達したら effect を発動して0に戻す
void BattleScene::TickCountRelics(const char* kind)
{
    auto& pd = PlayerDataManager::GetData();
    for (auto* r : RelicManager::OwnedByKind(kind))
    {
        if (r->count <= 0) continue;
        int& c = pd.relicCounters[r->id];
        if (++c >= r->count)                 // 進んで、しきい値に達したら
        {
            c = 0;                           // 0に戻す
            if (r->effect == "energy") m_player->AddEnergy(r->value);
            else                       m_player->AddBlock(r->value);   // 既定=ブロック
            RelicManager::NotifyFired(r->id);
        }
    }
}

void BattleScene::OnCardPlayed(const CardData* d)
{
    if (!d) return;
    const char* k = (d->type == CardType::Attack) ? "attack" :
        (d->type == CardType::Skill) ? "skill" :
        (d->type == CardType::Move) ? "move" : "";
    if (k[0]) TickCountRelics(k);   // その種別
    TickCountRelics("card");        // 全カード共通
}

bool BattleScene::IsRelicSpent(const std::string& id) const
{
    auto def = RelicManager::Get(id);
    if (!def) return false;
    const std::string& k = def->kind;
    // 戦闘開始で発動する系（最初のターン）＝戦闘中は常に使用済み
    if (k == "startBlock" || k == "startHeal" || k == "startDraw"
        || k == "startBuffAtk" || k == "startBuffAtkTurn" || k == "startPoison" || k == "startCard") return true;
    // 最初の攻撃
    if (k == "firstAttack") return m_firstAttackDone;
    if (k == "burnStatusEnergy") return m_burnStatusEnergyUsed;   // 煤けた手袋：このターンはもう使った
    if (k == "revive")   // 不死鳥の尾羽：1度使ったら暗く
    {
        const auto& rc = PlayerDataManager::GetData().relicCounters;
        auto it = rc.find(id);
        return it != rc.end() && it->second > 0;
    }
    return false;
}

// 立っているマスの信仰を、プレイヤーのバフとして反映する（毎フレーム）
// バフにしておくと、攻撃の加算がカードの数値表示（強化で緑）とバフ一覧にそのまま出る
void BattleScene::SyncFaithBuffs()
{
    if (!m_player) return;
    m_player->SetExhaustCount(m_deck.GetExhaustPileCount());   // 灰燼撃のダメージ（表示と実際）
    auto& bm = m_player->GetBuffManager();
    bm.RemoveBuff(BuffType::HolyGround);
    bm.RemoveBuff(BuffType::Sanctuary);

    int f = Faith::At(m_gridMap, m_playerCol, m_playerRow);
    m_player->SetFeetFaith(f);
    auto add = [&](BuffType t, int v) {
        Buff b; b.type = t; b.value = v; b.duration = -1;   // 永続（離れたら外す）
        b.name = BuffInfo::Get(t).name; b.description = L"";
        bm.AddBuff(b);
        };
    if (Faith::AttackBonus(f) > 0) add(BuffType::HolyGround, Faith::AttackBonus(f));
    if (Faith::IsSanctuary(f))     add(BuffType::Sanctuary, 1);

    bm.RemoveBuff(BuffType::Mire);                                  // 沼は上にいる間だけ
    const auto& tile = m_gridMap->GetCell(m_playerCol, m_playerRow).tileEffect;
    const TerrainDef* def = tile.active ? TerrainDataBase::Get(tile.id) : nullptr;
    if (def && def->effect == "WhileOn" && !RelicManager::HasKind("floorImmune"))
        add(StringToBuffType(def->buffType), tile.value);
}

// 浄化：手札の呪いを全て廃棄する（ターン終了のエセリアルと同じく、その場で溶かす）
void BattleScene::PurgeHandCurses()
{
    std::vector<int> curses;
    for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
    {
        const CardData* dd = m_hand.GetCards()[i]->GetData();
        if (dd && dd->type == CardType::Curse) curses.push_back(i);
    }
    BurnHandCards(curses);   // 燃やす入り口は1か所（燃える演出・燃やした時の効果も同じ）
}
