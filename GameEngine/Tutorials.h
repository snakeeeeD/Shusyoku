#pragma once
// 初めての時に1回だけ出すチュートリアルの中身。文や照らす場所（論理1280x720）を変えるならここだけ。
// 出すのは SceneManager::ShowTutorialOnce（既読なら出さない → 既読にして保存 → 出す／遅らせる）
#include <vector>
#include "TutorialPanel.h"
#include "PlayerData.h"

enum class TutorialId { Field, Battle, CardSelect, Shop, Craft };

struct TutorialDef
{
    TutorialId id;
    bool PlayerData::* seen;                    // 既読のフラグ（セーブデータ）
    float delay;                                // 遅らせて出す秒数（0＝すぐ）
    std::vector<TutorialPanel::Page> pages;
};

inline const std::vector<TutorialDef>& TutorialDefs()
{
    static const std::vector<TutorialDef> defs = {
        { TutorialId::Field, &PlayerData::tutorialField, 0.0f, {
            { L"マスをクリックして進む\n右端のボスを倒すと次の層へ", 170, 130, 940, 540 },
        } },
        { TutorialId::Battle, &PlayerData::tutorialBattle, 1.2f, {
            { L"カードを掴んで、使いたいマスの上で離す\n左から 移動・アタック・ブロック", 340, 580, 600, 220 },
            { L"色付きのマスは敵の攻撃範囲\n移動で避けて、アタックで倒そう",         170, 130, 940, 540 },
        } },
        { TutorialId::CardSelect, &PlayerData::tutorialCardSelect, 0.0f, {
            { L"1枚選ぶとデッキに加わる（スキップも可）", 376, 224, 490, 224 },
        } },
        { TutorialId::Shop, &PlayerData::tutorialShop, 0.0f, {
            { L"クリックで購入（ゴールドを使う）\n左下でカード削除、SELLで素材を売れる", 180, 120, 920, 470 },
        } },
        // 初勝利で初コアを得た瞬間（勝利画面を少し見せてから）。スポットライトは勝利画面のドロップ行に一致
        //（cx640 / rx490 / 行0=y270・行1=y322 / 300x42）
        { TutorialId::Craft, &PlayerData::tutorialCraft, 1.0f, {
            { L"【コア】カードの土台。何を作るかが決まる",                              482, 262, 316, 54 },
            { L"【素材】コアに足す強化部品\nアイテム画面の「合成する」で組み合わせる", 486, 318, 308, 50 },
        } },
    };
    return defs;
}
