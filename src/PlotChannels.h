#pragma once
// What the plots know about a channel besides its numbers: its group in the picker (Сцена /
// Объект / Группа / Поле / Движок), its name for people, its unit - and its colour, which follows
// the channel for the whole session, never its place in a list ("colour follows the entity, never
// its rank": switching another track on or off never repaints this one).
//
// Where a channel's name card comes from, first match wins:
//   0. the scene's energy as the first chart shows it: «Кинетическая», «Потенциальная», «Полная»;
//   1. the frame's measurements (scene/Channels.h) - the SDK measured it and sent its card along;
//   2. rf::describeChannel(id) - the SDK's catalogue of scene channels;
//   3. a short table of the engine's older physical channels ("rigid/kinetic energy J", ...);
//   4. otherwise it is an engine internal (a timer, a counter): group «Движок», the unit read off
//      the end of the id, the part in words and the rest in the engine's own words ("rigid/solve ms"
//      -> «Твёрдые тела: solve», мс); a few counters people ask for most have whole Russian names.
// A scene's own plots ("Высота мяча, м") are «Сцена», their unit read after the last comma.
//
// Colours: the eight dark-surface slots of the validated categorical palette (the dataviz skill's
// reference palette, checked with its validator on this chart surface #1c1e23: every adjacent pair
// CVD ΔE ≥ 8.4, normal-vision ΔE ≥ 19.3, every slot ≥ 3:1 against the surface), in this fixed
// order. A channel takes a slot when it is first shown and keeps it while it is shown; hidden, it
// gives the slot back - and gets the same one again when it returns, unless another channel took
// it meanwhile. So the tracks on screen use the first slots, and switching one track on or off
// never repaints the others. A ninth track at once takes slot 1 again with a dashed line, a
// seventeenth with a dotted one: composite encoding, never a generated ninth hue. Lines drawn on
// top of each other can bring any two slots together; the legend, the direct labels and the hover
// tooltip carry the names, so identity is never colour alone.
#include "scene/Channels.h"

#include <QColor>
#include <QString>

#include <map>
#include <string>
#include <vector>

// The picker's groups, in the order it shows them.
enum class PlotGroup { Scene, Object, Group, Field, Engine };
QString plotGroupTitle(PlotGroup group);

// A channel's name card.
struct PlotChannel {
    std::string id; // the Probe key and the CSV column ("rigid/kinetic energy")
    QString name;   // for people («Твёрдые тела: энергия движения»)
    QString unit;   // «Дж», «м/с», «мс», "" for a pure number
    PlotGroup group = PlotGroup::Engine;
};

class PlotCatalog {
public:
    // The cards the SDK sent with this frame's measurements (objects, groups and probes are only
    // known this way: their ids carry the viewer's labels, "Куб/speed"). An object's card is named
    // after the object: «Куб — скорость центра масс».
    void learn(const std::vector<rf::Measurement>& measurements);
    // The card of any channel (see the file head for where it comes from).
    PlotChannel card(const std::string& id) const;
    // A channel the SDK measures (scene/Channels.h): its numbers come with the frame's measurements.
    // The Probe keeps the last value of every channel it was ever told, so the same id in the Probe
    // may be a number from an earlier scene - the plots take such a channel from the measurements only.
    bool measuredBySdk(const std::string& id) const;

    // The channel's colour and line style: a slot is handed out on the first question and kept
    // while the channel is on screen (releaseAllBut lets the others go).
    QColor color(const std::string& id);
    Qt::PenStyle lineStyle(const std::string& id);
    bool hasColor(const std::string& id) const { return slot_.count(id) > 0; }
    void pin(const std::string& id); // this channel's slot for good (the scene's energy: always the same three colours)
    void releaseAllBut(const std::vector<std::string>& onScreen);

private:
    int slotOf(const std::string& id);

    std::map<std::string, PlotChannel> learned_;
    std::map<std::string, int> slot_;     // the channels on screen and their slots
    std::map<std::string, int> lastSlot_; // the slot each channel had last (it gets it back if free)
    std::vector<std::string> pinned_;
};

// A number for people: a decimal comma and `significant` digits (no trailing zeros); the very large
// and the very small as «1,5·10⁻⁶» - a power in superscript, never the programmer's «1.5e-06».
QString numberText(double v, int significant = 4);
