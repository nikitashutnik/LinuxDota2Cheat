#pragma once

#include "CBaseEntity.h"

#define MAX_ENTITIES_IN_LIST 512
#define MAX_ENTITY_LISTS 64

// 0x70 per entry in this build (handle lookups do `imul rcx, rcx, 0x70`)
class CEntityIdentity {
public:
    CBaseEntity *entity;
    void *unk;
    int entHandle; // LOWORD(handle) & 0x7FFF = entID
    int nameStringableIndex;
    void *name;
    void *designerName;
    void *unk28;
    uint32_t flags; // +0x30, EntityFlags_t (dormancy lives here)
    uint32_t pad34;
    void *pad[7];
};
static_assert( sizeof( CEntityIdentity ) == 0x70, "CEntityIdentity stride" );

class CEntityIdentities
{
public:
    CEntityIdentity m_pIdentities[MAX_ENTITIES_IN_LIST];
};

// Address of the game's pointer to the 64 identity chunks (what every inlined CHandle::Get() reads)
inline CEntityIdentities*** g_ppEntityChunks;
