#include "Files.hpp"

#include <cstring>

#if defined(_WIN32)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#else
	#include <dirent.h>
	#include <sys/stat.h>
	#include <sys/types.h>
#endif

namespace fnf {

std::string NormalizePath(const std::string& path) {
	std::string out = path;
	for (char& c : out) {
		if (c == '\\')
			c = '/';
	}
	// A trailing slash would make "dir/" and "dir" two different strings, and
	// everything here compares paths as strings.
	while (out.size() > 1 && out.back() == '/')
		out.pop_back();
	return out;
}

std::string JoinPath(const std::string& dir, const std::string& name) {
	if (dir.empty())
		return name;
	if (name.empty())
		return dir;
	std::string out = dir;
	if (out.back() != '/' && out.back() != '\\')
		out += '/';
	return out + name;
}

std::string FileName(const std::string& path) {
	size_t cut = path.find_last_of("/\\");
	return (cut == std::string::npos) ? path : path.substr(cut + 1);
}

std::string ParentPath(const std::string& path) {
	size_t cut = path.find_last_of("/\\");
	return (cut == std::string::npos) ? std::string() : path.substr(0, cut);
}

std::string FileExtension(const std::string& path) {
	std::string name = FileName(path);
	size_t dot = name.find_last_of('.');
	// A leading dot is a hidden file, not an extension: ".gitignore" has none.
	if (dot == std::string::npos || dot == 0)
		return std::string();
	return name.substr(dot);
}

std::string FileStem(const std::string& path) {
	std::string name = FileName(path);
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || dot == 0)
		return name;
	return name.substr(0, dot);
}

#if defined(_WIN32)

namespace {

/** Windows hands out UTF-16 and the rest of this plugin speaks UTF-8. Doing the
conversion here, explicitly, is the whole reason the listing is written out by
hand rather than left to a library that would quietly use the local code page. */
std::string ToUtf8(const wchar_t* wide) {
	if (!wide || !*wide)
		return std::string();
	int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (need <= 1)
		return std::string();
	std::string out((size_t) (need - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], need, nullptr, nullptr);
	return out;
}

std::wstring ToUtf16(const std::string& utf8) {
	if (utf8.empty())
		return std::wstring();
	int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
	if (need <= 1)
		return std::wstring();
	std::wstring out((size_t) (need - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &out[0], need);
	return out;
}

}  // namespace

bool ListDirectory(const std::string& path, std::vector<DirEntry>* out) {
	if (!out)
		return false;
	out->clear();
	std::wstring pattern = ToUtf16(NormalizePath(path) + "/*");
	if (pattern.empty())
		return false;

	WIN32_FIND_DATAW find;
	HANDLE h = FindFirstFileW(pattern.c_str(), &find);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	do {
		std::string name = ToUtf8(find.cFileName);
		if (name.empty() || name == "." || name == "..")
			continue;
		DirEntry e;
		e.name = name;
		e.isDir = (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		// A junction or a symlink. Followed nowhere, for the same reason as on
		// the other systems: a mod folder linking back up to the home directory
		// would take the whole walk with it.
		e.isSymlink = (find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
		out->push_back(std::move(e));
	} while (FindNextFileW(h, &find));
	FindClose(h);
	return true;
}

bool IsDirectory(const std::string& path) {
	std::wstring wide = ToUtf16(NormalizePath(path));
	if (wide.empty())
		return false;
	DWORD attrs = GetFileAttributesW(wide.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

#else

bool ListDirectory(const std::string& path, std::vector<DirEntry>* out) {
	if (!out)
		return false;
	out->clear();
	DIR* dir = opendir(NormalizePath(path).c_str());
	if (!dir)
		return false;

	struct dirent* ent;
	while ((ent = readdir(dir)) != nullptr) {
		std::string name = ent->d_name;
		if (name == "." || name == "..")
			continue;
		DirEntry e;
		e.name = name;

		// d_type is a shortcut that not every filesystem fills in, so when it
		// says "unknown" the answer has to be asked for properly.
		std::string full = JoinPath(path, name);
		struct stat st;
		if (lstat(full.c_str(), &st) == 0) {
			e.isSymlink = S_ISLNK(st.st_mode);
			e.isDir = e.isSymlink ? false : S_ISDIR(st.st_mode);
			if (e.isSymlink) {
				// Recorded, but still worth knowing what it points at for the
				// callers that only skip links into directories.
				struct stat target;
				if (stat(full.c_str(), &target) == 0)
					e.isDir = S_ISDIR(target.st_mode);
			}
		}
		out->push_back(std::move(e));
	}
	closedir(dir);
	return true;
}

bool IsDirectory(const std::string& path) {
	struct stat st;
	if (stat(NormalizePath(path).c_str(), &st) != 0)
		return false;
	return S_ISDIR(st.st_mode);
}

#endif

}  // namespace fnf
