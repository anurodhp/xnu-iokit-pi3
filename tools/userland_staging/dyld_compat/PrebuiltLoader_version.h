/* Real Apple build step (third_party/dyld/build-scripts/
 * prebuilt-loader-hash.sh) computes this as an md5 hash of several
 * generated .ast files, purely as a self-consistency magic number so a
 * given dyld build refuses to read a PrebuiltLoader cache written by a
 * DIFFERENT build of itself -- it never needs to match real Apple's own
 * dyld (this target never reads/writes real Apple prebuilt caches), so
 * any fixed, stable value is correct here. Not a real port of the hash
 * script, just its trivial fixed-output shape for a target that will
 * only ever compare its own PrebuiltLoader writes against its own
 * reads. */
#define PREBUILTLOADER_VERSION 0x00000001
