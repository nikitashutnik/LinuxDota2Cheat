#pragma once
#include "../SDK/SDK.h"
#include "../Interfaces.h"

// CUIEngine::RunScript (compile+run), UIEngine vtable slot 78. Only these 5 arguments exist in the current build.
typedef int (* RunScriptFn)( panorama::UIEngine* thisptr, panorama::IUIPanel* panel, const char* code, const char* path, uint64_t arg5 );
constexpr int kRunScriptSlot = 78;

// CParticleSystemMgr (ParticleSystemMgr003) vtable: 17 CreateParticleCollection( name ), 18 CreateParticleCollection( handle ),
// 19 DestroyParticleCollection. Verified in the Oct 2026 libparticles.so: slot 17 passes the binding FindParticleSystem returns
// straight to slot 18 (the handle is the binding pointer, by value), and CResourceSystem::GetResourceName (ResourceSystem013
// slot 34) reads the name as *(const char**)binding->info, i.e. binding->info->name.
constexpr int kCreateParticleCollectionSlot = 18;

// CNetworkMessages (NetworkMessagesVersion001) slot 3: UnserializeMessage( bf_read&, CNetMessagePB* msg, CUtlString* error ). The stream
// reader (slot 5) calls it through the vtable for every received message, with the stream at the message's size varint.
// CNetChan::PostReceivedNetMessage is no longer virtual in this build, so this is the one place every message passes through.
constexpr int kUnserializeMessageSlot = 3;

namespace Hooks
{
    // client
    void FrameStageNotify( CSource2Client* thisptr, ClientFrameStage_t stage );
    // panorama
    int RunScript( panorama::UIEngine* thisptr, panorama::IUIPanel* panel, const char* code, const char* path, uint64_t arg5 );
    // The UI engine is created some time after startup; hooks it once it exists (safe to call every frame)
    bool EnsureUIEngineHook();
    // particles
    CParticleCollection* CreateParticleCollection( CParticleSystemMgr* thisptr, WeakHandleContents_InfoForResourceTypeIParticleSystemDefinition* binding, void* unk, IParticleSystemQuery* query, bool bUnk, float fUnk, int iUnk );
    // net messages
    bool UnserializeMessage( CNetworkMessages* thisptr, bf_read* buf, void* msg, void* err );

    // Last HUD panel the game compiled a script for; the in-game HUD is a separate panel tree from the dashboard
    inline panorama::IUIPanel* hudPanel = nullptr;
}

// Networked particles (CUserMsg_ParticleManager) matched to the collections the client builds for them
namespace NetParticles
{
    void OnCollectionCreated( const char* name );
}
