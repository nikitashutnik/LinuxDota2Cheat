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

// The lazy ability-definition loader (see CDotaAbility.h): reads the entity's name, looks the definition up and stores it at
// +0x770. The build has two byte-identical copies of it; either one works.
static bool FindAbilityDefinitionLoader()
{
	uintptr_t fn = PatternFinder::FindPatternInModule( "libclient.so",
		"55 48 89 E5 41 54 53 48 89 FB 48 83 EC 10 48 8B 47 10 48 8B 70 18 48 8D 05 ?? ?? ?? ?? 48 85 F6 48 0F 45 C6 49 89 C4 E8 ?? ?? ?? ?? "
		"80 B8 ?? ?? 00 00 00 75 ?? 80 B8 ?? ?? 00 00 00 74 ?? 48 8D B8 ?? ?? 00 00 4C 89 E6 E8 ?? ?? ?? ?? 48 85 C0 48 89 83 70 07 00 00",
		"ability definition loader" );
	if( !fn )
		return false;

	LoadAbilityDefinition = reinterpret_cast<LoadAbilityDefinitionFn>( fn );
	return true;
}

// C_BaseEntity::GetAbsOrigin (what Entities.GetAbsOrigin calls): node = this->m_pGameSceneNode (+0x4a0); if node+0x120 is set,
// recompute the transform; return &node->m_vecAbsOrigin (+0xd8). Masking the offsets would also match the abs-angles getter.
static bool FindEntityGetAbsOrigin()
{
	uintptr_t fn = PatternFinder::FindPatternInModule( "libclient.so",
		"55 48 89 E5 53 48 83 EC 08 48 8B 9F A0 04 00 00 80 BB 20 01 00 00 00 75 17 48 8D 83 D8 00 00 00 48 8B 5D F8 C9 C3",
		"C_BaseEntity::GetAbsOrigin" );
	if( !fn )
		return false;

	EntityGetAbsOrigin = reinterpret_cast<EntityGetAbsOriginFn>( fn );
	return true;
}

bool Scanner::FindAllSigs()
{
	bool ok = true;

	bool r;
	r = FindCamera();      Logger::Log( "sig GDOTADefaultCamera: %s", r ? "ok" : "FAIL" ); ok &= r;
	r = FindEntityList();  Logger::Log( "sig entity chunk list: %s (%p)", r ? "ok" : "FAIL", (void*)g_ppEntityChunks ); ok &= r;
	r = FindDBPlayPanel(); Logger::Log( "sig g_DBPlayPanel: %s (%p)", r ? "ok" : "FAIL", (void*)g_ppDBPlayPanel ); ok &= r;
	// not fatal: without it, abilities the game hasn't looked at yet are left out of the cooldown rows
	r = FindAbilityDefinitionLoader(); Logger::Log( "sig ability definition loader: %s (%p)", r ? "ok" : "FAIL", (void*)LoadAbilityDefinition );
	// not fatal: without it the rows read m_vecAbsOrigin directly and may lag behind moving heroes
	r = FindEntityGetAbsOrigin(); Logger::Log( "sig C_BaseEntity::GetAbsOrigin: %s (%p)", r ? "ok" : "FAIL", (void*)EntityGetAbsOrigin );

	return ok;
}
