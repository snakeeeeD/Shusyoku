#pragma once
#include "EncounterData.h"
#include <string>
#include <vector>

class SpriteRenderer;
class TextRenderer;

// エディタツールの入口（Debugビルド専用）
// - ゲーム本体から呼ぶのはこのファイルの関数だけ
// - エディタを増やしても、ゲーム本体のコードは変えなくてよい
namespace EditorHub
{
    void DrawMenu();     // 各エディタを開くボタン（Debug ウィンドウの Global タブの中で呼ぶ）
    void DrawAll();      // 開いているエディタのウィンドウを描く（ImGui のフレーム内で毎フレーム呼ぶ）

    // ゲーム画面の上に描くプレビュー（カードの実物など）。SceneManager::Draw の最後で呼ぶ
    void DrawGameOverlay(SpriteRenderer* sprite, TextRenderer* text);

    // 「この敵の並びで戦闘をやり直す」要求
    void RequestBattle(const std::vector<EncounterEnemy>& enemies);
    // 要求があれば true を返して out に入れる（1回で消費）。BattleScene のデバッグUIから呼ぶ
    bool ConsumeBattleRequest(std::vector<EncounterEnemy>& out);

    // 「cards.json を読み直して（手札も作り直して）ほしい」要求。addToHand が空でなければ手札に1枚足す
    // ※手札の Card は CardDataBase の中を指しているので、読み直しは手札を作り直せる BattleScene に任せる
    void RequestCardReload(const std::string& addToHand);
    bool ConsumeCardReload(std::string& addToHand);
    bool IsCardReloadPending();   // まだゲーム側が受け取っていないか（エディタの表示用）

    // 「このイベントをゲーム内で開く」要求（本物のイベント画面でプレビューする）。SceneManager から呼ぶ
    void RequestOpenEvent(const std::string& eventId);
    bool ConsumeOpenEvent(std::string& eventId);
}
