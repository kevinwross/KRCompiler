#ifndef STARTREKNG_GAME_HPP
#define STARTREKNG_GAME_HPP

#include "computer.hpp"
#include "console.hpp"
#include "galaxy.hpp"
#include "scanner.hpp"
#include "ship.hpp"
#include "weapons.hpp"

/*
 * Game owns the ship, the galaxy and one object per device interface. Devices
 * receive Game& and use its accessors; Game drives the command loop and the
 * turn order.
 */
class Game {
public:
    Game();
    int run();

    Console& console();
    Galaxy& galaxy();
    Ship& ship();

    bool over() const;
    bool won() const;

    void enterQuadrant(bool first);
    void checkDocking();
    void klingonsShoot();
    void checkTime();
    void repairDamage();
    void winGame();
    void destroyEnterprise(const char* reason);
    void navigate();
    void shieldControl();
    void damageReport();
    void resign();
    void intro();

private:
    void initialize();

    Console console_;
    Galaxy galaxy_;
    Ship ship_;
    ShortRangeScanner srs_;
    LongRangeScanner lrs_;
    PhaserBank phasers_;
    TorpedoTube torpedoes_;
    LibraryComputer computer_;
    KlingonBatteries klingonBatteries_;
    bool over_;
    bool won_;
    int initialKlingons_;
};

#endif
