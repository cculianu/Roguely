#include "engine.h"

#include <SDL2/SDL.h>

#include <cstdlib>
#include <iostream>

int main(int argc, char * argv[]) {
    try {
        roguely::engine::Engine engine;
        engine.game_loop();
        return EXIT_SUCCESS;
    } catch (const std::exception & e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Caught Exception", e.what(), nullptr);
        return EXIT_FAILURE;
    }
}
