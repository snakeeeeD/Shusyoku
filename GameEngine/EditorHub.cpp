#include "EditorHub.h"

#ifdef _DEBUG

#include "CardEditor.h"
#include "EncounterEditor.h"
#include "EnemyEditor.h"
#include "External/imgui/imgui.h"

namespace
{
    bool g_hasBattle = false;
    std::vector<EncounterEnemy> g_battle;

    bool g_hasCardReload = false;
    std::string g_addToHand;
}

void EditorHub::DrawMenu()
{
    ImGui::SeparatorText("Editors");
    if (ImGui::Button("Encounter Editor")) EncounterEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Enemy Editor")) EnemyEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Card Editor")) CardEditor::Toggle();
}

void EditorHub::DrawAll()
{
    EncounterEditor::Draw();
    EnemyEditor::Draw();
    CardEditor::Draw();
}

void EditorHub::DrawGameOverlay(SpriteRenderer* sprite, TextRenderer* text)
{
    CardEditor::DrawGameOverlay(sprite, text);
}

void EditorHub::RequestBattle(const std::vector<EncounterEnemy>& enemies)
{
    g_battle = enemies;
    g_hasBattle = true;
}

bool EditorHub::ConsumeBattleRequest(std::vector<EncounterEnemy>& out)
{
    if (!g_hasBattle) return false;
    out = std::move(g_battle);
    g_battle.clear();
    g_hasBattle = false;
    return true;
}

void EditorHub::RequestCardReload(const std::string& addToHand)
{
    g_hasCardReload = true;
    if (!addToHand.empty()) g_addToHand = addToHand;   // 追加の要求は、消費されるまで残す
}

bool EditorHub::ConsumeCardReload(std::string& addToHand)
{
    if (!g_hasCardReload) return false;
    addToHand = g_addToHand;
    g_addToHand.clear();
    g_hasCardReload = false;
    return true;
}

bool EditorHub::IsCardReloadPending() { return g_hasCardReload; }

#else   // Release ではすべて何もしない

void EditorHub::DrawMenu() {}
void EditorHub::DrawAll() {}
void EditorHub::DrawGameOverlay(SpriteRenderer*, TextRenderer*) {}
void EditorHub::RequestBattle(const std::vector<EncounterEnemy>&) {}
bool EditorHub::ConsumeBattleRequest(std::vector<EncounterEnemy>&) { return false; }
void EditorHub::RequestCardReload(const std::string&) {}
bool EditorHub::ConsumeCardReload(std::string&) { return false; }
bool EditorHub::IsCardReloadPending() { return false; }

#endif
