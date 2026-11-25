#include <stdio.h>
#include <stdlib.h>

#include "game.hpp"
#include "geometry.hpp"
#include "ship.hpp"
#include "weapons.hpp"

static const int weaponDx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
static const int weaponDy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};

static bool insideGalaxy(int value) {
    return value >= 1 && value <= ST_GALAXY;
}

bool PhaserBank::available(Game& game) {
    if (game.ship().damaged(DEV_PHASERS)) {
        game.console().line("Phasers inoperative.");
        return false;
    }
    return true;
}

void PhaserBank::fire(Game& game) {
    if (!available(game)) {
        return;
    }
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    int klingons = galaxy.klingonsHere(ship.qx, ship.qy);
    if (klingons == 0) {
        game.console().line("Sensors show no enemy ships in this quadrant.");
        return;
    }
    printf("Energy available: %d\n", ship.energy);
    int fire = 0;
    if (!game.console().readNumber("Units to fire: ", 1, ship.energy, &fire)) {
        return;
    }
    ship.energy = ship.energy - fire;
    int perTarget = fire / klingons;
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (galaxy.object(ship.qx, ship.qy, x + 1, y + 1) != ST_KLINGON) {
                continue;
            }
            int slot = galaxy.klingonSlot(ship.qx, ship.qy, x + 1, y + 1);
            if (slot < 0 || slot > 2 || galaxy.klingonHp(ship.qx, ship.qy, slot) <= 0) {
                continue;
            }
            int distance = stDistance(ship.sx, ship.sy, x + 1, y + 1);
            if (distance < 1) {
                distance = 1;
            }
            int hit = perTarget * (2 + rand() % 2) / distance;
            if (ship.damaged(DEV_COMPUTER)) {
                hit = hit / 2;
            }
            if (hit < 1) {
                hit = 1;
            }
            galaxy.damageKlingon(ship.qx, ship.qy, x + 1, y + 1, hit);
            printf("%d unit hit on Klingon at sector %d,%d\n", hit, x + 1, y + 1);
            if (galaxy.klingonHp(ship.qx, ship.qy, slot) <= 0) {
                galaxy.destroyKlingon(ship.qx, ship.qy, x + 1, y + 1);
                printf("*** Klingon at sector %d,%d destroyed ***\n", x + 1, y + 1);
            } else {
                printf("  (%d units remaining)\n",
                       galaxy.klingonHp(ship.qx, ship.qy, slot));
            }
        }
    }
    if (galaxy.klingonsLeft() <= 0) {
        game.winGame();
        return;
    }
    game.klingonsShoot();
}

bool TorpedoTube::available(Game& game) {
    if (game.ship().damaged(DEV_TORPEDOES)) {
        game.console().line("Photon tubes are not operational.");
        return false;
    }
    if (game.ship().torpedoes <= 0) {
        game.console().line("All photon torpedoes have been expended.");
        return false;
    }
    if (game.ship().energy < 2) {
        game.console().line("Insufficient energy to launch a torpedo.");
        return false;
    }
    return true;
}

void TorpedoTube::fire(Game& game) {
    if (!available(game)) {
        return;
    }
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    int course = 0;
    if (!game.console().readNumber("Course (1-8): ", 1, 8, &course)) {
        return;
    }
    ship.torpedoes = ship.torpedoes - 1;
    ship.energy = ship.energy - 2;
    int x = ship.sx;
    int y = ship.sy;
    int tq1 = ship.qx;
    int tq2 = ship.qy;
    game.console().line("Torpedo track:");
    for (;;) {
        x = x + weaponDx[course];
        y = y + weaponDy[course];
        if (x < 1 || x > 8 || y < 1 || y > 8) {
            int nq1 = tq1;
            int nq2 = tq2;
            if (x < 1) {
                nq1 = nq1 - 1;
                x = 8;
            } else if (x > 8) {
                nq1 = nq1 + 1;
                x = 1;
            }
            if (y < 1) {
                nq2 = nq2 - 1;
                y = 8;
            } else if (y > 8) {
                nq2 = nq2 + 1;
                y = 1;
            }
            if (!insideGalaxy(nq1) || !insideGalaxy(nq2)) {
                game.console().line("Torpedo missed.");
                break;
            }
            tq1 = nq1;
            tq2 = nq2;
            galaxy.markKnown(tq1, tq2);
        }
        printf("  %d,%d\n", x, y);
        int code = galaxy.object(tq1, tq2, x, y);
        if (code == ST_KLINGON) {
            galaxy.destroyKlingon(tq1, tq2, x, y);
            printf("*** Klingon at sector %d,%d destroyed ***\n", x, y);
            break;
        }
        if (code == ST_STAR) {
            printf("Star at sector %d,%d absorbed the torpedo.\n", x, y);
            break;
        }
        if (code == ST_BASE) {
            galaxy.destroyStarbase(tq1, tq2, x, y);
            printf("*** Starbase at sector %d,%d destroyed ***\n", x, y);
            game.console().line("Starfleet Command is reviewing your record for court martial.");
            break;
        }
    }
    if (galaxy.klingonsLeft() <= 0) {
        game.winGame();
        return;
    }
    game.klingonsShoot();
}

void KlingonBatteries::fire(Game& game) {
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    if (ship.docked) {
        game.console().line("Starbase shields protect the Enterprise.");
        return;
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (galaxy.object(ship.qx, ship.qy, x + 1, y + 1) != ST_KLINGON) {
                continue;
            }
            int slot = galaxy.klingonSlot(ship.qx, ship.qy, x + 1, y + 1);
            if (slot < 0 || slot > 2) {
                continue;
            }
            int distance = stDistance(ship.sx, ship.sy, x + 1, y + 1);
            if (distance < 1) {
                distance = 1;
            }
            int hit = galaxy.klingonHp(ship.qx, ship.qy, slot) * (2 + rand() % 2) / distance;
            if (hit < 1) {
                hit = 1;
            }
            if (ship.shields <= 0) {
                game.destroyEnterprise("A Klingon hit the undefended hull.");
                return;
            }
            ship.shields = ship.shields - hit;
            printf("%d unit hit on Enterprise from sector %d,%d\n", hit, x + 1, y + 1);
            if (ship.shields <= 0) {
                ship.shields = 0;
                game.destroyEnterprise("The shields have failed.");
                return;
            }
            printf("  Shields down to %d units.\n", ship.shields);
            if (hit >= 20 && (rand() % 100 < 60 || hit > ship.shields / 5)) {
                int device = 1 + rand() % 8;
                int amount = 1 + rand() % 3;
                ship.damage(device, amount);
                printf("Damage Control reports: %s damaged.\n", deviceName(device));
            }
        }
    }
}
