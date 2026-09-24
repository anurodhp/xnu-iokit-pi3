/*
 * compat/iohid/IOKit/IOReporter.h -- DAR-245.
 *
 * IOHIDSystem.cpp:34 includes <IOKit/IOReporter.h> for IOHistogramReporter
 * and IOReportLegend (IOHIDSystem.cpp:572-593). This header is not in
 * xnu-7195. The same header IS in xnu-7195, under its exported name:
 * iokit/IOKit/IOKernelReporters.h, whose own file banner (line 30) still
 * says "FILE: IOReporter.h" and which declares IOHistogramReporter and
 * IOReportLegend. So this is a rename redirect, not a substitute.
 */
#ifndef _COMPAT_IOHID_IOREPORTER_H
#define _COMPAT_IOHID_IOREPORTER_H
#include <IOKit/IOKernelReporters.h>
#endif
