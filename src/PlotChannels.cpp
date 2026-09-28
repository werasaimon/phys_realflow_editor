// The plots' catalogue of channels: name cards and colours (see PlotChannels.h).
#include "PlotChannels.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

// The validated categorical order, dark-surface steps (see the file head of PlotChannels.h).
const std::array<QColor, 8> kSlots = {QColor("#3987e5"), QColor("#d95926"), QColor("#199e70"), QColor("#c98500"),
                                      QColor("#d55181"), QColor("#008300"), QColor("#9085e9"), QColor("#e66767")};

struct LegacyCard {
    const char* id;
    const char* name;
    const char* unit;
};

// The scene's energy as the first chart names it: plain words for everyone. The SDK measures the
// total; its two parts the panel adds up from the SDK's channels (PlotPanel::append).
const LegacyCard kEnergy[] = {
    {"scene/kinetic energy", "Кинетическая энергия", "Дж"},
    {"scene/potential energy", "Потенциальная энергия", "Дж"},
    {"scene/mechanical energy", "Полная энергия", "Дж"},
};

// The engine's older physical channels, reported every frame whatever the viewer asks for.
const LegacyCard kLegacy[] = {
    {"rigid/kinetic energy J", "Твёрдые тела: энергия движения (датчик движка)", "Дж"},
    {"particles/density error %", "Жидкость: ошибка плотности (датчик движка)", "%"},
    {"particles/max speed", "Частицы: наибольшая скорость", "м/с"},
    {"gas/Cd", "Газ: коэффициент сопротивления Cd", ""},
    {"gas/Cl", "Газ: коэффициент подъёмной силы Cl", ""},
    {"gas/smoke dm3", "Газ: объём дыма", "дм³"},
    {"magnets/max force N", "Магниты: наибольшая сила", "Н"},
    {"mhd/max B", "Магнитное поле: наибольшее |B|", "Тл"},
};

// The engine's counters people ask for most, in words (the rest keep the engine's own words).
const LegacyCard kEngine[] = {
    {"frame/step ms", "Кадр: шаг физики", "мс"},
    {"rigid/contacts", "Твёрдые тела: точек контакта", ""},
    {"rigid/bodies awake", "Твёрдые тела: не спят", ""},
    {"memory/allocations per frame", "Память: выделений за кадр", ""},
};

// The parts of the engine an internal's id starts with ("rigid/solve ms" -> «Твёрдые тела: solve»).
const LegacyCard kParts[] = {{"rigid", "Твёрдые тела", ""}, {"particles", "Частицы", ""}, {"gas", "Газ", ""},
                             {"mhd", "Магнитное поле", ""},  {"scene", "Сцена", ""},      {"frame", "Кадр", ""},
                             {"memory", "Память", ""}};

// The unit an engine id ends with ("rigid/solve ms"), and the id without it.
struct UnitSuffix {
    const char* suffix;
    const char* unit;
};
const UnitSuffix kSuffixes[] = {{" ms", "мс"}, {" s", "с"}, {" J", "Дж"}, {" N", "Н"}, {" %", "%"}, {" dm3", "дм³"}, {" m", "м"}};

PlotGroup groupOf(rf::ChannelGroup g) {
    switch (g) {
    case rf::ChannelGroup::Scene: return PlotGroup::Scene;
    case rf::ChannelGroup::Object: return PlotGroup::Object;
    case rf::ChannelGroup::Group: return PlotGroup::Group;
    case rf::ChannelGroup::Field: return PlotGroup::Field;
    }
    return PlotGroup::Engine;
}

// The SDK's card. An object's, a group's or a probe's name says what, not whose («Масса»): its
// label - the id before the last '/' - goes in front, the name after it in small letters.
PlotChannel fromSdk(const rf::ChannelInfo& info) {
    PlotChannel c{info.id, QString::fromStdString(info.name), QString::fromStdString(info.unit), groupOf(info.group)};
    const size_t slash = info.id.rfind('/');
    if (info.group != rf::ChannelGroup::Scene && slash != std::string::npos && !c.name.isEmpty()) {
        QString what = c.name;
        if (what.size() > 1 && what[1].isLower()) what[0] = what[0].toLower(); // «Масса» -> «масса», «|v|» stays
        c.name = QString::fromStdString(info.id.substr(0, slash)) + " — " + what;
    }
    return c;
}

// An engine internal: its unit read off the end of the id, its part in words, the rest in the
// engine's own words ("rigid/solve ms" -> «Твёрдые тела: solve», мс).
PlotChannel engineCard(const std::string& id) {
    for (const LegacyCard& e : kEngine)
        if (id == e.id) return {id, QString::fromUtf8(e.name), QString::fromUtf8(e.unit), PlotGroup::Engine};
    std::string rest = id;
    QString unit;
    for (const UnitSuffix& s : kSuffixes) {
        const std::string suffix = s.suffix;
        if (rest.size() > suffix.size() && rest.compare(rest.size() - suffix.size(), suffix.size(), suffix) == 0) {
            unit = QString::fromUtf8(s.unit);
            rest.resize(rest.size() - suffix.size());
            break;
        }
    }
    const size_t slash = rest.find('/');
    for (const LegacyCard& part : kParts)
        if (slash != std::string::npos && rest.compare(0, slash, part.id) == 0)
            return {id, QString::fromUtf8(part.name) + ": " + QString::fromStdString(rest.substr(slash + 1)), unit, PlotGroup::Engine};
    return {id, QString::fromStdString(rest), unit, PlotGroup::Engine};
}

} // namespace

QString plotGroupTitle(PlotGroup group) {
    switch (group) {
    case PlotGroup::Scene: return "Сцена";
    case PlotGroup::Object: return "Объект";
    case PlotGroup::Group: return "Группа";
    case PlotGroup::Field: return "Поле";
    case PlotGroup::Engine: return "Движок (для разработчиков)";
    }
    return QString();
}

void PlotCatalog::learn(const std::vector<rf::Measurement>& measurements) {
    for (const rf::Measurement& m : measurements)
        if (!learned_.count(m.info.id)) learned_[m.info.id] = fromSdk(m.info);
}

// 1. the scene's energy in plain words; 2. what the SDK sent with a measurement; 3. the SDK's
// catalogue; 4. the older engine channels; 5. a solver's own reading (not an ASCII "part/quantity"
// id); 6. an engine internal.
PlotChannel PlotCatalog::card(const std::string& id) const {
    for (const LegacyCard& e : kEnergy)
        if (id == e.id) return {id, QString::fromUtf8(e.name), QString::fromUtf8(e.unit), PlotGroup::Scene};
    if (auto it = learned_.find(id); it != learned_.end()) return it->second;
    if (const rf::ChannelInfo* info = rf::describeChannel(id)) return fromSdk(*info);
    for (const LegacyCard& l : kLegacy)
        if (id == l.id) return {id, QString::fromUtf8(l.name), QString::fromUtf8(l.unit), PlotGroup::Scene};
    const QString text = QString::fromStdString(id);
    if (id.find('/') == std::string::npos) { // the scene's own plot: «Высота мяча, м»
        const int comma = text.lastIndexOf(", ");
        if (comma > 0) return {id, text.left(comma), text.mid(comma + 2), PlotGroup::Scene};
        return {id, text, QString(), PlotGroup::Scene};
    }
    return engineCard(id);
}

bool PlotCatalog::measuredBySdk(const std::string& id) const { return learned_.count(id) || rf::describeChannel(id); }

// A channel on screen keeps its slot. A new one takes the slot it had last time if that one is
// free, else the first free slot.
int PlotCatalog::slotOf(const std::string& id) {
    if (auto it = slot_.find(id); it != slot_.end()) return it->second;
    auto taken = [this](int slot) {
        for (const auto& [other, s] : slot_)
            if (s == slot) return true;
        return false;
    };
    int slot = 0;
    if (auto last = lastSlot_.find(id); last != lastSlot_.end() && !taken(last->second)) slot = last->second;
    else
        while (taken(slot)) ++slot;
    slot_[id] = lastSlot_[id] = slot;
    return slot;
}

void PlotCatalog::pin(const std::string& id) {
    slotOf(id);
    pinned_.push_back(id);
}

// The channels no longer on any chart give their slots back (a pinned one keeps its own).
void PlotCatalog::releaseAllBut(const std::vector<std::string>& onScreen) {
    for (auto it = slot_.begin(); it != slot_.end();) {
        const bool keep = std::find(onScreen.begin(), onScreen.end(), it->first) != onScreen.end() ||
                          std::find(pinned_.begin(), pinned_.end(), it->first) != pinned_.end();
        it = keep ? std::next(it) : slot_.erase(it);
    }
}

QString numberText(double v, int significant) {
    if (!std::isfinite(v)) return "—";
    if (v == 0) return "0";
    const double a = std::fabs(v);
    const int power = int(std::floor(std::log10(a)));
    if (a >= 1e-3 && a < 1e5) {
        QString s = QString::number(v, 'f', std::max(0, significant - 1 - power));
        if (s.contains('.')) {
            while (s.endsWith('0')) s.chop(1);
            if (s.endsWith('.')) s.chop(1);
        }
        return s.replace('.', ',');
    }
    static const QString superscript = QString::fromUtf8("⁰¹²³⁴⁵⁶⁷⁸⁹");
    QString exponent = power < 0 ? QString::fromUtf8("⁻") : QString();
    for (const QChar c : QString::number(std::abs(power))) exponent += superscript[c.digitValue()];
    return numberText(v / std::pow(10.0, power), std::max(1, significant - 1)) + QString::fromUtf8("·10") + exponent;
}

QColor PlotCatalog::color(const std::string& id) { return kSlots[size_t(slotOf(id)) % kSlots.size()]; }

// Solid for the first eight channels, dashed for the next eight, dotted after that.
Qt::PenStyle PlotCatalog::lineStyle(const std::string& id) {
    static const Qt::PenStyle styles[] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};
    return styles[(size_t(slotOf(id)) / kSlots.size()) % 3];
}
