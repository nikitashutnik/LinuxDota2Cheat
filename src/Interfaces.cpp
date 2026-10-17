#include "Interfaces.h"

bool Interfaces::FindExportedInterfaces()
{
	client         = GetInterface<CSource2Client>( "../../dota/bin/linuxsteamrt64/libclient.so", "Source2Client002" );
	inputSystem    = GetInterface<IInputSystem>( "./libinputsystem.so", "InputSystemVersion001" );
	panoramaEngine = GetInterface<IPanoramaUIEngine>( "./libpanorama.so", "PanoramaUIEngine001" );
	particleSystemMgr = GetInterface<CParticleSystemMgr>( "./libparticles.so", "ParticleSystemMgr003" );
	networkMessages   = GetInterface<CNetworkMessages>( "./libnetworksystem.so", "NetworkMessagesVersion001" );

	for( auto& missing : requestedInterfaces )
		MC_PRINTF_ERROR( "Interface %s is NULL!\n", missing.name );

	return requestedInterfaces.empty();
}
