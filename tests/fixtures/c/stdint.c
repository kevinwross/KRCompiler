#include <stdint.h>
#include <stddef.h>

uint32_t value = 1;

int main(void) {
    size_t size = sizeof(value);
    return (int)size;
}
