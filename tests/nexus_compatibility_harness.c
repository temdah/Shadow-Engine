/* Read-only PE fixtures mapped as data, never loaded/executed as DLLs.
 * Actual compatibility module and patch transaction; no game process. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned char *fixture_host;
static unsigned char fixture_ready,fixture_intercepted;
static void *fixture_original;
static int fixture_missing_export;
static HMODULE WINAPI fixture_module(LPCWSTR name)
{ return !wcscmp(name,L"TroploNexusTools.ipe")?(HMODULE)fixture_host:NULL; }
static FARPROC WINAPI fixture_export(HMODULE module,LPCSTR name)
{
    (void)module;
    if(fixture_missing_export) return NULL;
    if(!strcmp(name,"g_NexusReady")) return (FARPROC)&fixture_ready;
    if(!strcmp(name,"g_RunGameIntercepted")) return (FARPROC)&fixture_intercepted;
    if(!strcmp(name,"OriginalRunGame")) return (FARPROC)&fixture_original;
    return NULL;
}
#define GetModuleHandleW fixture_module
#define GetProcAddress fixture_export
#include "../src/shadow_engine_patch.c"
#undef GetModuleHandleW
#undef GetProcAddress
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)

static unsigned char *map_fixture(const char *path)
{
    FILE *file=fopen(path,"rb");
    unsigned char *raw,*mapped;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *section;
    long bytes;
    unsigned i;
    CHECK(file); CHECK(!fseek(file,0,SEEK_END)); bytes=ftell(file);
    CHECK(bytes>4096 && bytes<64000000); rewind(file);
    raw=(unsigned char *)malloc((size_t)bytes); CHECK(raw);
    CHECK(fread(raw,1,(size_t)bytes,file)==(size_t)bytes); fclose(file);
    dos=(IMAGE_DOS_HEADER *)raw;
    CHECK(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0 && dos->e_lfanew<4096);
    nt=(IMAGE_NT_HEADERS64 *)(raw+dos->e_lfanew);
    CHECK(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64);
    CHECK(nt->OptionalHeader.SizeOfImage<64000000 && nt->OptionalHeader.SizeOfHeaders<=(unsigned long)bytes);
    mapped=(unsigned char *)VirtualAlloc(NULL,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(mapped); memcpy(mapped,raw,nt->OptionalHeader.SizeOfHeaders);
    section=IMAGE_FIRST_SECTION(nt);
    for(i=0;i<nt->FileHeader.NumberOfSections;++i) {
        CHECK((unsigned char *)(section+i+1)<=raw+bytes);
        CHECK(section[i].VirtualAddress<=nt->OptionalHeader.SizeOfImage &&
            section[i].SizeOfRawData<=nt->OptionalHeader.SizeOfImage-section[i].VirtualAddress);
        CHECK(section[i].PointerToRawData<=(unsigned long)bytes &&
            section[i].SizeOfRawData<=(unsigned long)bytes-section[i].PointerToRawData);
        memcpy(mapped+section[i].VirtualAddress,raw+section[i].PointerToRawData,section[i].SizeOfRawData);
    }
    free(raw); return mapped;
}
static void reset_game(const RuntimeProfile *p)
{
    size_t i;
    for(i=0;i<p->lua_frame_site_count;++i) {
        const LuaFramePatchSite *s=&p->lua_frame_sites[i];
        memcpy(g_shadow_engine.bootstrap.disrupt_base+s->rva,s->before,s->size);
    }
}
static void check_game(const RuntimeProfile *p,int patched)
{
    size_t i;
    for(i=0;i<p->lua_frame_site_count;++i) {
        const LuaFramePatchSite *s=&p->lua_frame_sites[i];
        CHECK(!memcmp(g_shadow_engine.bootstrap.disrupt_base+s->rva,patched?s->after:s->before,s->size));
    }
}
static void expect_skip(const RuntimeProfile *p,LONG state)
{
    int enabled=99;
    PatchTransaction transaction;
    CHECK(prepare_lua_frame_repair(p,&enabled) && !enabled);
    CHECK(g_shadow_engine.bootstrap.host_compatibility==state);
    CHECK(patch_transaction_begin(&transaction,"optional-skip",-1));
    CHECK(apply_lua_frame_repair(p,&enabled) && !enabled && !transaction.write_count);
    CHECK(patch_transaction_commit(&transaction));
}
int main(int argc,char **argv)
{
    const RuntimeProfile *p;
    unsigned file,region;
    CHECK(argc==3);
    for(region=0;region<ARRAY_COUNT(g_runtime_profiles);++region) {
    p=&g_runtime_profiles[region];
    CHECK(p->lua_frame_site_count==17);
    g_shadow_engine.bootstrap.disrupt_base=(unsigned char *)VirtualAlloc(NULL,p->image_size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(g_shadow_engine.bootstrap.disrupt_base);
    g_shadow_engine.bootstrap.snapshot.troplo=(HMODULE)1;
    fixture_intercepted=1; fixture_original=(void *)1;
    for(file=1;file<=2;++file) {
        const LuaFrameHostProfile *host_profile;
        IMAGE_NT_HEADERS64 *nt;
        unsigned char saved;
        int enabled,fail;
        PatchTransaction transaction;
        fixture_host=map_fixture(argv[file]);
        host_profile=lua_frame_host_profile(fixture_host); CHECK(host_profile);
        CHECK(!strcmp(host_profile->version,file==1?"1.1.12-6924c27":"1.1.13-ad643fa"));
        nt=(IMAGE_NT_HEADERS64 *)(fixture_host+((IMAGE_DOS_HEADER *)fixture_host)->e_lfanew);
        reset_game(p);
        CHECK(prepare_lua_frame_repair(p,&enabled) && enabled);
        CHECK(g_shadow_engine.bootstrap.host_compatibility==1);
        CHECK(patch_transaction_begin(&transaction,"known-host",-1));
        CHECK(apply_lua_frame_repair(p,&enabled) && enabled && transaction.write_count==17);
        CHECK(patch_transaction_commit(&transaction)); check_game(p,1); reset_game(p);

        /* Each actual format write failure must roll back all preceding writes. */
        for(fail=0;fail<17;++fail) {
            PatchRollbackResult rollback;
            enabled=1;
            CHECK(patch_transaction_begin(&transaction,"fault",fail));
            CHECK(!apply_lua_frame_repair(p,&enabled));
            rollback=patch_transaction_rollback(&transaction);
            CHECK(!rollback.failed_restores && rollback.restored_writes==(size_t)fail);
            check_game(p,0);
        }
        /* Every native site must match; existing/foreign Lua patches are untouched. */
        for(fail=0;fail<17;++fail) {
            const LuaFramePatchSite *s=&p->lua_frame_sites[fail];
            g_shadow_engine.bootstrap.disrupt_base[s->rva]^=1;
            expect_skip(p,5);
            CHECK(g_shadow_engine.bootstrap.disrupt_base[s->rva]==((unsigned char)s->before[0]^1));
            reset_game(p); check_game(p,0);
        }
        fixture_host[host_profile->init_guard_data_rva]=1; expect_skip(p,4);
        fixture_host[host_profile->init_guard_data_rva]=0;
        fixture_ready=1; expect_skip(p,4); fixture_ready=0;
        fixture_intercepted=0; expect_skip(p,4); fixture_intercepted=1;
        fixture_original=NULL; expect_skip(p,4); fixture_original=(void *)1;
        fixture_missing_export=1; expect_skip(p,4); fixture_missing_export=0;
        saved=fixture_host[host_profile->caller_rva+3];
        fixture_host[host_profile->caller_rva+3]^=1; expect_skip(p,4);
        fixture_host[host_profile->caller_rva+3]=saved;
        saved=fixture_host[host_profile->initialize_rva+32];
        fixture_host[host_profile->initialize_rva+32]^=1; expect_skip(p,4);
        fixture_host[host_profile->initialize_rva+32]=saved;
        nt->FileHeader.TimeDateStamp^=1; expect_skip(p,3); nt->FileHeader.TimeDateStamp^=1;
        nt->OptionalHeader.SizeOfImage+=4096; expect_skip(p,3); nt->OptionalHeader.SizeOfImage-=4096;
        nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386; expect_skip(p,3); nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        /* The host can leave the early window between plan and commit. */
        CHECK(prepare_lua_frame_repair(p,&enabled) && enabled);
        fixture_ready=1;
        CHECK(patch_transaction_begin(&transaction,"late-recheck",-1));
        CHECK(apply_lua_frame_repair(p,&enabled) && !enabled && !transaction.write_count);
        CHECK(patch_transaction_commit(&transaction)); fixture_ready=0; check_game(p,0);
        VirtualFree(fixture_host,0,MEM_RELEASE); fixture_host=NULL;
        expect_skip(p,3); check_game(p,0);
    }
    VirtualFree(g_shadow_engine.bootstrap.disrupt_base,0,MEM_RELEASE);
    }
    printf("PASS Nexus compatibility: %u checks; five game profiles x actual 1.1.12/1.1.13 PE fixtures, 17-site activation, each write rollback, late/layout/unknown-host and foreign-site isolation. Offline only.\n",checks);
    return 0;
}
