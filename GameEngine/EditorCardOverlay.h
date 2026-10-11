#pragma once

// エディタ共通：ゲーム画面の上にカードを実物の見た目（CardVisual）で並べて描く（Debugビルド専用）
// カード編集・合成編集のプレビューで使う

#ifdef _DEBUG

class SpriteRenderer;
class TextRenderer;
struct CardData;

namespace EditorCardOverlay
{
    struct Item
    {
        const CardData* card;
        const wchar_t* label;   // カードの上に出す見出し（"編集中" など）
        bool highlight;         // 見出しを緑にする（強化後など）
    };

    // left=true なら画面左側、false なら右側に並べる
    void Draw(SpriteRenderer* sprite, TextRenderer* text, const Item* items, int count, bool left);
}

#endif
