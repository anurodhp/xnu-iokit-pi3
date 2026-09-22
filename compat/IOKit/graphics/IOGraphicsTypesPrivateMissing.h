/*
 * IOGraphicsTypesPrivateMissing.h -- the ktrace codes IOGraphics-585's
 * own sources use and its own published header does not define.
 *
 * WHY THIS FILE EXISTS (DAR-331)
 *
 * Apple publishes IOGraphics as source, but the .cpp files in the drop
 * come from a newer internal revision than the headers shipped beside
 * them. IOGraphicsFamily/IOKit/graphics/IOGraphicsTypesPrivate.h defines
 * 84 DBG_IOG_* ktrace codes (:308-400); the sources reference 24 more
 * that are defined nowhere in the repository. Checked against three
 * published tags -- IOGraphics-530.66, -585 and -600 -- and all three
 * ship the same 84, so this is not a stale pin and there is no newer tag
 * to move to. tools/iographics_gap_audit.sh reproduces the exact list.
 *
 * WHAT THESE ARE. Nothing but tag numbers for trace events. Each one is
 * used only as the first argument of IOG_KTRACE()/IOG_KTRACE_NT()/
 * GMETRICFUNC(), i.e. as the `x` in IOKDBG_CODE(DBG_IOGRAPHICS, x)
 * (IOGraphicsKTrace.h:19-20), or as the DBG_IOG_SOURCE_xxx value passed
 * in arg1 of such an event to say which code path raised it. They do not
 * appear in any struct, ABI, IOKit property, user-client selector or
 * control flow -- verified by grepping every use of all 24 across the
 * whole pinned checkout, not assumed. Getting a number "wrong" therefore
 * cannot change kernel behaviour; it can only mislabel a trace record in
 * a decoder, and this project runs neither Apple's iogdiagnose nor any
 * other IOGraphics trace decoder.
 *
 * NUMBERING. Apple's own published scheme, continued rather than
 * invented: single-source event codes occupy 9-50 and multi-source ones
 * 100-102 (IOGraphicsTypesPrivate.h:308-352), so the 15 missing event
 * codes take 51-65 -- past the published block, below the multi-source
 * block, and deliberately NOT filling the one published hole at 34
 * (between DBG_IOG_CLAMSHELL 33 and DBG_IOG_HANDLE_VBL_INTERRUPT 35),
 * which is presumably one of these and cannot be identified. Source
 * codes run 1-34 (:357-390), so the 9 missing ones take 35-43.
 *
 * These are real values chosen here, not Apple's. If a future drop ever
 * publishes the real header, delete this file rather than merging it.
 *
 * Included by tools/iographics_gap_audit.sh (-include) so the audit
 * reports the gaps that still need real work rather than these; any
 * eventual vendoring into third_party/xnu-7195 must arrange the same
 * include, or every one of these comes back.
 */

#ifndef _IOKIT_IOGRAPHICSTYPESPRIVATE_MISSING_H
#define _IOKIT_IOGRAPHICSTYPESPRIVATE_MISSING_H

/*
 * Single-source event codes, continuing Apple's 9-50 block.
 * The cited line is the first use of each in the pinned sources.
 */
#define DBG_IOG_TIMELOCK                     51  // IOFramebuffer.cpp:747
#define DBG_IOG_CURSORLOCK                   52  // IOFramebuffer.cpp:1204
#define DBG_IOG_ASYNC_WORK                   53  // IOFramebuffer.cpp:1670
#define DBG_IOG_CAPTURED_RETRAIN             54  // IOFramebuffer.cpp:1811
#define DBG_IOG_CONNECT_WORK_ASYNC           55  // IOFramebuffer.cpp:1848
#define DBG_IOG_MSG_CONNECT_CHANGE           56  // IOFramebuffer.cpp:1924
#define DBG_IOG_SYSTEM_WORK                  57  // IOFramebuffer.cpp:6819
#define DBG_IOG_MUX_ACTIVITY_CHANGE          58  // IOFramebuffer.cpp:6844
#define DBG_IOG_EXT_END_CONNECT_CHANGE       59  // IOFramebuffer.cpp:7480
#define DBG_IOG_EXT_PROCESS_CONNECT_CHANGE   60  // IOFramebuffer.cpp:7533
#define DBG_IOG_CONNECT_CHANGE_INTERRUPT_V2  61  // IOFramebuffer.cpp:8853
#define DBG_IOG_FB_EXT_CLOSE                 62  // IOFramebuffer.cpp:10223
#define DBG_IOG_BUILTIN_PANEL_POWER          63  // IOFramebuffer.cpp:11783
#define DBG_IOG_SET_ATTR_FOR_CONN_EXT        64  // IOFramebuffer.cpp:13017
#define DBG_IOG_LOG_SYNCH                    65  // IODisplayWrangler.cpp:543

/*
 * Source codes, continuing Apple's 1-34 block. Each names the code path
 * that raised an event, and is passed in arg1 of the events above.
 */
#define DBG_IOG_SOURCE_SYSWORK_RESETCLAMSHELL_V2    35
#define DBG_IOG_SOURCE_SERVER_ACK_TIMEOUT           36
#define DBG_IOG_SOURCE_SET_TRANSFORM                37
#define DBG_IOG_SOURCE_SET_ATTR_FOR_CONN_EXT        38
#define DBG_IOG_SOURCE_OVERSCAN                     39
#define DBG_IOG_SOURCE_INIT_FB                      40
#define DBG_IOG_SOURCE_DO_SETUP                     41
#define DBG_IOG_SOURCE_DO_SET_DISPLAY_MODE          42
#define DBG_IOG_SOURCE_VENDOR                       43

#endif /* _IOKIT_IOGRAPHICSTYPESPRIVATE_MISSING_H */
