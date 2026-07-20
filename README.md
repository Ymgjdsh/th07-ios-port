# th07

A cross-platform port of 東方妖々夢　～ Perfect Cherry Blossom 1.00b by Team Shanghai Alice using SDL3 and OpenGL ES.

This is the reallyportable branch of the Touhou 7 decompilation. This is where changes go that I thought were too big for the standard portable branch, like migrating to SDL3, web support, mobile support, etc. 

Currently compared to the portable branch, this supports web builds (using Emscripten) and touch screen controls, uses SDL3 instead of SDL2, and supports running on Android. Please note that Android support is experimental and may have bugs.

This is a drop-in replacement for the original Touhou 7 binary that plays identically to the original, but is more portable to other platforms outside of Windows. There are a few bugs/incompatibilities though, namely:

* You cannot load into stages on big endian machines. This is because the way ecl files, stg files, etc. are loaded in the original game is not endian independent, resulting in it breaking on any system not on little endian.
* Text rendering looks off. To be clear it does "work" but the text looks too big.
* Some features that the original game had, like 16 bit color mode, midi output, etc. are outright unimplemented. This may or may not be "fixed" later, but the focus currently is to produce a playable game.

Work is currently being done to transition the game over to being more platform-independent.

## Building

### Dependencies

* cmake
* SDL3 (SDL3, SDL3_ttf, and SDL3_image)
* OpenGL ES 3.0+
* A compiler that supports C++17
* A little endian machine

#### Desktop

Run cmake on this repo, then build with whatever generator you chose.

You will also need to add a copy of `msgothic.ttc` into your game directory if you are not running this on Windows or otherwise don't have the "ＭＳ ゴシック" font installed.

#### Android

Clone the repo with submodules. Afterwards, create a directory named "assets" in the root of the repo, and move the files `th07.dat` and `thbgm.dat` from the original game, as well as a copy of `msgothic.ttc` for text rendering.

Before building the game, you'll want it to have the icon of the original game. The build will fail if you don't have an icon (since there isn't any bundled).

In order to do this, you'll need to extract the ico from the original `th07.exe`, and place it into the mipmap icon folders of the android project. You can use `icoextract` to do this.

Firstly, you have to install `icoextract`. You can do this using `pip`, as in `pip install icoextract`.

Afterwards, get an original copy of `th07.exe`. Then run `icoextract -i 105 th07.exe ic_launcher.ico`. Then, convert this ico to png using imagemagick or whatever. Move this png over to `android/app/src/main/res/mipmap-mdpi`.

Open the `android` folder in Android Studio, and build.

#### Emscripten

Clone the repo with submodules. Afterwards, create a directory named "assets" in the root of the repo, and move the files `th07.dat` and `thbgm.dat` from the original game, as well as a copy of `msgothic.ttc` for text rendering.

Then in the build directory, run `emcmake` and build as usual.

## Controls

Controls are identical to the original game for non-touch users.

For touch users, there are two sets of touch controls. Those used on the menu and the one used during gameplay. On the menu, swipe in any direction in order to move the select cursor around, tap for select, and tap with two fingers for back. During gameplay, move the player around with your finger (the player moves relative to your finger). To focus, hold down another finger while moving your character around. To bomb, tap the black bars on the side of the screen or the bottom left corner of the screen.

Note that you cannot save replays when using touch controls. It shows the "you cannot save a replay if you've used a continue," but this shows up regardless if you've used a continue or not if you've used touch controls at any point during gameplay.

## Todo

* Try to get the text rendering closer to the original
* Make the game endian independent

## Credits

* The earlier [decompilation for th06](https://github.com/GensokyoClub/th06), used as a source of shared types, function names, file names, source organization, basically everything. Because EoSD and PCB are so similar architecturally, the pre-existing th06 decompilation could be used as a direct reference for reverse engineering th07.

* The [decompilation for th08](https://github.com/GensokyoClub/th08) for the complete and actually readable LZSS implementation. Basically nothing changed from th07 to th08 at least in this regard, so it made it much simpler.

* EstexNT for porting the [var_order pragma](https://gist.github.com/EstexNT/e98a1384b906a3eedaaa3eeb7e58cd9d) to MSVC 7, which is used extensively throughout this project.
