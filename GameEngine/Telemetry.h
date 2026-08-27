#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <utility>
#include <fstream>
#include <sstream>
#include <ctime>
#include <winhttp.h>
#include <cstdlib>
#include <thread>
#pragma comment(lib, "winhttp.lib")

// テレメトリ値（int/double/bool/文字列を1つに包む）
struct TVal {
    enum Type { Int, Dbl, Bool, Str } type;
    long long i = 0; double d = 0; bool b = false; std::string s;
    TVal(int v) : type(Int), i(v) {}
    TVal(long long v) : type(Int), i(v) {}
    TVal(unsigned v) : type(Int), i((long long)v) {}
    TVal(double v) : type(Dbl), d(v) {}
    TVal(float v) : type(Dbl), d((double)v) {}
    TVal(bool v) : type(Bool), b(v) {}
    TVal(const char* v) : type(Str), s(v ? v : "") {}
    TVal(const std::string& v) : type(Str), s(v) {}
    TVal(const std::wstring& v) : type(Str), s(ToUtf8(v)) {}

    static std::string ToUtf8(const std::wstring& w) {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }
};
using TFields = std::vector<std::pair<std::string, TVal>>;

// ゲーム内データ収集ロガー。JSONL＋人間可読テキストを logs/ に同時出力
class Telemetry {
public:
    static Telemetry& Instance() { static Telemetry t; return t; }

    void Init(const std::string& build) {
        m_build = build;

        // exeのあるフォルダを基準にする（起動元に依存しない）
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string dir(exePath);
        size_t slash = dir.find_last_of("\\/");
        std::string logDir = (slash != std::string::npos ? dir.substr(0, slash + 1) : "") + "logs";
        CreateDirectoryA(logDir.c_str(), nullptr);

        std::string ts = TimeStamp("%Y%m%d_%H%M%S");
        std::srand((unsigned)GetTickCount());
        char suf[8]; sprintf_s(suf, "_%04X", std::rand() & 0xFFFF);
        m_sessionId = ts + suf;                       // 例 20260828_010203_9F3A
        std::string base = logDir + "/session_" + m_sessionId;
        m_jsonl.open(base + ".jsonl", std::ios::app);
        m_txt.open(base + ".txt", std::ios::app);
        m_open = m_jsonl.is_open();
        Log("session_start", { {"build", build} });
    }

    void Shutdown() {
        if (!m_open) return;
        Log("session_end", {});
        Upload();              // = Upload(false) 同期。閉じる前に確実に送り切る
        m_jsonl.close(); m_txt.close();
        m_open = false;
    }

    // 自動付与されるコンテキスト（各Logに毎回くっつく）
    void SetRun(int runId) { m_runId = runId; }
    void SetLocation(int layer, int floor) { m_layer = layer; m_floor = floor; }
    void SetScene(const std::string& s) { m_scene = s; }

    void BeginRun() 
    { 
        m_runId++; m_runStart = std::time(nullptr); }
    long long RunSeconds() const {
        return m_runStart ? (long long)(std::time(nullptr) - m_runStart) : 0;
    }

    void Log(const std::string& event, const TFields& fields = {}) {
        if (!m_open) return;
        std::string clock = TimeStamp("%H:%M:%S");

        // JSONL
        std::ostringstream j;
        j << "{\"t\":\"" << clock << "\""
            << ",\"session\":\"" << m_sessionId << "\""
            << ",\"event\":\"" << Esc(event) << "\""
            << ",\"run\":" << m_runId
            << ",\"layer\":" << m_layer << ",\"floor\":" << m_floor
            << ",\"scene\":\"" << Esc(m_scene) << "\"";
        for (auto& [k, v] : fields) j << ",\"" << Esc(k) << "\":" << JsonVal(v);
        j << "}";
        m_jsonl << j.str() << "\n"; m_jsonl.flush();
        m_payload += j.str(); m_payload += "\n";       // ← 送信用バッファ

        // 人間可読
        std::ostringstream h;
        h << "[" << clock << "] L" << m_layer << "-" << m_floor
            << " " << m_scene << " | " << event;
        for (auto& [k, v] : fields) h << "  " << k << "=" << PlainVal(v);
        m_txt << h.str() << "\n"; m_txt.flush();
    }

    // async=true: 裏スレッドで送信（ゲームを止めない）／false: 同期（終了時に使う）
    void Upload(bool async = false) {
        if (!m_upload || m_payload.empty()) return;
        std::string body = "{\"session_id\":\"" + Esc(m_sessionId)
            + "\",\"payload\":\"" + Esc(m_payload) + "\"}";
        if (async) std::thread(PostBody, body).detach();
        else       PostBody(std::move(body));
    }

private:
    static std::string TimeStamp(const char* fmt) {
        std::time_t now = std::time(nullptr);
        std::tm tm; localtime_s(&tm, &now);
        char buf[64]; std::strftime(buf, sizeof(buf), fmt, &tm);
        return buf;
    }
    static std::string Esc(const std::string& s) {
        std::string o; o.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n";
            else if (c == '\t') o += "\\t";
            else o += c;
        }
        return o;
    }
    static std::string JsonVal(const TVal& v) {
        switch (v.type) {
        case TVal::Int:  return std::to_string(v.i);
        case TVal::Dbl: { std::ostringstream o; o << v.d; return o.str(); }
        case TVal::Bool: return v.b ? "true" : "false";
        default:         return "\"" + Esc(v.s) + "\"";
        }
    }
    static std::string PlainVal(const TVal& v) {
        switch (v.type) {
        case TVal::Int:  return std::to_string(v.i);
        case TVal::Dbl: { std::ostringstream o; o << v.d; return o.str(); }
        case TVal::Bool: return v.b ? "true" : "false";
        default:         return v.s;
        }
    }

    static std::wstring ToW(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    static void PostBody(std::string body) {
        const wchar_t* host = L"rjshabmjcrshzwsctesi.supabase.co";
        const wchar_t* path = L"/rest/v1/telemetry_sessions";
        const std::string apikey = "sb_publishable_J2Zgib1bHLMOGH6v7yQwjA_hypILsD7";

        HINTERNET hs = WinHttpOpen(L"SyusyokuTelemetry/1.0",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hs) return;
        WinHttpSetTimeouts(hs, 5000, 5000, 5000, 5000);
        HINTERNET hc = WinHttpConnect(hs, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hc) {
            HINTERNET hr = WinHttpOpenRequest(hc, L"POST", path, nullptr,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hr) {
                std::wstring headers =
                    L"Content-Type: application/json\r\n"
                    L"apikey: " + ToW(apikey) + L"\r\n"
                    L"Authorization: Bearer " + ToW(apikey) + L"\r\n"
                    L"Prefer: return=minimal\r\n";
                WinHttpSendRequest(hr, headers.c_str(), (DWORD)-1,
                    (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
                WinHttpReceiveResponse(hr, nullptr);
                WinHttpCloseHandle(hr);
            }
            WinHttpCloseHandle(hc);
        }
        WinHttpCloseHandle(hs);
    }

    bool m_open = false;
    std::ofstream m_jsonl, m_txt;
    std::string m_sessionId, m_build, m_scene;
    int m_runId = 0, m_layer = 0, m_floor = 0;

    std::string m_payload;      // 送信用にJSONL全文を保持
    bool m_upload = true;       // 自分のPCで送りたくない時は false に

    std::time_t m_runStart = 0;
};