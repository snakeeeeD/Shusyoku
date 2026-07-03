#pragma once
#include "GameObject.h"
#include "BuffManager.h"

class Player : public GameObject
{
public:
    Player();
    void Update(float deltaTime) override;
    void Draw3D(class Renderer3D* renderer) override;

    // ゲッター
    int GetHp() const { return m_hp; }
    int GetMaxHp() const { return m_maxHp; }
    int GetEnergy() const { return m_energy; }
    int GetBlock() const { return m_block; }
    int GetMaxEnergy() const { return m_maxEnergy; }
    float GetDisplayHp() const { return m_displayHp; }
    BuffManager& GetBuffManager() { return m_buffManager; }


    // セッター
    void SetHp(int hp) { m_hp = hp; }
    void SetEnergy(int energy) { m_energy = energy; }
    void SetBlock(int block) { m_block = block; }

    void AddBlock(int amount);
    void TakeDamage(int damage);

    void Heal(int amount) { m_hp = min(m_hp + amount, m_maxHp); }
    void AddEnergy(int amount) { m_energy += amount; }

    void RestoreEnergy();          // ターン開始時に回復
    bool UseEnergy(int cost);      // エネルギー消費（足りなければfalse）

    void ResetBlock() { if (!m_buffManager.HasBuff(BuffType::Barricade)) m_block = 0; }

    void UpdateDisplayHp(float deltaTime) {
        float speed = 0.5f;
        if (m_displayHp > (float)GetHp())
            m_displayHp -= speed * deltaTime * (m_displayHp - GetHp());
        if (m_displayHp < (float)GetHp())
            m_displayHp = (float)GetHp();
    }

private:

    int m_hp;
    int m_maxHp;
    int m_energy;
    int m_maxEnergy;
    int m_block;
    float m_BillboardRotation = 0.0;
    float m_displayHp;

    BuffManager m_buffManager;
};