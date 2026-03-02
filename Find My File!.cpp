// Find My File!.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "Find My File!.h"
#include <winioctl.h>
#include <vector>
#include <string>
#include <map>
#include <commctrl.h>
#include <algorithm>
#include <unordered_map>
#include <shellapi.h>
#include <shlwapi.h>
#include <cctype>
#include <set>
#include <uxtheme.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
#include <shlobj.h>
#include <winhttp.h>
#include <functional>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "winhttp.lib")

#include <ole2.h>
#include <oledb.h>
#include <msdasc.h>
#include <comdef.h>

// Enable visual styles (Common Controls v6)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"" )

#define MAX_LOADSTRING 100
#define IDC_DRIVE_BUTTON_BASE 2000
#define IDC_SEARCH_EDIT     3000
#define IDC_FIND_BUTTON     3001
#define IDC_WINSEARCH_BUTTON 3004
#define IDC_TAB_CONTROL     3002
#define IDC_LISTVIEW        3003
#define IDM_CTX_OPEN        4000
#define IDM_CTX_COPY_PATH   4001
#define IDM_CTX_OPEN_FOLDER 4002
#define IDC_DARKMODE_BTN    5000
#define IDT_SEARCH_DEBOUNCE 6000
#define SEARCH_DEBOUNCE_MS  300
#define IDT_USN_POLL        6001
#define USN_POLL_MS         2000
#define WM_USN_UPDATED      (WM_USER + 1)
#define WM_OAUTH_COMPLETE   (WM_USER + 2)
#define IDC_PROPERTIES      7000
#define IDC_PROP_TOGGLE     7001
#define PROPERTIES_PANEL_W  420
#define SPLITTER_WIDTH      5
#define MIN_LIST_WIDTH      200
#define MIN_PROP_WIDTH      150
#define IDC_CLOUD_BTN       8000

// Global Variables:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hTreeView = NULL;
HWND hProgressBar = NULL;
HWND hStatusText = NULL;
HWND hSummaryText = NULL;
HWND hTabControl = NULL;
HWND hSearchEdit = NULL;
HWND hFindButton = NULL;
HWND hListView = NULL;
HWND hWinSearchButton = NULL;
std::wstring currentDriveLetter;
std::wstring currentSearchKeyword;
std::wstring currentSearchScope;
bool bWildcardSearch = false;  // true when current search uses wildcard/glob pattern
bool bWindowsSearch = false;   // true when results came from Windows Search indexer
HFONT hUIFont = NULL;
HBRUSH hToolbarBrush = NULL;
HBRUSH hStatusBrush = NULL;
HBRUSH hEditBrush = NULL;
HWND hDarkModeBtn = NULL;
bool bDarkMode = false;
bool bTreeRenderedOnce = false;
HIMAGELIST hSysImageList = NULL;
std::unordered_map<std::wstring, int> iconCache;  // extension -> icon index
int iFolderIcon = -1;  // cached folder icon index

// USN journal live monitor
std::thread usnMonitorThread;
std::atomic<bool> bUsnMonitorRunning{false};
std::mutex usnMutex;  // protects pendingUsnChanges
struct UsnChange {
    ULONGLONG fileRef;
    ULONGLONG parentRef;
    std::wstring fileName;
    bool isDirectory;
    DWORD reason;
    LARGE_INTEGER timestamp;
};
std::vector<UsnChange> pendingUsnChanges;
USN lastUsnProcessed = 0;
ULONGLONG usnJournalId = 0;
HWND hMainWnd = NULL;

// Properties panel
HWND hPropertiesPanel = NULL;
HBRUSH hPropertiesBrush = NULL;
HWND hPropToggleBtn = NULL;
HWND hPropToggleTooltip = NULL;
bool bPropertiesCollapsed = false;
HFONT hToggleFont = NULL;       // larger font for the toggle button glyph
HFONT hHeaderFont = NULL;       // bold font for ListView column headers
bool bToggleHovered = false;    // mouse is hovering over the toggle button

// Cloud storage providers — OAuth2-based API access (no local sync required)
HWND hCloudBtn = NULL;
enum CloudProviderId { CP_ONEDRIVE = 0, CP_GOOGLEDRIVE = 1, CP_ICLOUD = 2, CP_COUNT = 3 };
struct CloudProvider {
    CloudProviderId id;
    std::wstring name;
    std::wstring clientId;
    std::wstring clientSecret;
    std::wstring accessToken;
    std::wstring refreshToken;
    bool loggedIn;
};
CloudProvider cloudProviders[CP_COUNT] = {
    { CP_ONEDRIVE,    L"OneDrive",      L"", L"", L"", L"", false },
    { CP_GOOGLEDRIVE, L"Google Drive",   L"", L"", L"", L"", false },
    { CP_ICLOUD,      L"iCloud Drive",   L"", L"", L"", L"", false },
};

// OAuth background login state
static SOCKET oauthListenSock = INVALID_SOCKET;  // listen socket; closeable to cancel
static std::thread oauthThread;                   // background thread running OAuth flow
static std::atomic<bool> bOAuthInProgress{false};  // true while an OAuth login is running
static CloudProviderId oauthActiveProvider;         // which provider is being logged in
static HWND hOAuthDlg = NULL;                      // dialog to notify on completion
static bool oauthSuccess = false;                  // result of the background OAuth flow

// Splitter between list view and properties panel
int propertiesPanelW = PROPERTIES_PANEL_W;  // current width (resizable)
bool bSplitterDragging = false;
int splitterDragStartX = 0;
int splitterDragStartW = 0;
RECT splitterRect = {0, 0, 0, 0};  // cached hit-test rect in client coords

struct ColorScheme {
    COLORREF toolbarBg;
    COLORREF statusBg;
    COLORREF statusText;
    COLORREF windowBg;
    COLORREF windowText;
    COLORREF editBg;
    COLORREF editText;
    COLORREF treeBg;
    COLORREF treeText;
    COLORREF listBg;
    COLORREF listText;
    COLORREF highlightBg;
    COLORREF highlightText;
    COLORREF matchHighlight;
    COLORREF buttonBg;
    COLORREF buttonText;
    COLORREF buttonBorder;
    COLORREF buttonDisabledBg;
    COLORREF buttonDisabledText;
    COLORREF headerBg;
    COLORREF headerText;
    COLORREF headerBorder;
};

static const ColorScheme LightScheme = {
    RGB(240, 243, 249),  // toolbarBg
    RGB(230, 235, 245),  // statusBg
    RGB(50, 50, 50),     // statusText
    RGB(255, 255, 255),  // windowBg
    RGB(0, 0, 0),        // windowText
    RGB(255, 255, 255),  // editBg
    RGB(0, 0, 0),        // editText
    RGB(255, 255, 255),  // treeBg
    RGB(0, 0, 0),        // treeText
    RGB(255, 255, 255),  // listBg
    RGB(0, 0, 0),        // listText
    RGB(0, 120, 215),    // highlightBg
    RGB(255, 255, 255),  // highlightText
    RGB(255, 255, 0),    // matchHighlight
    RGB(225, 228, 235),  // buttonBg
    RGB(30, 30, 30),     // buttonText
    RGB(180, 185, 195),  // buttonBorder
    RGB(235, 235, 235),  // buttonDisabledBg
    RGB(160, 160, 160),  // buttonDisabledText
    RGB(234, 238, 246),  // headerBg
    RGB(45, 50, 65),     // headerText
    RGB(200, 206, 218),  // headerBorder
};

static const ColorScheme DarkScheme = {
    RGB(40, 40, 40),     // toolbarBg
    RGB(50, 50, 55),     // statusBg
    RGB(210, 210, 210),  // statusText
    RGB(30, 30, 30),     // windowBg
    RGB(220, 220, 220),  // windowText
    RGB(50, 50, 55),     // editBg
    RGB(220, 220, 220),  // editText
    RGB(30, 30, 30),     // treeBg
    RGB(220, 220, 220),  // treeText
    RGB(30, 30, 30),     // listBg
    RGB(220, 220, 220),  // listText
    RGB(60, 90, 140),    // highlightBg
    RGB(255, 255, 255),  // highlightText
    RGB(180, 150, 0),    // matchHighlight
    RGB(65, 65, 70),     // buttonBg
    RGB(220, 220, 220),  // buttonText
    RGB(100, 100, 110),  // buttonBorder
    RGB(50, 50, 55),     // buttonDisabledBg
    RGB(100, 100, 100),  // buttonDisabledText
    RGB(45, 47, 52),     // headerBg
    RGB(190, 195, 210),  // headerText
    RGB(65, 68, 78),     // headerBorder
};

static const ColorScheme& CurrentScheme() { return bDarkMode ? DarkScheme : LightScheme; }

// Structure to store file information
struct FileEntry {
    ULONGLONG fileReference;
    ULONGLONG parentReference;
    std::wstring fileName;
    std::wstring lowerFileName;  // pre-computed lowercased filename for fast search
    bool isDirectory;
    bool hasChildren;       // cached: does this dir have children in childrenMap?
    bool childrenLoaded;    // have we populated children into the TreeView?
};

// Data for the currently loaded drive
std::unordered_map<ULONGLONG, FileEntry> fileMap;
std::unordered_map<ULONGLONG, std::vector<ULONGLONG>> childrenMap;
// Map from HTREEITEM back to file reference for on-demand loading
std::unordered_map<HTREEITEM, ULONGLONG> treeItemToRef;

// Inverted index: lowercased token -> list of file references containing that token
std::unordered_map<std::wstring, std::vector<ULONGLONG>> invertedIndex;
// Stemmed index: stemmed token -> list of file references
std::unordered_map<std::wstring, std::vector<ULONGLONG>> stemmedIndex;
// Sorted token lists for O(log N) prefix lookups during search
std::vector<std::wstring> sortedTokens;
std::vector<std::wstring> sortedStemTokens;
// Trigram index: 3-char hash -> sorted list of file references containing that trigram.
// Used to eliminate the O(N) fileMap scan for substring matching.
std::unordered_map<ULONGLONG, std::vector<ULONGLONG>> trigramIndex;
// Cached full paths for each file reference
std::unordered_map<ULONGLONG, std::wstring> fullPathCache;
// Cached timestamps (USN timestamp) for each file reference
std::unordered_map<ULONGLONG, LARGE_INTEGER> timestampCache;

// WARP file activity recency: lowercased full path -> activity info.
// Tracks both the most recent timestamp and whether the activity looks like
// genuine user interaction vs. system-generated noise (Explorer thumbnails,
// antivirus scans, etc.).
struct WarpActivityInfo {
    ULONGLONG timestamp;    // most recent Unix epoch seconds (UTC)
    bool hasDirectAction;   // true if any CREATE/MODIFY/DELETE/RENAME was seen
    int totalOpens;         // total OPEN events recorded for this path
    int batchOpens;         // how many of those OPENs fell in a batch bucket
                            // (>= BATCH_THRESHOLD siblings in same time window)
};
std::unordered_map<std::wstring, WarpActivityInfo> warpRecencyByPath;
bool bWarpAvailable = false;  // true if WARP service responded on last query

// Search result for display
struct SearchResult {
    ULONGLONG fileRef;
    std::wstring fileName;
    std::wstring fullPath;
    std::wstring contentSnippet;  // content match preview from Windows Search
    std::wstring cloudSource;     // provider name for cloud results (e.g. L"OneDrive")
    bool isDirectory;
    int matchScore;  // higher = better match (includes all bonuses)
    int baseScore = 0;   // match type score before any bonuses (for quality label)
    int warpBonus = 0;   // WARP-specific recency boost included in matchScore (0 if none)
};
std::vector<SearchResult> searchResults;

// Drive buttons
struct DriveButton {
    HWND hButton;
    std::wstring driveLetter;
};
std::vector<DriveButton> driveButtons;

// Forward declarations
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void                ReadDriveAndBuildTree(HWND hWnd, const std::wstring& drive);
void                BuildTreeView(const std::wstring& drive);
HTREEITEM           InsertTreeItem(HWND hTree, HTREEITEM hParent, const std::wstring& text, ULONGLONG fileRef, bool hasChildren);
void                UpdateProgress(int percentage, const std::wstring& status);
void                PopulateChildren(ULONGLONG parentRef, HTREEITEM hParentItem);
bool                ReadDriveMFT(HWND hWnd, const std::wstring& drive, int& totalFiles, int& totalDirs);
std::vector<std::wstring> GetFixedNtfsDrives();
void                CreateDriveButtons(HWND hWnd);
void                LayoutControls(HWND hWnd);
void                BuildSearchIndex(const std::wstring& drive);
std::wstring        StemWord(const std::wstring& word);
std::wstring        BuildFullPath(ULONGLONG fileRef, const std::wstring& drive);
void                PerformSearch(const std::wstring& keyword, ULONGLONG scopeRef = 0);
void                PopulateSearchResults();
void                LaunchSearchResult(int index);
void                ShowTab(int tabIndex);
void                TriggerSearch(HWND hWnd);
int                 GetFileIconIndex(const std::wstring& fileName, bool isDirectory);
void                AddFileToIndex(ULONGLONG fileRef, const std::wstring& lowerName);
void                RemoveFileFromIndex(ULONGLONG fileRef, const std::wstring& lowerName);
void                StartUsnMonitor(const std::wstring& drive);
void                StopUsnMonitor();
void                ProcessPendingUsnChanges();
void                UpdatePropertiesPanel(int selectedIndex);
void                RebuildSortedTokens();
void                TriggerWindowsSearch(HWND hWnd);
void                LoadCloudConfig();
void                SaveCloudConfig();
void                UpdateCloudButtonLabel();
INT_PTR CALLBACK    CloudConfigDlgProc(HWND, UINT, WPARAM, LPARAM);
bool                CloudOAuth2Login(HWND hParent, CloudProviderId id);
void                CloudLogout(CloudProviderId id);
std::vector<SearchResult> CloudSearchFiles(CloudProviderId id, const std::wstring& keyword);
static std::string  WinHttpGet(const std::wstring& host, const std::wstring& path, const std::wstring& authHeader);
static std::string  WinHttpPost(const std::wstring& host, const std::wstring& path, const std::string& body, const std::wstring& contentType);
static std::wstring Utf8ToWide(const std::string& s);
static std::string  WideToUtf8(const std::wstring& s);
static std::wstring UrlEncode(const std::wstring& s);
static std::string  JsonGetString(const std::string& json, const std::string& key);
static std::vector<SearchResult> JsonParseOneDriveResults(const std::string& json);
static std::vector<SearchResult> JsonParseGoogleDriveResults(const std::string& json);
static void QueryWarpActivity();

// Update the tooltip text for the properties toggle button to reflect its current action.
static void UpdateToggleTooltip()
{
    if (!hPropToggleTooltip || !hPropToggleBtn)
        return;
    TOOLINFOW ti = {0};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(hPropToggleBtn);
    ti.uId = (UINT_PTR)hPropToggleBtn;
    ti.lpszText = (LPWSTR)(bPropertiesCollapsed ? L"Show properties panel" : L"Hide properties panel");
    SendMessage(hPropToggleTooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

static void ApplyFontToAllChildren(HWND hParent, HFONT hFont)
{
    HWND hChild = GetWindow(hParent, GW_CHILD);
    while (hChild)
    {
        SendMessage(hChild, WM_SETFONT, (WPARAM)hFont, TRUE);
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
}

static void ApplyTheme(HWND hWnd)
{
    const ColorScheme& cs = CurrentScheme();

    if (hToolbarBrush) DeleteObject(hToolbarBrush);
    hToolbarBrush = CreateSolidBrush(cs.toolbarBg);
    if (hStatusBrush) DeleteObject(hStatusBrush);
    hStatusBrush = CreateSolidBrush(cs.statusBg);
    if (hEditBrush) DeleteObject(hEditBrush);
    hEditBrush = CreateSolidBrush(cs.editBg);

    if (hTreeView)
    {
        TreeView_SetBkColor(hTreeView, cs.treeBg);
        TreeView_SetTextColor(hTreeView, cs.treeText);
    }
    if (hListView)
    {
        ListView_SetBkColor(hListView, cs.listBg);
        ListView_SetTextBkColor(hListView, cs.listBg);
        ListView_SetTextColor(hListView, cs.listText);
    }
    if (hTreeView)
        SetWindowTheme(hTreeView, bDarkMode ? L"DarkMode_Explorer" : L"Explorer", NULL);
    if (hListView)
        SetWindowTheme(hListView, bDarkMode ? L"DarkMode_Explorer" : L"Explorer", NULL);
    if (hDarkModeBtn)
        SetWindowTextW(hDarkModeBtn, bDarkMode ? L"\u2600 Light" : L"\u263D Dark");

    if (hPropertiesBrush) { DeleteObject(hPropertiesBrush); hPropertiesBrush = NULL; }
    hPropertiesBrush = CreateSolidBrush(cs.editBg);

    InvalidateRect(hWnd, NULL, TRUE);
    HWND hChild = GetWindow(hWnd, GW_CHILD);
    while (hChild)
    {
        InvalidateRect(hChild, NULL, TRUE);
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }

    // Force the properties panel to fully repaint its background on theme change;
    // a plain InvalidateRect is not enough for an EDIT control that caches its brush.
    if (hPropertiesPanel)
        RedrawWindow(hPropertiesPanel, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME);
    if (hSearchEdit)
        RedrawWindow(hSearchEdit, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME);
}

static const int TOOLBAR_ROW_HEIGHT = 44;

std::vector<std::wstring> GetFixedNtfsDrives()
{
    std::vector<std::wstring> drives;
    DWORD driveMask = GetLogicalDrives();
    
    for (int i = 0; i < 26; i++)
    {
        if (driveMask & (1 << i))
        {
            wchar_t driveLetter = L'A' + i;
            std::wstring drivePath = std::wstring(1, driveLetter) + L":\\";
            
            UINT driveType = GetDriveTypeW(drivePath.c_str());
            
            if (driveType == DRIVE_FIXED)
            {
                wchar_t fsName[MAX_PATH];
                if (GetVolumeInformationW(drivePath.c_str(), NULL, 0, NULL, NULL, NULL, fsName, MAX_PATH))
                {
                    if (wcscmp(fsName, L"NTFS") == 0)
                    {
                        drives.push_back(std::wstring(1, driveLetter));
                    }
                }
            }
        }
    }
    
    return drives;
}

void UpdateProgress(int percentage, const std::wstring& status)
{
    if (hProgressBar)
    {
        SendMessage(hProgressBar, PBM_SETPOS, (WPARAM)percentage, 0);
    }
    
    if (hStatusText)
    {
        SetWindowTextW(hStatusText, status.c_str());
    }
    
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// Insert a tree item. If hasChildren is true, adds cChildren=1 so the [+] button appears.
HTREEITEM InsertTreeItem(HWND hTree, HTREEITEM hParent, const std::wstring& text, ULONGLONG fileRef, bool hasChildren)
{
    TVINSERTSTRUCT tvins = {0};
    tvins.hParent = hParent;
    tvins.hInsertAfter = TVI_LAST;
    tvins.item.mask = TVIF_TEXT | TVIF_CHILDREN | TVIF_PARAM;
    tvins.item.pszText = (LPWSTR)text.c_str();
    tvins.item.cchTextMax = (int)text.length();
    tvins.item.cChildren = hasChildren ? 1 : 0;
    tvins.item.lParam = (LPARAM)fileRef;

    HTREEITEM hItem = (HTREEITEM)SendMessage(hTree, TVM_INSERTITEM, 0, (LPARAM)&tvins);
    if (hItem)
    {
        treeItemToRef[hItem] = fileRef;
    }
    return hItem;
}

// Populate one level of children for a directory node (on-demand)
void PopulateChildren(ULONGLONG parentRef, HTREEITEM hParentItem)
{
    auto childIt = childrenMap.find(parentRef);
    if (childIt == childrenMap.end())
        return;

    // Sort children: directories first, then by name
    std::vector<ULONGLONG>& children = childIt->second;
    std::sort(children.begin(), children.end(), [](ULONGLONG a, ULONGLONG b) {
        auto itA = fileMap.find(a);
        auto itB = fileMap.find(b);
        if (itA == fileMap.end()) return false;
        if (itB == fileMap.end()) return true;
        if (itA->second.isDirectory != itB->second.isDirectory)
            return itA->second.isDirectory > itB->second.isDirectory;
        return _wcsicmp(itA->second.fileName.c_str(), itB->second.fileName.c_str()) < 0;
    });

    SendMessage(hTreeView, WM_SETREDRAW, FALSE, 0);

    for (ULONGLONG childRef : children)
    {
        auto it = fileMap.find(childRef);
        if (it == fileMap.end())
            continue;

        FileEntry& child = it->second;

        if (child.fileName.empty() || child.fileName.length() > 255)
            continue;

        bool childHasChildren = false;
        if (child.isDirectory)
        {
            childHasChildren = child.hasChildren;
        }

        InsertTreeItem(hTreeView, hParentItem, child.fileName, childRef, childHasChildren);
    }

    SendMessage(hTreeView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTreeView, NULL, TRUE);
}

// Build full path for a file reference by walking up the parent chain
std::wstring BuildFullPath(ULONGLONG fileRef, const std::wstring& drive)
{
    // Check cache first
    auto cacheIt = fullPathCache.find(fileRef);
    if (cacheIt != fullPathCache.end())
        return cacheIt->second;

    std::vector<std::wstring> parts;
    ULONGLONG current = fileRef;
    int depth = 0;

    while (current != 5 && depth < 512)
    {
        auto it = fileMap.find(current);
        if (it == fileMap.end())
            break;
        parts.push_back(it->second.fileName);
        ULONGLONG parentRef = it->second.parentReference & 0x0000FFFFFFFFFFFF;
        if (parentRef == current)
            break;
        current = parentRef;
        depth++;
    }

    std::wstring path = drive + L":\\";
    for (int i = (int)parts.size() - 1; i >= 0; i--)
    {
        path += parts[i];
        if (i > 0)
            path += L"\\";
    }

    fullPathCache[fileRef] = path;
    return path;
}

// Helper: check if a wide string ends with a given suffix
static bool EndsWith(const std::wstring& s, const std::wstring& suffix)
{
    if (suffix.length() > s.length()) return false;
    return s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0;
}

// Helper: count vowel-consonant transitions (a rough measure of word complexity)
static int MeasureWord(const std::wstring& s)
{
    static const std::wstring vowels = L"aeiou";
    int m = 0;
    bool inVowel = false;
    for (size_t i = 0; i < s.length(); i++)
    {
        bool isV = vowels.find(s[i]) != std::wstring::npos;
        if (isV && !inVowel)
            inVowel = true;
        else if (!isV && inVowel)
        {
            m++;
            inVowel = false;
        }
    }
    return m;
}

// Simplified Porter-style stemmer for English words (operates on lowercased wstring).
// Strips common suffixes to produce a root form so that morphological variants
// like "walking", "walked", "walker" all reduce to "walk".
std::wstring StemWord(const std::wstring& word)
{
    // Only stem words that are long enough and purely alphabetic
    if (word.length() < 4)
        return word;

    for (auto ch : word)
    {
        if (ch < L'a' || ch > L'z')
            return word;
    }

    std::wstring stem = word;

    // Step 1: Plurals and past participles
    if (EndsWith(stem, L"sses"))
        stem = stem.substr(0, stem.length() - 2);
    else if (EndsWith(stem, L"ies") && stem.length() > 4)
        stem = stem.substr(0, stem.length() - 2);
    else if (EndsWith(stem, L"ness") && stem.length() > 5)
        stem = stem.substr(0, stem.length() - 4);
    else if (EndsWith(stem, L"ment") && stem.length() > 5)
        stem = stem.substr(0, stem.length() - 4);
    else if (EndsWith(stem, L"tion") && stem.length() > 5)
        stem = stem.substr(0, stem.length() - 4);
    else if (EndsWith(stem, L"able") && stem.length() > 5)
        stem = stem.substr(0, stem.length() - 4);
    else if (EndsWith(stem, L"ible") && stem.length() > 5)
        stem = stem.substr(0, stem.length() - 4);
    else if (EndsWith(stem, L"less") && stem.length() > 5)
        stem = stem.substr(0, stem.length() - 4);
    else if (EndsWith(stem, L"ful") && stem.length() > 4)
        stem = stem.substr(0, stem.length() - 3);
    else if (EndsWith(stem, L"ous") && stem.length() > 4)
        stem = stem.substr(0, stem.length() - 3);
    else if (EndsWith(stem, L"ive") && stem.length() > 4)
        stem = stem.substr(0, stem.length() - 3);
    else if (EndsWith(stem, L"ing") && stem.length() > 4)
    {
        std::wstring base = stem.substr(0, stem.length() - 3);
        // Handle doubling: running -> run (drop doubled consonant)
        if (base.length() >= 2 && base[base.length() - 1] == base[base.length() - 2])
        {
            std::wstring vowels = L"aeiou";
            if (vowels.find(base[base.length() - 1]) == std::wstring::npos)
                base = base.substr(0, base.length() - 1);
        }
        if (MeasureWord(base) > 0)
            stem = base;
    }
    else if (EndsWith(stem, L"ed") && stem.length() > 4)
    {
        std::wstring base = stem.substr(0, stem.length() - 2);
        // Handle doubling: stopped -> stop
        if (base.length() >= 2 && base[base.length() - 1] == base[base.length() - 2])
        {
            std::wstring vowels = L"aeiou";
            if (vowels.find(base[base.length() - 1]) == std::wstring::npos)
                base = base.substr(0, base.length() - 1);
        }
        if (MeasureWord(base) > 0)
            stem = base;
    }
    else if (EndsWith(stem, L"er") && stem.length() > 4)
    {
        std::wstring base = stem.substr(0, stem.length() - 2);
        if (base.length() >= 2 && base[base.length() - 1] == base[base.length() - 2])
        {
            std::wstring vowels = L"aeiou";
            if (vowels.find(base[base.length() - 1]) == std::wstring::npos)
                base = base.substr(0, base.length() - 1);
        }
        if (MeasureWord(base) > 0)
            stem = base;
    }
    else if (EndsWith(stem, L"ly") && stem.length() > 4)
    {
        std::wstring base = stem.substr(0, stem.length() - 2);
        if (MeasureWord(base) > 0)
            stem = base;
    }
    else if (EndsWith(stem, L"al") && stem.length() > 4)
    {
        std::wstring base = stem.substr(0, stem.length() - 2);
        if (MeasureWord(base) > 0)
            stem = base;
    }
    else if (EndsWith(stem, L"es") && stem.length() > 4)
    {
        stem = stem.substr(0, stem.length() - 2);
    }
    else if (EndsWith(stem, L"s") && !EndsWith(stem, L"ss") && stem.length() > 3)
    {
        stem = stem.substr(0, stem.length() - 1);
    }

    return stem;
}

// Build the inverted index for searching
void BuildSearchIndex(const std::wstring& drive)
{
    invertedIndex.clear();
    stemmedIndex.clear();
    fullPathCache.clear();
    sortedTokens.clear();
    sortedStemTokens.clear();
    trigramIndex.clear();
    invertedIndex.reserve(fileMap.size());
    stemmedIndex.reserve(fileMap.size());

    int processed = 0;
    int total = (int)fileMap.size();

    for (auto& pair : fileMap)
    {
        ULONGLONG fileRef = pair.first;
        const FileEntry& entry = pair.second;

        if (entry.fileName.empty() || entry.fileName.length() > 255)
            continue;

        const std::wstring& lowerName = entry.lowerFileName;

        invertedIndex[lowerName].push_back(fileRef);

        // Also index substrings split by common separators (space, dot, dash, underscore)
        std::wstring token;
        for (size_t i = 0; i < lowerName.size(); i++)
        {
            wchar_t ch = lowerName[i];
            if (ch == L' ' || ch == L'.' || ch == L'-' || ch == L'_')
            {
                if (!token.empty())
                {
                    if (token != lowerName)
                        invertedIndex[token].push_back(fileRef);
                    token.clear();
                }
            }
            else
            {
                token += ch;
            }
        }
        if (!token.empty() && token != lowerName)
        {
            invertedIndex[token].push_back(fileRef);
        }

        // Also index under stemmed forms for morphological matching
        std::wstring stemmedName = StemWord(lowerName);
        if (stemmedName != lowerName)
            stemmedIndex[stemmedName].push_back(fileRef);

        // Stem each token from the separator split and index
        {
            std::wstring tok2;
            for (size_t si = 0; si < lowerName.size(); si++)
            {
                wchar_t sc = lowerName[si];
                if (sc == L' ' || sc == L'.' || sc == L'-' || sc == L'_')
                {
                    if (!tok2.empty())
                    {
                        std::wstring stemmedTok = StemWord(tok2);
                        if (stemmedTok != tok2 && stemmedTok != lowerName)
                            stemmedIndex[stemmedTok].push_back(fileRef);
                        tok2.clear();
                    }
                }
                else
                {
                    tok2 += sc;
                }
            }
            if (!tok2.empty())
            {
                std::wstring stemmedTok = StemWord(tok2);
                if (stemmedTok != tok2 && stemmedTok != lowerName)
                    stemmedIndex[stemmedTok].push_back(fileRef);
            }
        }

        processed++;
        if (processed % 100000 == 0)
        {
            std::wstring status = L"Building search index: " +
                std::to_wstring(processed) + L" / " + std::to_wstring(total) + L"...";
            UpdateProgress(92 + (int)((processed * 6ULL) / total), status);
        }
    }

    // Build sorted token vectors for O(log N) prefix range lookups
    sortedTokens.reserve(invertedIndex.size());
    for (auto& kv : invertedIndex)
        sortedTokens.push_back(kv.first);
    std::sort(sortedTokens.begin(), sortedTokens.end());

    sortedStemTokens.reserve(stemmedIndex.size());
    for (auto& kv : stemmedIndex)
        sortedStemTokens.push_back(kv.first);
    std::sort(sortedStemTokens.begin(), sortedStemTokens.end());

    // Build trigram index for O(1) substring candidate lookup.
    // For each filename, extract every 3-character window and map it to the file ref.
    trigramIndex.clear();
    trigramIndex.reserve(32768);
    for (auto& pair : fileMap)
    {
        const std::wstring& ln = pair.second.lowerFileName;
        if (ln.length() < 3)
            continue;
        ULONGLONG ref = pair.first;
        for (size_t i = 0; i + 2 < ln.length(); i++)
        {
            ULONGLONG key = ((ULONGLONG)(unsigned short)ln[i]) |
                            ((ULONGLONG)(unsigned short)ln[i + 1] << 16) |
                            ((ULONGLONG)(unsigned short)ln[i + 2] << 32);
            trigramIndex[key].push_back(ref);
        }
    }
    // Sort and deduplicate each posting list for fast intersection
    for (auto& kv : trigramIndex)
    {
        std::sort(kv.second.begin(), kv.second.end());
        kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end());
    }
}

// Add a single file to the inverted/stemmed/trigram indexes incrementally.
void AddFileToIndex(ULONGLONG fileRef, const std::wstring& lowerName)
{
    if (lowerName.empty() || lowerName.length() > 255)
        return;

    invertedIndex[lowerName].push_back(fileRef);

    std::wstring token;
    for (size_t i = 0; i < lowerName.size(); i++)
    {
        wchar_t ch = lowerName[i];
        if (ch == L' ' || ch == L'.' || ch == L'-' || ch == L'_')
        {
            if (!token.empty())
            {
                if (token != lowerName)
                    invertedIndex[token].push_back(fileRef);
                std::wstring stemmedTok = StemWord(token);
                if (stemmedTok != token && stemmedTok != lowerName)
                    stemmedIndex[stemmedTok].push_back(fileRef);
                token.clear();
            }
        }
        else
        {
            token += ch;
        }
    }
    if (!token.empty() && token != lowerName)
    {
        invertedIndex[token].push_back(fileRef);
        std::wstring stemmedTok = StemWord(token);
        if (stemmedTok != token && stemmedTok != lowerName)
            stemmedIndex[stemmedTok].push_back(fileRef);
    }

    std::wstring stemmedName = StemWord(lowerName);
    if (stemmedName != lowerName)
        stemmedIndex[stemmedName].push_back(fileRef);

    if (lowerName.length() >= 3)
    {
        for (size_t i = 0; i + 2 < lowerName.length(); i++)
        {
            ULONGLONG key = ((ULONGLONG)(unsigned short)lowerName[i]) |
                            ((ULONGLONG)(unsigned short)lowerName[i + 1] << 16) |
                            ((ULONGLONG)(unsigned short)lowerName[i + 2] << 32);
            auto& list = trigramIndex[key];
            list.push_back(fileRef);
        }
    }
}

// Remove a single file from the inverted/stemmed/trigram indexes.
void RemoveFileFromIndex(ULONGLONG fileRef, const std::wstring& lowerName)
{
    if (lowerName.empty() || lowerName.length() > 255)
        return;

    auto removeRef = [fileRef](std::vector<ULONGLONG>& v) {
        v.erase(std::remove(v.begin(), v.end(), fileRef), v.end());
    };

    auto tryRemove = [&](std::unordered_map<std::wstring, std::vector<ULONGLONG>>& idx,
                         const std::wstring& key) {
        auto it = idx.find(key);
        if (it != idx.end())
        {
            removeRef(it->second);
            if (it->second.empty())
                idx.erase(it);
        }
    };

    tryRemove(invertedIndex, lowerName);

    std::wstring token;
    for (size_t i = 0; i < lowerName.size(); i++)
    {
        wchar_t ch = lowerName[i];
        if (ch == L' ' || ch == L'.' || ch == L'-' || ch == L'_')
        {
            if (!token.empty())
            {
                if (token != lowerName)
                    tryRemove(invertedIndex, token);
                std::wstring stemmedTok = StemWord(token);
                if (stemmedTok != token && stemmedTok != lowerName)
                    tryRemove(stemmedIndex, stemmedTok);
                token.clear();
            }
        }
        else
        {
            token += ch;
        }
    }
    if (!token.empty() && token != lowerName)
    {
        tryRemove(invertedIndex, token);
        std::wstring stemmedTok = StemWord(token);
        if (stemmedTok != token && stemmedTok != lowerName)
            tryRemove(stemmedIndex, stemmedTok);
    }

    std::wstring stemmedName = StemWord(lowerName);
    if (stemmedName != lowerName)
        tryRemove(stemmedIndex, stemmedName);

    if (lowerName.length() >= 3)
    {
        for (size_t i = 0; i + 2 < lowerName.length(); i++)
        {
            ULONGLONG key = ((ULONGLONG)(unsigned short)lowerName[i]) |
                            ((ULONGLONG)(unsigned short)lowerName[i + 1] << 16) |
                            ((ULONGLONG)(unsigned short)lowerName[i + 2] << 32);
            auto tgIt = trigramIndex.find(key);
            if (tgIt != trigramIndex.end())
            {
                removeRef(tgIt->second);
                if (tgIt->second.empty())
                    trigramIndex.erase(tgIt);
            }
        }
    }
}

// Rebuild the sorted token vectors after incremental changes.
void RebuildSortedTokens()
{
    sortedTokens.clear();
    sortedTokens.reserve(invertedIndex.size());
    for (auto& kv : invertedIndex)
        sortedTokens.push_back(kv.first);
    std::sort(sortedTokens.begin(), sortedTokens.end());

    sortedStemTokens.clear();
    sortedStemTokens.reserve(stemmedIndex.size());
    for (auto& kv : stemmedIndex)
        sortedStemTokens.push_back(kv.first);
    std::sort(sortedStemTokens.begin(), sortedStemTokens.end());
}

// Process pending USN changes (called on the UI thread via WM_USN_UPDATED).
void ProcessPendingUsnChanges()
{
    std::vector<UsnChange> changes;
    {
        std::lock_guard<std::mutex> lock(usnMutex);
        changes.swap(pendingUsnChanges);
    }

    if (changes.empty())
        return;

    bool indexDirty = false;

    for (auto& ch : changes)
    {
        ULONGLONG fileRef = ch.fileRef;
        ULONGLONG parentRef = ch.parentRef;

        if (ch.reason & USN_REASON_FILE_DELETE)
        {
            auto it = fileMap.find(fileRef);
            if (it != fileMap.end())
            {
                RemoveFileFromIndex(fileRef, it->second.lowerFileName);
                ULONGLONG oldParent = it->second.parentReference & 0x0000FFFFFFFFFFFF;
                auto cIt = childrenMap.find(oldParent);
                if (cIt != childrenMap.end())
                {
                    auto& cv = cIt->second;
                    cv.erase(std::remove(cv.begin(), cv.end(), fileRef), cv.end());
                }
                fullPathCache.erase(fileRef);
                timestampCache.erase(fileRef);
                fileMap.erase(it);
                indexDirty = true;
            }
        }
        else if (ch.reason & (USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME))
        {
            auto existing = fileMap.find(fileRef);
            if (existing != fileMap.end())
            {
                RemoveFileFromIndex(fileRef, existing->second.lowerFileName);
                ULONGLONG oldParent = existing->second.parentReference & 0x0000FFFFFFFFFFFF;
                auto cIt = childrenMap.find(oldParent);
                if (cIt != childrenMap.end())
                {
                    auto& cv = cIt->second;
                    cv.erase(std::remove(cv.begin(), cv.end(), fileRef), cv.end());
                }
                fullPathCache.erase(fileRef);
            }

            FileEntry entry;
            entry.fileReference = fileRef;
            entry.parentReference = parentRef;
            entry.fileName = ch.fileName;
            entry.lowerFileName = ch.fileName;
            for (auto& lch : entry.lowerFileName)
                lch = towlower(lch);
            entry.isDirectory = ch.isDirectory;
            entry.hasChildren = false;
            entry.childrenLoaded = false;

            timestampCache[fileRef] = ch.timestamp;
            fileMap[fileRef] = std::move(entry);
            ULONGLONG parentRef48 = parentRef & 0x0000FFFFFFFFFFFF;
            childrenMap[parentRef48].push_back(fileRef);

            auto parentIt = fileMap.find(parentRef48);
            if (parentIt != fileMap.end() && parentIt->second.isDirectory)
                parentIt->second.hasChildren = true;

            AddFileToIndex(fileRef, fileMap[fileRef].lowerFileName);
            indexDirty = true;
        }
        else if (ch.reason & USN_REASON_BASIC_INFO_CHANGE)
        {
            timestampCache[fileRef] = ch.timestamp;
        }
    }

    if (indexDirty)
    {
        RebuildSortedTokens();
        fullPathCache.clear();
    }
}

// Background thread: poll USN journal for new changes.
static void UsnMonitorThreadFunc(std::wstring drive)
{
    std::wstring volumePath = L"\\\\.\\" + drive + L":";
    HANDLE hVolume = CreateFileW(volumePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE)
        return;

    const DWORD bufferSize = 64 * 1024;
    std::vector<BYTE> buffer(bufferSize);

    while (bUsnMonitorRunning.load())
    {
        READ_USN_JOURNAL_DATA_V0 readData = {0};
        readData.StartUsn = lastUsnProcessed;
        readData.ReasonMask = USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE |
                              USN_REASON_RENAME_NEW_NAME | USN_REASON_RENAME_OLD_NAME |
                              USN_REASON_BASIC_INFO_CHANGE;
        readData.ReturnOnlyOnClose = FALSE;
        readData.Timeout = 0;
        readData.BytesToWaitFor = 0;
        readData.UsnJournalID = usnJournalId;

        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(hVolume, FSCTL_READ_USN_JOURNAL,
            &readData, sizeof(readData),
            buffer.data(), bufferSize,
            &bytesReturned, NULL);

        if (!ok || bytesReturned <= sizeof(USN))
        {
            Sleep(USN_POLL_MS);
            continue;
        }

        USN nextUsn = *(USN*)buffer.data();
        DWORD offset = sizeof(USN);

        std::vector<UsnChange> batch;

        while (offset < bytesReturned)
        {
            USN_RECORD* record = (USN_RECORD*)(buffer.data() + offset);
            if (record->RecordLength == 0)
                break;
            if (offset + record->RecordLength > bytesReturned)
                break;

            DWORD fnLen = record->FileNameLength / sizeof(WCHAR);
            if (fnLen > 0 && fnLen <= 255 &&
                record->FileNameOffset + record->FileNameLength <= record->RecordLength)
            {
                WCHAR* fn = (WCHAR*)((BYTE*)record + record->FileNameOffset);
                UsnChange ch;
                ch.fileRef = record->FileReferenceNumber & 0x0000FFFFFFFFFFFF;
                ch.parentRef = record->ParentFileReferenceNumber;
                ch.fileName = std::wstring(fn, fnLen);
                ch.isDirectory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                ch.reason = record->Reason;
                ch.timestamp.QuadPart = record->TimeStamp.QuadPart;
                batch.push_back(std::move(ch));
            }

            offset += record->RecordLength;
        }

        if (!batch.empty())
        {
            {
                std::lock_guard<std::mutex> lock(usnMutex);
                pendingUsnChanges.insert(pendingUsnChanges.end(),
                    std::make_move_iterator(batch.begin()),
                    std::make_move_iterator(batch.end()));
            }
            if (hMainWnd)
                PostMessage(hMainWnd, WM_USN_UPDATED, 0, 0);
        }

        lastUsnProcessed = nextUsn;
        Sleep(USN_POLL_MS);
    }

    CloseHandle(hVolume);
}

void StartUsnMonitor(const std::wstring& drive)
{
    StopUsnMonitor();

    std::wstring volumePath = L"\\\\.\\" + drive + L":";
    HANDLE hVolume = CreateFileW(volumePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume != INVALID_HANDLE_VALUE)
    {
        DWORD bytesReturned = 0;
        USN_JOURNAL_DATA_V0 journalData = {0};
        if (DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0,
            &journalData, sizeof(journalData), &bytesReturned, NULL))
        {
            lastUsnProcessed = journalData.NextUsn;
            usnJournalId = journalData.UsnJournalID;
        }
        CloseHandle(hVolume);
    }

    bUsnMonitorRunning.store(true);
    usnMonitorThread = std::thread(UsnMonitorThreadFunc, drive);
}

void StopUsnMonitor()
{
    bUsnMonitorRunning.store(false);
    if (usnMonitorThread.joinable())
        usnMonitorThread.join();
    std::lock_guard<std::mutex> lock(usnMutex);
    pendingUsnChanges.clear();
}

// Send a request to WARP and read the full response as a UTF-8 string.
// Returns an empty string on any pipe communication failure.
static std::string WarpPipeQuery(const char* request)
{
    HANDLE hPipe = CreateFileW(
        L"\\\\.\\pipe\\WarpFileActivityAPI",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPipe == INVALID_HANDLE_VALUE)
        return "";

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &mode, NULL, NULL);

    DWORD bytesWritten = 0;
    if (!WriteFile(hPipe, request, (DWORD)strlen(request), &bytesWritten, NULL))
    {
        CloseHandle(hPipe);
        return "";
    }

    // Read response — loop to handle ERROR_MORE_DATA on message-mode pipes.
    // WARP caps responses at ~64 KB per message, but the pipe layer may still
    // split delivery across multiple ReadFile calls.
    std::string json;
    json.reserve(65536);
    std::vector<char> buf(65536);
    for (;;)
    {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hPipe, buf.data(), (DWORD)buf.size(), &bytesRead, NULL);
        if (bytesRead > 0)
            json.append(buf.data(), bytesRead);
        if (ok)
            break;  // complete message received
        if (GetLastError() == ERROR_MORE_DATA)
            continue;  // more chunks remain
        break;  // genuine error — use whatever we got
    }
    CloseHandle(hPipe);
    return json;
}

// Parse WARP activity objects from JSON and merge into warpRecencyByPath.
// Keeps the most recent timestamp per lowercased path.  Also collects OPEN
// events for a post-processing pass that detects batch folder-browsing noise.
struct WarpOpenEvent {
    std::wstring path;       // lowercased full path
    std::wstring parentDir;  // lowercased parent directory
    ULONGLONG timestamp;
};
static std::vector<WarpOpenEvent> pendingOpenEvents;

static void ParseWarpJson(const std::string& json)
{
    size_t pos = 0;
    while (pos < json.size())
    {
        // Find next "timestamp" key
        size_t tsPos = json.find("\"timestamp\"", pos);
        if (tsPos == std::string::npos) break;

        // Find the enclosing object start
        size_t objStart = json.rfind('{', tsPos);
        if (objStart == std::string::npos) { pos = tsPos + 11; continue; }

        // Find matching '}'
        int depth = 0;
        size_t objEnd = objStart;
        for (size_t i = objStart; i < json.size(); i++)
        {
            if (json[i] == '{') depth++;
            else if (json[i] == '}') { depth--; if (depth == 0) { objEnd = i + 1; break; } }
        }

        // If we couldn't find a matching '}', the JSON was likely truncated — stop.
        if (depth != 0)
            break;

        std::string obj = json.substr(objStart, objEnd - objStart);

        // Extract timestamp (integer value after "timestamp":)
        ULONGLONG ts = 0;
        {
            std::string tsKey = "\"timestamp\"";
            size_t kp = obj.find(tsKey);
            if (kp != std::string::npos)
            {
                kp += tsKey.size();
                while (kp < obj.size() && (obj[kp] == ' ' || obj[kp] == ':' || obj[kp] == '\t'))
                    kp++;
                while (kp < obj.size() && obj[kp] >= '0' && obj[kp] <= '9')
                {
                    ts = ts * 10 + (obj[kp] - '0');
                    kp++;
                }
            }
        }

        // Extract path and action
        std::string pathStr = JsonGetString(obj, "path");
        std::string action = JsonGetString(obj, "action");

        if (!pathStr.empty() && ts > 0)
        {
            bool isDirect = (action == "CREATE" || action == "MODIFY" ||
                            action == "DELETE" || action == "RENAME");

            std::wstring wpath = Utf8ToWide(pathStr);
            for (auto& ch : wpath)
                ch = towlower(ch);
            auto it = warpRecencyByPath.find(wpath);
            if (it == warpRecencyByPath.end())
            {
                warpRecencyByPath[wpath] = { ts, isDirect, (action == "OPEN") ? 1 : 0, 0 };
            }
            else
            {
                if (ts > it->second.timestamp)
                    it->second.timestamp = ts;
                if (isDirect)
                    it->second.hasDirectAction = true;
                if (action == "OPEN")
                    it->second.totalOpens++;
            }

            // Collect OPEN events for batch-detection post-processing
            if (action == "OPEN")
            {
                // Extract parent directory from the lowercased path
                size_t lastSep = wpath.rfind(L'\\');
                if (lastSep != std::wstring::npos && lastSep > 0)
                {
                    WarpOpenEvent evt;
                    evt.path = wpath;
                    evt.parentDir = wpath.substr(0, lastSep);
                    evt.timestamp = ts;
                    pendingOpenEvents.push_back(std::move(evt));
                }
            }
        }

        pos = objEnd;
    }
}

// Query the WARP file activity service via named pipe.
// Queries progressively longer time windows (24h ? 7d ? 15d ? 30d) and merges
// results.  Shorter windows are queried first so that recent data is always
// captured even if a longer window's response is truncated at the 64 KB pipe
// message limit.  Results are merged — the map keeps max(timestamp) per path,
// so overlapping windows are harmless.
static void QueryWarpActivity()
{
    warpRecencyByPath.clear();
    pendingOpenEvents.clear();
    bWarpAvailable = false;

    // Query from shortest to longest.  Each response is capped at ~64 KB by
    // WARP, so shorter windows are more likely to contain complete data.  We
    // merge all results to build the fullest picture possible.
    static const char* windows[] = {
        "{\"window\":\"24h\"}",
        "{\"window\":\"7d\"}",
        "{\"window\":\"15d\"}",
        "{\"window\":\"30d\"}",
    };

    for (const char* req : windows)
    {
        std::string json = WarpPipeQuery(req);
        if (json.empty())
        {
            // If even the first query fails, WARP isn't running.
            if (!bWarpAvailable)
                return;
            // Otherwise a longer window just failed — stop extending, keep
            // whatever we already collected from shorter windows.
            break;
        }

        bWarpAvailable = true;
        size_t prevSize = warpRecencyByPath.size();
        ParseWarpJson(json);

        // If a longer window didn't add any new paths beyond what we already
        // had, it means either (a) there's no older data, or (b) the response
        // was truncated and only contained duplicates we already have.  In
        // either case, stop — querying an even longer window won't help.
        if (warpRecencyByPath.size() == prevSize && prevSize > 0)
            break;
    }

    // --- Batch-OPEN detection ---
    // When a user browses a folder in Explorer, the OS generates OPEN events
    // for many sibling files within a tight time window (thumbnail generation,
    // icon extraction, search indexer reads, antivirus scans).  These are not
    // real user interactions and should not boost ranking.
    //
    // Strategy: group OPEN events by (parentDir, timestamp quantised to 3-second
    // buckets).  If a bucket has >= 5 sibling OPENs, mark all of them as batch
    // noise.  A genuine user open is typically 1–2 files, not a burst of many.

    // Count OPENs per (parentDir, time-bucket)
    const ULONGLONG BUCKET_SECS = 3;
    const size_t BATCH_THRESHOLD = 5;
    // key = parentDir + "|" + bucket_id_as_string
    std::unordered_map<std::wstring, size_t> bucketCounts;
    for (const auto& evt : pendingOpenEvents)
    {
        ULONGLONG bucket = evt.timestamp / BUCKET_SECS;
        wchar_t bucketStr[24];
        swprintf_s(bucketStr, 24, L"|%llu", bucket);
        std::wstring key = evt.parentDir + bucketStr;
        bucketCounts[key]++;
    }

    // Mark entries that belong to large buckets — increment batchOpens count
    // per file for each of its OPEN events that falls in a batch bucket.
    for (const auto& evt : pendingOpenEvents)
    {
        ULONGLONG bucket = evt.timestamp / BUCKET_SECS;
        wchar_t bucketStr[24];
        swprintf_s(bucketStr, 24, L"|%llu", bucket);
        std::wstring key = evt.parentDir + bucketStr;

        auto bc = bucketCounts.find(key);
        if (bc != bucketCounts.end() && bc->second >= BATCH_THRESHOLD)
        {
            auto wit = warpRecencyByPath.find(evt.path);
            if (wit != warpRecencyByPath.end())
                wit->second.batchOpens++;
        }
    }

    pendingOpenEvents.clear();
    pendingOpenEvents.shrink_to_fit();
}

// Return the WARP-only recency bonus for a file (0 if no WARP data).
// Returns up to +125 for files accessed today.  The large magnitude is
// intentional — it allows recent WARP activity to promote a weaker match
// type (e.g. substring) above a stronger match type (e.g. exact) that has
// no recent activity.  'now' is the current time as a 64-bit FILETIME value.
static int GetWarpRecencyBonus(ULONGLONG fileRef, ULONGLONG now)
{
    if (!bWarpAvailable || warpRecencyByPath.empty())
        return 0;

    // Look up the full path — build it on-demand if the cache was invalidated
    // (e.g. by ProcessPendingUsnChanges after a USN journal poll).
    auto pathIt = fullPathCache.find(fileRef);
    std::wstring filePath;
    if (pathIt != fullPathCache.end())
    {
        filePath = pathIt->second;
    }
    else if (!currentDriveLetter.empty())
    {
        filePath = BuildFullPath(fileRef, currentDriveLetter);
    }
    else
    {
        return 0;
    }

    std::wstring lowerPath = filePath;
    for (auto& ch : lowerPath)
        ch = towlower(ch);

    // Only match the file's own path — never inherit recency from a parent
    // folder.  A file inside a recently-browsed directory gets no boost unless
    // it was directly accessed.
    auto warpIt = warpRecencyByPath.find(lowerPath);
    if (warpIt == warpRecencyByPath.end())
        return 0;

    // Files with CREATE/MODIFY/DELETE/RENAME always qualify.
    // Files with OPEN-only qualify UNLESS *every* OPEN event was part of a
    // batch of sibling files opened simultaneously (folder-browsing noise).
    // A genuine user open typically also has at least one non-batch OPEN
    // (e.g., the user opened the file at a different time than the folder
    // was browsed, or the file was the only one opened in that time window).
    if (!warpIt->second.hasDirectAction)
    {
        // OPEN-only entry: check if it has any non-batch opens
        if (warpIt->second.totalOpens > 0 &&
            warpIt->second.batchOpens >= warpIt->second.totalOpens)
            return 0;  // all opens were batch noise — no genuine user interaction
    }

    const ULONGLONG EPOCH_DIFF = 116444736000000000ULL;
    ULONGLONG nowUnix = (now > EPOCH_DIFF) ? (now - EPOCH_DIFF) / 10000000ULL : 0;
    ULONGLONG warpTs = warpIt->second.timestamp;

    if (warpTs == 0 || nowUnix < warpTs)
        return 0;

    ULONGLONG ageSec = nowUnix - warpTs;
    const ULONGLONG SECS_PER_DAY = 86400ULL;
    ULONGLONG daysOld = ageSec / SECS_PER_DAY;

    if (daysOld == 0)   return 125;  // today
    if (daysOld <= 1)   return 110;  // yesterday
    if (daysOld <= 3)   return 100;  // last 3 days
    if (daysOld <= 7)   return 85;   // last week
    if (daysOld <= 14)  return 65;   // last 2 weeks
    if (daysOld <= 30)  return 40;   // last month
    return 15;                        // older WARP activity
}

// Compute a recency bonus based on WARP activity signals and USN timestamp.
// WARP provides real user interaction data (open, modify, create) and is the
// primary signal. Files with WARP activity get a strong bonus (up to 125)
// that intentionally outweighs match-type differences so recently-used files
// surface above stale exact matches.
// Files with only USN timestamps get a smaller bonus (up to 10).
// Files with no recency information at all get 0 (ranked lowest).
// 'now' is the current time as a 64-bit FILETIME value.
static int GetRecencyBonus(ULONGLONG fileRef, ULONGLONG now)
{
    // 1) Try WARP activity signal (delegates to GetWarpRecencyBonus)
    int wb = GetWarpRecencyBonus(fileRef, now);
    if (wb > 0)
        return wb;

    // 2) Fallback: USN timestamp (weaker signal — filesystem metadata only)
    auto tsIt = timestampCache.find(fileRef);
    if (tsIt == timestampCache.end())
        return 0;

    ULONGLONG fileTime = (ULONGLONG)tsIt->second.QuadPart;
    if (fileTime == 0 || fileTime > now)
        return 0;

    ULONGLONG diff = now - fileTime;
    const ULONGLONG TICKS_PER_DAY = 864000000000ULL; // 10^7 * 86400
    ULONGLONG daysOld = diff / TICKS_PER_DAY;

    if (daysOld <= 7)   return 10;
    if (daysOld <= 30)  return 7;
    if (daysOld <= 365) return 3;
    return 0;
}

// Check if keyword appears case-sensitively in the filename
static bool HasExactCaseMatch(const std::wstring& fileName, const std::wstring& keyword)
{
    size_t pos = 0;
    while (pos + keyword.length() <= fileName.length())
    {
        size_t found = fileName.find(keyword, pos);
        if (found == std::wstring::npos)
            break;
        return true;
    }
    return false;
}

// Compute the Levenshtein edit distance between two wide strings.
// Returns early if the distance exceeds maxDist, returning maxDist+1.
static int LevenshteinDistance(const std::wstring& a, const std::wstring& b, int maxDist)
{
    int m = (int)a.length();
    int n = (int)b.length();
    if (abs(m - n) > maxDist)
        return maxDist + 1;
    if (m == 0) return n;
    if (n == 0) return m;

    // Use a single-row DP with O(min(m,n)) space
    // Ensure 'b' is the shorter string for the row
    const std::wstring& s1 = (m >= n) ? a : b;
    const std::wstring& s2 = (m >= n) ? b : a;
    int len1 = (int)s1.length();
    int len2 = (int)s2.length();

    std::vector<int> prev(len2 + 1);
    std::vector<int> curr(len2 + 1);

    for (int j = 0; j <= len2; j++)
        prev[j] = j;

    for (int i = 1; i <= len1; i++)
    {
        curr[0] = i;
        int rowMin = curr[0];
        for (int j = 1; j <= len2; j++)
        {
            int cost = (towlower(s1[i - 1]) == towlower(s2[j - 1])) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            curr[j] = min(del, min(ins, sub));
            if (curr[j] < rowMin)
                rowMin = curr[j];
        }
        if (rowMin > maxDist)
            return maxDist + 1;
        std::swap(prev, curr);
    }
    return prev[len2];
}

// Check if a search keyword contains wildcard characters (* or ?)
static bool IsWildcardPattern(const std::wstring& s)
{
    for (wchar_t ch : s)
    {
        if (ch == L'*' || ch == L'?')
            return true;
    }
    return false;
}

// Case-insensitive glob/wildcard match. Supports * (any sequence) and ? (any single char).
// Uses an iterative algorithm with backtracking — no recursion, O(N*M) worst case.
static bool WildcardMatch(const wchar_t* str, const wchar_t* pattern)
{
    const wchar_t* sp = nullptr;  // backtrack position in str
    const wchar_t* pp = nullptr;  // backtrack position in pattern

    while (*str)
    {
        if (*pattern == L'*')
        {
            pattern++;
            // Collapse consecutive *'s
            while (*pattern == L'*') pattern++;
            if (!*pattern)
                return true; // trailing * matches everything
            pp = pattern;
            sp = str;
        }
        else if (*pattern == L'?' || towlower(*str) == towlower(*pattern))
        {
            str++;
            pattern++;
        }
        else if (pp)
        {
            // Backtrack: advance the match-start position by one
            sp++;
            str = sp;
            pattern = pp;
        }
        else
        {
            return false;
        }
    }
    // Consume trailing *'s in pattern
    while (*pattern == L'*') pattern++;
    return *pattern == L'\0';
}

// Extract contiguous literal (non-wildcard) spans from a pattern.
// Used to accelerate wildcard search via trigram pre-filtering.
static std::vector<std::wstring> ExtractLiteralSpans(const std::wstring& pattern)
{
    std::vector<std::wstring> spans;
    std::wstring current;
    for (wchar_t ch : pattern)
    {
        if (ch == L'*' || ch == L'?')
        {
            if (!current.empty())
            {
                spans.push_back(current);
                current.clear();
            }
        }
        else
        {
            current += ch;
        }
    }
    if (!current.empty())
        spans.push_back(current);
    return spans;
}

// Check if a file reference is a descendant of the given ancestor directory reference
static bool IsDescendantOf(ULONGLONG fileRef, ULONGLONG ancestorRef)
{
    ULONGLONG current = fileRef;
    int depth = 0;
    while (current != 5 && depth < 512)
    {
        ULONGLONG parentRef;
        auto it = fileMap.find(current);
        if (it == fileMap.end())
            return false;
        parentRef = it->second.parentReference & 0x0000FFFFFFFFFFFF;
        if (parentRef == ancestorRef)
            return true;
        if (parentRef == current)
            return false;
        current = parentRef;
        depth++;
    }
    return false;
}

// Helper: use binary search on a sorted token vector to find all tokens with a given prefix.
// Calls 'callback' for each matching token's posting list in the given index.
template<typename Func>
static void ForEachPrefixMatch(
    const std::vector<std::wstring>& sortedKeys,
    const std::unordered_map<std::wstring, std::vector<ULONGLONG>>& index,
    const std::wstring& prefix,
    Func callback)
{
    auto lo = std::lower_bound(sortedKeys.begin(), sortedKeys.end(), prefix);
    for (auto it = lo; it != sortedKeys.end(); ++it)
    {
        const std::wstring& token = *it;
        if (token.length() < prefix.length() ||
            token.compare(0, prefix.length(), prefix) != 0)
            break;
        auto idxIt = index.find(token);
        if (idxIt != index.end())
            callback(token, idxIt->second);
    }
}

// Perform a search and populate searchResults sorted by match quality.
// If scopeRef != 0, only include results that are descendants of that directory.
void PerformSearch(const std::wstring& keyword, ULONGLONG scopeRef)
{
    searchResults.clear();
    bWildcardSearch = false;

    if (keyword.empty() || fileMap.empty())
        return;

    std::wstring lowerKeyword = keyword;
    for (auto& ch : lowerKeyword)
        ch = towlower(ch);

    // --- Wildcard/glob search path ---
    if (IsWildcardPattern(lowerKeyword))
    {
        bWildcardSearch = true;

        // Get current time once for recency bonus
        FILETIME ftNow;
        GetSystemTimeAsFileTime(&ftNow);
        ULONGLONG now = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;

        // Try to accelerate via trigram pre-filtering:
        // Extract literal spans from the pattern, pick the longest one (>= 3 chars),
        // and use its trigrams to narrow the candidate set before running the glob.
        std::vector<std::wstring> literals = ExtractLiteralSpans(lowerKeyword);
        std::wstring longestLiteral;
        for (auto& span : literals)
        {
            if (span.length() > longestLiteral.length())
                longestLiteral = span;
        }

        const std::vector<ULONGLONG>* trigramCandidates = nullptr;
        if (longestLiteral.length() >= 3)
        {
            // Extract trigrams from the longest literal span
            std::vector<ULONGLONG> litTrigrams;
            litTrigrams.reserve(longestLiteral.length() - 2);
            for (size_t i = 0; i + 2 < longestLiteral.length(); i++)
            {
                ULONGLONG key = ((ULONGLONG)(unsigned short)longestLiteral[i]) |
                                ((ULONGLONG)(unsigned short)longestLiteral[i + 1] << 16) |
                                ((ULONGLONG)(unsigned short)longestLiteral[i + 2] << 32);
                litTrigrams.push_back(key);
            }
            // Pick the trigram with the smallest posting list
            const std::vector<ULONGLONG>* smallest = nullptr;
            for (ULONGLONG tg : litTrigrams)
            {
                auto tgIt = trigramIndex.find(tg);
                if (tgIt == trigramIndex.end())
                {
                    smallest = nullptr;
                    break;
                }
                if (!smallest || tgIt->second.size() < smallest->size())
                    smallest = &tgIt->second;
            }
            trigramCandidates = smallest;
        }

        searchResults.reserve(4096);

        auto processWildcardCandidate = [&](ULONGLONG ref, const FileEntry& entry) {
            if (scopeRef != 0 && ref != scopeRef && !IsDescendantOf(ref, scopeRef))
                return;
            if (!WildcardMatch(entry.lowerFileName.c_str(), lowerKeyword.c_str()))
                return;

            int base = 90; // wildcard match base score
            int wb = GetWarpRecencyBonus(ref, now);
            int score = base + ((wb > 0) ? wb : GetRecencyBonus(ref, now));

            SearchResult sr;
            sr.fileRef = ref;
            sr.fileName = entry.fileName;
            sr.isDirectory = entry.isDirectory;
            sr.matchScore = score;
            sr.baseScore = base;
            sr.warpBonus = wb;
            searchResults.push_back(sr);
        };

        if (trigramCandidates)
        {
            // Accelerated path: only check trigram candidates
            for (ULONGLONG ref : *trigramCandidates)
            {
                auto fIt = fileMap.find(ref);
                if (fIt == fileMap.end())
                    continue;
                processWildcardCandidate(ref, fIt->second);
            }
            // Also check files with names < 3 chars (missed by trigram index)
            for (auto& pair : fileMap)
            {
                if (pair.second.lowerFileName.length() < 3)
                    processWildcardCandidate(pair.first, pair.second);
            }
        }
        else
        {
            // Fallback: scan all files (no usable trigram acceleration)
            for (auto& pair : fileMap)
            {
                if (pair.second.fileName.empty() || pair.second.fileName.length() > 255)
                    continue;
                processWildcardCandidate(pair.first, pair.second);
            }
        }

        // Sort and cap
        const size_t MAX_RESULTS = 10000;
        auto cmpResults = [](const SearchResult& a, const SearchResult& b) {
            if (a.matchScore != b.matchScore)
                return a.matchScore > b.matchScore;
            return _wcsicmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
        };
        if (searchResults.size() > MAX_RESULTS)
        {
            std::partial_sort(searchResults.begin(),
                              searchResults.begin() + MAX_RESULTS,
                              searchResults.end(), cmpResults);
            searchResults.resize(MAX_RESULTS);
        }
        else
        {
            std::sort(searchResults.begin(), searchResults.end(), cmpResults);
        }
        for (auto& sr : searchResults)
            sr.fullPath = BuildFullPath(sr.fileRef, currentDriveLetter);
        return;
    }

    // --- Normal (non-wildcard) search path ---
    const size_t kwLen = lowerKeyword.length();

    // Use a flat hash map for collecting scores
    std::unordered_map<ULONGLONG, int> matchScores;
    matchScores.reserve(8192);

    // Helper lambda to insert/update a score
    auto addScore = [&](ULONGLONG ref, int score) {
        auto res = matchScores.emplace(ref, score);
        if (!res.second && res.first->second < score)
            res.first->second = score;
    };

    // 1) Exact token match (score 100) — O(1) hash lookup
    {
        auto exactIt = invertedIndex.find(lowerKeyword);
        if (exactIt != invertedIndex.end())
        {
            for (ULONGLONG ref : exactIt->second)
                addScore(ref, 100);
        }
    }

    // 2) Prefix matches on sorted token list — O(log N + matches)
    ForEachPrefixMatch(sortedTokens, invertedIndex, lowerKeyword,
        [&](const std::wstring& token, const std::vector<ULONGLONG>& refs) {
            if (token == lowerKeyword) return; // already handled as exact
            for (ULONGLONG ref : refs)
                addScore(ref, 75);
        });

    // 3) Substring match via trigram index — eliminates the O(N) fileMap scan.
    //    For keywords >= 3 chars: extract trigrams from the keyword, find the trigram
    //    with the smallest posting list, then verify actual substring on those candidates.
    //    For keywords < 3 chars: fall back to brute-force scan (rare, very fast anyway).
    if (kwLen >= 3)
    {
        // Extract all trigram keys from the keyword
        std::vector<ULONGLONG> kwTrigrams;
        kwTrigrams.reserve(kwLen - 2);
        for (size_t i = 0; i + 2 < kwLen; i++)
        {
            ULONGLONG key = ((ULONGLONG)(unsigned short)lowerKeyword[i]) |
                            ((ULONGLONG)(unsigned short)lowerKeyword[i + 1] << 16) |
                            ((ULONGLONG)(unsigned short)lowerKeyword[i + 2] << 32);
            kwTrigrams.push_back(key);
        }

        // Pick the trigram with the smallest posting list for maximum selectivity
        const std::vector<ULONGLONG>* smallest = nullptr;
        for (ULONGLONG tg : kwTrigrams)
        {
            auto tgIt = trigramIndex.find(tg);
            if (tgIt == trigramIndex.end())
            {
                smallest = nullptr;
                break; // trigram not in any filename — zero candidates
            }
            if (!smallest || tgIt->second.size() < smallest->size())
                smallest = &tgIt->second;
        }

        if (smallest)
        {
            for (ULONGLONG ref : *smallest)
            {
                if (matchScores.count(ref))
                    continue;
                auto fIt = fileMap.find(ref);
                if (fIt == fileMap.end())
                    continue;
                const std::wstring& ln = fIt->second.lowerFileName;
                if (ln.length() >= kwLen && ln.find(lowerKeyword) != std::wstring::npos)
                {
                    matchScores[ref] = 40;
                }
            }
        }
    }
    else if (kwLen > 0 && kwLen < 3)
    {
        // Short keyword fallback — brute-force scan (only for 1-2 char queries)
        for (auto& pair : fileMap)
        {
            if (matchScores.count(pair.first))
                continue;
            const std::wstring& ln = pair.second.lowerFileName;
            if (ln.length() >= kwLen && ln.find(lowerKeyword) != std::wstring::npos)
            {
                matchScores[pair.first] = 40;
            }
        }
    }

    // 4) Stemmed matching — O(1) hash + O(log N) prefix range
    std::wstring stemmedKeyword = StemWord(lowerKeyword);
    if (!stemmedKeyword.empty())
    {
        auto stemExact = stemmedIndex.find(stemmedKeyword);
        if (stemExact != stemmedIndex.end())
        {
            for (ULONGLONG ref : stemExact->second)
            {
                if (!matchScores.count(ref))
                    matchScores[ref] = 30;
            }
        }
        if (stemmedKeyword != lowerKeyword)
        {
            auto stemInOrig = invertedIndex.find(stemmedKeyword);
            if (stemInOrig != invertedIndex.end())
            {
                for (ULONGLONG ref : stemInOrig->second)
                {
                    if (!matchScores.count(ref))
                        matchScores[ref] = 30;
                }
            }
            // Prefix match on sorted stemmed token list
            ForEachPrefixMatch(sortedStemTokens, stemmedIndex, stemmedKeyword,
                [&](const std::wstring& stemToken, const std::vector<ULONGLONG>& refs) {
                    if (stemToken == stemmedKeyword) return;
                    for (ULONGLONG ref : refs)
                    {
                        if (!matchScores.count(ref))
                            matchScores[ref] = 25;
                    }
                });
        }
    }

    // 5) Fuzzy matching via Levenshtein distance (max distance 2).
    //    Only check tokens whose length is within ±2 of the keyword length,
    //    since edit distance ? 2 can change length by at most 2.
    //    Skip very short keywords (< 4 chars) to avoid noisy false positives.
    if (kwLen >= 4)
    {
        int maxDist = (kwLen >= 7) ? 2 : 1; // stricter for shorter words
        int minLen = (int)kwLen - maxDist;
        int maxLen = (int)kwLen + maxDist;
        if (minLen < 1) minLen = 1;

        // Use sortedTokens for efficient iteration; binary-search is not helpful
        // for fuzzy matching, but length filtering eliminates most candidates.
        for (const std::wstring& token : sortedTokens)
        {
            int tLen = (int)token.length();
            if (tLen < minLen || tLen > maxLen)
                continue;
            // Skip tokens already matched by earlier stages
            // (check a representative ref from the posting list)
            auto idxIt = invertedIndex.find(token);
            if (idxIt == invertedIndex.end())
                continue;

            int dist = LevenshteinDistance(lowerKeyword, token, maxDist);
            if (dist > 0 && dist <= maxDist)
            {
                // Score: 18 for distance 1, 15 for distance 2
                int fuzzyScore = (dist == 1) ? 18 : 15;
                for (ULONGLONG ref : idxIt->second)
                {
                    if (!matchScores.count(ref))
                        matchScores[ref] = fuzzyScore;
                }
            }
        }
    }

    // Get current time once for all recency bonus calculations
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULONGLONG now = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;

    // Build results with bonus scoring — defer fullPath to after truncation
    searchResults.reserve(matchScores.size());
    for (auto& pair : matchScores)
    {
        ULONGLONG ref = pair.first;
        auto it = fileMap.find(ref);
        if (it == fileMap.end())
            continue;

        if (scopeRef != 0 && ref != scopeRef && !IsDescendantOf(ref, scopeRef))
            continue;

        int base = pair.second;
        int score = base;

        if (HasExactCaseMatch(it->second.fileName, keyword))
            score += 15;

        int wb = GetWarpRecencyBonus(ref, now);
        score += (wb > 0) ? wb : GetRecencyBonus(ref, now);

        SearchResult sr;
        sr.fileRef = ref;
        sr.fileName = it->second.fileName;
        sr.baseScore = base;
        sr.warpBonus = wb;
        // fullPath deferred — will be built only for the final top-N results
        sr.isDirectory = it->second.isDirectory;
        sr.matchScore = score;
        searchResults.push_back(sr);
    }

    // Cap results: use partial_sort to only fully sort the top N
    const size_t MAX_RESULTS = 10000;
    auto cmpResults = [](const SearchResult& a, const SearchResult& b) {
        if (a.matchScore != b.matchScore)
            return a.matchScore > b.matchScore;
        return _wcsicmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
    };

    if (searchResults.size() > MAX_RESULTS)
    {
        std::partial_sort(searchResults.begin(),
                          searchResults.begin() + MAX_RESULTS,
                          searchResults.end(), cmpResults);
        searchResults.resize(MAX_RESULTS);
    }
    else
    {
        std::sort(searchResults.begin(), searchResults.end(), cmpResults);
    }

    // Now build full paths only for the final result set
    for (auto& sr : searchResults)
    {
        sr.fullPath = BuildFullPath(sr.fileRef, currentDriveLetter);
    }
}

// Format a file's last-write time into a date/time string.
// First tries the USN timestamp cache; falls back to querying the filesystem.
static std::wstring FormatTimestamp(ULONGLONG fileRef, const std::wstring& fullPath)
{
    FILETIME ft = {0, 0};

    auto tsIt = timestampCache.find(fileRef);
    if (tsIt != timestampCache.end() && tsIt->second.QuadPart != 0)
    {
        ft.dwLowDateTime = (DWORD)(tsIt->second.QuadPart & 0xFFFFFFFF);
        ft.dwHighDateTime = (DWORD)(tsIt->second.QuadPart >> 32);
    }
    else if (!fullPath.empty())
    {
        WIN32_FILE_ATTRIBUTE_DATA fad = {0};
        if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fad))
        {
            ft = fad.ftLastWriteTime;
            LARGE_INTEGER li;
            li.LowPart = ft.dwLowDateTime;
            li.HighPart = ft.dwHighDateTime;
            timestampCache[fileRef] = li;
        }
    }

    if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0)
        return L"";

    FILETIME ftLocal;
    FileTimeToLocalFileTime(&ft, &ftLocal);

    SYSTEMTIME st;
    FileTimeToSystemTime(&ftLocal, &st);

    wchar_t buf[64];
    swprintf_s(buf, 64, L"%04d-%02d-%02d %02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return std::wstring(buf);
}

// Get the system icon index for a file, using extension-based caching.
int GetFileIconIndex(const std::wstring& fileName, bool isDirectory)
{
    if (isDirectory)
    {
        if (iFolderIcon >= 0)
            return iFolderIcon;
        SHFILEINFOW sfi = {0};
        SHGetFileInfoW(L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        iFolderIcon = sfi.iIcon;
        return iFolderIcon;
    }

    // Extract extension (lowercased) for cache key
    std::wstring ext;
    size_t dot = fileName.rfind(L'.');
    if (dot != std::wstring::npos)
    {
        ext = fileName.substr(dot);
        for (auto& ch : ext)
            ch = towlower(ch);
    }
    else
    {
        ext = L"._no_ext_";
    }

    auto cacheIt = iconCache.find(ext);
    if (cacheIt != iconCache.end())
        return cacheIt->second;

    // Ask the shell for this extension's icon using a fake filename
    std::wstring fakeName = L"file" + ext;
    SHFILEINFOW sfi = {0};
    SHGetFileInfoW(fakeName.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
        SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
    iconCache[ext] = sfi.iIcon;
    return sfi.iIcon;
}

// Execute the current search (called from Find button and debounce timer)
void TriggerSearch(HWND hWnd)
{
    wchar_t searchText[512] = {0};
    GetWindowTextW(hSearchEdit, searchText, 512);
    std::wstring keyword(searchText);
    if (keyword.empty() || fileMap.empty())
        return;

    currentSearchKeyword = keyword;
    for (auto& ch : currentSearchKeyword)
        ch = towlower(ch);
    bWindowsSearch = false;

    // Determine search scope from selected tree node
    ULONGLONG scopeRef = 0;
    currentSearchScope.clear();
    HTREEITEM hSelItem = TreeView_GetSelection(hTreeView);
    if (hSelItem)
    {
        auto refIt = treeItemToRef.find(hSelItem);
        if (refIt != treeItemToRef.end())
        {
            ULONGLONG selRef = refIt->second;
            if (selRef != 5)
            {
                auto feIt = fileMap.find(selRef);
                if (feIt != fileMap.end() && feIt->second.isDirectory)
                {
                    scopeRef = selRef;
                    currentSearchScope = BuildFullPath(selRef, currentDriveLetter);
                }
            }
        }
    }

    LARGE_INTEGER searchFreq, searchT1, searchT2;
    QueryPerformanceFrequency(&searchFreq);
    QueryPerformanceCounter(&searchT1);

    PerformSearch(keyword, scopeRef);
    PopulateSearchResults();

    QueryPerformanceCounter(&searchT2);
    double searchElapsedMS = (searchT2.QuadPart - searchT1.QuadPart) * 1000.0 / searchFreq.QuadPart;

    // Switch to Search Results tab
    TabCtrl_SetCurSel(hTabControl, 1);
    ShowTab(1);

    std::wstring status = L"Found " + std::to_wstring(searchResults.size()) + L" results";
    if (searchResults.size() >= 10000)
        status += L" (showing first 10,000)";
    if (!currentSearchScope.empty())
        status += L"  |  Scope: " + currentSearchScope;
    std::wstring stemmed = StemWord(currentSearchKeyword);
    if (stemmed != currentSearchKeyword)
        status += L"  |  Stem: \"" + stemmed + L"\"";
    wchar_t timeBuf[64];
    swprintf_s(timeBuf, 64, L"  |  %.1f ms", searchElapsedMS);
    status += timeBuf;
    if (bWarpAvailable)
        status += L"  |  WARP \u2714";
    SetWindowTextW(hStatusText, status.c_str());
}

// --- Cloud storage provider configuration (OAuth2 + REST API) ---

// Cloud providers require OAuth2 client credentials to authenticate.
// Users enter their Client ID (and Client Secret for Google/iCloud) directly
// in the Cloud Configuration dialog. Credentials are persisted locally in
// %APPDATA%\FindMyFile\cloud_tokens.ini.
//
// To obtain credentials, register an application at:
//   OneDrive  — Azure App Registration  (https://portal.azure.com)
//   Google    — Google Cloud Console     (https://console.cloud.google.com)
//   iCloud    — Apple Developer account  (https://developer.apple.com)
// Each registration must add http://localhost:5483/callback as a redirect URI.

static const int  OAUTH_PORT = 5483;
static const wchar_t* OAUTH_REDIRECT = L"http://localhost:5483/callback";

// Return the path to the cloud tokens file.
static std::wstring GetCloudConfigPath()
{
    wchar_t appData[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData)))
    {
        std::wstring dir = std::wstring(appData) + L"\\FindMyFile";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\cloud_tokens.ini";
    }
    return L"";
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (len <= 0) return L"";
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

static std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string u(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &u[0], len, NULL, NULL);
    return u;
}

static std::wstring UrlEncode(const std::wstring& s)
{
    std::string utf8 = WideToUtf8(s);
    std::wstring out;
    for (unsigned char c : utf8)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (wchar_t)c;
        else
        {
            wchar_t buf[8];
            swprintf_s(buf, 8, L"%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// Minimal JSON string extractor: find "key":"value" or "key": "value"
static std::string JsonGetString(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    // Skip whitespace and colon
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
        pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++; // skip opening quote
    std::string val;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            pos++;
            if (json[pos] == '"') val += '"';
            else if (json[pos] == '\\') val += '\\';
            else if (json[pos] == '/') val += '/';
            else if (json[pos] == 'n') val += '\n';
            else if (json[pos] == 't') val += '\t';
            else val += json[pos];
        }
        else
        {
            val += json[pos];
        }
        pos++;
    }
    return val;
}

// WinHTTP GET request returning body as UTF-8 string.
static std::string WinHttpGet(const std::wstring& host, const std::wstring& path, const std::wstring& authHeader)
{
    std::string result;
    HINTERNET hSession = WinHttpOpen(L"FindMyFile/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

    if (!authHeader.empty())
        WinHttpAddRequestHeaders(hRequest, authHeader.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL))
    {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize > 0)
            {
                std::vector<char> buf(dwSize);
                DWORD dwRead = 0;
                WinHttpReadData(hRequest, buf.data(), dwSize, &dwRead);
                result.append(buf.data(), dwRead);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// WinHTTP POST request returning body as UTF-8 string.
static std::string WinHttpPost(const std::wstring& host, const std::wstring& path,
                               const std::string& body, const std::wstring& contentType)
{
    std::string result;
    HINTERNET hSession = WinHttpOpen(L"FindMyFile/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

    std::wstring headers = L"Content-Type: " + contentType;

    if (WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1,
            (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
        WinHttpReceiveResponse(hRequest, NULL))
    {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize > 0)
            {
                std::vector<char> buf(dwSize);
                DWORD dwRead = 0;
                WinHttpReadData(hRequest, buf.data(), dwSize, &dwRead);
                result.append(buf.data(), dwRead);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// OAuth2 loopback redirect: start a temporary HTTP server on localhost,
// wait for the browser callback carrying the authorization code.
// The listen socket handle is stored in oauthListenSock so the UI thread
// can close it to cancel a pending accept() without waiting for the timeout.
static std::string ListenForOAuthCallback()
{
    // Create a simple TCP listener on OAUTH_PORT
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) { WSACleanup(); return ""; }

    // Publish the socket so the UI thread can close it for cancellation
    oauthListenSock = listenSock;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(OAUTH_PORT);

    // Allow port reuse so rapid re-logins work
    int yes = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        oauthListenSock = INVALID_SOCKET;
        closesocket(listenSock);
        WSACleanup();
        return "";
    }
    listen(listenSock, 1);

    // Set a 120-second timeout so we don't block forever
    DWORD timeout = 120000;
    setsockopt(listenSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    SOCKET clientSock = accept(listenSock, NULL, NULL);
    std::string code;
    if (clientSock != INVALID_SOCKET)
    {
        char buf[4096] = {0};
        int n = recv(clientSock, buf, sizeof(buf) - 1, 0);
        if (n > 0)
        {
            std::string req(buf, n);
            // Extract ?code=... from GET /callback?code=XXX&...
            size_t codePos = req.find("code=");
            if (codePos != std::string::npos)
            {
                codePos += 5;
                size_t end = req.find_first_of("& \r\n", codePos);
                if (end == std::string::npos) end = req.size();
                code = req.substr(codePos, end - codePos);
            }

            // Send a user-friendly response page
            const char* response =
                "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                "<html><body style='font-family:Segoe UI;text-align:center;padding:40px'>"
                "<h2>Signed in successfully!</h2>"
                "<p>You can close this tab and return to Find My File!</p>"
                "</body></html>";
            send(clientSock, response, (int)strlen(response), 0);
        }
        closesocket(clientSock);
    }

    // Only close if not already closed by cancellation
    if (oauthListenSock != INVALID_SOCKET)
    {
        closesocket(listenSock);
        oauthListenSock = INVALID_SOCKET;
    }
    WSACleanup();
    return code;
}

// Cancel a pending OAuth listen by closing the socket from the UI thread.
static void CancelOAuthListen()
{
    SOCKET s = oauthListenSock;
    if (s != INVALID_SOCKET)
    {
        oauthListenSock = INVALID_SOCKET;
        closesocket(s);  // unblocks accept() on the background thread
    }
}

// Perform OAuth2 login for a given provider. Opens the browser, waits for callback.
// This function blocks until the OAuth flow completes — call from a background thread.
bool CloudOAuth2Login(HWND hParent, CloudProviderId id)
{
    CloudProvider& cp = cloudProviders[id];

    if (cp.clientId.empty())
        return false;

    std::wstring authUrl;
    std::wstring redirectUri = OAUTH_REDIRECT;

    if (id == CP_ONEDRIVE)
    {
        authUrl = L"https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize"
                  L"?client_id=" + cp.clientId +
                  L"&response_type=code"
                  L"&redirect_uri=" + UrlEncode(redirectUri) +
                  L"&scope=" + UrlEncode(L"Files.Read Files.Read.All offline_access") +
                  L"&response_mode=query";
    }
    else if (id == CP_GOOGLEDRIVE)
    {
        if (cp.clientSecret.empty())
            return false;
        authUrl = L"https://accounts.google.com/o/oauth2/v2/auth"
                  L"?client_id=" + cp.clientId +
                  L"&response_type=code"
                  L"&redirect_uri=" + UrlEncode(redirectUri) +
                  L"&scope=" + UrlEncode(L"https://www.googleapis.com/auth/drive.readonly") +
                  L"&access_type=offline&prompt=consent";
    }
    else if (id == CP_ICLOUD)
    {
        authUrl = L"https://appleid.apple.com/auth/authorize"
                  L"?client_id=" + UrlEncode(cp.clientId) +
                  L"&response_type=code"
                  L"&redirect_uri=" + UrlEncode(redirectUri) +
                  L"&scope=" + UrlEncode(L"name email") +
                  L"&response_mode=query";
    }
    else
    {
        return false;
    }

    // Open browser
    ShellExecuteW(hParent, L"open", authUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);

    // Wait for the callback (blocks until redirect, timeout, or cancellation)
    std::string code = ListenForOAuthCallback();
    if (code.empty())
        return false;

    // Exchange the authorization code for access + refresh tokens
    std::string tokenBody;
    std::wstring tokenHost;
    std::wstring tokenPath;

    if (id == CP_ONEDRIVE)
    {
        tokenHost = L"login.microsoftonline.com";
        tokenPath = L"/consumers/oauth2/v2.0/token";
        tokenBody = "client_id=" + WideToUtf8(cp.clientId) +
                    "&grant_type=authorization_code"
                    "&code=" + code +
                    "&redirect_uri=" + WideToUtf8(redirectUri) +
                    "&scope=" + WideToUtf8(L"Files.Read Files.Read.All offline_access");
    }
    else if (id == CP_GOOGLEDRIVE)
    {
        tokenHost = L"oauth2.googleapis.com";
        tokenPath = L"/token";
        tokenBody = "client_id=" + WideToUtf8(cp.clientId) +
                    "&client_secret=" + WideToUtf8(cp.clientSecret) +
                    "&grant_type=authorization_code"
                    "&code=" + code +
                    "&redirect_uri=" + WideToUtf8(redirectUri);
    }
    else if (id == CP_ICLOUD)
    {
        tokenHost = L"appleid.apple.com";
        tokenPath = L"/auth/token";
        tokenBody = "client_id=" + WideToUtf8(cp.clientId) +
                    "&grant_type=authorization_code"
                    "&code=" + code +
                    "&redirect_uri=" + WideToUtf8(redirectUri);
    }

    std::string tokenResp = WinHttpPost(tokenHost, tokenPath, tokenBody,
                                        L"application/x-www-form-urlencoded");
    if (tokenResp.empty())
        return false;

    std::string at = JsonGetString(tokenResp, "access_token");
    std::string rt = JsonGetString(tokenResp, "refresh_token");
    if (at.empty())
        return false;

    cp.accessToken = Utf8ToWide(at);
    cp.refreshToken = Utf8ToWide(rt);
    cp.loggedIn = true;

    SaveCloudConfig();
    return true;
}

// Background thread function for OAuth login. Runs CloudOAuth2Login and
// posts WM_OAUTH_COMPLETE to the dialog when done.
static void OAuthLoginThreadFunc(HWND hParent, CloudProviderId id)
{
    bool ok = CloudOAuth2Login(hParent, id);
    oauthSuccess = ok;
    bOAuthInProgress.store(false);
    // Notify the dialog (if still alive) that the flow is complete
    if (hOAuthDlg && IsWindow(hOAuthDlg))
        PostMessage(hOAuthDlg, WM_OAUTH_COMPLETE, (WPARAM)ok, 0);
}

// Start an OAuth login on a background thread so the UI remains responsive.
static void StartOAuthLoginAsync(HWND hDlg, CloudProviderId id)
{
    // Join any previous thread
    if (oauthThread.joinable())
        oauthThread.join();

    hOAuthDlg = hDlg;
    oauthActiveProvider = id;
    bOAuthInProgress.store(true);
    oauthSuccess = false;
    oauthThread = std::thread(OAuthLoginThreadFunc, hDlg, id);
}

// Cancel any in-progress OAuth login and clean up.
static void CancelOAuthLogin()
{
    if (!bOAuthInProgress.load())
        return;
    CancelOAuthListen();  // unblocks the accept() call
    if (oauthThread.joinable())
        oauthThread.join();
    bOAuthInProgress.store(false);
}

// Try to refresh an expired access token using the stored refresh token.
static bool CloudRefreshToken(CloudProviderId id)
{
    CloudProvider& cp = cloudProviders[id];
    if (cp.refreshToken.empty()) return false;

    std::string body;
    std::wstring host, path;

    if (id == CP_ONEDRIVE)
    {
        host = L"login.microsoftonline.com";
        path = L"/consumers/oauth2/v2.0/token";
        body = "client_id=" + WideToUtf8(cp.clientId) +
               "&grant_type=refresh_token"
               "&refresh_token=" + WideToUtf8(cp.refreshToken) +
               "&scope=" + WideToUtf8(L"Files.Read Files.Read.All offline_access");
    }
    else if (id == CP_GOOGLEDRIVE)
    {
        host = L"oauth2.googleapis.com";
        path = L"/token";
        body = "client_id=" + WideToUtf8(cp.clientId) +
               "&client_secret=" + WideToUtf8(cp.clientSecret) +
               "&grant_type=refresh_token"
               "&refresh_token=" + WideToUtf8(cp.refreshToken);
    }
    else
    {
        return false;
    }

    std::string resp = WinHttpPost(host, path, body, L"application/x-www-form-urlencoded");
    if (resp.empty()) return false;

    std::string at = JsonGetString(resp, "access_token");
    if (at.empty()) return false;

    cp.accessToken = Utf8ToWide(at);
    // Some providers rotate refresh tokens
    std::string newRt = JsonGetString(resp, "refresh_token");
    if (!newRt.empty())
        cp.refreshToken = Utf8ToWide(newRt);

    SaveCloudConfig();
    return true;
}

void CloudLogout(CloudProviderId id)
{
    CloudProvider& cp = cloudProviders[id];
    cp.accessToken.clear();
    cp.refreshToken.clear();
    cp.loggedIn = false;
    SaveCloudConfig();
    UpdateCloudButtonLabel();
}

// Parse OneDrive search results from Microsoft Graph JSON response.
static std::vector<SearchResult> JsonParseOneDriveResults(const std::string& json)
{
    std::vector<SearchResult> results;
    // Look for each "hitsContainers"[0]."hits"[] entry, or simpler:
    // The /search/query endpoint returns nested JSON. We use the simpler
    // /me/drive/root/search(q='...') endpoint which returns {"value":[...]}.
    // Each item has "name", "webUrl", "parentReference":{"path":"..."}, "folder" or "file".
    size_t pos = 0;
    while (pos < json.size())
    {
        size_t itemStart = json.find("\"name\"", pos);
        if (itemStart == std::string::npos) break;

        // Find the enclosing object boundaries (search backward for '{')
        size_t objStart = json.rfind('{', itemStart);
        if (objStart == std::string::npos) { pos = itemStart + 6; continue; }

        // Find matching '}' — simple brace counting
        int depth = 0;
        size_t objEnd = objStart;
        for (size_t i = objStart; i < json.size(); i++)
        {
            if (json[i] == '{') depth++;
            else if (json[i] == '}') { depth--; if (depth == 0) { objEnd = i + 1; break; } }
        }
        std::string obj = json.substr(objStart, objEnd - objStart);

        std::string name = JsonGetString(obj, "name");
        std::string webUrl = JsonGetString(obj, "webUrl");
        bool isFolder = (obj.find("\"folder\"") != std::string::npos &&
                         obj.find("\"folder\":null") == std::string::npos);

        if (!name.empty())
        {
            // Build a cloud path from parentReference.path + name
            std::string parentPath = JsonGetString(obj, "path");
            std::wstring displayPath;
            if (!parentPath.empty())
            {
                // parentReference.path is like "/drive/root:/Documents"
                size_t colonPos = parentPath.find(':');
                if (colonPos != std::string::npos)
                    displayPath = L"OneDrive:" + Utf8ToWide(parentPath.substr(colonPos + 1)) + L"/" + Utf8ToWide(name);
                else
                    displayPath = L"OneDrive:/" + Utf8ToWide(name);
            }
            else
            {
                displayPath = L"OneDrive:/" + Utf8ToWide(name);
            }
            // Use webUrl as the "full path" so double-click opens in browser
            if (!webUrl.empty())
                displayPath = Utf8ToWide(webUrl);

            SearchResult sr;
            sr.fileRef = 0;
            sr.fileName = Utf8ToWide(name);
            sr.fullPath = displayPath;
            sr.cloudSource = L"OneDrive";
            sr.isDirectory = isFolder;
            sr.matchScore = 51; // cloud filename match tier
            results.push_back(std::move(sr));
        }

        pos = objEnd;
    }
    return results;
}

// Parse Google Drive search results from the files.list JSON response.
static std::vector<SearchResult> JsonParseGoogleDriveResults(const std::string& json)
{
    std::vector<SearchResult> results;
    size_t pos = 0;
    while (pos < json.size())
    {
        size_t namePos = json.find("\"name\"", pos);
        if (namePos == std::string::npos) break;

        size_t objStart = json.rfind('{', namePos);
        if (objStart == std::string::npos) { pos = namePos + 6; continue; }

        int depth = 0;
        size_t objEnd = objStart;
        for (size_t i = objStart; i < json.size(); i++)
        {
            if (json[i] == '{') depth++;
            else if (json[i] == '}') { depth--; if (depth == 0) { objEnd = i + 1; break; } }
        }
        std::string obj = json.substr(objStart, objEnd - objStart);

        std::string name = JsonGetString(obj, "name");
        std::string mimeType = JsonGetString(obj, "mimeType");
        std::string webLink = JsonGetString(obj, "webViewLink");
        bool isFolder = (mimeType == "application/vnd.google-apps.folder");

        if (!name.empty())
        {
            SearchResult sr;
            sr.fileRef = 0;
            sr.fileName = Utf8ToWide(name);
            sr.fullPath = webLink.empty() ? (L"GoogleDrive:/" + sr.fileName) : Utf8ToWide(webLink);
            sr.cloudSource = L"Google Drive";
            sr.isDirectory = isFolder;
            sr.matchScore = 51;
            results.push_back(std::move(sr));
        }

        pos = objEnd;
    }
    return results;
}

// Search a single cloud provider via its REST API.
std::vector<SearchResult> CloudSearchFiles(CloudProviderId id, const std::wstring& keyword)
{
    std::vector<SearchResult> results;
    CloudProvider& cp = cloudProviders[id];
    if (!cp.loggedIn || cp.accessToken.empty())
        return results;

    std::wstring authHeader = L"Authorization: Bearer " + cp.accessToken;

    if (id == CP_ONEDRIVE)
    {
        // Microsoft Graph: GET /me/drive/root/search(q='{keyword}')
        std::wstring path = L"/v1.0/me/drive/root/search(q='" + UrlEncode(keyword) + L"')?$top=200";
        std::string resp = WinHttpGet(L"graph.microsoft.com", path, authHeader);

        // Check for 401 and try refresh
        if (resp.find("\"code\"") != std::string::npos &&
            (resp.find("InvalidAuthenticationToken") != std::string::npos ||
             resp.find("ExpiredToken") != std::string::npos ||
             resp.find("401") != std::string::npos))
        {
            if (CloudRefreshToken(id))
            {
                authHeader = L"Authorization: Bearer " + cp.accessToken;
                resp = WinHttpGet(L"graph.microsoft.com", path, authHeader);
            }
            else
            {
                cp.loggedIn = false;
                return results;
            }
        }

        results = JsonParseOneDriveResults(resp);
    }
    else if (id == CP_GOOGLEDRIVE)
    {
        // Google Drive API: GET /drive/v3/files?q=name contains '{keyword}'
        std::wstring query = L"name contains '" + keyword + L"'";
        std::wstring path = L"/drive/v3/files?q=" + UrlEncode(query) +
                            L"&fields=files(name,mimeType,webViewLink)&pageSize=200";
        std::string resp = WinHttpGet(L"www.googleapis.com", path, authHeader);

        // Check for 401 and try refresh
        if (resp.find("\"code\": 401") != std::string::npos ||
            resp.find("\"status\": \"UNAUTHENTICATED\"") != std::string::npos)
        {
            if (CloudRefreshToken(id))
            {
                authHeader = L"Authorization: Bearer " + cp.accessToken;
                resp = WinHttpGet(L"www.googleapis.com", path, authHeader);
            }
            else
            {
                cp.loggedIn = false;
                return results;
            }
        }

        results = JsonParseGoogleDriveResults(resp);
    }
    // iCloud: Apple does not provide a public REST search API for iCloud Drive
    // on Windows. Results would require the Apple CloudKit JS/REST API with
    // a registered container, which is beyond typical desktop app scope.
    // Users will see "Not available" in the dialog for iCloud.

    return results;
}

// Load tokens and credentials from persistent config file.
void LoadCloudConfig()
{
    std::wstring path = GetCloudConfigPath();
    if (path.empty()) return;

    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8");
    if (!f) return;

    wchar_t line[4096];
    while (fgetws(line, 4096, f))
    {
        std::wstring s(line);
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' '))
            s.pop_back();
        if (s.empty()) continue;

        // Format: id|clientId|clientSecret|accessToken|refreshToken
        size_t sep1 = s.find(L'|');
        if (sep1 == std::wstring::npos) continue;
        size_t sep2 = s.find(L'|', sep1 + 1);
        if (sep2 == std::wstring::npos) continue;
        size_t sep3 = s.find(L'|', sep2 + 1);
        if (sep3 == std::wstring::npos) continue;
        size_t sep4 = s.find(L'|', sep3 + 1);
        if (sep4 == std::wstring::npos) continue;

        int id = _wtoi(s.substr(0, sep1).c_str());
        if (id < 0 || id >= CP_COUNT) continue;

        cloudProviders[id].clientId = s.substr(sep1 + 1, sep2 - sep1 - 1);
        cloudProviders[id].clientSecret = s.substr(sep2 + 1, sep3 - sep2 - 1);
        cloudProviders[id].accessToken = s.substr(sep3 + 1, sep4 - sep3 - 1);
        cloudProviders[id].refreshToken = s.substr(sep4 + 1);
        cloudProviders[id].loggedIn = !cloudProviders[id].accessToken.empty();
    }
    fclose(f);
}

// Save tokens and credentials to persistent config file.
void SaveCloudConfig()
{
    std::wstring path = GetCloudConfigPath();
    if (path.empty()) return;

    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8");
    if (!f) return;

    for (int i = 0; i < CP_COUNT; i++)
    {
        auto& cp = cloudProviders[i];
        if (!cp.clientId.empty() || !cp.accessToken.empty() || !cp.refreshToken.empty())
        {
            fwprintf(f, L"%d|%s|%s|%s|%s\n", i,
                     cp.clientId.c_str(),
                     cp.clientSecret.c_str(),
                     cp.accessToken.c_str(),
                     cp.refreshToken.c_str());
        }
    }
    fclose(f);
}

void UpdateCloudButtonLabel()
{
    if (!hCloudBtn) return;
    int online = 0;
    for (int i = 0; i < CP_COUNT; i++)
        if (cloudProviders[i].loggedIn) online++;

    std::wstring label;
    if (online == 0)
        label = L"\u2601 Cloud";
    else
        label = L"\u2601 Cloud (" + std::to_wstring(online) + L"/" +
                std::to_wstring(CP_COUNT) + L")";
    SetWindowTextW(hCloudBtn, label.c_str());
}

// Update the status labels and button enabled state in the cloud config dialog.
// Sign In is only enabled when the required credentials are filled in.
static void RefreshCloudDialogStatus(HWND hDlg)
{
    // Helper: check if a provider has required credentials filled in
    auto hasCredentials = [](CloudProviderId id) -> bool {
        CloudProvider& cp = cloudProviders[id];
        if (cp.clientId.empty()) return false;
        // Google Drive and iCloud require a client secret too
        if ((id == CP_GOOGLEDRIVE || id == CP_ICLOUD) && cp.clientSecret.empty())
            return false;
        return true;
    };

    auto setProviderUI = [&](CloudProviderId id, int statusId, int loginId, int logoutId) {
        CloudProvider& cp = cloudProviders[id];
        bool hasCreds = hasCredentials(id);
        if (cp.loggedIn)
        {
            SetDlgItemTextW(hDlg, statusId, L"\u2714 Signed in");
            EnableWindow(GetDlgItem(hDlg, loginId), FALSE);
            EnableWindow(GetDlgItem(hDlg, logoutId), TRUE);
        }
        else
        {
            if (hasCreds)
                SetDlgItemTextW(hDlg, statusId, L"Ready to sign in");
            else
                SetDlgItemTextW(hDlg, statusId, L"Enter credentials above");
            EnableWindow(GetDlgItem(hDlg, loginId), hasCreds ? TRUE : FALSE);
            EnableWindow(GetDlgItem(hDlg, logoutId), FALSE);
        }
    };
    setProviderUI(CP_ONEDRIVE,    IDC_OD_STATUS, IDC_OD_LOGIN, IDC_OD_LOGOUT);
    setProviderUI(CP_GOOGLEDRIVE, IDC_GD_STATUS, IDC_GD_LOGIN, IDC_GD_LOGOUT);
    setProviderUI(CP_ICLOUD,      IDC_IC_STATUS, IDC_IC_LOGIN, IDC_IC_LOGOUT);

    // iCloud note: no public API — override status
    if (!cloudProviders[CP_ICLOUD].loggedIn)
    {
        if (hasCredentials(CP_ICLOUD))
            SetDlgItemTextW(hDlg, IDC_IC_STATUS, L"Ready (limited — no public search API)");
        else
            SetDlgItemTextW(hDlg, IDC_IC_STATUS, L"Enter credentials (limited — no public search API)");
    }

    int online = 0;
    for (int i = 0; i < CP_COUNT; i++)
        if (cloudProviders[i].loggedIn) online++;
    wchar_t buf[128];
    swprintf_s(buf, 128, L"%d of %d providers connected", online, CP_COUNT);
    SetDlgItemTextW(hDlg, IDC_CLOUD_STATUS, buf);
}

// Helper: read text from a dialog edit control into a wstring.
static std::wstring GetDlgItemWString(HWND hDlg, int id)
{
    wchar_t buf[1024] = {0};
    GetDlgItemTextW(hDlg, id, buf, 1024);
    std::wstring s(buf);
    // Trim whitespace
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    return s;
}

// Read all credential edit boxes into the cloudProviders array.
static void ReadCredentialsFromDialog(HWND hDlg)
{
    cloudProviders[CP_ONEDRIVE].clientId = GetDlgItemWString(hDlg, IDC_OD_CLIENTID);
    cloudProviders[CP_GOOGLEDRIVE].clientId = GetDlgItemWString(hDlg, IDC_GD_CLIENTID);
    cloudProviders[CP_GOOGLEDRIVE].clientSecret = GetDlgItemWString(hDlg, IDC_GD_SECRET);
    cloudProviders[CP_ICLOUD].clientId = GetDlgItemWString(hDlg, IDC_IC_CLIENTID);
    cloudProviders[CP_ICLOUD].clientSecret = GetDlgItemWString(hDlg, IDC_IC_SECRET);
}

// Helper: disable all login/logout buttons and credential fields while OAuth is in progress.
static void SetCloudDialogBusy(HWND hDlg, bool busy)
{
    EnableWindow(GetDlgItem(hDlg, IDC_OD_LOGIN),    !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_OD_LOGOUT),   !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_GD_LOGIN),    !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_GD_LOGOUT),   !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_IC_LOGIN),    !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_IC_LOGOUT),   !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_OD_CLIENTID), !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_GD_CLIENTID), !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_GD_SECRET),   !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_IC_CLIENTID), !busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_IC_SECRET),   !busy ? TRUE : FALSE);
    // Repurpose Close button as Cancel while busy
    SetDlgItemTextW(hDlg, IDOK, busy ? L"Cancel" : L"Close");
}

// Dialog procedure for the Cloud Configuration dialog.
INT_PTR CALLBACK CloudConfigDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        // Populate edit boxes with stored credentials
        SetDlgItemTextW(hDlg, IDC_OD_CLIENTID, cloudProviders[CP_ONEDRIVE].clientId.c_str());
        SetDlgItemTextW(hDlg, IDC_GD_CLIENTID, cloudProviders[CP_GOOGLEDRIVE].clientId.c_str());
        SetDlgItemTextW(hDlg, IDC_GD_SECRET,   cloudProviders[CP_GOOGLEDRIVE].clientSecret.c_str());
        SetDlgItemTextW(hDlg, IDC_IC_CLIENTID, cloudProviders[CP_ICLOUD].clientId.c_str());
        SetDlgItemTextW(hDlg, IDC_IC_SECRET,   cloudProviders[CP_ICLOUD].clientSecret.c_str());
        RefreshCloudDialogStatus(hDlg);
        return (INT_PTR)TRUE;

    case WM_OAUTH_COMPLETE:
        {
            // Background OAuth thread finished — update UI
            bool ok = (wParam != 0);
            CloudProvider& cp = cloudProviders[oauthActiveProvider];
            if (ok)
            {
                std::wstring msg = cp.name + L": signed in successfully.";
                SetDlgItemTextW(hDlg, IDC_CLOUD_STATUS, msg.c_str());
            }
            else
            {
                std::wstring msg = cp.name + L": sign-in failed or cancelled.";
                SetDlgItemTextW(hDlg, IDC_CLOUD_STATUS, msg.c_str());
            }
            if (oauthThread.joinable())
                oauthThread.join();
            SetCloudDialogBusy(hDlg, false);
            UpdateCloudButtonLabel();
            RefreshCloudDialogStatus(hDlg);
            return (INT_PTR)TRUE;
        }

    case WM_COMMAND:
        {
            int wmId2 = LOWORD(wParam);
            int notif = HIWORD(wParam);

            // When any credential edit box changes, re-read all credentials
            // and refresh button enabled states
            if (notif == EN_CHANGE)
            {
                if (wmId2 == IDC_OD_CLIENTID || wmId2 == IDC_GD_CLIENTID ||
                    wmId2 == IDC_GD_SECRET || wmId2 == IDC_IC_CLIENTID ||
                    wmId2 == IDC_IC_SECRET)
                {
                    ReadCredentialsFromDialog(hDlg);
                    RefreshCloudDialogStatus(hDlg);
                    return (INT_PTR)TRUE;
                }
            }

            // Block login actions while an OAuth flow is in progress
            if (bOAuthInProgress.load())
            {
                if (wmId2 == IDOK || wmId2 == IDCANCEL)
                {
                    // "Cancel" button pressed — abort the OAuth flow
                    CancelOAuthLogin();
                    SetDlgItemTextW(hDlg, IDC_CLOUD_STATUS, L"Sign-in cancelled.");
                    SetCloudDialogBusy(hDlg, false);
                    RefreshCloudDialogStatus(hDlg);
                    return (INT_PTR)TRUE;
                }
                return (INT_PTR)TRUE;
            }

            if (wmId2 == IDC_OD_LOGIN)
            {
                ReadCredentialsFromDialog(hDlg);
                SaveCloudConfig();
                SetDlgItemTextW(hDlg, IDC_CLOUD_STATUS, L"Signing in to OneDrive... (waiting for browser)");
                SetCloudDialogBusy(hDlg, true);
                StartOAuthLoginAsync(hDlg, CP_ONEDRIVE);
                return (INT_PTR)TRUE;
            }
            if (wmId2 == IDC_OD_LOGOUT)
            {
                CloudLogout(CP_ONEDRIVE);
                RefreshCloudDialogStatus(hDlg);
                return (INT_PTR)TRUE;
            }
            if (wmId2 == IDC_GD_LOGIN)
            {
                ReadCredentialsFromDialog(hDlg);
                SaveCloudConfig();
                SetDlgItemTextW(hDlg, IDC_CLOUD_STATUS, L"Signing in to Google Drive... (waiting for browser)");
                SetCloudDialogBusy(hDlg, true);
                StartOAuthLoginAsync(hDlg, CP_GOOGLEDRIVE);
                return (INT_PTR)TRUE;
            }
            if (wmId2 == IDC_GD_LOGOUT)
            {
                CloudLogout(CP_GOOGLEDRIVE);
                RefreshCloudDialogStatus(hDlg);
                return (INT_PTR)TRUE;
            }
            if (wmId2 == IDC_IC_LOGIN)
            {
                ReadCredentialsFromDialog(hDlg);
                SaveCloudConfig();
                SetDlgItemTextW(hDlg, IDC_CLOUD_STATUS, L"Signing in to iCloud... (waiting for browser)");
                SetCloudDialogBusy(hDlg, true);
                StartOAuthLoginAsync(hDlg, CP_ICLOUD);
                return (INT_PTR)TRUE;
            }
            if (wmId2 == IDC_IC_LOGOUT)
            {
                CloudLogout(CP_ICLOUD);
                RefreshCloudDialogStatus(hDlg);
                return (INT_PTR)TRUE;
            }
            if (wmId2 == IDOK || wmId2 == IDCANCEL)
            {
                // Always save credentials on close
                ReadCredentialsFromDialog(hDlg);
                SaveCloudConfig();
                UpdateCloudButtonLabel();
                hOAuthDlg = NULL;
                EndDialog(hDlg, wmId2);
                return (INT_PTR)TRUE;
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}

// Perform a search using the Windows Search Indexer via OLE DB.
void TriggerWindowsSearch(HWND hWnd)
{
    wchar_t searchText[512] = {0};
    GetWindowTextW(hSearchEdit, searchText, 512);
    std::wstring keyword(searchText);
    if (keyword.empty())
        return;

    currentSearchKeyword = keyword;
    for (auto& ch : currentSearchKeyword)
        ch = towlower(ch);

    LARGE_INTEGER searchFreq, searchT1, searchT2;
    QueryPerformanceFrequency(&searchFreq);
    QueryPerformanceCounter(&searchT1);

    searchResults.clear();
    bWildcardSearch = false;
    bWindowsSearch = true;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool comInited = SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE;

    if (!comInited)
    {
        SetWindowTextW(hStatusText, L"Failed to initialize COM.");
        return;
    }

    // Use the Search OLE DB provider via IDataInitialize
    IDataInitialize* pDataInit = NULL;
    hr = CoCreateInstance(
        CLSID_MSDAINITIALIZE, NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IDataInitialize), (void**)&pDataInit);

    if (FAILED(hr) || !pDataInit)
    {
        SetWindowTextW(hStatusText, L"Failed to create OLE DB data initializer.");
        if (comInited) CoUninitialize();
        return;
    }

    IDBInitialize* pDBInitialize = NULL;
    hr = pDataInit->GetDataSource(
        NULL, CLSCTX_INPROC_SERVER,
        L"provider=Search.CollatorDSO.1;EXTENDED PROPERTIES='Application=Windows';",
        __uuidof(IDBInitialize), (IUnknown**)&pDBInitialize);
    pDataInit->Release();

    if (FAILED(hr) || !pDBInitialize)
    {
        SetWindowTextW(hStatusText, L"Failed to connect to Windows Search provider.");
        if (comInited) CoUninitialize();
        return;
    }

    hr = pDBInitialize->Initialize();
    if (FAILED(hr))
    {
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, L"Failed to initialize Windows Search provider.");
        if (comInited) CoUninitialize();
        return;
    }

    IDBCreateSession* pCreateSession = NULL;
    hr = pDBInitialize->QueryInterface(__uuidof(IDBCreateSession), (void**)&pCreateSession);
    if (FAILED(hr) || !pCreateSession)
    {
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, L"Failed to create search session.");
        if (comInited) CoUninitialize();
        return;
    }

    IDBCreateCommand* pCreateCommand = NULL;
    hr = pCreateSession->CreateSession(NULL, __uuidof(IDBCreateCommand), (IUnknown**)&pCreateCommand);
    pCreateSession->Release();
    if (FAILED(hr) || !pCreateCommand)
    {
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, L"Failed to create command object.");
        if (comInited) CoUninitialize();
        return;
    }

    ICommandText* pCommandText = NULL;
    hr = pCreateCommand->CreateCommand(NULL, __uuidof(ICommandText), (IUnknown**)&pCommandText);
    pCreateCommand->Release();
    if (FAILED(hr) || !pCommandText)
    {
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, L"Failed to create command text.");
        if (comInited) CoUninitialize();
        return;
    }

    // Determine search scope from selected tree node
    std::wstring scopePath;
    currentSearchScope.clear();
    HTREEITEM hSelItem = TreeView_GetSelection(hTreeView);
    if (hSelItem)
    {
        auto refIt = treeItemToRef.find(hSelItem);
        if (refIt != treeItemToRef.end())
        {
            ULONGLONG selRef = refIt->second;
            if (selRef != 5)
            {
                auto feIt = fileMap.find(selRef);
                if (feIt != fileMap.end() && feIt->second.isDirectory)
                {
                    scopePath = BuildFullPath(selRef, currentDriveLetter);
                    currentSearchScope = scopePath;
                }
            }
        }
    }

    // Build the SQL query — search by System.FileName LIKE '%keyword%'
    // Escape single quotes in the keyword
    std::wstring escapedKw;
    for (wchar_t c : keyword)
    {
        if (c == L'\'')
            escapedKw += L"''";
        else
            escapedKw += c;
    }

    // Build scope — use the selected folder if available, otherwise fall back to drive
    std::wstring scopeClause;
    if (!scopePath.empty())
    {
        // Ensure trailing backslash for folder scope
        std::wstring scopeDir = scopePath;
        if (!scopeDir.empty() && scopeDir.back() != L'\\')
            scopeDir += L'\\';
        scopeClause = L" AND SCOPE='file:///" + scopeDir + L"'";
    }
    else if (!currentDriveLetter.empty())
    {
        scopeClause = L" AND SCOPE='file:///" + currentDriveLetter + L":\\'";
    }

    // Build escaped keyword for CONTAINS predicate (double any single quotes)
    // CONTAINS uses a different escaping: wrap the term in double quotes for exact phrase
    std::wstring containsKw;
    for (wchar_t c : keyword)
    {
        if (c == L'\'')
            containsKw += L"''";
        else if (c == L'"')
            containsKw += L"\"\"";
        else
            containsKw += c;
    }

    // Search both filename (LIKE) and file contents (CONTAINS).
    // CONTAINS searches the full-text index which covers file content for
    // supported file types (documents, source code, text files, etc.).
    std::wstring sql = L"SELECT System.ItemName, System.ItemPathDisplay, "
                       L"System.ItemType, System.Search.AutoSummary "
                       L"FROM SystemIndex WHERE "
                       L"(System.FileName LIKE '%" + escapedKw + L"%'"
                       L" OR CONTAINS('\"" + containsKw + L"\"'))" + scopeClause;

    hr = pCommandText->SetCommandText(DBGUID_DEFAULT, sql.c_str());
    if (FAILED(hr))
    {
        pCommandText->Release();
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, L"Failed to set query text.");
        if (comInited) CoUninitialize();
        return;
    }

    IRowset* pRowset = NULL;
    DBROWCOUNT rowsAffected = 0;
    hr = pCommandText->Execute(NULL, __uuidof(IRowset), NULL, &rowsAffected, (IUnknown**)&pRowset);
    pCommandText->Release();
    if (FAILED(hr) || !pRowset)
    {
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, L"Windows Search query failed.");
        if (comInited) CoUninitialize();
        return;
    }

    // Get column accessor
    IAccessor* pAccessor = NULL;
    hr = pRowset->QueryInterface(__uuidof(IAccessor), (void**)&pAccessor);
    if (FAILED(hr) || !pAccessor)
    {
        pRowset->Release();
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, L"Failed to get row accessor.");
        if (comInited) CoUninitialize();
        return;
    }

    // We have 4 columns: ItemName, ItemPathDisplay, ItemType, AutoSummary
    // The Windows Search OLE DB provider only reliably supports DBTYPE_VARIANT
    // bindings. We bind each column as a VARIANT and extract strings afterwards.
    // VARIANT requires 8-byte alignment; use #pragma pack or careful layout.
    struct RowData {
        DBSTATUS dwNameStatus;
        DBLENGTH dwNameLen;
        DBSTATUS dwPathStatus;
        DBLENGTH dwPathLen;
        DBSTATUS dwTypeStatus;
        DBLENGTH dwTypeLen;
        DBSTATUS dwSummaryStatus;
        DBLENGTH dwSummaryLen;
        // Place all VARIANTs together at the end for natural alignment
        VARIANT vName;
        VARIANT vPath;
        VARIANT vType;
        VARIANT vSummary;
    };

    DBBINDING rgBindings[4];
    memset(rgBindings, 0, sizeof(rgBindings));

    // Column 1: System.ItemName (ordinal 1)
    rgBindings[0].iOrdinal  = 1;
    rgBindings[0].obStatus  = offsetof(RowData, dwNameStatus);
    rgBindings[0].obLength  = offsetof(RowData, dwNameLen);
    rgBindings[0].obValue   = offsetof(RowData, vName);
    rgBindings[0].dwPart    = DBPART_VALUE | DBPART_STATUS | DBPART_LENGTH;
    rgBindings[0].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    rgBindings[0].cbMaxLen  = sizeof(VARIANT);
    rgBindings[0].wType     = DBTYPE_VARIANT;

    // Column 2: System.ItemPathDisplay (ordinal 2)
    rgBindings[1].iOrdinal  = 2;
    rgBindings[1].obStatus  = offsetof(RowData, dwPathStatus);
    rgBindings[1].obLength  = offsetof(RowData, dwPathLen);
    rgBindings[1].obValue   = offsetof(RowData, vPath);
    rgBindings[1].dwPart    = DBPART_VALUE | DBPART_STATUS | DBPART_LENGTH;
    rgBindings[1].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    rgBindings[1].cbMaxLen  = sizeof(VARIANT);
    rgBindings[1].wType     = DBTYPE_VARIANT;

    // Column 3: System.ItemType (ordinal 3)
    rgBindings[2].iOrdinal  = 3;
    rgBindings[2].obStatus  = offsetof(RowData, dwTypeStatus);
    rgBindings[2].obLength  = offsetof(RowData, dwTypeLen);
    rgBindings[2].obValue   = offsetof(RowData, vType);
    rgBindings[2].dwPart    = DBPART_VALUE | DBPART_STATUS | DBPART_LENGTH;
    rgBindings[2].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    rgBindings[2].cbMaxLen  = sizeof(VARIANT);
    rgBindings[2].wType     = DBTYPE_VARIANT;

    // Column 4: System.Search.AutoSummary (ordinal 4)
    rgBindings[3].iOrdinal  = 4;
    rgBindings[3].obStatus  = offsetof(RowData, dwSummaryStatus);
    rgBindings[3].obLength  = offsetof(RowData, dwSummaryLen);
    rgBindings[3].obValue   = offsetof(RowData, vSummary);
    rgBindings[3].dwPart    = DBPART_VALUE | DBPART_STATUS | DBPART_LENGTH;
    rgBindings[3].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    rgBindings[3].cbMaxLen  = sizeof(VARIANT);
    rgBindings[3].wType     = DBTYPE_VARIANT;

    HACCESSOR hAcc = 0;
    DBBINDSTATUS rgStatus[4];
    hr = pAccessor->CreateAccessor(DBACCESSOR_ROWDATA, 4, rgBindings, sizeof(RowData),
                                   &hAcc, rgStatus);
    if (FAILED(hr))
    {
        // Build a diagnostic message with the HRESULT and per-binding status
        wchar_t diagBuf[256];
        swprintf_s(diagBuf, 256,
            L"Failed to create accessor. hr=0x%08X  status[0]=%u status[1]=%u status[2]=%u status[3]=%u",
            (unsigned)hr, (unsigned)rgStatus[0], (unsigned)rgStatus[1], (unsigned)rgStatus[2], (unsigned)rgStatus[3]);
        pAccessor->Release();
        pRowset->Release();
        pDBInitialize->Release();
        SetWindowTextW(hStatusText, diagBuf);
        if (comInited) CoUninitialize();
        return;
    }

    // Fetch rows
    const size_t MAX_RESULTS = 10000;
    searchResults.reserve(4096);

    // Helper: extract a wstring from a VARIANT (handles VT_BSTR and VT_LPWSTR)
    auto VariantToWString = [](VARIANT& v) -> std::wstring {
        if (v.vt == VT_BSTR && v.bstrVal)
            return std::wstring(v.bstrVal, SysStringLen(v.bstrVal));
        if (v.vt == VT_LPWSTR && v.bstrVal)
            return std::wstring((LPCWSTR)v.bstrVal);
        // Try coercing to BSTR
        VARIANT vStr;
        VariantInit(&vStr);
        if (SUCCEEDED(VariantChangeType(&vStr, &v, 0, VT_BSTR)) && vStr.bstrVal)
        {
            std::wstring result(vStr.bstrVal, SysStringLen(vStr.bstrVal));
            VariantClear(&vStr);
            return result;
        }
        VariantClear(&vStr);
        return std::wstring();
    };

    // Build a lowercased keyword for filename-match detection
    std::wstring lowerKw = keyword;
    for (auto& ch2 : lowerKw)
        ch2 = towlower(ch2);

    HROW hRows[50];
    HROW* pRows = hRows;
    DBCOUNTITEM cRowsObtained = 0;

    while (searchResults.size() < MAX_RESULTS)
    {
        hr = pRowset->GetNextRows(DB_NULL_HCHAPTER, 0, 50, &cRowsObtained, &pRows);
        if (FAILED(hr) || cRowsObtained == 0)
            break;

        for (DBCOUNTITEM i = 0; i < cRowsObtained; i++)
        {
            RowData rd;
            memset(&rd, 0, sizeof(rd));
            VariantInit(&rd.vName);
            VariantInit(&rd.vPath);
            VariantInit(&rd.vType);
            VariantInit(&rd.vSummary);

            hr = pRowset->GetData(hRows[i], hAcc, &rd);
            if (FAILED(hr))
            {
                VariantClear(&rd.vName);
                VariantClear(&rd.vPath);
                VariantClear(&rd.vType);
                VariantClear(&rd.vSummary);
                continue;
            }

            if (rd.dwNameStatus != DBSTATUS_S_OK || rd.dwPathStatus != DBSTATUS_S_OK)
            {
                VariantClear(&rd.vName);
                VariantClear(&rd.vPath);
                VariantClear(&rd.vType);
                VariantClear(&rd.vSummary);
                continue;
            }

            SearchResult sr;
            sr.fileRef = 0;
            sr.fileName = VariantToWString(rd.vName);
            sr.fullPath = VariantToWString(rd.vPath);

            if (sr.fileName.empty() || sr.fullPath.empty())
            {
                VariantClear(&rd.vName);
                VariantClear(&rd.vPath);
                VariantClear(&rd.vType);
                VariantClear(&rd.vSummary);
                continue;
            }

            // Determine if it's a directory based on ItemType
            if (rd.dwTypeStatus == DBSTATUS_S_OK)
            {
                std::wstring itemType = VariantToWString(rd.vType);
                sr.isDirectory = (itemType == L"Directory" || itemType == L"Folder" ||
                                  itemType == L"File folder" || itemType.empty());
            }
            else
            {
                // Fallback: check file attributes
                DWORD attrs = GetFileAttributesW(sr.fullPath.c_str());
                sr.isDirectory = (attrs != INVALID_FILE_ATTRIBUTES &&
                                  (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
            }

            // Extract content snippet from AutoSummary
            if (rd.dwSummaryStatus == DBSTATUS_S_OK)
            {
                sr.contentSnippet = VariantToWString(rd.vSummary);
                // Collapse whitespace for display: replace newlines/tabs with spaces
                for (auto& sc : sr.contentSnippet)
                {
                    if (sc == L'\r' || sc == L'\n' || sc == L'\t')
                        sc = L' ';
                }
                // Trim to a reasonable display length
                if (sr.contentSnippet.length() > 300)
                    sr.contentSnippet = sr.contentSnippet.substr(0, 297) + L"...";
            }

            VariantClear(&rd.vName);
            VariantClear(&rd.vPath);
            VariantClear(&rd.vType);
            VariantClear(&rd.vSummary);

            // Determine whether the keyword matched the filename or only content
            std::wstring lowerName = sr.fileName;
            for (auto& ch2 : lowerName)
                ch2 = towlower(ch2);
            bool filenameMatch = (lowerName.find(lowerKw) != std::wstring::npos);

            // For content-only matches, ensure the snippet actually contains the keyword.
            // AutoSummary is a generic file summary that often omits the search term.
            // Try to read a snippet directly from the file content.
            if (!filenameMatch)
            {
                std::wstring lowerSnip = sr.contentSnippet;
                for (auto& sc2 : lowerSnip)
                    sc2 = towlower(sc2);
                if (sr.contentSnippet.empty() || lowerSnip.find(lowerKw) == std::wstring::npos)
                {
                    // Try reading a small portion of the file to find the keyword
                    HANDLE hFile = CreateFileW(sr.fullPath.c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                    if (hFile != INVALID_HANDLE_VALUE)
                    {
                        // Read up to 64KB to search for the keyword
                        const DWORD readSize = 64 * 1024;
                        std::vector<char> buf(readSize + 2, 0);
                        DWORD bytesRead = 0;
                        if (ReadFile(hFile, buf.data(), readSize, &bytesRead, NULL) && bytesRead > 0)
                        {
                            // Try as UTF-16 first (BOM check)
                            std::wstring fileText;
                            if (bytesRead >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE)
                            {
                                fileText = std::wstring((wchar_t*)(buf.data() + 2), (bytesRead - 2) / sizeof(wchar_t));
                            }
                            else
                            {
                                // Treat as ANSI/UTF-8 and widen
                                int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)bytesRead, NULL, 0);
                                if (wlen > 0)
                                {
                                    fileText.resize(wlen);
                                    MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)bytesRead, &fileText[0], wlen);
                                }
                            }
                            if (!fileText.empty())
                            {
                                std::wstring lowerFile = fileText;
                                for (auto& fc : lowerFile)
                                    fc = towlower(fc);
                                size_t kwPos = lowerFile.find(lowerKw);
                                if (kwPos != std::wstring::npos)
                                {
                                    // Extract ~40 chars before and ~200 chars after the match
                                    size_t snippetStart = (kwPos > 40) ? kwPos - 40 : 0;
                                    size_t snippetLen = 280;
                                    if (snippetStart + snippetLen > fileText.length())
                                        snippetLen = fileText.length() - snippetStart;
                                    sr.contentSnippet = fileText.substr(snippetStart, snippetLen);
                                    // Collapse whitespace
                                    for (auto& sc3 : sr.contentSnippet)
                                    {
                                        if (sc3 == L'\r' || sc3 == L'\n' || sc3 == L'\t')
                                            sc3 = L' ';
                                    }
                                    if (snippetStart > 0)
                                        sr.contentSnippet = L"..." + sr.contentSnippet;
                                    if (snippetStart + snippetLen < fileText.length())
                                        sr.contentSnippet += L"...";
                                }
                            }
                        }
                        CloseHandle(hFile);
                    }
                }
            }

            // Filename matches rank higher than content-only matches
            sr.matchScore = filenameMatch ? 60 : 20;
            searchResults.push_back(std::move(sr));

            if (searchResults.size() >= MAX_RESULTS)
                break;
        }

        pRowset->ReleaseRows(cRowsObtained, hRows, NULL, NULL, NULL);
        pRows = hRows;
    }

    pAccessor->ReleaseAccessor(hAcc, NULL);
    pAccessor->Release();
    pRowset->Release();
    pDBInitialize->Uninitialize();
    pDBInitialize->Release();
    if (comInited) CoUninitialize();

    // --- Query logged-in cloud providers via REST API and merge results ---
    int cloudHits = 0;
    for (int ci = 0; ci < CP_COUNT; ci++)
    {
        if (!cloudProviders[ci].loggedIn)
            continue;
        std::vector<SearchResult> cloudResults = CloudSearchFiles((CloudProviderId)ci, keyword);
        for (auto& cr : cloudResults)
        {
            cr.matchScore = 51; // cloud results ranked between local filename(60) and content(20)
            searchResults.push_back(std::move(cr));
            cloudHits++;
        }
    }

    // Sort: filename matches first, then cloud, then content matches, alphabetical within
    std::sort(searchResults.begin(), searchResults.end(),
        [](const SearchResult& a, const SearchResult& b) {
            if (a.matchScore != b.matchScore)
                return a.matchScore > b.matchScore;
            return _wcsicmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
        });

    PopulateSearchResults();

    QueryPerformanceCounter(&searchT2);
    double searchElapsedMS = (searchT2.QuadPart - searchT1.QuadPart) * 1000.0 / searchFreq.QuadPart;

    // Switch to Search Results tab
    TabCtrl_SetCurSel(hTabControl, 1);
    ShowTab(1);

    // Count filename vs content matches for the status line
    int filenameHits = 0, contentHits = 0;
    std::map<std::wstring, int> cloudHitsByProvider;
    for (auto& sr2 : searchResults)
    {
        if (sr2.matchScore >= 60) filenameHits++;
        else if (sr2.matchScore == 51)
        {
            std::wstring src = sr2.cloudSource.empty() ? L"Cloud" : sr2.cloudSource;
            cloudHitsByProvider[src]++;
        }
        else contentHits++;
    }

    std::wstring status = L"Windows Search: " + std::to_wstring(searchResults.size()) + L" results";
    if (searchResults.size() >= MAX_RESULTS)
        status += L" (showing first 10,000)";
    status += L"  |  " + std::to_wstring(filenameHits) + L" filename, "
              + std::to_wstring(contentHits) + L" content";
    for (auto& kv : cloudHitsByProvider)
        status += L", " + std::to_wstring(kv.second) + L" " + kv.first;
    if (!currentSearchScope.empty())
        status += L"  |  Scope: " + currentSearchScope;
    wchar_t timeBuf[64];
    swprintf_s(timeBuf, 64, L"  |  %.1f ms", searchElapsedMS);
    status += timeBuf;
    SetWindowTextW(hStatusText, status.c_str());
}

// Format a byte size into a human-readable string.
static std::wstring FormatFileSize(ULONGLONG size)
{
    wchar_t buf[64];
    if (size < 1024ULL)
        swprintf_s(buf, 64, L"%llu bytes", size);
    else if (size < 1024ULL * 1024)
        swprintf_s(buf, 64, L"%.1f KB", size / 1024.0);
    else if (size < 1024ULL * 1024 * 1024)
        swprintf_s(buf, 64, L"%.1f MB", size / (1024.0 * 1024.0));
    else
        swprintf_s(buf, 64, L"%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
    return std::wstring(buf);
}

// Update the properties panel with details for the selected search result.
void UpdatePropertiesPanel(int selectedIndex)
{
    if (!hPropertiesPanel)
        return;

    if (selectedIndex < 0 || selectedIndex >= (int)searchResults.size())
    {
        SetWindowTextW(hPropertiesPanel, L"");
        InvalidateRect(hPropertiesPanel, NULL, TRUE);
        return;
    }

    const SearchResult& sr = searchResults[selectedIndex];

    std::wstring info;
    info += L"  Name:\r\n  " + sr.fileName + L"\r\n\r\n";
    info += L"  Type:  " + std::wstring(sr.isDirectory ? L"Folder" : L"File") + L"\r\n\r\n";
    info += L"  Path:\r\n  " + sr.fullPath + L"\r\n\r\n";

    if (!sr.contentSnippet.empty())
    {
        // Build snippet text with «keyword» markers around each match
        std::wstring snippet = sr.contentSnippet;
        std::wstring lowerSnippet = snippet;
        for (auto& sc : lowerSnippet)
            sc = towlower(sc);
        std::wstring marked;
        size_t kwLen = currentSearchKeyword.length();
        size_t spos = 0;
        while (spos < snippet.length())
        {
            size_t found = (kwLen > 0) ? lowerSnippet.find(currentSearchKeyword, spos) : std::wstring::npos;
            if (found == std::wstring::npos)
            {
                marked += snippet.substr(spos);
                break;
            }
            marked += snippet.substr(spos, found - spos);
            marked += L"\u00AB";
            marked += snippet.substr(found, kwLen);
            marked += L"\u00BB";
            spos = found + kwLen;
        }
        info += L"  Content Match:\r\n  " + marked + L"\r\n\r\n";
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {0};
    if (GetFileAttributesExW(sr.fullPath.c_str(), GetFileExInfoStandard, &fad))
    {
        if (!sr.isDirectory)
        {
            ULONGLONG fileSize = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            info += L"  Size:  " + FormatFileSize(fileSize) + L"\r\n\r\n";
        }

        auto fmtTime = [](const FILETIME& ft) -> std::wstring {
            if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0)
                return L"—";
            FILETIME ftLocal;
            FileTimeToLocalFileTime(&ft, &ftLocal);
            SYSTEMTIME st;
            FileTimeToSystemTime(&ftLocal, &st);
            wchar_t buf[64];
            swprintf_s(buf, 64, L"%04d-%02d-%02d %02d:%02d:%02d",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            return std::wstring(buf);
        };

        info += L"  Created:\r\n  " + fmtTime(fad.ftCreationTime) + L"\r\n\r\n";
        info += L"  Modified:\r\n  " + fmtTime(fad.ftLastWriteTime) + L"\r\n\r\n";
        info += L"  Accessed:\r\n  " + fmtTime(fad.ftLastAccessTime) + L"\r\n\r\n";

        std::wstring attrs;
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY)  attrs += L"R ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)    attrs += L"H ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)    attrs += L"S ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)   attrs += L"A ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) attrs += L"C ";
        if (fad.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) attrs += L"E ";
        if (!attrs.empty())
            info += L"  Attributes:  " + attrs + L"\r\n";
    }
    else
    {
        info += L"  (File not accessible)\r\n";
    }

    SetWindowTextW(hPropertiesPanel, info.c_str());
    InvalidateRect(hPropertiesPanel, NULL, TRUE);
}

// Populate the ListView with search results
void PopulateSearchResults()
{
    if (!hListView)
        return;

    // Clear the properties panel so stale data from a previous search doesn't linger
    if (hPropertiesPanel)
    {
        SetWindowTextW(hPropertiesPanel, L"");
        InvalidateRect(hPropertiesPanel, NULL, TRUE);
    }

    SendMessage(hListView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hListView);

    for (size_t i = 0; i < searchResults.size(); i++)
    {
        const SearchResult& sr = searchResults[i];

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)sr.fileName.c_str();
        lvi.lParam = (LPARAM)i;
        lvi.iImage = GetFileIconIndex(sr.fileName, sr.isDirectory);
        ListView_InsertItem(hListView, &lvi);

        // Type column
        ListView_SetItemText(hListView, (int)i, 1,
            (LPWSTR)(sr.isDirectory ? L"Folder" : L"File"));

        // Full path column
        ListView_SetItemText(hListView, (int)i, 2, (LPWSTR)sr.fullPath.c_str());

        // Modified date column
        std::wstring modDate = FormatTimestamp(sr.fileRef, sr.fullPath);
        ListView_SetItemText(hListView, (int)i, 3, (LPWSTR)modDate.c_str());

        // Match quality column
        // MFT scores: Exact=100, Wildcard=90, Prefix=75, Token=50, Substring=40, Stem=30/25
        // Bonuses: case match +15, recency +10/+7/+3
        // Windows Search scores: Filename=60, Content=20
        const wchar_t* quality = L"";
        if (bWindowsSearch)
        {
            if (sr.matchScore == 51)
                quality = sr.cloudSource.empty() ? L"Cloud" : sr.cloudSource.c_str();
            else
                quality = (sr.matchScore >= 60) ? L"Filename" : L"Content";
        }
        else if (bWildcardSearch) quality = L"Wildcard";
        else if (sr.baseScore >= 100) quality = L"Exact";
        else if (sr.baseScore >= 75) quality = L"Prefix";
        else if (sr.baseScore >= 50) quality = L"Token Match";
        else if (sr.baseScore >= 40) quality = L"Substring";
        else if (sr.baseScore >= 25) quality = L"Stem";
        else if (sr.baseScore >= 15) quality = L"Fuzzy";
        ListView_SetItemText(hListView, (int)i, 4, (LPWSTR)quality);

        // WARP Boost column
        wchar_t warpBuf[16] = {0};
        if (sr.warpBonus > 0)
            swprintf_s(warpBuf, 16, L"+%d", sr.warpBonus);
        else
            wcscpy_s(warpBuf, 16, L"\u2014");
        ListView_SetItemText(hListView, (int)i, 5, warpBuf);

        // Total Score column
        wchar_t scoreBuf[16] = {0};
        swprintf_s(scoreBuf, 16, L"%d", sr.matchScore);
        ListView_SetItemText(hListView, (int)i, 6, scoreBuf);

        // Snippet column — show content preview for Windows Search content matches
        if (!sr.contentSnippet.empty())
            ListView_SetItemText(hListView, (int)i, 7, (LPWSTR)sr.contentSnippet.c_str());
    }

    SendMessage(hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hListView, NULL, TRUE);
}

// Launch a file or folder from search results
void LaunchSearchResult(int index)
{
    if (index < 0 || index >= (int)searchResults.size())
        return;

    const SearchResult& sr = searchResults[index];

    // Cloud results have URLs — open in browser
    if (sr.fullPath.find(L"http://") == 0 || sr.fullPath.find(L"https://") == 0)
    {
        ShellExecuteW(NULL, L"open", sr.fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    if (sr.isDirectory)
    {
        ShellExecuteW(NULL, L"explore", sr.fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        ShellExecuteW(NULL, L"open", sr.fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

// Show/hide tab pages
void ShowTab(int tabIndex)
{
    if (tabIndex == 0)
    {
        ShowWindow(hTreeView, SW_SHOW);
        ShowWindow(hListView, SW_HIDE);
        if (hPropertiesPanel) ShowWindow(hPropertiesPanel, SW_HIDE);
        if (hPropToggleBtn) ShowWindow(hPropToggleBtn, SW_HIDE);
    }
    else
    {
        ShowWindow(hTreeView, SW_HIDE);
        ShowWindow(hListView, SW_SHOW);
        if (hPropertiesPanel && !bPropertiesCollapsed) ShowWindow(hPropertiesPanel, SW_SHOW);
        if (hPropToggleBtn) ShowWindow(hPropToggleBtn, SW_SHOW);
    }
}

bool ReadDriveMFT(HWND hWnd, const std::wstring& drive, int& totalFiles, int& totalDirs)
{
    try
    {
        std::wstring volumePath = L"\\\\.\\" + drive + L":";
        
        HANDLE hVolume = CreateFileW(volumePath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        
        if (hVolume == INVALID_HANDLE_VALUE)
        {
            MessageBoxW(hWnd, L"Failed to open volume. Run as Administrator.",
                L"Error", MB_OK | MB_ICONERROR);
            return false;
        }

        DWORD bytesReturned = 0;
        
        // Get USN journal data
        USN_JOURNAL_DATA_V0 journalData = {0};
        if (!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0,
            &journalData, sizeof(journalData), &bytesReturned, NULL))
        {
            // Journal might not exist - try to create it
            CREATE_USN_JOURNAL_DATA createData = {0};
            createData.MaximumSize = 0;
            createData.AllocationDelta = 0;
            DeviceIoControl(hVolume, FSCTL_CREATE_USN_JOURNAL, &createData,
                sizeof(createData), NULL, 0, &bytesReturned, NULL);
            
            if (!DeviceIoControl(hVolume, FSCTL_QUERY_USN_JOURNAL, NULL, 0,
                &journalData, sizeof(journalData), &bytesReturned, NULL))
            {
                CloseHandle(hVolume);
                MessageBoxW(hWnd, L"Failed to query USN journal.", L"Error", MB_OK | MB_ICONERROR);
                return false;
            }
        }

        // Buffer for enumeration results
        const DWORD bufferSize = 128 * 1024; // 128KB buffer for better throughput
        std::vector<BYTE> buffer(bufferSize);
        
        MFT_ENUM_DATA_V0 enumData = {0};
        enumData.StartFileReferenceNumber = 0;
        enumData.LowUsn = 0;
        enumData.HighUsn = journalData.NextUsn;
        
        // Pre-reserve capacity for typical drive sizes
        fileMap.reserve(2000000);
        childrenMap.reserve(500000);

        int fileCount = 0;
        int dirCount = 0;
        int totalEntries = 0;
        
        UpdateProgress(15, L"Enumerating drive " + drive + L":...");
        
        while (true)
        {
            BOOL result = DeviceIoControl(hVolume, FSCTL_ENUM_USN_DATA,
                &enumData, sizeof(enumData),
                buffer.data(), bufferSize,
                &bytesReturned, NULL);
            
            if (!result)
            {
                DWORD err = GetLastError();
                if (err == ERROR_HANDLE_EOF)
                    break;
                // Any other error - stop
                break;
            }
            
            if (bytesReturned <= sizeof(USN))
                break;
            
            // First 8 bytes are the next start reference number
            ULONGLONG nextRef = *(ULONGLONG*)buffer.data();
            
            // Walk the USN_RECORD entries after the 8-byte header
            DWORD offset = sizeof(USN);
            while (offset < bytesReturned)
            {
                USN_RECORD* record = (USN_RECORD*)(buffer.data() + offset);
                
                if (record->RecordLength == 0)
                    break;
                
                if (offset + record->RecordLength > bytesReturned)
                    break;
                
                // Extract file reference and parent reference (lower 48 bits)
                ULONGLONG fileRef = record->FileReferenceNumber & 0x0000FFFFFFFFFFFF;
                ULONGLONG parentRef = record->ParentFileReferenceNumber & 0x0000FFFFFFFFFFFF;
                
                // Get filename
                DWORD fileNameLength = record->FileNameLength / sizeof(WCHAR);
                
                if (fileNameLength > 0 && fileNameLength <= 255 &&
                    record->FileNameOffset + record->FileNameLength <= record->RecordLength)
                {
                    WCHAR* fileName = (WCHAR*)((BYTE*)record + record->FileNameOffset);
                    
                    FileEntry entry;
                    entry.fileReference = fileRef;
                    entry.parentReference = record->ParentFileReferenceNumber;
                    entry.fileName = std::wstring(fileName, fileNameLength);
                    entry.lowerFileName = entry.fileName;
                    for (auto& lch : entry.lowerFileName)
                        lch = towlower(lch);
                    entry.isDirectory = (record->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    entry.hasChildren = false;
                    entry.childrenLoaded = false;
                    
                    bool isDir = entry.isDirectory;
                    LARGE_INTEGER ts;
                    ts.QuadPart = record->TimeStamp.QuadPart;
                    timestampCache[fileRef] = ts;
                    fileMap.emplace(fileRef, std::move(entry));
                    childrenMap[parentRef].push_back(fileRef);
                    
                    if (isDir)
                        dirCount++;
                    else
                        fileCount++;
                    
                    totalEntries++;
                }
                
                offset += record->RecordLength;
            }
            
            // Update progress periodically
            if (totalEntries % 50000 == 0)
            {
                std::wstring status = L"Reading drive " + drive + L": " +
                    std::to_wstring(totalEntries) + L" entries...";
                UpdateProgress(15 + min((int)((totalEntries * 65ULL) / 5000000), 65), status);
            }
            
            // Move to next batch
            enumData.StartFileReferenceNumber = nextRef;
        }
        
        CloseHandle(hVolume);
        
        totalFiles = fileCount;
        totalDirs = dirCount;
        
        // Pre-compute hasChildren flag for all directories
        UpdateProgress(82, L"Indexing directory structure...");
        for (auto& pair : fileMap)
        {
            FileEntry& entry = pair.second;
            if (entry.isDirectory)
            {
                entry.hasChildren = (childrenMap.find(entry.fileReference) != childrenMap.end());
            }
            else
            {
                entry.hasChildren = false;
            }
            entry.childrenLoaded = false;
        }
        
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void BuildTreeView(const std::wstring& drive)
{
    if (!hTreeView)
        return;

    UpdateProgress(85, L"Building tree structure...");

    try
    {
        TreeView_DeleteAllItems(hTreeView);
        treeItemToRef.clear();

        std::wstring driveLabel = drive + L":\\";
        
        // Root directory is MFT entry 5 - always has children
        HTREEITEM hDriveRoot = InsertTreeItem(hTreeView, TVI_ROOT, driveLabel, 5, true);
        
        if (hDriveRoot == NULL)
            return;

        UpdateProgress(90, L"Loading root folder contents...");

        // Only populate the first level (direct children of root)
        PopulateChildren(5, hDriveRoot);
        
        // Mark root as loaded so TVN_ITEMEXPANDING won't re-populate
        auto rootIt = fileMap.find(5);
        if (rootIt != fileMap.end())
        {
            rootIt->second.childrenLoaded = true;
        }

        TreeView_Expand(hTreeView, hDriveRoot, TVE_EXPAND);
        
        UpdateProgress(100, L"Complete!");
    }
    catch (const std::exception& e)
    {
        std::string error = "Exception in BuildTreeView: ";
        error += e.what();
        MessageBoxA(NULL, error.c_str(), "Error", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        MessageBoxW(NULL, L"Unknown exception in BuildTreeView", L"Error", MB_OK | MB_ICONERROR);
    }
}

void ReadDriveAndBuildTree(HWND hWnd, const std::wstring& drive)
{
    try
    {
        // Show progress controls and hide summary
        if (hProgressBar)
            ShowWindow(hProgressBar, SW_SHOW);
        if (hStatusText)
            ShowWindow(hStatusText, SW_SHOW);
        if (hSummaryText)
            ShowWindow(hSummaryText, SW_HIDE);

        UpdateProgress(0, L"Initializing...");

        // Stop any existing USN monitor
        StopUsnMonitor();

        // Clear previous data
        fileMap.clear();
        childrenMap.clear();
        treeItemToRef.clear();
        invertedIndex.clear();
        stemmedIndex.clear();
        sortedTokens.clear();
        sortedStemTokens.clear();
        trigramIndex.clear();
        fullPathCache.clear();
        timestampCache.clear();
        searchResults.clear();
        warpRecencyByPath.clear();
        bWarpAvailable = false;

        // Disable drive buttons during scan
        for (auto& db : driveButtons)
        {
            EnableWindow(db.hButton, FALSE);
        }

        UpdateProgress(5, L"Opening drive " + drive + L":...");

        LARGE_INTEGER freq, t1, t2;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t1);

        int totalFiles = 0;
        int totalDirs = 0;
        
        if (ReadDriveMFT(hWnd, drive, totalFiles, totalDirs))
        {
            QueryPerformanceCounter(&t2);
            double elapsedTimeMS = (t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;

            // Update summary text
            if (hSummaryText)
            {
                std::wstring summaryText = L"Drive " + drive + L":  |  MFT Read: " +
                    std::to_wstring((int)elapsedTimeMS) +
                    L" ms  |  Folders: " + std::to_wstring(totalDirs) +
                    L"  |  Files: " + std::to_wstring(totalFiles);
                SetWindowTextW(hSummaryText, summaryText.c_str());
                ShowWindow(hSummaryText, SW_SHOW);
            }

            UpdateProgress(85, L"Building tree view...");
            
            currentDriveLetter = drive;
            BuildTreeView(drive);
            
            UpdateProgress(92, L"Building search index...");
            BuildSearchIndex(drive);

            // Pre-populate fullPathCache for all files so WARP recency lookups work
            UpdateProgress(98, L"Building path cache...");
            for (auto& pair : fileMap)
                BuildFullPath(pair.first, drive);

            // Query WARP for file activity recency signals
            UpdateProgress(99, L"Querying WARP activity...");
            QueryWarpActivity();

            if (!bTreeRenderedOnce)
            {
                bTreeRenderedOnce = true;
                if (hSearchEdit) EnableWindow(hSearchEdit, TRUE);
                if (hFindButton) EnableWindow(hFindButton, TRUE);
                if (hWinSearchButton) EnableWindow(hWinSearchButton, TRUE);
            }

            // Start live USN journal monitoring
            StartUsnMonitor(drive);
        }
        else
        {
            UpdateProgress(0, L"Failed to read drive " + drive + L":");
        }

        // Re-enable drive buttons
        for (auto& db : driveButtons)
        {
            EnableWindow(db.hButton, TRUE);
        }

        // Hide progress bar after completion
        Sleep(500);
        if (hProgressBar)
            ShowWindow(hProgressBar, SW_HIDE);
        if (hStatusText)
            SetWindowTextW(hStatusText, L"Select a drive to scan...");
    }
    catch (const std::exception& e)
    {
        std::string error = "Exception: ";
        error += e.what();
        MessageBoxA(hWnd, error.c_str(), "Error", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        MessageBoxW(hWnd, L"Unknown exception", L"Error", MB_OK | MB_ICONERROR);
    }
    
    // Re-enable drive buttons on error path too
    for (auto& db : driveButtons)
    {
        EnableWindow(db.hButton, TRUE);
    }
}

void CreateDriveButtons(HWND hWnd)
{
    for (auto& db : driveButtons)
    {
        if (db.hButton)
            DestroyWindow(db.hButton);
    }
    driveButtons.clear();
    
    std::vector<std::wstring> drives = GetFixedNtfsDrives();
    
    int buttonWidth = 80;
    int buttonHeight = 30;
    int spacing = 6;
    int startX = 12;
    int btnY = (TOOLBAR_ROW_HEIGHT - buttonHeight) / 2;
    
    for (size_t i = 0; i < drives.size(); i++)
    {
        std::wstring label = L"Scan " + drives[i] + L":";
        
        HWND hBtn = CreateWindowExW(
            0, L"BUTTON", label.c_str(),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            startX + (int)(i * (buttonWidth + spacing)), btnY,
            buttonWidth, buttonHeight,
            hWnd,
            (HMENU)(INT_PTR)(IDC_DRIVE_BUTTON_BASE + i),
            hInst, NULL);
        
        DriveButton db;
        db.hButton = hBtn;
        db.driveLetter = drives[i];
        driveButtons.push_back(db);
    }
}

void LayoutControls(HWND hWnd)
{
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);
    
    int buttonRowHeight = TOOLBAR_ROW_HEIGHT;
    int statusHeight = 26;
    int progressHeight = 20;
    int summaryHeight = 26;
    int tabHeaderHeight = 25;
    int currentY = 0;
    
    // Position drive buttons
    int buttonWidth = 80;
    int buttonHeight = 30;
    int spacing = 6;
    int startX = 12;
    int buttonsEndX = startX;
    int btnY = (buttonRowHeight - buttonHeight) / 2;
    for (size_t i = 0; i < driveButtons.size(); i++)
    {
        if (driveButtons[i].hButton)
        {
            SetWindowPos(driveButtons[i].hButton, NULL,
                startX + (int)(i * (buttonWidth + spacing)), btnY,
                buttonWidth, buttonHeight, SWP_NOZORDER);
        }
        buttonsEndX = startX + (int)((i + 1) * (buttonWidth + spacing));
    }
    
    // Position search edit and find button after drive buttons
    int searchX = buttonsEndX + 12;
    int searchEditWidth = 220;
    int findBtnWidth = 65;
    int editY = (buttonRowHeight - 26) / 2;
    if (hSearchEdit)
    {
        SetWindowPos(hSearchEdit, NULL, searchX, editY,
            searchEditWidth, 26, SWP_NOZORDER);
    }
    if (hFindButton)
    {
        SetWindowPos(hFindButton, NULL, searchX + searchEditWidth + 6, btnY,
            findBtnWidth, buttonHeight, SWP_NOZORDER);
    }
    int winSearchBtnWidth = 175;
    if (hWinSearchButton)
    {
        SetWindowPos(hWinSearchButton, NULL,
            searchX + searchEditWidth + 6 + findBtnWidth + 6, btnY,
            winSearchBtnWidth, buttonHeight, SWP_NOZORDER);
    }
    int darkBtnWidth = 75;
    int cloudBtnWidth = 120;
    if (hCloudBtn)
    {
        SetWindowPos(hCloudBtn, NULL,
            rcClient.right - darkBtnWidth - 12 - cloudBtnWidth - 6, btnY,
            cloudBtnWidth, buttonHeight, SWP_NOZORDER);
    }
    if (hDarkModeBtn)
    {
        SetWindowPos(hDarkModeBtn, NULL,
            rcClient.right - darkBtnWidth - 12, btnY,
            darkBtnWidth, buttonHeight, SWP_NOZORDER);
    }
    currentY += buttonRowHeight;
    
    if (hStatusText)
    {
        SetWindowPos(hStatusText, NULL, 0, currentY,
            rcClient.right, statusHeight, SWP_NOZORDER);
    }
    currentY += statusHeight;
    
    if (hProgressBar)
    {
        SetWindowPos(hProgressBar, NULL, 10, currentY,
            rcClient.right - 20, progressHeight, SWP_NOZORDER);
    }
    currentY += progressHeight;
    
    if (hSummaryText)
    {
        SetWindowPos(hSummaryText, NULL, 0, currentY,
            rcClient.right, summaryHeight, SWP_NOZORDER);
    }
    currentY += summaryHeight;
    
    // Tab control fills remaining space
    int tabTop = currentY;
    int tabHeight = rcClient.bottom - tabTop;
    if (hTabControl)
    {
        SetWindowPos(hTabControl, NULL, 0, tabTop,
            rcClient.right, tabHeight, SWP_NOZORDER);
    }
    
    // Get tab display area (inside the tab control)
    RECT rcTab;
    if (hTabControl)
    {
        GetClientRect(hTabControl, &rcTab);
        TabCtrl_AdjustRect(hTabControl, FALSE, &rcTab);
    }
    else
    {
        rcTab.left = 0;
        rcTab.top = tabHeaderHeight;
        rcTab.right = rcClient.right;
        rcTab.bottom = tabHeight;
    }
    
    int contentX = rcTab.left;
    int contentY = tabTop + rcTab.top;
    int contentW = rcTab.right - rcTab.left;
    int contentH = rcTab.bottom - rcTab.top;
    
    // Toggle button dimensions
    int toggleBtnW = 36;
    int toggleBtnH = 36;
    int toggleBtnPad = 4;

    if (hTreeView)
    {
        SetWindowPos(hTreeView, HWND_TOP, contentX, contentY,
            contentW, contentH, 0);
    }
    if (hListView)
    {
        int listW = contentW;
        if (hPropertiesPanel && !bPropertiesCollapsed)
        {
            // Clamp properties width to valid range
            if (propertiesPanelW > contentW - MIN_LIST_WIDTH - SPLITTER_WIDTH)
                propertiesPanelW = contentW - MIN_LIST_WIDTH - SPLITTER_WIDTH;
            if (propertiesPanelW < MIN_PROP_WIDTH)
                propertiesPanelW = MIN_PROP_WIDTH;
            listW = contentW - propertiesPanelW - SPLITTER_WIDTH;
        }
        SetWindowPos(hListView, HWND_TOP, contentX, contentY,
            listW, contentH, 0);
    }
    if (hPropertiesPanel)
    {
        if (bPropertiesCollapsed)
        {
            ShowWindow(hPropertiesPanel, SW_HIDE);
            // Zero out splitter rect so it won't be drawn or hit-tested
            splitterRect = {0, 0, 0, 0};
        }
        else
        {
            int listW = contentW - propertiesPanelW - SPLITTER_WIDTH;
            int splitterX = contentX + listW;
            int propX = splitterX + SPLITTER_WIDTH;
            SetWindowPos(hPropertiesPanel, HWND_TOP, propX, contentY,
                propertiesPanelW, contentH, 0);
            // Only show if on the search results tab
            int curTab = hTabControl ? TabCtrl_GetCurSel(hTabControl) : 0;
            if (curTab == 1)
                ShowWindow(hPropertiesPanel, SW_SHOW);

            // Cache the splitter rect in main window client coordinates
            splitterRect.left = splitterX;
            splitterRect.top = contentY;
            splitterRect.right = splitterX + SPLITTER_WIDTH;
            splitterRect.bottom = contentY + contentH;
        }
    }
    // Position the properties toggle button next to the splitter / properties panel edge
    if (hPropToggleBtn)
    {
        int tbX;
        if (bPropertiesCollapsed)
        {
            // Collapsed: sit at the right edge of the content area
            tbX = contentX + contentW - toggleBtnW - toggleBtnPad;
        }
        else
        {
            // Expanded: sit just to the left of the splitter
            int listW = contentW - propertiesPanelW - SPLITTER_WIDTH;
            tbX = contentX + listW - toggleBtnW - toggleBtnPad;
        }
        int tbY = contentY + (contentH - toggleBtnH) / 2;
        SetWindowPos(hPropToggleBtn, HWND_TOP, tbX, tbY,
            toggleBtnW, toggleBtnH, SWP_NOACTIVATE);
        // Ensure the toggle button stays above the ListView in z-order
        BringWindowToTop(hPropToggleBtn);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_FINDMYFILE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FINDMYFILE));

    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FINDMYFILE));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = NULL;
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_FINDMYFILE);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 1024, 700, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            CreateDriveButtons(hWnd);

            // Search box and Find button (placed after drive buttons in the toolbar row)
            hSearchEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_DISABLED,
                0, 5, 200, 25, hWnd, (HMENU)IDC_SEARCH_EDIT, hInst, NULL);

            hFindButton = CreateWindowExW(
                0, L"BUTTON", L"Find",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
                0, 5, 60, 25, hWnd, (HMENU)IDC_FIND_BUTTON, hInst, NULL);

            hWinSearchButton = CreateWindowExW(
                0, L"BUTTON", L"Find with Windows Search",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED,
                0, 5, 160, 25, hWnd, (HMENU)IDC_WINSEARCH_BUTTON, hInst, NULL);

            hStatusText = CreateWindowExW(
                0, L"STATIC", L"Select a drive to scan...",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 40, 0, 25, hWnd, (HMENU)1003, hInst, NULL);

            hProgressBar = CreateWindowExW(
                0, PROGRESS_CLASS, NULL,
                WS_CHILD | PBS_SMOOTH,
                0, 65, 0, 20, hWnd, (HMENU)1002, hInst, NULL);

            if (hProgressBar)
            {
                SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
                SendMessage(hProgressBar, PBM_SETSTEP, 1, 0);
            }

            hSummaryText = CreateWindowExW(
                0, L"STATIC", L"",
                WS_CHILD | SS_CENTER,
                0, 85, 0, 25, hWnd, (HMENU)1004, hInst, NULL);

            // Tab control
            hTabControl = CreateWindowExW(
                0, WC_TABCONTROL, L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                0, 110, 0, 0, hWnd, (HMENU)IDC_TAB_CONTROL, hInst, NULL);

            if (hTabControl)
            {
                TCITEMW tie = {0};
                tie.mask = TCIF_TEXT;
                tie.pszText = (LPWSTR)L"Browse";
                TabCtrl_InsertItem(hTabControl, 0, &tie);
                tie.pszText = (LPWSTR)L"Search Results";
                TabCtrl_InsertItem(hTabControl, 1, &tie);
            }

            hTreeView = CreateWindowExW(
                0, WC_TREEVIEW, L"Tree View",
                WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES |
                TVS_HASBUTTONS | TVS_LINESATROOT,
                0, 110, 0, 0, hWnd, (HMENU)1001, hInst, NULL);

            hListView = CreateWindowExW(
                0, WC_LISTVIEW, L"",
                WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 110, 0, 0, hWnd, (HMENU)IDC_LISTVIEW, hInst, NULL);

            if (hListView)
            {
                ListView_SetExtendedListViewStyle(hListView,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);

                LVCOLUMNW lvc = {0};
                lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

                lvc.iSubItem = 0;
                lvc.pszText = (LPWSTR)L"Name";
                lvc.cx = 250;
                ListView_InsertColumn(hListView, 0, &lvc);

                lvc.iSubItem = 1;
                lvc.pszText = (LPWSTR)L"Type";
                lvc.cx = 60;
                ListView_InsertColumn(hListView, 1, &lvc);

                lvc.iSubItem = 2;
                lvc.pszText = (LPWSTR)L"Full Path";
                lvc.cx = 350;
                ListView_InsertColumn(hListView, 2, &lvc);

                lvc.iSubItem = 3;
                lvc.pszText = (LPWSTR)L"Modified";
                lvc.cx = 130;
                ListView_InsertColumn(hListView, 3, &lvc);

                lvc.iSubItem = 4;
                lvc.pszText = (LPWSTR)L"Match";
                lvc.cx = 90;
                ListView_InsertColumn(hListView, 4, &lvc);

                lvc.iSubItem = 5;
                lvc.pszText = (LPWSTR)L"WARP Boost";
                lvc.cx = 85;
                ListView_InsertColumn(hListView, 5, &lvc);

                lvc.iSubItem = 6;
                lvc.pszText = (LPWSTR)L"Total Score";
                lvc.cx = 80;
                ListView_InsertColumn(hListView, 6, &lvc);

                lvc.iSubItem = 7;
                lvc.pszText = (LPWSTR)L"Snippet";
                lvc.cx = 300;
                ListView_InsertColumn(hListView, 7, &lvc);

                // Attach the system small icon image list to the ListView
                SHFILEINFOW sfi = {0};
                hSysImageList = (HIMAGELIST)SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi),
                    SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
                if (hSysImageList)
                    ListView_SetImageList(hListView, hSysImageList, LVSIL_SMALL);
            }

            // Initially show Browse tab, hide Search Results
            ShowTab(0);

            // Properties panel (right side of Search Results tab)
            hPropertiesPanel = CreateWindowExW(
                0, L"EDIT", L"",
                WS_CHILD | WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 110, PROPERTIES_PANEL_W, 0, hWnd, (HMENU)IDC_PROPERTIES, hInst, NULL);

            hMainWnd = hWnd;

            // Create modern font and apply to all controls
            hUIFont = CreateFontW(
                -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            if (hUIFont)
                ApplyFontToAllChildren(hWnd, hUIFont);

            // Apply Explorer visual theme to tree and list views
            if (hTreeView)
                SetWindowTheme(hTreeView, L"Explorer", NULL);
            if (hListView)
                SetWindowTheme(hListView, L"Explorer", NULL);

            hToolbarBrush = CreateSolidBrush(CurrentScheme().toolbarBg);
            hStatusBrush = CreateSolidBrush(CurrentScheme().statusBg);
            hEditBrush = CreateSolidBrush(CurrentScheme().editBg);

            hDarkModeBtn = CreateWindowExW(
                0, L"BUTTON", L"\u263D Dark",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 70, 30, hWnd, (HMENU)IDC_DARKMODE_BTN, hInst, NULL);
            if (hUIFont && hDarkModeBtn)
                SendMessage(hDarkModeBtn, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            // Cloud storage providers button
            hCloudBtn = CreateWindowExW(
                0, L"BUTTON", L"\u2601 Cloud",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 120, 30, hWnd, (HMENU)IDC_CLOUD_BTN, hInst, NULL);
            if (hUIFont && hCloudBtn)
                SendMessage(hCloudBtn, WM_SETFONT, (WPARAM)hUIFont, TRUE);

            // Load saved cloud configuration
            LoadCloudConfig();
            UpdateCloudButtonLabel();

            // Larger font for the toggle button glyph
            hToggleFont = CreateFontW(
                -20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            // Bold font for ListView column headers
            hHeaderFont = CreateFontW(
                -14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            if (hHeaderFont && hListView)
            {
                HWND hHeader = ListView_GetHeader(hListView);
                if (hHeader)
                    SendMessage(hHeader, WM_SETFONT, (WPARAM)hHeaderFont, TRUE);
            }

            // Properties panel collapse/expand toggle button (hidden until Search Results tab)
            hPropToggleBtn = CreateWindowExW(
                0, L"BUTTON", L"\u00BB",
                WS_CHILD | WS_CLIPSIBLINGS | BS_OWNERDRAW,
                0, 0, 36, 36, hWnd, (HMENU)IDC_PROP_TOGGLE, hInst, NULL);
            if (hToggleFont && hPropToggleBtn)
                SendMessage(hPropToggleBtn, WM_SETFONT, (WPARAM)hToggleFont, TRUE);

            // Tooltip for the toggle button
            hPropToggleTooltip = CreateWindowExW(
                0, TOOLTIPS_CLASSW, NULL,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hWnd, NULL, hInst, NULL);
            if (hPropToggleTooltip && hPropToggleBtn)
            {
                TOOLINFOW ti = {0};
                ti.cbSize = sizeof(TOOLINFOW);
                ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.hwnd = hWnd;
                ti.uId = (UINT_PTR)hPropToggleBtn;
                ti.lpszText = (LPWSTR)L"Hide properties panel";
                SendMessage(hPropToggleTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }
        }
        break;
    case WM_SIZE:
        LayoutControls(hWnd);
        break;
    case WM_PAINT:
        {
            // Draw the splitter bar between list view and properties panel
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            if (hPropertiesPanel && IsWindowVisible(hPropertiesPanel) &&
                splitterRect.right > splitterRect.left)
            {
                const ColorScheme& cs = CurrentScheme();
                HBRUSH hSplitBg = CreateSolidBrush(cs.windowBg);
                FillRect(hdc, &splitterRect, hSplitBg);
                DeleteObject(hSplitBg);

                // Draw a 1px separator line in the center of the splitter
                int cx = (splitterRect.left + splitterRect.right) / 2;
                HPEN hSepPen = CreatePen(PS_SOLID, 1, cs.buttonBorder);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hSepPen);
                MoveToEx(hdc, cx, splitterRect.top, NULL);
                LineTo(hdc, cx, splitterRect.bottom);
                SelectObject(hdc, hOldPen);
                DeleteObject(hSepPen);
            }
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_SETCURSOR:
        {
            // Show horizontal resize cursor when hovering over the splitter
            if ((HWND)wParam == hWnd && LOWORD(lParam) == HTCLIENT &&
                hPropertiesPanel && IsWindowVisible(hPropertiesPanel))
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hWnd, &pt);
                if (pt.x >= splitterRect.left && pt.x < splitterRect.right &&
                    pt.y >= splitterRect.top && pt.y < splitterRect.bottom)
                {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return TRUE;
                }
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    case WM_LBUTTONDOWN:
        {
            int xPos = (short)LOWORD(lParam);
            int yPos = (short)HIWORD(lParam);
            if (hPropertiesPanel && IsWindowVisible(hPropertiesPanel) &&
                xPos >= splitterRect.left && xPos < splitterRect.right &&
                yPos >= splitterRect.top && yPos < splitterRect.bottom)
            {
                bSplitterDragging = true;
                splitterDragStartX = xPos;
                splitterDragStartW = propertiesPanelW;
                SetCapture(hWnd);
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (bSplitterDragging)
        {
            int xPos = (short)LOWORD(lParam);
            int delta = xPos - splitterDragStartX;
            int newW = splitterDragStartW - delta;

            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            RECT rcTab;
            if (hTabControl)
            {
                GetClientRect(hTabControl, &rcTab);
                TabCtrl_AdjustRect(hTabControl, FALSE, &rcTab);
            }
            else
            {
                rcTab.left = 0;
                rcTab.right = rcClient.right;
            }
            int contentW = rcTab.right - rcTab.left;
            int maxPropW = contentW - MIN_LIST_WIDTH - SPLITTER_WIDTH;
            if (newW < MIN_PROP_WIDTH) newW = MIN_PROP_WIDTH;
            if (newW > maxPropW) newW = maxPropW;

            if (newW != propertiesPanelW)
            {
                propertiesPanelW = newW;
                LayoutControls(hWnd);
                // Repaint the splitter area
                InvalidateRect(hWnd, &splitterRect, FALSE);
            }
        }
        break;
    case WM_LBUTTONUP:
        if (bSplitterDragging)
        {
            bSplitterDragging = false;
            ReleaseCapture();
        }
        break;
    case WM_NOTIFY:
        {
            NMHDR* pnmh = (NMHDR*)lParam;
            if (pnmh->idFrom == 1001 && pnmh->code == TVN_ITEMEXPANDING)
            {
                NMTREEVIEW* pnmtv = (NMTREEVIEW*)lParam;
                if (pnmtv->action == TVE_EXPAND)
                {
                    HTREEITEM hItem = pnmtv->itemNew.hItem;
                    ULONGLONG fileRef = (ULONGLONG)pnmtv->itemNew.lParam;

                    HTREEITEM hChild = TreeView_GetChild(hTreeView, hItem);
                    if (hChild != NULL)
                    {
                        break;
                    }

                    PopulateChildren(fileRef, hItem);
                }
            }
            else if (pnmh->idFrom == IDC_TAB_CONTROL && pnmh->code == TCN_SELCHANGE)
            {
                int sel = TabCtrl_GetCurSel(hTabControl);
                ShowTab(sel);
            }
            else if (pnmh->idFrom == IDC_LISTVIEW && pnmh->code == NM_DBLCLK)
            {
                NMITEMACTIVATE* pnmia = (NMITEMACTIVATE*)lParam;
                if (pnmia->iItem >= 0)
                {
                    LaunchSearchResult(pnmia->iItem);
                }
            }
            else if (pnmh->idFrom == IDC_LISTVIEW && pnmh->code == LVN_ITEMCHANGED)
            {
                NMLISTVIEW* pnmlv = (NMLISTVIEW*)lParam;
                if ((pnmlv->uChanged & LVIF_STATE) &&
                    (pnmlv->uNewState & LVIS_SELECTED) &&
                    !(pnmlv->uOldState & LVIS_SELECTED))
                {
                    UpdatePropertiesPanel(pnmlv->iItem);
                }
            }
            else if (pnmh->idFrom == IDC_LISTVIEW && pnmh->code == NM_RCLICK)
            {
                NMITEMACTIVATE* pnmia = (NMITEMACTIVATE*)lParam;
                if (pnmia->iItem >= 0 && pnmia->iItem < (int)searchResults.size())
                {
                    POINT pt;
                    GetCursorPos(&pt);
                    HMENU hMenu = CreatePopupMenu();
                    if (hMenu)
                    {
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_OPEN, L"Open");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_COPY_PATH, L"Copy Path");
                        AppendMenuW(hMenu, MF_STRING, IDM_CTX_OPEN_FOLDER, L"Open Containing Folder");
                        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
                        DestroyMenu(hMenu);
                    }
                }
            }
            else if (pnmh->idFrom == IDC_LISTVIEW && pnmh->code == LVN_GETINFOTIP)
            {
                NMLVGETINFOTIPW* pTip = (NMLVGETINFOTIPW*)lParam;
                int idx = pTip->iItem;
                int sub = pTip->iSubItem;
                if (idx >= 0 && idx < (int)searchResults.size())
                {
                    const SearchResult& sr = searchResults[idx];
                    const wchar_t* tipText = NULL;
                    if (sub == 7 && !sr.contentSnippet.empty())
                        tipText = sr.contentSnippet.c_str();
                    else if (sub == 2 && !sr.fullPath.empty())
                        tipText = sr.fullPath.c_str();
                    else if (sub == 0 && !sr.fileName.empty())
                        tipText = sr.fileName.c_str();
                    if (tipText)
                        wcsncpy_s(pTip->pszText, pTip->cchTextMax, tipText, _TRUNCATE);
                }
            }
            else if (pnmh->idFrom == IDC_LISTVIEW && pnmh->code == NM_CUSTOMDRAW
                     && pnmh->hwndFrom == hListView)
            {
                NMLVCUSTOMDRAW* pcd = (NMLVCUSTOMDRAW*)lParam;
                switch (pcd->nmcd.dwDrawStage)
                {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                    return CDRF_NOTIFYSUBITEMDRAW;
                case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
                    {
                        int iItem = (int)pcd->nmcd.dwItemSpec;
                        int iSubItem = pcd->iSubItem;

                        if (currentSearchKeyword.empty() ||
                            iItem < 0 || iItem >= (int)searchResults.size())
                        {
                            return CDRF_DODEFAULT;
                        }

                        // Custom-draw the Name column (subitem 0) and Snippet column (subitem 7)
                        if (iSubItem != 0 && iSubItem != 7)
                            return CDRF_DODEFAULT;

                        const SearchResult& sr = searchResults[iItem];

                        // For wildcard searches, skip substring highlighting
                        if (bWildcardSearch)
                            return CDRF_DODEFAULT;

                        // Choose the text to highlight in
                        std::wstring cellText;
                        if (iSubItem == 0)
                            cellText = sr.fileName;
                        else
                            cellText = sr.contentSnippet;

                        if (cellText.empty())
                            return CDRF_DODEFAULT;

                        std::wstring lowerText = cellText;
                        for (auto& ch : lowerText)
                            ch = towlower(ch);

                        size_t matchPos = lowerText.find(currentSearchKeyword);
                        if (matchPos == std::wstring::npos)
                            return CDRF_DODEFAULT;

                        // We'll draw the entire cell ourselves
                        HDC hdc = pcd->nmcd.hdc;
                        RECT rcItem;
                        if (iSubItem == 0)
                        {
                            ListView_GetSubItemRect(hListView, iItem, 0, LVIR_BOUNDS, &rcItem);
                            // Adjust for column 0 which includes the whole row in report mode
                            RECT rcCol0;
                            Header_GetItemRect(ListView_GetHeader(hListView), 0, &rcCol0);
                            rcItem.right = rcItem.left + (rcCol0.right - rcCol0.left);
                        }
                        else
                        {
                            ListView_GetSubItemRect(hListView, iItem, iSubItem, LVIR_BOUNDS, &rcItem);
                        }

                        // Fill background (selected or normal)
                        const ColorScheme& cs = CurrentScheme();
                        bool isSelected = (ListView_GetItemState(hListView, iItem, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                        COLORREF bgColor = isSelected ? cs.highlightBg : cs.listBg;
                        COLORREF textColor = isSelected ? cs.highlightText : cs.listText;
                        HBRUSH hBgBrush = CreateSolidBrush(bgColor);
                        FillRect(hdc, &rcItem, hBgBrush);
                        DeleteObject(hBgBrush);

                        // Small left padding
                        rcItem.left += 4;

                        size_t matchLen = currentSearchKeyword.length();

                        SetBkMode(hdc, TRANSPARENT);
                        int x = rcItem.left;
                        int rightEdge = rcItem.right;

                        // Walk through the text, highlighting all occurrences
                        size_t pos = 0;
                        while (pos < cellText.length() && x < rightEdge)
                        {
                            size_t found = lowerText.find(currentSearchKeyword, pos);
                            if (found == std::wstring::npos)
                                found = cellText.length(); // no more matches

                            // Draw text before this match (or remaining text)
                            if (found > pos)
                            {
                                std::wstring seg = cellText.substr(pos, found - pos);
                                SetTextColor(hdc, textColor);
                                SIZE sz;
                                GetTextExtentPoint32W(hdc, seg.c_str(), (int)seg.length(), &sz);
                                RECT rc = {x, rcItem.top, min((long)(x + sz.cx), (long)rightEdge), rcItem.bottom};
                                DrawTextW(hdc, seg.c_str(), (int)seg.length(), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                                x += sz.cx;
                            }

                            // Draw highlighted match
                            if (found < cellText.length() && x < rightEdge)
                            {
                                std::wstring seg = cellText.substr(found, matchLen);
                                SIZE sz;
                                GetTextExtentPoint32W(hdc, seg.c_str(), (int)seg.length(), &sz);
                                RECT rcHighlight = {x, rcItem.top + 1, min((long)(x + sz.cx), (long)rightEdge), rcItem.bottom - 1};
                                HBRUSH hHighlight = CreateSolidBrush(cs.matchHighlight);
                                FillRect(hdc, &rcHighlight, hHighlight);
                                DeleteObject(hHighlight);
                                SetTextColor(hdc, RGB(0, 0, 0));
                                RECT rc = {x, rcItem.top, min((long)(x + sz.cx), (long)rightEdge), rcItem.bottom};
                                DrawTextW(hdc, seg.c_str(), (int)seg.length(), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                                x += sz.cx;
                                pos = found + matchLen;
                            }
                            else
                            {
                                break;
                            }
                        }

                        return CDRF_SKIPDEFAULT;
                    }
                }
            }
            // Custom-draw the ListView header for prominent column headers
            else if (hListView && pnmh->hwndFrom == ListView_GetHeader(hListView)
                     && pnmh->code == NM_CUSTOMDRAW)
            {
                NMCUSTOMDRAW* pcd = (NMCUSTOMDRAW*)lParam;
                switch (pcd->dwDrawStage)
                {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                    {
                        const ColorScheme& cs = CurrentScheme();
                        HDC hdc = pcd->hdc;
                        RECT rc = pcd->rc;

                        HBRUSH hHdrBrush = CreateSolidBrush(cs.headerBg);
                        FillRect(hdc, &rc, hHdrBrush);
                        DeleteObject(hHdrBrush);

                        // Draw a bottom border line
                        HPEN hBorderPen = CreatePen(PS_SOLID, 1, cs.headerBorder);
                        HPEN hOldPen2 = (HPEN)SelectObject(hdc, hBorderPen);
                        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
                        LineTo(hdc, rc.right, rc.bottom - 1);
                        SelectObject(hdc, hOldPen2);
                        DeleteObject(hBorderPen);

                        // Get the header item text
                        HWND hHeader = pnmh->hwndFrom;
                        int iItem = (int)pcd->dwItemSpec;
                        wchar_t hdrText[128] = {0};
                        HDITEMW hdi = {0};
                        hdi.mask = HDI_TEXT;
                        hdi.pszText = hdrText;
                        hdi.cchTextMax = 128;
                        Header_GetItem(hHeader, iItem, &hdi);

                        // Draw text with bold font
                        SetBkMode(hdc, TRANSPARENT);
                        SetTextColor(hdc, cs.headerText);
                        if (hHeaderFont)
                            SelectObject(hdc, hHeaderFont);
                        rc.left += 6;
                        DrawTextW(hdc, hdrText, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                        return CDRF_SKIPDEFAULT;
                    }
                }
            }
        }
        break;
    case WM_CTLCOLORSTATIC:
        {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtl = (HWND)lParam;
            if (hCtl == hStatusText || hCtl == hSummaryText)
            {
                const ColorScheme& cs = CurrentScheme();
                SetBkColor(hdcStatic, cs.statusBg);
                SetTextColor(hdcStatic, cs.statusText);
                return (LRESULT)hStatusBrush;
            }
            if (hCtl == hPropertiesPanel)
            {
                const ColorScheme& cs = CurrentScheme();
                SetBkColor(hdcStatic, cs.editBg);
                SetTextColor(hdcStatic, cs.windowText);
                if (!hPropertiesBrush)
                    hPropertiesBrush = CreateSolidBrush(cs.editBg);
                return (LRESULT)hPropertiesBrush;
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    case WM_CTLCOLOREDIT:
        {
            HDC hdcEdit = (HDC)wParam;
            HWND hCtl = (HWND)lParam;
            if (hCtl == hSearchEdit)
            {
                const ColorScheme& cs = CurrentScheme();
                SetBkColor(hdcEdit, cs.editBg);
                SetTextColor(hdcEdit, cs.editText);
                return (LRESULT)hEditBrush;
            }
            if (hCtl == hPropertiesPanel)
            {
                const ColorScheme& cs = CurrentScheme();
                SetBkColor(hdcEdit, cs.editBg);
                SetTextColor(hdcEdit, cs.windowText);
                if (!hPropertiesBrush)
                    hPropertiesBrush = CreateSolidBrush(cs.editBg);
                return (LRESULT)hPropertiesBrush;
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            const ColorScheme& cs = CurrentScheme();

            RECT rcToolbar = rc;
            rcToolbar.bottom = TOOLBAR_ROW_HEIGHT;
            FillRect(hdc, &rcToolbar, hToolbarBrush ? hToolbarBrush : (HBRUSH)(COLOR_BTNFACE + 1));

            RECT rcRest = rc;
            rcRest.top = TOOLBAR_ROW_HEIGHT;
            HBRUSH hRestBrush = CreateSolidBrush(cs.windowBg);
            FillRect(hdc, &rcRest, hRestBrush);
            DeleteObject(hRestBrush);

            return 1;
        }
    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT* pDIS = (DRAWITEMSTRUCT*)lParam;
            if (pDIS->CtlType == ODT_BUTTON)
            {
                const ColorScheme& cs = CurrentScheme();
                bool isDisabled = (pDIS->itemState & ODS_DISABLED) != 0;
                bool isPressed = (pDIS->itemState & ODS_SELECTED) != 0;
                bool isToggle = (pDIS->hwndItem == hPropToggleBtn);

                // Detect hover for the toggle button
                bool isHovered = false;
                if (isToggle && !isDisabled)
                {
                    POINT ptCur;
                    GetCursorPos(&ptCur);
                    ScreenToClient(pDIS->hwndItem, &ptCur);
                    RECT rcBtn = pDIS->rcItem;
                    isHovered = (PtInRect(&rcBtn, ptCur) != 0);

                    // Start tracking mouse leave so we repaint when the cursor exits
                    if (isHovered && !bToggleHovered)
                    {
                        TRACKMOUSEEVENT tme = {0};
                        tme.cbSize = sizeof(tme);
                        tme.dwFlags = TME_LEAVE;
                        tme.hwndTrack = pDIS->hwndItem;
                        TrackMouseEvent(&tme);
                    }
                    bToggleHovered = isHovered;
                }

                COLORREF bgClr = isDisabled ? cs.buttonDisabledBg : cs.buttonBg;
                COLORREF txtClr = isDisabled ? cs.buttonDisabledText : cs.buttonText;
                COLORREF brdClr = cs.buttonBorder;

                if (isHovered && !isPressed && !isDisabled)
                {
                    // Lighten / brighten background on hover
                    bgClr = RGB(min(255, GetRValue(bgClr) + 20),
                                min(255, GetGValue(bgClr) + 20),
                                min(255, GetBValue(bgClr) + 20));
                    brdClr = RGB(min(255, GetRValue(brdClr) + 30),
                                min(255, GetGValue(brdClr) + 30),
                                min(255, GetBValue(brdClr) + 30));
                }

                if (isPressed && !isDisabled)
                {
                    bgClr = RGB(GetRValue(bgClr) * 85 / 100,
                                GetGValue(bgClr) * 85 / 100,
                                GetBValue(bgClr) * 85 / 100);
                }

                HDC hdc = pDIS->hDC;
                RECT rc = pDIS->rcItem;
                int radius = 6;

                HBRUSH hBg = CreateSolidBrush(bgClr);
                HPEN hPen = CreatePen(PS_SOLID, 1, brdClr);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBg);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
                SelectObject(hdc, hOldBrush);
                SelectObject(hdc, hOldPen);
                DeleteObject(hBg);
                DeleteObject(hPen);

                wchar_t text[128] = {0};
                GetWindowTextW(pDIS->hwndItem, text, 128);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, txtClr);
                // Use the larger toggle font for the toggle button, normal font for others
                if (isToggle && hToggleFont)
                    SelectObject(hdc, hToggleFont);
                else if (hUIFont)
                    SelectObject(hdc, hUIFont);
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                if (pDIS->itemState & ODS_FOCUS)
                {
                    RECT rcFocus = {rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3};
                    DrawFocusRect(hdc, &rcFocus);
                }

                return TRUE;
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            
            if (wmId == IDC_FIND_BUTTON)
            {
                KillTimer(hWnd, IDT_SEARCH_DEBOUNCE);
                TriggerSearch(hWnd);
                break;
            }

            if (wmId == IDC_WINSEARCH_BUTTON)
            {
                KillTimer(hWnd, IDT_SEARCH_DEBOUNCE);
                TriggerWindowsSearch(hWnd);
                break;
            }

            // As-you-type: restart debounce timer on edit change
            if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == hSearchEdit)
            {
                if (bTreeRenderedOnce && !fileMap.empty())
                {
                    KillTimer(hWnd, IDT_SEARCH_DEBOUNCE);
                    SetTimer(hWnd, IDT_SEARCH_DEBOUNCE, SEARCH_DEBOUNCE_MS, NULL);
                }
                break;
            }
            
            if (wmId == IDM_CTX_OPEN)
            {
                int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                if (sel >= 0)
                    LaunchSearchResult(sel);
                break;
            }

            if (wmId == IDM_CTX_COPY_PATH)
            {
                int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)searchResults.size())
                {
                    const std::wstring& path = searchResults[sel].fullPath;
                    if (OpenClipboard(hWnd))
                    {
                        EmptyClipboard();
                        size_t bytes = (path.length() + 1) * sizeof(wchar_t);
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                        if (hMem)
                        {
                            wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                            if (pMem)
                            {
                                memcpy(pMem, path.c_str(), bytes);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                        }
                        CloseClipboard();
                    }
                }
                break;
            }

            if (wmId == IDM_CTX_OPEN_FOLDER)
            {
                int sel = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
                if (sel >= 0 && sel < (int)searchResults.size())
                {
                    const SearchResult& sr = searchResults[sel];
                    if (sr.isDirectory)
                    {
                        ShellExecuteW(NULL, L"explore", sr.fullPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                    else
                    {
                        std::wstring folder = sr.fullPath;
                        size_t lastSlash = folder.find_last_of(L'\\');
                        if (lastSlash != std::wstring::npos)
                            folder = folder.substr(0, lastSlash);
                        ShellExecuteW(NULL, L"explore", folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                }
                break;
            }

            if (wmId == IDC_PROP_TOGGLE)
            {
                bPropertiesCollapsed = !bPropertiesCollapsed;
                SetWindowTextW(hPropToggleBtn, bPropertiesCollapsed ? L"\u00AB" : L"\u00BB");
                UpdateToggleTooltip();
                LayoutControls(hWnd);
                InvalidateRect(hWnd, NULL, TRUE);
                break;
            }

            if (wmId == IDC_DARKMODE_BTN)
            {
                bDarkMode = !bDarkMode;
                ApplyTheme(hWnd);
                break;
            }

            if (wmId == IDC_CLOUD_BTN)
            {
                DialogBoxW(hInst, MAKEINTRESOURCE(IDD_CLOUD_CONFIG), hWnd, CloudConfigDlgProc);
                break;
            }

            if (wmId >= IDC_DRIVE_BUTTON_BASE && wmId < IDC_DRIVE_BUTTON_BASE + (int)driveButtons.size())
            {
                int driveIdx = wmId - IDC_DRIVE_BUTTON_BASE;
                ReadDriveAndBuildTree(hWnd, driveButtons[driveIdx].driveLetter);
                break;
            }
            
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_TIMER:
        if (wParam == IDT_SEARCH_DEBOUNCE)
        {
            KillTimer(hWnd, IDT_SEARCH_DEBOUNCE);
            TriggerSearch(hWnd);
        }
        break;
    case WM_USN_UPDATED:
        ProcessPendingUsnChanges();
        break;
    case WM_MOUSELEAVE:
        // Forwarded from the toggle button's TrackMouseEvent; repaint to clear hover
        if (bToggleHovered)
        {
            bToggleHovered = false;
            if (hPropToggleBtn)
                InvalidateRect(hPropToggleBtn, NULL, TRUE);
        }
        break;
    case WM_DESTROY:
        CancelOAuthLogin();
        StopUsnMonitor();
        if (hPropToggleTooltip) { DestroyWindow(hPropToggleTooltip); hPropToggleTooltip = NULL; }
        if (hToggleFont) { DeleteObject(hToggleFont); hToggleFont = NULL; }
        if (hHeaderFont) { DeleteObject(hHeaderFont); hHeaderFont = NULL; }
        if (hUIFont) { DeleteObject(hUIFont); hUIFont = NULL; }
        if (hToolbarBrush) { DeleteObject(hToolbarBrush); hToolbarBrush = NULL; }
        if (hStatusBrush) { DeleteObject(hStatusBrush); hStatusBrush = NULL; }
        if (hEditBrush) { DeleteObject(hEditBrush); hEditBrush = NULL; }
        if (hPropertiesBrush) { DeleteObject(hPropertiesBrush); hPropertiesBrush = NULL; }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
