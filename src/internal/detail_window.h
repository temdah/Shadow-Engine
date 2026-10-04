#ifndef SHADOW_ENGINE_DETAIL_WINDOW_H
#define SHADOW_ENGINE_DETAIL_WINDOW_H

#ifndef SHADOW_ENGINE_DETAIL_WINDOWS
#define SHADOW_ENGINE_DETAIL_WINDOWS 0
#endif

enum DetailWindowPhase {
    DETAIL_WINDOW_OFF=0, DETAIL_WINDOW_OPEN=1,
    DETAIL_WINDOW_DRAINING=2, DETAIL_WINDOW_FROZEN=3
};
enum DetailWindowKind {
    DETAIL_WINDOW_ADMISSION=0, DETAIL_WINDOW_TAIL_BEFORE=1,
    DETAIL_WINDOW_TAIL_AFTER=2, DETAIL_WINDOW_KIND_COUNT=3
};
typedef struct DetailWindowStatus {
    LONG epoch,phase,change;
} DetailWindowStatus;
/* Only scalar diagnostic identities cross the native producer. TLS contains a
 * serial value, never this ticket's address or a borrowed history pointer.
 * Seal consumes the ticket even when optional metadata observation is lost. */
typedef struct DetailProducerTicket {
    LONG serial,epoch;
    unsigned control_index,bound;
    DWORD tls_index;
    uintptr_t previous_serial;
} DetailProducerTicket;

#if SHADOW_ENGINE_DETAIL_WINDOWS
/* Scalar scope tickets may cross native calls; metadata ownership may not.
 * Successful enter owns one nonwaiting lock and MUST pair with leave before
 * a native call, file output or returning from the callback. */
static LONG detail_window_epoch(void);
static int detail_window_enter(LONG epoch,unsigned kind);
static void detail_window_leave(void);
static DetailWindowStatus detail_window_status(void);
static void detail_window_toggle(void);
static void detail_window_mark(void);
static void detail_window_finish(void);
static void detail_window_poll(void);
#else
static LONG detail_window_epoch(void) { return 1; }
static int detail_window_enter(LONG epoch,unsigned kind)
{ (void)epoch; (void)kind; return 1; }
static void detail_window_leave(void) { }
static DetailWindowStatus detail_window_status(void)
{ DetailWindowStatus result={0}; return result; }
static void detail_window_toggle(void) { }
static void detail_window_mark(void) { }
static void detail_window_finish(void) { }
static void detail_window_poll(void) { }
#endif

#endif
