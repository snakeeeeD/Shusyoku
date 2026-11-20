#include "EffectManager.h"
#include "EffectDataBase.h"
#include "Renderer3D.h"
#include "ScreenShake.h"
#include "Se.h"
#include <algorithm>
#include <cstdlib>

int EffectManager::Play(const std::string& id, float x, float y, float z, float rot, float delay, XMFLOAT4 tint, float scale)
{
    int handle = s_nextHandle++;
    if (delay > 0.0f)
    {
        s_pending.push_back({ id, x, y, z, rot, tint, scale, delay });
        return handle;
    }
    SpawnAt at = s_cam;
    at.x = x; at.y = y; at.z = z; at.rot = rot; at.tint = tint; at.scale = scale;
    SpawnParts(id, at, handle);
    return handle;
}

int EffectManager::PlayFrom(const std::string& id, const XMFLOAT3& from, const XMFLOAT3& to)
{
    int handle = s_nextHandle++;
    SpawnAt at = s_cam;
    at.x = to.x; at.y = to.y; at.z = to.z;
    at.hasFrom = true; at.from = from;
    SpawnParts(id, at, handle);
    return handle;
}

// 部品を全部出す（Play と PlayFrom の共通部分）
void EffectManager::SpawnParts(const std::string& id, SpawnAt at, int handle)
{
    const EffectDef* def = EffectDataBase::Get(id);
    if (!def) return;
    Se::Play(def->se);   // effects.json の "se"（時間差のときは、出るときに鳴る＝見た目と同時）
    if (def->shake > 0.0f) ScreenShake::Add(def->shake);

    at.mirror = def->randomSide && (rand() & 1);   // 左右は1回だけ決めて、全部の部品に同じ向きを渡す
    for (auto& partDef : def->parts)   // 種類を気にせず、データに「出して」と頼む
    {
        std::unique_ptr<EffectPart> part = partDef->Spawn(at);
        part->handle = handle;
        s_parts.push_back(std::move(part));
    }
    if (!def->then.empty())
        Play(def->then, at.x, at.y, at.z, at.rot, def->thenDelay, at.tint, at.scale);   // 着弾後に連鎖（飛び始めは渡さない）
}

void EffectManager::Stop(int handle, float fadeSec)
{
    if (handle == 0) return;
    for (auto& p : s_parts)
        if (p->handle == handle) p->Stop(fadeSec);
}

void EffectManager::SetEnds(int handle, const XMFLOAT3& from, const XMFLOAT3& to)
{
    if (handle == 0) return;
    for (auto& p : s_parts)
        if (p->handle == handle) p->SetEnds(from, to);
}

void EffectManager::Update(float dt)
{
    for (size_t i = 0; i < s_pending.size(); )   // 時間差の予約：時間が来たら出す
    {
        s_pending[i].delay -= dt;
        if (s_pending[i].delay <= 0.0f)
        {
            PendingPlay p = s_pending[i];
            s_pending[i] = s_pending.back(); s_pending.pop_back();
            Play(p.id, p.x, p.y, p.z, p.rot, 0.0f, p.tint, p.scale);
        }
        else ++i;
    }
    for (size_t i = 0, n = s_parts.size(); i < n; ++i)   // 番号で回す：Update の中から Play してよい（増えた演出は次のフレームから動く）
        s_parts[i]->Update(dt);
    s_parts.erase(std::remove_if(s_parts.begin(), s_parts.end(),
        [](const std::unique_ptr<EffectPart>& p) { return p->IsDone(); }), s_parts.end());
}

void EffectManager::Draw(Renderer3D* r, EffectLayer layer)
{
    XMMATRIX inv = XMMatrixInverse(nullptr, r->GetViewMatrix());   // 逆ビュー＝カメラの姿勢
    XMStoreFloat3(&s_cam.camRight, inv.r[0]);
    XMStoreFloat3(&s_cam.camUp, inv.r[1]);
    XMStoreFloat3(&s_cam.camFwd, inv.r[2]);
    XMStoreFloat3(&s_cam.camPos, inv.r[3]);
    s_cam.hasCam = true;
    for (auto& p : s_parts) p->Draw(r, layer);   // どの順番で何を描くかは、種類が決める
}

void EffectManager::Clear()
{
    s_parts.clear();
    s_pending.clear();
}
