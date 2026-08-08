#ifndef VAULT_TEST_FRAMEWORK_H
#define VAULT_TEST_FRAMEWORK_H

#include <stdio.h>

static int g_test_failures = 0;
static int g_test_count = 0;

#define TEST_ASSERT(cond) \
    do { \
        g_test_count++; \
        if (!(cond)) { \
            g_test_failures++; \
            printf("FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define TEST_ASSERT_EQ_INT(expected, actual) \
    do { \
        g_test_count++; \
        long _e = (long)(expected); \
        long _a = (long)(actual); \
        if (_e != _a) { \
            g_test_failures++; \
            printf("FAIL: %s:%d: expected %ld, got %ld\n", __FILE__, __LINE__, _e, _a); \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        printf("RUN  %s\n", #fn); \
        fn(); \
    } while (0)

#endif /* VAULT_TEST_FRAMEWORK_H */
