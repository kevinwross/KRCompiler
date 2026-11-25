#ifndef STARTREKNG_WEAPONS_HPP
#define STARTREKNG_WEAPONS_HPP

class Game;

/*
 * A weapon system is a device the game can order to fire. The ship's two
 * systems and the Klingon batteries share the interface, so the command loop
 * does not know which is which.
 */
class WeaponSystem {
public:
    virtual ~WeaponSystem() {
    }

    virtual bool available(Game& game) = 0;
    virtual void fire(Game& game) = 0;
};

class PhaserBank : public WeaponSystem {
public:
    bool available(Game& game);
    void fire(Game& game);
};

class TorpedoTube : public WeaponSystem {
public:
    bool available(Game& game);
    void fire(Game& game);
};

class KlingonBatteries {
public:
    void fire(Game& game);
};

#endif
