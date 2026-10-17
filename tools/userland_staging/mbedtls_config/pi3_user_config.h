/*
 * Mbed TLS user config for this system (build_mbedtls.sh passes it as
 * MBEDTLS_USER_CONFIG_FILE; included after the stock mbedtls_config.h).
 *
 * The Pi 3's BCM2837 (Cortex-A53) has no ARMv8 Cryptography Extension, but
 * Xcode 12's clang defines __ARM_FEATURE_CRYPTO for every arm64-apple-ios
 * target (Apple's A7 onwards all have it), and aesce.c then takes AESE/AESMC
 * as present without asking the CPU. The first https:// connection died with
 * SIGILL in mbedtls_aesce_crypt_ecb. The table-based AES in aes.c does the work
 * instead.
 */
#undef MBEDTLS_AESCE_C
#undef MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_IF_PRESENT
#undef MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_ONLY
#undef MBEDTLS_SHA512_USE_A64_CRYPTO_IF_PRESENT
#undef MBEDTLS_SHA512_USE_A64_CRYPTO_ONLY
