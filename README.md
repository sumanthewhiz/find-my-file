# Find My File!

A high-performance Windows desktop application that reads the NTFS Master File Table (MFT) directly to index millions of files in seconds, then provides instant search across your entire file system — including cloud storage providers — with a modern, themeable UI.

![Windows](https://img.shields.io/badge/platform-Windows%2010%2F11-blue) ![C++14](https://img.shields.io/badge/language-C%2B%2B14-orange) ![Win32 API](https://img.shields.io/badge/framework-Win32%20API-lightgrey)

---

## Features

### Core Search & Indexing
- **Lightning-fast MFT scanning** — Reads the NTFS Master File Table directly via `FSCTL_ENUM_USN_DATA`, indexing 4–5 million files in 2–5 seconds
- **Multi-drive support** — Automatically detects and lists all fixed NTFS drives (C:, D:, E:, etc.) with per-drive scan buttons
- **Rich search modes**:
  - **Exact token match** — Instant O(1) hash lookup for full filename/token matches
  - **Prefix match** — O(log N) binary search on sorted token lists
  - **Substring match** — Trigram index eliminates brute-force scans for 3+ character queries
  - **Stemmed/morphological match** — A built-in Porter-style stemmer maps variants like *walking*, *walked*, *walker* ? *walk*
  - **Wildcard/glob patterns** — Supports `*` and `?` with trigram-accelerated pre-filtering
- **Scoped searches** — Select any folder in the tree view to restrict search results to that subtree
- **Live filesystem monitoring** — Background USN journal polling detects file creates, deletes, and renames in real time and updates the index incrementally

### Windows Search Integration
- **Full-text content search** — The "Find with Windows Search" button queries the Windows Search Indexer via OLE DB, searching both filenames (`LIKE`) and file contents (`CONTAINS`)
- **Content snippet extraction** — For content-only matches, reads up to 64 KB of each file to extract a contextual snippet around the keyword
- **Combined results** — Merges local filename hits, content hits, and cloud results into a single ranked list

### Cloud Storage Integration
- **OneDrive** — Search files via the Microsoft Graph API (`/me/drive/root/search`)
- **Google Drive** — Search files via the Google Drive API v3 (`files.list` with `name contains` query)
- **iCloud Drive** — OAuth sign-in supported; search is limited due to the lack of a public REST search API on Windows
- **OAuth2 loopback flow** — Opens the system browser for authentication, listens on `localhost:5483/callback` for the redirect, and exchanges the authorization code for access/refresh tokens — all on a background thread so the UI stays responsive
- **Token persistence** — Credentials and tokens are saved to `%APPDATA%\FindMyFile\cloud_tokens.ini` and automatically refreshed on expiry
- **Provider-specific result labeling** — Cloud results are tagged as "OneDrive" or "Google Drive" in the Match column, not a generic "Cloud"

### User Interface
- **Tabbed layout** — Browse tab (tree view) and Search Results tab (list view + properties panel)
- **Hierarchical tree view** — On-demand lazy loading: only one level of children is populated per expansion, keeping memory and startup time low
- **Search results list** — Six columns: Name, Type, Full Path, Modified, Match quality, and Snippet — with system file icons from the shell image list
- **Keyword highlighting** — Custom-drawn cells highlight every occurrence of the search keyword in the Name and Snippet columns
- **Properties panel** — A resizable right-hand panel shows file name, type, path, size, timestamps (created/modified/accessed), attributes, and content match previews with `«keyword»` markers
- **Splitter** — Drag the vertical splitter between the list view and properties panel to resize; a collapse/expand toggle button (» / «) hides or shows the panel entirely
- **Dark mode** — A single click toggles between a light and dark color scheme, applied consistently to all controls including tree view, list view, headers, buttons, and edit boxes
- **Owner-drawn buttons** — All buttons use rounded rectangles with hover/press visual feedback, matching the active color scheme
- **As-you-type search** — A 300 ms debounce timer triggers a search automatically after typing stops; no need to press Enter or click Find

### Search Ranking
Results are scored and sorted by match quality:

| Score | Category | Description |
|-------|----------|-------------|
| 100 | Exact | Full token/filename match |
| 90 | Wildcard | Glob pattern match |
| 75 | Prefix | Keyword is a prefix of a token |
| 60 | Filename (Win Search) | Windows Search filename hit |
| 51 | Cloud | OneDrive / Google Drive result |
| 50 | Token | Indexed token match |
| 40 | Substring | Keyword found inside filename |
| 30 | Stem (exact) | Stemmed form matches exactly |
| 25 | Stem (prefix) | Stemmed form is a prefix |
| 20 | Content (Win Search) | Windows Search content-only hit |

Bonus points are added for:
- **Case-sensitive match** (+15) — The keyword appears with exact casing in the filename
- **Recency** (+10 / +7 / +3) — File modified within the last 7 days, 30 days, or 1 year

---

## Requirements

- **OS**: Windows 10 or 11 (x64)
- **Privileges**: Must run as **Administrator** (required for direct MFT access via `DeviceIoControl`)
- **File system**: NTFS (the app auto-detects and skips non-NTFS volumes)
- **Build tools**: Visual Studio 2022 with the C++ Desktop workload

---

## Building

1. Open `Find My File!.sln` in Visual Studio 2022
2. Select the **x64 Release** configuration
3. Build the solution (**Ctrl+Shift+B**)

The project links against: `comctl32.lib`, `shlwapi.lib`, `uxtheme.lib`, `ole32.lib`, `oleaut32.lib`, `winhttp.lib`, `ws2_32.lib`.

---

## Usage

### Basic Workflow

1. **Launch** the application as Administrator
2. **Click a drive button** (e.g., "Scan C:") to read the MFT and build the file tree
3. **Browse** using the tree view (Browse tab) — nodes expand on demand
4. **Search** by typing in the search box:
   - The **Find** button uses the MFT-based inverted index (fastest, filename only)
   - The **Find with Windows Search** button also searches file contents and cloud providers
5. **View results** in the Search Results tab — click a row to see properties; double-click to open
6. **Right-click** a result for options: Open, Copy Path, Open Containing Folder
7. **Scope searches** by selecting a folder in the tree before searching — only descendants of that folder are returned
8. **Use wildcards**: `*.pdf`, `report_202?.*`, `*budget*`

### Cloud Setup

1. Click the **? Cloud** button in the toolbar
2. Enter your OAuth Client ID (and Client Secret for Google/iCloud) for each provider:
   - **OneDrive**: Register an app at [Azure Portal](https://portal.azure.com) ? App registrations. Add `http://localhost:5483/callback` as a **Mobile and desktop applications** redirect URI.
   - **Google Drive**: Create credentials at [Google Cloud Console](https://console.cloud.google.com) ? APIs & Services ? Credentials. Add `http://localhost:5483/callback` as an authorized redirect URI and enable the Google Drive API.
   - **iCloud**: Register at [Apple Developer](https://developer.apple.com). Note: iCloud search is limited on Windows due to no public REST search API.
3. Click **Sign In** — a browser tab opens for authentication; the app listens locally for the OAuth callback
4. After signing in, cloud results automatically appear when using "Find with Windows Search"

---

## Architecture

### High-Level Overview

```
???????????????????????????????????????????????????????????????????
?                        Win32 UI Layer                           ?
?  ????????????  ????????????????  ????????????  ??????????????  ?
?  ? TreeView ?  ?   ListView   ?  ?Properties?  ?  Buttons/  ?  ?
?  ? (Browse) ?  ?(Search Rslt) ?  ?  Panel   ?  ?  Toolbar   ?  ?
?  ????????????  ????????????????  ????????????  ??????????????  ?
?       ?               ?               ?                         ?
?  ?????????????????????????????????????????????????????????????  ?
?  ?                   Layout & Theme Engine                    ?  ?
?  ?          (LayoutControls / ApplyTheme / WM_DRAWITEM)      ?  ?
?  ?????????????????????????????????????????????????????????????  ?
???????????????????????????????????????????????????????????????????
                          ?
???????????????????????????????????????????????????????????????????
?                       Search Engine                             ?
?  ????????????????  ????????????????  ?????????????????????????  ?
?  ?Inverted Index?  ?Stemmed Index ?  ?    Trigram Index       ?  ?
?  ?  (token?refs)?  ? (stem?refs)  ?  ? (3-char hash?refs)    ?  ?
?  ????????????????  ????????????????  ?????????????????????????  ?
?         ?                 ?                       ?              ?
?  ??????????????????????????????????????????????????????????????  ?
?  ?         PerformSearch / Wildcard / Ranking / Sorting        ?  ?
?  ??????????????????????????????????????????????????????????????  ?
???????????????????????????????????????????????????????????????????
                          ?
???????????????????????????????????????????????????????????????????
?                       Data Layer                                ?
?  ??????????????  ????????????????  ???????????????????????????? ?
?  ?  fileMap    ?  ? childrenMap  ?  ? fullPathCache /          ? ?
?  ?(ref?entry) ?  ?(parent?kids) ?  ? timestampCache           ? ?
?  ??????????????  ????????????????  ???????????????????????????? ?
?        ?                ?                                       ?
?  ?????????????????????????????????????????????????????????????  ?
?  ?   ReadDriveMFT (FSCTL_ENUM_USN_DATA) / USN Journal Poll   ?  ?
?  ??????????????????????????????????????????????????????????????  ?
???????????????????????????????????????????????????????????????????
                          ?
???????????????????????????????????????????????????????????????????
?                   External Integrations                         ?
?  ????????????????????  ????????????????????  ????????????????  ?
?  ? Windows Search   ?  ? OneDrive (Graph) ?  ? Google Drive  ?  ?
?  ? (OLE DB / SQL)   ?  ? (WinHTTP/REST)   ?  ?(WinHTTP/REST)?  ?
?  ????????????????????  ????????????????????  ????????????????  ?
?                                                                 ?
?  OAuth2 loopback server (Winsock TCP on localhost:5483)         ?
???????????????????????????????????????????????????????????????????
```

### Key Data Structures

| Structure | Purpose |
|-----------|---------|
| `fileMap` (`unordered_map<ULONGLONG, FileEntry>`) | Maps MFT file reference numbers to file metadata (name, parent, flags). Central lookup table. |
| `childrenMap` (`unordered_map<ULONGLONG, vector<ULONGLONG>>`) | Maps each directory's reference to its children's references. Drives the tree view. |
| `invertedIndex` (`unordered_map<wstring, vector<ULONGLONG>>`) | Maps lowercased filename tokens to file references. Primary search index. |
| `stemmedIndex` | Same structure, but keyed by Porter-stemmed tokens for morphological matching. |
| `trigramIndex` (`unordered_map<ULONGLONG, vector<ULONGLONG>>`) | Maps 3-character hash keys to sorted, deduplicated file reference lists. Enables O(1) substring candidate lookup. |
| `sortedTokens` / `sortedStemTokens` | Sorted vectors of all index keys, enabling O(log N) prefix range queries via `std::lower_bound`. |
| `fullPathCache` / `timestampCache` | Memoize expensive path-building walks and filesystem timestamp queries. |
| `treeItemToRef` (`unordered_map<HTREEITEM, ULONGLONG>`) | Reverse map from Win32 tree items back to MFT references for on-demand expansion. |

### MFT Reading Pipeline

1. Open the volume handle (`\\.\C:`) with `GENERIC_READ`
2. Query the USN journal via `FSCTL_QUERY_USN_JOURNAL` (create one if missing)
3. Enumerate all MFT records with `FSCTL_ENUM_USN_DATA` in 128 KB batches
4. Parse each `USN_RECORD`: extract the 48-bit file reference, parent reference, filename, attributes, and timestamp
5. Build `fileMap` and `childrenMap` in a single pass
6. Pre-compute `hasChildren` flags for all directories
7. Build the search indexes (inverted, stemmed, trigram) with progress reporting

### Live Monitoring

After the initial scan, a background thread (`UsnMonitorThreadFunc`) polls the USN journal every 2 seconds using `FSCTL_READ_USN_JOURNAL`. New creates, deletes, and renames are batched and posted to the UI thread via `WM_USN_UPDATED`. The UI thread then incrementally updates `fileMap`, `childrenMap`, all three search indexes, and the sorted token vectors — no full rescan needed.

### Search Algorithm

**Normal search** runs four index lookups in order, collecting the best score per file:

1. **Exact token** (score 100) — O(1) hash lookup in `invertedIndex`
2. **Prefix** (score 75) — `std::lower_bound` on `sortedTokens`, walk forward while prefix matches
3. **Substring** (score 40) — Extract trigrams from the keyword, find the trigram posting list with the fewest entries, verify actual substring match on those candidates only
4. **Stemmed** (score 30/25) — Same hash + prefix strategy on `stemmedIndex`

Results are capped at 10,000 via `std::partial_sort` and full paths are built only for the final set.

**Wildcard search** uses an iterative backtracking glob matcher. When the pattern contains a literal span ? 3 characters, trigram pre-filtering narrows candidates before running the glob.

### Cloud Search Flow

1. User clicks "Find with Windows Search"
2. Local results are gathered via OLE DB
3. For each logged-in cloud provider, `CloudSearchFiles` makes an HTTPS GET (via WinHTTP) to the provider's search API
4. JSON responses are parsed with a lightweight hand-rolled extractor (`JsonGetString`, `JsonParseOneDriveResults`, `JsonParseGoogleDriveResults`)
5. Cloud results are merged into the main results list with score 51 and tagged with their provider name
6. Double-clicking a cloud result opens its `webUrl` / `webViewLink` in the default browser

### Theming

Two complete `ColorScheme` structs (22 colors each) define the light and dark palettes. `ApplyTheme` propagates colors to all controls, including Explorer dark-mode themes on the tree and list views. All buttons are owner-drawn (`BS_OWNERDRAW` + `WM_DRAWITEM`) with rounded corners, hover brightening, and press darkening.

---

## Performance Characteristics

| Metric | Typical Value |
|--------|---------------|
| MFT scan time (C: drive, ~4M files) | 2–5 seconds |
| Search index build time | 1–3 seconds |
| Search latency (inverted index) | < 50 ms |
| Search latency (trigram substring) | < 200 ms |
| Memory usage (~5M file entries) | ~500 MB |
| USN journal poll interval | 2 seconds |
| Search debounce (as-you-type) | 300 ms |

---

## File Structure

```
Find My File!/
??? Find My File!.sln          # Visual Studio solution
??? Find My File!.vcxproj      # Project file
??? Find My File!.cpp          # All application logic (~4,500 lines)
??? Find My File!.h            # App header (includes resource.h)
??? Resource.h                 # Dialog/control ID definitions
??? Find My File!.rc           # Resource script (menus, dialogs, icons, strings)
??? framework.h                # Precompiled header (Win32 + Winsock includes)
??? targetver.h                # Windows SDK version targeting
??? Find My File!.ico          # Application icon
??? small.ico                  # Small application icon
??? README.md                  # This file
```

The entire application is implemented in a single `.cpp` file with no external dependencies beyond the Windows SDK and system libraries.

---

## License

MIT License — See LICENSE file for details.

## Author

Suman Ghosh — [@sumanthewhiz](https://github.com/sumanthewhiz)
