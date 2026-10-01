/*
 * qos_boost.dylib (DAR-439 test aid): DYLD_INSERT_LIBRARIES constructor that
 * raises the calling (main) thread to USER_INTERACTIVE, to A/B an unmodified
 * wmaker/Xvfb/Xorg against the same binary with the QoS call. (A wrapper that
 * sets QoS then exec()s does not work: exec resets the main thread's QoS to
 * the task's primordial QoS, osfmk/kern/task_policy.c:2061-2100.)
 * Prints the result to stderr when QOS_BOOST_VERBOSE is set.
 */
#include <pthread/qos.h>
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor)) static void qos_boost(void)
{
    int rc = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    if (getenv("QOS_BOOST_VERBOSE"))
        fprintf(stderr, "qos_boost: pthread_set_qos_class_self_np rc=%d\n", rc);
}
