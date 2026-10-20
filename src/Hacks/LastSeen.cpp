#include "LastSeen.h"

#include <algorithm>
#include <cctype>
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
    int unitName = -1;       // m_iszUnitName, "npc_dota_hero_elder_titan"
    int lifeState = -1;      // m_lifeState, 0 = alive (Entities.IsAlive)
    int abilities = -1;      // m_vecAbilities: count at +0, CHandle* at +8 (C_DOTA_BaseNPC::GetAbilityCount / GetAbility)
    int inventoryItems = -1; // handle of inventory slot s at +4*s (C_DOTA_UnitInventory::GetItem: m_hItems + 4 + 4*s, slots 0-24)
    int health = -1, maxHealth = -1, mana = -1, maxMana = -1; // m_iHealth, m_iMaxHealth (int), m_flMana, m_flMaxMana (float)
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
    o.unitName = info.FindField( "m_iszUnitName" );
    o.lifeState = info.FindField( "m_lifeState" );
    o.abilities = info.FindField( "m_vecAbilities" );
    o.health = info.FindField( "m_iHealth" );
    o.maxHealth = info.FindField( "m_iMaxHealth" );
    o.mana = info.FindField( "m_flMana" );
    o.maxMana = info.FindField( "m_flMaxMana" );
    int inventory = info.FindField( "m_Inventory" ), items = Schema::FindClass( "C_DOTA_UnitInventory" ).FindField( "m_hItems" );
    if( inventory >= 0 && items >= 0 )
        o.inventoryItems = inventory + items + 4;

    o.sceneNode = info.FindField( "m_pGameSceneNode" );
    SchemaClassInfo sceneNode = Schema::FindClass( "CGameSceneNode" );
    o.absOrigin = sceneNode.FindField( "m_vecAbsOrigin" );
    o.dormant = sceneNode.FindField( "m_bDormant" ); // dormancy is tracked on the scene node

    Logger::Log( "offsets %s: team +0x%x, sceneNode +0x%x, absOrigin +0x%x, dormant +0x%x, simTime +0x%x, illusion +0x%x, localCtrl +0x%x, health +0x%x/+0x%x, mana +0x%x/+0x%x",
                 info.Name(), o.teamNum, o.sceneNode, o.absOrigin, o.dormant, o.simulationTime, o.replicatingHero, o.isLocalController, o.health, o.maxHealth, o.mana, o.maxMana );
    return o;
}

template<typename T>
static T Read( void* base, int offset ) { return *reinterpret_cast<T*>( reinterpret_cast<char*>( base ) + offset ); }

// CHandle -> entity, as the game's inlined CHandle::Get(): 15 index bits pick chunk and slot, and the identity must hold the same handle
static CBaseEntity* FromHandle( CEntityIdentities** chunks, uint32_t h )
{
    if( h == 0xFFFFFFFF || h == 0xFFFFFFFE )
        return nullptr;
    CEntityIdentities* chunk = chunks[( h & 0x7fff ) / MAX_ENTITIES_IN_LIST];
    if( !chunk )
        return nullptr;
    CEntityIdentity& id = chunk->m_pIdentities[h % MAX_ENTITIES_IN_LIST];
    return (uint32_t)id.entHandle == h ? id.entity : nullptr;
}

struct AbilityOffsets
{
    int level, maxLevelOverride, hidden, cooldown;   // C_DOTABaseAbility; m_fCooldown is the time left (GetCooldownTimeRemaining returns it)
    int charges, secondaryCharges;                   // C_DOTA_Item
    bool Valid() const { return level >= 0 && maxLevelOverride >= 0 && hidden >= 0 && cooldown >= 0 && charges >= 0 && secondaryCharges >= 0; }
};

static const AbilityOffsets& Ability()
{
    static const AbilityOffsets a = []{
        SchemaClassInfo ab = Schema::FindClass( "C_DOTABaseAbility" ), item = Schema::FindClass( "C_DOTA_Item" );
        AbilityOffsets r = { ab.FindField( "m_iLevel" ), ab.FindField( "m_nMaxLevelOverride" ), ab.FindField( "m_bHidden" ), ab.FindField( "m_fCooldown" ),
                             item.FindField( "m_iCurrentCharges" ), item.FindField( "m_iSecondaryCharges" ) };
        Logger::Log( "ability offsets: level +0x%x, maxLevelOverride +0x%x, hidden +0x%x, cooldown +0x%x, charges +0x%x/+0x%x",
                     r.level, r.maxLevelOverride, r.hidden, r.cooldown, r.charges, r.secondaryCharges );
        return r;
    }();
    return a;
}

// The ability's (or item's) KV definition, attached on first use like every C_DOTABaseAbility getter does
static void* Definition( CBaseEntity* ability )
{
    void* def = Read<void*>( ability, kAbilityDefinition );
    if( !def && LoadAbilityDefinition ){
        LoadAbilityDefinition( ability, 0 );
        def = Read<void*>( ability, kAbilityDefinition );
    }
    return def;
}

static const char* DefinitionName( CBaseEntity* ability )
{
    void* def = ability ? Definition( ability ) : nullptr;
    return def ? Read<const char*>( def, 0 ) : nullptr;
}

static bool JsIdentifier( const char* s ) // goes into a quoted JS string
{
    if( !s || !*s )
        return false;
    for( ; *s; s++ )
        if( !isalnum( (unsigned char)*s ) && *s != '_' )
            return false;
    return true;
}

struct Shown
{
    int index;
    CBaseEntity* entity;
    uint32_t handle;
    int sceneNode, absOrigin;
};
static std::vector<Shown> shown; // heroes of the last units push; Positions() re-reads where they are

// An enemy hero in sight and alive, for the overlay: {i:index,x,y,z,ab:[[name,level,maxLevel,cooldownLeft],...],w:[observers,sentries]}
// with the abilities the HUD shows (not hidden, not passive: talents and the attribute bonus are passive) and the wards in
// inventory and backpack
static void AppendUnit( std::string& out, CEntityIdentities** chunks, CBaseEntity* npc, uint32_t handle, int index, const ClassOffsets& o )
{
    const AbilityOffsets& a = Ability();
    void* node = Read<void*>( npc, o.sceneNode );
    if( !node || o.dormant < 0 || Read<uint8_t>( node, o.dormant ) || o.lifeState < 0 || Read<uint8_t>( npc, o.lifeState ) != 0 || o.abilities < 0 || !a.Valid() )
        return; // in the fog (the client keeps stale data) or dead

    char buf[256];
    snprintf( buf, sizeof( buf ), "%s{i:%d,x:%.0f,y:%.0f,z:%.0f,ab:[", out.size() > 1 ? "," : "", index,
              Read<float>( node, o.absOrigin ), Read<float>( node, o.absOrigin + 4 ), Read<float>( node, o.absOrigin + 8 ) );
    out += buf;
    int count = Read<int>( npc, o.abilities );
    uint32_t* handles = Read<uint32_t*>( npc, o.abilities + 8 );
    for( int k = 0, n = 0; handles && k < count && k < 64; k++ ){
        CBaseEntity* ab = FromHandle( chunks, handles[k] );
        const char* name = DefinitionName( ab );
        if( !JsIdentifier( name ) || Read<bool>( ab, a.hidden ) || ( Read<uint64_t>( Definition( ab ), kDefinitionBehavior ) & 3 ) )
            continue; // DOTA_ABILITY_BEHAVIOR_HIDDEN (1) | DOTA_ABILITY_BEHAVIOR_PASSIVE (2)
        int maxLevel = Read<int>( ab, a.maxLevelOverride );
        if( maxLevel < 0 )
            maxLevel = Read<int>( Definition( ab ), kDefinitionMaxLevel );
        // shortcut: the game shows 0 while a modifier on the owner makes it ignore cooldowns (ability vfunc +0x878); we show the timer
        snprintf( buf, sizeof( buf ), "%s['%s',%d,%d,%.2f]", n++ ? "," : "", name, Read<int>( ab, a.level ), std::min( std::max( maxLevel, 0 ), 10 ), Read<float>( ab, a.cooldown ) );
        out += buf;
    }
    out += "]";
    if( o.inventoryItems >= 0 ){
        int obs = 0, sen = 0;
        for( int s = 0; s < 9; s++ ){ // inventory 0-5 and backpack 6-8
            CBaseEntity* item = FromHandle( chunks, Read<uint32_t>( npc, o.inventoryItems + 4 * s ) );
            const char* name = DefinitionName( item );
            if( !name )
                continue;
            if( !strcmp( name, "item_ward_dispenser" ) ){ // its own charge display pairs these with item_ward_observer / item_ward_sentry
                obs += Read<int>( item, a.charges );
                sen += Read<int>( item, a.secondaryCharges );
            } else if( !strcmp( name, "item_ward_observer" ) )
                obs += Read<int>( item, a.charges );
            else if( !strcmp( name, "item_ward_sentry" ) )
                sen += Read<int>( item, a.charges );
        }
        snprintf( buf, sizeof( buf ), ",w:[%d,%d]", obs, sen );
        out += buf;
    }
    out += "}";
    shown.push_back( { index, npc, handle, o.sceneNode, o.absOrigin } );
}

// A hero's health and mana for the top bar: 'npc_dota_hero_axe':[health,maxHealth,mana,maxMana,visible], mana in whole points.
// In the fog the client keeps the values from when we last saw him
static void AppendBar( std::string& out, CBaseEntity* hero, const ClassOffsets& o )
{
    const char* name = Read<const char*>( hero, o.unitName );
    if( !JsIdentifier( name ) || o.health < 0 || o.maxHealth < 0 || o.mana < 0 || o.maxMana < 0 )
        return;
    void* node = Read<void*>( hero, o.sceneNode );
    char buf[160];
    snprintf( buf, sizeof( buf ), "%s'%s':[%d,%d,%d,%d,%d]", out.size() > 1 ? "," : "", name, Read<int>( hero, o.health ), Read<int>( hero, o.maxHealth ),
              (int)Read<float>( hero, o.mana ), (int)Read<float>( hero, o.maxMana ), node && o.dormant >= 0 && !Read<uint8_t>( node, o.dormant ) );
    out += buf;
}

std::string LastSeen::Positions()
{
    std::string s = "{";
    CEntityIdentities** chunks = g_ppEntityChunks ? *g_ppEntityChunks : nullptr;
    for( const Shown& u : shown ){
        if( !chunks || FromHandle( chunks, u.handle ) != u.entity )
            continue; // gone since the scan
        void* node = Read<void*>( u.entity, u.sceneNode );
        if( !node )
            continue;
        const float* p = EntityGetAbsOrigin ? EntityGetAbsOrigin( u.entity ) : reinterpret_cast<const float*>( reinterpret_cast<char*>( node ) + u.absOrigin );
        char buf[96];
        snprintf( buf, sizeof( buf ), "%d:[%.1f,%.1f,%.1f],", u.index, p[0], p[1], p[2] );
        s += buf;
    }
    return s + "}";
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
    std::string units = "[", bars = "{", lanes = "[";
    shown.clear();

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

            // hero classes are C_DOTA_Unit_Hero_X or, for newer heroes, CDOTA_Unit_Hero_X (Elder Titan, ...)
            bool creep = !strcmp( cls, "C_DOTA_BaseNPC_Creep_Lane" ) || !strcmp( cls, "C_DOTA_BaseNPC_Creep_Siege" );
            bool building = !strcmp( cls, "C_DOTA_BaseNPC_Tower" ) || !strcmp( cls, "C_DOTA_BaseNPC_Barracks" );
            if( !creep && !building && ( !strstr( cls, "DOTA_Unit_Hero_" ) || strstr( cls, "Beastmaster_" ) ) )
                continue;

            ClassOffsets& o = OffsetsFor( binding );
            if( o.teamNum < 0 || o.sceneNode < 0 || o.absOrigin < 0 || o.unitName < 0 )
                continue;
            int team = Read<int>( entity, o.teamNum );
            if( localTeam < 0 )
                continue;
            if( creep || building ){ // the lanes the overlay walks enemy waves along: both sides' towers and barracks, our creeps ('')
                void* node = Read<void*>( entity, o.sceneNode );
                const char* name = building ? Read<const char*>( entity, o.unitName ) : "";
                if( node && ( building ? JsIdentifier( name ) : team == localTeam ) ){
                    char buf[160];
                    snprintf( buf, sizeof( buf ), "%s['%s',%.0f,%.0f,%d]", lanes.size() > 1 ? "," : "", name, Read<float>( node, o.absOrigin ),
                              Read<float>( node, o.absOrigin + 4 ), o.lifeState >= 0 && Read<uint8_t>( entity, o.lifeState ) == 0 );
                    lanes += buf;
                }
                continue;
            }
            if( o.replicatingHero >= 0 && Read<uint32_t>( entity, o.replicatingHero ) != 0xFFFFFFFF )
                continue; // illusion
            // allies too: the game's own ally bars show the same fractions, a check of these fields
            AppendBar( bars, entity, o );
            if( team == localTeam )
                continue;
            AppendUnit( units, chunks, entity, chunk->m_pIdentities[i].entHandle, c * MAX_ENTITIES_IN_LIST + i, o ); // enemy heroes get an ability row

            const char* unitName = Read<const char*>( entity, o.unitName );
            if( !unitName || !*unitName )
                continue;

            Track& t = tracks[entity];
            if( t.npcName != unitName ){
                t = Track();
                t.npcName = unitName;
                t.index = c * MAX_ENTITIES_IN_LIST + i;
            }
            t.lastScanMs = now;

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
        it = it->second.lastScanMs != now ? tracks.erase( it ) : std::next( it );

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
    units += "]";
    bars += "}";
    lanes = "{t:" + std::to_string( localTeam ) + ",p:" + lanes + "]}";

    UI::PushMarkers( marks.c_str(), units.c_str(), bars.c_str(), lanes.c_str() );
}
