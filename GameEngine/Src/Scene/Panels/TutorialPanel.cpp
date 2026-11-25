#include "TutorialPanel.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"

void TutorialPanel::Show(const std::vector<Page>& pages)
{
	if (pages.empty()) return;
	m_pages = pages;
	m_page = 0;
	m_open = true;
}

void TutorialPanel::ShowDelayed(const std::vector<Page>& pages, float delay)
{
	m_pending = pages;
	m_delay = delay;
}

bool TutorialPanel::HandleInput(UiContext& ui)
{
	if (!ui.input->GetMouseButtonTrigger(0)) return false;
	Audio::PlaySE("Assets/Sound/se/click.mp3");
	m_page++;
	if (m_page < (int)m_pages.size()) return false;
	m_open = false;
	return true;   // 最後のページを閉じた
}

void TutorialPanel::Update(float dt)
{
	if (m_delay <= 0.0f) return;
	m_delay -= dt;
	if (m_delay <= 0.0f && !m_pending.empty())
	{
		Show(m_pending);
		m_pending.clear();
	}
}

void TutorialPanel::Draw(UiContext& ui, const URect& spot)
{
	if (m_page >= (int)m_pages.size()) { m_open = false; return; }
	const auto& pg = m_pages[m_page];
	float hx = spot.x, hy = spot.y, hw = spot.w, hh = spot.h;   // 照らす場所（戦闘の物なら今の位置。外で調べて渡す）
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float sw = (float)ui.screenW, sh = (float)ui.screenH;
	XMFLOAT4 dim(0, 0, 0, 0.7f);

	ui.sprite->Begin();
	if (hw > 0.0f)
	{
		float x = hx, y = hy, w = hw, h = hh;
		ui.sprite->DrawSprite(white, 0, 0, sw, y, 0.0f, dim);
		ui.sprite->DrawSprite(white, 0, y + h, sw, sh - (y + h), 0.0f, dim);
		ui.sprite->DrawSprite(white, 0, y, x, h, 0.0f, dim);
		ui.sprite->DrawSprite(white, x + w, y, sw - (x + w), h, 0.0f, dim);
		UiWindow::Frame(ui.sprite, white, x, y, w, h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
	}
	else ui.sprite->DrawSprite(white, 0, 0, sw, sh, 0.0f, dim);

	// 攻撃範囲の説明ページ：危険マークを中央に大きく見せる
	if (pg.showHitmark)
	{
		auto hm = TextureManager::Get("ui_hitmark");
		if (hm)
		{
			float sz = 120.0f;
			ui.sprite->DrawSprite(hm, sw / 2.0f - sz / 2.0f, sh * 0.40f - sz / 2.0f,
				sz, sz, 0.0f, XMFLOAT4(1, 1, 1, 1));
		}
	}

	// テキストボックス（対象が上なら下側、下なら上側に置く）
	float bw = 560.0f, tw = bw - 50.0f, bx = sw / 2.0f - bw / 2.0f;
	float bh = max(150.0f, 25.0f + ui.text->Measure(pg.text.c_str(), 22.0f, FontRole::Body, tw).height + 50.0f);   // 本文＋「クリックで次へ」
	float by = (pg.hy + pg.hh * 0.5f < sh * 0.5f) ? sh - bh - 60.0f : 80.0f;
	UiWindow::Draw(ui.sprite, white, bx - 6, by - 6, bw + 12, bh + 12);
	ui.sprite->End();

	ui.text->Begin();
	ui.text->DrawTextBox(pg.text.c_str(), bx + 25.0f, by + 25.0f, tw, 22.0f, D2D1::ColorF(1, 1, 1));
	wchar_t pgn[32]; swprintf_s(pgn, L"%d / %d", m_page + 1, (int)m_pages.size());
	ui.text->DrawText(pgn, bx + bw - 80.0f, by + 10.0f, 16.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	bool last = (m_page == (int)m_pages.size() - 1);
	ui.text->DrawText(last ? L"クリックで閉じる" : L"クリックで次へ",
		bx + bw / 2.0f - 70.0f, by + bh - 34.0f, 18.0f, D2D1::ColorF(0.85f, 0.85f, 0.6f));
	ui.text->End();

	if (pg.stepsTip)
	{
		float tx = pg.hx - 20.0f, ty = pg.hy + pg.hh + 10.0f, tw = 320.0f, th = 60.0f;
		ui.sprite->Begin();
		ui.sprite->DrawSprite(white, tx, ty, tw, th, 0.0f, XMFLOAT4(0.05f, 0.05f, 0.1f, 0.98f));
		ui.sprite->End();
		ui.text->Begin();
		ui.text->DrawText(L"歩数", tx + 10.0f, ty + 6.0f, 16.0f, D2D1::ColorF(1, 0.9f, 0.6f));
		ui.text->DrawText(L"0を超えて進むと敵が強くなる", tx + 10.0f, ty + 30.0f, 13.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
		ui.text->End();
	}
}

