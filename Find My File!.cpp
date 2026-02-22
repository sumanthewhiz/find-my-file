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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")

// Enable visual styles (Common Controls v6)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"" )

#define MAX_LOADSTRING 100
#define IDC_DRIVE_BUTTON_BASE 2000
#define IDC_SEARCH_EDIT     3000
#define IDC_FIND_BUTTON     3001
#define IDC_TAB_CONTROL     3002
#define IDC_LISTVIEW        3003
#define IDM_CTX_OPEN        4000
#define IDM_CTX_COPY_PATH   4001
#define IDM_CTX_OPEN_FOLDER 4002
#define IDC_DARKMODE_BTN    5000

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
std::wstring currentDriveLetter;
std::wstring currentSearchKeyword;
std::wstring currentSearchScope;
HFONT hUIFont = NULL;
HBRUSH hToolbarBrush = NULL;
HBRUSH hStatusBrush = NULL;
HBRUSH hEditBrush = NULL;
HWND hDarkModeBtn = NULL;
bool bDarkMode = false;

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
};

static const ColorScheme& CurrentScheme() { return bDarkMode ? DarkScheme : LightScheme; }

// Structure to store file information
struct FileEntry {
    ULONGLONG fileReference;
    ULONGLONG parentReference;
    std::wstring fileName;
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
// Cached full paths for each file reference
std::unordered_map<ULONGLONG, std::wstring> fullPathCache;
// Cached timestamps (USN timestamp) for each file reference
std::unordered_map<ULONGLONG, LARGE_INTEGER> timestampCache;

// Search result for display
struct SearchResult {
    ULONGLONG fileRef;
    std::wstring fileName;
    std::wstring fullPath;
    bool isDirectory;
    int matchScore;  // higher = better match
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

    InvalidateRect(hWnd, NULL, TRUE);
    HWND hChild = GetWindow(hWnd, GW_CHILD);
    while (hChild)
    {
        InvalidateRect(hChild, NULL, TRUE);
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
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

        // Index the full filename as a lowercased key
        std::wstring lowerName = entry.fileName;
        for (auto& ch : lowerName)
            ch = towlower(ch);

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
}

// Compute a recency bonus (0-10) based on USN timestamp age
static int GetRecencyBonus(ULONGLONG fileRef)
{
    auto tsIt = timestampCache.find(fileRef);
    if (tsIt == timestampCache.end())
        return 0;

    // Get current time as FILETIME (100-nanosecond intervals since 1601)
    FILETIME ftNow;
    GetSystemTimeAsFileTime(&ftNow);
    ULONGLONG now = ((ULONGLONG)ftNow.dwHighDateTime << 32) | ftNow.dwLowDateTime;
    ULONGLONG fileTime = (ULONGLONG)tsIt->second.QuadPart;

    if (fileTime == 0 || fileTime > now)
        return 0;

    // Time difference in 100-ns units
    ULONGLONG diff = now - fileTime;
    const ULONGLONG TICKS_PER_DAY = 864000000000ULL; // 10^7 * 86400

    ULONGLONG daysOld = diff / TICKS_PER_DAY;

    if (daysOld <= 7)   return 10;  // Modified within last week
    if (daysOld <= 30)  return 7;   // Modified within last month
    if (daysOld <= 365) return 3;   // Modified within last year
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

// Perform a search and populate searchResults sorted by match quality.
// If scopeRef != 0, only include results that are descendants of that directory.
void PerformSearch(const std::wstring& keyword, ULONGLONG scopeRef)
{
    searchResults.clear();

    if (keyword.empty() || fileMap.empty())
        return;

    std::wstring lowerKeyword = keyword;
    for (auto& ch : lowerKeyword)
        ch = towlower(ch);

    // Collect matching file refs with base scores
    std::unordered_map<ULONGLONG, int> matchScores;

    // 1) Exact full-name match (highest base score = 100)
    auto exactIt = invertedIndex.find(lowerKeyword);
    if (exactIt != invertedIndex.end())
    {
        for (ULONGLONG ref : exactIt->second)
        {
            matchScores[ref] = max(matchScores[ref], 100);
        }
    }

    // 2) Scan inverted index for prefix matches (score 75) and substring matches (score 50)
    for (auto& pair : invertedIndex)
    {
        const std::wstring& token = pair.first;

        if (token == lowerKeyword)
            continue;

        bool isPrefix = (token.length() >= lowerKeyword.length() &&
                         token.compare(0, lowerKeyword.length(), lowerKeyword) == 0);
        bool isSubstring = (!isPrefix && token.find(lowerKeyword) != std::wstring::npos);

        if (isPrefix || isSubstring)
        {
            int score = isPrefix ? 75 : 50;
            for (ULONGLONG ref : pair.second)
            {
                matchScores[ref] = max(matchScores[ref], score);
            }
        }
    }

    // 3) Also scan all filenames for substring match in the full name (score 40)
    for (auto& pair : fileMap)
    {
        if (matchScores.count(pair.first))
            continue;

        std::wstring lowerName = pair.second.fileName;
        for (auto& ch : lowerName)
            ch = towlower(ch);

        if (lowerName.find(lowerKeyword) != std::wstring::npos)
        {
            matchScores[pair.first] = 40;
        }
    }

    // 4) Stemmed matching: stem the keyword and look up in stemmedIndex (score 30)
    std::wstring stemmedKeyword = StemWord(lowerKeyword);
    if (!stemmedKeyword.empty())
    {
        // Exact stem match
        auto stemExact = stemmedIndex.find(stemmedKeyword);
        if (stemExact != stemmedIndex.end())
        {
            for (ULONGLONG ref : stemExact->second)
            {
                if (!matchScores.count(ref))
                    matchScores[ref] = 30;
            }
        }
        // Also check if the stemmed keyword matches unstemmed index entries
        // (e.g. query "walking" stems to "walk", which matches literal token "walk")
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
            // Prefix match on stemmed index
            for (auto& pair : stemmedIndex)
            {
                const std::wstring& stemToken = pair.first;
                if (stemToken == stemmedKeyword)
                    continue;
                if (stemToken.length() >= stemmedKeyword.length() &&
                    stemToken.compare(0, stemmedKeyword.length(), stemmedKeyword) == 0)
                {
                    for (ULONGLONG ref : pair.second)
                    {
                        if (!matchScores.count(ref))
                            matchScores[ref] = 25;
                    }
                }
            }
        }
    }

    // Build results with bonus scoring
    searchResults.reserve(matchScores.size());
    for (auto& pair : matchScores)
    {
        ULONGLONG ref = pair.first;
        auto it = fileMap.find(ref);
        if (it == fileMap.end())
            continue;

        // Filter by scope: skip files not under the scoped directory
        if (scopeRef != 0 && ref != scopeRef && !IsDescendantOf(ref, scopeRef))
            continue;

        int score = pair.second;

        // Bonus: exact case-sensitive match of the keyword in the filename (+15)
        if (HasExactCaseMatch(it->second.fileName, keyword))
            score += 15;

        // Bonus: file recency based on USN timestamp (+10/+7/+3)
        score += GetRecencyBonus(ref);

        SearchResult sr;
        sr.fileRef = ref;
        sr.fileName = it->second.fileName;
        sr.fullPath = BuildFullPath(ref, currentDriveLetter);
        sr.isDirectory = it->second.isDirectory;
        sr.matchScore = score;
        searchResults.push_back(sr);
    }

    // Sort: highest score first, then alphabetical
    std::sort(searchResults.begin(), searchResults.end(),
        [](const SearchResult& a, const SearchResult& b) {
            if (a.matchScore != b.matchScore)
                return a.matchScore > b.matchScore;
            return _wcsicmp(a.fileName.c_str(), b.fileName.c_str()) < 0;
        });

    // Cap results for performance
    const size_t MAX_RESULTS = 10000;
    if (searchResults.size() > MAX_RESULTS)
        searchResults.resize(MAX_RESULTS);
}

// Format a USN timestamp (FILETIME-based LARGE_INTEGER) into a date/time string
static std::wstring FormatTimestamp(ULONGLONG fileRef)
{
    auto tsIt = timestampCache.find(fileRef);
    if (tsIt == timestampCache.end() || tsIt->second.QuadPart == 0)
        return L"";

    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(tsIt->second.QuadPart & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(tsIt->second.QuadPart >> 32);

    FILETIME ftLocal;
    FileTimeToLocalFileTime(&ft, &ftLocal);

    SYSTEMTIME st;
    FileTimeToSystemTime(&ftLocal, &st);

    wchar_t buf[64];
    swprintf_s(buf, 64, L"%04d-%02d-%02d %02d:%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return std::wstring(buf);
}

// Populate the ListView with search results
void PopulateSearchResults()
{
    if (!hListView)
        return;

    SendMessage(hListView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hListView);

    for (size_t i = 0; i < searchResults.size(); i++)
    {
        const SearchResult& sr = searchResults[i];

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = (int)i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)sr.fileName.c_str();
        lvi.lParam = (LPARAM)i;
        ListView_InsertItem(hListView, &lvi);

        // Type column
        ListView_SetItemText(hListView, (int)i, 1,
            (LPWSTR)(sr.isDirectory ? L"Folder" : L"File"));

        // Full path column
        ListView_SetItemText(hListView, (int)i, 2, (LPWSTR)sr.fullPath.c_str());

        // Modified date column
        std::wstring modDate = FormatTimestamp(sr.fileRef);
        ListView_SetItemText(hListView, (int)i, 3, (LPWSTR)modDate.c_str());

        // Match quality column
        // Base scores: Exact=100, Prefix=75, Token=50, Substring=40, Stem=30/25
        // Bonuses: case match +15, recency +10/+7/+3
        const wchar_t* quality = L"Stem";
        if (sr.matchScore >= 100) quality = L"Exact";
        else if (sr.matchScore >= 75) quality = L"Prefix";
        else if (sr.matchScore >= 50) quality = L"Token Match";
        else if (sr.matchScore >= 40) quality = L"Substring";
        ListView_SetItemText(hListView, (int)i, 4, (LPWSTR)quality);
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
    }
    else
    {
        ShowWindow(hTreeView, SW_HIDE);
        ShowWindow(hListView, SW_SHOW);
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

        // Clear previous data
        fileMap.clear();
        childrenMap.clear();
        treeItemToRef.clear();
        invertedIndex.clear();
        stemmedIndex.clear();
        fullPathCache.clear();
        timestampCache.clear();
        searchResults.clear();

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
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
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
    int darkBtnWidth = 75;
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
    
    if (hTreeView)
    {
        SetWindowPos(hTreeView, NULL, contentX, contentY,
            contentW, contentH, SWP_NOZORDER);
    }
    if (hListView)
    {
        SetWindowPos(hListView, NULL, contentX, contentY,
            contentW, contentH, SWP_NOZORDER);
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
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                0, 5, 200, 25, hWnd, (HMENU)IDC_SEARCH_EDIT, hInst, NULL);

            hFindButton = CreateWindowExW(
                0, L"BUTTON", L"Find",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 5, 60, 25, hWnd, (HMENU)IDC_FIND_BUTTON, hInst, NULL);

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
                WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 110, 0, 0, hWnd, (HMENU)IDC_LISTVIEW, hInst, NULL);

            if (hListView)
            {
                ListView_SetExtendedListViewStyle(hListView,
                    LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

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
            }

            // Initially show Browse tab, hide Search Results
            ShowTab(0);

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
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 70, 30, hWnd, (HMENU)IDC_DARKMODE_BTN, hInst, NULL);
            if (hUIFont && hDarkModeBtn)
                SendMessage(hDarkModeBtn, WM_SETFONT, (WPARAM)hUIFont, TRUE);
        }
        break;
    case WM_SIZE:
        LayoutControls(hWnd);
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
            else if (pnmh->idFrom == IDC_LISTVIEW && pnmh->code == NM_CUSTOMDRAW)
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

                        // Only custom-draw the Name column (subitem 0)
                        if (iSubItem != 0 || currentSearchKeyword.empty() ||
                            iItem < 0 || iItem >= (int)searchResults.size())
                        {
                            return CDRF_DODEFAULT;
                        }

                        const SearchResult& sr = searchResults[iItem];
                        std::wstring lowerName = sr.fileName;
                        for (auto& ch : lowerName)
                            ch = towlower(ch);

                        size_t matchPos = lowerName.find(currentSearchKeyword);
                        if (matchPos == std::wstring::npos)
                        {
                            return CDRF_DODEFAULT;
                        }

                        // We'll draw the entire cell ourselves
                        HDC hdc = pcd->nmcd.hdc;
                        RECT rcItem;
                        ListView_GetSubItemRect(hListView, iItem, 0, LVIR_BOUNDS, &rcItem);

                        // Adjust for column 0 which includes the whole row in report mode
                        RECT rcCol0;
                        Header_GetItemRect(ListView_GetHeader(hListView), 0, &rcCol0);
                        rcItem.right = rcItem.left + (rcCol0.right - rcCol0.left);

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

                        const std::wstring& name = sr.fileName;
                        size_t matchLen = currentSearchKeyword.length();

                        // Split into: before | match | after
                        std::wstring before = name.substr(0, matchPos);
                        std::wstring match = name.substr(matchPos, matchLen);
                        std::wstring after = name.substr(matchPos + matchLen);

                        SetBkMode(hdc, TRANSPARENT);
                        int x = rcItem.left;

                        // Draw text before match
                        if (!before.empty())
                        {
                            SetTextColor(hdc, textColor);
                            SIZE sz;
                            GetTextExtentPoint32W(hdc, before.c_str(), (int)before.length(), &sz);
                            RECT rc = {x, rcItem.top, x + sz.cx, rcItem.bottom};
                            DrawTextW(hdc, before.c_str(), (int)before.length(), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            x += sz.cx;
                        }

                        // Draw matched portion with highlight background
                        if (!match.empty())
                        {
                            SIZE sz;
                            GetTextExtentPoint32W(hdc, match.c_str(), (int)match.length(), &sz);
                            RECT rcHighlight = {x, rcItem.top + 1, x + sz.cx, rcItem.bottom - 1};
                            HBRUSH hHighlight = CreateSolidBrush(cs.matchHighlight);
                            FillRect(hdc, &rcHighlight, hHighlight);
                            DeleteObject(hHighlight);
                            SetTextColor(hdc, RGB(0, 0, 0));
                            RECT rc = {x, rcItem.top, x + sz.cx, rcItem.bottom};
                            DrawTextW(hdc, match.c_str(), (int)match.length(), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                            x += sz.cx;
                        }

                        // Draw text after match
                        if (!after.empty())
                        {
                            SetTextColor(hdc, textColor);
                            RECT rc = {x, rcItem.top, rcItem.right, rcItem.bottom};
                            DrawTextW(hdc, after.c_str(), (int)after.length(), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        }

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
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            
            if (wmId == IDC_FIND_BUTTON)
            {
                wchar_t searchText[512] = {0};
                GetWindowTextW(hSearchEdit, searchText, 512);
                std::wstring keyword(searchText);
                if (!keyword.empty() && !fileMap.empty())
                {
                    currentSearchKeyword = keyword;
                    for (auto& ch : currentSearchKeyword)
                        ch = towlower(ch);

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
                            // Only scope if it's a directory and not the root (5)
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

                    PerformSearch(keyword, scopeRef);
                    PopulateSearchResults();
                    // Switch to Search Results tab
                    TabCtrl_SetCurSel(hTabControl, 1);
                    ShowTab(1);
                    // Update status with scope and stem info for transparency
                    std::wstring status = L"Found " + std::to_wstring(searchResults.size()) + L" results";
                    if (searchResults.size() >= 10000)
                        status += L" (showing first 10,000)";
                    if (!currentSearchScope.empty())
                        status += L"  |  Scope: " + currentSearchScope;
                    std::wstring stemmed = StemWord(currentSearchKeyword);
                    if (stemmed != currentSearchKeyword)
                        status += L"  |  Stem: \"" + stemmed + L"\"";
                    SetWindowTextW(hStatusText, status.c_str());
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

            if (wmId == IDC_DARKMODE_BTN)
            {
                bDarkMode = !bDarkMode;
                ApplyTheme(hWnd);
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
    case WM_DESTROY:
        if (hUIFont) { DeleteObject(hUIFont); hUIFont = NULL; }
        if (hToolbarBrush) { DeleteObject(hToolbarBrush); hToolbarBrush = NULL; }
        if (hStatusBrush) { DeleteObject(hStatusBrush); hStatusBrush = NULL; }
        if (hEditBrush) { DeleteObject(hEditBrush); hEditBrush = NULL; }
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
