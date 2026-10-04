/* Independent Windows SDK/MinGW layout cross-check, not a game executable. */
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include "../src/internal/etw_abi.h"
#define CHECK(name,condition) typedef char name[(condition)?1:-1]
CHECK(properties_size,sizeof(SETraceProperties)==sizeof(EVENT_TRACE_PROPERTIES));
CHECK(properties_name,offsetof(SETraceProperties,name_offset)==offsetof(EVENT_TRACE_PROPERTIES,LoggerNameOffset));
CHECK(properties_lost,offsetof(SETraceProperties,events_lost)==offsetof(EVENT_TRACE_PROPERTIES,EventsLost));
CHECK(header_size,sizeof(SEEventHeader)==sizeof(EVENT_HEADER));
CHECK(event_size,sizeof(SEEventRecord)==sizeof(EVENT_RECORD));
CHECK(event_context,offsetof(SEEventRecord,context)==offsetof(EVENT_RECORD,UserContext));
CHECK(log_size,sizeof(SETraceLogfile)==sizeof(EVENT_TRACE_LOGFILEW));
CHECK(log_header,offsetof(SETraceLogfile,header)==offsetof(EVENT_TRACE_LOGFILEW,LogfileHeader));
CHECK(log_callback,offsetof(SETraceLogfile,event_callback)==offsetof(EVENT_TRACE_LOGFILEW,EventRecordCallback));
CHECK(log_context,offsetof(SETraceLogfile,context)==offsetof(EVENT_TRACE_LOGFILEW,Context));
CHECK(enable_size,sizeof(SEEnable)==sizeof(ENABLE_TRACE_PARAMETERS));
CHECK(enable_filters,offsetof(SEEnable,filters)==offsetof(ENABLE_TRACE_PARAMETERS,EnableFilterDesc));
CHECK(property_size,sizeof(SEProperty)==sizeof(PROPERTY_DATA_DESCRIPTOR));
int main(void) { return 0; }
