#include <unistd.h>

int main(void) {
    char buffer[128];
    if (getcwd(buffer, sizeof(buffer)) <= 0 || buffer[0] == 0) {
        return 1;
    }
    if (chdir(".") != 0) {
        return 2;
    }
    return 0;
}
