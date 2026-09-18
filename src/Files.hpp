#pragma once
// ============================================================================
// Walking a folder, on all three operating systems, without <filesystem>.
//
// This exists because of one line in libc++: std::filesystem is marked
// unavailable below macOS 10.15, and Rack targets older than that. A plugin
// that uses it does not merely warn on the VCV build farm's macOS job — it
// fails to compile. MinGW and older libstdc++ have their own versions of the
// same argument (libstdc++fs, and whether it needs linking), so the whole
// dependency is better gone.
//
// What replaces it is small because the plugin asks very little: list a folder,
// say whether something is a folder, and chop a path into its parts. The path
// handling is plain string work, which is portable by construction — and on
// Windows the listing converts UTF-16 to UTF-8 itself, which is more correct
// than std::filesystem::path::string() ever was, since that one goes through the
// machine's local code page and mangles any mod folder named outside it.
// ============================================================================
#include <string>
#include <vector>

namespace fnf {

struct DirEntry {
	std::string name;  // the entry's own name, not a path
	bool isDir = false;
	bool isSymlink = false;
};

/** The entries of one folder, in no particular order. False if it could not be
opened at all, which is a different thing from it being empty. */
bool ListDirectory(const std::string& path, std::vector<DirEntry>* out);

bool IsDirectory(const std::string& path);

/** Joins with a forward slash, which every one of the three accepts — Windows
included, all the way down to its own API. */
std::string JoinPath(const std::string& dir, const std::string& name);

/** "a/b/c.png" -> "c.png" */
std::string FileName(const std::string& path);
/** "a/b/c.png" -> "c" */
std::string FileStem(const std::string& path);
/** "a/b/c.png" -> ".png", and "" when there is no dot in the last component */
std::string FileExtension(const std::string& path);
/** "a/b/c.png" -> "a/b" */
std::string ParentPath(const std::string& path);

/** Both separators become "/", and any trailing one is dropped. Everything
inside this plugin compares paths as strings, so they all have to be spelled the
same way first. */
std::string NormalizePath(const std::string& path);

}  // namespace fnf
