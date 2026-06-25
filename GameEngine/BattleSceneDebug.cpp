#ifdef _DEBUG
#include "BattleScene.h"
#include "External/imgui/imgui.h"
#include "EncounterDataBase.h"
#include "EnemyDataBase.h"

void BattleScene::DrawImGui()
{
    ImGui::Begin("Battle Debug");

    // --- エンカウンター選択 ---
    ImGui::Text("Encounter");
    ImGui::SliderInt("Template", &m_debugEncounterIndex, -1, EncounterDataBase::GetCount() - 1);
    ImGui::SliderInt("Rank", &m_debugRank, 1, 3);
    if (ImGui::Button("Reload & Reset"))
    {
        EnemyDataBase::Reload();
        EncounterDataBase::Reload();

        for (auto enemy : m_enemies)
        {
            for (auto& [dc, dr] : enemy->GetGridShape())
                m_gridMap->SetCellType(enemy->gridCol + dc, enemy->gridRow + dr, CellType::Empty);
            delete enemy;
        }
        m_enemies.clear();

        const EncounterData* encounter = (m_debugEncounterIndex < 0)
            ? EncounterDataBase::GetRandom(m_debugRank)
            : EncounterDataBase::GetByIndex(m_debugEncounterIndex);

        if (encounter)
        {
            for (auto& ee : encounter->enemies)
                AddEnemy(ee.col, ee.row, ee.id);
        }
        for (auto enemy : m_enemies)
            enemy->DecideNextAction();
    }

    ImGui::Separator();

    // --- プレイヤー ---
    ImGui::Text("Player");
    int hp = m_player->GetHp();
    int maxHp = m_player->GetMaxHp();
    if (ImGui::SliderInt("Player HP", &hp, 0, maxHp))
        m_player->SetHp(hp);

    int energy = m_player->GetEnergy();
    if (ImGui::SliderInt("Energy", &energy, 0, m_player->GetMaxEnergy()))
        m_player->SetEnergy(energy);

    int block = m_player->GetBlock();
    if (ImGui::SliderInt("Player Block", &block, 0, 50))
        m_player->SetBlock(block);

    ImGui::Separator();

    // --- 敵 ---
    ImGui::Text("Enemies");
    for (int i = 0; i < (int)m_enemies.size(); i++)
    {
        ImGui::PushID(i);
        Enemy* e = m_enemies[i];
        int eHp = e->GetHp();
        char label[64];
        sprintf_s(label, "%s (%d,%d)", e->GetId().c_str(), e->gridCol, e->gridRow);
        if (ImGui::SliderInt(label, &eHp, 0, e->GetMaxHp()))
            e->SetHp(eHp);
        ImGui::PopID();
    }

    ImGui::End();
}
#endif