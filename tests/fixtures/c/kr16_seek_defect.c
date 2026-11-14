/* A KR16 seek leaves the descriptor unreadable: this fixture is a *reproduction*
   of a host-service defect, not a passing test. It returns 8 at the failing step.
   Keep it out of CTest until the VM is fixed; 	ests/CMakeLists.txt does not
   register it. */

int krc16_sys_open(const char*, unsigned);
int krc16_sys_close(int);
int krc16_sys_write(int, const void*, unsigned);
int krc16_sys_read(int, void*, unsigned);
long krc16_sys_seek(int, long);

static char name[] = "raw-seek2.txt";
static char buffer[8];

int main(void) {
    int fd = krc16_sys_open(name, 0x0301);
    if (fd < 0) { return 1; }
    if (krc16_sys_write(fd, "abcde", 5) != 5) { return 2; }
    if (krc16_sys_close(fd) != 0) { return 3; }

    fd = krc16_sys_open(name, 0);
    if (fd < 0) { return 4; }
    /* A read before any seek. */
    if (krc16_sys_read(fd, buffer, 1) != 1) { return 5; }
    if (buffer[0] != 'a') { return 6; }
    /* A seek back to zero, then a read. */
    if (krc16_sys_seek(fd, 0) != 0) { return 7; }
    if (krc16_sys_read(fd, buffer, 1) != 1) { return 8; }
    if (buffer[0] != 'a') { return 9; }
    /* A seek to the middle, then a read. */
    if (krc16_sys_seek(fd, 3) != 3) { return 10; }
    if (krc16_sys_read(fd, buffer, 1) != 1) { return 11; }
    if (buffer[0] != 'd') { return 12; }
    krc16_sys_close(fd);
    return 42;
}
