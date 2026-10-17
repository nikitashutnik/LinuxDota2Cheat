#include "Hooks.h"

#include "../Hacks/Zoom.h"
#include "../Hacks/LastSeen.h"
#include "../GUI/Gui.h"
#include "../Utils/Util.h"

typedef void (* FrameStageNotifyFn)( CSource2Client*, ClientFrameStage_t );

static bool Pressed( ButtonCode_t key, bool& wasDown )
{
    bool down = inputSystem->IsButtonDown( key );
    bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

void Hooks::FrameStageNotify( CSource2Client *thisptr, ClientFrameStage_t stage )
{
    // Called once per stage, several times a frame; do our work about once a frame (4 ms: still every frame at 240 fps)
    static long lastMs = 0;
    long now = Util::GetEpochMs();
    if( now - lastMs >= 4 && EnsureUIEngineHook() ){
        lastMs = now;

        static bool endWasDown = false, delWasDown = false;
        if( Pressed( ButtonCode_t::END, endWasDown ) )
            UI::ToggleUI();
        if( Pressed( ButtonCode_t::DELETE, delWasDown ) )
            UI::RunDevScript();

        UI::PollBridge();
        Zoom::Tick();
        LastSeen::Tick(); // also when switched off: its push then carries on:0, which hides the icons
        UI::UpdateGround();
    }

    clientVMT->GetOriginalMethod<FrameStageNotifyFn>( 36 )( thisptr, stage );
}
