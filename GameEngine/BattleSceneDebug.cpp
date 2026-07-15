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
            enemy->DecideNextAction(m_playerCol, m_playerRow, m_turnCount);
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

    ImGui::Separator();

    // --- カード追加 ---
    ImGui::Text("Add Card");
    {
        static int selectedCard = 0;
        static std::vector<std::string> cardIds;

        if (cardIds.empty())
        {
            for (auto& [id, data] : CardDataBase::GetAll())
                cardIds.push_back(id);
            std::sort(cardIds.begin(), cardIds.end());
        }

        if (!cardIds.empty())
        {
            if (ImGui::BeginCombo("Card", cardIds[selectedCard].c_str()))
            {
                for (int i = 0; i < (int)cardIds.size(); i++)
                {
                    bool isSelected = (i == selectedCard);
                    if (ImGui::Selectable(cardIds[i].c_str(), isSelected))
                        selectedCard = i;
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button("Add to Hand"))
                m_hand.AddCard(cardIds[selectedCard]);
        }
}

    ImGui::Separator();

    // --- バフ/デバフ付与 ---
    ImGui::Text("Buff / Debuff");
    {
        static int targetType = 0;
        static int targetEnemy = 0;
        static int buffIndex = 0;
        static int buffValue = 3;
        static int buffDuration = 3;

        const char* targets[] = { "Player", "Enemy" };
        ImGui::Combo("Target", &targetType, targets, 2);

        if (targetType == 1 && !m_enemies.empty())
        {
            ImGui::SliderInt("Enemy Index", &targetEnemy, 0, (int)m_enemies.size() - 1);
        }

        const char* buffNames[] = {
        "AttackUp", "DefenseUp", "Barricade", "Thorns", "Momentum",
        "MoveUp", "Charge", "HitAndRun", "Reposition",
        "AttackDown", "Weak", "DefenseDown", "Frail", "Vulnerable",
        "Root", "Slow", "Burn", "Poison"
        };
        BuffType buffTypes[] = {
            BuffType::AttackUp, BuffType::DefenseUp, BuffType::Barricade, BuffType::Thorns, BuffType::Momentum,
            BuffType::MoveUp, BuffType::Charge, BuffType::HitAndRun, BuffType::Reposition,
            BuffType::AttackDown, BuffType::Weak, BuffType::DefenseDown, BuffType::Frail, BuffType::Vulnerable,
            BuffType::Root, BuffType::Slow, BuffType::Burn, BuffType::Poison
        };
        const wchar_t* buffDisplayNames[] = {
            L"AttackUP", L"DefenseUP", L"Barricade", L"Thorns", L"Momentum",
            L"MoveUP", L"Charge", L"HitAndRun", L"Reposition",
            L"AttackDOWN", L"Weak", L"DefenseDOWN", L"Frail", L"Vulnerable",
            L"Root", L"Slow", L"Burn", L"Poison"
        };

        ImGui::Combo("Buff Type", &buffIndex, buffNames, 18);
        ImGui::SliderInt("Value", &buffValue, 1, 20);
        ImGui::SliderInt("Duration", &buffDuration, -1, 10);

        if (ImGui::Button("Apply Buff"))
        {
            Buff buff;
            buff.type = buffTypes[buffIndex];
            buff.value = buffValue;
            buff.duration = buffDuration;
            buff.name = buffDisplayNames[buffIndex];
            buff.description = L"Debug";

            if (targetType == 0)
                m_player->GetBuffManager().AddBuff(buff);
            else if (!m_enemies.empty())
                m_enemies[targetEnemy]->GetBuffManager().AddBuff(buff);
        }
    }

    ImGui::Separator();

    // --- 手札操作 ---
    ImGui::Text("Hand");
    {
        const auto& cards = m_hand.GetCards();
        for (int i = 0; i < (int)cards.size(); i++)
        {
            ImGui::PushID(i + 1000);
            char label[64];
            sprintf_s(label, "Discard: %s", cards[i]->GetId().c_str());
            if (ImGui::Button(label))
            {
                m_deck.DiscardCard(cards[i]->GetId());
                m_hand.RemoveCard(i);
                m_battleUI->OnCardRemoved(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    ImGui::Separator();

    // --- 手札リセット ---
    if (ImGui::Button("Reset Hand"))
    {
        for (auto card : m_hand.GetCards())
            m_deck.DiscardCard(card->GetId());
        m_hand.Clear();
        m_battleUI->ClearCardAnimations();

        for (int i = 0; i < 5; i++)
        {
            std::string id = m_deck.DrawCard();
            if (!id.empty())
                m_hand.AddCard(id);
        }
    }

    ImGui::End();
}
#else
#include "BattleScene.h"
void BattleScene::DrawImGui() {}
#endif