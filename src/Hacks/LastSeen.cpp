#include "LastSeen.h"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Interfaces.h"
#include "../Settings.h"
#include "../GUI/Gui.h"
#include "../Utils/Util.h"

// Schema offsets for one entity class, resolved once per class through Schema_DynamicBinding()
struct ClassOffsets
{
    int teamNum = -1;
    int sceneNode = -1;
    int absOrigin = -1;      // inside CGameSceneNode
    int dormant = -1;        // m_bDormant, if the class has it
    int simulationTime = -1;
    int replicatingHero = -1; // illusions
    int isLocalController = -1;
    int taggedVisible = -1;
};

static ClassOffsets& OffsetsFor( CSchemaClassBinding* b )
{
    static std::unordered_map<CSchemaClassBinding*, ClassOffsets> cache;
    auto it = cache.find( b );
    if( it != cache.end() )
        return it->second;

    ClassOffsets& o = cache[b];
    SchemaClassInfo info = b->Info();
    if( !info )
        return o;
    o.teamNum = info.FindField( "m_iTeamNum" );
    o.dormant = info.FindField( "m_bDormant" );
    o.simulationTime = info.FindField( "m_flSimulationTime" );
    o.replicatingHero = info.FindField( "m_hReplicatingOtherHeroModel" );
    o.isLocalController = info.FindField( "m_bIsLocalPlayerController" );
    o.taggedVisible = info.FindField( "m_iTaggedAsVisibleByTeam" );

    o.sceneNode = info.FindField( "m_pGameSceneNode" );
    SchemaClassInfo sceneNode = Schema::FindClass( "CGameSceneNode" );
    o.absOrigin = sceneNode.FindField( "m_vecAbsOrigin" );
    o.dormant = sceneNode.FindField( "m_bDormant" ); // dormancy is tracked on the scene node

    Logger::Log( "offsets %s: team +0x%x, sceneNode +0x%x, absOrigin +0x%x, dormant +0x%x, simTime +0x%x, illusion +0x%x, localCtrl +0x%x",
                 info.Name(), o.teamNum, o.sceneNode, o.absOrigin, o.dormant, o.simulationTime, o.replicatingHero, o.isLocalController );
    return o;
}

template<typename T>
static T Read( void* base, int offset ) { return *reinterpret_cast<T*>( reinterpret_cast<char*>( base ) + offset ); }

// C_DOTA_Unit_Hero_BountyHunter -> npc_dota_hero_bounty_hunter; most classes map by lowercasing alone
static std::string NpcName( const char* className )
{
    static const char* splitNames[] = { "BountyHunter", "ChaosKnight", "CrystalMaiden", "DarkSeer", "DarkWillow", "DeathProphet", "DoomBringer",
        "DragonKnight", "DrowRanger", "EarthSpirit", "EmberSpirit", "FacelessVoid", "KeeperOfTheLight", "LoneDruid", "MonkeyKing", "NightStalker",
        "PhantomAssassin", "PhantomLancer", "SandKing", "ShadowShaman", "SkeletonKing", "SpiritBreaker", "StormSpirit", "TemplarAssassin",
        "TrollWarlord", "WitchDoctor" };
    const char* suffix = className + strlen( "C_DOTA_Unit_Hero_" );
    bool split = false;
    for( const char* s : splitNames )
        if( !strcmp( s, suffix ) ){ split = true; break; }

    std::string name = "npc_dota_hero_";
    for( const char* c = suffix; *c; c++ ){
        if( split && c != suffix && *c >= 'A' && *c <= 'Z' && c[-1] >= 'a' && c[-1] <= 'z' )
            name += '_';
        name += (char)( ( *c >= 'A' && *c <= 'Z' ) ? *c + 32 : *c );
    }
    return name;
}

struct Track
{
    std::string npcName;
    int index = 0;
    float x = 0, y = 0, z = 0;
    float lastSimTime = 0;
    long lastUpdateMs = 0;   // last time the entity was being networked (visible)
    bool everSeen = false;
    bool visible = false;
    long lastScanMs = 0;
    uint32_t lastFlags = 0xFFFFFFFF;
    int lastTagged = -1;
    int lastDormant = -2;
    float lastX = 0, lastY = 0;
};

struct Teleport
{
    int index;
    float x, y, z;
    long startMs;
    float channelSec;
    bool ended = false, completed = false;
    long endedMs = 0;
};

static std::mutex teleportGuard; // reported from the net message hook
static std::vector<Teleport> teleports;

void LastSeen::OnTeleport( int entityIndex, float x, float y, float z, long startMs, float channelSec )
{
    std::lock_guard<std::mutex> lock( teleportGuard );
    for( auto it = teleports.begin(); it != teleports.end(); )
        it = ( it->index == entityIndex ) ? teleports.erase( it ) : std::next( it );
    teleports.push_back( { entityIndex, x, y, z, startMs, channelSec } );
}

void LastSeen::OnTeleportEnd( int entityIndex, bool completed )
{
    std::lock_guard<std::mutex> lock( teleportGuard );
    for( Teleport& t : teleports )
        if( t.index == entityIndex && !t.ended ){
            t.ended = true;
            t.completed = completed;
            t.endedMs = Util::GetEpochMs();
        }
}

void LastSeen::Tick()
{
    static long lastMs = 0;
    long now = Util::GetEpochMs();
    if( now - lastMs < 250 )
        return;
    lastMs = now;

    CEntityIdentities** chunks = g_ppEntityChunks ? *g_ppEntityChunks : nullptr;
    if( !chunks )
        return;

    static std::unordered_map<CBaseEntity*, Track> tracks;
    static int localTeam = -1;
    static long lastTeamLog = 0;

    for( int c = 0; c < MAX_ENTITY_LISTS; c++ ){
        CEntityIdentities* chunk = chunks[c];
        if( !chunk )
            continue;

        for( int i = 0; i < MAX_ENTITIES_IN_LIST; i++ ){
            CBaseEntity* entity = chunk->m_pIdentities[i].entity;
            if( !entity )
                continue;
            CSchemaClassBinding* binding = entity->Schema_DynamicBinding();
            if( !binding || !binding->binaryName )
                continue;
            const char* cls = binding->binaryName;

            if( !strcmp( cls, "C_DOTAPlayerController" ) ){
                ClassOffsets& o = OffsetsFor( binding );
                if( o.isLocalController >= 0 && o.teamNum >= 0 && Read<bool>( entity, o.isLocalController ) ){
                    int team = Read<int>( entity, o.teamNum );
                    if( team != localTeam ){
                        localTeam = team;
                        Logger::Log( "local team = %d", localTeam );
                    }
                }
                continue;
            }

            if( strncmp( cls, "C_DOTA_Unit_Hero_", 17 ) != 0 || strstr( cls, "Beastmaster_" ) )
                continue;

            ClassOffsets& o = OffsetsFor( binding );
            if( o.teamNum < 0 || o.sceneNode < 0 || o.absOrigin < 0 )
                continue;
            int team = Read<int>( entity, o.teamNum );
            if( localTeam < 0 || team == localTeam )
                continue;
            if( o.replicatingHero >= 0 && Read<uint32_t>( entity, o.replicatingHero ) != 0xFFFFFFFF )
                continue; // illusion

            Track& t = tracks[entity];
            t.lastScanMs = now;
            if( t.npcName.empty() ){
                t.npcName = NpcName( cls );
                t.index = c * MAX_ENTITIES_IN_LIST + i;
            }

            // diagnostics: which networked signal flips when the enemy enters/leaves the fog
            uint32_t flags = chunk->m_pIdentities[i].flags;
            float sim = o.simulationTime >= 0 ? Read<float>( entity, o.simulationTime ) : 0.0f;
            int tagged = o.taggedVisible >= 0 ? Read<int>( entity, o.taggedVisible ) : -1;
            void* node = Read<void*>( entity, o.sceneNode );
            float px = node ? Read<float>( node, o.absOrigin ) : 0.0f;
            float py = node ? Read<float>( node, o.absOrigin + 4 ) : 0.0f;
            bool moved = px != t.lastX || py != t.lastY;
            int nodeDormant = ( node && o.dormant >= 0 ) ? Read<uint8_t>( node, o.dormant ) : -1;
            if( flags != t.lastFlags || tagged != t.lastTagged || sim != t.lastSimTime || nodeDormant != t.lastDormant )
                Logger::Log( "%s: flags 0x%x tagged 0x%x sim %.2f dormant %d pos (%.0f, %.0f)%s", t.npcName.c_str(), flags, tagged, sim, nodeDormant, px, py, moved ? " moved" : "" );
            t.lastFlags = flags;
            t.lastTagged = tagged;
            t.lastDormant = nodeDormant;

            t.lastSimTime = sim;
            t.lastX = px; t.lastY = py;
            bool dormant = nodeDormant != 0; // CGameSceneNode::m_bDormant; unresolved (-1) counts as never seen

            if( !dormant ){
                t.x = px; t.y = py; t.z = Read<float>( node, o.absOrigin + 8 ); // not dormant implies a scene node (nodeDormant is -1 without one)
                t.everSeen = true;
                t.lastUpdateMs = now; // marker age counts from the last visible tick, not the last move: an idle enemy gets a marker too
            }
            if( t.visible != !dormant ){
                t.visible = !dormant;
                Logger::Log( "%s %s at (%.0f, %.0f), t %ld", t.npcName.c_str(), t.visible ? "visible" : "lost", t.x, t.y, now % 1000000 );
            }
        }
    }

    if( now - lastTeamLog > 30000 ){
        lastTeamLog = now;
        Logger::Log( "lastseen: local team %d, tracking %zu enemy heroes", localTeam, tracks.size() );
    }

    // drop entities that disappeared from the list
    for( auto it = tracks.begin(); it != tracks.end(); )
        it = ( it->second.lastScanMs != now ) ? tracks.erase( it ) : std::next( it );

    std::string marks = "[";
    for( auto& kv : tracks ){
        Track& t = kv.second;
        if( t.visible || !t.everSeen )
            continue;
        float age = ( now - t.lastUpdateMs ) / 1000.0f;
        if( age > Settings::lastSeenSecs )
            continue;
        char buf[160];
        snprintf( buf, sizeof( buf ), "%s{n:'%s',i:%d,x:%.0f,y:%.0f,z:%.0f,a:%.1f}", marks.size() > 1 ? "," : "", t.npcName.c_str(), t.index, t.x, t.y, t.z, age );
        marks += buf;
    }

    {
        std::lock_guard<std::mutex> lock( teleportGuard );
        for( auto it = teleports.begin(); it != teleports.end(); ){
            Teleport& tp = *it;
            Track* track = nullptr;
            for( auto& kv : tracks )
                if( kv.second.index == tp.index )
                    track = &kv.second;
            bool drop = !track || ( tp.ended && !tp.completed ) || ( !tp.ended && now - tp.startMs > (long)( tp.channelSec * 1000 ) + 5000 );
            if( track && tp.ended && tp.completed ){ // arrived: the destination is where the hero was last known to be
                track->x = tp.x;
                track->y = tp.y;
                track->z = tp.z;
                track->lastUpdateMs = tp.endedMs;
                track->everSeen = true;
                drop = true;
            }
            if( drop ){
                it = teleports.erase( it );
                continue;
            }
            char buf[160];
            snprintf( buf, sizeof( buf ), "%s{n:'%s',i:%d,x:%.0f,y:%.0f,z:%.0f,a:%.1f,t:1,c:%.1f}", marks.size() > 1 ? "," : "", track->npcName.c_str(), tp.index + 50000, tp.x, tp.y, tp.z, ( now - tp.startMs ) / 1000.0f, tp.channelSec );
            marks += buf;
            ++it;
        }
    }
    marks += "]";

    UI::PushMarkers( marks.c_str() );
}
