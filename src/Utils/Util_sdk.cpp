#include "Util_sdk.h"

#include "../Interfaces.h"

#include <link.h>

ButtonCode_t Util::GetButtonCode(const char* buttonName)
{
	for (int i = 0; i < ButtonCode_t::LAST_KEY_VALUE; i++)
	{
		const char* currentButton = inputSystem->CodeToString((ButtonCode_t) i);
		if (strcmp(currentButton, buttonName) == 0)
			return (ButtonCode_t) i;
	}

	return ButtonCode_t::BUTTON_CODE_INVALID;
}


int Util::FindDataMapElementOffset( Datamap *dMap, const char *element ) {
    while( dMap ){
        for( uint64_t i = 0; i < dMap->numFields; i++ ){
            if( !dMap->dataDesc[i].fieldName )
                continue;

            if( !strcmp(dMap->dataDesc[i].fieldName, element) )
                return dMap->dataDesc[i].fieldOffset[TD_OFFSET_NORMAL];

            if( dMap->dataDesc[i].type == FIELD_EMBEDDED ){
                if( dMap->dataDesc[i].td ){

                    int temp = Util::FindDataMapElementOffset( dMap->dataDesc[i].td, element );
                    if( temp != 0 )
                        return temp;
                }
            }
        }
        dMap = dMap->baseMap;
    }


    return 0;
}

GFX_API Util::GetGraphicsApiType() {
    static GFX_API ret = GFX_API::UNKNOWN;

    dl_iterate_phdr([] (struct dl_phdr_info* info, size_t, void*) {
        /* Only one of the rendersystem dll's will be loaded at once. */
        if( strstr(info->dlpi_name, "librendersystemgl") ){
            ret = GFX_API::OPENGL;
        }
        if( strstr(info->dlpi_name, "librendersystemvulkan") ){
            ret = GFX_API::VULKAN;
        }

        return 0;
    }, nullptr);

    return ret;
}
