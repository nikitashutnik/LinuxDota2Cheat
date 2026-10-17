#include "Schema.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdio>

#include "common.h"
#include "../Utils/Memory.h"
#include "../Utils/Logger.h"

static SchemaLayout layout;

static bool ReadString( const void* ptr, char* out, size_t cap )
{
    if( !ptr || (uintptr_t)ptr < 0x10000 )
        return false;
    out[0] = 0;
    // read in small steps so a string near the end of a mapping still works
    for( size_t done = 0; done < cap - 1; done += 16 ){
        size_t n = cap - 1 - done < 16 ? cap - 1 - done : 16;
        if( !Memory::SafeRead( reinterpret_cast<const char*>( ptr ) + done, out + done, n ) ){
            out[done] = 0;
            return done > 0;
        }
        out[done + n] = 0;
        if( memchr( out + done, 0, n ) )
            return true;
    }
    return true;
}

static bool Printable( const char* s )
{
    if( !s[0] )
        return false;
    for( ; *s; s++ )
        if( *s < 0x20 || *s > 0x7e )
            return false;
    return true;
}

static bool ReadableName( const void* ptr, char* out, size_t cap )
{
    return ReadString( ptr, out, cap ) && Printable( out );
}

static bool IsPointer( uint64_t w )
{
    uint64_t probe;
    return w >= 0x10000 && Memory::SafeRead( (void*)w, &probe, sizeof( probe ) );
}

const char* SchemaClassInfo::Name() const       { return (const char*)Word( layout.name ); }
int SchemaClassInfo::Size() const               { return (int32_t)( Word( layout.counts ) & 0xffffffff ); }
int SchemaClassInfo::FieldCount() const         { return (int16_t)( ( Word( layout.counts ) >> 32 ) & 0xffff ); }
int SchemaClassInfo::BaseClassCount() const     { return (int)( ( Word( layout.counts + 1 ) >> 8 ) & 0xff ); }
SchemaClassFieldData_t* SchemaClassInfo::Fields() const        { return (SchemaClassFieldData_t*)Word( layout.fields ); }
SchemaBaseClassInfoData_t* SchemaClassInfo::BaseClasses() const{ return (SchemaBaseClassInfoData_t*)Word( layout.baseClasses ); }

SchemaClassInfo SchemaClassInfo::Parent() const
{
    SchemaClassInfo p;
    if( base && BaseClassCount() > 0 && BaseClasses() && BaseClasses()[0].parent )
        p.base = reinterpret_cast<char*>( BaseClasses()[0].parent );
    return p;
}

int SchemaClassInfo::FindField( const char* fieldName, SchemaClassFieldData_t** out, int depth ) const
{
    if( !base || !layout.valid || depth > 16 )
        return -1;
    int count = FieldCount();
    SchemaClassFieldData_t* fields = Fields();
    if( count < 0 || count > 4096 )
        return -1;
    for( int i = 0; i < count && fields; i++ ){
        if( fields[i].name && !strcmp( fields[i].name, fieldName ) ){
            if( out ) *out = &fields[i];
            return fields[i].offset;
        }
    }
    return Parent().FindField( fieldName, out, depth + 1 );
}

// Detects the class info layout from a class whose name is known and which has fields and one base class
static bool DetectLayout( void* raw, const char* expectedName )
{
    uint64_t w[16] = {};
    if( !Memory::SafeRead( raw, w, sizeof( w ) ) )
        return false;

    SchemaLayout l;
    char str[160];
    // name: first word that reads as the expected string
    for( int i = 0; i < 3; i++ )
        if( ReadableName( (void*)w[i], str, sizeof( str ) ) && !strcmp( str, expectedName ) ){ l.name = i; break; }
    if( l.name < 0 )
        return false;

    // counts: first non-pointer word after the name with a plausible size / field count
    for( int i = l.name + 1; i < 8 && l.counts < 0; i++ ){
        if( IsPointer( w[i] ) )
            continue;
        int32_t size = (int32_t)( w[i] & 0xffffffff );
        int16_t fieldCount = (int16_t)( ( w[i] >> 32 ) & 0xffff );
        if( size > 0 && size < 0x100000 && fieldCount > 0 && fieldCount < 4096 )
            l.counts = i;
    }
    if( l.counts < 0 )
        return false;
    int32_t size = (int32_t)( w[l.counts] & 0xffffffff );

    for( int i = l.counts + 2; i < 14; i++ ){
        if( !IsPointer( w[i] ) )
            continue;
        if( l.fields < 0 ){
            SchemaClassFieldData_t f;
            CSchemaType t;
            char tname[160];
            if( Memory::SafeRead( (void*)w[i], &f, sizeof( f ) ) && ReadableName( f.name, str, sizeof( str ) ) &&
                f.offset >= 0 && f.offset < size && f.type && Memory::SafeRead( f.type, &t, sizeof( t ) ) && ReadableName( t.name, tname, sizeof( tname ) ) ){
                l.fields = i;
                continue;
            }
        }
        if( l.baseClasses < 0 ){
            SchemaBaseClassInfoData_t b;
            uint64_t pw[3];
            if( Memory::SafeRead( (void*)w[i], &b, sizeof( b ) ) && b.offset < 0x10000 && IsPointer( (uint64_t)b.parent ) &&
                Memory::SafeRead( b.parent, pw, sizeof( pw ) ) && ReadableName( (void*)pw[l.name], str, sizeof( str ) ) ){
                l.baseClasses = i;
                continue;
            }
        }
    }
    if( l.fields < 0 || l.baseClasses < 0 )
        return false;

    l.valid = true;
    layout = l;
    Logger::Log( "schema class info layout: name@+0x%x counts@+0x%x fields@+0x%x bases@+0x%x (probe %s: size 0x%x, %d fields, %d bases)",
                 l.name * 8, l.counts * 8, l.fields * 8, l.baseClasses * 8, expectedName, size,
                 (int)( ( w[l.counts] >> 32 ) & 0xffff ), (int)( ( w[l.counts + 1] >> 8 ) & 0xff ) );
    return true;
}

// CSchemaSystem vtable: [13] FindTypeScopeForModule(const char* module, void* unused) -> CSchemaSystemTypeScope*
// CSchemaSystemTypeScope vtable: [2] FindDeclaredClass(const char* name) -> class info
typedef void* (* FindTypeScopeForModuleFn)( void* schemaSystem, const char* module, void* unused );
typedef void* (* FindDeclaredClassFn)( void* typeScope, const char* className );

static void* schemaSystem;
static void* clientTypeScope;

static void* FindRaw( const char* className )
{
    return clientTypeScope ? getvfunc<FindDeclaredClassFn>( clientTypeScope, 2 )( clientTypeScope, className ) : nullptr;
}

bool Schema::Init()
{
    schemaSystem = GetInterface<void>( "./libschemasystem.so", "SchemaSystem_001" );
    if( !schemaSystem )
        return false;

    auto findTypeScope = getvfunc<FindTypeScopeForModuleFn>( schemaSystem, 13 );
    for( const char* module : { "libclient.so", "client" } ){
        clientTypeScope = findTypeScope( schemaSystem, module, nullptr );
        Logger::Log( "schema type scope for '%s': %p", module, clientTypeScope );
        if( clientTypeScope )
            break;
    }
    if( !clientTypeScope )
        return false;

    void* probe = FindRaw( "C_BaseEntity" );
    if( !DetectLayout( probe, "C_BaseEntity" ) ){
        uint64_t words[16] = {};
        if( probe ) Memory::SafeRead( probe, words, sizeof( words ) );
        std::string dump;
        for( uint64_t v : words ){ char b[24]; snprintf( b, sizeof( b ), " %llx", (unsigned long long)v ); dump += b; }
        Logger::Log( "schema: could not detect class info layout from C_BaseEntity %p:%s", probe, dump.c_str() );
        return false;
    }
    return true;
}

SchemaClassInfo Schema::FindClass( const char* className )
{
    static std::unordered_map<std::string, SchemaClassInfo> cache;
    auto it = cache.find( className );
    if( it != cache.end() )
        return it->second;

    SchemaClassInfo info;
    if( layout.valid ){
        void* raw = FindRaw( className );
        char name[160] = "";
        if( raw && Memory::SafeRead( raw, name, 0 ) ){
            SchemaClassInfo candidate;
            candidate.base = reinterpret_cast<char*>( raw );
            if( ReadableName( candidate.Name(), name, sizeof( name ) ) && !strcmp( name, className ) && candidate.FieldCount() >= 0 && candidate.FieldCount() < 4096 )
                info = candidate;
        }
        if( info )
            Logger::Log( "schema class %s: size 0x%x, %d fields, %d bases", className, info.Size(), info.FieldCount(), info.BaseClassCount() );
        else
            Logger::Log( "schema class %s: not found (%p)", className, raw );
    }
    cache[className] = info;
    return info;
}

// CSchemaSystemTypeScope class table as read by FindDeclaredClass: 256 buckets of 0x48 bytes at +0x5f8,
// each bucket's first word heads a list of { uint32 hash; uint32 pad; node* next; void* classInfo; }
bool Schema::DumpAll( const char* path )
{
    SchemaClassInfo probe = FindClass( "C_BaseEntity" );
    if( !clientTypeScope || !probe )
        return false;

    std::vector<void*> infos;
    bool sawProbe = false;
    for( int b = 0; b < 256; b++ ){
        void* node = nullptr;
        if( !Memory::SafeRead( reinterpret_cast<char*>( clientTypeScope ) + 0x5f8 + b * 0x48, &node, sizeof( node ) ) )
            break;
        for( int guard = 0; node && guard < 100000; guard++ ){
            struct { uint32_t hash; uint32_t pad; void* next; void* info; } n;
            if( !Memory::SafeRead( node, &n, sizeof( n ) ) )
                break;
            if( n.info ){
                infos.push_back( n.info );
                if( n.info == probe.base )
                    sawProbe = true;
            }
            node = n.next;
        }
    }
    if( !sawProbe ){
        Logger::Log( "schema dump: class table layout changed (%zu entries walked, C_BaseEntity not among them)", infos.size() );
        return false;
    }

    struct Entry { std::string name; SchemaClassInfo info; };
    std::vector<Entry> classes;
    for( void* raw : infos ){
        SchemaClassInfo ci;
        ci.base = reinterpret_cast<char*>( raw );
        char name[160];
        uint64_t words[16];
        if( !Memory::SafeRead( raw, words, sizeof( words ) ) || !ReadableName( ci.Name(), name, sizeof( name ) ) || ci.FieldCount() < 0 || ci.FieldCount() > 4096 )
            continue;
        classes.push_back( { name, ci } );
    }
    std::sort( classes.begin(), classes.end(), []( const Entry& a, const Entry& b ){ return a.name < b.name; } );

    FILE* f = fopen( path, "w" );
    if( !f ){
        Logger::Log( "schema dump: can't write %s", path );
        return false;
    }
    fprintf( f, "// Dota 2 client schema dump: %zu classes\n", classes.size() );
    for( Entry& e : classes ){
        char parent[160] = "";
        SchemaClassInfo p = e.info.Parent();
        if( p ) ReadString( p.Name(), parent, sizeof( parent ) );
        fprintf( f, "\nclass %s%s%s  // size 0x%x\n", e.name.c_str(), parent[0] ? " : " : "", parent, e.info.Size() );

        int count = e.info.FieldCount();
        SchemaClassFieldData_t* fields = e.info.Fields();
        for( int i = 0; i < count && fields; i++ ){
            SchemaClassFieldData_t fd;
            if( !Memory::SafeRead( &fields[i], &fd, sizeof( fd ) ) )
                break;
            char fname[128] = "?", tname[128] = "?";
            ReadString( fd.name, fname, sizeof( fname ) );
            CSchemaType t;
            if( fd.type && Memory::SafeRead( fd.type, &t, sizeof( t ) ) )
                ReadString( t.name, tname, sizeof( tname ) );
            fprintf( f, "    +0x%04x %s : %s\n", fd.offset, fname, tname );
        }
    }
    fclose( f );
    Logger::Log( "schema dump: %zu classes written to %s", classes.size(), path );
    return true;
}
