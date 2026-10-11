#include "EditorHub.h"

#ifdef _DEBUG

#include "CardEditor.h"
#include "CraftEditor.h"
#include "EncounterEditor.h"
#include "EnemyEditor.h"
#include "EventEditor.h"
#include "External/imgui/imgui.h"
#include "RefCheck.h"
#include "RelicEditor.h"

namespace
{
    bool g_hasBattle = false;
    std::vector<EncounterEnemy> g_battle;

    bool g_hasCardReload = false;
    std::string g_addToHand;

    std::string g_openEvent;
}

void EditorHub::DrawMenu()
{
    ImGui::SeparatorText("Editors");
    if (ImGui::Button("Encounter Editor")) EncounterEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Enemy Editor")) EnemyEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Card Editor")) CardEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Event Editor")) EventEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Craft Editor")) CraftEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Relic Editor")) RelicEditor::Toggle();
    if (ImGui::Button("Check References")) RefCheck::Toggle();   // 全JSONの参照切れ・重複idを一覧に
}

void EditorHub::DrawAll()
{
    EncounterEditor::Draw();
    EnemyEditor::Draw();
    CardEditor::Draw();
    EventEditor::Draw();
    CraftEditor::Draw();
    RelicEditor::Draw();
    RefCheck::Draw();
}

void EditorHub::DrawGameOverlay(SpriteRenderer* sprite, TextRenderer* text)
{
    CardEditor::DrawGameOverlay(sprite, text);    // 既定は右側
    CraftEditor::DrawGameOverlay(sprite, text);   // 既定は左側
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

void EditorHub::RequestOpenEvent(const std::string& eventId) { g_openEvent = eventId; }

bool EditorHub::ConsumeOpenEvent(std::string& eventId)
{
    if (g_openEvent.empty()) return false;
    eventId = g_openEvent;
    g_openEvent.clear();
    return true;
}

#else   // Release ではすべて何もしない

void EditorHub::DrawMenu() {}
void EditorHub::DrawAll() {}
void EditorHub::DrawGameOverlay(SpriteRenderer*, TextRenderer*) {}
void EditorHub::RequestBattle(const std::vector<EncounterEnemy>&) {}
bool EditorHub::ConsumeBattleRequest(std::vector<EncounterEnemy>&) { return false; }
void EditorHub::RequestCardReload(const std::string&) {}
bool EditorHub::ConsumeCardReload(std::string&) { return false; }
bool EditorHub::IsCardReloadPending() { return false; }
void EditorHub::RequestOpenEvent(const std::string&) {}
bool EditorHub::ConsumeOpenEvent(std::string&) { return false; }

#endif
