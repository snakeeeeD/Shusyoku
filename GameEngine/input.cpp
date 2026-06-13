#include "input.h"


Input input;

int Input::s_wheelDelta = 0;

//コンストラクタ
Input::Input()
{
	VibrationTime = 0;
}

//デストラクタ
Input::~Input()
{
	//振動を終了させる
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = 0;
	vibration.wRightMotorSpeed = 0;
	XInputSetState(0, &vibration);
}

void Input::Update()
{
	m_currentWheelDelta = s_wheelDelta;
	s_wheelDelta = 0;

	// 1フレーム前の入力保存
	for (int i = 0; i < 256; i++) keyState_old[i] = keyState[i];
	controllerState_old = controllerState;

	// キーボード
	GetKeyboardState(keyState);

	// ---- Pad index 自動検出 ----
	m_padIndex = -1;
	ZeroMemory(&controllerState, sizeof(XINPUT_STATE));

	for (int i = 0; i < 4; i++)
	{
		XINPUT_STATE state{};
		if (XInputGetState(i, &state) == ERROR_SUCCESS)
		{
			m_padIndex = i;
			controllerState = state;
			break;
		}
	}

	for (int i = 0; i < 3; i++)
		mouseState_old[i] = mouseState[i];

	mouseState[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	mouseState[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	mouseState[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

	// ---- 振動時間管理 ----
	if (VibrationTime > 0)
	{
		VibrationTime--;
		if (VibrationTime == 0 && m_padIndex != -1)
		{
			XINPUT_VIBRATION vibration{};
			XInputSetState(m_padIndex, &vibration);
		}
	}
}

//キー入力
bool Input::GetKeyPress(int key) //プレス
{
	return keyState[key] & 0x80;
}
bool Input::GetKeyTrigger(int key) //トリガー
{
	return (keyState[key] & 0x80) && !(keyState_old[key] & 0x80);
}
bool Input::GetKeyRelease(int key) //リリース
{
	return !(keyState[key] & 0x80) && (keyState_old[key] & 0x80);
}

//左アナログスティック
DirectX::XMFLOAT2 Input::GetLeftAnalogStick(void)
{
	SHORT x = controllerState.Gamepad.sThumbLX; // -32768～32767
	SHORT y = controllerState.Gamepad.sThumbLY; // -32768～32767

	DirectX::XMFLOAT2 res;
	res.x = x / 32767.0f; //-1～1
	res.y = y / 32767.0f; //-1～1
	return res;
}
//右アナログスティック
DirectX::XMFLOAT2 Input::GetRightAnalogStick(void)
{
	SHORT x = controllerState.Gamepad.sThumbRX; // -32768～32767
	SHORT y = controllerState.Gamepad.sThumbRY; // -32768～32767

	DirectX::XMFLOAT2 res;
	res.x = x / 32767.0f; //-1～1
	res.y = y / 32767.0f; //-1～1
	return res;
}

//左トリガー
float Input::GetLeftTrigger(void)
{
	BYTE t = controllerState.Gamepad.bLeftTrigger; // 0～255
	return t / 255.0f;
}
//右トリガー
float Input::GetRightTrigger(void)
{
	BYTE t = controllerState.Gamepad.bRightTrigger; // 0～255
	return t / 255.0f;
}

//ボタン入力
bool Input::GetButtonPress(WORD btn) //プレス
{
	return (controllerState.Gamepad.wButtons & btn) != 0;
}
bool Input::GetButtonTrigger(WORD btn) //トリガー
{
	return (controllerState.Gamepad.wButtons & btn) != 0 && (controllerState_old.Gamepad.wButtons & btn) == 0;
}
bool Input::GetButtonRelease(WORD btn) //リリース
{
	return (controllerState.Gamepad.wButtons & btn) == 0 && (controllerState_old.Gamepad.wButtons & btn) != 0;
}

//振動
void Input::SetVibration(int frame, float powor)
{
	// XINPUT_VIBRATION構造体のインスタンスを作成
	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));

	// モーターの強度を設定（0～65535）
	vibration.wLeftMotorSpeed = (WORD)(powor * 65535.0f);
	vibration.wRightMotorSpeed = (WORD)(powor * 65535.0f);
	XInputSetState(0, &vibration);

	//振動継続時間を代入
	VibrationTime = frame;
}

POINT Input::GetMousePos() const
{
	POINT p;
	GetCursorPos(&p);

	// ウィンドウ座標に変換
	ScreenToClient(m_hWnd, &p);

	// ウィンドウの実際のサイズを取得
	RECT rect;
	GetClientRect(m_hWnd, &rect);
	float windowW = (float)(rect.right - rect.left);
	float windowH = (float)(rect.bottom - rect.top);

	// 1280x720 基準に変換
	p.x = (LONG)(p.x * 1280.0f / windowW);
	p.y = (LONG)(p.y * 720.0f / windowH);
	return p;
}

bool Input::GetMouseButtonPress(int button)
{
	return (GetAsyncKeyState(button) & 0x8000) != 0;
}

bool Input::GetMouseButtonTrigger(int button)
{
	return mouseState[button] && !mouseState_old[button];
}

bool Input::GetMouseButtonRelease(int button)
{
	return !mouseState[button] && mouseState_old[button];
}
