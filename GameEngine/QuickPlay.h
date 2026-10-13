#pragma once
#include <string>
#include <vector>
#include "Scenetype.h"
#include "EncounterData.h"
#include "PlayerData.h"

struct QuickDeck
{
    std::string  tag;                       // 軸にするカードのタグ（RunSetupに渡す）
    std::wstring name, desc;                // 選択画面の表示
    int archN = 5, suppN = 3, randN = 2;    // 軸・補助・完全ランダムの枚数
    std::vector<std::string> relics;        // このデッキに固定で持たせるレリックID
    std::vector<std::string> cards;         // 固定デッキ（空ならtagから自動で組む）
};

// クイックプレイ（5分体験）：デッキを選んで 戦闘→報酬 を繰り返し、最後にボス
// 各シーンはこのモードを知らない。SceneManagerが「フィールドへ戻る」を次の戦闘/結果に読み替える
class QuickPlay
{
public:
    static void Load(const std::string& path);
    static const std::vector<QuickDeck>& Decks() { return s_decks; }

    static void Enter();            // 開始準備：本編の状態を退避し、セーブを止める
    static void BuildDeck(int i);   // i番のデッキ・レリックを組む（選び直し可）
    static void Begin();            // 組んだ状態で戦闘を開始
    static void End();              // 終了：本編の状態に戻し、セーブを再開（タイトルへ戻る時に必ず呼ばれる）
    static bool Active() { return s_active; }

    static SceneType NextScene();   // 次は戦闘か結果画面か
    static void TakeBattle(std::string& encounter, EncCategory& cat);   // 次の戦闘を取り出して1つ進める

private:
    static inline std::vector<QuickDeck>   s_decks;
    static inline std::vector<std::string> s_battles;   // エンカウントID（この順に戦う）
    static inline int  s_stage = 0;
    static inline bool s_entered = false, s_active = false;
    static inline PlayerData s_before, s_base;          // 本編の退避／デッキを組む前の状態
};