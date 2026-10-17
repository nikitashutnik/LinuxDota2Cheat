#pragma once

#include <cstdint>
#include <cstring>

// Source 2 schema system. Field offsets are resolved by name at runtime through SchemaSystem_001.
// The class info struct layout differs between engine builds, so it is detected once from C_BaseEntity
// (which words hold the name, the size/field counts, the field array and the base class array).

struct CSchemaType
{
    void* vtable;
    const char* name;        // ex: "int32", "CGameSceneNode*", "C_BaseEntity"
    void* typeScope;
    uint8_t category;        // 0 builtin, 1 ptr, 2 bitfield, 3 fixed array, 4 atomic, 5 declared class, 6 declared enum
    uint8_t atomicCategory;
    uint8_t pad[6];
    void* target;
};

struct SchemaClassFieldData_t
{
    const char* name;
    CSchemaType* type;
    int32_t offset;
    int32_t metadataCount;
    void* metadata;
}; // 0x20

struct SchemaBaseClassInfoData_t
{
    uint32_t offset;
    uint32_t pad;
    void* parent; // class info
}; // 0x10
static_assert( sizeof( SchemaClassFieldData_t ) == 0x20, "schema field size" );
static_assert( sizeof( SchemaBaseClassInfoData_t ) == 0x10, "schema base size" );
static_assert( sizeof( CSchemaType ) == 0x28, "schema type size" );

// Word indices inside the class info struct, detected at runtime
struct SchemaLayout
{
    bool valid = false;
    int name = -1;        // const char*
    int counts = -1;      // int32 size, int16 fieldCount, int16 staticFieldCount
    int fields = -1;      // SchemaClassFieldData_t*
    int baseClasses = -1; // SchemaBaseClassInfoData_t*
};

// View over a class info struct
struct SchemaClassInfo
{
    char* base = nullptr;

    explicit operator bool() const { return base != nullptr; }

    uint64_t Word( int index ) const              { return *reinterpret_cast<uint64_t*>( base + index * 8 ); }
    const char* Name() const;
    int Size() const;
    int FieldCount() const;
    int BaseClassCount() const;
    SchemaClassFieldData_t* Fields() const;
    SchemaBaseClassInfoData_t* BaseClasses() const;
    SchemaClassInfo Parent() const;

    // Offset of a field by name, searching this class and then its base classes. -1 when missing.
    int FindField( const char* fieldName, SchemaClassFieldData_t** out = nullptr, int depth = 0 ) const;
};

namespace Schema
{
    bool Init(); // SchemaSystem_001 + the libclient.so type scope + layout detection
    SchemaClassInfo FindClass( const char* className );
    // Writes every class of the client type scope (name, size, parent, fields with type and offset) to a text file
    bool DumpAll( const char* path );
}

// What C_BaseEntity::Schema_DynamicBinding() returns: a per-class binding object; only the name at +8 is used
struct CSchemaClassBinding
{
    void* unk0;
    const char* binaryName; // ex: C_DOTA_Unit_Hero_Axe

    SchemaClassInfo Info() { return Schema::FindClass( binaryName ); }
};
