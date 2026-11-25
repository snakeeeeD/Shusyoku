#pragma once
#include "CardData.h"
#include "Player.h"
#include "Enemy.h"
#include "GridMap.h"
#include "Hand.h"
#include "Deck.h"
#include <vector>



class CardExecutor
{
public:
    struct ExecuteResult
    {
        bool success;
        bool cardUsed;
        int  moveDist = 0;                    // このカードで移動したマス数
        std::vector<std::string> drawnCards;
        CardEffectType pendingSelection = CardEffectType::None;
        int pendingDiscard = 0;
        std::vector<Enemy*> multiHitTargets;  // 追撃対象
        int    multiHitRemain = 0;         // 残りヒット数
        int    multiHitDamage = 0;         // 1発の確定ダメージ
        CardEffectData multiHitOnHit, multiHitOnHit2;   // 追撃のヒットごとにも付ける命中時の効果（弱体・毒など）
        bool startChainDetonate = false;
        int  chainCol = 0, chainRow = 0;
        bool chainFull = false;
        bool startSeqDetonate = false;
        std::vector<std::pair<int, int>> seqDetonateCells;
        bool seqFull = false;
        bool placeDecoy = false;
        int  decoyCol = 0, decoyRow = 0;
        bool purgeHandCurses = false;   // 浄化：手札の呪いを廃棄してほしい（演出があるのでシーン側で行う）
        int  pendingBurn = 0;           // 手札を燃やす枚数（燃やす演出・選ぶ画面はシーン側）
        bool burnRandom = false;        // true ならランダムに燃やす（false なら選ぶ画面を開く）
        Enemy* hitTarget = nullptr;     // 単体攻撃で狙った敵（燃やした後の追撃に使う）
        std::vector<std::string> addToHand;   // ランダムに出したカード（手札へ入れるのは、真ん中で見せる演出があるシーン側）
        int  addCost = -1;                    // そのカードのこのターンのコスト（-1＝いつも）
    };

    struct MovePreview 
    {
        bool immovable = false;
        int destCol, destRow;      // 移動先
        bool hitsWall = false;     // 壁or敵にぶつかるか

        // Pull: immovable時のプレイヤー移動先
        int playerDestCol = -1, playerDestRow = -1;
        // Knockback: 後ろの敵に衝突
        bool hasCollision = false;
        int collisionCol = -1, collisionRow = -1;
    };

    ExecuteResult Execute(
        const CardData& data,
        const std::string& cardId,
        int targetCol, int targetRow,
        Player* player,
        std::vector<Enemy*>& enemies,
        GridMap* gridMap,
        int playerCol, int playerRow,
        Hand& hand, int cardIndex,
        Deck& deck,
        int& outNewPlayerCol,
        int& outNewPlayerRow,
        const std::vector<std::pair<int, int>>* explicitPath = nullptr
    );

    static MovePreview PreviewKnockback(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies);
    static MovePreview PreviewPull(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies,
        int aimCol, int aimRow);

    // 罠のダメージの上乗せ（レリック「罠の達人」＋プレイヤーの罠術バフ）。戦闘のプレイヤーは BattleScene が設定
    static void SetBattlePlayer(const Player* p) { s_battlePlayer = p; }
    static int  TrapDamageBonus();
    static inline const Player* s_battlePlayer = nullptr;

    static void TriggerTrap(Cell& cell, Enemy* enemy, int col, int row,
        GridMap* gridMap, std::vector<Enemy*>& enemies);

    static void TriggerTerrain(Cell& cell, Player* player);

    static bool DetonateTrap(Cell& cell, int col, int row,
        GridMap* gridMap, std::vector<Enemy*>& enemies,
        bool fullPower = false, bool chain = false);

private:
    // どのファイルからも使う小さな関数（CardExecutor.cpp）
    static int  EffectiveHits(const CardData& data, const Player* player);                 // 連撃の回数（聖域・レリック込み）
    static void PlayOnHitVfx(const CardEffectData& e, Enemy* enemy, GridMap* gridMap);     // 命中時のデバフの演出
    static void ApplyOnHitFaith(const CardEffectData& e, Enemy* enemy, GridMap* gridMap);  // 命中時の「刻む」
    static void ApplyAllEnemyEffect(const CardData& data, std::vector<Enemy*>& enemies, GridMap* gridMap);   // 全体効果

    // 1回のカード使用（Execute の引数と、途中の値）。使い方ごとの処理はこのメンバー関数で、機能ごとのファイルにある
    // 戻り値が bool の関数：true＝使えたので後片付けへ、false＝ここで終わり（使えなかった／その場で使い切った）
    struct Play
    {
        const CardData& data;
        const std::string& cardId;
        int targetCol, targetRow;
        Player* player;
        std::vector<Enemy*>& enemies;
        GridMap* gridMap;
        int playerCol, playerRow;
        Hand& hand;
        int cardIndex;
        Deck& deck;
        int& outNewPlayerCol;
        int& outNewPlayerRow;
        const std::vector<std::pair<int, int>>* explicitPath;
        int payCost = 0;
        int pendingDiscard = 0;
        ExecuteResult result{ false, false };

        // 攻撃（CardExecutorAttack.cpp）
        bool Attack();
        bool AnyEnemyInRange() const;
        bool AttackAllOnly();
        bool AttackArea();
        bool AttackPierce();
        bool DashToEmptyCell();
        bool AttackSingle(Enemy* target);
        void DashToTarget(Enemy* target, int range);
        int  ApplySpecialAttack(Enemy* target);
        void ApplyHitEffects(Enemy* target);
        // 移動（CardExecutorMove.cpp）
        bool Move();
        bool FindMovePath(std::vector<std::pair<int, int>>& path);
        void ApplyMoveExtras();
        // スキル（CardExecutorSkill.cpp）
        bool Skill();
        bool CanUseSkill() const;
        bool InRange(int c, int r) const;
        bool ApplySkillEffect();
        void ApplySub(const CardEffectData& sub);
        // 罠（CardExecutorTrap.cpp）
        void SkillPlaceTrap();  void SkillPlaceTrapArea();  void SkillDetonate();  void SkillRecallTraps();
        void SkillClearFloors();  void SkillDetonateAt();  void SkillDetonateChain();
        // 信仰（CardExecutorFaith.cpp）
        void SkillEngrave();  void SkillEngraveArea();  void SkillJudgement();  void SkillMiracle();  void SkillPurify();
        // パワー・後片付け（CardExecutorFinish.cpp）・演出（CardExecutorVfx.cpp）
        bool Power();
        void Finish();
        void ApplySelfDamage();
        void PlayCardVfx();
    };
    static std::vector<Enemy*> GetEnemiesInRange(
        const CardData& data,
        int playerCol, int playerRow,
        std::vector<Enemy*>& enemies, int aimDx, int aimDy);

    static Enemy* GetEnemyAt(int col, int row, std::vector<Enemy*>& enemies);

    static void ApplyKnockback(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies);
    static void ApplyPull(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies,
        int& outNewPlayerCol, int& outNewPlayerRow);
};