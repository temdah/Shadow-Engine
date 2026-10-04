/* Minimal x64 Windows ETW ABI for TinyCC's older Windows headers.
 * Field layout follows Microsoft evntrace/evntcons/tdh declarations. No event
 * payload layouts: TDH resolves named DXGI properties. See workload OKF.
 * Verified against independent SDK declarations by tests/etw_abi_probe.c. */
#ifndef SHADOW_ETW_ABI_H
#define SHADOW_ETW_ABI_H
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
typedef struct SETraceProperties {
    ULONG size,provider;
    uint64_t history,timestamp;
    GUID guid;
    ULONG clock,flags;
    ULONG buffer_kb,min_buffers,max_buffers,max_file_mb,mode,flush_seconds,enable_flags;
    LONG age;
    ULONG buffers,free_buffers,events_lost,written,log_lost,realtime_lost;
    HANDLE logger_thread;
    ULONG file_offset,name_offset;
} SETraceProperties;
typedef struct SEEventDescriptor {
    USHORT id;
    UCHAR version,channel,level,opcode;
    USHORT task;
    uint64_t keyword;
} SEEventDescriptor;
typedef struct SEEventHeader {
    USHORT size,type,flags,property;
    ULONG thread,pid;
    int64_t timestamp;
    GUID provider;
    SEEventDescriptor event;
    uint64_t processor;
    GUID activity;
} SEEventHeader;
typedef struct SEEventRecord {
    SEEventHeader header;
    ULONG buffer_context;
    USHORT extended_count,data_size;
    void *extended,*data,*context;
} SEEventRecord;
typedef struct SEClassicHeader {
    USHORT size,type;
    ULONG version,thread,pid;
    int64_t timestamp;
    GUID guid;
    uint64_t processor;
} SEClassicHeader;
typedef struct SEClassicEvent {
    SEClassicHeader header;
    ULONG instance,parent;
    GUID parent_guid;
    void *data;
    ULONG length,context;
} SEClassicEvent;
typedef struct SETraceHeader {
    ULONG buffer,version,provider_version,processors;
    int64_t end;
    ULONG resolution,max_file,mode,written;
    GUID instance;
    wchar_t *logger,*file;
    TIME_ZONE_INFORMATION zone;
    int64_t boot,frequency,start;
    ULONG flags,lost;
} SETraceHeader;
typedef struct SETraceLogfile {
    wchar_t *file,*logger;
    int64_t time;
    ULONG buffers_read,mode;
    SEClassicEvent current;
    SETraceHeader header;
    void *buffer_callback;
    ULONG buffer_size,filled,events_lost;
    void (WINAPI *event_callback)(SEEventRecord *);
    ULONG kernel;
    void *context;
} SETraceLogfile;
typedef struct SEFilter { uint64_t pointer; ULONG size,type; } SEFilter;
typedef struct SEEnable {
    ULONG version,property,flags;
    GUID source;
    SEFilter *filters;
    ULONG count;
} SEEnable;
typedef struct SEProperty { uint64_t name; ULONG index,reserved; } SEProperty;
/* Also enforce the independently checked x64 sizes in the TinyCC build. */
typedef char SETracePropertiesSize[(sizeof(SETraceProperties)==120)?1:-1];
typedef char SEEventRecordSize[(sizeof(SEEventRecord)==112)?1:-1];
typedef char SETraceLogfileSize[(sizeof(SETraceLogfile)==448)?1:-1];
typedef char SEEnableSize[(sizeof(SEEnable)==48)?1:-1];
#endif
