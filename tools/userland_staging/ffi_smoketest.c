/* DAR-388: libffi smoketest (build_ffi_smoketest.sh). Exercises the two
 * paths gnustep-base's NSInvocation/forwarding uses:
 *   1. ffi_call of a function with mixed int/double/pointer arguments;
 *   2. an ffi closure: ffi_closure_alloc + ffi_prep_closure_loc, then a
 *      call through the returned code pointer. On arm64 Darwin this is the
 *      trampoline-table path (fficonfig_arm64.h FFI_EXEC_TRAMPOLINE_TABLE):
 *      closures.c dlopen()s /usr/lib/libffi-trampolines.dylib and
 *      vm_remap()s its trampoline page.
 * Prints one PASS/FAIL line per case and a final PASS/FAIL. */
#include <ffi.h>
#include <stdio.h>
#include <string.h>

static double mix(int a, double b, const char *s, long c)
{
    return a + b + (double)strlen(s) + (double)c;
}

static void adder(ffi_cif *cif, void *ret, void **args, void *user)
{
    (void)cif;
    long base = *(long *)user;
    *(ffi_arg *)ret = (ffi_arg)(base + *(int *)args[0] + *(int *)args[1]);
}

int main(void)
{
    int failures = 0;

    ffi_cif cif;
    ffi_type *at[4] = { &ffi_type_sint, &ffi_type_double, &ffi_type_pointer, &ffi_type_slong };
    int a = 2; double b = 0.5; const char *s = "four"; long c = 10;
    void *av[4] = { &a, &b, &s, &c };
    double r = 0;
    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 4, &ffi_type_double, at) != FFI_OK) {
        printf("FAIL: ffi_prep_cif\n"); failures++;
    } else {
        ffi_call(&cif, FFI_FN(mix), &r, av);
        int ok = r == 16.5;
        printf("%s: ffi_call mix(2, 0.5, \"four\", 10) = %g\n", ok ? "PASS" : "FAIL", r);
        failures += !ok;
    }

    void *code = NULL;
    ffi_closure *cl = ffi_closure_alloc(sizeof(ffi_closure), &code);
    ffi_cif ccif;
    ffi_type *ct[2] = { &ffi_type_sint, &ffi_type_sint };
    static long base = 100;
    if (!cl || !code) {
        printf("FAIL: ffi_closure_alloc returned %p/%p\n", (void *)cl, code); failures++;
    } else if (ffi_prep_cif(&ccif, FFI_DEFAULT_ABI, 2, &ffi_type_sint, ct) != FFI_OK ||
               ffi_prep_closure_loc(cl, &ccif, adder, &base, code) != FFI_OK) {
        printf("FAIL: ffi_prep_closure_loc\n"); failures++;
    } else {
        int (*fn)(int, int) = (int (*)(int, int))code;
        int got = fn(20, 3);
        int ok = got == 123;
        printf("%s: closure(20, 3) with base 100 = %d (code %p)\n", ok ? "PASS" : "FAIL", got, code);
        failures += !ok;
        ffi_closure_free(cl);
    }

    printf("ffi_smoketest: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
