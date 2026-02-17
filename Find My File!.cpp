// Find My File!.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "Find My File!.h"
#include <winioctl.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <map>
#include <commctrl.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND hTreeView = NULL;                          // TreeView control handle
HWND hProgressBar = NULL;                       // Progress bar control handle
HWND hStatusText = NULL;                        // Status text control handle
HWND hSummaryText = NULL;                       // Summary text control handle

// Debug logging
std::wofstream debugLog;

void LogDebug(const std::wstring& message)
{
    if (!debugLog.is_open())
    {
        debugLog.open(L"C:\\mft_debug.log", std::ios::app);
    }
    if (debugLog.is_open())
    {
        debugLog << message << std::endl;
        debugLog.flush();
    }
}

// Structure to store file information
struct FileEntry {
    ULONGLONG fileReference;
    ULONGLONG parentReference;
    std::wstring fileName;
    bool isDirectory;
    HTREEITEM hTreeItem;
    std::wstring driveLetter;  // To distinguish between drives
};

// Separate maps for each drive
std::map<std::wstring, std::map<ULONGLONG, FileEntry>> driveFileMaps;
std::map<std::wstring, std::map<ULONGLONG, std::vector<ULONGLONG>>> driveChildrenMaps;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
void                ReadMFTAndListFiles(HWND hWnd);
void                BuildTreeView();
HTREEITEM           AddItemToTree(HWND hTreeView, LPWSTR lpszItem, HTREEITEM hParent);
void                UpdateProgress(int percentage, const std::wstring& status);
void                AddChildrenRecursive(const std::wstring& drive, ULONGLONG parentRef, int depth);
bool                ReadDriveMFT(HWND hWnd, const std::wstring& drive, int& totalFiles, int& totalDirs);
std::vector<std::wstring> GetFixedDrives();

// MFT structures
#pragma pack(push, 1)
typedef struct {
    DWORD Type;
    WORD UsaOffset;
    WORD UsaCount;
    ULONGLONG Lsn;
    WORD SequenceNumber;
    WORD LinkCount;
    WORD AttrsOffset;
    WORD Flags;
    DWORD BytesInUse;
    DWORD BytesAllocated;
    ULONGLONG BaseFileRecord;
    WORD NextAttrId;
} FILE_RECORD_HEADER;

typedef struct {
    DWORD Type;
    DWORD Length;
    BYTE NonResident;
    BYTE NameLength;
    WORD NameOffset;
    WORD Flags;
    WORD AttributeId;
} ATTRIBUTE_HEADER;

typedef struct {
    ATTRIBUTE_HEADER Header;
    DWORD ValueLength;
    WORD ValueOffset;
    BYTE Flags;
    BYTE Reserved;
} RESIDENT_ATTRIBUTE;

typedef struct {
    ULONGLONG ParentDirectory;
    ULONGLONG CreationTime;
    ULONGLONG ModificationTime;
    ULONGLONG MftChangeTime;
    ULONGLONG LastAccessTime;
    ULONGLONG AllocatedSize;
    ULONGLONG RealSize;
    DWORD FileAttributes;
    DWORD Reserved;
    BYTE FileNameLength;
    BYTE NameType;
    WCHAR FileName[1];
} FILE_NAME_ATTRIBUTE;
#pragma pack(pop)

std::vector<std::wstring> GetFixedDrives()
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
            
            // Only include fixed drives (local hard drives)
            if (driveType == DRIVE_FIXED)
            {
                // Check if it's NTFS
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
    
    // Process messages to keep UI responsive
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

HTREEITEM AddItemToTree(HWND hTreeView, LPWSTR lpszItem, HTREEITEM hParent)
{
    TVITEM tvi = {0};
    TVINSERTSTRUCT tvins = {0};

    tvi.mask = TVIF_TEXT | TVIF_PARAM;
    tvi.pszText = lpszItem;
    tvi.cchTextMax = (UINT)wcslen(lpszItem);

    tvins.item = tvi;
    tvins.hInsertAfter = TVI_LAST;
    tvins.hParent = hParent;

    return (HTREEITEM)SendMessage(hTreeView, TVM_INSERTITEM, 0, (LPARAM)&tvins);
}

void AddChildrenRecursive(const std::wstring& drive, ULONGLONG parentRef, int depth)
{
    auto& fileMap = driveFileMaps[drive];
    auto& childrenMap = driveChildrenMaps[drive];

    // Check if this parent has children
    if (childrenMap.find(parentRef) == childrenMap.end())
        return;

    // Get parent's tree item
    if (fileMap.find(parentRef) == fileMap.end())
        return;

    FileEntry& parent = fileMap[parentRef];
    if (parent.hTreeItem == NULL)
        return;

    // Add all children
    std::vector<ULONGLONG>& children = childrenMap[parentRef];
    
    // Limit number of direct children to prevent TreeView issues
    size_t maxChildren = min(children.size(), (size_t)10000);
    
    for (size_t i = 0; i < maxChildren; i++)
    {
        ULONGLONG childRef = children[i];
        
        if (fileMap.find(childRef) != fileMap.end())
        {
            FileEntry& child = fileMap[childRef];
            
            // Add to tree if not already added
            if (child.hTreeItem == NULL)
            {
                // Validate filename
                if (child.fileName.empty() || child.fileName.length() > 255)
                    continue;
                
                child.hTreeItem = AddItemToTree(hTreeView, 
                    (LPWSTR)child.fileName.c_str(), parent.hTreeItem);

                // Recursively add this child's children if it's a directory
                if (child.isDirectory && child.hTreeItem != NULL)
                {
                    AddChildrenRecursive(drive, childRef, depth + 1);
                }
            }
        }
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
            return false;
        }

        NTFS_VOLUME_DATA_BUFFER volumeData = {0};
        DWORD bytesReturned = 0;
        
        if (!DeviceIoControl(hVolume, FSCTL_GET_NTFS_VOLUME_DATA, NULL, 0, 
            &volumeData, sizeof(volumeData), &bytesReturned, NULL))
        {
            CloseHandle(hVolume);
            return false;
        }

        ULONGLONG mftStartLcn = volumeData.MftStartLcn.QuadPart;
        DWORD bytesPerCluster = volumeData.BytesPerCluster;
        DWORD bytesPerFileRecord = volumeData.BytesPerFileRecordSegment;
        ULONGLONG totalRecords = volumeData.MftValidDataLength.QuadPart / bytesPerFileRecord;
        
        if (bytesPerFileRecord == 0 || bytesPerFileRecord > 4096)
        {
            CloseHandle(hVolume);
            return false;
        }

        std::vector<BYTE> fileRecordBuffer;
        
        try
        {
            fileRecordBuffer.resize(bytesPerFileRecord);
        }
        catch (const std::bad_alloc&)
        {
            CloseHandle(hVolume);
            return false;
        }
        
        int fileCount = 0;
        int dirCount = 0;
        
        ULONGLONG maxRecordsToRead = totalRecords;

        auto& fileMap = driveFileMaps[drive];
        auto& childrenMap = driveChildrenMaps[drive];
        
        // Storage for debug info
        std::wstring debugInfo = L"Drive " + drive + L":\n\n";
        bool foundWindows = false;
        bool foundUsers = false;
        
        // Read MFT entries
        for (ULONGLONG i = 0; i < maxRecordsToRead; i++)
        {
            if (i % 1000 == 0)
            {
                std::wstring status = L"Reading drive " + drive + L": " + 
                    std::to_wstring(i) + L" / " + std::to_wstring(maxRecordsToRead);
                UpdateProgress(10 + (int)((i * 70) / maxRecordsToRead), status);
            }
            
            ULONGLONG mftOffset = mftStartLcn * bytesPerCluster + (i * bytesPerFileRecord);
            
            LARGE_INTEGER offset;
            offset.QuadPart = mftOffset;
            
            if (SetFilePointerEx(hVolume, offset, NULL, FILE_BEGIN))
            {
                DWORD bytesRead = 0;
                if (ReadFile(hVolume, fileRecordBuffer.data(), bytesPerFileRecord, 
                    &bytesRead, NULL) && bytesRead == bytesPerFileRecord)
                {
                    FILE_RECORD_HEADER* fileRecord = (FILE_RECORD_HEADER*)fileRecordBuffer.data();
                    
                    if (fileRecord->Type != 'ELIF')
                        continue;
                    
                    if (!(fileRecord->Flags & 0x01))
                        continue;
                    
                    if (fileRecord->AttrsOffset >= bytesPerFileRecord || 
                        fileRecord->BytesInUse > bytesPerFileRecord ||
                        fileRecord->AttrsOffset >= fileRecord->BytesInUse)
                        continue;
                    
                    BYTE* attrPtr = fileRecordBuffer.data() + fileRecord->AttrsOffset;
                    BYTE* recordEnd = fileRecordBuffer.data() + fileRecord->BytesInUse;
                    
                    FileEntry entry;
                    entry.fileReference = i;
                    entry.hTreeItem = NULL;
                    entry.isDirectory = false;
                    entry.driveLetter = drive;
                    bool foundFileName = false;
                    DWORD fileAttributes = 0;
                    
                    while (attrPtr < recordEnd)
                    {
                        if (attrPtr + sizeof(ATTRIBUTE_HEADER) > recordEnd)
                            break;
                        
                        ATTRIBUTE_HEADER* attr = (ATTRIBUTE_HEADER*)attrPtr;
                        
                        if (attr->Type == 0xFFFFFFFF)
                            break;
                        
                        if (attr->Length == 0 || attr->Length > (DWORD)(recordEnd - attrPtr))
                            break;
                        
                        if (attr->Type == 0x30) // FILE_NAME attribute
                        {
                            if (!attr->NonResident)
                            {
                                if (attrPtr + sizeof(RESIDENT_ATTRIBUTE) > recordEnd)
                                    break;
                                
                                RESIDENT_ATTRIBUTE* resAttr = (RESIDENT_ATTRIBUTE*)attr;
                                
                                if (resAttr->ValueOffset >= attr->Length)
                                    break;
                                
                                BYTE* valuePtr = attrPtr + resAttr->ValueOffset;
                                
                                if (valuePtr + offsetof(FILE_NAME_ATTRIBUTE, FileName) > recordEnd)
                                    break;
                                
                                FILE_NAME_ATTRIBUTE* fnAttr = (FILE_NAME_ATTRIBUTE*)valuePtr;
                                
                                if (fnAttr->FileNameLength > 255)
                                    break;
                                
                                size_t filenameBytes = fnAttr->FileNameLength * sizeof(WCHAR);
                                if (valuePtr + offsetof(FILE_NAME_ATTRIBUTE, FileName) + filenameBytes > recordEnd)
                                    break;
                                
                                try
                                {
                                    std::wstring candidateName = std::wstring(fnAttr->FileName, fnAttr->FileNameLength);
                                    ULONGLONG candidateParent = fnAttr->ParentDirectory;
                                    DWORD candidateAttrs = fnAttr->FileAttributes;
                                    BYTE nameType = fnAttr->NameType;
                                    
                                    if (!foundFileName)
                                    {
                                        entry.fileName = candidateName;
                                        entry.parentReference = candidateParent;
                                        fileAttributes = candidateAttrs;
                                        foundFileName = true;
                                    }
                                    else if (nameType != 2)
                                    {
                                        entry.fileName = candidateName;
                                        entry.parentReference = candidateParent;
                                        fileAttributes = candidateAttrs;
                                    }
                                    
                                    entry.isDirectory = ((candidateAttrs & 0x10000000) != 0) || 
                                                       ((candidateAttrs & 0x10) != 0);
                                    
                                    // Collect debug info for specific folders
                                    if (candidateName == L"Windows" && !foundWindows)
                                    {
                                        ULONGLONG parentLower = candidateParent & 0x0000FFFFFFFFFFFF;
                                        debugInfo += L"Windows: MFT#" + std::to_wstring(i) + 
                                            L", Parent=" + std::to_wstring(parentLower) +
                                            L", NameType=" + std::to_wstring(nameType) +
                                            L", isDir=" + (entry.isDirectory ? L"YES" : L"NO") + L"\n";
                                        foundWindows = true;
                                    }
                                    else if (candidateName == L"Users" && !foundUsers)
                                    {
                                        ULONGLONG parentLower = candidateParent & 0x0000FFFFFFFFFFFF;
                                        debugInfo += L"Users: MFT#" + std::to_wstring(i) + 
                                            L", Parent=" + std::to_wstring(parentLower) +
                                            L", NameType=" + std::to_wstring(nameType) +
                                            L", isDir=" + (entry.isDirectory ? L"YES" : L"NO") + L"\n";
                                        foundUsers = true;
                                    }
                                }
                                catch (...)
                                {
                                    break;
                                }
                            }
                        }
                        
                        attrPtr += attr->Length;
                    }
                    
                    if (foundFileName && !entry.fileName.empty())
                    {
                        try
                        {
                            fileMap[i] = entry;
                            if (entry.isDirectory)
                                dirCount++;
                            else
                                fileCount++;
                            
                            ULONGLONG parentRef = entry.parentReference & 0x0000FFFFFFFFFFFF;
                            childrenMap[parentRef].push_back(i);
                        }
                        catch (const std::bad_alloc&)
                        {
                            CloseHandle(hVolume);
                            return false;
                        }
                    }
                }
            }
        }
        
        CloseHandle(hVolume);
        
        totalFiles = fileCount;
        totalDirs = dirCount;
        
        // Add info about root children
        if (childrenMap.find(5) != childrenMap.end())
        {
            debugInfo += L"\nchildrenMap[5] has " + std::to_wstring(childrenMap[5].size()) + L" children\n";
            debugInfo += L"First 20 children of root:\n";
            for (size_t j = 0; j < min(childrenMap[5].size(), (size_t)20); j++)
            {
                ULONGLONG childIdx = childrenMap[5][j];
                if (fileMap.find(childIdx) != fileMap.end())
                {
                    debugInfo += L"  [" + std::to_wstring(childIdx) + L"] " + 
                        fileMap[childIdx].fileName + 
                        (fileMap[childIdx].isDirectory ? L" [DIR]" : L" [FILE]") + L"\n";
                }
            }
        }
        else
        {
            debugInfo += L"\nERROR: childrenMap[5] NOT FOUND!\n";
        }
        
        // Show the debug info
        MessageBoxW(hWnd, debugInfo.c_str(), (L"Debug Info - Drive " + drive).c_str(), MB_OK | MB_ICONINFORMATION);
        
        return true;
    }
    catch (...)
    {
        return false;
    }
}

void BuildTreeView()
{
    if (!hTreeView)
        return;

    LogDebug(L"BuildTreeView: Starting");
    UpdateProgress(85, L"Building tree structure...");

    try
    {
        LogDebug(L"BuildTreeView: Clearing existing items");
        TreeView_DeleteAllItems(hTreeView);

        // Create tree for each drive
        for (auto& drivePair : driveFileMaps)
        {
            const std::wstring& drive = drivePair.first;
            auto& fileMap = drivePair.second;
            
            LogDebug(L"BuildTreeView: Building tree for drive " + drive);
            
            // Add drive root item
            std::wstring driveLabel = drive + L":\\";
            HTREEITEM hDriveRoot = AddItemToTree(hTreeView, (LPWSTR)driveLabel.c_str(), TVI_ROOT);
            
            if (hDriveRoot == NULL)
            {
                LogDebug(L"BuildTreeView: ERROR - Failed to create root for drive " + drive);
                continue;
            }
            
            // Clear tree item pointers for all entries in this drive
            for (auto& pair : fileMap)
            {
                pair.second.hTreeItem = NULL;
            }

            // Root directory (MFT entry 5)
            if (fileMap.find(5) != fileMap.end())
            {
                fileMap[5].hTreeItem = hDriveRoot;
                
                LogDebug(L"BuildTreeView: Building hierarchy for drive " + drive);
                
                // Recursively add all children starting from root
                AddChildrenRecursive(drive, 5, 0);
                
                // Expand drive root
                TreeView_Expand(hTreeView, hDriveRoot, TVE_EXPAND);
            }
            else
            {
                LogDebug(L"BuildTreeView: ERROR - Root entry 5 not found for drive " + drive);
            }
        }

        UpdateProgress(100, L"Complete!");
        LogDebug(L"BuildTreeView: Complete");
    }
    catch (const std::exception& e)
    {
        std::string error = "Exception in BuildTreeView: ";
        error += e.what();
        LogDebug(L"BuildTreeView: EXCEPTION - " + std::wstring(error.begin(), error.end()));
        MessageBoxA(NULL, error.c_str(), "Error", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        LogDebug(L"BuildTreeView: UNKNOWN EXCEPTION");
        MessageBoxW(NULL, L"Unknown exception in BuildTreeView", L"Error", MB_OK | MB_ICONERROR);
    }
}

void ReadMFTAndListFiles(HWND hWnd)
{
    try
    {
        LogDebug(L"\n\n========== NEW SCAN STARTED ==========");
        
        // Show progress controls and hide summary
        if (hProgressBar)
            ShowWindow(hProgressBar, SW_SHOW);
        if (hStatusText)
            ShowWindow(hStatusText, SW_SHOW);
        if (hSummaryText)
            ShowWindow(hSummaryText, SW_HIDE);

        UpdateProgress(0, L"Initializing...");

        LogDebug(L"ReadMFT: Clearing previous data");
        driveFileMaps.clear();
        driveChildrenMaps.clear();

        UpdateProgress(5, L"Detecting drives...");
        
        std::vector<std::wstring> drives = GetFixedDrives();
        
        if (drives.empty())
        {
            MessageBoxW(hWnd, L"No NTFS fixed drives found!", L"Error", MB_OK | MB_ICONERROR);
            return;
        }
        
        LogDebug(L"ReadMFT: Found " + std::to_wstring(drives.size()) + L" NTFS drives");

        LARGE_INTEGER freq, t1, t2;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t1);

        int totalFiles = 0;
        int totalDirs = 0;
        
        // Read MFT for each drive
        for (size_t driveIdx = 0; driveIdx < drives.size(); driveIdx++)
        {
            const std::wstring& drive = drives[driveIdx];
            
            int progressBase = 10 + (int)((driveIdx * 70) / drives.size());
            int progressRange = 70 / (int)drives.size();
            
            UpdateProgress(progressBase, L"Reading drive " + drive + L":...");
            
            int driveFiles = 0;
            int driveDirs = 0;
            
            if (ReadDriveMFT(hWnd, drive, driveFiles, driveDirs))
            {
                totalFiles += driveFiles;
                totalDirs += driveDirs;
                LogDebug(L"ReadMFT: Drive " + drive + L" successful");
            }
            else
            {
                LogDebug(L"ReadMFT: Drive " + drive + L" failed");
            }
        }
        
        QueryPerformanceCounter(&t2);
        double elapsedTimeMS = (t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;

        LogDebug(L"ReadMFT: Complete - Total Files: " + std::to_wstring(totalFiles) + 
                 L", Total Dirs: " + std::to_wstring(totalDirs) + 
                 L", Time: " + std::to_wstring((int)elapsedTimeMS) + L" ms");

        // Update summary text
        if (hSummaryText)
        {
            std::wstring summaryText = L"MFT Read Time: " + std::to_wstring((int)elapsedTimeMS) + 
                L" ms  |  Drives: " + std::to_wstring(drives.size()) +
                L"  |  Folders: " + std::to_wstring(totalDirs) + 
                L"  |  Files: " + std::to_wstring(totalFiles);
            SetWindowTextW(hSummaryText, summaryText.c_str());
            ShowWindow(hSummaryText, SW_SHOW);
        }

        UpdateProgress(85, L"MFT reading complete. Building tree...");
        LogDebug(L"ReadMFT: Starting tree build");

        // Build the tree view
        BuildTreeView();

        LogDebug(L"ReadMFT: Tree build complete");

        // Hide progress controls after completion
        Sleep(1000);
        if (hProgressBar)
            ShowWindow(hProgressBar, SW_HIDE);
        if (hStatusText)
            ShowWindow(hStatusText, SW_HIDE);
            
        LogDebug(L"ReadMFT: All operations complete\n");
    }
    catch (const std::exception& e)
    {
        std::string error = "Exception in ReadMFTAndListFiles: ";
        error += e.what();
        LogDebug(L"ReadMFT: EXCEPTION - " + std::wstring(error.begin(), error.end()));
        MessageBoxA(hWnd, error.c_str(), "Error", MB_OK | MB_ICONERROR);
    }
    catch (...)
    {
        LogDebug(L"ReadMFT: UNKNOWN EXCEPTION");
        MessageBoxW(hWnd, L"Unknown exception in ReadMFTAndListFiles", L"Error", MB_OK | MB_ICONERROR);
    }
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_FINDMYFILE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FINDMYFILE));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
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
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_FINDMYFILE);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, 800, 600, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            // Create status text control
            hStatusText = CreateWindowExW(
                0,
                L"STATIC",
                L"Click anywhere to scan drives...",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 30,
                hWnd,
                (HMENU)1003,
                hInst,
                NULL);

            // Create progress bar control
            hProgressBar = CreateWindowExW(
                0,
                PROGRESS_CLASS,
                NULL,
                WS_CHILD | PBS_SMOOTH,
                0, 30, 0, 30,
                hWnd,
                (HMENU)1002,
                hInst,
                NULL);

            if (hProgressBar)
            {
                SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
                SendMessage(hProgressBar, PBM_SETSTEP, 1, 0);
            }

            // Create summary text control (initially hidden)
            hSummaryText = CreateWindowExW(
                0,
                L"STATIC",
                L"",
                WS_CHILD | SS_CENTER,
                0, 60, 0, 25,
                hWnd,
                (HMENU)1004,
                hInst,
                NULL);

            // Create TreeView control
            hTreeView = CreateWindowExW(
                0,
                WC_TREEVIEW,
                L"Tree View",
                WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | 
                TVS_HASBUTTONS | TVS_LINESATROOT,
                0, 85, 0, 0,
                hWnd,
                (HMENU)1001,
                hInst,
                NULL);
            
            if (hTreeView)
            {
                // Add initial message
                HTREEITEM hRoot = AddItemToTree(hTreeView, 
                    (LPWSTR)L"Click anywhere to scan drives...", TVI_ROOT);
            }
        }
        break;
    case WM_SIZE:
        {
            // Resize controls to fill client area
            RECT rcClient;
            GetClientRect(hWnd, &rcClient);
            
            int statusHeight = 30;
            int progressHeight = 30;
            int summaryHeight = 25;
            int totalHeaderHeight = statusHeight + progressHeight + summaryHeight;
            
            if (hStatusText)
            {
                SetWindowPos(hStatusText, NULL, 0, 0, 
                    rcClient.right, statusHeight, SWP_NOZORDER);
            }
            
            if (hProgressBar)
            {
                SetWindowPos(hProgressBar, NULL, 10, statusHeight, 
                    rcClient.right - 20, progressHeight, SWP_NOZORDER);
            }

            if (hSummaryText)
            {
                SetWindowPos(hSummaryText, NULL, 0, statusHeight + progressHeight, 
                    rcClient.right, summaryHeight, SWP_NOZORDER);
            }
            
            if (hTreeView)
            {
                SetWindowPos(hTreeView, NULL, 0, totalHeaderHeight, 
                    rcClient.right, rcClient.bottom - totalHeaderHeight, SWP_NOZORDER);
            }
        }
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
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
    case WM_LBUTTONDOWN:
        // Trigger MFT read on left mouse click
        ReadMFTAndListFiles(hWnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
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
