#pragma once

#include "SDK/SDK.h"
#include "Utils/Vmt.h"
#include "Utils/Logger.h"

#include <memory>

// Interfaces
inline CSource2Client* client;
inline IInputSystem* inputSystem;
inline IPanoramaUIEngine* panoramaEngine;
inline CParticleSystemMgr* particleSystemMgr;
inline CNetworkMessages* networkMessages;

// Address of a game global; the game assigns it at runtime, so deref on use
inline panorama::CPanel2D** g_ppDBPlayPanel;

// VMTs
inline std::unique_ptr<VMT> clientVMT;
inline std::unique_ptr<VMT> uiEngineVMT;
inline std::unique_ptr<VMT> particleSystemVMT;
inline std::unique_ptr<VMT> networkMessagesVMT;

namespace Interfaces
{
	bool FindExportedInterfaces();
}
