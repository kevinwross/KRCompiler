#include "sensor.hpp"

Sensor::~Sensor() {
}

int Sensor::reading() {
    return 1;
}

int FixedSensor::reading() {
    return 42;
}
