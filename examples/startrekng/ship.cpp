#include "ship.hpp"

const char* deviceName(int device) {
    switch (device) {
    case DEV_WARP:
        return "Warp Engines";
    case DEV_SRS:
        return "Short Range Sensors";
    case DEV_LRS:
        return "Long Range Sensors";
    case DEV_PHASERS:
        return "Phaser Control";
    case DEV_TORPEDOES:
        return "Photon Tubes";
    case DEV_DAMAGE_CONTROL:
        return "Damage Control";
    case DEV_SHIELDS:
        return "Shield Control";
    case DEV_COMPUTER:
        return "Library Computer";
    default:
        return "Unknown";
    }
}

Ship::Ship() {
    reset();
}

void Ship::reset() {
    energy = 3000;
    shields = 0;
    torpedoes = 10;
    stardate = 3000;
    startStardate = stardate;
    endStardate = stardate;
    docked = false;
    qx = 1;
    qy = 1;
    sx = 1;
    sy = 1;
    for (int i = 0; i < 9; ++i) {
        damage_[i] = 0;
    }
}

void Ship::dock() {
    docked = true;
    energy = 3000;
    shields = 0;
    torpedoes = 10;
    for (int i = 1; i <= 8; ++i) {
        damage_[i] = 0;
    }
}

void Ship::damage(int device, int amount) {
    if (device >= 1 && device <= 8) {
        damage_[device] = damage_[device] - amount;
    }
}

bool Ship::damaged(int device) const {
    if (device < 1 || device > 8) {
        return false;
    }
    return damage_[device] < 0;
}

void Ship::repairOneTurn(Console& console) {
    if (damaged(DEV_DAMAGE_CONTROL)) {
        return;
    }
    for (int i = 1; i <= 8; ++i) {
        if (damage_[i] < 0) {
            damage_[i] = damage_[i] + 1;
            if (damage_[i] >= 0) {
                damage_[i] = 0;
                console.text(deviceName(i));
                console.line(" repair completed.");
            }
        }
    }
}
