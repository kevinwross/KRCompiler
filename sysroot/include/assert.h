#ifndef KRC_ASSERT_H
#define KRC_ASSERT_H

void __krc_assert_fail(const char *expression, const char *file, int line);

#define assert(expression) ((void)0)

#endif
