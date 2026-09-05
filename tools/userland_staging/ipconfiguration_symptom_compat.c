/* DAR-170: real, honest no-op bodies for SymptomReporter.h's compat
 * declarations (see that header's own comment). report_symptoms.c's own
 * real caller already treats "no reporter" (NULL return from
 * symptom_framework_init()) as a legitimate real outcome -- it just
 * skips sending the diagnostic symptom and returns false from
 * report_address_acquisition_symptom(), which nothing in the real DHCP
 * state machine (ipconfigd.c) ever checks the return value of. This is
 * not a functional shortcut: no real Apple system without the
 * SymptomReporter daemon running (or, here, without the framework
 * existing at all) behaves any differently either. */
#include "SymptomReporter/SymptomReporter.h"
#include <stddef.h>

symptom_framework_t
symptom_framework_init(int numeric_id, const char *text_id)
{
	(void)numeric_id;
	(void)text_id;
	return NULL;
}

symptom_t
symptom_new(symptom_framework_t framework, symptom_ident_t ident)
{
	(void)framework;
	(void)ident;
	return NULL;
}

int
symptom_set_qualifier(symptom_t symptom, uint64_t value, int qualifier)
{
	(void)symptom;
	(void)value;
	(void)qualifier;
	return -1;
}

int
symptom_send(symptom_t symptom)
{
	(void)symptom;
	return -1;
}
