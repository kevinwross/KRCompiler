#include "device.hpp"

Device::Device() {
    scale = 0;
}

int Device::reading() {
    return scale;
}
