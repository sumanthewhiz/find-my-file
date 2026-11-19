#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include "WarpContext.h"
#include "SearchRanking.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "advapi32.lib")

namespace WarpContext
{
    using namespace winrt::Windows::Data::Json;
    namespace
    {
        struct Handle
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            explicit Handle(HANDLE handle) : value(handle) {}
            ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
        };
        JsonObject Parse(const std::string& text)
        {
            if (text.empty() || text.size() > MaxMessageBytes) throw std::runtime_error("invalid_response");
            JsonObject value;
            if (!JsonObject::TryParse(winrt::to_hstring(text), value)) throw std::runtime_error("invalid_response");
            return value;
        }
        bool Bool(const JsonObject& value, const wchar_t* name) { return value.GetNamedBoolean(name); }
        double Number(const JsonObject& value, const wchar_t* name)
        {
            const double n = value.GetNamedNumber(name);
            if (!std::isfinite(n)) throw std::runtime_error("invalid_response");
            return n;
        }
        std::string Text(const JsonObject& value, const wchar_t* name, size_t limit = 128)
        {
            const auto s = winrt::to_string(value.GetNamedString(name));
            if (s.size() > limit) throw std::runtime_error("invalid_response");
            return s;
        }
        std::wstring BoundedText(std::wstring text, size_t chars)
        {
            if (text.size() > chars) text.resize(chars);
            if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF) text.pop_back();
            return text;
        }
        std::string ErrorCode(const JsonObject& root)
        {
            if (!root.HasKey(L"ok") || Bool(root, L"ok")) return {};
            const auto error = root.GetNamedObject(L"error");
            return Text(error, L"code", 64);
        }
        int64_t Expiry(const JsonObject& root, int64_t now)
        {
            const double n = Number(root, L"expires_ms");
            if (std::floor(n) != n || n <= double(now) || n > double(now + 60000))
                throw std::runtime_error("context_expired");
            return static_cast<int64_t>(n);
        }
        bool SameUser(HANDLE process)
        {
            HANDLE otherRaw = nullptr, selfRaw = nullptr;
            if (!OpenProcessToken(process, TOKEN_QUERY, &otherRaw)) return false;
            Handle other(otherRaw);
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &selfRaw)) return false;
            Handle self(selfRaw);
            auto sid = [](HANDLE token) {
                DWORD bytes = 0;
                GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
                std::vector<unsigned char> data(bytes);
                if (!bytes || !GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes)) data.clear();
                return data;
            };
            const auto a = sid(other.value), b = sid(self.value);
            return !a.empty() && !b.empty() && EqualSid(
                reinterpret_cast<const TOKEN_USER*>(a.data())->User.Sid,
                reinterpret_cast<const TOKEN_USER*>(b.data())->User.Sid);
        }
        bool Peer(HANDLE pipe, const std::wstring& expected)
        {
            ULONG pid = 0, session = 0; DWORD mine = 0;
            if (!GetNamedPipeServerProcessId(pipe, &pid) || !GetNamedPipeServerSessionId(pipe, &session) ||
                !ProcessIdToSessionId(GetCurrentProcessId(), &mine) || session != mine) return false;
            Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
            if (process.value == INVALID_HANDLE_VALUE || !process.value || !SameUser(process.value)) return false;
            wchar_t name[32768]; DWORD size = static_cast<DWORD>(std::size(name));
            if (!QueryFullProcessImageNameW(process.value, 0, name, &size)) return false;
            const std::wstring path(name, size);
            return _wcsicmp(path.substr(path.find_last_of(L'\\') + 1).c_str(), expected.c_str()) == 0;
        }
        DWORD Transfer(HANDLE pipe, HANDLE event, bool read, void* bytes, DWORD count,
                       DWORD& transferred, ULONGLONG deadline, const Cancelled& cancelled)
        {
            if (cancelled()) return ERROR_OPERATION_ABORTED;
            if (GetTickCount64() >= deadline) return ERROR_TIMEOUT;
            ResetEvent(event);
            OVERLAPPED operation{}; operation.hEvent = event;
            const BOOL done = read ? ReadFile(pipe, bytes, count, nullptr, &operation) :
                WriteFile(pipe, bytes, count, nullptr, &operation);
            const DWORD error = done ? ERROR_SUCCESS : GetLastError();
            // Even an immediate ERROR_MORE_DATA completes an overlapped operation.
            if (done || error == ERROR_MORE_DATA)
                return GetOverlappedResult(pipe, &operation, &transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
            if (error != ERROR_IO_PENDING) return error;
            while (GetTickCount64() < deadline && !cancelled())
            {
                const DWORD wait = static_cast<DWORD>(std::min<ULONGLONG>(25, deadline - GetTickCount64()));
                if (WaitForSingleObject(event, wait) == WAIT_OBJECT_0)
                    return GetOverlappedResult(pipe, &operation, &transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
            }
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            return cancelled() ? ERROR_OPERATION_ABORTED : ERROR_TIMEOUT;
        }
        std::string PipeCall(const std::vector<std::wstring>& names, const std::wstring& expected,
            const std::string& request, ULONGLONG deadline, const Cancelled& cancelled)
        {
            if (request.size() > MaxMessageBytes) throw std::runtime_error("invalid_request");
            HANDLE raw = INVALID_HANDLE_VALUE;
            while (GetTickCount64() < deadline && !cancelled())
            {
                bool busy = false;
                for (const auto& name : names)
                {
                    raw = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                        OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
                    if (raw != INVALID_HANDLE_VALUE) break;
                    busy = busy || GetLastError() == ERROR_PIPE_BUSY;
                }
                if (raw != INVALID_HANDLE_VALUE || !busy) break;
                Sleep(10);
            }
            Handle pipe(raw);
            if (raw == INVALID_HANDLE_VALUE) throw std::runtime_error("unavailable");
            if (!Peer(raw, expected)) throw std::runtime_error("untrusted_peer");
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(raw, &mode, nullptr, nullptr)) throw std::runtime_error("transport_error");
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event.value) throw std::runtime_error("transport_error");
            DWORD bytes = 0;
            const auto sent = Transfer(raw, event.value, false, const_cast<char*>(request.data()),
                static_cast<DWORD>(request.size()), bytes, deadline, cancelled);
            if (sent != ERROR_SUCCESS || bytes != request.size()) throw std::runtime_error("timeout");
            std::string response;
            std::vector<char> buffer(16384);
            do
            {
                bytes = 0;
                const auto received = Transfer(raw, event.value, true, buffer.data(),
                    static_cast<DWORD>(buffer.size()), bytes, deadline, cancelled);
                if (received != ERROR_SUCCESS && received != ERROR_MORE_DATA) throw std::runtime_error("timeout");
                if (response.size() + bytes > MaxMessageBytes) throw std::runtime_error("invalid_response");
                response.append(buffer.data(), bytes);
                if (received == ERROR_SUCCESS) return response;
            } while (GetTickCount64() < deadline && !cancelled());
            throw std::runtime_error("timeout");
        }
        std::wstring Reason(const std::string& value)
        {
            if (value == "current_task_member") return L"Observed in the current task hypothesis";
            if (value == "observed_in_requested_interval") return L"Observed in the selected time window";
            if (value == "same_directory_or_repository_association") return L"Same directory/project association (not proof of use)";
            if (value == "download_origin_observed_not_reading") return L"Download origin observed; reading is not inferred";
            if (value == "recent_file_observation") return L"Direct file activity in the current 15-minute window (not a primary-task claim)";
            return L"Supported WARP association";
        }
        std::wstring FallbackReason(const std::string& value)
        {
            if (value == "truncated") return L"Relevant evidence exceeded the safety limit";
            if (value == "no_qualified_evidence") return L"No qualified document or page activity in this window";
            if (value == "stale") return L"Latest qualified current-context evidence is older than five minutes";
            if (value == "not_present") return L"Current context is unavailable while away or locked";
            if (value == "paused") return L"WARP capture is paused";
            if (value == "privacy_redacted" || value == "excluded") return L"Privacy exclusions prevent this assessment";
            if (value == "query_mismatch") return L"Query or match quality does not support a contextual boost";
            if (value == "ambiguous_or_unqualified") return L"Artifact identity or activity attribution is uncertain";
            if (value == "action_not_observed") return L"The requested action was not observed";
            return L"No supported association with the selected task/window";
        }
    }

    int64_t NowMs()
    {
        FILETIME time; GetSystemTimeAsFileTime(&time);
        return static_cast<int64_t>(((uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime) / 10000) - 11644473600000LL;
    }
    std::wstring NormalizePath(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0) path = L"\\\\" + path.substr(8);
        else if (path.compare(0, 4, L"\\\\?\\") == 0) path.erase(0, 4);
        if (path.empty()) return path;
        const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
            path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr, 0);
        if (size <= 0) return {};
        std::wstring lower(size, L'\0');
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, path.data(),
            static_cast<int>(path.size()), lower.data(), size, nullptr, nullptr, 0);
        return lower;
    }
    std::wstring StatusText(const Result& result)
    {
        if (result.available)
        {
            const bool applied = std::any_of(result.features.begin(), result.features.end(),
                [](const Feature& feature) { return feature.delta > 0; });
            std::wstring text = applied ? L"WARP context applied (max +8)" : L"WARP: no task match for these candidates; baseline order";
            if (!applied && !result.features.empty())
            {
                const auto& fallback = result.features.front().fallback;
                const bool same = std::all_of(result.features.begin(), result.features.end(),
                    [&](const Feature& feature) { return feature.fallback == fallback; });
                if (same)
                {
                    if (fallback == "truncated") text = L"WARP evidence limit reached; baseline order";
                    else if (fallback == "no_qualified_evidence") text = L"WARP: no qualified document/page activity; baseline order";
                    else if (fallback == "stale") text = L"WARP context is older than 5 minutes; baseline order";
                    else if (fallback == "not_present") text = L"WARP: user away/locked; baseline order";
                    else if (fallback == "paused") text = L"WARP capture paused; baseline order";
                    else if (fallback == "query_mismatch") text = L"WARP: no query-supported boost; baseline order";
                    else if (fallback == "privacy_redacted") text = L"WARP privacy exclusions apply; baseline order";
                }
            }
            if (result.partial) text += L" (partial evidence)";
            return text;
        }
        if (result.status == "forbidden" || result.status == "unauthorized") return L"WARP access denied; baseline results";
        if (result.status == "unknown_op") return L"WARP Phase C update required; baseline results";
        if (result.status == "context_invalidated") return L"WARP context cleared/changed; baseline results";
        if (result.status == "context_expired") return L"WARP context expired; baseline results";
        if (result.status == "invalid_response" || result.status == "untrusted_peer") return L"WARP response not accepted; baseline results";
        if (result.status == "timeout") return L"WARP timed out; baseline results";
        return L"WARP unavailable; baseline results";
    }
    std::wstring DeltaText(double delta, bool assessed)
    {
        if (!assessed) return L"\u2014";
        if (delta <= 0) return L"0.0";
        wchar_t text[32]; swprintf_s(text, L"+%.1f", delta);
        return text;
    }
    std::string ContextRequest(int seconds, int64_t now)
    {
        if (seconds != 0 && seconds != 1800 && seconds != 7200 && seconds != 86400)
            throw std::runtime_error("invalid_request");
        JsonObject value;
        value.Insert(L"op", JsonValue::CreateStringValue(L"GetSearchContext"));
        value.Insert(L"mode", JsonValue::CreateStringValue(seconds ? L"interval" : L"current"));
        if (seconds)
        {
            value.Insert(L"start", JsonValue::CreateNumberValue(double(now / 1000 - seconds)));
            value.Insert(L"end", JsonValue::CreateNumberValue(double(now / 1000)));
            value.Insert(L"as_of", JsonValue::CreateNumberValue(double(now / 1000)));
        }
        return winrt::to_string(value.Stringify());
    }
    std::string CandidateRequest(const Request& request)
    {
        const auto query = winrt::to_string(request.query);
        if (query.empty() || query.size() > 1024 || request.reference.empty() || request.reference.size() > 128 ||
            request.candidates.empty() || request.candidates.size() > MaxCandidates ||
            (request.intent != "general" && request.intent != "download" && request.intent != "open" && request.intent != "modify"))
            throw std::runtime_error("invalid_request");
        JsonObject root;
        root.Insert(L"op", JsonValue::CreateStringValue(L"ScoreCandidates"));
        root.Insert(L"context_ref", JsonValue::CreateStringValue(winrt::to_hstring(request.reference)));
        root.Insert(L"query", JsonValue::CreateStringValue(request.query));
        root.Insert(L"intent", JsonValue::CreateStringValue(winrt::to_hstring(request.intent)));
        JsonArray rows; std::set<std::wstring> keys;
        for (const auto& item : request.candidates)
        {
            const auto key = NormalizePath(item.key);
            const bool absolute = (key.size() >= 3 && key[1] == L':' && key[2] == L'\\') ||
                (key.size() > 4 && key.compare(0, 2, L"\\\\") == 0);
            if (!absolute || !keys.insert(key).second || !std::isfinite(item.base) || item.base < 0 || item.base > 200)
                throw std::runtime_error("invalid_request");
            JsonObject row;
            row.Insert(L"key", JsonValue::CreateStringValue(key));
            row.Insert(L"name", JsonValue::CreateStringValue(BoundedText(item.name, 256)));
            row.Insert(L"title", JsonValue::CreateStringValue(BoundedText(item.title, 512)));
            row.Insert(L"base_score", JsonValue::CreateNumberValue(item.base));
            row.Insert(L"exact", JsonValue::CreateBooleanValue(item.exact));
            rows.Append(row);
        }
        root.Insert(L"candidates", rows);
        const auto text = winrt::to_string(root.Stringify());
        if (text.size() > MaxMessageBytes) throw std::runtime_error("invalid_request");
        return text;
    }
    Result DecodeScores(const std::string& json, const Request& request, int64_t now)
    {
        Result result; result.generation = request.generation;
        try
        {
            const auto root = Parse(json);
            const auto error = ErrorCode(root);
            if (!error.empty()) { result.status = error; return result; }
            if (!Bool(root, L"ok") || Number(root, L"api_version") != 2 ||
                Text(root, L"rule_version") != "phase-c-rank-1" ||
                Text(root, L"context_ref") != request.reference ||
                Number(root, L"max_score_delta") != 8 || !Bool(root, L"exact_matches_first") ||
                Bool(root, L"query_and_candidates_persisted"))
                throw std::runtime_error("invalid_response");
            result.expiresMs = Expiry(root, now); result.reference = request.reference;
            result.partial = Bool(root, L"partial");
            const auto rows = root.GetNamedArray(L"candidates");
            if (request.candidates.empty() || rows.Size() != request.candidates.size() || rows.Size() > MaxCandidates)
                throw std::runtime_error("invalid_response");
            std::set<std::wstring> expected, seen;
            for (const auto& item : request.candidates) expected.insert(NormalizePath(item.key));
            if (expected.size() != request.candidates.size()) throw std::runtime_error("invalid_response");
            for (const auto& value : rows)
            {
                const auto row = value.GetObject();
                Feature feature;
                feature.key = NormalizePath(std::wstring(row.GetNamedString(L"key")));
                if (!expected.count(feature.key) || !seen.insert(feature.key).second)
                    throw std::runtime_error("invalid_response");
                feature.found = Bool(row, L"found");
                const bool eligible = Bool(row, L"eligible");
                const auto fallback = Text(row, L"fallback", 128);
                feature.fallback = fallback;
                const double delta = Number(row, L"score_delta");
                if (delta < 0 || delta > 8 || (delta > 0 && (!eligible || !fallback.empty())))
                    throw std::runtime_error("invalid_response");
                const auto candidate = std::find_if(request.candidates.begin(), request.candidates.end(),
                    [&](const Candidate& item) { return NormalizePath(item.key) == feature.key; });
                feature.delta = WarpSearch::BoundedDelta({candidate->base, delta, candidate->exact});
                const auto reasons = row.GetNamedArray(L"reasons"), support = row.GetNamedArray(L"evidence_refs");
                if (reasons.Size() > 16 || support.Size() > 16 || (delta > 0 && (!reasons.Size() || !support.Size())))
                    throw std::runtime_error("invalid_response");
                for (const auto& ref : support)
                    if (winrt::to_string(ref.GetString()).empty() || ref.GetString().size() > 256)
                        throw std::runtime_error("invalid_response");
                for (const auto& reason : reasons)
                {
                    const auto code = winrt::to_string(reason.GetString());
                    if (code.size() > 128) throw std::runtime_error("invalid_response");
                    if (!feature.reason.empty()) feature.reason += L"; ";
                    feature.reason += Reason(code);
                }
                if (feature.reason.empty()) feature.reason = FallbackReason(fallback);
                result.features.push_back(std::move(feature));
            }
            result.available = true; result.status = "ok";
        }
        catch (const std::runtime_error& error) { result = {}; result.generation = request.generation; result.status = error.what(); }
        catch (...) { result = {}; result.generation = request.generation; result.status = "invalid_response"; }
        return result;
    }
    Result Evaluate(const Request& original, const Transport& exchange, const Cancelled& cancelled)
    {
        Result result; result.generation = original.generation;
        try
        {
            Request request = original;
            if (cancelled()) throw std::runtime_error("cancelled");
            if (original.candidates.empty() || original.candidates.size() > MaxSearchCandidates)
                throw std::runtime_error("invalid_request");
            std::set<std::wstring> keys;
            for (const auto& candidate : original.candidates)
                if (!keys.insert(NormalizePath(candidate.key)).second) throw std::runtime_error("invalid_request");
            auto batch = [&](size_t offset) {
                const auto end = std::min(original.candidates.size(), offset + MaxCandidates);
                request.candidates.assign(original.candidates.begin()+offset, original.candidates.begin()+end);
            };
            // Validate the entire retrieved set before sending any query/candidate data.
            if (request.reference.empty()) request.reference = "validation";
            for (size_t offset = 0; offset < original.candidates.size(); offset += MaxCandidates)
            {
                batch(offset);
                CandidateRequest(request);
            }
            request.reference = original.reference;
            if (request.reference.empty())
            {
                const auto root = Parse(exchange(ContextRequest(request.windowSeconds, NowMs()), cancelled));
                const auto error = ErrorCode(root);
                if (!error.empty()) { result.status = error; return result; }
                if (!Bool(root, L"ok") || Number(root, L"api_version") != 2 || Bool(root, L"model_on_request_path") ||
                    Text(root.GetNamedObject(L"context"), L"schema") != "warp.task-context.v1")
                    throw std::runtime_error("invalid_response");
                Expiry(root, NowMs());
                request.reference = Text(root, L"context_ref");
            }
            Result combined; combined.generation = original.generation;
            for (size_t offset = 0; offset < original.candidates.size(); offset += MaxCandidates)
            {
                if (cancelled()) throw std::runtime_error("cancelled");
                batch(offset);
                const auto response = exchange(CandidateRequest(request), cancelled);
                if (cancelled()) throw std::runtime_error("cancelled");
                auto decoded = DecodeScores(response, request, NowMs());
                if (!decoded.available) return decoded;
                if (offset && (decoded.expiresMs != combined.expiresMs || decoded.reference != combined.reference))
                    throw std::runtime_error("invalid_response");
                combined.reference = decoded.reference; combined.expiresMs = decoded.expiresMs;
                combined.partial = combined.partial || decoded.partial;
                combined.features.insert(combined.features.end(),
                    std::make_move_iterator(decoded.features.begin()), std::make_move_iterator(decoded.features.end()));
            }
            if (combined.expiresMs <= NowMs()) throw std::runtime_error("context_expired");
            combined.available = true; combined.status = "ok";
            return combined;
        }
        catch (const std::runtime_error& error) { result.status = error.what(); }
        catch (...) { result.status = "invalid_response"; }
        return result;
    }
    Transport LocalPipeTransport(unsigned budgetMs, std::vector<std::wstring> names, std::wstring expected)
    {
        if (names.empty())
        {
            DWORD session = 0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
            names = {L"\\\\.\\pipe\\WarpFileActivityAPI-" + std::to_wstring(session), L"\\\\.\\pipe\\WarpFileActivityAPI"};
        }
        const ULONGLONG deadline = GetTickCount64() + std::min(budgetMs, 5000u);
        return [names = std::move(names), expected = std::move(expected), deadline](const std::string& text, const Cancelled& cancelled) {
            return PipeCall(names, expected, text, deadline, cancelled);
        };
    }
    Worker::Worker(Completion completed, std::function<Transport()> transportFactory) :
        m_completed(std::move(completed)), m_transportFactory(std::move(transportFactory)), m_thread(&Worker::Run, this) {}
    Worker::~Worker() { Stop(); }
    uint64_t Worker::Cancel()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = false;
        return ++m_generation;
    }
    uint64_t Worker::Submit(Request request)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        request.generation = ++m_generation;
        m_request = std::move(request); m_pending = true;
        m_cv.notify_one();
        return m_generation.load();
    }
    void Worker::Stop()
    {
        { std::lock_guard<std::mutex> lock(m_mutex); m_stopped = true; ++m_generation; m_pending = false; }
        m_cv.notify_one();
        if (m_thread.joinable()) m_thread.join();
    }
    void Worker::Run()
    {
        try { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
        catch (...) { return; }
        for (;;)
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [&] { return m_pending || m_stopped; });
                if (m_stopped) break;
                request = std::move(m_request); m_pending = false;
            }
            const auto cancelled = [&] { return m_stopped || request.generation != m_generation; };
            auto result = Evaluate(request, m_transportFactory ? m_transportFactory() : LocalPipeTransport(), cancelled);
            if (!cancelled()) m_completed(std::move(result));
        }
        winrt::uninit_apartment();
    }
    Demo ReadDemo(const std::wstring& path)
    {
        if (std::filesystem::file_size(path) > MaxMessageBytes) throw std::runtime_error("Demo fixture is too large");
        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        std::string line; std::getline(input, line);
        const auto root = Parse(line);
        if (!Bool(root, L"synthetic")) throw std::runtime_error("Demo requires an explicitly synthetic fixture");
        Demo demo; demo.id = Text(root, L"id"); demo.sourceCommit = Text(root, L"warp_commit");
        demo.request.query = std::wstring(root.GetNamedString(L"query"));
        demo.request.intent = Text(root, L"intent");
        const auto packet = root.GetNamedObject(L"features");
        demo.request.reference = Text(packet, L"context_ref");
        for (const auto& value : root.GetNamedArray(L"candidates"))
        {
            const auto row = value.GetObject();
            Candidate item;
            item.key = std::wstring(row.GetNamedString(L"key")); item.name = std::wstring(row.GetNamedString(L"name"));
            item.title = std::wstring(row.GetNamedString(L"title", L""));
            item.base = Number(row, L"base_score"); item.exact = Bool(row, L"exact");
            demo.request.candidates.push_back(std::move(item));
        }
        CandidateRequest(demo.request);
        demo.result = DecodeScores(winrt::to_string(packet.Stringify()), demo.request,
            static_cast<int64_t>(Number(root, L"replay_now_ms")));
        if (!demo.result.available) throw std::runtime_error("Demo feature packet is invalid");
        return demo;
    }
}
