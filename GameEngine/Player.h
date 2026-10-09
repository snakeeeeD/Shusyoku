#pragma once
#include "GameObject.h"
#include "BuffManager.h"
#include "DamageFeedback.h"
#include "CardType.h"

class Player : public GameObject
{
public:
    Player();
    void Update(float deltaTime) override;
    void Draw3D(class Renderer3D* renderer) override;

    // ゲッター
    int GetHp() const override { return m_hp; }
    int GetMaxHp() const { return m_maxHp; }
    int GetEnergy() const { return m_energy; }
    int GetBlock() const { return m_block; }
    int GetMaxEnergy() const { return m_maxEnergy; }
    BuffManager& GetBuffManager() { return m_buffManager; }
    const BuffManager& GetBuffManager() const { return m_buffManager; }
    int GetMoveRange(int baseRange) const;   // バフ＋レリック込みの移動範囲

    // セッター
    void SetHp(int hp) { m_hp = hp; }
    void SetMaxHp(int hp) { m_maxHp = hp; }
    void SetEnergy(int energy) { m_energy = energy; }
    void SetBlock(int block) { m_block = block; }

    void AddBlock(int amount);
    void TakeDamage(int damage, DamageFeel feel = DamageFeel::Hit);

    void Heal(int amount);
    void AddEnergy(int amount) { m_energy += amount; }

    void RestoreEnergy();          // ターン開始時に回復
    bool UseEnergy(int cost);      // エネルギー消費（足りなければfalse）

    void ResetBlock() { if (!m_buffManager.HasBuff(BuffType::Barricade)) m_block = 0; }

    void LoseHp(int amount) { m_hp -= amount; if (m_hp < 0) m_hp = 0; }   // 0まで許可＝自傷で死ぬ

    void SetFacing(int dx, int dz) { m_faceDx = dx; m_faceDz = dz; }   // 攻撃時などに明示指定
    enum class Anim { Idle, Attack, Skill, Hurt };
    void StartAttack() { m_anim = Anim::Attack; m_animTime = 0.0f; }   // 攻撃モーション
    void StartSkill() { m_anim = Anim::Skill;  m_animTime = 0.0f; }   // スキル／パワーのモーション
    void StartHurt() { m_anim = Anim::Hurt;   m_animTime = 0.0f; }   // 被弾
    void OnCardUsed(CardType type, int dc, int dr);   // カードが実際に使われた時の動き（dc,dr＝対象への差分）

private:

    int m_hp;
    int m_maxHp;
    int m_energy;
    int m_maxEnergy;
    int m_block;
    float m_BillboardRotation = 0.0;

    BuffManager m_buffManager;

    int   m_faceDx = 0, m_faceDz = 1;        // 既定は手前向き(正面)
    float m_prevX = 0.0f, m_prevZ = 0.0f;    // 移動差分の判定用
    float m_atkTime = 0.0f;                  // 攻撃モーションの経過時間
    Anim  m_anim = Anim::Idle;      // 再生中のアニメ
    float m_animTime = 0.0f;        // 攻撃／スキルの経過時間
    float m_idleTime = 0.0f;        // アイドルの経過時間（ループ）
};