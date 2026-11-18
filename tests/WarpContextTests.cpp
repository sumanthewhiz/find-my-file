#include <windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include "WarpContext.h"
#include "SearchRanking.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace winrt::Windows::Data::Json;
using namespace WarpContext;

namespace
{
    void Check(bool condition, const char* message)
    { if (!condition) throw std::runtime_error(message); }
    Request Sample()
    {
        Request request; request.query = L"budget"; request.reference = "test-reference";
        request.candidates = {{L"C:\\Synthetic\\Other\\budget.xlsx", L"budget.xlsx", L"", 100, true},
            {L"C:\\Synthetic\\Project\\budget-plan.xlsx", L"budget-plan.xlsx", L"", 96, false},
            {L"C:\\Synthetic\\Unseen\\budget-old.xlsx", L"budget-old.xlsx", L"", 91, false}};
        return request;
    }
    JsonObject Packet(const Request& request, const std::vector<double>& deltas, int64_t now = NowMs())
    {
        JsonObject root;
        root.Insert(L"api_version", JsonValue::CreateNumberValue(2));
        root.Insert(L"ok", JsonValue::CreateBooleanValue(true));
        root.Insert(L"context_ref", JsonValue::CreateStringValue(winrt::to_hstring(request.reference)));
        root.Insert(L"expires_ms", JsonValue::CreateNumberValue(double(now + 60000)));
        root.Insert(L"rule_version", JsonValue::CreateStringValue(L"phase-c-rank-1"));
        root.Insert(L"partial", JsonValue::CreateBooleanValue(false));
        root.Insert(L"max_score_delta", JsonValue::CreateNumberValue(8));
        root.Insert(L"exact_matches_first", JsonValue::CreateBooleanValue(true));
        root.Insert(L"query_and_candidates_persisted", JsonValue::CreateBooleanValue(false));
        JsonArray rows;
        for (size_t i = 0; i < request.candidates.size(); ++i)
        {
            JsonObject row;
            row.Insert(L"key", JsonValue::CreateStringValue(NormalizePath(request.candidates[i].key)));
            row.Insert(L"found", JsonValue::CreateBooleanValue(false));
            row.Insert(L"eligible", JsonValue::CreateBooleanValue(deltas[i] > 0));
            row.Insert(L"fallback", JsonValue::CreateStringValue(L""));
            row.Insert(L"score_delta", JsonValue::CreateNumberValue(deltas[i]));
            JsonArray reasons, support;
            if (deltas[i] > 0)
            {
                reasons.Append(JsonValue::CreateStringValue(L"same_directory_or_repository_association"));
                support.Append(JsonValue::CreateStringValue(L"synthetic:1"));
            }
            row.Insert(L"reasons", reasons); row.Insert(L"evidence_refs", support);
            rows.Append(row);
        }
        root.Insert(L"candidates", rows);
        return root;
    }
    JsonObject Reference(const Request& request)
    {
        JsonObject root;
        root.Insert(L"api_version", JsonValue::CreateNumberValue(2));
        root.Insert(L"ok", JsonValue::CreateBooleanValue(true));
        root.Insert(L"context_ref", JsonValue::CreateStringValue(winrt::to_hstring(request.reference)));
        root.Insert(L"expires_ms", JsonValue::CreateNumberValue(double(NowMs() + 60000)));
        root.Insert(L"model_on_request_path", JsonValue::CreateBooleanValue(false));
        JsonObject context; context.Insert(L"schema", JsonValue::CreateStringValue(L"warp.task-context.v1"));
        root.Insert(L"context", context);
        return root;
    }
    std::vector<size_t> Order(const Request& request, const Result& result)
    {
        std::map<std::wstring, double> scores;
        if (result.available) for (const auto& feature : result.features) scores[feature.key] = feature.delta;
        std::vector<WarpSearch::RankInput> input;
        for (const auto& candidate : request.candidates)
            input.push_back({candidate.base, scores[NormalizePath(candidate.key)], candidate.exact});
        return WarpSearch::Rank(input, result.available);
    }
    void JsonRoundTrip()
    {
        auto request = Sample(); request.query = L"budget \"2026\" \u65E5\u672C";
        request.candidates[0].title = L"Quotes \" } ] and \u00E9";
        const auto value = JsonObject::Parse(winrt::to_hstring(CandidateRequest(request)));
        Check(value.GetNamedString(L"query") == request.query, "query JSON escaping");
        Check(value.GetNamedArray(L"candidates").GetObjectAt(0).GetNamedString(L"title") == request.candidates[0].title, "metadata escaping");
        const auto interval = JsonObject::Parse(winrt::to_hstring(ContextRequest(7200, 1800000000000)));
        Check(interval.GetNamedNumber(L"end") - interval.GetNamedNumber(L"start") == 7200, "historical bounds");
        Check(interval.GetNamedNumber(L"as_of") == interval.GetNamedNumber(L"end"), "historical knowledge cutoff");
    }
    void ExactUnseenAndFallback()
    {
        const auto request = Sample();
        auto result = DecodeScores(winrt::to_string(Packet(request, {0, 8, 8}).Stringify()), request, NowMs());
        Check(result.available && !result.features[1].found && result.features[1].delta == 8, "unseen candidate can benefit");
        const auto order = Order(request, result);
        Check(order == std::vector<size_t>({0, 1, 2}), "exact must outrank contextual nonexact");
        auto noExact = request; noExact.candidates[0].exact = false;
        Check(Order(noExact, result).front() == 1, "same features rerank ambiguous names");
        const auto baseline = Order(noExact, {});
        Check(baseline == std::vector<size_t>({0, 1, 2}), "no context preserves baseline");
    }
    void RejectMalformedFeatures()
    {
        const auto request = Sample();
        for (int defect = 0; defect < 9; ++defect)
        {
            auto packet = Packet(request, {0, 4, 0});
            auto rows = packet.GetNamedArray(L"candidates");
            auto row = rows.GetObjectAt(1);
            switch (defect)
            {
            case 0: row.Insert(L"score_delta", JsonValue::CreateNumberValue(125)); break;
            case 1: row.Insert(L"key", JsonValue::CreateStringValue(L"C:\\not-a-candidate.docx")); break;
            case 2: rows.SetAt(1, rows.GetAt(0)); break;
            case 3: row.Insert(L"score_delta", JsonValue::CreateStringValue(L"4")); break;
            case 4: packet.Insert(L"context_ref", JsonValue::CreateStringValue(L"wrong")); break;
            case 5: row.Insert(L"evidence_refs", JsonArray()); break;
            case 6: packet.Insert(L"expires_ms", JsonValue::CreateNumberValue(double(NowMs() - 1))); break;
            case 7: row.Insert(L"eligible", JsonValue::CreateBooleanValue(false)); break;
            case 8: packet.Insert(L"query_and_candidates_persisted", JsonValue::CreateBooleanValue(true)); break;
            }
            const auto result = DecodeScores(winrt::to_string(packet.Stringify()), request, NowMs());
            Check(!result.available && result.features.empty(), "malformed batch must fail atomically");
            Check(Order(request, result) == Order(request, {}), "malformed response changed baseline");
        }
        Check(!DecodeScores("{truncated", request, NowMs()).available, "malformed JSON accepted");
        Check(!DecodeScores(std::string(MaxMessageBytes + 1, ' '), request, NowMs()).available, "oversized JSON accepted");
    }
    void ErrorFallbacks()
    {
        const auto request = Sample();
        for (const auto code : {"forbidden", "unknown_op", "context_expired", "context_invalidated"})
        {
            const auto text = std::string("{\"api_version\":2,\"ok\":false,\"error\":{\"code\":\"") + code + "\",\"message\":\"test\"}}";
            const auto result = DecodeScores(text, request, NowMs());
            Check(!result.available && result.status == code, "error code not preserved");
            Check(Order(request, result) == Order(request, {}), "failure changed ranking");
        }
    }
    void NoBoostDiagnosticsAreNotTransportFailures()
    {
        const auto request = Sample();
        const std::pair<const wchar_t*, const wchar_t*> cases[] = {
            {L"truncated", L"evidence limit"}, {L"no_qualified_evidence", L"no qualified"},
            {L"stale", L"older than"}, {L"not_present", L"away/locked"},
            {L"paused", L"paused"}, {L"query_mismatch", L"query-supported"}};
        for (const auto& test : cases)
        {
            auto packet = Packet(request, {0, 0, 0});
            for (const auto& value : packet.GetNamedArray(L"candidates"))
                value.GetObject().Insert(L"fallback", JsonValue::CreateStringValue(test.first));
            const auto result = DecodeScores(winrt::to_string(packet.Stringify()), request, NowMs());
            Check(result.available && result.features.size() == request.candidates.size(), "valid no-boost packet rejected");
            Check(StatusText(result).find(test.second) != std::wstring::npos, "specific fallback hidden");
            Check(DeltaText(result.features[0].delta, true) == L"0.0", "assessed zero shown as blank");
            Check(Order(request, result) == Order(request, {}), "diagnostic change altered baseline");
        }
        Check(DeltaText(0, false) == L"\u2014", "unassessed result must remain distinct");
        Check(DeltaText(7.2, true) == L"+7.2", "positive delta display changed");
    }
    void RelevanceAndBatchLimits()
    {
        auto request = Sample(); request.candidates[1].base = 15;
        const auto result = DecodeScores(winrt::to_string(Packet(request, {0, 8, 0}).Stringify()), request, NowMs());
        Check(result.available && result.features[1].delta == 0, "weak relevance rescued by context");
        request.candidates.resize(257);
        int calls = 0;
        const auto invalid = Evaluate(request, [&](const std::string&, const Cancelled&) { ++calls; return ""; }, [] { return false; });
        Check(!invalid.available && calls == 0, "oversized batch reached service");
    }
    void TwoStageAndReferenceReuse()
    {
        const auto sample = Sample(); auto request = sample; request.reference.clear();
        int calls = 0;
        const auto exchange = [&](const std::string& text, const Cancelled&) {
            ++calls;
            const auto root = JsonObject::Parse(winrt::to_hstring(text));
            if (root.GetNamedString(L"op") == L"GetSearchContext") return winrt::to_string(Reference(sample).Stringify());
            Check(root.GetNamedString(L"context_ref") == winrt::to_hstring(sample.reference), "frozen reference not reused");
            Check(root.GetNamedString(L"query") == sample.query, "query not sent");
            Sleep(10);
            return winrt::to_string(Packet(sample, {0, 4.2, 0}).Stringify());
        };
        auto result = Evaluate(request, exchange, [] { return false; });
        Check(result.available && calls == 2, "two-stage client");
        request.reference = result.reference; calls = 0;
        result = Evaluate(request, exchange, [] { return false; });
        Check(result.available && calls == 1, "refresh should validate the frozen reference");
    }
    class FakePipe
    {
    public:
        std::wstring name;
        std::thread thread;
        FakePipe(std::string response, DWORD delay = 0)
        {
            static unsigned sequence = 0;
            name = L"\\\\.\\pipe\\FindMyFileContextTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++sequence);
            std::promise<bool> ready; auto created = ready.get_future();
            thread = std::thread([this, response = std::move(response), delay, ready = std::move(ready)]() mutable {
                HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                    1, 65536, 65536, 0, nullptr);
                ready.set_value(pipe != INVALID_HANDLE_VALUE);
                if (pipe == INVALID_HANDLE_VALUE) return;
                if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED)
                {
                    char request[65536]; DWORD bytes = 0;
                    if (ReadFile(pipe, request, sizeof(request), &bytes, nullptr) && bytes)
                    {
                        Sleep(delay);
                        WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytes, nullptr);
                        FlushFileBuffers(pipe);
                    }
                }
                DisconnectNamedPipe(pipe); CloseHandle(pipe);
            });
            if (!created.get())
            {
                thread.join();
                throw std::runtime_error("isolated pipe creation");
            }
        }
        ~FakePipe()
        {
            const HANDLE wake = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
            if (thread.joinable()) thread.join();
        }
    };
    void PipeChunksAndPeer()
    {
        const std::string response = "{\"padding\":\"" + std::string(50000, 'x') + "\"}";
        FakePipe server(response);
        auto transport = LocalPipeTransport(1000, {server.name}, L"WarpContextTests.exe");
        const auto actual = transport("{}", [] { return false; });
        if (actual != response) throw std::runtime_error("message-mode chunk boundary: " +
            std::to_string(actual.size()) + " received / " + std::to_string(response.size()) + " expected");
        FakePipe wrongPeer(response);
        bool rejected = false;
        try { LocalPipeTransport(1000, {wrongPeer.name}, L"not-warp.exe")("{}", [] { return false; }); }
        catch (const std::exception& error) { rejected = std::string(error.what()) == "untrusted_peer"; }
        Check(rejected, "wrong executable accepted");
    }
    void PipeTimeoutAndBound()
    {
        FakePipe server("{}", 500);
        const auto start = GetTickCount64();
        bool failed = false;
        try { LocalPipeTransport(80, {server.name}, L"WarpContextTests.exe")("{}", [] { return false; }); }
        catch (...) { failed = true; }
        Check(failed && GetTickCount64() - start < 1000, "hung server did not time out");
        FakePipe oversized(std::string(MaxMessageBytes + 1, 'x'));
        failed = false;
        try { LocalPipeTransport(2000, {oversized.name}, L"WarpContextTests.exe")("{}", [] { return false; }); }
        catch (...) { failed = true; }
        Check(failed, "oversized message accepted");
    }
    void LatestRequestWins()
    {
        const auto sample = Sample();
        std::promise<void> entered; auto entering = entered.get_future();
        std::promise<Result> completed; auto completion = completed.get_future();
        std::atomic<int> calls{0};
        Worker worker([&](Result result) { completed.set_value(std::move(result)); }, [&] {
            return Transport([&](const std::string&, const Cancelled& cancelled) {
                if (++calls == 1)
                {
                    entered.set_value();
                    while (!cancelled()) Sleep(1);
                    throw std::runtime_error("cancelled");
                }
                return winrt::to_string(Packet(sample, {0, 4.2, 0}).Stringify());
            });
        });
        worker.Submit(sample);
        Check(entering.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "worker did not start");
        const auto generation = worker.Submit(sample);
        Check(completion.wait_for(std::chrono::seconds(3)) == std::future_status::ready, "worker did not finish latest");
        const auto result = completion.get();
        Check(result.available && result.generation == generation, "stale result was published");
        worker.Stop();
    }
    int Replay(const std::wstring& path, const std::wstring& output)
    {
        std::ifstream input(std::filesystem::path(path), std::ios::binary);
        std::ofstream out(std::filesystem::path(output), std::ios::binary);
        Check(bool(input) && bool(out), "replay file unavailable");
        std::string line; int count = 0;
        while (std::getline(input, line))
        {
            if (line.empty()) continue;
            const auto row = JsonObject::Parse(winrt::to_hstring(line));
            Check(row.GetNamedBoolean(L"synthetic"), "non-synthetic replay refused");
            Request request; request.query = std::wstring(row.GetNamedString(L"query"));
            request.intent = winrt::to_string(row.GetNamedString(L"intent"));
            const auto packet = row.GetNamedObject(L"features");
            request.reference = winrt::to_string(packet.GetNamedString(L"context_ref", L"invalidated"));
            std::vector<winrt::hstring> ids;
            for (const auto& value : row.GetNamedArray(L"candidates"))
            {
                const auto item = value.GetObject();
                ids.push_back(item.GetNamedString(L"id"));
                request.candidates.push_back({std::wstring(item.GetNamedString(L"key")),
                    std::wstring(item.GetNamedString(L"name", L"")), std::wstring(item.GetNamedString(L"title", L"")),
                    item.GetNamedNumber(L"base_score"), item.GetNamedBoolean(L"exact", false)});
            }
            CandidateRequest(request);
            const auto result = DecodeScores(winrt::to_string(packet.Stringify()), request,
                static_cast<int64_t>(row.GetNamedNumber(L"replay_now_ms")));
            JsonObject observed;
            observed.Insert(L"id", JsonValue::CreateStringValue(row.GetNamedString(L"id")));
            observed.Insert(L"available", JsonValue::CreateBooleanValue(result.available));
            observed.Insert(L"status", JsonValue::CreateStringValue(winrt::to_hstring(result.status)));
            auto order = [&](bool context) {
                JsonArray values;
                for (auto index : Order(request, context ? result : Result{}))
                    values.Append(JsonValue::CreateStringValue(ids[index]));
                return values;
            };
            observed.Insert(L"baseline", order(false));
            observed.Insert(L"contextual", order(true));
            out << winrt::to_string(observed.Stringify()) << '\n'; ++count;
        }
        Check(count > 0 && bool(out), "empty or unwritten replay");
        std::cout << "Replayed " << count << " real WARP feature packets through the consumer\n";
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try
    {
        if (argc == 4 && wcscmp(argv[1], L"--replay") == 0) return Replay(argv[2], argv[3]);
        const std::pair<const char*, void(*)()> tests[] = {
            {"JsonRoundTripAndHistoricalBounds", JsonRoundTrip}, {"ExactUnseenAndFallback", ExactUnseenAndFallback},
            {"RejectMalformedFeatures", RejectMalformedFeatures}, {"ErrorFallbacks", ErrorFallbacks},
            {"NoBoostDiagnosticsAreNotTransportFailures", NoBoostDiagnosticsAreNotTransportFailures},
            {"RelevanceAndBatchLimits", RelevanceAndBatchLimits}, {"TwoStageAndReferenceReuse", TwoStageAndReferenceReuse},
            {"PipeChunksAndPeer", PipeChunksAndPeer}, {"PipeTimeoutAndBound", PipeTimeoutAndBound},
            {"LatestRequestWins", LatestRequestWins}};
        for (const auto& test : tests) { test.second(); std::cout << "PASS " << test.first << '\n'; }
        std::cout << std::size(tests) << " consumer tests passed\n";
        winrt::uninit_apartment();
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; }
    catch (const winrt::hresult_error& error) { std::cerr << "FAIL " << winrt::to_string(error.message()) << '\n'; }
    return 1;
}
