#include "GlossaryPanel.h"
#include "Glossary.h"
#include "GlossaryLink.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "VideoClip.h"
#include <algorithm>

extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pImmediateContext;

GlossaryPanel::GlossaryPanel() = default;
GlossaryPanel::~GlossaryPanel() = default;   // VideoClip の中身が見えるここで消す

void GlossaryPanel::Close()
{
	m_open = false;
	m_back.clear();
	if (m_video) m_video->Close();
}

// 窓の各部の位置（描画とクリックで共有）
struct GlossUI { URect panel, close, back, cat0, list, detail; float catStep, rowH; int rows; };
static GlossUI GlossLayout(int sw, int sh)
{
	GlossUI g;
	float pw = 960.0f, ph = 650.0f, px = (sw - pw) / 2.0f, py = (sh - ph) / 2.0f;
	g.panel = { px, py, pw, ph };
	g.cat0 = { px + 20.0f, py + 64.0f, 150.0f, 38.0f }; g.catStep = 44.0f;
	g.list = { px + 186.0f, py + 64.0f, 262.0f, 510.0f }; g.rowH = 35.0f; g.rows = (int)(g.list.h / g.rowH);
	g.detail = { px + 464.0f, py + 64.0f, 476.0f, 510.0f };
	g.close = { px + pw / 2.0f - 70.0f, py + ph - 52.0f, 140.0f, 38.0f };
	g.back  = { g.close.x - 150.0f, g.close.y, 140.0f, 38.0f };   // 閉じるの左
	return g;
}

// 今のカテゴリの用語（Glossary::All() の添字）
std::vector<int> GlossaryPanel::Items() const
{
	std::vector<int> v;
	auto& cats = Glossary::Categories();
	auto& all = Glossary::All();
	if (m_cat < 0 || m_cat >= (int)cats.size()) return v;
	for (int i = 0; i < (int)all.size(); i++)
		if (all[i].category == cats[m_cat] && !Glossary::Locked(all[i])) v.push_back(i);   // まだ出会っていない敵の用語は隠す
	return v;
}

// 説明欄の並び（動画・説明文の上端・関連ボタン）。描画とクリック判定で共有
// 説明文が長くて関連ボタンまで入らない時は、動画を16:9のまま小さくして場所を空ける
GlossaryPanel::Detail GlossaryPanel::DetailLayout(UiContext& ui, const GlossaryEntry& e, const URect& d)
{
	Detail L;
	auto& out = L.related;
	float x = d.x + 12.0f, y = 0.0f;   // 関連ボタン：まず上端0で並べて、行数（全体の高さ）を出す
	for (auto& id : e.related)
	{
		const GlossaryEntry* r = Glossary::Get(id);
		if (!r || Glossary::Locked(*r)) continue;
		float w = ui.text->Measure(r->name.c_str(), 16.0f, FontRole::Body).width + 28.0f;
		if (x + w > d.x + d.w - 12.0f) { x = d.x + 12.0f; y += 36.0f; }
		out.push_back({ { x, y, w, 32.0f }, id });
		x += w + 8.0f;
	}
	float relH  = out.empty() ? 0.0f : y + 32.0f + 40.0f;   // ボタン＋「関連」の文字ぶん
	float descH = ui.text->Measure(e.desc.c_str(), 17.0f, FontRole::Body, d.w - 24.0f).height;
	float bottom = d.y + d.h - 6.0f;
	if (!e.clip.empty())
	{
		float room = bottom - relH - descH - 12.0f - (d.y + 54.0f);   // 動画に使える高さ
		float vh = (std::max)(150.0f, (std::min)(252.0f, room));
		float vw = vh * 16.0f / 9.0f;
		L.video   = { d.x + 14.0f + (448.0f - vw) / 2.0f, d.y + 54.0f, vw, vh };   // 元の幅の中で中央
		L.descTop = L.video.y + vh + 12.0f;
	}
	else L.descTop = d.y + (e.icon.empty() ? 52.0f : 96.0f);
	// 関連ボタンを置く高さ：説明文の下。動画が最小でも入らない時は、下の端に合わせて上げる（保険）
	float below = L.descTop + descH + 40.0f;
	float fit = bottom - (y + 32.0f);
	float y0 = (below < fit) ? below : fit;
	for (auto& o : out) o.first.y += y0;
	return L;
}

// id のページを開く（空なら先頭）
void GlossaryPanel::Open(const std::string& id)
{
	m_open = true;
	m_cat = 0; m_sel = 0; m_scroll = 0;
	const GlossaryEntry* e = id.empty() ? nullptr : Glossary::Get(id);
	if (!e || Glossary::Locked(*e)) return;
	auto& cats = Glossary::Categories();
	for (int c = 0; c < (int)cats.size(); c++) if (cats[c] == e->category) m_cat = c;
	auto items = Items();
	for (int i = 0; i < (int)items.size(); i++) if (Glossary::All()[items[i]].id == id) m_sel = i;
	GlossUI g = GlossLayout(0, 0);   // 一覧の行数だけ使う（画面の大きさに依らない）
	if (m_sel >= g.rows) m_scroll = m_sel - g.rows + 1;
}

void GlossaryPanel::HandleInput(UiContext& ui)
{
	POINT m = ui.input->GetMousePos();
	bool click = ui.input->GetMouseButtonTrigger(0);
	GlossUI g = GlossLayout(ui.screenW, ui.screenH);
	auto items = Items();
	int maxScroll = (int)items.size() - g.rows; if (maxScroll < 0) maxScroll = 0;

	int wheel = ui.input->GetMouseWheelDelta();
	if (wheel != 0 && g.list.has(m))
	{
		m_scroll += (wheel > 0) ? -1 : 1;
		if (m_scroll < 0) m_scroll = 0;
		if (m_scroll > maxScroll) m_scroll = maxScroll;
	}
	auto pushPage = [&]() {                                    // ページを切り替える直前に、今のページを記録
		if (m_back.size() >= 50) m_back.erase(m_back.begin());
		m_back.push_back({ m_cat, m_sel, m_scroll });
	};
	auto goBack = [&]() {
		if (m_back.empty()) return false;
		Page p = m_back.back(); m_back.pop_back();
		m_cat = p.cat; m_sel = p.sel; m_scroll = p.scroll;
		Audio::PlaySE("Assets/Sound/se/click.mp3");
		return true;
	};
	if (ui.input->GetKeyTrigger(VK_ESCAPE)) { Close(); return; }
	if (ui.input->GetKeyTrigger(VK_BACK) && goBack()) return;
	if (!click) return;
	if (g.back.has(m) && goBack()) return;

	if (g.close.has(m) || !g.panel.has(m))
	{
		Audio::PlaySE("Assets/Sound/se/click.mp3");
		Close();
		return;
	}
	auto& cats = Glossary::Categories();
	for (int c = 0; c < (int)cats.size(); c++)
	{
		URect r = { g.cat0.x, g.cat0.y + c * g.catStep, g.cat0.w, g.cat0.h };
		if (r.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); if (c != m_cat) pushPage(); m_cat = c; m_sel = 0; m_scroll = 0; return; }
	}
	for (int row = 0; row < g.rows; row++)
	{
		int i = m_scroll + row;
		if (i >= (int)items.size()) break;
		URect r = { g.list.x, g.list.y + row * g.rowH, g.list.w, g.rowH - 3.0f };
		if (r.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); if (i != m_sel) pushPage(); m_sel = i; return; }
	}
	if (m_sel >= 0 && m_sel < (int)items.size())
	{
		const GlossaryEntry& e = Glossary::All()[items[m_sel]];
		Detail L = DetailLayout(ui, e, g.detail);
		for (auto& [r, id] : L.related)
			if (r.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); pushPage(); Open(id); return; }
		auto hits = Glossary::FindTerms(e.desc, e.name);             // 説明文の中の用語
		float top = L.descTop;
		int h = ui.text->HitTestSpans(e.desc.c_str(), g.detail.x + 12.0f, top, g.detail.w - 24.0f, 17.0f,
			GlossaryLink::Spans(hits), (float)m.x, (float)m.y);
		if (h >= 0) { Audio::PlaySE("Assets/Sound/se/click.mp3"); pushPage(); Open(hits[h].id); return; }
	}
}

void GlossaryPanel::Draw(UiContext& ui)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	GlossUI g = GlossLayout(ui.screenW, ui.screenH);
	POINT mp = ui.input->GetMousePos();
	auto& cats = Glossary::Categories();
	auto& all = Glossary::All();
	auto items = Items();
	if (m_sel >= (int)items.size()) m_sel = 0;
	const GlossaryEntry* sel = items.empty() ? nullptr : &all[items[m_sel]];
	Detail lay;
	if (sel) lay = DetailLayout(ui, *sel, g.detail);
	auto& related = lay.related;

	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, 0, (float)ui.screenW, (float)ui.screenH, 0.0f, XMFLOAT4(0, 0, 0, 0.6f)); // 暗幕
	UiWindow::Draw(ui.sprite, white, g.panel.x, g.panel.y, g.panel.w, g.panel.h);
	for (int c = 0; c < (int)cats.size(); c++)
	{
		URect r = { g.cat0.x, g.cat0.y + c * g.catStep, g.cat0.w, g.cat0.h };
		XMFLOAT4 col = (c == m_cat) ? XMFLOAT4(0.55f, 0.45f, 0.25f, 1)
			: r.has(mp) ? XMFLOAT4(0.32f, 0.36f, 0.5f, 1) : XMFLOAT4(0.22f, 0.24f, 0.34f, 1);
		UiWindow::Button(ui.sprite, white, r.x, r.y, r.w, r.h, col);
	}
	UiWindow::Draw(ui.sprite, white, g.list.x - 6.0f, g.list.y - 6.0f, g.list.w + 12.0f, g.list.h + 12.0f);
	for (int row = 0; row < g.rows; row++)
	{
		int i = m_scroll + row;
		if (i >= (int)items.size()) break;
		URect r = { g.list.x, g.list.y + row * g.rowH, g.list.w, g.rowH - 3.0f };
		if (i == m_sel)   ui.sprite->DrawSprite(white, r.x, r.y, r.w, r.h, 0.0f, XMFLOAT4(0.55f, 0.45f, 0.25f, 0.55f));
		else if (r.has(mp))    ui.sprite->DrawSprite(white, r.x, r.y, r.w, r.h, 0.0f, XMFLOAT4(0.4f, 0.45f, 0.6f, 0.35f));
	}
	UiWindow::Draw(ui.sprite, white, g.detail.x, g.detail.y, g.detail.w, g.detail.h);
	if (sel && sel->clip.empty() && !sel->icon.empty())
		if (auto tex = TextureManager::Get(sel->icon))
			ui.sprite->DrawSprite(tex, g.detail.x + 14.0f, g.detail.y + 14.0f, 64.0f, 64.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));

	// 動画：選んでいる用語の分だけ開いてループ再生
	std::string clipPath = sel ? sel->clip : "";
	if (!m_video) m_video = std::make_unique<VideoClip>();
	if (m_video->Path() != clipPath)
	{
		m_video->Close();
		if (!clipPath.empty()) m_video->Open(g_pd3dDevice, clipPath);
		m_videoTick = GetTickCount64();
	}
	if (!clipPath.empty())
	{
		ULONGLONG now = GetTickCount64();
		m_video->Update(g_pImmediateContext, (now - m_videoTick) / 1000.0);
		m_videoTick = now;
		URect v = lay.video;
		ui.sprite->DrawSprite(white, v.x - 2.0f, v.y - 2.0f, v.w + 4.0f, v.h + 4.0f, 0.0f, XMFLOAT4(0.8f, 0.66f, 0.32f, 1));   // 金の縁
		if (auto srv = m_video->SRV())
			ui.sprite->DrawSprite(srv, v.x, v.y, v.w, v.h, 0.0f, XMFLOAT4(1, 1, 1, 1));
		else
			ui.sprite->DrawSprite(white, v.x, v.y, v.w, v.h, 0.0f, XMFLOAT4(0.05f, 0.05f, 0.08f, 1));
	}
	for (auto& [r, id] : related)
		UiWindow::Button(ui.sprite, white, r.x, r.y, r.w, r.h,
			r.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	UiWindow::Button(ui.sprite, white, g.close.x, g.close.y, g.close.w, g.close.h,
		g.close.has(mp) ? XMFLOAT4(0.5f, 0.35f, 0.35f, 1) : XMFLOAT4(0.35f, 0.25f, 0.25f, 1));
	bool canBack = !m_back.empty();                    // 戻る（記録がない時は暗く）
	UiWindow::Button(ui.sprite, white, g.back.x, g.back.y, g.back.w, g.back.h,
		!canBack ? XMFLOAT4(0.18f, 0.18f, 0.2f, 1) : g.back.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	ui.sprite->End();

	ui.text->Begin();
	const D2D1_COLOR_F W = D2D1::ColorF(1, 1, 1), GOLD = D2D1::ColorF(1.0f, 0.85f, 0.4f);
	ui.text->DrawTextCentered(L"用語集", g.panel.x + g.panel.w / 2.0f, g.panel.y + 12.0f, g.panel.w, 26.0f,
		D2D1::ColorF(1, 0.9f, 0.6f), 40.0f);
	for (int c = 0; c < (int)cats.size(); c++)
		ui.text->DrawTextCentered(cats[c].c_str(), g.cat0.x + g.cat0.w / 2.0f, g.cat0.y + c * g.catStep,
			g.cat0.w, 18.0f, W, g.cat0.h);
	for (int row = 0; row < g.rows; row++)
	{
		int i = m_scroll + row;
		if (i >= (int)items.size()) break;
		float y = g.list.y + row * g.rowH;
		ui.text->DrawText(all[items[i]].name.c_str(), g.list.x + 12.0f, y + 5.0f, 17.0f, i == m_sel ? GOLD : W);
	}
	if ((int)items.size() > g.rows)   // まだ下（上）にある時の目印
	{
		if (m_scroll > 0)
			ui.text->DrawText(L"▲", g.list.x + g.list.w - 22.0f, g.list.y - 2.0f, 14.0f, GOLD);
		if (m_scroll + g.rows < (int)items.size())
			ui.text->DrawText(L"▼", g.list.x + g.list.w - 22.0f, g.list.y + g.list.h - 18.0f, 14.0f, GOLD);
	}
	if (sel)
	{
		float tx = g.detail.x + ((sel->icon.empty() || !sel->clip.empty()) ? 14.0f : 90.0f);
		{
			TextRenderer::RoleScope heading(*ui.text, FontRole::Heading);
			ui.text->DrawText(sel->name.c_str(), tx, g.detail.y + 18.0f, 26.0f, GOLD);
		}
		float top = lay.descTop;
		auto hits = Glossary::FindTerms(sel->desc, sel->name);       // 用語は色付き、マウスが乗ると明るく
		int hov = ui.text->HitTestSpans(sel->desc.c_str(), g.detail.x + 12.0f, top, g.detail.w - 24.0f, 17.0f,
			GlossaryLink::Spans(hits), (float)mp.x, (float)mp.y);
		ui.text->DrawTextSpans(sel->desc.c_str(), g.detail.x + 12.0f, top, g.detail.w - 24.0f, 17.0f, W,
			GlossaryLink::Spans(hits, hov));
		if (!related.empty())
			ui.text->DrawText(L"関連", related[0].first.x, related[0].first.y - 22.0f, 14.0f,
				D2D1::ColorF(0.7f, 0.75f, 0.85f));
		for (auto& [r, id] : related)
			if (const GlossaryEntry* re = Glossary::Get(id))
				ui.text->DrawTextCentered(re->name.c_str(), r.x + r.w / 2.0f, r.y, r.w, 16.0f, W, r.h);
	}
	ui.text->DrawTextCentered(L"閉じる", g.close.x + g.close.w / 2.0f, g.close.y, g.close.w, 20.0f, W, g.close.h);
	ui.text->DrawTextCentered(L"◀ 戻る", g.back.x + g.back.w / 2.0f, g.back.y, g.back.w, 20.0f,
		canBack ? W : D2D1::ColorF(0.5f, 0.5f, 0.5f), g.back.h);
	ui.text->End();
}

