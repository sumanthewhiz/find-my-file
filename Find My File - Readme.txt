================================================================================
                            Find My File!
          High-Performance File Search for Windows 10/11
================================================================================

FEATURES
--------

  Core Search & Indexing
  - Lightning-fast MFT scanning: indexes 4-5 million files in 2-5 seconds
  - Multi-drive support: automatically detects all fixed NTFS drives
  - Rich search modes:
      * Exact match      - instant lookup for full filename matches
      * Prefix match     - finds files starting with your keyword
      * Substring match  - finds your keyword anywhere in the filename
      * Stemmed match    - matches word variants (e.g. walk/walking/walked)
      * Wildcard/glob    - supports * and ? patterns (e.g. *.pdf, report_202?.*)
      * Fuzzy match      - catches common typos and misspellings
  - Scoped searches: select a folder in the tree to limit results to that subtree
  - Persistent index cache: index is saved on exit and reloaded on next launch,
    so startup is near-instant without re-scanning drives
  - Live filesystem monitoring: file creates, deletes, and renames are detected
    in real time and the index updates automatically
  - WARP recency ranking (optional): integrates with WARP to boost recently
    used files in search results. Works without WARP installed.

  Windows Search Integration
  - Full-text content search via the Windows Search Indexer
  - Content snippet extraction showing keyword context
  - Combined results merging filename hits, content hits, and cloud results

  Cloud Storage Integration
  - OneDrive search via Microsoft Graph API
  - Google Drive search via Google Drive API v3
  - iCloud Drive sign-in supported (limited search on Windows)
  - OAuth2 authentication through your default browser
  - Tokens saved and auto-refreshed between sessions

  User Interface
  - Tabbed layout: Browse tab (tree view) and Search Results tab
  - Hierarchical tree view with lazy loading
  - Search results with six columns: Name, Type, Path, Modified, Match, Snippet
  - Keyword highlighting in results
  - Resizable properties panel with file details and content previews
  - Dark mode toggle (single click)
  - As-you-type search (no need to press Enter)

REQUIREMENTS
------------

  - Windows 10 or 11 (x64)
  - Must run as Administrator (required for direct MFT access)
  - NTFS file system (non-NTFS volumes are skipped automatically)
  - WARP (optional): install for enhanced recency-based ranking

USAGE
-----

  Basic Workflow:

  1. Launch the application as Administrator.
  2. If a cached index exists from a previous session, it loads automatically.
     Otherwise, click a drive button (e.g. "Scan C:") to build the file index.
  3. Browse using the tree view (Browse tab) - nodes expand on demand.
  4. Search by typing in the search box:
       - "Find" uses the MFT-based index (fastest, filename only)
       - "Find with Windows Search" also searches file contents and cloud
  5. View results in the Search Results tab:
       - Click a row to see properties
       - Double-click to open the file
  6. Right-click a result for: Open, Copy Path, Open Containing Folder.
  7. Scope searches by selecting a folder in the tree before searching.
  8. Use wildcards: *.pdf, report_202?.*, *budget*

  Cloud Setup:

  1. Click the Cloud button in the toolbar.
  2. Enter your OAuth Client ID (and Secret for Google/iCloud) for each provider:
       - OneDrive: register an app at https://portal.azure.com
         Add http://localhost:5483/callback as a redirect URI.
       - Google Drive: create credentials at https://console.cloud.google.com
         Add http://localhost:5483/callback as a redirect URI and enable the
         Google Drive API.
       - iCloud: register at https://developer.apple.com
         (search is limited on Windows)
  3. Click "Sign In" - a browser tab opens for authentication.
  4. After signing in, cloud results appear in "Find with Windows Search" results.

DATA FILES
----------

  The following files are created automatically under %APPDATA%\FindMyFile\:

  - index.db          Cached file index (loaded on startup, saved on exit)
  - cloud_tokens.ini  OAuth credentials and tokens for cloud providers

LICENSE
-------

  MIT License - see LICENSE file for details.

  Author: Suman Ghosh