#ifndef KRC_UNISTD_H
#define KRC_UNISTD_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 4
#define O_TRUNC 8
#define O_APPEND 16

int open(const char *path, int flags);
int close(int descriptor);
int read(int descriptor, void *buffer, int count);
int write(int descriptor, const void *buffer, int count);
int seek(int descriptor, int offset, int whence);
int unlink(const char *path);
int getcwd(char *buffer, int capacity);
int chdir(const char *path);
int dup(int descriptor);
int dup2(int source, int target);
int truncate(const char *path, int length);
int stat(const char *path, void *buffer, int capacity);

#endif
