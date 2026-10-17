#pragma once
#include <cstring>
#include <dlfcn.h>
#include <vector>
#include "ConMsg.h"


struct InterfaceMetaData
{
	const char* name;
	uint32_t numVMs;
	void* interface;
};

inline std::vector<InterfaceMetaData> requestedInterfaces = {}; // requested but failed
inline std::vector<InterfaceMetaData> grabbedInterfaces = {}; // grabbed and can be used

typedef void* (*InstantiateInterfaceFn) ();

struct InterfaceReg
{
	InstantiateInterfaceFn m_CreateFn;
	const char *m_pName;
	InterfaceReg *m_pNext;
};

inline void**& getvtable(void* inst, size_t offset = 0)
{
	return *reinterpret_cast<void***>((size_t)inst + offset);
}

template< typename Fn >
inline Fn getvfunc( void* inst, size_t index, size_t offset = 0 ) {
	return reinterpret_cast<Fn>(getvtable( inst, offset )[index]);
}

template <typename interface>
interface* GetInterface(const char* filename, const char* version, uint32_t expectedVMs = 0)
{
	InterfaceMetaData data = {};
	data.name = version;
	data.numVMs = expectedVMs;

	void* library = dlopen(filename, RTLD_NOLOAD | RTLD_NOW | RTLD_LOCAL);

	if ( !library ){
		ConMsg( "%s: Could not open library: %s\n", __func__, filename );
		requestedInterfaces.push_back(data);
		return NULL;
	}

	auto createInterface = reinterpret_cast<void*(*)(const char*, int*)>(dlsym( library, "CreateInterface" ));

	if ( !createInterface ) {
		dlclose(library);
		ConMsg( "%s: Could not find \"CreateInterface\" for library: %s\n", __func__, filename );
		requestedInterfaces.push_back(data);
		return NULL;
	}

	int returnCode = 0;
	void* result = createInterface(version, &returnCode);
	dlclose(library);

	if( !result ){
		ConMsg( "%s: Could not find Interface Named: %s in library %s\n", __func__, version, filename );
		requestedInterfaces.push_back(data);
		return NULL;
	}

	data.interface = result;
	grabbedInterfaces.push_back(data);
	return reinterpret_cast<interface*>(result);
}

inline uintptr_t GetAbsoluteAddress(uintptr_t instruction_ptr, int offset, int size)
{
	return instruction_ptr + *reinterpret_cast<int32_t*>(instruction_ptr + offset) + size;
}

template <typename T>
T GetSymbolAddress(const char* filename, const char* symbol)
{
	void* handle = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
	T result = reinterpret_cast<T>(dlsym(handle, symbol));
	dlclose(handle);

	return result;
}