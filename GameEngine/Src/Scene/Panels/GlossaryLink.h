#pragma once
#include <string>
#include <vector>
#include <functional>
#include "TextRenderer.h"

// 説明文の中で見つかった用語（文字の位置と、用語集の id）
struct TermHit
{
    unsigned start = 0, length = 0;
    std::string id;
};

// 説明文から用語を探す窓口。Glossary が読み込み時に find を設定する
// （CardTooltip などが Glossary.h を直接読むと include が循環するため、ここを経由する）
struct GlossaryLink
{
    static inline std::function<std::vector<TermHit>(const std::wstring&, const std::wstring&)> find;
    static inline std::function<void(const std::string&)> open;   // 用語集のそのページを開く

    // exclude：自分自身の名前（その用語の説明の中では色を付けない）
    static std::vector<TermHit> Find(const std::wstring& text, const std::wstring& exclude = L"")
    {
        return find ? find(text, exclude) : std::vector<TermHit>();
    }

    static constexpr D2D1_COLOR_F TermColor() { return D2D1_COLOR_F{ 1.0f, 0.85f, 0.4f, 1.0f }; }

    // 見つかった用語を色の範囲に変える（hovered 番目だけ明るく）
    static std::vector<TextSpan> Spans(const std::vector<TermHit>& hits, int hovered = -1)
    {
        std::vector<TextSpan> out;
        for (int i = 0; i < (int)hits.size(); i++)
            out.push_back({ hits[i].start, hits[i].length,
                i == hovered ? D2D1_COLOR_F{ 1.0f, 1.0f, 0.75f, 1.0f } : TermColor() });
        return out;
    }
};
