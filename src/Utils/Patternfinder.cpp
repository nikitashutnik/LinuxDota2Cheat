#include "Patternfinder.h"

#include <link.h>
#include <vector>
#include <cstring>
#include "../Interfaces.h"

static std::vector<dlinfo_t> libraries;

static void BuildLibraryList() {
    if (!libraries.empty()) return;

    dl_iterate_phdr([] (struct dl_phdr_info* info, size_t, void*) {
        dlinfo_t lib = {};
        lib.library = info->dlpi_name;

        for (int i = 0; i < info->dlpi_phnum; i++) {
            if (info->dlpi_phdr[i].p_type != PT_LOAD)
                continue;
            MemSegment seg;
            seg.address = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
            seg.size = info->dlpi_phdr[i].p_memsz;
            lib.segments.push_back(seg);
        }

        libraries.push_back(std::move(lib));
        return 0;
    }, nullptr);
}

static bool GetLibrarySegments(const char* library, std::vector<MemSegment>** segs) {
    BuildLibraryList();

    for (auto& current : libraries) {
        if (!strcasestr(current.library, library))
            continue;
        *segs = &current.segments;
        return true;
    }
    return false;
}

inline bool Compare(const unsigned char* pData, const unsigned char* bMask, const char* szMask)
{
    for (; *szMask; ++szMask, ++pData, ++bMask)
        if (*szMask == 'x' && *pData != *bMask)
            return false;

    return (*szMask) == 0;
}

uintptr_t PatternFinder::FindPattern(uintptr_t dwAddress, uintptr_t dwLen, unsigned char* bMask, const char* szMask)
{
	for (uintptr_t i = 0; i < dwLen; i++)
		if (Compare((unsigned char*)(dwAddress + i), bMask, szMask))
			return (uintptr_t)(dwAddress + i);

	return 0;
}

uintptr_t PatternFinder::FindPatternInModule(const char* moduleName, const char* signature, const char* why)
{
	std::vector<MemSegment>* segs = nullptr;
    size_t sigLen = strlen(signature);

    unsigned int bMaskIndex = 0;
    unsigned int szMaskIndex = 0;

    unsigned char *bMask;
    char *szMask;
    char byteBuffer[3] = { 0 };

	if (!GetLibrarySegments(moduleName, &segs)){
		MC_PRINTF_ERROR("Could Not Get info for Module %s\n", moduleName);
		return 0;
	}

    bMask = new unsigned char[sigLen]();
    szMask = new char[sigLen]();

    for( size_t i = 0; i < sigLen; i++ ){
        if( signature[i] == ' ' ){
            szMaskIndex++;
            continue;
        }

        if( signature[i] == '?' ){
            szMask[szMaskIndex] = '?';
        } else {
            szMask[szMaskIndex] = 'x';
        }

        if( signature[i+1] == ' ' || signature[i+1] == '\0' ){
            byteBuffer[0] = signature[i - 1];
            byteBuffer[1] = signature[i];
            bMask[bMaskIndex] = (unsigned char)strtoul( byteBuffer, nullptr, 16 );
            bMaskIndex++;
        }
    }

    // scan each PT_LOAD segment individually — no unmapped holes
    uintptr_t ret = 0;
    for (const auto& seg : *segs) {
        ret = FindPattern(seg.address, seg.size, bMask, szMask);
        if (ret) break;
    }

	if( !ret )
		MC_PRINTF_ERROR("Could not find pattern for %s\n", why );

    delete[] bMask;
    delete[] szMask;
	return ret;
}
