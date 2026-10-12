#pragma once
#include <string>
#include <vector>

// ヒント1件（光らせる場所は論理1280x720。hw=0なら枠なし）
struct HintDef
{
    const char* id;
    const wchar_t* text;
    float hx = 0, hy = 0, hw = 0, hh = 0;
};

// その場ヒント：状況が初めて起きた時に1回だけ、操作を止めずに出す
class Hint
{
public:
    static void Request(const char* id);   // 状況が起きたら呼ぶ（既読・表示待ちなら無視）
    static void Resolve(const char* id);   // 状況が解消したら呼ぶ（表示中ならフェードして閉じる）
    static void Update(float dt);
    static void Clear();                   // シーン切替・戦闘終了時
    static const HintDef* Current() { return s_cur; }
    static float Alpha();

private:
    static const HintDef* Find(const char* id);
    static void Next();
    static inline const HintDef* s_cur = nullptr;
    static inline float s_time = 0.0f;
    static inline std::vector<const HintDef*> s_queue;
};