#ifndef KRC_STDDEF_H
#define KRC_STDDEF_H

#include <bits/types.h>

#define NULL ((void *)0)
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
