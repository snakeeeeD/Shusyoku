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
#include "PileFx.h"
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

// カードを使う入力（離して使う・狙う先・使った後）
// （BattleScene のメンバー。BattleSceneInput.cpp から機能ごとに分けたファイル）

// 左ボタンを離したら使う（使用ラインより上で離した時）。離したら移動の経路は消す
void BattleScene::HandleReleaseToPlay()
{
    const auto& cards = m_hand.GetCards();
    // 全カード: グリッドエリア上で左ボタンを離したら使用
    if (m_input.GetMouseButtonRelease(0) && m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)cards.size())
    {
        POINT releasePos = m_input.GetMousePos();
        if (releasePos.y < m_screenHeight - CardVisual::PLAY_LINE_OFFSET)   // 使用ライン（見た目の線と同じ定数）
        {
            const Card* card = cards[m_selectedCardIndex];
            CardData dataCopy = *card->GetData();
            ApplyKnifeThrow(dataCopy, m_player);
            CardType ct = dataCopy.type;

            const std::vector<std::pair<int, int>>* usePath = nullptr;
            bool moveCanceled = false;
            if (ct == CardType::Move) moveCanceled = ReleaseMovePath(usePath);

            int targetCol = m_playerCol;
            int targetRow = m_playerRow;
            bool canTry = !moveCanceled;

            if (canTry) canTry = FindReleaseTarget(dataCopy, releasePos, usePath != nullptr, targetCol, targetRow);

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

// 移動カードを離した時：引いた経路を使えるか（踏ん張り中・右クリックで取り消した直後・指すマスが変わった直後は使わない）。使わないなら true
bool BattleScene::ReleaseMovePath(const std::vector<std::pair<int, int>>*& usePath)
{
    bool moveCanceled = false;
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
    return moveCanceled;
}

// 離したマスから狙う先を決める（射程内の敵が1体だけなら自動で狙う・離したマスに止まっていた時だけ使う）。使えないなら false
bool BattleScene::FindReleaseTarget(const CardData& dataCopy, POINT releasePos, bool hasPath, int& targetCol, int& targetRow)
{
    CardType ct = dataCopy.type;
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

    bool needCell = PicksCell(*m_hand.GetCards()[m_selectedCardIndex]->GetData());
    if (!((ct == CardType::Attack || ct == CardType::Move || needCell) && !hasPath)) return true;   // マスを選ばないカード
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
            else return false;
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
                return false;
        }
    }
    return true;
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

    int faBonus = ApplyFirstAttackBonus(dataPtr);

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

    RemoveFirstAttackBonus(faBonus, execResult.cardUsed);

    if (execResult.cardUsed) AfterCardUsed(execResult, dataPtr, dataCopy, newPlayerCol, newPlayerRow, usePath);
    return execResult.cardUsed;
}

// 各戦闘の最初の攻撃：レリックの分だけ、このカードの間だけ攻撃UP（上乗せした量を返す）
int BattleScene::ApplyFirstAttackBonus(const CardData* dataPtr)
{
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
    return faBonus;
}

// 最初の攻撃の上乗せを戻す（使えたら、もう最初の攻撃ではない）
void BattleScene::RemoveFirstAttackBonus(int faBonus, bool used)
{
    if (faBonus <= 0) return;
    Buff fb; fb.type = BuffType::AttackUp; fb.value = -faBonus; fb.duration = 1; fb.name = L""; fb.description = L""; m_player->GetBuffManager().AddBuff(fb);
    if (m_player->GetBuffManager().GetBuffValue(BuffType::AttackUp) == 0)
        m_player->GetBuffManager().RemoveBuff(BuffType::AttackUp);   // 0残り掃除
    if (used) m_firstAttackDone = true;
}

// カードを使えた後：音・刃の守り・動く・引く・使った演出・選ぶ画面・浄化・レリック・廃棄・手札に加える
void BattleScene::AfterCardUsed(const CardExecutor::ExecuteResult& execResult, const CardData* dataPtr, const CardData& dataCopy,
    int newPlayerCol, int newPlayerRow, const std::vector<std::pair<int, int>>* usePath)
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
    if (execResult.pendingSelection != CardEffectType::None) OpenPendingSelection(execResult.pendingSelection);

    m_camera.SetAimKeep(dataPtr->type == CardType::Move);   // 移動したらカメラは寄せた位置のまま
    m_battleUI->OnCardRemoved(m_selectedCardIndex);
    m_selectedCardIndex = -1;
    if (execResult.purgeHandCurses) PurgeHandCurses();   // 浄化：使ったカードのアニメを外してから（先にやると選択が-1になり、アニメの並びがずれる）
    OnCardPlayed(dataPtr);
    if (dataCopy.exhaust) OnCardBurned(dataPtr);   // 使って廃棄も「廃棄」として数える（炉心・レリックが発動）
    for (int k = 0; k < (int)execResult.addToHand.size(); k++)   // ランダムに出したカード：使ったカードが真ん中で燃えてから、1枚ずつ見せて手札へ
        AddToHand(execResult.addToHand[k], execResult.addCost, BattleUI::BurnShowTime() + 0.15f + k * 0.25f);

    OpenHandChoices(execResult, dataPtr);   // 選んで捨てる／廃棄する
}

// カードの効果で一覧から選ぶ画面を開く（探す＝山札、回収＝捨て札、灰からの再生＝廃棄札。使ったばかりの1枚は出さない）
void BattleScene::OpenPendingSelection(CardEffectType type)
{
    m_cardSelecting = true;
    m_selectingType = type;
    if (m_selectingType == CardEffectType::Search)
        m_showDrawPile = true;
    else if (m_selectingType == CardEffectType::Salvage)
        m_showDiscardPile = true;
    else if (m_selectingType == CardEffectType::Reclaim)   // 灰からの再生：廃棄札の一覧（使ったばかりの1枚は出さない。ほかに無ければ開かない）
    {
        m_reclaimHideIdx = m_deck.GetExhaustPileCount() - 1;   // 使ったカードは廃棄札の一番後ろ
        if (m_deck.GetExhaustPileCount() > 1) m_showExhaustPile = true;
        else { m_cardSelecting = false; m_selectingType = CardEffectType::None; m_reclaimHideIdx = -1; }
    }
}

// カードの効果で手札を捨てる／廃棄する（ランダムならすぐ、選ぶなら画面を開く。廃棄した後に、このカードの「廃棄した後の効果」）
void BattleScene::OpenHandChoices(const CardExecutor::ExecuteResult& execResult, const CardData* dataPtr)
{
    if (execResult.pendingDiscard > 0)
        OpenDiscardSelect(execResult.pendingDiscard);
    if (execResult.pendingBurn > 0)   // 手札を燃やす：ランダムならすぐ、選ぶなら画面を開く。燃やした後に、このカードの「燃やした後の効果」
    {
        m_burnSource = dataPtr;
        m_burnTarget = execResult.hitTarget;
        if (execResult.burnRandom) ApplyBurnEffect(m_burnSource, BurnRandomCards(execResult.pendingBurn), m_burnTarget);
        else                       OpenBurnSelect(execResult.pendingBurn);
    }
}
