#include "GUI/Gui.h"
#include "Hooks/Hooks.h"
#include "Interfaces.h"
#include "Scanner.h"
#include "Hacks/Zoom.h"
#include "Utils/Logger.h"
#include "Settings.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <dlfcn.h> //dlopen
#include <link.h> // link map
#include <signal.h>

struct sigaction sa;
struct sigaction oldSa;

void *mcPrev, *mcCurr, *mcNext;

/* We need to restore the linkmap before our unloading will work.
 * The unload script will send a signal that we intercept here */
static void RestoreLinkMapEntry( int sigNum, siginfo_t *si, void * uContext )
{
    if( mcPrev )
    {
        auto *previousEntry = reinterpret_cast<link_map*>(mcPrev);
        previousEntry->l_next = reinterpret_cast<link_map*>(mcCurr);
    }
    if( mcNext )
    {
        auto *nextEntry = reinterpret_cast<link_map*>(mcNext);
        nextEntry->l_prev = reinterpret_cast<link_map*>(mcCurr);
    }
}

/* Removes library from dynamic library linked list */
static void RemoveLinkMapEntry()
{
    auto *map = reinterpret_cast<struct link_map*>(dlopen(nullptr, RTLD_NOW));
    map = map->l_next->l_next;
    while (map) {
        if( strstr( map->l_name, "libDota2Helper.so" ) != nullptr ){
            mcPrev = map->l_prev;
            mcCurr = map;
            mcNext = map->l_next;
            if( map->l_prev ){
                map->l_prev->l_next = map->l_next;
            }
            if( map->l_next ){
                map->l_next->l_prev = map->l_prev;
            }
            return;
        }
        map = map->l_next;
    }
}

/* Entrypoint to the Library. Called when loading */
int __attribute__((constructor)) Startup()
{
    remove( Logger::path );
    Logger::Log( "Startup" );
    Settings::Load();

    /* Setup new Signal Handler */
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = RestoreLinkMapEntry;
    sigaction(SIGXCPU, &sa, &oldSa);

    if( !Interfaces::FindExportedInterfaces() ){
        Logger::Log( "FindExportedInterfaces failed, stopping" );
        return 1;
    }
    if( Schema::Init() ){
        std::string dump = std::string( getenv( "HOME" ) ? getenv( "HOME" ) : "/tmp" ) + "/.config/dota2helper_schema.txt";
        Schema::DumpAll( dump.c_str() );
    } else {
        Logger::Log( "schema system: FAILED (last-seen disabled)" );
    }

    if( !Scanner::FindAllSigs() ){
        MC_PRINTF_ERROR( "Failed to find one of the signatures. Stopping...\n" );
        return 2;
    }

    clientVMT = std::unique_ptr<VMT>( new VMT( client ) );
    clientVMT->HookVM( Hooks::FrameStageNotify, 36 );
    clientVMT->ApplyVMT();

    particleSystemVMT = std::unique_ptr<VMT>( new VMT( particleSystemMgr ) );
    particleSystemVMT->HookVM( Hooks::CreateParticleCollection, kCreateParticleCollectionSlot );
    particleSystemVMT->ApplyVMT();

    networkMessagesVMT = std::unique_ptr<VMT>( new VMT( networkMessages ) );
    networkMessagesVMT->HookVM( Hooks::UnserializeMessage, kUnserializeMessageSlot );
    networkMessagesVMT->ApplyVMT();

    if( !Hooks::EnsureUIEngineHook() )
        Logger::Log( "UI engine not created yet, hooking it from FrameStageNotify later" );

    MC_PRINTF( "Loaded. Press END for the menu.\n" );

    RemoveLinkMapEntry();
    return 0;
}

/* Called when un-injecting the library */
void __attribute__((destructor)) Shutdown()
{
    Zoom::Reset();
    if( uiEngineVMT )
        UI::Destroy();

    uiEngineVMT.reset();
    networkMessagesVMT.reset();
    particleSystemVMT.reset();
    clientVMT.reset();

    MC_PRINTF( "Unloaded.\n" );

    /* Restore previous signal handler */
    sigaction(SIGXCPU, &oldSa, nullptr );
}
