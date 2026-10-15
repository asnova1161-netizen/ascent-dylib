// ===========================================================================
//  ascent-ios / main.mm
//  Dylib entry. Build a plain arm64 dylib and inject it into Standoff 2
//  (TrollFools / TrollStore). On load we start the scanner thread and, once
//  UIKit is up, the transparent overlay window.
// ===========================================================================
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>

#include "game.hpp"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

void ascentStartOverlay(void);

static void ascentLog(const char* msg) {
    const char* home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/Documents/ascent_log.txt", home);
    if (FILE* f = fopen(path, "a")) {
        fprintf(f, "[ascent] %s\n", msg);
        fclose(f);
    }
}

__attribute__((constructor))
static void ascent_init(void) {
    ascentLog("dylib loaded");

    // Give the app a moment to finish launching before touching UIKit/scanner.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        gameStartScanner();
        ascentStartOverlay();
        ascentLog("scanner + overlay started");
    });
}
