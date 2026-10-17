#include "Hooks.h"
#include "../Settings.h"

// Runs in the accept-match popup's own script scope: the popup owns the timer, so a closed popup just cancels it
static const char* acceptMatchJS =
    "$.Schedule(0.4, function(){ $.DispatchEvent('DOTAPlayAcceptMatch', $.GetContextPanel()); });";

int Hooks::RunScript( panorama::UIEngine* thisptr, panorama::IUIPanel* panel, const char* code, const char* path, uint64_t arg5 )
{
    auto original = uiEngineVMT->GetOriginalMethod<RunScriptFn>( kRunScriptSlot );

    if( path && ( strstr( path, "scripts/hud" ) || strstr( path, "/hud_" ) ) )
        hudPanel = panel;

    static int logged = 0;
    if( logged < 8 || ( path && strstr( path, "popup" ) ) ){
        logged++;
        Logger::Log( "RunScript panel=%p path=%s arg5=%llu", (void*)panel, path ? path : "(null)", (unsigned long long)arg5 );
    }

    int ret = original( thisptr, panel, code, path, arg5 );

    if( Settings::autoAccept && path && strstr( path, "popup_accept_match" ) ){
        MC_PRINTF( "Match found - auto accepting\n" );
        original( thisptr, panel, acceptMatchJS, path, 1 ); // arg5 0 would rerun the popup's own script, cached under this path
    }

    return ret;
}

bool Hooks::EnsureUIEngineHook()
{
    if( uiEngineVMT )
        return true;
    panorama::UIEngine* ui = panoramaEngine->AccessUIEngine();
    if( !ui )
        return false;

    uiEngineVMT = std::unique_ptr<VMT>( new VMT( ui ) );
    uiEngineVMT->HookVM( Hooks::RunScript, kRunScriptSlot );
    uiEngineVMT->ApplyVMT();
    Logger::Log( "UI engine %p hooked", (void*)ui );
    return true;
}
