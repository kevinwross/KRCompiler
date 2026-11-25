#ifndef KRC_FIXTURE_SENSOR_HPP
#define KRC_FIXTURE_SENSOR_HPP

/* A polymorphic interface whose vtable and methods live in another TU. */
class Sensor {
public:
    virtual ~Sensor();
    virtual int reading();
};

class FixedSensor : public Sensor {
public:
    int reading();
};

#endif
