#ifndef STARTREKNG_SHIP_HPP
#define STARTREKNG_SHIP_HPP

#include "console.hpp"

/* Device numbers used by damage_ and by the repair pass. */
enum DeviceId {
    DEV_NONE = 0,
    DEV_WARP = 1,
    DEV_SRS = 2,
    DEV_LRS = 3,
    DEV_PHASERS = 4,
    DEV_TORPEDOES = 5,
    DEV_DAMAGE_CONTROL = 6,
    DEV_SHIELDS = 7,
    DEV_COMPUTER = 8
};

const char* deviceName(int device);

/*
 * The Enterprise. A damaged device has a negative counter; it returns to zero
 * one point per turn when damage control is operational.
 */
class Ship {
public:
    Ship();

    void reset();
    void dock();
    void repairOneTurn(Console& console);
    void damage(int device, int amount);
    bool damaged(int device) const;

    int energy;
    int shields;
    int torpedoes;
    int qx;
    int qy;
    int sx;
    int sy;
    int stardate;
    int startStardate;
    int endStardate;
    bool docked;
    int damage_[9];
};

#endif
