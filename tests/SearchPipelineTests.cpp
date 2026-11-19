#include <filesystem>
#include "..\Find My File!.cpp"
#include <iostream>

namespace
{
    void Check(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }

    const std::wstring GuideName = L"File Search Evals With and Without a Local Search Broker - Step-by-Step Guide.md";
    const std::wstring SyntheticGuide = L"C:\\WarpSynthetic\\Desktop\\" + GuideName;

    void SeedIndex()
    {
        StopUsnMonitor();
        fileMap.clear(); childrenMap.clear(); timestampCache.clear(); pendingUsnChanges.clear();
        currentDriveLetter = L"C";
        const auto add = [](ULONGLONG id, ULONGLONG parent, const std::wstring& name, bool directory) {
            FileEntry entry{};
            entry.fileReference = id; entry.parentReference = parent;
            entry.fileName = name; entry.lowerFileName = name; entry.isDirectory = directory;
            for (auto& ch : entry.lowerFileName) ch = towlower(ch);
            fileMap[id] = std::move(entry); childrenMap[parent].push_back(id);
        };
        add(5, 5, L"", true);
        add(10, 5, L"WarpSynthetic", true);
        add(11, 10, L"Desktop", true);
        add(12, 11, L"old-notes.txt", false);
        BuildSearchIndex(currentDriveLetter);
    }

    void QueueGuide()
    {
        UsnChange change{};
        change.fileRef = 20; change.parentRef = 11; change.fileName = GuideName;
        change.reason = USN_REASON_FILE_CREATE;
        pendingUsnChanges.push_back(std::move(change));
    }

    void CheckGuideRequest()
    {
        currentSearchKeyword = L"eval";
        PerformSearch(L"eval");
        baselineResults = searchResults;
        const auto request = BuildWarpRequest();
        Check(request.query == L"eval", "retrieval query changed before scoring");
        const auto key = WarpContext::NormalizePath(SyntheticGuide);
        const auto found = std::find_if(request.candidates.begin(), request.candidates.end(),
            [&](const WarpContext::Candidate& item) { return item.key == key; });
        Check(found != request.candidates.end(), "new Markdown file omitted from real retrieval-to-request path");
        Check(found->base == 75 && !found->exact, "eval prefix score/exact flag incorrect");
        Check(found->name == GuideName, "candidate filename corrupted");
        auto serializable = request; serializable.reference = "synthetic-context";
        if (serializable.candidates.size() > WarpContext::MaxCandidates)
            serializable.candidates.resize(WarpContext::MaxCandidates);
        Check(!WarpContext::CandidateRequest(serializable).empty(), "real candidate request rejected");
    }

    void NewlyCreatedMarkdownIsRetrievedAndScored()
    {
        SeedIndex();
        QueueGuide();
        Check(ProcessPendingUsnChanges(), "journal create was not applied");
        CheckGuideRequest();
    }

    void CacheResumesOnlyFromAValidDurableCheckpoint()
    {
        SeedIndex();
        wchar_t temporary[MAX_PATH]; GetTempPathW(MAX_PATH, temporary);
        const auto folder = std::filesystem::path(temporary) /
            (L"findmyfile-pipeline-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(folder);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{folder};
        const auto cache = (folder / L"index.db").wstring(), legacy = (folder / L"legacy.db").wstring();
        QueueGuide();
        usnJournalId = 19; lastUsnProcessed = 1200; usnCheckpointValid = true;
        StopUsnMonitor();
        Check(SaveIndexToFile(cache), "cache save failed");
        fileMap.clear();
        Check(LoadIndexFromFile(cache), "cache reload failed");
        CheckGuideRequest();
        Check(usnCheckpointValid && usnJournalId == 19 && lastUsnProcessed == 1200, "checkpoint did not round-trip");
        USN_JOURNAL_DATA_V0 journal{};
        journal.UsnJournalID = 19; journal.FirstUsn = 1000; journal.LowestValidUsn = 900; journal.NextUsn = 1500;
        Check(JournalContainsCheckpoint(journal, usnJournalId, lastUsnProcessed), "valid offline catch-up rejected");
        Check(!JournalContainsCheckpoint(journal, 20, lastUsnProcessed), "replaced journal accepted");
        Check(!JournalContainsCheckpoint(journal, 19, 800), "expired checkpoint accepted");
        Check(!JournalContainsCheckpoint(journal, 19, 1600), "future checkpoint accepted");
        std::ifstream input(cache, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();
        const DWORD version = 2; memcpy(bytes.data()+8, &version, sizeof(version));
        bytes.erase(12+4+currentDriveLetter.size()*sizeof(wchar_t), 16);
        std::ofstream output(legacy, std::ios::binary); output.write(bytes.data(), bytes.size()); output.close();
        Check(LoadIndexFromFile(legacy), "legacy cache must remain readable for display");
        Check(!usnCheckpointValid, "legacy cache incorrectly treated as current");
    }

    void LaterMatchesAreNotSilentlyDroppedFromContextScoring()
    {
        SeedIndex();
        for (int i = 0; i < 300; ++i)
        {
            UsnChange change{};
            change.fileRef = 100+i; change.parentRef = 11;
            change.fileName = L"eval-" + std::to_wstring(i) + L".txt";
            change.reason = USN_REASON_FILE_CREATE;
            pendingUsnChanges.push_back(std::move(change));
        }
        QueueGuide();
        Check(ProcessPendingUsnChanges(), "journal changes missing");
        CheckGuideRequest();
        const auto request = BuildWarpRequest();
        Check(request.candidates.size() == 301, "retrieved files truncated at one API batch");
        const auto key = WarpContext::NormalizePath(SyntheticGuide);
        const auto guide = std::find_if(request.candidates.begin(), request.candidates.end(),
            [&](const WarpContext::Candidate& item) { return item.key == key; });
        Check(size_t(guide-request.candidates.begin()) >= WarpContext::MaxCandidates, "fixture did not exercise a later batch");
    }

    void LiveIndexUpdatesDoNotRestartTheDisplayedSearch()
    {
        SeedIndex(); QueueGuide(); ProcessPendingUsnChanges(); CheckGuideRequest();
        HWND window = CreateWindowExW(0,L"STATIC",L"FindMyFile pipeline test",0,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
        Check(window != nullptr, "hidden test window unavailable");
        UsnChange change{};
        change.fileRef = 999; change.parentRef = 11; change.fileName = L"unrelated-new.txt";
        change.reason = USN_REASON_FILE_CREATE; pendingUsnChanges.push_back(std::move(change));
        WndProc(window,WM_USN_UPDATED,0,0);
        Sleep(SEARCH_DEBOUNCE_MS+50);
        MSG message{};
        const bool restarted = PeekMessageW(&message,window,WM_TIMER,WM_TIMER,PM_REMOVE) != FALSE;
        DestroyWindow(window);
        Check(!restarted, "background filesystem activity restarted the displayed search");
        Check(fileMap.count(999) == 1, "index update was discarded");
    }

    void QueryCachedIndex(const std::wstring& query, const std::wstring& target, bool refresh)
    {
        Check(LoadIndexFromFile(), "existing cached index unavailable");
        if (refresh)
        {
            const auto drive = currentDriveLetter;
            fileMap.clear(); childrenMap.clear(); timestampCache.clear(); fullPathCache.clear();
            int files = 0, directories = 0;
            Check(ReadDriveMFT(nullptr, drive, files, directories), "MFT refresh failed");
            BuildSearchIndex(drive);
            Check(StartUsnMonitor(drive), "filesystem monitor could not resume after scan");
            StopUsnMonitor();
            Check(SaveIndexToFile(), "refreshed cache could not be saved");
        }
        currentSearchKeyword = query;
        PerformSearch(query);
        baselineResults = searchResults;
        const auto request = BuildWarpRequest();
        const auto key = WarpContext::NormalizePath(target);
        const auto targetRow = std::find_if(searchResults.begin(), searchResults.end(),
            [&](const SearchResult& row) { return WarpContext::NormalizePath(row.fullPath) == key; });
        const auto candidate = std::find_if(request.candidates.begin(), request.candidates.end(),
            [&](const WarpContext::Candidate& row) { return row.key == key; });
        size_t queryMatches = 0;
        for (const auto& row : request.candidates)
            if (WarpContext::NormalizePath(row.name).find(query) != std::wstring::npos) ++queryMatches;
        std::cout << "results=" << searchResults.size() << " batch=" << request.candidates.size()
            << " literal_query_matches=" << queryMatches << " target_retrieved=" << (targetRow != searchResults.end())
            << " target_in_batch=" << (candidate != request.candidates.end()) << '\n';
        if (targetRow != searchResults.end())
            std::cout << "target_baseline_rank=" << targetRow-searchResults.begin()+1 << " target_base=" << targetRow->baseScore << '\n';
        const auto name = target.substr(target.find_last_of(L'\\') + 1);
        size_t named = 0;
        for (const auto& pair : fileMap)
            if (_wcsicmp(pair.second.fileName.c_str(), name.c_str()) == 0)
            {
                ++named;
                std::cout << "named_target_cached_path=" << WideToUtf8(BuildFullPath(pair.first,currentDriveLetter))
                    << " excluded_by_baseline=" << IsInGoopFolder(pair.first) << '\n';
            }
        std::cout << "target_name_in_cache=" << named << " cached_drive=" << WideToUtf8(currentDriveLetter) << '\n';
        for (int seconds : {0, 7200})
        {
            auto probe = request; probe.windowSeconds = seconds;
            auto result = WarpContext::Evaluate(probe, WarpContext::LocalPipeTransport(), [] { return false; });
            std::map<std::string,size_t> fallbacks;
            for (const auto& feature : result.features) ++fallbacks[feature.fallback];
            std::cout << "window_seconds=" << seconds << " available=" << result.available << " status=" << result.status << '\n';
            for (const auto& pair : fallbacks) std::cout << "fallback=" << pair.first << " count=" << pair.second << '\n';
            for (const auto& feature : result.features)
                if (feature.key == key) std::cout << "target_found=" << feature.found << " target_delta=" << feature.delta << '\n';
            ApplyWarpContext(result);
            for (const auto& row : searchResults)
                if (WarpContext::NormalizePath(row.fullPath) == key)
                    std::cout << "target_applied_delta=" << row.warpBonus << " target_assessed=" << !row.warpReason.empty() << '\n';
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    const auto hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    try
    {
        if (argc == 4 && (wcscmp(argv[1], L"--cached-query") == 0 || wcscmp(argv[1], L"--refresh-cached-query") == 0))
        {
            QueryCachedIndex(argv[2], argv[3], wcscmp(argv[1], L"--refresh-cached-query") == 0);
            RoUninitialize();
            return 0;
        }
        NewlyCreatedMarkdownIsRetrievedAndScored();
        std::cout << "PASS NewlyCreatedMarkdownIsRetrievedAndScored\n";
        CacheResumesOnlyFromAValidDurableCheckpoint();
        std::cout << "PASS CacheResumesOnlyFromAValidDurableCheckpoint\n";
        LaterMatchesAreNotSilentlyDroppedFromContextScoring();
        std::cout << "PASS LaterMatchesAreNotSilentlyDroppedFromContextScoring\n";
        LiveIndexUpdatesDoNotRestartTheDisplayedSearch();
        std::cout << "PASS LiveIndexUpdatesDoNotRestartTheDisplayedSearch\n";
        RoUninitialize();
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RoUninitialize();
    return 1;
}
