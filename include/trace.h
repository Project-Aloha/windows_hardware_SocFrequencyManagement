// Minimal trace.h for SocFrequencyManagement WPP
// This file provides WPP markers for tracewpp and a simple TraceEvents
// fallback for build-time processing and debug printing.

#ifndef QCOM_CPUFREQ_TRACE_H
#define QCOM_CPUFREQ_TRACE_H

// WPP configuration markers consumed by tracewpp
// begin_wpp config
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...)
// end_wpp

#include <wdm.h>

// Fallback implementation when WPP-generated code is not used yet.
#ifndef TraceEvents
#define TraceEvents(LEVEL, FLAGS, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, __VA_ARGS__)
#endif

#endif // QCOM_CPUFREQ_TRACE_H
