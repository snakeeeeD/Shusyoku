#pragma once
// アイテムの窓：持ち物（コア・素材の一覧）と合成（コア＋素材でカードを作る／合成カードに素材を足す）。
// 2つは同じ画面の2つの状態（「合成する」で合成へ、「戻る」で持ち物へ）なので、位置・マス・スクロールを共有してモードで出し分ける。
// 窓の外のこと（「戻る」ボタン・ドラッグの判定・作ったカードの出現演出・戦闘中かどうか）は SceneManager が持つ
#include <Windows.h>
#include <string>
#include <utility>
#include <vector>
#include "URect.h"
#include "UiContext.h"

class ItemPanel
{
public:
    void OpenInventory();                 // 帯の「アイテム」から
    void BackToInventory();               // 合成の「戻る」
    void Close() { m_mode = Mode::Closed; }
    bool IsOpen() const { return m_mode != Mode::Closed; }
    bool IsInventory() const { return m_mode == Mode::Inventory; }
    bool IsCraft() const { return m_mode == Mode::Craft; }

    // クリック（持ち物：「合成する」で合成へ／合成：枠・コア・合成カード・素材・作成）。作ったカードの ID を返す（空＝なし）
    std::string HandleClick(UiContext& ui, POINT m, bool canCraft);
    void ScrollBy(float dy) { m_scrollTarget += dy; }      // ホイール・ドラッグ（ドラッグの判定はデッキ閲覧と共通なので外）
    void Update(UiContext& ui, float dt, bool dragging);   // スクロールの補間・はみ出した分を戻す
    void Draw(UiContext& ui, bool canCraft);               // 持ち物／合成 を出し分け（canCraft＝戦闘中でない）

private:
    enum class Mode { Closed, Inventory, Craft };
    Mode m_mode = Mode::Closed;

    // 合成
    std::string m_base;                 // 選んだコア
    std::vector<std::string> m_mods;    // 付ける素材
    int  m_editIdx = -1;                // 素材を足している合成カード（デッキの番号。-1＝新しく作る）
    int  m_locked = 0;                  // m_mods の先頭の、もう付いている素材の数（外せない）
    std::string HandleCraftClick(POINT m);
    std::string DoCraft();              // 作ったカードの ID（作れなければ空）
    std::vector<int> CraftEditables() const;   // 素材を足せる合成カード（デッキの番号）
    std::vector<std::pair<std::string, int>> CraftBases() const;   // 持っているコア
    std::vector<std::pair<std::string, int>> CraftMods() const;    // 持っている素材
    std::string CraftRecipeId() const;
    int CraftModSlots() const;

    // 素材の一覧のスクロール
    float m_scroll = 0.0f, m_scrollTarget = 0.0f;

    // 位置（描画とクリックで共有。持ち物と合成で少し違う）
    void GetCraftSlotRect(int i, float& x, float& y, float& w, float& h) const;
    void GetCraftBtnRect(float& x, float& y, float& w, float& h) const;
    void GetCraftBaseRect(int i, float& x, float& y, float& w, float& h) const;
    void GetCraftModRect(int i, float& x, float& y, float& w, float& h) const;
    float MatMaxScroll() const;
    float MatTop() const;
    float MatBottom() const;
    float MatAlphaAt(float y) const;
    float CoreTop() const;
    std::string HoveredItem(POINT mp) const;
    int  m_sw = 1280, m_sh = 720;   // 位置の計算に使う画面の大きさ（HandleClick/Update/Draw の最初で受け取る）

    // 描画
    void DrawInventory(UiContext& ui, bool canCraft);
    void DrawCraft(UiContext& ui);
    void DrawItemTooltip(UiContext& ui, const std::string& id, POINT mp);
    // アイテムのマス（持ち物・合成で共通）：左にアイコン → 名前（入りきらなければ縮める）→ 右上の角に個数バッジ
    float DrawItemIcons(UiContext& ui, const std::vector<const char*>& icons, float x, float y, float h, float size, float a);   // 戻り値＝名前の左端
    void  DrawItemBadge(UiContext& ui, float x, float y, float w, float a);
    void  DrawItemBadgeText(UiContext& ui, int count, float x, float y, float w, float a);
    void  DrawItemName(UiContext& ui, const wchar_t* s, float left, float right, float y, float h, D2D1_COLOR_F c, bool matFade);
    std::string m_lastHoverKey;     // ホバー音を重ねないためのキー
};
