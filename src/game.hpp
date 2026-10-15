// ===========================================================================
//  ascent-ios / game.hpp
//  Player pattern scanner + camera pick. Runs on its own thread and publishes
//  an immutable snapshot the overlay reads on the main thread.
//
//  Player pattern (from wonderful/cheat.cpp:17) matches the `scale` field of a
//  TMatrix block; the block base is (match_addr - 0x20):
//      00 00 80 3F  00 00 80 3F  00 00 80 3F   -> scale.xyz = 1,1,1
//      80 96 18 4B                              -> scale.w   = 1e7
//      00 00 00 00  CD CC
//
//  Camera: mtx_checker marker, view-projection at (marker - 0x178 + 0x100).
//  Multiple candidates are scored by projecting the found players and picking
//  the one that lands the most of them on-screen (wonderful PickCameraByPlayers).
// ===========================================================================
#pragma once

#include "math.hpp"
#include <memory>
#include <vector>

struct Player {
    Vec3 pos;          // world feet position
    float distance;    // meters from camera
    bool  valid;
};

struct Snapshot {
    std::vector<Player> players;
    Mat4 camera;
    bool hasCamera = false;
    Vec3 cameraPos;
    int  scannedAt = 0;  // seconds since app start of last full scan
};

// Latest snapshot (immutable, swapped under the hood).
std::shared_ptr<const Snapshot> gameSnapshot();

// Start/stop the background scanner.
void gameStartScanner();
void gameStopScanner();

// Log line accessible from the overlay.
const char* gameStatusLine();
