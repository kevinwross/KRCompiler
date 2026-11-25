#ifndef KRC_FIXTURE_DEVICE_HPP
#define KRC_FIXTURE_DEVICE_HPP

/* A device whose constructor and reading() live in another translation unit. */
class Device {
public:
    Device();
    int reading();

    int scale;
};

#endif
