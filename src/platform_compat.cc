#include "platform_compat.h"

#include <string.h>

#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <timeapi.h>
#else
#include <chrono>
#endif

#include <SDL.h>

namespace fallout {

static bool compatFileModeMayWrite(const char* mode)
{
    return mode != NULL && (strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL);
}

#ifndef _WIN32
typedef struct CompatDirectoryCacheEntry {
    std::unordered_map<std::string, std::string> entries;
} CompatDirectoryCacheEntry;

static const size_t kCompatDirectoryEntryCacheMaxSize = 1024;
static std::unordered_map<std::string, CompatDirectoryCacheEntry> compatDirectoryEntryCache;

static std::string compatLowercase(std::string value)
{
    for (char& ch : value) {
        ch = static_cast<char>(SDL_tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

static void compatDirectoryEntryCacheClear()
{
    compatDirectoryEntryCache.clear();
}

// Miyoo Mini: the original code listed every directory of a path again for
// each file opened (opendir/readdir per path component), thousands of times
// while loading a map or a save. Listings are now kept in memory, and
// creating, removing or renaming a file only changes one entry of one
// listing. The cache key of a directory is the textual prefix of the path up
// to its last separator ("." when there is none, "/" for the root).
static bool compatDirectoryEntryCacheSplit(const char* nativePath, std::string& directoryPath, std::string& name)
{
    const char* sep = strrchr(nativePath, '/');
    if (sep == NULL) {
        directoryPath = ".";
        name = nativePath;
    } else if (sep == nativePath) {
        directoryPath = "/";
        name = sep + 1;
    } else {
        directoryPath.assign(nativePath, sep - nativePath);
        name = sep + 1;
    }

    return !name.empty();
}

static void compatDirectoryEntryCacheAdd(const char* nativePath)
{
    std::string directoryPath;
    std::string name;
    if (!compatDirectoryEntryCacheSplit(nativePath, directoryPath, name)) {
        return;
    }

    auto directory = compatDirectoryEntryCache.find(directoryPath);
    if (directory != compatDirectoryEntryCache.end()) {
        directory->second.entries[compatLowercase(name)] = name;
    }
}

static void compatDirectoryEntryCacheRemove(const char* nativePath)
{
    std::string directoryPath;
    std::string name;
    if (!compatDirectoryEntryCacheSplit(nativePath, directoryPath, name)) {
        return;
    }

    auto directory = compatDirectoryEntryCache.find(directoryPath);
    if (directory != compatDirectoryEntryCache.end()) {
        directory->second.entries.erase(compatLowercase(name));
    }
}

static const CompatDirectoryCacheEntry* compatDirectoryEntryCacheGet(const std::string& directoryPath)
{
    auto existing = compatDirectoryEntryCache.find(directoryPath);
    if (existing != compatDirectoryEntryCache.end()) {
        return &(existing->second);
    }

    DIR* dir = opendir(directoryPath.c_str());
    if (dir == NULL) {
        return NULL;
    }

    if (compatDirectoryEntryCache.size() >= kCompatDirectoryEntryCacheMaxSize) {
        compatDirectoryEntryCache.clear();
    }

    auto inserted = compatDirectoryEntryCache.emplace(directoryPath, CompatDirectoryCacheEntry());
    CompatDirectoryCacheEntry& cacheEntry = inserted.first->second;

    struct dirent* entry = readdir(dir);
    while (entry != NULL) {
        cacheEntry.entries.emplace(compatLowercase(entry->d_name), entry->d_name);
        entry = readdir(dir);
    }

    closedir(dir);

    return &cacheEntry;
}
#else
static void compatDirectoryEntryCacheClear()
{
}

static void compatDirectoryEntryCacheAdd(const char*)
{
}

static void compatDirectoryEntryCacheRemove(const char*)
{
}
#endif

static void compat_prepare_native_path(char* nativePath, const char* path)
{
    strncpy(nativePath, path, COMPAT_MAX_PATH - 1);
    nativePath[COMPAT_MAX_PATH - 1] = '\0';
    compat_windows_path_to_native(nativePath);
    compat_resolve_path(nativePath);
}

int compat_stricmp(const char* string1, const char* string2)
{
    return SDL_strcasecmp(string1, string2);
}

int compat_strnicmp(const char* string1, const char* string2, size_t size)
{
    return SDL_strncasecmp(string1, string2, size);
}

char* compat_strupr(char* string)
{
    return SDL_strupr(string);
}

char* compat_strlwr(char* string)
{
    return SDL_strlwr(string);
}

char* compat_itoa(int value, char* buffer, int radix)
{
    return SDL_itoa(value, buffer, radix);
}

void compat_splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
#ifdef _WIN32
    _splitpath(path, drive, dir, fname, ext);
#else
    const char* driveStart = path;
    if (path[0] == '/' && path[1] == '/') {
        path += 2;
        while (*path != '\0' && *path != '/' && *path != '.') {
            path++;
        }
    }

    if (drive != NULL) {
        size_t driveSize = path - driveStart;
        if (driveSize > COMPAT_MAX_DRIVE - 1) {
            driveSize = COMPAT_MAX_DRIVE - 1;
        }
        strncpy(drive, path, driveSize);
        drive[driveSize] = '\0';
    }

    const char* dirStart = path;
    const char* fnameStart = path;
    const char* extStart = NULL;

    const char* end = path;
    while (*end != '\0') {
        if (*end == '/') {
            fnameStart = end + 1;
        } else if (*end == '.') {
            extStart = end;
        }
        end++;
    }

    if (extStart == NULL) {
        extStart = end;
    }

    if (dir != NULL) {
        size_t dirSize = fnameStart - dirStart;
        if (dirSize > COMPAT_MAX_DIR - 1) {
            dirSize = COMPAT_MAX_DIR - 1;
        }
        strncpy(dir, path, dirSize);
        dir[dirSize] = '\0';
    }

    if (fname != NULL) {
        size_t fileNameSize = extStart - fnameStart;
        if (fileNameSize > COMPAT_MAX_FNAME - 1) {
            fileNameSize = COMPAT_MAX_FNAME - 1;
        }
        strncpy(fname, fnameStart, fileNameSize);
        fname[fileNameSize] = '\0';
    }

    if (ext != NULL) {
        size_t extSize = end - extStart;
        if (extSize > COMPAT_MAX_EXT - 1) {
            extSize = COMPAT_MAX_EXT - 1;
        }
        strncpy(ext, extStart, extSize);
        ext[extSize] = '\0';
    }
#endif
}

void compat_makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
#ifdef _WIN32
    _makepath(path, drive, dir, fname, ext);
#else
    path[0] = '\0';

    if (drive != NULL) {
        if (*drive != '\0') {
            strcpy(path, drive);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (dir != NULL) {
        if (*dir != '\0') {
            if (*dir != '/' && *path == '/') {
                path++;
            }

            strcpy(path, dir);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (fname != NULL && *fname != '\0') {
        if (*fname != '/' && *path == '/') {
            path++;
        }

        strcpy(path, fname);
        path = strchr(path, '\0');
    } else {
        if (*path == '/') {
            path++;
        }
    }

    if (ext != NULL) {
        if (*ext != '\0') {
            if (*ext != '.') {
                *path++ = '.';
            }

            strcpy(path, ext);
            path = strchr(path, '\0');
        }
    }

    *path = '\0';
#endif
}

int compat_read(int fileHandle, void* buf, unsigned int size)
{
    return read(fileHandle, buf, size);
}

int compat_write(int fileHandle, const void* buf, unsigned int size)
{
    return write(fileHandle, buf, size);
}

long compat_lseek(int fileHandle, long offset, int origin)
{
    return lseek(fileHandle, offset, origin);
}

long compat_tell(int fd)
{
    return lseek(fd, 0, SEEK_CUR);
}

long compat_filelength(int fd)
{
    long originalOffset = lseek(fd, 0, SEEK_CUR);
    lseek(fd, 0, SEEK_SET);
    long filesize = lseek(fd, 0, SEEK_END);
    lseek(fd, originalOffset, SEEK_SET);
    return filesize;
}

int compat_mkdir(const char* path)
{
    compatDirectoryEntryCacheClear();

    char nativePath[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativePath, path);

#ifdef _WIN32
    int rc = mkdir(nativePath);
#else
    int rc = mkdir(nativePath, 0755);
#endif
    if (rc == 0) {
        compatDirectoryEntryCacheClear();
    }
    return rc;
}

unsigned int compat_timeGetTime()
{
#ifdef _WIN32
    return timeGetTime();
#else
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count());
#endif
}

FILE* compat_fopen(const char* path, const char* mode)
{
    bool mayWrite = compatFileModeMayWrite(mode);

    char nativePath[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativePath, path);
    FILE* stream = fopen(nativePath, mode);
    if (mayWrite && stream != NULL) {
        compatDirectoryEntryCacheAdd(nativePath);
    }
    return stream;
}

int compat_remove(const char* path)
{
    char nativePath[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativePath, path);
    int rc = remove(nativePath);
    if (rc == 0) {
        compatDirectoryEntryCacheRemove(nativePath);
    }
    return rc;
}

int compat_rename(const char* oldFileName, const char* newFileName)
{
    char nativeOldFileName[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativeOldFileName, oldFileName);

    char nativeNewFileName[COMPAT_MAX_PATH];
    compat_prepare_native_path(nativeNewFileName, newFileName);

    int rc = rename(nativeOldFileName, nativeNewFileName);
    if (rc == 0) {
        compatDirectoryEntryCacheRemove(nativeOldFileName);
        compatDirectoryEntryCacheAdd(nativeNewFileName);
    }
    return rc;
}

void compat_windows_path_to_native(char* path)
{
#ifndef _WIN32
    char* pch = path;
    while (*pch != '\0') {
        if (*pch == '\\') {
            *pch = '/';
        }
        pch++;
    }
#endif
}

void compat_resolve_path(char* path)
{
#ifndef _WIN32
    char* pch = path;
    std::string directoryPath;
    if (pch[0] == '/') {
        directoryPath = "/";
        pch++;
    } else {
        directoryPath = ".";
    }

    while (true) {
        while (*pch == '/') {
            pch++;
        }

        if (*pch == '\0') {
            break;
        }

        char* sep = strchr(pch, '/');
        size_t length;
        if (sep != NULL) {
            length = sep - pch;
        } else {
            length = strlen(pch);
        }

        const CompatDirectoryCacheEntry* directoryCacheEntry = compatDirectoryEntryCacheGet(directoryPath);
        if (directoryCacheEntry == NULL) {
            break;
        }

        std::string component(pch, length);
        auto cacheEntry = directoryCacheEntry->entries.find(compatLowercase(component));
        if (cacheEntry == directoryCacheEntry->entries.end()) {
            break;
        }

        strncpy(pch, cacheEntry->second.c_str(), length);

        if (sep == NULL) {
            break;
        }

        *sep = '\0';
        directoryPath = path;
        *sep = '/';

        pch = sep + 1;
    }
#endif
}

char* compat_strdup(const char* string)
{
    return SDL_strdup(string);
}

// It's a replacement for compat_filelength(fileno(stream)) on platforms without
// fileno defined.
long getFileSize(FILE* stream)
{
    long originalOffset = ftell(stream);
    fseek(stream, 0, SEEK_END);
    long filesize = ftell(stream);
    fseek(stream, originalOffset, SEEK_SET);
    return filesize;
}

} // namespace fallout
