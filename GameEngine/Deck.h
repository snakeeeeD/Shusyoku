#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <random>

class Deck
{
public:
    Deck();

    void AddCard(const std::string& id);
    void AddCardRandom(const std::string& id);   // 山札のランダムな位置に挿入
    std::string DrawCard();
    std::string DrawSpecificCard(const std::string& id);
    std::string SalvageCard(const std::string& id);
    void DiscardCard(const std::string& id);
    void ExhaustCard(const std::string& id);
    void ShuffleDrawPile();
    void Reset();

    int GetDrawPileCount()    const { return (int)m_drawPile.size(); }
    int GetDiscardPileCount() const { return (int)m_discardPile.size(); }
    int GetExhaustPileCount() const { return (int)m_exhaustPile.size(); }
    const std::vector<std::string>& GetExhaustPile() const { return m_exhaustPile; }

    const std::vector<std::string>& GetDrawPile()    const { return m_drawPile; }    // ← 追加
    const std::vector<std::string>& GetDiscardPile() const { return m_discardPile; } // ← 追加

    // 引いた拍子に捨て札を山札へ戻していたら、戻した後に引いた枚数を返す（演出用。読むと消える。戻していなければ0）
    int ConsumeReshuffled() { int n = m_reshuffled ? m_drawnAfterReshuffle : 0; m_reshuffled = false; m_drawnAfterReshuffle = 0; return n; }

private:
    std::vector<std::string> m_drawPile;
    std::vector<std::string> m_discardPile;
    std::vector<std::string> m_exhaustPile;
    bool m_reshuffled = false;
    int  m_drawnAfterReshuffle = 0;
};