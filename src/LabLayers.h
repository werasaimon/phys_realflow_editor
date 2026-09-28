#pragma once
// The Laboratory's catalogue of what the engine can draw for research (rf::DrawLayer in the SDK's
// core/Probe.h): each layer's group, its name in plain words, how to read it, the colours it is drawn
// with, and the quick presets («Контакты», «Всё о телах», «Газ», «Ничего»).
//
// The colours next to a layer are the ones the SDK really draws it with (rigid/RigidDebugDraw.cpp,
// rigid/WatchedPairDraw.cpp, particles/ParticleDebugDraw.cpp, gas/GasDebugDraw.cpp, scene/Snapshot.cpp):
// a legend that lied would be worse than none. A layer coloured by a quantity - the depth of a tree,
// the tension of a thread, the strength of a field - shows a small gradient from blue (little) to red
// (much), and its tooltip says so in words, so the colour is never the only way to read it.
#include "core/Probe.h"

#include <QColor>
#include <QString>

#include <cstdint>
#include <vector>

// One checkbox of the Laboratory.
struct LabLayer {
    rf::DrawLayer layer;
    QString group;              // «Контакты», «Тела», ... (labGroups() gives their order)
    QString name;               // what the checkbox says
    QString tip;                // what the layer shows, and how to read its colours
    std::vector<QColor> colors; // the swatch: one little square per colour, or one gradient
    bool gradient = false;      // the colours are the ends of a scale (blue = little, red = much)
};

// Every layer the SDK can draw, grouped, in the order the panel shows them.
const std::vector<LabLayer>& labLayers();
// The groups in the panel's order.
const std::vector<QString>& labGroups();

// A quick preset: a set of layers switched on together with one click (everything else off).
struct LabPreset {
    QString name;
    QString tip;
    uint32_t mask = 0; // Probe::bit of every layer it turns on
};
const std::vector<LabPreset>& labPresets();
