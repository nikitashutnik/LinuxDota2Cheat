#include "Zoom.h"

#include "../Interfaces.h"
#include "../Settings.h"

void Zoom::Tick()
{
    if( Settings::camDistance <= 0.0f || !GetCurrentCamera )
        return;

    CDotaCamera* camera = GetCurrentCamera();

    static float lastLogged = 0.0f;
    if( lastLogged != Settings::camDistance ){
        lastLogged = Settings::camDistance;
        Logger::Log( "Zoom: camera %p, want %g, has %g", (void*)camera, Settings::camDistance, camera ? camera->GetDistance() : 0.0f );
    }

    if( camera )
        camera->SetDistance( Settings::camDistance );
}

void Zoom::Reset()
{
    if( Settings::camDistance <= 0.0f || !GetCurrentCamera )
        return;

    if( CDotaCamera* camera = GetCurrentCamera() )
        camera->SetDistance( 1200.0f );
}
