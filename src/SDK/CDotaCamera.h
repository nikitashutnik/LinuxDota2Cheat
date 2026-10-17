#pragma once

#include <cstdint>

// CDOTA_Camera. Field offsets come from the Panorama GameUI script bindings:
// SetCameraPitchMax +0x2d8, SetCameraPitchMin +0x2dc, SetCameraYaw +0x2e0, SetCameraDistance +0x2e4
class CDotaCamera
{
public:
	void SetDistance( float dist )
	{
		*(float*)( (uintptr_t)this + 0x2e4 ) = dist;
	}

	float GetDistance()
	{
		return *(float*)( (uintptr_t)this + 0x2e4 );
	}
};

// GDOTADefaultCamera() - the function every camera script binding calls first
typedef CDotaCamera* (* GetCameraFn)( void );
inline GetCameraFn GetCurrentCamera;
