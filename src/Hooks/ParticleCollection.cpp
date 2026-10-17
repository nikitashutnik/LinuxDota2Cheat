#include "Hooks.h"

typedef CParticleCollection* (* CreateParticleCollectionFn)( CParticleSystemMgr*, WeakHandleContents_InfoForResourceTypeIParticleSystemDefinition*, void*, IParticleSystemQuery*, bool, float, int );

CParticleCollection* Hooks::CreateParticleCollection( CParticleSystemMgr* thisptr, WeakHandleContents_InfoForResourceTypeIParticleSystemDefinition* binding, void* unk, IParticleSystemQuery* query, bool bUnk, float fUnk, int iUnk )
{
    CParticleCollection* collection = particleSystemVMT->GetOriginalMethod<CreateParticleCollectionFn>( kCreateParticleCollectionSlot )( thisptr, binding, unk, query, bUnk, fUnk, iUnk );

    const char* name = ( binding && binding->info ) ? binding->info->name : nullptr; // same null checks as CResourceSystem::GetResourceName
    if( name )
        NetParticles::OnCollectionCreated( name );
    return collection;
}
