#include "RestPanel.h"
#include "PlayerDataManager.h"
#include "RelicManager.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "Telemetry.h"
#include <string>

int RestPanel::HealAmount()
{
	return REST_HEAL + RelicManager::SumValue("restHeal");
}

URect RestPanel::BtnRect(int i) const
{
	const float w = 260.0f, h = 60.0f;
	return { m_sw / 2.0f - w / 2.0f, m_sh / 2.0f - 110.0f + i * 80.0f, w, h };
}

RestPanel::Action RestPanel::HandleInput(UiContext& ui, bool backHit)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	if (ui.input->GetMouseButtonTrigger(0)) m_armed = true;   // 押下が休憩画面内で始まった時だけ武装
	if (!m_armed || !ui.input->GetMouseButtonRelease(0)) return Action::None;
	m_armed = false;
	if (backHit) return Action::Finish;   // 戻る＝休憩を終える

	POINT m = ui.input->GetMousePos();
	for (int i = 0; i < 2; i++)
	{
		if (!BtnRect(i).has(m)) continue;
		Audio::PlaySE("Assets/Sound/se/click.mp3");
		Telemetry::Instance().Log("rest_choice", { {"choice", i == 0 ? "heal" : "upgrade"} });
		if (i == 0)   // 回復
		{
			auto& pd = PlayerDataManager::GetData();
			pd.hp += HealAmount();
			if (pd.hp > pd.maxHp) pd.hp = pd.maxHp;
			return Action::Finish;
		}
		return Action::Upgrade;   // 強化（デッキの強化モードを流用）
	}
	return Action::None;
}

void RestPanel::Draw(UiContext& ui, bool showTips)
{
	m_sw = ui.screenW; m_sh = ui.screenH;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = ui.input->GetMousePos();
	const int heal = HealAmount();
	const std::wstring labels[2] = { L"回復 +" + std::to_wstring(heal), L"カードを強化" };
	const std::wstring descs[2] = { L"HPを" + std::to_wstring(heal) + L"回復する", L"手持ちカード1枚を選んで強化する" };
	const XMFLOAT4 base[2] = {
		XMFLOAT4(0.25f, 0.45f, 0.30f, 1.0f),    // 回復
		XMFLOAT4(0.50f, 0.45f, 0.20f, 1.0f) };  // 強化

	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, UiContext::BAR_H, (float)m_sw, (float)m_sh - UiContext::BAR_H, 0.0f,
		XMFLOAT4(0.0f, 0.0f, 0.05f, 0.9f));
	for (int i = 0; i < 2; i++)
	{
		URect r = BtnRect(i);
		bool hov = r.has(mp);
		UiWindow::Draw(ui.sprite, white, r.x, r.y, r.w, r.h);
		const XMFLOAT4& c = base[i];
		ui.sprite->DrawSprite(white, r.x + 6, r.y + 6, r.w - 12, r.h - 12, 0.0f,
			XMFLOAT4(c.x, c.y, c.z, hov ? 0.6f : 0.4f));   // 種類色を中に薄く
	}
	ui.sprite->End();

	ui.text->Begin();
	ui.text->DrawTextCentered(L"休憩", m_sw / 2.0f, m_sh / 2.0f - 190.0f, 300.0f, 32.0f, D2D1::ColorF(1, 1, 1));
	for (int i = 0; i < 2; i++)
	{
		URect r = BtnRect(i);
		ui.text->DrawText(labels[i].c_str(), r.x + 20.0f, r.y + 18.0f, 22.0f, D2D1::ColorF(1, 1, 1));
	}
	ui.text->End();

	// ホバーで各行動の説明ウィンドウ
	if (!showTips) return;
	for (int i = 0; i < 2; i++)
	{
		URect r = BtnRect(i);
		if (!r.has(mp)) continue;
		float tw = 300.0f, th = 34.0f, tx = r.x + r.w + 12.0f, ty = r.y + (r.h - th) / 2.0f;
		if (tx + tw > m_sw - 6.0f) tx = r.x - tw - 12.0f;   // 右に出ないなら左へ
		ui.sprite->Begin();
		UiWindow::Draw(ui.sprite, white, tx, ty, tw, th);
		ui.sprite->End();
		ui.text->Begin();
		ui.text->DrawText(descs[i].c_str(), tx + 12.0f, ty + 8.0f, 15.0f, D2D1::ColorF(0.95f, 0.95f, 0.95f));
		ui.text->End();
		break;
	}
}
