int open(const char *path, int flags);
int close(int descriptor);
int read(int descriptor, void *buffer, int count);
int write(int descriptor, const void *buffer, int count);
int seek(int descriptor, int offset, int whence);
int unlink(const char *path);
int rename(const char *old_path, const char *new_path);
int getchar(void);
int sys_time(void);
int sys_random(void);
int sys_argc(void);
int sys_arg(int index, char *buffer, int size);

int main(void) {
    const char *path = "kr32_fileio_test.txt";
    const char *renamed = "kr32_fileio_renamed.txt";
    unlink(path);
    unlink(renamed);
    int descriptor = open(path, 2 | 4 | 8);
    if (descriptor < 0) {
        return 1;
    }
    const char text[] = "hello";
    if (write(descriptor, text, 5) != 5) {
        return 2;
    }
    if (close(descriptor) != 0) {
        return 3;
    }
    if (rename(path, renamed) != 0) {
        return 4;
    }
    if (unlink(renamed) != 0) {
        return 5;
    }
    return 0;
}
