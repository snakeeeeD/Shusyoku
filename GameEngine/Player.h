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

    void AddBlock(int amount);
    void TakeDamage(int damage);

    void RestoreEnergy();          // ターン開始時に回復
    bool UseEnergy(int cost);      // エネルギー消費（足りなければfalse）
    int GetMaxEnergy() const { return m_maxEnergy; }

    void ResetBlock() { m_block = 0; }

    BuffManager& GetBuffManager() { return m_buffManager; }

private:

    int m_hp;
    int m_maxHp;
    int m_energy;
    int m_maxEnergy;
    int m_block;
    float m_BillboardRotation = 0.0;

    BuffManager m_buffManager;
};