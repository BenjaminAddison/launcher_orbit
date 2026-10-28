// Desktop harness for the Orbit UI: scripted keys on a fixed 60 fps timeline,
// frame capture, and the design mockup's sample data.
#pragma once
#include "orbit_screens.h"
#include <functional>
#include <string>
#include <vector>

struct Ev {
    float t;
    uint8_t k;
    char ch;
};
struct Shot {
    std::string id;
    float t;
    std::vector<Ev> keys;
    std::function<void()> run; // builds the stack and runs it; capture interrupts it
};
extern std::vector<Shot> SHOTS;
void addShots();
void addGallery(); // README screens and films, no mockup reference
// key helpers mirroring the mockup's K()/downs()
std::vector<Ev> K(float t0, std::vector<std::pair<uint8_t, char>> keys, float gap = 0.12f);
std::vector<Ev> downs(float t0, int n, float gap = 0.1f);
std::vector<Ev> cat(std::vector<Ev> a, const std::vector<Ev> &b);
void runForever(); // frames until the capture interrupts
