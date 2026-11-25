#include "SettingsPanel.h"
#include "Settings.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"

void SettingsPanel::Close()
{
	m_open = false;
	m_confirm = 0;
	m_dragSlider = -1;
	Settings::Save();
}

// 窓の各部の位置（描画とクリックで共有）
SettingsPanel::Layout SettingsPanel::MakeLayout(const UiContext& ui) const
{
	Layout u{};
	float pw = 440.0f, ph = m_canTitle ? 480.0f : 438.0f;   // 「タイトルに戻る」の分だけ縦に伸ばす
	float px = (ui.screenW - pw) / 2.0f, py = (ui.screenH - ph) / 2.0f;
	u.panel = { px, py, pw, ph };
	u.disp = { px + 180.0f, py + 54.0f, 200.0f, 34.0f };
	u.bgmTrack = { px + 180.0f, py + 120.0f, 200.0f, 14.0f };
	u.seTrack = { px + 180.0f, py + 168.0f, 200.0f, 14.0f };
	u.shake = { px + 180.0f, py + 208.0f, 120.0f, 34.0f };
	u.aimCam = { px + 180.0f, py + 250.0f, 120.0f, 34.0f };
	float by = py + 292.0f;
	u.glossary = { px + pw / 2.0f - 90.0f, by, 180.0f, 34.0f }; by += 42.0f;   // 用語集
	if (m_canTitle) { u.title = { px + pw / 2.0f - 90.0f, by, 180.0f, 34.0f }; by += 42.0f; }   // タイトルに戻る
	else            u.title = { -1000.0f, -1000.0f, 0.0f, 0.0f };                             // 画面外（押せない）
	u.gameEnd = { px + pw / 2.0f - 90.0f, by, 180.0f, 34.0f };
	u.close = { px + pw / 2.0f - 70.0f, py + ph - 52.0f, 140.0f, 38.0f };
	// 確認ダイアログ（はい/いいえ）
	float cw = 360.0f, ch = 170.0f;
	float ccx = (ui.screenW - cw) / 2.0f, ccy = (ui.screenH - ch) / 2.0f;
	u.cfmPanel = { ccx, ccy, cw, ch };
	u.cfmYes = { ccx + 40.0f, ccy + ch - 60.0f, 120.0f, 40.0f };
	u.cfmNo = { ccx + cw - 160.0f, ccy + ch - 60.0f, 120.0f, 40.0f };
	return u;
}

SettingsPanel::Action SettingsPanel::HandleInput(UiContext& ui)
{
	POINT m = ui.input->GetMousePos();
	bool click = ui.input->GetMouseButtonTrigger(0);
	Layout su = MakeLayout(ui);

	// 確認ダイアログ表示中は はい/いいえ だけ受け付ける
	if (m_confirm != 0)
	{
		if (click)
		{
			if (su.cfmYes.has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				if (m_confirm == 1) { m_confirm = 0; return Action::Quit; }
				if (m_confirm == 2) { Close(); return Action::ReturnToTitle; }
				m_confirm = 0;
			}
			else if (su.cfmNo.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_confirm = 0; }
		}
		return Action::None;
	}
	auto& s = Settings::Get();
	if (ui.input->GetMouseButtonTrigger(0))
	{
		URect bt = su.bgmTrack; bt.y -= 8; bt.h += 16;   // 掴みやすく
		URect st = su.seTrack;  st.y -= 8; st.h += 16;
		if (bt.has(m)) m_dragSlider = 0; else if (st.has(m)) m_dragSlider = 1;
	}
	if (ui.input->GetMouseButtonRelease(0)) { if (m_dragSlider != -1) Settings::Save(); m_dragSlider = -1; }
	if (m_dragSlider == 0 || m_dragSlider == 1)
	{
		const URect& t = (m_dragSlider == 0) ? su.bgmTrack : su.seTrack;
		float v = (m.x - t.x) / t.w; if (v < 0) v = 0; if (v > 1) v = 1;
		if (m_dragSlider == 0) { s.bgmVolume = v; Audio::SetBgmVolume(v); }
		else { s.seVolume = v;  Audio::SetSeMasterVolume(v); }
	}
	if (click)
	{
		if (su.disp.has(m))
		{
			DisplayMode nx = (s.displayMode == DisplayMode::Borderless) ? DisplayMode::Windowed : DisplayMode::Borderless;
			SetDisplayMode(nx); Audio::PlaySE("Assets/Sound/se/click.mp3");
		}
		else if (su.shake.has(m)) { s.screenShake = !s.screenShake; Settings::Save(); Audio::PlaySE("Assets/Sound/se/click.mp3"); }
		else if (su.aimCam.has(m)) { s.aimCamera = !s.aimCamera; Settings::Save(); Audio::PlaySE("Assets/Sound/se/click.mp3"); }
		else if (su.glossary.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); return Action::OpenGlossary; }
		else if (m_canTitle && su.title.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_confirm = 2; }
		else if (su.gameEnd.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_confirm = 1; }
		else if (su.close.has(m) || !su.panel.has(m)) { Close(); Audio::PlaySE("Assets/Sound/se/click.mp3"); }
	}
	return Action::None;
}

void SettingsPanel::Draw(UiContext& ui)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	Layout u = MakeLayout(ui);
	// 描く位置はホバーで少し持ち上げる（判定と色は元の u のまま）
	Layout d = u;
	for (URect* r : { &d.disp, &d.shake, &d.aimCam, &d.glossary, &d.title, &d.gameEnd, &d.close, &d.cfmYes, &d.cfmNo })
		*r = ui.Lifted(*r);
	POINT mp = ui.input->GetMousePos();
	auto& s = Settings::Get();

	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, 0, (float)ui.screenW, (float)ui.screenH, 0.0f, XMFLOAT4(0, 0, 0, 0.6f)); // 暗幕
	UiWindow::Draw(ui.sprite, white, u.panel.x, u.panel.y, u.panel.w, u.panel.h);

	UiWindow::Button(ui.sprite, white, d.disp.x, d.disp.y, d.disp.w, d.disp.h,
		u.disp.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));

	auto slider = [&](const URect& t, float val) {
		ui.sprite->DrawSprite(white, t.x, t.y, t.w, t.h, 0.0f, XMFLOAT4(0.15f, 0.15f, 0.2f, 1));      // 溝
		ui.sprite->DrawSprite(white, t.x, t.y, t.w * val, t.h, 0.0f, XMFLOAT4(0.3f, 0.6f, 0.9f, 1));  // 塗り
		ui.sprite->DrawSprite(white, t.x + t.w * val - 5.0f, t.y - 5.0f, 10.0f, t.h + 10.0f, 0.0f, XMFLOAT4(0.9f, 0.9f, 1.0f, 1)); // つまみ
		};
	slider(u.bgmTrack, s.bgmVolume);
	slider(u.seTrack, s.seVolume);

	UiWindow::Button(ui.sprite, white, d.shake.x, d.shake.y, d.shake.w, d.shake.h,
		s.screenShake ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1) : XMFLOAT4(0.45f, 0.3f, 0.3f, 1));
	UiWindow::Button(ui.sprite, white, d.aimCam.x, d.aimCam.y, d.aimCam.w, d.aimCam.h,
		s.aimCamera ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1) : XMFLOAT4(0.45f, 0.3f, 0.3f, 1));
	UiWindow::Button(ui.sprite, white, d.glossary.x, d.glossary.y, d.glossary.w, d.glossary.h,
		u.glossary.has(mp) ? XMFLOAT4(0.55f, 0.45f, 0.25f, 1) : XMFLOAT4(0.4f, 0.33f, 0.2f, 1));
	UiWindow::Button(ui.sprite, white, d.title.x, d.title.y, d.title.w, d.title.h,
		u.title.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	UiWindow::Button(ui.sprite, white, d.gameEnd.x, d.gameEnd.y, d.gameEnd.w, d.gameEnd.h,
		u.gameEnd.has(mp) ? XMFLOAT4(0.75f, 0.30f, 0.30f, 1) : XMFLOAT4(0.50f, 0.24f, 0.24f, 1));
	UiWindow::Button(ui.sprite, white, d.close.x, d.close.y, d.close.w, d.close.h,
		u.close.has(mp) ? XMFLOAT4(0.5f, 0.35f, 0.35f, 1) : XMFLOAT4(0.35f, 0.25f, 0.25f, 1));
	ui.sprite->End();

	ui.text->Begin();
	// 文字はボタンの枠の中央に（手打ちのオフセットだと文字の幅でずれる）
	auto inBtn = [&](const wchar_t* t, const URect& r, float size, D2D1_COLOR_F c) {
		ui.text->DrawTextCentered(t, r.x + r.w / 2.0f, r.y, r.w, size, c, r.h);
		};
	// 左の項目名は左揃えのまま、その行の縦の中央に
	auto rowLabel = [&](const wchar_t* t, const URect& row, float size) {
		float cy = row.y + row.h / 2.0f;
		float maxW = row.x - (u.panel.x + 40.0f) - 12.0f;            // ボタンの手前まで
		float w = ui.text->Measure(t, size, FontRole::Body).width;
		if (w > maxW) size *= maxW / w;                               // 入りきらない時だけ縮める
		ui.text->DrawText(t, u.panel.x + 40.0f, cy - size * 0.62f, size, D2D1::ColorF(1, 1, 1));
		};
	const D2D1_COLOR_F W = D2D1::ColorF(1, 1, 1);

	inBtn(L"設定", { u.panel.x, u.panel.y + 10.0f, u.panel.w, 36.0f }, 26, D2D1::ColorF(1, 0.9f, 0.6f));

	rowLabel(L"表示", u.disp, 20);
	inBtn(s.displayMode == DisplayMode::Borderless ? L"全画面" : L"ウィンドウ", d.disp, 20, W);
	rowLabel(L"BGM", u.bgmTrack, 20);
	rowLabel(L"SE", u.seTrack, 20);
	rowLabel(L"画面シェイク", u.shake, 18);
	inBtn(s.screenShake ? L"ON" : L"OFF", d.shake, 20, W);
	rowLabel(L"移動のカメラ追従", u.aimCam, 18);
	inBtn(s.aimCamera ? L"ON" : L"OFF", d.aimCam, 20, W);
	inBtn(L"用語集", d.glossary, 20, W);
	inBtn(L"タイトルに戻る", d.title, 20, W);
	inBtn(L"ゲーム終了", d.gameEnd, 20, D2D1::ColorF(1, 0.9f, 0.9f));
	inBtn(L"閉じる", d.close, 20, W);

	// 音量の%（つまみの右、行の縦の中央）
	wchar_t b[16];
	auto pct = [&](float v, const URect& t) {
		swprintf_s(b, L"%d%%", (int)(v * 100 + 0.5f));
		ui.text->DrawText(b, t.x + t.w + 14.0f, t.y + t.h / 2.0f - 16 * 0.62f, 16, D2D1::ColorF(0.8f, 0.9f, 1));
		};
	pct(s.bgmVolume, u.bgmTrack);
	pct(s.seVolume, u.seTrack);
	ui.text->End();

	if (m_confirm != 0)
	{
		ui.sprite->Begin();
		ui.sprite->DrawSprite(white, 0, 0, (float)ui.screenW, (float)ui.screenH, 0.0f, XMFLOAT4(0, 0, 0, 0.5f));
		UiWindow::Draw(ui.sprite, white, u.cfmPanel.x, u.cfmPanel.y, u.cfmPanel.w, u.cfmPanel.h);
		UiWindow::Button(ui.sprite, white, d.cfmYes.x, d.cfmYes.y, d.cfmYes.w, d.cfmYes.h,
			u.cfmYes.has(mp) ? XMFLOAT4(0.75f, 0.30f, 0.30f, 1) : XMFLOAT4(0.50f, 0.24f, 0.24f, 1));
		UiWindow::Button(ui.sprite, white, d.cfmNo.x, d.cfmNo.y, d.cfmNo.w, d.cfmNo.h,
			u.cfmNo.has(mp) ? XMFLOAT4(0.35f, 0.45f, 0.6f, 1) : XMFLOAT4(0.25f, 0.30f, 0.42f, 1));
		ui.sprite->End();

		ui.text->Begin();
		const wchar_t* msg = (m_confirm == 1) ? L"ゲームを終了しますか？" : L"タイトルに戻りますか？";
		ui.text->DrawTextCentered(msg, u.cfmPanel.x + u.cfmPanel.w / 2.0f, u.cfmPanel.y + 30.0f, u.cfmPanel.w, 22, D2D1::ColorF(1, 1, 1), 50.0f);
		ui.text->DrawTextCentered(L"はい", d.cfmYes.x + d.cfmYes.w / 2.0f, d.cfmYes.y, d.cfmYes.w, 20, D2D1::ColorF(1, 0.9f, 0.9f), d.cfmYes.h);
		ui.text->DrawTextCentered(L"いいえ", d.cfmNo.x + d.cfmNo.w / 2.0f, d.cfmNo.y, d.cfmNo.w, 20, D2D1::ColorF(1, 1, 1), d.cfmNo.h);
		ui.text->End();
	}
}
