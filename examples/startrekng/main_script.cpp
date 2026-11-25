#include "game.hpp"
#include "script.hpp"

int main() {
    Game game;
    game.console().setScript(kScriptText, kScriptLength);
    return game.run();
}
