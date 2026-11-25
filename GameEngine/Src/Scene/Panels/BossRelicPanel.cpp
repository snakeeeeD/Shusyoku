#include "BossRelicPanel.h"
#include "PlayerDataManager.h"
#include "RelicManager.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "Telemetry.h"
#include "GameUtils.h"

// クリック：選ぶ（取得時の効果を適用して保存）／受け取らない
void BossRelicPanel::HandleInput(UiContext& ui)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (ui.input->GetMouseButtonTrigger(0))
	{
		auto& pd = PlayerDataManager::GetData();
		POINT m = ui.input->GetMousePos();
		int n = (int)pd.bossRelicChoices.size();
		for (int i = 0; i < n; i++)
			if (CardRect(i, n).has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				std::string offered;
				for (auto& c : pd.bossRelicChoices) { if (!offered.empty()) offered += ","; offered += c; }
				Telemetry::Instance().Log("boss_relic_pick", { {"chosen", pd.bossRelicChoices[i]}, {"offered", offered} });
				std::string id = pd.bossRelicChoices[i];
				pd.bossRelicChoices.clear();
				PlayerDataManager::AddRelic(id);   // 取得時の効果（最大HP・歩数、代償も）を適用して保存
				return;
			}
		if (SkipRect().has(m))
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			Telemetry::Instance().Log("boss_relic_pick", { {"chosen", "(skip)"} });
			pd.bossRelicChoices.clear();
			PlayerDataManager::Save();
		}
	}
}

void BossRelicPanel::Draw(UiContext& ui)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	auto& choices = PlayerDataManager::GetData().bossRelicChoices;
	int n = (int)choices.size();
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = ui.input->GetMousePos();
	const float bnW = 520.0f, bnH = 90.0f, bnX = m_sw / 2.0f - bnW / 2.0f, bnY = 64.0f;

	// ---- スプライト ----
	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.92f));
	ui.sprite->DrawSprite(TextureManager::Get("ui_banner"), bnX, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	int hovIdx = -1;
	for (int i = 0; i < n; i++)
	{
		URect r = CardRect(i, n);
		char key[16]; sprintf_s(key, "boss%d", i);
		bool hov = UiHover(r.x, r.y, r.w, r.h, mp, key, m_lastHoverKey);
		if (hov) hovIdx = i;
		float y = hov ? r.y - 6.0f : r.y;
		UiWindow::Draw(ui.sprite, white, r.x, y, r.w, r.h);
		if (hov) UiWindow::Frame(ui.sprite, white, r.x, y, r.w, r.h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
		// アイコン（個別アイコンがあればボスの色で染める。無ければ台座の円）
		const RelicDef* rdef = RelicManager::Get(choices[i]);
		auto icon = (rdef && !rdef->icon.empty()) ? TextureManager::Get(rdef->icon) : nullptr;
		ui.sprite->DrawSprite(icon ? icon : TextureManager::Get("particle"), r.x + r.w / 2.0f - 44.0f, y + 22.0f, 88.0f, 88.0f, 0.0f,
			icon ? XMFLOAT4(0.95f, 0.4f, 0.4f, 1.0f) : XMFLOAT4(0.9f, 0.3f, 0.3f, 0.35f));
	}
	URect sk = SkipRect();
	bool skHov = UiHover(sk.x, sk.y, sk.w, sk.h, mp, "bossSkip", m_lastHoverKey);
	UiWindow::Button(ui.sprite, white, sk.x, skHov ? sk.y - 3.0f : sk.y, sk.w, sk.h,
		skHov ? XMFLOAT4(0.4f, 0.4f, 0.5f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	ui.sprite->End();

	// ---- 文字 ----
	ui.text->Begin();
	ui.text->DrawOutlinedTextCentered(L"ボスのレリックを1つ選ぶ", bnX + bnW / 2.0f, bnY, bnW, bnH, 28.0f,
		D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	for (int i = 0; i < n; i++)
	{
		const RelicDef* d = RelicManager::Get(choices[i]);
		if (!d) continue;
		URect r = CardRect(i, n);
		float y = r.has(mp) ? r.y - 6.0f : r.y;
		ui.text->DrawTextCentered(ToWString(d->name).c_str(), r.x + r.w / 2.0f, y + 124.0f, r.w, 24.0f,
			D2D1::ColorF(1.0f, 0.8f, 0.75f));

		// 説明：行ごとに折り返し、「代償」で始まる行は赤
		std::wstring desc = ToWString(d->desc);
		float ly = y + 168.0f;
		size_t s0 = 0;
		while (true)
		{
			size_t e = desc.find(L'\n', s0);
			std::wstring line = desc.substr(s0, e == std::wstring::npos ? std::wstring::npos : e - s0);
			bool cost = (line.rfind(L"代償", 0) == 0);
			std::wstring wrapped = CardVisual::WrapText(line, 15);
			int lines = 1; for (wchar_t c : wrapped) if (c == L'\n') lines++;
			ui.text->DrawTextCentered(wrapped.c_str(), r.x + r.w / 2.0f, ly, r.w - 30.0f, 16.0f,
				cost ? D2D1::ColorF(1.0f, 0.45f, 0.45f) : D2D1::ColorF(0.92f, 0.92f, 0.92f), lines * 22.0f);
			ly += lines * 22.0f + 8.0f;
			if (e == std::wstring::npos) break;
			s0 = e + 1;
		}
	}
	ui.text->DrawTextCentered(L"受け取らない", sk.x + sk.w / 2.0f, skHov ? sk.y - 3.0f : sk.y, sk.w, 18.0f,
		D2D1::ColorF(1, 1, 1), sk.h);
	ui.text->End();

	// ホバー中のレリックが代償でカードを加えるなら、その実物を窓の右に並べる
	// （隣の窓の文字より手前に出したいので、全部描いた後に別のパスで描く）
	if (hovIdx >= 0)
	{
		const RelicDef* hd = RelicManager::Get(choices[hovIdx]);
		const CardData* ac = (hd && !hd->AddedCard().empty()) ? CardDataBase::Get(hd->AddedCard()) : nullptr;
		if (ac)
		{
			const float S = 1.2f;
			URect r = CardRect(hovIdx, n);
			float cx = r.x + r.w + 24.0f, cy = r.y + 60.0f;   // カードの基準位置
			ui.sprite->Begin();
			CardVisual::DrawBase(ui.sprite, white, cx, cy, S, 0.0f, CardVisual::GetCardColor(ac->type, false), ac, ui.time);
			ui.sprite->End();
			ui.text->Begin();
			CardVisual::DrawTexts(ui.text, ac, nullptr, cx, cy, S, 0.0f, 1.0f);
			float x, y, w, h; CardVisual::GetRect(cx, cy, S, x, y, w, h);
			ui.text->DrawOutlinedTextCentered(L"代償で加わる", x + w / 2.0f, y - 30.0f, w + 40.0f, 24.0f, 16.0f,
				D2D1::ColorF(1.0f, 0.45f, 0.45f), D2D1::ColorF(0, 0, 0), 1.5f);
			ui.text->End();
		}
	}
}

