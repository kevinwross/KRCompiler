#include "sensor.hpp"

int main() {
    FixedSensor fixed;
    Sensor* sensor = &fixed;
    return sensor->reading();
}
