#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

struct MemSegment
{
	uintptr_t address = 0;
	size_t size = 0;
};

struct dlinfo_t
{
	const char* library = nullptr;
	std::vector<MemSegment> segments;
};

namespace PatternFinder
{
	uintptr_t FindPattern(uintptr_t dwAddress, uintptr_t dwLen, unsigned char* bMask, const char* szMask);
	uintptr_t FindPatternInModule(const char* moduleName, const char *signature, const char* why = "NO_NAME_PROVIDED");
}
