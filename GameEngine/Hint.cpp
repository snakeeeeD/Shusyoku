#include "Hint.h"
#include "PlayerDataManager.h"
#include <algorithm>
#include <cstring>

// ヒント一覧。増やす時はここに1行足して、状況が起きる所で Hint::Request(id) を呼ぶ
static const HintDef DEFS[] = {
    { "threat",  L"頭上に赤い警告 ＝ 今の位置に攻撃が当たる\n移動カードで色付きマスの外へ逃げよう" },
    { "endTurn", L"エナジー（左下）を使い切った\nターンエンドで敵の番へ。次のターンに回復する", 1072, 636, 196, 72 },
    { "focus",   L"敵や右上の窓にマウスを乗せると\nその敵の攻撃範囲が点滅する" },
    { "camera",  L"ホイール：ズーム　右ドラッグ：カメラ移動\nホイールクリック：自分の位置に戻す" },
    { "steps",   L"歩数が0になった\nここから先は進むほど敵が強くなる", 130, 5, 110, 28 },
};
static constexpr float SHOW_TIME = 9.0f;   // 自動で閉じるまで（秒）
static constexpr float FADE = 0.3f;

const HintDef* Hint::Find(const char* id)
{
    for (auto& d : DEFS)
        if (strcmp(d.id, id) == 0) return &d;
    return nullptr;
}

void Hint::Request(const char* id)
{
    const HintDef* d = Find(id);
    if (!d || s_cur == d) return;
    if (PlayerDataManager::GetData().seenHints.count(id)) return;
    if (std::find(s_queue.begin(), s_queue.end(), d) != s_queue.end()) return;
    s_queue.push_back(d);
    if (!s_cur) Next();
}

void Hint::Resolve(const char* id)
{
    const HintDef* d = Find(id);
    if (!d) return;
    s_queue.erase(std::remove(s_queue.begin(), s_queue.end(), d), s_queue.end());
    if (s_cur == d && s_time < SHOW_TIME - FADE) s_time = SHOW_TIME - FADE;   // 解消したらフェードして閉じる
}

void Hint::Update(float dt)
{
    if (!s_cur) return;
    s_time += dt;
    if (s_time >= SHOW_TIME) Next();
}

void Hint::Next()
{
    s_cur = nullptr;
    if (s_queue.empty()) return;
    s_cur = s_queue.front();
    s_queue.erase(s_queue.begin());
    s_time = 0.0f;
    PlayerDataManager::GetData().seenHints.insert(s_cur->id);   // 表示した時点で既読
    PlayerDataManager::Save();
}

void Hint::Clear()
{
    if (s_cur && s_time < 2.0f)   // ほぼ見せずに消える時は未読に戻す（次の機会にまた出る）
        PlayerDataManager::GetData().seenHints.erase(s_cur->id);
    s_cur = nullptr;
    s_queue.clear();
}

void Hint::MarkAllSeen()
{
    Clear();   // 表示中・順番待ちのヒントを消す
    auto& seen = PlayerDataManager::GetData().seenHints;
    for (auto& d : DEFS) seen.insert(d.id);
}

float Hint::Alpha()
{
    if (!s_cur) return 0.0f;
    float a = s_time / FADE, b = (SHOW_TIME - s_time) / FADE;
    if (b < a) a = b;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}