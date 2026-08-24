/* Module 15: reversible executable-write transactions.
 *
 * Every active code mutation is journaled before it is written. A failed
 * phase restores bytes in reverse order, clears published trampoline pointers
 * and releases relay/trampoline allocations. Successful transactions retain
 * their executable allocations for the lifetime of the ASI. */
#define PATCH_TRANSACTION_MAX_WRITES 96U
#define PATCH_TRANSACTION_MAX_WRITE_SIZE 32U
#define PATCH_TRANSACTION_MAX_ALLOCATIONS 24U

typedef struct PatchWriteRecord {
    unsigned char *target;
    size_t size;
    unsigned char original[PATCH_TRANSACTION_MAX_WRITE_SIZE];
} PatchWriteRecord;

typedef struct PatchAllocationRecord {
    void *allocation;
    void **published_pointer;
} PatchAllocationRecord;

typedef struct PatchTransaction {
    const char *name;
    PatchWriteRecord writes[PATCH_TRANSACTION_MAX_WRITES];
    PatchAllocationRecord allocations[PATCH_TRANSACTION_MAX_ALLOCATIONS];
    size_t write_count;
    size_t allocation_count;
    LONG write_attempts;
    LONG fail_after_write;
    int active;
} PatchTransaction;

typedef struct PatchRollbackResult {
    size_t restored_writes;
    size_t failed_restores;
    size_t released_allocations;
} PatchRollbackResult;

static PatchTransaction *g_active_patch_transaction;

static int patch_transaction_begin(PatchTransaction *transaction,
                                   const char *name,LONG fail_after_write)
{
    if(!transaction || g_active_patch_transaction) return 0;
    memset(transaction,0,sizeof(*transaction));
    transaction->name=name;
    transaction->fail_after_write=fail_after_write;
    transaction->active=1;
    g_active_patch_transaction=transaction;
    return 1;
}

static void *patch_allocate(size_t size,void **published_pointer)
{
    PatchTransaction *transaction=g_active_patch_transaction;
    PatchAllocationRecord *record;
    void *allocation;
    if(!transaction || !transaction->active || !size ||
       transaction->allocation_count>=PATCH_TRANSACTION_MAX_ALLOCATIONS)
        return NULL;
    allocation=VirtualAlloc(NULL,size,MEM_COMMIT|MEM_RESERVE,
                            PAGE_EXECUTE_READWRITE);
    if(!allocation) return NULL;
    record=&transaction->allocations[transaction->allocation_count++];
    record->allocation=allocation;
    record->published_pointer=published_pointer;
    if(published_pointer) *published_pointer=NULL;
    return allocation;
}

static int patch_write_bytes(unsigned char *target,const void *data,size_t size)
{
    PatchTransaction *transaction=g_active_patch_transaction;
    PatchWriteRecord *record;
    LONG attempt;
    if(!transaction || !transaction->active || !target || !data || !size ||
       size>PATCH_TRANSACTION_MAX_WRITE_SIZE ||
       transaction->write_count>=PATCH_TRANSACTION_MAX_WRITES)
        return 0;
    attempt=transaction->write_attempts++;
    if(transaction->fail_after_write>=0 &&
       attempt==transaction->fail_after_write)
        return 0;
    record=&transaction->writes[transaction->write_count];
    record->target=target;
    record->size=size;
    memcpy(record->original,target,size);
    if(!raw_write_bytes(target,data,size)) return 0;
    ++transaction->write_count;
    return 1;
}

static PatchRollbackResult patch_transaction_rollback(
    PatchTransaction *transaction)
{
    PatchRollbackResult result;
    size_t index;
    memset(&result,0,sizeof(result));
    if(!transaction || !transaction->active ||
       g_active_patch_transaction!=transaction)
        return result;
    for(index=transaction->write_count;index>0;--index) {
        PatchWriteRecord *record=&transaction->writes[index-1];
        if(raw_write_bytes(record->target,record->original,record->size))
            ++result.restored_writes;
        else
            ++result.failed_restores;
    }
    /* Never free executable memory if even one target could not be restored:
     * an unreverted jump may still reference its relay or trampoline. Leaking
     * on this exceptional path is safer than publishing freed executable
     * memory. */
    if(result.failed_restores==0) {
        for(index=transaction->allocation_count;index>0;--index) {
            PatchAllocationRecord *record=&transaction->allocations[index-1];
            if(record->published_pointer) *record->published_pointer=NULL;
            if(record->allocation &&
               VirtualFree(record->allocation,0,MEM_RELEASE))
                ++result.released_allocations;
        }
    }
    transaction->active=0;
    g_active_patch_transaction=NULL;
    return result;
}

static int patch_transaction_commit(PatchTransaction *transaction)
{
    if(!transaction || !transaction->active ||
       g_active_patch_transaction!=transaction)
        return 0;
    transaction->active=0;
    g_active_patch_transaction=NULL;
    return 1;
}
