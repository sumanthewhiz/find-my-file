#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace WarpContext
{
    constexpr size_t MaxCandidates = 256;
    constexpr size_t MaxSearchCandidates = 10000;
    constexpr size_t MaxMessageBytes = 1024 * 1024;
    struct Candidate
    {
        std::wstring key, name, title;
        double base = 0;
        bool exact = false;
    };
    struct Feature
    {
        std::wstring key, reason;
        std::string fallback;
        double delta = 0;
        bool found = false;
    };
    struct Request
    {
        uint64_t generation = 0;
        std::wstring query;
        std::string intent = "general";
        int windowSeconds = 0; // 0 = current; otherwise an explicit trailing interval
        std::vector<Candidate> candidates;
        std::string reference;
    };
    struct Result
    {
        uint64_t generation = 0;
        bool available = false, partial = false;
        int64_t expiresMs = 0;
        std::string reference, status = "unavailable";
        std::vector<Feature> features;
    };
    using Cancelled = std::function<bool()>;
    using Transport = std::function<std::string(const std::string&, const Cancelled&)>;

    int64_t NowMs();
    std::wstring NormalizePath(std::wstring path);
    std::wstring StatusText(const Result& result);
    std::wstring DeltaText(double delta, bool assessed);
    std::string ContextRequest(int windowSeconds, int64_t nowMs);
    std::string CandidateRequest(const Request& request);
    Result DecodeScores(const std::string& json, const Request& request, int64_t nowMs);
    Result Evaluate(const Request& request, const Transport& exchange, const Cancelled& cancelled);

    // Alternate names/image are used only by the isolated pipe tests, never by the UI.
    Transport LocalPipeTransport(unsigned budgetMs = 5000,
        std::vector<std::wstring> names = {}, std::wstring expectedImage = L"WARP!.exe");

    class Worker
    {
    public:
        using Completion = std::function<void(Result)>;
        explicit Worker(Completion completed, std::function<Transport()> transportFactory = {});
        ~Worker();
        uint64_t Cancel();
        uint64_t Submit(Request request);
        void Stop();
    private:
        void Run();
        Completion m_completed;
        std::function<Transport()> m_transportFactory;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<uint64_t> m_generation{0};
        std::atomic<bool> m_stopped{false};
        bool m_pending = false;
        Request m_request;
        std::thread m_thread;
    };

    struct Demo
    {
        std::string id, sourceCommit;
        Request request;
        Result result;
    };
    // Reads only the explicit synthetic fixture; never discovers files or opens a pipe.
    Demo ReadDemo(const std::wstring& path);
}
