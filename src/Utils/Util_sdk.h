#pragma once

#include "../SDK/IInputSystem.h" //ButtonCode
#include "../SDK/Datamap.h"

#include <cstdint>

enum GFX_API
{
    UNKNOWN = -1,
    OPENGL = 0,
    VULKAN = 1,
    DIRECTX = 2,
};

namespace Util
{
	ButtonCode_t GetButtonCode(const char* buttonName);

	int FindDataMapElementOffset( Datamap *dMap, const char *element );

	GFX_API GetGraphicsApiType(void);
}
