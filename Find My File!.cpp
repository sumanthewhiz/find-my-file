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

#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100
#define IDC_DRIVE_BUTTON_BASE 2000

// Global Variables:
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hTreeView = NULL;
HWND hProgressBar = NULL;
HWND hStatusText = NULL;
HWND hSummaryText = NULL;

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
            
            BuildTreeView(drive);
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
    int spacing = 5;
    int startX = 10;
    
    for (size_t i = 0; i < drives.size(); i++)
    {
        std::wstring label = L"Scan " + drives[i] + L":";
        
        HWND hBtn = CreateWindowExW(
            0, L"BUTTON", label.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            startX + (int)(i * (buttonWidth + spacing)), 5,
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
    
    int buttonRowHeight = 40;
    int statusHeight = 25;
    int progressHeight = 20;
    int summaryHeight = 25;
    int currentY = 0;
    
    int buttonWidth = 80;
    int spacing = 5;
    int startX = 10;
    for (size_t i = 0; i < driveButtons.size(); i++)
    {
        if (driveButtons[i].hButton)
        {
            SetWindowPos(driveButtons[i].hButton, NULL,
                startX + (int)(i * (buttonWidth + spacing)), 5,
                buttonWidth, 30, SWP_NOZORDER);
        }
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
    
    if (hTreeView)
    {
        SetWindowPos(hTreeView, NULL, 0, currentY,
            rcClient.right, rcClient.bottom - currentY, SWP_NOZORDER);
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
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS;
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
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_FINDMYFILE);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 900, 650, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
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

            hTreeView = CreateWindowExW(
                0, WC_TREEVIEW, L"Tree View",
                WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES |
                TVS_HASBUTTONS | TVS_LINESATROOT,
                0, 110, 0, 0, hWnd, (HMENU)1001, hInst, NULL);
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

                    // Check if children are already loaded by looking for a dummy child
                    HTREEITEM hChild = TreeView_GetChild(hTreeView, hItem);
                    if (hChild != NULL)
                    {
                        // Children already populated - nothing to do
                        break;
                    }

                    // Populate children on demand
                    PopulateChildren(fileRef, hItem);
                }
            }
        }
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            
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
