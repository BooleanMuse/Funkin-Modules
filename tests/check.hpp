#pragma once
// A test harness small enough to read in one sitting. No framework: these tests
// run on the desktop with plain g++, so that the engine can be checked without
// opening Rack.
#include <cmath>
#include <cstdio>
#include <string>

namespace check {

inline int& failures() {
	static int n = 0;
	return n;
}
inline int& checks() {
	static int n = 0;
	return n;
}

// How a value is printed when a check fails. std::to_string alone cannot take a
// std::string, and comparing names is half of what these tests do.
inline std::string show(const std::string& s) {
	return "\"" + s + "\"";
}
inline std::string show(const char* s) {
	return std::string("\"") + (s ? s : "(null)") + "\"";
}
template <typename T>
inline std::string show(const T& v) {
	return std::to_string(v);
}

inline void report(bool ok, const char* file, int line, const char* expr, const std::string& detail) {
	checks()++;
	if (ok)
		return;
	failures()++;
	std::printf("  FAIL %s:%d  %s\n", file, line, expr);
	if (!detail.empty())
		std::printf("       %s\n", detail.c_str());
}

inline int summary(const char* name) {
	std::printf("%s: %d checks, %d failed\n", name, checks(), failures());
	return failures() == 0 ? 0 : 1;
}

}  // namespace check

#define CHECK(expr) check::report((expr), __FILE__, __LINE__, #expr, "")

#define CHECK_EQ(a, b)                                                                    \
	do {                                                                                  \
		auto va_ = (a);                                                                   \
		auto vb_ = (b);                                                                   \
		check::report(va_ == vb_, __FILE__, __LINE__, #a " == " #b,                       \
		              "got " + check::show(va_) + ", expected " + check::show(vb_));      \
	} while (0)

#define CHECK_NEAR(a, b, eps)                                                             \
	do {                                                                                  \
		double va_ = (double) (a);                                                        \
		double vb_ = (double) (b);                                                        \
		check::report(std::fabs(va_ - vb_) <= (eps), __FILE__, __LINE__, #a " ~= " #b,    \
		              "got " + std::to_string(va_) + ", expected " + std::to_string(vb_)); \
	} while (0)

#define SECTION(name) std::printf("- %s\n", name)
