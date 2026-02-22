# Find My File!

A high-performance Windows application that instantly readsthe NTFS Master File Table (MFT) directly, and provides instant searchability by name for any file or folder on your entire file system structure.

## Features

- **Lightning-fast initial scanning**: Reads the MFT directly for sub-second indexing of millions of files
- **Multi-drive support**: Scans all fixed NTFS drives (C:, D:, E:, etc.)
- **Hierarchical tree view**: Browse your entire file system in an intuitive tree structure
- **Blazing fast search**: Search results in <200ms
- **Scoped Searches**: Clicking on a tree node automatically scopes the searches to that folder and its subfolders only
- **Preview Pane**: Quick property previews for search results

## Requirements

- Windows 10/11 (x64)
- Administrator privileges (required to access MFT)
- NTFS file system

## Building

1. Open `Find My File!.sln` in Visual Studio 2022
2. Select x64 Release configuration
3. Build Solution (Ctrl+Shift+B)

## Usage

1. Run as Administrator
2. Click anywhere in the window to start scanning
3. Wait for MFT reading to complete
4. Search for your files or folders by their names or part of it

## Technical Details

- Written in C++ using Win32 API
- Direct MFT access via `FSCTL_GET_NTFS_VOLUME_DATA`
- Efficient memory-mapped data structures for handling millions of entries
- Custom bounds checking and validation for MFT record parsing

## Performance

- Typical scan time: 2-5 seconds for 4-5 million files
- Memory usage: ~500MB for 5 million entries
- Supports volumes with billions of MFT entries (with 2M record limit for performance)

## License

MIT License - See LICENSE file for details

## Author

Suman Ghosh - [@sumanthewhiz](https://github.com/sumanthewhiz)
