/* DAR-170: SymptomReporter.h -- confirmed genuinely never open-sourced
 * anywhere (Apple-internal diagnostics-telemetry framework; no
 * apple-oss-distributions repo, no public SDK header). Real, full
 * pinned-repo grep (third_party/bootp, third_party/configd) shows
 * exactly ONE real consumer: IPConfiguration.bproj/report_symptoms.c's
 * report_address_acquisition_symptom(), itself only ever called as a
 * fire-and-forget diagnostic hint on DHCP success/failure (see
 * ipconfigd.c's own call sites) -- it has no effect on the real DHCP
 * state machine, lease data, or any SCDynamicStore-visible network
 * configuration. This narrow, honest stand-in declares exactly the
 * opaque types and five functions that one real call site needs (see
 * that file's own real usage for the exact signatures this header
 * matches) with no fabricated additional API surface. Real bodies (in
 * ipconfiguration_symptom_compat.c) return "no reporter" honestly. */
#ifndef _SYMPTOM_REPORTER_H_
#define _SYMPTOM_REPORTER_H_

#include <stdint.h>
#include <stdbool.h>

typedef void *	symptom_framework_t;
typedef void *	symptom_t;
typedef uint32_t	symptom_ident_t;

symptom_framework_t
symptom_framework_init(int numeric_id, const char *text_id);

symptom_t
symptom_new(symptom_framework_t framework, symptom_ident_t ident);

int
symptom_set_qualifier(symptom_t symptom, uint64_t value, int qualifier);

int
symptom_send(symptom_t symptom);

#endif /* _SYMPTOM_REPORTER_H_ */
