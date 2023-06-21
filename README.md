# Roguely

![Roguely Logo](assets/roguely-logo.png)

Roguely is a very simple Roguelike in SDL/C++/Lua.

The game is using very simple cellular automata to generate maps. Each time you
run the game you'll get a new map. Enemies spawn, move around randomly and you
can attack them to increase your score. You can pick up health gems and coins.
Dead enemies spawn a treasure chest with a chance to increase health and score.
Look for the golden candle and if you collect it you will win the game.

There is no enemy AI yet. Enemies won't aggro on you. You can avoid enemies
easily. This is an area that I plan to invest some time on in the future.

The screenshot below shows off game sprites that are in the current iteration
of the sprite sheet. Not all sprites in this screenshot are currently being used
in the game. There is still a lot of work still left to do.

![Roguely Logo](screenshots/sprite-sandbox.png)

## Next Steps

This concluded during rewrite.

- [x] REWRITE!!!
- [x] Switch from manual dependency configuration to `vcpkg`
- [x] Refactor current code so we can:
  - [x] Integrate Lua
  - [x] Finish ECS system
  - [x] Move all in game objects (including player) to entities
  - [x] Remove hard coded entity references that we have now
  - [x] Optimize rendering pipeline
  - [ ] Expand the title screen to add a screen to document how the game is played and who the enemies are as well as what the objectives are.

## Tileset

I've included the Photoshop file that was used to create the tileset. My
workflow is to develop tiles in 8x8 and then upscale them to 32x32 and then
export the tileset as a PNG which is used in the game.

The tileset is sort of all over the place. It's not well organized and has
ventured into some animation tiles. We aren't doing any animation yet and a lot
of these tiles are not currently being used. The main focus so far has been to
get a basic engine up, provide lua support and get something working and for
that I am very happy and recognize that there is still a long way to go before
a proper game can be made.

## Game Objectives (so far)

Not a lot to do besides run around and kill enemies and increase score. If you
run across the golden candle you win. It's highlighted in yellow on the mini
map. If the level generation spawned you in a blocked area just hit the space
bar to warp to another location.

## Screenshots

Title Screen:

![Title Screen](screenshots/title-screen.png)

Combat Text:

![Combat Text](screenshots/combat-text.png)

Current Gameplay:

![Current](screenshots/current.png)

Older gameplay showing how it evolved:

![Old](screenshots/seventh.png)

![Old](screenshots/sixth.png)

![Old](screenshots/fifth.png)

![Old](screenshots/fourth.png)

![Old](screenshots/third.png)

![Old](screenshots/second.png)

My first screenshot when I was able to render sprites:

![Old](screenshots/first.png)

## Videos

[![Game Play (Part 2)](https://img.youtube.com/vi/Bs1GXWLNYok/0.jpg)](https://www.youtube.com/watch?v=Bs1GXWLNYok)

[![Game Play (Part 1)](https://img.youtube.com/vi/IOBuFlfgCSE/0.jpg)](https://www.youtube.com/watch?v=IOBuFlfgCSE)

## Building

This code uses `vcpkg` and for dependencies management and `cmake` to build. This expects a sand build environment to be installed. I've installed `Visual Studio 2022` and use `Visual Studio Code` to develop the game.

You can get `vckpg` here: [https://github.com/Microsoft/vcpkg](https://github.com/Microsoft/vcpkg)

You can get `Visual Studio` here:
[https://visualstudio.microsoft.com/downloads/](https://visualstudio.microsoft.com/downloads/)

You can get `Visual Studio Code` here:
[https://code.visualstudio.com/](https://code.visualstudio.com/)

Roguely has the following dependencies which are managed by `vcpkg`:

- SDL2
- SDL_image
- SDL_mixer (with mpg123 support)
- SDL_ttf
- Lua
- Sol2
- Boost (using boost/uuid, boost/matrix)
- magic_enum
- fmt

## How to use the game engine

This engine provides a simple Entity Component System and exposes that to Lua.
Since this engine uses SDL2 it also exposes several SDL2 functions to Lua to
make creating grid turn based games easier. The engines primary design is to
make creating grid turn based games easier.

The engine expects that the `roguely.lua` file will have a global table called
`Game` which at the least defines the following properties:

```lua
Game = {
  window_title = "Roguely (2023 Edition) - A simple Roguelike in C++/Lua/SDL2",
  window_icon_path = "assets/icon.png",
  window_width = 1280,
  window_height = 640,
  map_width = 100,
  map_height = 100,
  spritesheet_name = "roguely-x",
  spritesheet_path = "assets/roguely-x.png",
  spritesheet_sprite_width = 8,
  spritesheet_sprite_height = 8,
  spritesheet_sprite_scale_factor = 4,
  font_path = "assets/NESCyrillic.ttf",
  soundtrack_path = "assets/ExitExitProper.mp3",
  logo_image_path = "assets/roguely-logo.png",
  start_game_image_path = "assets/press-space-bar-to-play.png",
  credit_image_path = "assets/credits.png",
  sounds = {
      coin = "assets/sounds/coin.wav",
      bump = "assets/sounds/bump.wav",
      combat = "assets/sounds/combat.wav",
      death = "assets/sounds/death.wav",
      pickup = "assets/sounds/pickup.wav",
      warp = "assets/sounds/warp.wav",
      walk = "assets/sounds/walk.wav"
  },
  entities = {
    player = {
      components = {
          sprite_component = {
              name = "player",
              spritesheet_name = "roguely-x",
              sprite_id = 15,
              blink = false,
              render = function(self, game, player, dx, dy, scale_factor)
                  -- handle sprite player render here
              end
          },
          position_component = { x = 0, y = 0 },
          stats_component = {
              max_health = 50,
              health = 50,
              health_recovery = 10,
              attack = 5,
              crit_chance = 2,
              crit_multiplier = 2,
              score = 0,
              kills = 0,
              level = 0,
              experience = 0,
          },
          healthbar_component = {
            -- handle render of health bar
          },
          tick_component = {
            -- handle what occurs during a game tick (every one second)
          }
      }
    }
  }
}
```

When games are started up the engine first looks to make sure required
properties are in the `Game` table and then it calls `_init()`. This function
can be used to setup your game. You can spawn entities, set up your maps, etc...

It's expected that during the intialization you will add various system
functions that will be called in order to handle game entities.

For instance:

```lua
add_system("render_system", render_system)
add_system("keyboard_input_system", keyboard_input_system)
add_system("combat_system", combat_system)
add_system("leveling_system", leveling_system)
add_system("loot system", loot_system)
add_system("tick_system", tick_system)
add_system("mob_movement_system", mob_movement_system)
```

Systems are just functions that look like:

```lua
function keyboard_input_system(key, player, entities, entities_in_viewport)
end
```

## License

MIT

## Credits for Audio

Music track `Exit Exit Proper - Pipe Choir` from:

- [http://www.pipechoir.com/](http://www.pipechoir.com/)
- [https://soundcloud.com/pipe-choir-three](https://soundcloud.com/pipe-choir-three)
- [https://freemusicarchive.org/music/P_C_III](https://freemusicarchive.org/music/P_C_III)

Creative Commons License: <http://www.pipechoir.com/music-licenses.html>

The sounds in the `assets/sounds` folder came from [https://opengameart.org/](https://opengameart.org/)

## Author(s)

Frank Hale &lt;<frankhaledevelops@gmail.com>&gt;

## Date

21 June 2023
