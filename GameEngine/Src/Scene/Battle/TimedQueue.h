#pragma once
// 時間を置いて1つずつ処理する順番待ち（連撃の追撃・敵の連撃・罠の順番起爆など）
// 先頭を1つ取り出して処理 → 処理が返した秒数だけ待って次へ。負の数を返したら残りを捨てて終わり。
// 使い方：q.Start(items, 最初の待ち);  毎フレーム q.Update(dt, [&](T& x) { ...; return 次までの秒数; });
#include <vector>
#include <utility>
#include <algorithm>

template <class T>
class TimedQueue
{
public:
    // 入れ直して始める（前に残っていた分は捨てる）
    void Start(std::vector<T> items, float firstDelay = 0.0f)
    {
        m_items = std::move(items);
        m_timer = firstDelay;
    }
    void Push(const T& v) { m_items.push_back(v); }     // 後ろに足す（連鎖の次の波など）
    void Delay(float sec) { m_timer = sec; }            // 次の処理までの時間を決め直す
    void Clear() { m_items.clear(); }
    bool Empty() const { return m_items.empty(); }

    // 条件に合うものが入っているか（同じ罠を2回積まない、など）
    template <class P>
    bool Any(P pred) const { return std::any_of(m_items.begin(), m_items.end(), pred); }

    // 時間が来たら先頭を1つ取り出して step に渡す。step の戻り値＝次までの秒数（負なら残りを捨てる）
    // step の中で Push してもよい（取り出してから呼ぶので）
    template <class F>
    void Update(float dt, F&& step)
    {
        if (m_items.empty()) return;
        m_timer -= dt;
        if (m_timer > 0.0f) return;
        T item = std::move(m_items.front());
        m_items.erase(m_items.begin());
        float next = step(item);
        if (next < 0.0f) m_items.clear();
        m_timer = next;
    }

private:
    std::vector<T> m_items;
    float m_timer = 0.0f;
};
