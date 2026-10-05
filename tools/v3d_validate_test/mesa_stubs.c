/* Link stubs for test_mesa_vc4 (kept out of that file: it includes Mesa headers whose
 * prototypes for these symbols would conflict). */
#include <stddef.h>
/* Debug-only parts of Mesa the driver references but does not call without
 * debug flags (CLIF dumps, scanout import through renderonly). */
void *clif_dump_init(void) { return NULL; }
void clif_dump_destroy(void *c) { (void)c; }
void *renderonly_create_gpu_import_for_resource(void) { return NULL; }
void renderonly_scanout_destroy(void) { }
int v3d_group_get_length(void) { return 0; }
const char *v3d_group_get_name(void) { return ""; }
void v3d_print_group(void) { }
void *v3d_spec_find_instruction(void) { return NULL; }
void *v3d_spec_load(void) { return NULL; }

