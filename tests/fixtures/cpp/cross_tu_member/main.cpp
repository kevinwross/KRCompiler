#include "device.hpp"

int main() {
    Device device;
    device.scale = 6;
    return device.reading() * 7;
}
