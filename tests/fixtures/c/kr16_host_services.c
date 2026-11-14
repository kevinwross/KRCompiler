/* The KR16 host-service wrappers: the version call reports ABI version 1, and a
   file written through the wrappers reads back unchanged. Returns 42. */

unsigned krc16_sys_abi_version(void);
unsigned krc16_sys_capabilities(void);
int krc16_sys_open(const char* path, unsigned flags);
int krc16_sys_close(int fd);
int krc16_sys_read(int fd, void* buffer, unsigned count);
int krc16_sys_write(int fd, const void* buffer, unsigned count);
long krc16_sys_seek(int fd, long offset, int whence);
int krc16_sys_unlink(const char* path);

#define OPEN_CREATE 0x0100
#define OPEN_TRUNC 0x0200

static char name[] = "krc16-host-test.txt";
static char written[] = "host";
static char read_back[8];

int main(void) {
    if (krc16_sys_abi_version() != 1) {
        return 1;
    }
    /* The file capability bits are set when the host file system is enabled. */
    unsigned capabilities = krc16_sys_capabilities();
    if ((capabilities & 1u) == 0u) {
        return 2;
    }

    int fd = krc16_sys_open(name, 1 | OPEN_CREATE | OPEN_TRUNC);
    if (fd < 0) {
        return 3;
    }
    if (krc16_sys_write(fd, written, 4) != 4) {
        return 4;
    }
    if (krc16_sys_close(fd) != 0) {
        return 5;
    }

    fd = krc16_sys_open(name, 0);
    if (fd < 0) {
        return 6;
    }
    if (krc16_sys_read(fd, read_back, 4) != 4) {
        return 7;
    }
    if (read_back[0] != 'h' || read_back[1] != 'o' || read_back[2] != 's' ||
        read_back[3] != 't') {
        return 8;
    }
    if (krc16_sys_close(fd) != 0) {
        return 9;
    }
    if (krc16_sys_unlink(name) != 0) {
        return 10;
    }
    return 42;
}
