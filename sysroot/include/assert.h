#ifndef KRC_ASSERT_H
#define KRC_ASSERT_H

void __krc_assert_fail(const char *expression, const char *file, int line);

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) do { if (!(expression)) __krc_assert_fail("assertion", __FILE__, __LINE__); } while (0)
#endif

#endif
