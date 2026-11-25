#include <stdio.h>
#include <stdlib.h>

#include "game.hpp"
#include "geometry.hpp"

static const int navigationDx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
static const int navigationDy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};

Game::Game() {
    over_ = false;
    won_ = false;
    initialKlingons_ = 0;
}

Console& Game::console() {
    return console_;
}

Galaxy& Game::galaxy() {
    return galaxy_;
}

Ship& Game::ship() {
    return ship_;
}

bool Game::over() const {
    return over_;
}

bool Game::won() const {
    return won_;
}

void Game::initialize() {
    srand(1);
    over_ = false;
    won_ = false;
    ship_.reset();
    ship_.stardate = 3000;
    ship_.startStardate = 3000;
    ship_.endStardate = 3000 + 25 + rand() % 10;
    galaxy_.generate();
    initialKlingons_ = galaxy_.klingonsLeft();
    ship_.qx = 1 + rand() % ST_GALAXY;
    ship_.qy = 1 + rand() % ST_GALAXY;
    for (;;) {
        ship_.sx = 1 + rand() % ST_GALAXY;
        ship_.sy = 1 + rand() % ST_GALAXY;
        if (galaxy_.object(ship_.qx, ship_.qy, ship_.sx, ship_.sy) == ST_EMPTY) {
            break;
        }
    }
}

void Game::intro() {
    console_.line("Super Star Trek NG");
    console_.line("An object-oriented voyage for the KRC virtual machines.");
    console_.line("");
    printf("Your orders are to destroy the %d Klingon warships which have\n",
           initialKlingons_);
    printf("invaded the galaxy before they can attack Federation Headquarters\n");
    printf("on stardate %d. This gives you %d stardates.\n",
           ship_.endStardate, ship_.endStardate - ship_.stardate);
    if (galaxy_.starbasesLeft() == 1) {
        console_.line("There is 1 starbase in the galaxy for resupplying your ship.");
    } else {
        printf("There are %d starbases in the galaxy for resupplying your ship.\n",
               galaxy_.starbasesLeft());
    }
    console_.line("");
}

void Game::enterQuadrant(bool first) {
    galaxy_.markKnown(ship_.qx, ship_.qy);
    ship_.docked = false;
    char name[32];
    galaxy_.regionName(ship_.qx, ship_.qy, name, 32);
    if (first) {
        printf("Your mission begins in the %s quadrant.\n", name);
    } else {
        printf("Now entering the %s quadrant.\n", name);
    }
    int klingons = galaxy_.klingonsHere(ship_.qx, ship_.qy);
    if (klingons > 0) {
        console_.line("Combat area: condition red.");
        if (ship_.shields < 200) {
            console_.line("Shields are dangerously low.");
        }
    }
}

void Game::checkDocking() {
    if (galaxy_.baseNear(ship_.qx, ship_.qy, ship_.sx, ship_.sy)) {
        if (!ship_.docked) {
            ship_.dock();
            console_.line("Starbase docking complete. Energy and torpedoes replenished;");
            console_.line("all systems repaired and shields dropped.");
        }
    } else {
        ship_.docked = false;
    }
}

void Game::klingonsShoot() {
    klingonBatteries_.fire(*this);
}

void Game::checkTime() {
    if (ship_.stardate > ship_.endStardate) {
        console_.line("Starfleet Command has declared the mission a failure.");
        printf("It is stardate %d; the deadline was %d.\n",
               ship_.stardate, ship_.endStardate);
        over_ = true;
        won_ = false;
    }
}

void Game::repairDamage() {
    ship_.repairOneTurn(console_);
}

void Game::winGame() {
    console_.line("Congratulations, Captain. The last Klingon battle cruiser");
    console_.line("menacing the Federation has been destroyed.");
    int elapsed = ship_.stardate - ship_.startStardate;
    if (elapsed < 1) {
        elapsed = 1;
    }
    int rating = 1000 * initialKlingons_ * initialKlingons_ / (elapsed * elapsed);
    printf("Efficiency rating: %d\n", rating);
    over_ = true;
    won_ = true;
}

void Game::destroyEnterprise(const char* reason) {
    console_.line(reason);
    console_.line("The Enterprise has been destroyed. The Federation will be conquered.");
    over_ = true;
    won_ = false;
}

void Game::navigate() {
    int course = 0;
    if (!console_.readNumber("Course (1-8): ", 1, 8, &course)) {
        return;
    }
    int warp = 0;
    if (!console_.readNumber("Warp factor (0-8): ", 0, 8, &warp)) {
        return;
    }
    if (warp == 0) {
        console_.line("Helm answers: no movement ordered.");
        return;
    }
    ship_.docked = false;
    if (ship_.damaged(DEV_WARP) && warp > 1) {
        console_.line("Warp engines are damaged; maximum speed is warp 1.");
        return;
    }
    int steps = warp * 8;
    int cost = steps + 10;
    if (ship_.energy < cost) {
        console_.line("Insufficient energy for that maneuver.");
        return;
    }
    ship_.energy = ship_.energy - cost;
    int previousQx = ship_.qx;
    int previousQy = ship_.qy;
    for (int step = 0; step < steps; ++step) {
        int nx = ship_.sx + navigationDx[course];
        int ny = ship_.sy + navigationDy[course];
        int nqx = ship_.qx;
        int nqy = ship_.qy;
        if (nx < 1) {
            nqx = nqx - 1;
            nx = ST_GALAXY;
        } else if (nx > ST_GALAXY) {
            nqx = nqx + 1;
            nx = 1;
        }
        if (ny < 1) {
            nqy = nqy - 1;
            ny = ST_GALAXY;
        } else if (ny > ST_GALAXY) {
            nqy = nqy + 1;
            ny = 1;
        }
        if (nqx < 1 || nqx > ST_GALAXY || nqy < 1 || nqy > ST_GALAXY) {
            console_.line("Starfleet Command: permission to cross the galactic");
            console_.line("perimeter is denied. Engines shut down.");
            break;
        }
        if (galaxy_.object(nqx, nqy, nx, ny) != ST_EMPTY) {
            printf("Warp engines shut down at sector %d,%d due to bad navigation.\n",
                   ship_.sx, ship_.sy);
            break;
        }
        ship_.qx = nqx;
        ship_.qy = nqy;
        ship_.sx = nx;
        ship_.sy = ny;
        if (ship_.qx != previousQx || ship_.qy != previousQy) {
            enterQuadrant(false);
            previousQx = ship_.qx;
            previousQy = ship_.qy;
        }
    }
    checkDocking();
    ship_.stardate = ship_.stardate + 1;
    repairDamage();
    if (!over_ && galaxy_.klingonsHere(ship_.qx, ship_.qy) > 0) {
        klingonsShoot();
    }
    if (!over_) {
        checkTime();
    }
    if (!over_) {
        srs_.scan(*this);
    }
}

void Game::shieldControl() {
    if (ship_.damaged(DEV_SHIELDS)) {
        console_.line("Shield control inoperable.");
        return;
    }
    int available = ship_.energy + ship_.shields;
    if (available <= 0) {
        console_.line("No energy is available for the shields.");
        return;
    }
    printf("Energy available: %d\n", available);
    int value = 0;
    if (!console_.readNumber("Units to shields: ", 0, available - 1, &value)) {
        return;
    }
    ship_.energy = available - value;
    ship_.shields = value;
    printf("Shields now at %d units; engines at %d units.\n",
           ship_.shields, ship_.energy);
}

void Game::damageReport() {
    console_.line("Device                 State");
    for (int i = 1; i <= 8; ++i) {
        printf("%-22s %s\n", deviceName(i),
               ship_.damage_[i] < 0 ? "damaged" : "operational");
    }
    if (ship_.damaged(DEV_DAMAGE_CONTROL)) {
        console_.line("Damage control is damaged; no repairs are underway.");
    }
}

void Game::resign() {
    console_.line("You have resigned your commission.");
    printf("There were %d Klingon battle cruisers left.\n", galaxy_.klingonsLeft());
    over_ = true;
    won_ = false;
}

int Game::run() {
    initialize();
    intro();
    enterQuadrant(true);
    checkDocking();
    srs_.scan(*this);

    char line[64];
    while (!over_) {
        console_.text("\nCommand? ");
        if (!console_.readLine(line, 64)) {
            console_.line("\nInput ended.");
            break;
        }
        if (Console::startsWith3(line, "nav")) {
            navigate();
        } else if (Console::startsWith3(line, "srs")) {
            srs_.scan(*this);
        } else if (Console::startsWith3(line, "lrs")) {
            lrs_.scan(*this);
        } else if (Console::startsWith3(line, "pha")) {
            phasers_.fire(*this);
        } else if (Console::startsWith3(line, "tor")) {
            torpedoes_.fire(*this);
        } else if (Console::startsWith3(line, "she")) {
            shieldControl();
        } else if (Console::startsWith3(line, "dam")) {
            damageReport();
        } else if (Console::startsWith3(line, "com")) {
            computer_.run(*this);
        } else if (Console::startsWith3(line, "xxx")) {
            resign();
        } else if (Console::lower(line[0]) == 'q') {
            break;
        } else if (line[0] == 0) {
            /* An empty line is not a command. */
        } else {
            console_.line("Commands: nav srs lrs pha tor she dam com xxx (q quits)");
        }
        if (!over_) {
            checkTime();
        }
    }

    if (won_) {
        console_.line("Mission complete.");
    } else if (over_) {        printf("Mission ended with %d Klingon(s) remaining.\n",
               galaxy_.klingonsLeft());
    }
    return 0;
}
