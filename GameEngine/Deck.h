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
    void AddCardRandom(const std::string& id);   // RD‚Ìƒ‰ƒ“ƒ_ƒ€‚ÈˆÊ’u‚É‘}“ü
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

    const std::vector<std::string>& GetDrawPile()    const { return m_drawPile; }    // © ’Ç‰Á
    const std::vector<std::string>& GetDiscardPile() const { return m_discardPile; } // © ’Ç‰Á

private:
    std::vector<std::string> m_drawPile;
    std::vector<std::string> m_discardPile;
    std::vector<std::string> m_exhaustPile;
};