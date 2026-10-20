// ps1.h - SAMPSP4's PlayStation 1 player (PCSX-ReARMed through a small libretro front end).
#pragma once

// Is this file a PS1 game (disc image, CD .chd or PSone Classic EBOOT)?
int ps1_is_game(const char *path);

// Plays the game until the player picks "Salir" in the menu. Needs the screen and controller
// (plat_init done). Returns 0 when the player quit, or -1 if the game couldn't start.
int ps1_run(const char *path);

// Built-in engine hook (PS4Main.cpp): vault_ps1_main() opens the screen, runs the game and
// releases everything again before returning.
int vault_ps1_main(const char *path);
