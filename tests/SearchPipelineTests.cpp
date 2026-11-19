#include <filesystem>
#include <cstdlib>
#include <future>
#include <new>
#include "..\Find My File!.cpp"
#include <psapi.h>
#include <iostream>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#pragma comment(lib, "psapi.lib")

namespace
{
    thread_local size_t allocationBudget = SIZE_MAX;
    struct AllocationLimit
    {
        explicit AllocationLimit(size_t bytes) { allocationBudget = bytes; }
        ~AllocationLimit() { allocationBudget = SIZE_MAX; }
    };
}

void* operator new(size_t bytes)
{
    if (bytes > allocationBudget) throw std::bad_alloc();
    void* memory = std::malloc(bytes ? bytes : 1);
    if (!memory) throw std::bad_alloc();
    if (allocationBudget != SIZE_MAX) allocationBudget -= bytes;
    return memory;
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }

namespace
{
    void Check(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }

    const std::wstring GuideName = L"File Search Evals With and Without a Local Search Broker - Step-by-Step Guide.md";
    const std::wstring SyntheticGuide = L"C:\\WarpSynthetic\\Desktop\\" + GuideName;

    void SeedIndex()
    {
        StopFilenameWorker();
        indexBuildCancelled=false;
        StopUsnMonitor();
        indexMemoryFailed = false;
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
        Check(ProcessPendingUsnChanges(true), "journal changes missing");
        CheckGuideRequest();
        const auto request = BuildWarpRequest();
        Check(request.candidates.size() == 301, "retrieved files truncated at one API batch");
        const auto key = WarpContext::NormalizePath(SyntheticGuide);
        const auto guide = std::find_if(request.candidates.begin(), request.candidates.end(),
            [&](const WarpContext::Candidate& item) { return item.key == key; });
        Check(size_t(guide-request.candidates.begin()) >= WarpContext::MaxCandidates, "fixture did not exercise a later batch");
    }

    bool ShortPathAliasesShareOneContextAssessment()
    {
        SeedIndex();
        wchar_t temporary[MAX_PATH]; GetTempPathW(MAX_PATH, temporary);
        const auto folder = std::filesystem::path(temporary) /
            (L"findmyfile-alias-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(folder);
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
        } cleanup{folder};
        const auto file = folder / L"context-evals-long-filename.md";
        { std::ofstream output(file, std::ios::binary); output << "Synthetic alias regression"; }
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetShortPathNameW(file.c_str(), buffer.data(), (DWORD)buffer.size());
        Check(length > 0 && length < buffer.size(), "cannot obtain test file's short path");
        const std::wstring alias(buffer.data(), length);
        if (_wcsicmp(alias.c_str(), file.c_str()) == 0) return false;

        SearchResult row{};
        row.fileName = file.filename().wstring(); row.fullPath = alias;
        row.matchScore = row.baseScore = 60;
        currentSearchKeyword = L"evals"; bWindowsSearch = true; bWildcardSearch = false;
        baselineResults = {row};
        row.fullPath = file.wstring(); baselineResults.push_back(row);
        row.fileName = GuideName; row.fullPath = SyntheticGuide; baselineResults.push_back(row);
        auto request = BuildWarpRequest();
        Check(request.candidates.size() == 2, "short and long paths reached WARP as duplicate canonical candidates");
        const auto canonical = WarpContext::NormalizePath(file.wstring());
        Check(request.candidates[0].key == canonical, "short path was not expanded before scoring");

        request.reference = "synthetic-alias-context";
        request.candidates[0].key = alias;
        using namespace winrt::Windows::Data::Json;
        const auto assessed = WarpContext::Evaluate(request, [&](const std::string& text, const WarpContext::Cancelled&) {
            const auto sent = JsonObject::Parse(winrt::to_hstring(text));
            const auto rows = sent.GetNamedArray(L"candidates");
            Check(rows.Size() == 2 && rows.GetObjectAt(0).GetNamedString(L"key") == canonical,
                "wire request retained the short-path alias");
            auto response = JsonObject::Parse(LR"({"api_version":2,"ok":true,"context_ref":"synthetic-alias-context",
                "rule_version":"phase-c-rank-1","partial":false,"max_score_delta":8,
                "exact_matches_first":true,"query_and_candidates_persisted":false})");
            response.Insert(L"expires_ms", JsonValue::CreateNumberValue(double(WarpContext::NowMs()+60000)));
            JsonArray features;
            for (const auto& value : rows)
            {
                auto feature = JsonObject::Parse(LR"({"found":true,"eligible":true,"fallback":"","score_delta":4.2,
                    "reasons":["observed_in_requested_interval"],"evidence_refs":["synthetic:alias"]})");
                feature.Insert(L"key", value.GetObject().GetNamedValue(L"key"));
                features.Append(feature);
            }
            response.Insert(L"candidates", features);
            return winrt::to_string(response.Stringify());
        }, [] { return false; });
        Check(assessed.available && assessed.features.size() == 2, "canonical response rejected for a short-path candidate");
        ApplyWarpContext(assessed);
        Check(searchResults.size() == 3, "context alias deduplication removed visible baseline rows");
        Check(std::all_of(searchResults.begin(), searchResults.end(),
            [](const SearchResult& result) { return result.warpBonus == 4.2 && !result.warpReason.empty(); }),
            "an alias or unrelated guide lost its supported context score");
        return true;
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

    void ProgressPumpYieldsWhenMessagesKeepArriving()
    {
        struct Flood { int dispatched = 0; };
        const wchar_t* name = L"FindMyFileProgressPumpTest";
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = name;
        cls.lpfnWndProc = [](HWND window,UINT message,WPARAM wp,LPARAM lp) -> LRESULT {
            if (message == WM_NCCREATE)
                SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
            if (message == WM_APP+99)
            {
                auto* flood = (Flood*)GetWindowLongPtrW(window,GWLP_USERDATA);
                if (++flood->dispatched < 2000) PostMessageW(window,WM_APP+99,0,0);
                return 0;
            }
            return DefWindowProcW(window,message,wp,lp);
        };
        Check(RegisterClassW(&cls)!=0,"progress test class registration failed");
        Flood flood;
        HWND window = CreateWindowExW(0,name,L"",0,0,0,1,1,nullptr,nullptr,cls.hInstance,&flood);
        Check(window!=nullptr,"progress test window unavailable");
        PostMessageW(window,WM_APP+99,0,0);
        UpdateProgress(15,L"Enumerating synthetic records");
        const int dispatched = flood.dispatched;
        MSG pending{};
        while (PeekMessageW(&pending,window,WM_APP+99,WM_APP+99,PM_REMOVE)) {}
        DestroyWindow(window); UnregisterClassW(name,cls.hInstance);
        Check(dispatched==0,"progress reporting dispatched reentrant work instead of returning to the worker");
    }

    void ProgressAdvancesAcrossBatchBoundaries()
    {
        EnumerationProgress progress; progress.lastTick=1000;
        Check(!progress.Due(49999,1050),"progress throttling reported too early");
        Check(progress.Due(50137,1051),"batch crossing 50,000 entries left enumeration status unchanged");
        Check(!progress.Due(50138,1052),"progress was not throttled");
        Check(progress.Due(50200,1151),"slow enumeration did not report elapsed progress");
    }

    void FilenameSearchCancelsWithoutBlockingInput()
    {
        SeedIndex(); QueueGuide(); ProcessPendingUsnChanges();
        HWND window=CreateWindowExW(0,L"STATIC",L"Async filename test",0,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
        HWND edit=CreateWindowExW(0,L"EDIT",L"old",WS_CHILD,0,0,1,1,window,(HMENU)IDC_SEARCH_EDIT,nullptr,nullptr);
        Check(window && edit,"async search controls unavailable");
        struct Cleanup {
            HWND window;
            ~Cleanup() {
                StopFilenameWorker();
                MSG message{};
                while(PeekMessageW(&message,window,WM_FILENAME_RESULT,WM_FILENAME_RESULT,PM_REMOVE))
                    delete (FilenameResult*)message.lParam;
                hSearchEdit=nullptr; DestroyWindow(window);
            }
        } cleanup{window};
        hSearchEdit=edit;
        StartFilenameWorker(window);
        {
            std::lock_guard<std::mutex> busy(indexDataMutex);
            TriggerSearch(window);
            SetWindowTextW(edit,L"eval");
            WndProc(window,WM_COMMAND,MAKEWPARAM(IDC_SEARCH_EDIT,EN_CHANGE),(LPARAM)edit);
            TriggerSearch(window);
        }
        MSG message{}; const auto until=GetTickCount64()+5000;
        while(!PeekMessageW(&message,window,WM_FILENAME_RESULT,WM_FILENAME_RESULT,PM_REMOVE) && GetTickCount64()<until)
            Sleep(5);
        Check(message.message==WM_FILENAME_RESULT,"latest background search never completed");
        auto* result=(FilenameResult*)message.lParam;
        const bool latest=result && result->request.query==L"eval" && result->request.generation==filenameGeneration;
        const bool found=result && std::any_of(result->rows.begin(),result->rows.end(),
            [](const SearchResult& row){return row.fileName==GuideName;});
        FinishFilenameSearch(window,message.lParam);
        Check(latest && found,"cancelled or incorrect filename results reached the UI");
    }

    void TypingDoesNotRebuildThousandsOfVisibleRows()
    {
        SeedIndex();
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
        HWND window=CreateWindowExW(0,L"STATIC",L"Virtual results test",0,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
        hSearchEdit=CreateWindowExW(0,L"EDIT",L"eval",WS_CHILD,0,0,1,1,window,(HMENU)IDC_SEARCH_EDIT,nullptr,nullptr);
        hListView=CreateWindowExW(0,WC_LISTVIEW,L"",WS_CHILD|LVS_REPORT|LVS_OWNERDATA,0,0,1,1,window,(HMENU)IDC_LISTVIEW,nullptr,nullptr);
        Check(window && hSearchEdit && hListView,"virtual results controls unavailable");
        int changes=0;
        SetWindowSubclass(hListView,[](HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context)->LRESULT {
            if(message==LVM_SETITEMCOUNT || message==LVM_DELETEALLITEMS || message==LVM_INSERTITEMW) ++*(int*)context;
            return DefSubclassProc(h,message,wp,lp);
        },1,(DWORD_PTR)&changes);
        SearchResult row{}; row.fileName=L"eval.md"; row.fullPath=L"C:\\Synthetic\\eval.md";
        row.baseScore=row.matchScore=75; row.warpBonus=1.6; row.warpReason=L"Observed";
        searchResults.assign(10000,row); baselineResults=searchResults; warpContextResult.available=true;
        PopulateSearchResults();
        Check(ListView_GetItemCount(hListView)==10000,"virtual result count missing");
        changes=0;
        WndProc(window,WM_COMMAND,MAKEWPARAM(IDC_SEARCH_EDIT,EN_CHANGE),(LPARAM)hSearchEdit);
        const bool unchanged=changes==0;
        wchar_t text[64]{};
        NMLVDISPINFOW info{}; info.hdr.idFrom=IDC_LISTVIEW; info.hdr.code=LVN_GETDISPINFOW;
        info.item.mask=LVIF_TEXT; info.item.iItem=9999; info.item.iSubItem=5; info.item.pszText=text; info.item.cchTextMax=64;
        WndProc(window,WM_NOTIFY,IDC_LISTVIEW,(LPARAM)&info);
        const bool cell=wcscmp(text,L"+1.6")==0;
        KillTimer(window,IDT_SEARCH_DEBOUNCE); DestroyWindow(window); hSearchEdit=hListView=nullptr;
        searchResults.clear(); baselineResults.clear();
        Check(unchanged && !warpContextResult.available,"typing synchronously rebuilt previous result rows");
        Check(cell,"virtual result text lost the Context+ column");
    }

    void IndexBuildCanBeCancelledBeforeItAcquiresTheIndex()
    {
        SeedIndex();
        HWND window=CreateWindowExW(0,L"STATIC",L"Index cancellation test",0,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
        Check(window!=nullptr,"index cancellation window unavailable");
        std::unique_lock<std::mutex> busy(indexDataMutex);
        struct Cleanup {
            HWND window; std::unique_lock<std::mutex>& busy;
            ~Cleanup() {
                indexBuildCancelled=true;
                if(busy.owns_lock()) busy.unlock();
                if(indexBuildThread.joinable()) indexBuildThread.join();
                MSG message{};
                while(PeekMessageW(&message,window,WM_INDEX_COMPLETE,WM_INDEX_COMPLETE,PM_REMOVE))
                    delete (IndexBuildResult*)message.lParam;
                indexBuildInProgress=false; indexBuildCancelled=false; closeAfterIndexBuild=false;
                DestroyWindow(window);
            }
        } cleanup{window,busy};
        StartIndexBuild(window,L"",false);
        Check(indexBuildInProgress,"background index work was not started");
        const auto worker=indexBuildThread.get_id();
        WndProc(window,WM_COMMAND,IDC_DRIVE_BUTTON_BASE,0);
        Check(indexBuildThread.get_id()==worker,"nested drive command started another build");
        WndProc(window,WM_CLOSE,0,0);
        Check(IsWindow(window) && indexBuildCancelled,"close destroyed the window while index work was active");
        busy.unlock();
        MSG message{}; const auto until=GetTickCount64()+5000;
        while(!PeekMessageW(&message,window,WM_INDEX_COMPLETE,WM_INDEX_COMPLETE,PM_REMOVE) && GetTickCount64()<until)
            Sleep(5);
        Check(message.message==WM_INDEX_COMPLETE,"cancelled index work did not finish");
        const bool cancelled=((IndexBuildResult*)message.lParam)->cancelled;
        FinishIndexBuild(window,message.lParam);
        Check(cancelled && !indexBuildInProgress && !usnCheckpointValid,"cancelled index became a persistable snapshot");
    }

    void SortedLookupGrowthStaysWithinPointerMemoryBudget()
    {
        SeedIndex();
        const std::wstring prefix = L"unique-eval-" + std::wstring(120,L'x') + L"-";
        for (int i = 0; i < 20000; ++i)
            invertedIndex[prefix+std::to_wstring(i)] = {12};
        decltype(sortedTokens){}.swap(sortedTokens);
        decltype(sortedStemTokens){}.swap(sortedStemTokens);
        {
            AllocationLimit limit((invertedIndex.size()+stemmedIndex.size())*sizeof(void*)+4096);
            RebuildSortedTokens();
        }
        size_t matches = 0;
        ForEachPrefixMatch(sortedTokens,invertedIndex,prefix,
            [&](const std::wstring&,const std::vector<ULONGLONG>&){ ++matches; });
        Check(matches == 20000, "prefix lookup lost tokens after memory-bounded rebuild");
        invertedIndex.rehash(invertedIndex.bucket_count()*2);
        matches = 0;
        ForEachPrefixMatch(sortedTokens,invertedIndex,prefix,
            [&](const std::wstring&,const std::vector<ULONGLONG>&){ ++matches; });
        Check(matches == 20000, "rehashing invalidated sorted lookup");
        invertedIndex.erase(prefix+L"0");
        RebuildSortedTokens();
        matches = 0;
        ForEachPrefixMatch(sortedTokens,invertedIndex,prefix,
            [&](const std::wstring&,const std::vector<ULONGLONG>&){ ++matches; });
        Check(matches == 19999, "deleted token remained in sorted lookup");
    }

    void AllocationFailurePreservesTheSavedCache()
    {
        SeedIndex();
        wchar_t temporary[MAX_PATH]; GetTempPathW(MAX_PATH,temporary);
        const auto folder = std::filesystem::path(temporary) /
            (L"findmyfile-memory-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(folder);
        struct Cleanup { std::filesystem::path path; ~Cleanup(){ std::error_code ec; std::filesystem::remove_all(path,ec); } } cleanup{folder};
        const auto cache = (folder/L"index.db").wstring();
        usnJournalId = 19; lastUsnProcessed = 1200; usnCheckpointValid = true;
        Check(SaveIndexToFile(cache),"initial cache save failed");
        const auto bytes = std::filesystem::file_size(cache);
        QueueGuide();
        bool applied = true;
        {
            AllocationLimit limit(0);
            applied = ProcessPendingUsnChanges();
        }
        Check(!applied && indexMemoryFailed && !usnCheckpointValid,"allocation failure escaped or remained persistable");
        Check(!SaveIndexToFile(cache),"partial update overwrote the previous cache");
        Check(std::filesystem::file_size(cache) == bytes,"cached snapshot changed after allocation failure");
        Check(LoadIndexFromFile(cache),"previous cache could not be recovered");
        Check(!fileMap.count(20) && !indexMemoryFailed,"partial journal row survived recovery");
        UsnChange timestamp{}; timestamp.fileRef = 12; timestamp.reason = USN_REASON_BASIC_INFO_CHANGE;
        pendingUsnChanges.push_back(timestamp);
        Check(!ProcessPendingUsnChanges(),"timestamp-only update incorrectly changed token index");
        currentSearchKeyword = L"old"; PerformSearch(L"old");
        Check(!searchResults.empty(),"timestamp-only update cleared prefix lookup");
    }

    void RepeatedCreateDoesNotRebuildUnchangedLookup()
    {
        SeedIndex();
        timestampCache[12]={};
        UsnChange change{};
        change.fileRef=12; change.parentRef=11; change.fileName=L"old-notes.txt";
        change.reason=USN_REASON_FILE_CREATE|USN_REASON_CLOSE;
        pendingUsnChanges.push_back(change);
        bool changed=true;
        { AllocationLimit limit(0); changed=ProcessPendingUsnChanges(); }
        Check(!changed && !sortedTokensDirty && !indexMemoryFailed,
            "repeated create/close invalidated an unchanged filename lookup");
    }

    void CatchUpDefersLargeLookupRebuildUntilSearch()
    {
        SeedIndex();
        for (int i=0; i<20000; ++i)
            invertedIndex[L"existing-eval-"+std::to_wstring(i)] = {12};
        RebuildSortedTokens();
        decltype(sortedTokens){}.swap(sortedTokens);
        QueueGuide();
        bool applied = false;
        {
            AllocationLimit limit(16384);
            applied = ProcessPendingUsnChanges();
        }
        Check(applied && !indexMemoryFailed,"catch-up rebuilt the entire token view under memory pressure");
        CheckGuideRequest();
        std::vector<UsnChange> batch(MAX_UI_USN_CHANGES+1);
        for (size_t i=0;i<batch.size();++i)
        {
            batch[i].fileRef = 100000+i; batch[i].reason = USN_REASON_BASIC_INFO_CHANGE;
        }
        pendingUsnChanges = std::move(batch);
        ProcessPendingUsnChanges();
        Check(timestampCache.count(100000+MAX_UI_USN_CHANGES-1)==1 &&
            timestampCache.count(100000+MAX_UI_USN_CHANGES)==0,"UI catch-up exceeded its per-turn work bound");
        StopUsnMonitor();
        Check(timestampCache.count(100000+MAX_UI_USN_CHANGES)==1,"shutdown did not drain the remaining checkpoint changes");
    }

    void CatchUpYieldsToKeyboardMessages()
    {
        SeedIndex();
        HWND window=CreateWindowExW(0,L"STATIC",L"Catch-up fairness test",0,0,0,1,1,nullptr,nullptr,nullptr,nullptr);
        Check(window!=nullptr,"catch-up fairness window unavailable");
        hMainWnd=window;
        pendingUsnChanges.resize(MAX_UI_USN_CHANGES+1);
        for(auto& change:pendingUsnChanges) {change.fileRef=12;change.reason=USN_REASON_BASIC_INFO_CHANGE;}
        ProcessPendingUsnChanges();
        MSG message{};
        const bool immediatelyPosted=PeekMessageW(&message,window,WM_USN_UPDATED,WM_USN_UPDATED,PM_REMOVE)!=FALSE;
        Sleep(25);
        const bool timer=PeekMessageW(&message,window,WM_TIMER,WM_TIMER,PM_REMOVE)!=FALSE;
        KillTimer(window,IDT_INDEX_UPDATES); hMainWnd=nullptr; DestroyWindow(window);
        StopUsnMonitor();
        Check(!immediatelyPosted && timer,"continuous posted catch-up work can starve keyboard input");
    }

    void CatchUpBackpressureIsBoundedAndCancellable()
    {
        SeedIndex();
        std::vector<UsnChange> full(MAX_PENDING_USN_CHANGES);
        for (auto& change:full) { change.fileRef = 12; change.reason = USN_REASON_BASIC_INFO_CHANGE; }
        auto run = [&](bool stop) {
            bUsnMonitorRunning = true; lastUsnProcessed = 100;
            Check(QueueUsnBatch(full,200),"initial catch-up batch rejected");
            std::promise<void> entered; auto entering = entered.get_future();
            auto producer = std::async(std::launch::async,[&] {
                std::vector<UsnChange> next(1);
                next[0].fileRef = 12; next[0].reason = USN_REASON_BASIC_INFO_CHANGE;
                entered.set_value();
                return QueueUsnBatch(next,300);
            });
            entering.wait();
            const bool blocked = producer.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
            size_t queued = 0;
            { std::lock_guard<std::mutex> lock(usnMutex); queued = pendingUsnChanges.size(); }
            if (stop) StopUsnMonitor(); else ProcessPendingUsnChanges();
            const bool finished = producer.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            if (!finished) { bUsnMonitorRunning = false; usnQueueSpace.notify_all(); }
            const bool accepted = producer.get();
            StopUsnMonitor();
            Check(blocked && queued == MAX_PENDING_USN_CHANGES,"catch-up producer ran beyond its memory bound");
            Check(finished && accepted == !stop,"blocked catch-up did not resume/cancel correctly");
            Check(lastUsnProcessed == (stop ? 200 : 300),"journal cursor skipped an unqueued batch");
        };
        run(false);
        run(true);
    }

    void ReplayExistingCacheStartup()
    {
        const auto started = GetTickCount64();
        Check(LoadIndexFromFile(),"existing cached index unavailable");
        const auto count = fileMap.size();
        ULONGLONG first = 0;
        for (const auto& pair:fileMap) first = (std::max)(first,pair.first);
        ++first;
        const std::wstring prefix = L"fmfstartupreplay"+std::to_wstring(GetCurrentProcessId())+L"-";
        const size_t records = 1024;
        for (bool remove : {false,true})
        {
            std::vector<UsnChange> batch(records);
            for (size_t i=0;i<records;++i)
            {
                batch[i].fileRef = first+i; batch[i].parentRef = 5;
                batch[i].fileName = prefix+std::to_wstring(i)+L".txt";
                batch[i].reason = remove ? USN_REASON_FILE_DELETE : USN_REASON_FILE_CREATE;
            }
            bUsnMonitorRunning = true;
            Check(QueueUsnBatch(batch,lastUsnProcessed),"synthetic catch-up batch rejected");
            for (;;)
            {
                { std::lock_guard<std::mutex> lock(usnMutex); if(pendingUsnChanges.empty()) break; }
                ProcessPendingUsnChanges();
                Check(!indexMemoryFailed,"cached catch-up exhausted memory");
            }
            currentSearchKeyword = prefix;
            PerformSearch(prefix);
            Check(!indexMemoryFailed,"cached lookup rebuild exhausted memory");
            Check(searchResults.size() == (remove ? 0 : records),"cached search missed synthetic create/delete changes");
        }
        StopUsnMonitor();
        PROCESS_MEMORY_COUNTERS_EX memory{};
        GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        std::cout << "PASS existing_cache_startup_replay entries=" << count
            << " synthetic_changes=" << records*2 << " pointer_bits=" << sizeof(void*)*8
            << " private_mb=" << memory.PrivateUsage/(1024*1024)
            << " elapsed_ms=" << GetTickCount64()-started << " cache_written=false\n";
    }

    void ProfileDriveEnumeration(const std::wstring& drive)
    {
        Check(drive.size()==1 && iswalpha(drive[0]),"provide one drive letter");
        int files=0, directories=0;
        const auto started=GetTickCount64();
        Check(ReadDriveMFT(nullptr,drive,files,directories),"read-only MFT enumeration failed");
        const auto enumerated=GetTickCount64();
        BuildSearchIndex(drive);
        PROCESS_MEMORY_COUNTERS_EX memory{};
        GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
        std::cout << "PASS full_drive_enumeration files=" << files << " directories=" << directories
            << " enumeration_ms=" << enumerated-started << " indexing_ms=" << GetTickCount64()-enumerated
            << " pointer_bits=" << sizeof(void*)*8 << " private_mb=" << memory.PrivateUsage/(1024*1024)
            << " cache_written=false\n";
    }

    void ProfileCachedTyping()
    {
        Check(LoadIndexFromFile(),"existing cache unavailable");
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
        struct State {
            std::atomic<bool> typed{false},applied{false},failed{false};
            std::vector<double> inputMs;
        } state;
        WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpszClassName=L"FindMyFileCachedTypingProfile";
        cls.lpfnWndProc=[](HWND window,UINT message,WPARAM wp,LPARAM lp)->LRESULT {
            if(message==WM_NCCREATE)
                SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
            if(message==WM_COMMAND || message==WM_TIMER || message==WM_FILENAME_RESULT || message==WM_NOTIFY)
            {
                const auto result=WndProc(window,message,wp,lp);
                auto* state=(State*)GetWindowLongPtrW(window,GWLP_USERDATA);
                if(message==WM_FILENAME_RESULT && currentSearchKeyword==L"eval" && state->typed) state->applied=true;
                return result;
            }
            return DefWindowProcW(window,message,wp,lp);
        };
        Check(RegisterClassW(&cls)!=0,"typing profile class unavailable");
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"Read-only FindMyFile typing measurement",0,0,0,640,480,
            nullptr,nullptr,cls.hInstance,&state);
        Check(window!=nullptr,"typing profile window unavailable");
        hSearchEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,600,26,window,(HMENU)IDC_SEARCH_EDIT,nullptr,nullptr);
        hListView=CreateWindowExW(0,WC_LISTVIEW,L"",WS_CHILD|LVS_REPORT|LVS_OWNERDATA,0,30,600,400,window,(HMENU)IDC_LISTVIEW,nullptr,nullptr);
        bTreeRenderedOnce=true;
        StartFilenameWorker(window);
        const auto started=GetTickCount64();
        std::thread input([&] {
            for(const auto* query:{L"e",L"ev",L"eva",L"eval"})
            {
                const auto begin=std::chrono::steady_clock::now();
                if(wcscmp(query,L"eval")==0) state.typed=true;
                DWORD_PTR ignored=0;
                if(!SendMessageTimeoutW(hSearchEdit,WM_SETTEXT,0,(LPARAM)query,SMTO_ABORTIFHUNG,5000,&ignored))
                {state.failed=true;return;}
                state.inputMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
                Sleep(400);
            }
        });
        const auto until=GetTickCount64()+60000;
        while(!state.failed && !state.applied && GetTickCount64()<until)
        {
            MSG message{};
            if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message);DispatchMessageW(&message);}
            else MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        input.join();
        StopFilenameWorker();
        MSG pending{};
        while(PeekMessageW(&pending,window,WM_FILENAME_RESULT,WM_FILENAME_RESULT,PM_REMOVE))
            delete (FilenameResult*)pending.lParam;
        KillTimer(window,IDT_SEARCH_DEBOUNCE);
        const auto rows=searchResults.size(),entries=fileMap.size();
        const bool ok=state.applied && !state.failed;
        DestroyWindow(window); UnregisterClassW(cls.lpszClassName,cls.hInstance);
        hSearchEdit=hListView=nullptr; bTreeRenderedOnce=false;
        Check(ok && !state.inputMs.empty(),"cached typing did not complete");
        std::cout << "PASS cached_typing entries=" << entries << " result_count=" << rows
            << " pointer_bits=" << sizeof(void*)*8
            << " input_max_ms=" << *std::max_element(state.inputMs.begin(),state.inputMs.end())
            << " elapsed_ms=" << GetTickCount64()-started << " cache_written=false\n";
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
        if (argc==3 && wcscmp(argv[1],L"--enumeration-profile")==0)
        {
            ProfileDriveEnumeration(argv[2]);
            RoUninitialize();
            return 0;
        }
        if (argc==2 && wcscmp(argv[1],L"--typing-profile")==0)
        {
            ProfileCachedTyping();
            RoUninitialize();
            return 0;
        }
        if (argc == 2 && wcscmp(argv[1],L"--startup-replay") == 0)
        {
            ReplayExistingCacheStartup();
            RoUninitialize();
            return 0;
        }
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
        if (ShortPathAliasesShareOneContextAssessment())
            std::cout << "PASS ShortPathAliasesShareOneContextAssessment\n";
        else std::cout << "SKIP ShortPathAliasesShareOneContextAssessment (volume does not create 8.3 aliases)\n";
        LiveIndexUpdatesDoNotRestartTheDisplayedSearch();
        std::cout << "PASS LiveIndexUpdatesDoNotRestartTheDisplayedSearch\n";
        ProgressPumpYieldsWhenMessagesKeepArriving();
        std::cout << "PASS ProgressPumpYieldsWhenMessagesKeepArriving\n";
        ProgressAdvancesAcrossBatchBoundaries();
        std::cout << "PASS ProgressAdvancesAcrossBatchBoundaries\n";
        FilenameSearchCancelsWithoutBlockingInput();
        std::cout << "PASS FilenameSearchCancelsWithoutBlockingInput\n";
        TypingDoesNotRebuildThousandsOfVisibleRows();
        std::cout << "PASS TypingDoesNotRebuildThousandsOfVisibleRows\n";
        IndexBuildCanBeCancelledBeforeItAcquiresTheIndex();
        std::cout << "PASS IndexBuildCanBeCancelledBeforeItAcquiresTheIndex\n";
        SortedLookupGrowthStaysWithinPointerMemoryBudget();
        std::cout << "PASS SortedLookupGrowthStaysWithinPointerMemoryBudget\n";
        AllocationFailurePreservesTheSavedCache();
        std::cout << "PASS AllocationFailurePreservesTheSavedCache\n";
        RepeatedCreateDoesNotRebuildUnchangedLookup();
        std::cout << "PASS RepeatedCreateDoesNotRebuildUnchangedLookup\n";
        CatchUpDefersLargeLookupRebuildUntilSearch();
        std::cout << "PASS CatchUpDefersLargeLookupRebuildUntilSearch\n";
        CatchUpYieldsToKeyboardMessages();
        std::cout << "PASS CatchUpYieldsToKeyboardMessages\n";
        CatchUpBackpressureIsBoundedAndCancellable();
        std::cout << "PASS CatchUpBackpressureIsBoundedAndCancellable\n";
        RoUninitialize();
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    RoUninitialize();
    return 1;
}
