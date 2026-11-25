#pragma once
// アイテムの窓：持ち物（コア・素材の一覧）と合成（コア＋素材でカードを作る／合成カードに素材を足す）。
// 2つは同じ画面の2つの状態（「合成する」で合成へ、「戻る」で持ち物へ）なので、位置・マスを共有してモードで出し分ける。
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
    void ScrollBy(float dy) { if (IsCraft()) m_scrollTarget += dy; }   // 合成の下の一覧だけ（入りきらない時に動く）
    void Update(UiContext& ui, float dt, bool dragging);              // スクロールの補間・はみ出した分を戻す
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

    // 位置（描画とクリックで共有。持ち物と合成で少し違う）
    void GetCraftSlotRect(int i, float& x, float& y, float& w, float& h) const;
    void GetCraftBtnRect(float& x, float& y, float& w, float& h) const;
    void GetCraftBaseRect(int i, float& x, float& y, float& w, float& h) const;
    void GetCraftModRect(int i, float& x, float& y, float& w, float& h) const;
    float m_scroll = 0.0f, m_scrollTarget = 0.0f;   // 合成の下の一覧のスクロール
    float ListMaxScroll() const;                    // 一覧が窓に入りきらない分
    float ListAlpha(float y) const;                 // 一覧の上下の端で薄くする（y＝マスの上端。0＝見えない）
    float MatTop() const;      // 素材のマスの上端
    float MatBottom() const;   // 持ち物の窓の下端の目安
    float CoreTop() const;     // コアのマスの上端（持ち物は見出しの分だけ下）

    // マスの大きさ（持ち物・合成で共通）
    static constexpr float TILE_W = 150.0f, TILE_H = 38.0f, TILE_GAP = 8.0f;
    static constexpr int   MAT_COLS = 6;                 // 持ち物の列の数
    static constexpr float PANEL_PAD = 24.0f;            // 窓の縁から中身まで
    // 合成の画面（上：枠→出来上がり／下：持っている物）
    static constexpr int   CRAFT_COLS = 7;               // 下の一覧の列の数
    static constexpr int   CRAFT_CORE_COLS = 4;          // 左の4列がコア、残りの3列が空き枠のある合成カード
    static constexpr float CRAFT_TILE_W = TILE_W;        // 合成の画面のマスの幅（持ち物と同じ）
    static constexpr float CRAFT_LEFT_W = CRAFT_COLS * CRAFT_TILE_W + (CRAFT_COLS - 1) * TILE_GAP;   // 窓の中身の幅
    static constexpr float CRAFT_TOP = 96.0f, CRAFT_BOTTOM = 650.0f;   // 窓の上下（下は右下の「戻る」に重ねない）
    static constexpr float SLOT_Y = 116.0f, SLOT_H = 46.0f, SLOT_GAP = 10.0f, SLOT_MAX_W = 150.0f;   // 合成の枠（一番上の1行）
    static constexpr float PLUS_W = 34.0f;               // 「＋」（コアの枠の後ろ）の幅
    static constexpr float PREVIEW_Y = 176.0f, PREVIEW_SCALE = 1.8f;   // 出来上がりのカード（画面の真ん中）
    static constexpr float SIDE_GAP = 30.0f;             // カードと、左の説明・右のボタンのすき間
    static constexpr float CRAFT_BTN_W = 240.0f, CRAFT_BTN_H = 60.0f;  // 「合成する」（カードの右）
    static constexpr float DESC_H = 160.0f;              // 素材の効果の説明（カードの左、カードと同じ高さから）
    static constexpr float LIST_Y = 444.0f, HEAD_H = 24.0f;            // 下の一覧の見出し（カードの下）／見出しからマスまで
    static constexpr float LIST_FADE = 20.0f;                          // 一覧の上下の端で薄くする幅（ここより内側がはっきり見える）
    URect GridRect(int col, int row, float top, int cols) const;       // 格子のマス（窓の真ん中にそろえる）
    URect CraftPanelRect() const;
    URect PreviewCardRect() const;
    URect DescRect() const;
    void  GetCraftEditRect(int k, float& x, float& y, float& w, float& h) const;   // 空き枠のある合成カード
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
    float DrawTile(UiContext& ui, float x, float y, float w, float h, XMFLOAT4 col, bool hov);   // マス1つ（ホバーで浮いて金の縁）。戻り値＝浮いた分
    void  DrawItemName(UiContext& ui, const wchar_t* s, float left, float right, float y, float h, D2D1_COLOR_F c);
    std::string m_lastHoverKey;     // ホバー音を重ねないためのキー
};
