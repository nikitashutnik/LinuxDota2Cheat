#include "Scanner.h"

#include "Interfaces.h"
#include "Utils/Patternfinder.h"

// GDOTADefaultCamera(): the function every Panorama GameUI camera binding (SetCameraYaw, SetCameraDistance, ...) calls first
static bool FindCamera()
{
	uintptr_t fn = PatternFinder::FindPatternInModule( "libclient.so",
		"55 BF FF FF FF FF 48 89 E5 53 48 83 EC 08 E8 ?? ?? ?? ?? 48 85 C0 74 ?? 48 89 C7 48 8B 00 FF 90 28 01 00 00",
		"GDOTADefaultCamera" );
	if( !fn )
		return false;

	GetCurrentCamera = reinterpret_cast<GetCameraFn>( fn );
	return true;
}

// Inlined CHandle::Get(): `mov rcx,[rip+g_pEntityChunks] ; ... shr rdx,9 ; and edx,0x3f ; mov rdx,[rcx+rdx*8]`
static bool FindEntityList()
{
	uintptr_t site = PatternFinder::FindPatternInModule( "libclient.so",
		"48 8B 0D ?? ?? ?? ?? 74 ?? 48 85 C9 74 ?? 48 89 C2 48 C1 EA 09 83 E2 3F 48 8B 14 D1 48 85 D2 74",
		"entity chunk list" );
	if( !site )
		return false;

	g_ppEntityChunks = reinterpret_cast<CEntityIdentities***>( GetAbsoluteAddress( site, 3, 7 ) );
	return true;
}

// CDOTA_DB_Play stores `this` into a global right before loading "layout/play.xml":
//   mov [rip+g_DBPlayPanel], rbx ; call operator new ; mov rdi, rax ; mov r12, rax
static bool FindDBPlayPanel()
{
	uintptr_t line = PatternFinder::FindPatternInModule( "libclient.so",
		"48 89 1D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 89 C7 49 89 C4",
		"DBPlay panel" );
	if( !line )
		return false;

	g_ppDBPlayPanel = reinterpret_cast<panorama::CPanel2D**>( GetAbsoluteAddress( line, 3, 7 ) );
	return true;
}

bool Scanner::FindAllSigs()
{
	bool ok = true;

	bool r;
	r = FindCamera();      Logger::Log( "sig GDOTADefaultCamera: %s", r ? "ok" : "FAIL" ); ok &= r;
	r = FindEntityList();  Logger::Log( "sig entity chunk list: %s (%p)", r ? "ok" : "FAIL", (void*)g_ppEntityChunks ); ok &= r;
	r = FindDBPlayPanel(); Logger::Log( "sig g_DBPlayPanel: %s (%p)", r ? "ok" : "FAIL", (void*)g_ppDBPlayPanel ); ok &= r;

	return ok;
}
