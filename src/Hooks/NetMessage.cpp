#include "Hooks.h"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../Hacks/LastSeen.h"
#include "../Utils/Util.h"

typedef bool (* UnserializeMessageFn)( CNetworkMessages*, bf_read*, void*, void* );

// Source bit streams are LSB-first; the size varint and payload start at an arbitrary bit
struct BitReader
{
    const uint8_t* data;
    int bit, end;

    int Byte()
    {
        if( bit + 8 > end )
            return -1;
        int sh = bit & 7;
        const uint8_t* p = data + ( bit >> 3 );
        unsigned v = p[0] >> sh;
        if( sh )
            v |= p[1] << ( 8 - sh );
        bit += 8;
        return v & 0xff;
    }
};

// Copies the message payload without moving the stream. -1 when the stream is short or the message too big
static int ReadPayload( bf_read* buf, uint8_t* out, int cap )
{
    BitReader r = { buf->m_pData, buf->m_iCurBit, buf->m_nDataBits };
    unsigned size = 0;
    for( int s = 0; s < 35; s += 7 ){
        int c = r.Byte();
        if( c < 0 )
            return -1;
        size |= unsigned( c & 0x7f ) << s;
        if( !( c & 0x80 ) )
            break;
    }
    if( (int)size > cap )
        return -1;
    for( unsigned i = 0; i < size; i++ ){
        int c = r.Byte();
        if( c < 0 )
            return -1;
        out[i] = (uint8_t)c;
    }
    return (int)size;
}

// Minimal protobuf wire reader: varint (0), fixed64 (1), length-delimited (2), fixed32 (5)
struct Pb
{
    const uint8_t* p;
    const uint8_t* e;
};

static bool Varint( Pb& b, uint64_t& v )
{
    v = 0;
    for( int s = 0; s < 64 && b.p < b.e; s += 7 ){
        uint8_t c = *b.p++;
        v |= uint64_t( c & 0x7f ) << s;
        if( !( c & 0x80 ) )
            return true;
    }
    return false;
}

static bool Field( Pb& b, int& num, int& wire, uint64_t& v, Pb& sub )
{
    uint64_t key;
    if( b.p >= b.e || !Varint( b, key ) )
        return false;
    num = (int)( key >> 3 );
    wire = (int)( key & 7 );
    switch( wire ){
        case 0: return Varint( b, v );
        case 1: if( b.e - b.p < 8 ) return false; memcpy( &v, b.p, 8 ); b.p += 8; return true;
        case 5: { uint32_t u; if( b.e - b.p < 4 ) return false; memcpy( &u, b.p, 4 ); b.p += 4; v = u; return true; }
        case 2: { uint64_t len; if( !Varint( b, len ) || uint64_t( b.e - b.p ) < len ) return false; sub = { b.p, b.p + len }; b.p += len; return true; }
        default: return false;
    }
}

static void ReadVector( Pb b, float out[3] ) // CMsgVector: x, y, z as fixed32 floats
{
    int num, wire;
    uint64_t v = 0;
    Pb sub;
    while( Field( b, num, wire, v, sub ) )
        if( num >= 1 && num <= 3 && wire == 5 ){
            uint32_t u = (uint32_t)v;
            memcpy( &out[num - 1], &u, 4 );
        }
}

// CUserMsg_ParticleManager (usermessages.proto): type=1, index=2, create_particle=4 { particle_name_index=1, entity_handle=3,
// entity_handle_for_modifiers=4 }, destroy_particle=5, update_particle_transform=30 { control_point=1, position=2 }
enum ParticleEvent { CREATE = 0, DESTROY = 7, RELEASE = 9, UPDATE_TRANSFORM = 27 };
constexpr uint32_t kInvalidHandle = 16777215; // networked ehandle: 14 index bits + 10 serial bits, all ones when invalid

static int EntityIndex( uint32_t netHandle ) { return netHandle & 0x3fff; }

struct Create
{
    uint64_t nameIndex = 0;
    uint32_t entity = kInvalidHandle, forModifiers = kInvalidHandle;
    long createdMs = 0;
    std::string name;      // known once the client has built a collection with this resource id
    bool hasPos = false, reported = false;
    float pos[3] = {};
};

static std::mutex guard;
static std::unordered_map<uint32_t, Create> creates;           // live net particle index -> its create
static std::unordered_map<uint64_t, std::string> namesByIndex; // particle resource id -> resource name
static std::unordered_map<int, float> channelByHero;           // hero entity index -> teleport channel seconds (cp7.x of the start effect)

// A teleport destination effect: a world-origin "teleport_end" system whose modifiers come from the teleporting hero
static void MaybeReport( Create& c )
{
    if( c.reported || !c.hasPos || c.name.empty() || c.entity != kInvalidHandle || c.forModifiers == kInvalidHandle )
        return;
    if( c.name.find( "teleport_end" ) == std::string::npos || c.name.find( "endcap" ) != std::string::npos )
        return;
    c.reported = true;
    int hero = EntityIndex( c.forModifiers );
    auto d = channelByHero.find( hero );
    float channel = d != channelByHero.end() ? d->second : 3.0f;
    Logger::Log( "teleport: entity %d -> (%.0f, %.0f, %.0f), %.1fs channel, %s", hero, c.pos[0], c.pos[1], c.pos[2], channel, c.name.c_str() );
    LastSeen::OnTeleport( hero, c.pos[0], c.pos[1], c.pos[2], c.createdMs, channel );
}

static void HandleParticleManager( const uint8_t* payload, int len, long t )
{
    Pb b = { payload, payload + len }, sub = {}, body = {};
    int type = -1, num, wire, bodyField = 0;
    uint64_t v = 0, index = 0;
    while( Field( b, num, wire, v, sub ) ){
        if( num == 1 ) type = (int)v;
        else if( num == 2 ) index = v;
        else if( wire == 2 ){ body = sub; bodyField = num; }
    }
    uint32_t idx = (uint32_t)index;

    std::lock_guard<std::mutex> lock( guard );
    if( type == CREATE && bodyField == 4 ){
        if( creates.size() > 4096 ) // a leak of never-destroyed indices; start over
            creates.clear();
        Create c;
        c.createdMs = t;
        while( Field( body, num, wire, v, sub ) ){
            if( num == 1 ) c.nameIndex = v;
            else if( num == 3 ) c.entity = (uint32_t)v;
            else if( num == 4 ) c.forModifiers = (uint32_t)v;
        }
        auto known = namesByIndex.find( c.nameIndex );
        if( known != namesByIndex.end() )
            c.name = known->second;
        creates[idx] = c;
    } else if( type == UPDATE_TRANSFORM && bodyField == 30 ){
        auto it = creates.find( idx );
        if( it == creates.end() )
            return;
        int cp = -1;
        float pos[3] = {};
        while( Field( body, num, wire, v, sub ) ){
            if( num == 1 ) cp = (int)v;
            else if( num == 2 ) ReadVector( sub, pos );
        }
        Create& c = it->second;
        if( cp == 0 && !c.hasPos ){
            memcpy( c.pos, pos, sizeof( pos ) );
            c.hasPos = true;
            MaybeReport( c );
        } else if( cp == 7 && c.entity != kInvalidHandle ){
            channelByHero[EntityIndex( c.entity )] = pos[0];
        }
    } else if( type == DESTROY || type == RELEASE ){
        auto it = creates.find( idx );
        if( it == creates.end() )
            return;
        Create& c = it->second;
        if( c.reported && type == DESTROY ){
            int hero = EntityIndex( c.forModifiers );
            auto d = channelByHero.find( hero );
            float channel = d != channelByHero.end() ? d->second : 3.0f;
            bool completed = t - c.createdMs >= (long)( channel * 1000 ) - 250;
            Logger::Log( "teleport: entity %d %s after %.1fs", hero, completed ? "arrived" : "cancelled", ( t - c.createdMs ) / 1000.0f );
            LastSeen::OnTeleportEnd( hero, completed );
        }
        creates.erase( it );
    }
}

// The binding's resource id is the particle_name_index of the net messages, so one built collection names every create of that system
void NetParticles::OnCollectionCreated( uint64_t resourceId, const char* name )
{
    std::lock_guard<std::mutex> lock( guard );
    if( !namesByIndex.emplace( resourceId, name ).second )
        return;
    for( auto& kv : creates )
        if( kv.second.nameIndex == resourceId && kv.second.name.empty() ){
            kv.second.name = name;
            MaybeReport( kv.second );
        }
}

bool Hooks::UnserializeMessage( CNetworkMessages* thisptr, bf_read* buf, void* msg, void* err )
{
    uint8_t payload[4096];
    int len = ReadPayload( buf, payload, sizeof( payload ) );

    bool ok = networkMessagesVMT->GetOriginalMethod<UnserializeMessageFn>( kUnserializeMessageSlot )( thisptr, buf, msg, err );
    if( !ok || len < 0 || !msg )
        return ok;

    // CNetMessagePB slot 4 returns the handle the dispatcher works with; its message id sits at +0x18 (ProcessMessages reads it)
    typedef void* (* GetHandleFn)( void* );
    void* handle = ( *reinterpret_cast<GetHandleFn**>( msg ) )[4]( msg );
    if( !handle )
        return ok;
    int id = *reinterpret_cast<int*>( reinterpret_cast<char*>( handle ) + 0x18 );
    if( id == 145 || id == 489 ) // CUserMsg_ParticleManager: UM_ParticleManager and DOTA_UM_ParticleManager
        HandleParticleManager( payload, len, Util::GetEpochMs() );
    return ok;
}
