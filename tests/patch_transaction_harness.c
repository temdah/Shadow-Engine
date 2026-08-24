#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static LONG g_raw_write_calls;
static LONG g_fail_raw_write=-1;

static int raw_write_bytes(unsigned char *target,const void *data,size_t size)
{
    LONG call=g_raw_write_calls++;
    if(call==g_fail_raw_write) return 0;
    memcpy(target,data,size);
    FlushInstructionCache(GetCurrentProcess(),target,size);
    return 1;
}

#include "../src/modules/15_patch_transaction.inc"

static int bytes_equal(const unsigned char *left,const unsigned char *right,
                       size_t size)
{
    return memcmp(left,right,size)==0;
}

static int run_injected_failure(LONG fail_after)
{
    static const unsigned char original[8]={0x10,0x11,0x12,0x13,
                                            0x14,0x15,0x16,0x17};
    static const unsigned char first[4]={0x21,0x22,0x23,0x24};
    static const unsigned char second[4]={0x31,0x32,0x33,0x34};
    static const unsigned char third[4]={0x41,0x42,0x43,0x44};
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    unsigned char *target=(unsigned char *)VirtualAlloc(NULL,64,
        MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    void *published=NULL;
    int ok=1;
    if(!target) return 0;
    memcpy(target,original,sizeof(original));
    if(!patch_transaction_begin(&transaction,"fault-test",fail_after)) ok=0;
    if(ok) {
        published=patch_allocate(32,&published);
        if(!published) ok=0;
    }
    if(ok && !patch_write_bytes(target,first,sizeof(first))) ok=0;
    if(ok && !patch_write_bytes(target,second,sizeof(second))) ok=0;
    if(ok && !patch_write_bytes(target+4,third,sizeof(third))) ok=0;
    if(ok) {
        VirtualFree(target,0,MEM_RELEASE);
        return 0;
    }
    rollback=patch_transaction_rollback(&transaction);
    ok=rollback.failed_restores==0 &&
       rollback.restored_writes==(size_t)fail_after &&
       rollback.released_allocations==1 &&
       published==NULL && bytes_equal(target,original,sizeof(original));
    VirtualFree(target,0,MEM_RELEASE);
    return ok;
}

static int run_successful_commit(void)
{
    static const unsigned char original[4]={1,2,3,4};
    static const unsigned char replacement[4]={5,6,7,8};
    PatchTransaction transaction;
    unsigned char *target=(unsigned char *)VirtualAlloc(NULL,64,
        MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    void *published=NULL;
    int ok;
    if(!target) return 0;
    memcpy(target,original,sizeof(original));
    ok=patch_transaction_begin(&transaction,"commit-test",-1);
    if(ok) {
        published=patch_allocate(32,&published);
        ok=published!=NULL;
    }
    if(ok) ok=patch_write_bytes(target,replacement,sizeof(replacement));
    if(ok) ok=patch_transaction_commit(&transaction);
    if(ok) ok=bytes_equal(target,replacement,sizeof(replacement)) &&
              published!=NULL && g_active_patch_transaction==NULL;
    if(published) VirtualFree(published,0,MEM_RELEASE);
    VirtualFree(target,0,MEM_RELEASE);
    return ok;
}

static int run_failed_restore_retains_allocation(void)
{
    static const unsigned char original[4]={9,8,7,6};
    static const unsigned char replacement[4]={6,7,8,9};
    PatchTransaction transaction;
    PatchRollbackResult rollback;
    unsigned char *target=(unsigned char *)VirtualAlloc(NULL,64,
        MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    void *published=NULL;
    int ok;
    if(!target) return 0;
    memcpy(target,original,sizeof(original));
    g_raw_write_calls=0;
    g_fail_raw_write=-1;
    ok=patch_transaction_begin(&transaction,"restore-failure-test",-1);
    if(ok) {
        published=patch_allocate(32,&published);
        ok=published!=NULL;
    }
    if(ok) ok=patch_write_bytes(target,replacement,sizeof(replacement));
    g_fail_raw_write=g_raw_write_calls;
    rollback=patch_transaction_rollback(&transaction);
    ok=ok && rollback.failed_restores==1 &&
       rollback.released_allocations==0 && published!=NULL &&
       bytes_equal(target,replacement,sizeof(replacement));
    g_fail_raw_write=-1;
    if(published) VirtualFree(published,0,MEM_RELEASE);
    VirtualFree(target,0,MEM_RELEASE);
    return ok;
}

int main(void)
{
    LONG failure;
    for(failure=0;failure<3;++failure) {
        if(!run_injected_failure(failure)) {
            printf("FAIL: injected write %ld\n",(long)failure);
            return 1;
        }
    }
    if(!run_successful_commit()) {
        printf("FAIL: successful commit\n");
        return 1;
    }
    if(!run_failed_restore_retains_allocation()) {
        printf("FAIL: failed restore allocation retention\n");
        return 1;
    }
    printf("PASS: transaction rollback faults=3 reverse-order=1 pointer-clear=1 allocation-release=1 failed-restore-retention=1 commit=1\n");
    return 0;
}
