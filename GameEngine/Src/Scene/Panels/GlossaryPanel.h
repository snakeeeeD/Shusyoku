#pragma once
// 用語集の窓。どこからでも Glossary::RequestOpen で開ける（SceneManager が受け取って Open する）
// 状態（今のカテゴリ・選んでいる用語・戻るの記録・動画）はこのクラスの中だけ
#include <Windows.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "URect.h"
#include "UiContext.h"

class VideoClip;
struct GlossaryEntry;

class GlossaryPanel
{
public:
    GlossaryPanel();
    ~GlossaryPanel();

    void Open(const std::string& id);   // id のページを開く（空なら先頭）
    void Close();                       // 動画も止め、戻るの記録も消す
    bool IsOpen() const { return m_open; }
    void HandleInput(UiContext& ui);
    void Draw(UiContext& ui);

private:
    std::vector<int> Items() const;     // 今のカテゴリの用語（Glossary::All() の添字）
    // 説明欄の並び（動画・説明文の上端・関連ボタン）。描画とクリック判定で共有
    struct Detail { URect video{}; float descTop = 0.0f; std::vector<std::pair<URect, std::string>> related; };
    Detail DetailLayout(UiContext& ui, const GlossaryEntry& e, const URect& d);

    bool m_open = false;
    int  m_cat = 0, m_sel = 0, m_scroll = 0;
    struct Page { int cat, sel, scroll; };
    std::vector<Page> m_back;              // 戻る用：前に見ていたページ（閉じたら消す）
    std::unique_ptr<VideoClip> m_video;    // 選んでいる用語の動画
    ULONGLONG m_videoTick = 0;
};
