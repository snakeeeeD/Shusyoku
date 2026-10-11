#pragma once

// エディタツール共通の部品（Debugビルド専用）
// - JSON ファイルの読み書き（生のJSONを編集し、BOM・改行コード・項目の順番を保つ）
// - 盤面の描画（ゲーム画面と同じ並びにするため列を左右反転）
// - 敵・エフェクト・カードの ID 一覧（JSON から直接読む）
// - 「JSON の項目」を直接編集する ImGui 部品

#ifdef _DEBUG

#include "External/imgui/imgui.h"
#include <nlohmann/json.hpp>
#include <d3d11.h>
#include <string>
#include <utility>
#include <vector>

namespace EditorCommon
{
    // 項目の順番を保つ JSON（普通の json だとキーがアルファベット順に並び替わり、git の差分が全行になる）
    using ojson = nlohmann::ordered_json;

    // ==================================================================
    // JSON ファイル
    // ==================================================================
    struct JsonFile
    {
        ojson doc;
        bool bom = false;      // 元ファイルの書式（保存時に合わせる）
        bool crlf = false;
        int indent = 1;        // 元ファイルのインデント幅（encounters.json は1、enemies.json は2）
        std::string error;

        bool Load(const char* path);
        // 直前の状態を backupPath に1つ残してから保存（インデント・BOM・改行コードは元ファイルに合わせる）
        bool Save(const char* path, const char* backupPath);
    };

    // ==================================================================
    // 盤面（BattleScene::Init と同じ値にする）
    // ==================================================================
    constexpr int GRID_COLS  = 9;               // m_gridMap->Init(9, 7, ...)
    constexpr int GRID_ROWS  = 7;
    constexpr int PLAYER_COL = GRID_COLS / 2;   // m_playerCol = GetCols() / 2
    constexpr int PLAYER_ROW = GRID_ROWS / 2;   // m_playerRow = GetRows() / 2

    // 【左右反転】ゲームのカメラは -Z 方向を向いている（LookAtLH）ので、画面の右＝ワールドの -X になり、
    // col 0 は画面の「右端」に映る。ツールの盤面もゲーム画面と同じ並びにするため、列だけ左右反転して描く。
    // データ上の col/row は変えない。反転するのは「描く位置」と「マウス→マス」の変換だけ（自分自身が逆変換）。
    inline int ScreenCol(int col) { return GRID_COLS - 1 - col; }
    inline bool InBounds(int c, int r) { return c >= 0 && c < GRID_COLS && r >= 0 && r < GRID_ROWS; }

    // 盤面を描く場所（InvisibleButton を置き、マウスがどのマスにあるかを持つ）
    struct GridCanvas
    {
        ImVec2 origin;
        float cell = 64.0f;
        ImDrawList* dl = nullptr;
        bool hovered = false;
        int hc = -1, hr = -1;      // マウスのあるマス（データの col/row）

        void Begin(const char* id, float cellSize);
        bool HoverCell() const { return hovered && InBounds(hc, hr); }

        ImVec2 Min(int c, int r) const { return ImVec2(origin.x + ScreenCol(c) * cell, origin.y + r * cell); }
        ImVec2 Max(int c, int r) const { return ImVec2(origin.x + (ScreenCol(c) + 1) * cell, origin.y + (r + 1) * cell); }

        void DrawBoard() const;                                   // 市松模様のマス
        void FillCell(int c, int r, ImU32 col) const;             // 盤面外は描かない
        void OutlineCell(int c, int r, ImU32 col, float thick = 2.0f) const;
        // 複数マスを囲む画面上の矩形（反転しているので画面の左端は col の最大値）
        void Bounds(const std::vector<std::pair<int, int>>& cells, ImVec2& a, ImVec2& b) const;
    };

    // 矩形 [p0,p1] に縦横比を保って画像を描く（下揃え）。画像が無ければ fallbackText を描く
    void DrawTextureFit(ImDrawList* dl, ID3D11ShaderResourceView* srv, ImVec2 p0, ImVec2 p1,
        ImU32 tint, const char* fallbackText = nullptr);

    // ==================================================================
    // ID 一覧（JSON から直接読む。Reload() で読み直し）
    // ==================================================================
    struct EnemyDef
    {
        std::string id;
        std::string texture;
        std::vector<std::pair<int, int>> shape;   // 占有マス（基準マスからの相対）
        bool boss = false;
    };

    const std::vector<EnemyDef>& EnemyDefs();
    const EnemyDef* FindEnemy(const std::string& id);
    const std::vector<std::string>& EnemyIds();
    const std::vector<std::string>& EffectIds();    // effects.json
    const std::vector<std::string>& CardIds();      // cards.json
    const std::vector<std::string>& TerrainIds();   // terrains.json（罠の種類）
    void ReloadIdLists();

    // ==================================================================
    // 「JSON の項目」を直接編集する ImGui 部品（変更したら true）
    // - key が無い時は def を表示する。変更した時だけ key を書き込む
    // ==================================================================
    bool InputIntKey(const char* label, ojson& obj, const char* key, int def, float width = 90.0f);
    bool InputFloatKey(const char* label, ojson& obj, const char* key, float def, float width = 90.0f);
    bool CheckboxKey(const char* label, ojson& obj, const char* key, bool def = false);
    bool InputTextKey(const char* label, ojson& obj, const char* key, float width = 200.0f);

    // 文字列の選択肢から選ぶ。optional=true なら先頭に "(none)"（選ぶと key を消す）
    // 今の値が選択肢に無い（打ち間違いなど）時は赤で表示する
    bool ComboKey(const char* label, ojson& obj, const char* key, const std::vector<std::string>& options,
        const char* def, bool optional, float width = 150.0f);

    // 「保存できない理由」を赤字で並べる
    void DrawProblems(const std::vector<std::string>& problems);
}

#endif
