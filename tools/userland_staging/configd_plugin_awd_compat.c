/*
 * Real, narrow no-op stand-in for InterfaceAdvisoryReportCreate/Submit/
 * SetFlags/SetAdvisoryCount -- the plain-C-declared (IPMonitorAWDReport.h)
 * but ObjC-implemented (IPMonitorAWDReport.m, DAR-239's scope, untouched
 * here) AWD (Apple Wireless Diagnostics) telemetry-report shim
 * IPMonitorControl/IPMonitorControlServer.c calls into
 * (SubmitInterfaceAdvisoryMetric()).
 *
 * NOT a fabricated behavior: reading IPMonitorAWDReport.m's own real,
 * unmodified source (DAR-241 investigation, 2026-09-04) shows
 * InterfaceAdvisoryReportCreate() ALREADY has this exact real early-out:
 *
 *     if ([AWDServerConnection class] == nil) {
 *         return (NULL);
 *     }
 *
 * -- a real ObjC runtime class lookup for WirelessDiagnostics.framework.
 * This project has no objc4/WirelessDiagnostics.framework at all
 * (DAR-236/237), so on a real, unmodified macOS box with
 * WirelessDiagnostics.framework missing or not yet loaded,
 * `[AWDServerConnection class]` returns nil and this function ALREADY
 * returns NULL -- exactly the behavior implemented here.
 * IPMonitorControlServer.c's own real, existing caller
 * (SubmitInterfaceAdvisoryMetric(), ~line 320) already has a real
 * `if (report == NULL) { return; }` guard immediately after calling
 * InterfaceAdvisoryReportCreate() -- so with Create always returning
 * NULL, SetFlags/SetAdvisoryCount/Submit are real, linkable, but never
 * actually invoked from that real call site. AWD is a pure, one-way
 * analytics/telemetry report (network-interface-issue metrics sent to
 * an internal Apple analytics collector) -- it has zero effect on any
 * actual interface/DHCP/DNS configuration behavior either way, purely
 * informational, and this project has no AWD collector daemon to
 * receive a report even if one were generated.
 */
#include "IPMonitorAWDReport.h"

InterfaceAdvisoryReportRef
InterfaceAdvisoryReportCreate(AWDIPMonitorInterfaceType type)
{
	(void)type;
	return NULL;
}

void
InterfaceAdvisoryReportSubmit(InterfaceAdvisoryReportRef report)
{
	(void)report;
}

void
InterfaceAdvisoryReportSetFlags(InterfaceAdvisoryReportRef report,
				AWDIPMonitorInterfaceAdvisoryReport_Flags flags)
{
	(void)report;
	(void)flags;
}

void
InterfaceAdvisoryReportSetAdvisoryCount(InterfaceAdvisoryReportRef report,
					uint32_t count)
{
	(void)report;
	(void)count;
}
