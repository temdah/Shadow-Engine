#ifndef SHADOW_ENGINE_SUPPORT_REPORT_H
#define SHADOW_ENGINE_SUPPORT_REPORT_H
/* Module76 owns this state on the module75 mailbox worker. */
struct SupportReportState;
static int support_report_request(void);
static int support_report_active(void);
static void support_report_poll(void);
#endif
