#ifndef KRC_HOST_H
#define KRC_HOST_H

/*
 * KR32 host-service wrappers. These are provided by the target runtime and
 * forward to the VM's SYSCALL vectors. Results are byte counts, descriptor
 * values, or the platform's negative error codes.
 */

enum {
    KRC_OPEN_READ = 0,
    KRC_OPEN_WRITE = 1,
    KRC_OPEN_READ_WRITE = 2,
    KRC_OPEN_CREATE = 4,
    KRC_OPEN_TRUNCATE = 8,
    KRC_OPEN_APPEND = 16
};

int getchar(void);
int open(const char *path, int flags);
int close(int descriptor);
int read(int descriptor, void *buffer, int count);
int write(int descriptor, const void *buffer, int count);
int seek(int descriptor, int offset, int whence);
int unlink(const char *path);
int rename(const char *old_path, const char *new_path);
int stat(const char *path, void *buffer, int capacity);
int truncate(const char *path, int length);
int getcwd(char *buffer, int capacity);
int chdir(const char *path);
int dup(int descriptor);
int dup2(int source, int target);
int sys_time(void);
int sys_random(void);
int sys_argc(void);
int sys_arg(int index, char *buffer, int size);

#endif
