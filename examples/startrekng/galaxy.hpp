#ifndef STARTREKNG_GALAXY_HPP
#define STARTREKNG_GALAXY_HPP

/* Sector contents and the galaxy's shape. */
enum SectorContent {
    ST_EMPTY = 0,
    ST_STAR = 1,
    ST_KLINGON = 2,
    ST_BASE = 3
};

enum GalaxyShape {
    ST_GALAXY = 8,
    ST_KLINGON_SLOTS = 4
};

/*
 * The galaxy is 8x8 quadrants, each an 8x8 grid of sectors. It owns every
 * object and answers questions about them; it never prints.
 */
class Galaxy {
public:
    Galaxy();

    void reset();
    void generate();

    int object(int qx, int qy, int sx, int sy) const;
    int klingonSlot(int qx, int qy, int sx, int sy) const;
    int klingonHp(int qx, int qy, int slot) const;
    int klingonsHere(int qx, int qy) const;
    int starsHere(int qx, int qy) const;
    int quadrantCode(int qx, int qy) const;
    int klingonsLeft() const;
    int starbasesLeft() const;

    bool baseNear(int qx, int qy, int sx, int sy) const;
    bool isKnown(int qx, int qy) const;
    void markKnown(int qx, int qy);
    void regionName(int qx, int qy, char* buffer, int capacity) const;

    void damageKlingon(int qx, int qy, int sx, int sy, int amount);
    void destroyKlingon(int qx, int qy, int sx, int sy);
    void destroyStarbase(int qx, int qy, int sx, int sy);

private:
    int index(int qx, int qy) const;
    void clearQuadrant(int qx, int qy);
    bool placeRandom(int qx, int qy, int code, int slot);

    unsigned char objects_[64][8][8];
    signed char slots_[64][8][8];
    int hitPoints_[64][4];
    unsigned char known_[64];
    int klingonsLeft_;
    int starbasesLeft_;
};

#endif
