#include <stdlib.h>

#include "galaxy.hpp"

static const char* const kRegionNames[8] = {
    "Antares", "Rigel", "Procyon", "Vega",
    "Canopus", "Altair", "Sagittarius", "Pollux"
};

static const char* const kRegionSuffixes[4] = {"", " II", " III", " IV"};

Galaxy::Galaxy() {
    reset();
}

int Galaxy::index(int qx, int qy) const {
    return (qx - 1) * ST_GALAXY + (qy - 1);
}

void Galaxy::reset() {
    klingonsLeft_ = 0;
    starbasesLeft_ = 0;
    for (int i = 0; i < 64; ++i) {
        known_[i] = 0;
        for (int x = 0; x < 8; ++x) {
            for (int y = 0; y < 8; ++y) {
                objects_[i][x][y] = ST_EMPTY;
                slots_[i][x][y] = -1;
            }
        }
        for (int s = 0; s < 4; ++s) {
            hitPoints_[i][s] = 0;
        }
    }
}

void Galaxy::clearQuadrant(int qx, int qy) {
    const int at = index(qx, qy);
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            objects_[at][x][y] = ST_EMPTY;
            slots_[at][x][y] = -1;
        }
    }
    for (int s = 0; s < 4; ++s) {
        hitPoints_[at][s] = 0;
    }
}

bool Galaxy::placeRandom(int qx, int qy, int code, int slot) {
    const int at = index(qx, qy);
    for (int attempt = 0; attempt < 100; ++attempt) {
        int x = rand() % 8;
        int y = rand() % 8;
        if (objects_[at][x][y] == ST_EMPTY) {
            objects_[at][x][y] = (unsigned char)code;
            slots_[at][x][y] = (signed char)slot;
            return true;
        }
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (objects_[at][x][y] == ST_EMPTY) {
                objects_[at][x][y] = (unsigned char)code;
                slots_[at][x][y] = (signed char)slot;
                return true;
            }
        }
    }
    return false;
}

void Galaxy::generate() {
    reset();
    for (int qx = 1; qx <= ST_GALAXY; ++qx) {
        for (int qy = 1; qy <= ST_GALAXY; ++qy) {
            clearQuadrant(qx, qy);
            int roll = rand() % 100;
            int klingons = 0;
            if (roll > 98) {
                klingons = 3;
            } else if (roll > 95) {
                klingons = 2;
            } else if (roll > 80) {
                klingons = 1;
            }
            int base = rand() % 100 > 96 ? 1 : 0;
            int stars = rand() % 8;
            for (int i = 0; i < klingons; ++i) {
                if (placeRandom(qx, qy, ST_KLINGON, i)) {
                    hitPoints_[index(qx, qy)][i] = 100 + rand() % 200;
                    klingonsLeft_ = klingonsLeft_ + 1;
                }
            }
            if (base && placeRandom(qx, qy, ST_BASE, -1)) {
                starbasesLeft_ = starbasesLeft_ + 1;
            }
            for (int i = 0; i < stars; ++i) {
                placeRandom(qx, qy, ST_STAR, -1);
            }
        }
    }
    if (starbasesLeft_ == 0) {
        int qx = 1 + rand() % ST_GALAXY;
        int qy = 1 + rand() % ST_GALAXY;
        if (placeRandom(qx, qy, ST_BASE, -1)) {
            starbasesLeft_ = starbasesLeft_ + 1;
        }
    }
}

int Galaxy::object(int qx, int qy, int sx, int sy) const {
    if (qx < 1 || qx > ST_GALAXY || qy < 1 || qy > ST_GALAXY) {
        return ST_EMPTY;
    }
    if (sx < 1 || sx > 8 || sy < 1 || sy > 8) {
        return ST_EMPTY;
    }
    return objects_[index(qx, qy)][sx - 1][sy - 1];
}

int Galaxy::klingonSlot(int qx, int qy, int sx, int sy) const {
    return slots_[index(qx, qy)][sx - 1][sy - 1];
}

int Galaxy::klingonHp(int qx, int qy, int slot) const {
    if (slot < 0 || slot >= 4) {
        return 0;
    }
    return hitPoints_[index(qx, qy)][slot];
}

int Galaxy::klingonsHere(int qx, int qy) const {
    int count = 0;
    const int at = index(qx, qy);
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (objects_[at][x][y] == ST_KLINGON) {
                count = count + 1;
            }
        }
    }
    return count;
}

int Galaxy::starsHere(int qx, int qy) const {
    int count = 0;
    const int at = index(qx, qy);
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (objects_[at][x][y] == ST_STAR) {
                count = count + 1;
            }
        }
    }
    return count;
}

int Galaxy::quadrantCode(int qx, int qy) const {
    int klingons = 0;
    int bases = 0;
    int stars = 0;
    const int at = index(qx, qy);
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            int code = objects_[at][x][y];
            if (code == ST_KLINGON) {
                klingons = klingons + 1;
            } else if (code == ST_BASE) {
                bases = bases + 1;
            } else if (code == ST_STAR) {
                stars = stars + 1;
            }
        }
    }
    return klingons * 100 + bases * 10 + stars;
}

int Galaxy::klingonsLeft() const {
    return klingonsLeft_;
}

int Galaxy::starbasesLeft() const {
    return starbasesLeft_;
}

bool Galaxy::baseNear(int qx, int qy, int sx, int sy) const {
    for (int x = sx - 1; x <= sx + 1; ++x) {
        for (int y = sy - 1; y <= sy + 1; ++y) {
            if (x >= 1 && x <= 8 && y >= 1 && y <= 8 &&
                object(qx, qy, x, y) == ST_BASE) {
                return true;
            }
        }
    }
    return false;
}

bool Galaxy::isKnown(int qx, int qy) const {
    if (qx < 1 || qx > ST_GALAXY || qy < 1 || qy > ST_GALAXY) {
        return false;
    }
    return known_[index(qx, qy)] != 0;
}

void Galaxy::markKnown(int qx, int qy) {
    if (qx >= 1 && qx <= ST_GALAXY && qy >= 1 && qy <= ST_GALAXY) {
        known_[index(qx, qy)] = 1;
    }
}

void Galaxy::regionName(int qx, int qy, char* buffer, int capacity) const {
    const char* name = kRegionNames[((qx - 1) * 2 + (qy > 4 ? 1 : 0)) % 8];
    const char* suffix = kRegionSuffixes[(qy - 1) % 4];
    int used = 0;
    for (int i = 0; name[i] != 0 && used + 1 < capacity; ++i) {
        buffer[used] = name[i];
        used = used + 1;
    }
    for (int i = 0; suffix[i] != 0 && used + 1 < capacity; ++i) {
        buffer[used] = suffix[i];
        used = used + 1;
    }
    buffer[used] = 0;
}

void Galaxy::damageKlingon(int qx, int qy, int sx, int sy, int amount) {
    int slot = klingonSlot(qx, qy, sx, sy);
    if (slot < 0 || slot >= 4) {
        return;
    }
    hitPoints_[index(qx, qy)][slot] = hitPoints_[index(qx, qy)][slot] - amount;
}

void Galaxy::destroyKlingon(int qx, int qy, int sx, int sy) {
    int slot = klingonSlot(qx, qy, sx, sy);
    const int at = index(qx, qy);
    objects_[at][sx - 1][sy - 1] = ST_EMPTY;
    slots_[at][sx - 1][sy - 1] = -1;
    if (slot >= 0 && slot < 4) {
        hitPoints_[at][slot] = 0;
    }
    klingonsLeft_ = klingonsLeft_ - 1;
}

void Galaxy::destroyStarbase(int qx, int qy, int sx, int sy) {
    const int at = index(qx, qy);
    objects_[at][sx - 1][sy - 1] = ST_EMPTY;
    starbasesLeft_ = starbasesLeft_ - 1;
}
