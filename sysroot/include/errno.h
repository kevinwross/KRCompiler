#ifndef KRC_ERRNO_H
#define KRC_ERRNO_H

int *__krc_errno_location(void);

#define errno (*__krc_errno_location())

#define EDOM 33
#define ERANGE 34
#define EINVAL 22
#define ENOMEM 12
#define ENOENT 2

#endif
