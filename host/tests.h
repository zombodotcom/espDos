#ifndef TESTS_H
#define TESTS_H

/* Minimal test harness. Each test is a void(void) function registered with
 * REGISTER_TEST(); ASSERT(cond, msg) records failures without aborting so
 * a single test can report multiple problems. run_all_tests() runs every
 * registered test, calling reset_kernel_state() before each, and returns
 * the count of failed tests. */

typedef void (*test_fn_t)(void);

void register_test(const char *name, test_fn_t fn);
void test_assert(int cond, const char *expr, const char *file, int line);
int  run_all_tests(void);

/* Reset RAM disk + kernel global state to "freshly initialized 320 KB
 * empty volume". Called between tests for isolation. */
void reset_kernel_state(void);

#define REGISTER_TEST(fn) \
    static void __attribute__((constructor)) _register_##fn(void) { \
        register_test(#fn, fn); \
    }

#define ASSERT(cond) test_assert((cond), #cond, __FILE__, __LINE__)

#endif
