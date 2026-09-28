// The Laboratory's layers and presets (see LabLayers.h). Every colour below is copied from the place
// in the SDK that draws the layer; the comment next to each names that place.
#include "LabLayers.h"

#include <algorithm>
#include <initializer_list>

using rf::DrawLayer;

namespace {

// An SDK colour (components 0..1, as rf::Probe draws with) as a QColor.
QColor sdk(float r, float g, float b) {
    auto channel = [](float v) { return int(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
    return QColor(channel(r), channel(g), channel(b));
}

// rf::heatColor at its two ends and its middle: blue (little), green, red (much) - core/Probe.cpp.
std::vector<QColor> heatScale() { return {sdk(0.1f, 0.1f, 1.0f), sdk(0.1f, 1.0f, 0.1f), sdk(1.0f, 0.1f, 0.1f)}; }

// 1. Contacts - rigid/RigidDebugDraw.cpp, drawContacts.
void addContacts(std::vector<LabLayer>& v) {
    const QString g = "Контакты";
    v.push_back({DrawLayer::ContactPoints, g, "Точки контакта",
                 "Где тела касаются друг друга: красный крестик на каждой точке, которую держит решатель. "
                 "Щёлкните по точке — откроется её карточка.",
                 {sdk(1.0f, 0.3f, 0.2f)}});
    v.push_back({DrawLayer::ContactNormals, g, "Нормали",
                 "Куда контакт толкает: оранжевая стрелка 10 см от тела A к телу B.", {sdk(1.0f, 0.6f, 0.2f)}});
    v.push_back({DrawLayer::ContactImpulses, g, "Силы",
                 "Сколько контакт давит: жёлтая стрелка, 1 м стрелки = 100 Н (импульс точки / шаг).", {sdk(1.0f, 1.0f, 0.3f)}});
    v.push_back({DrawLayer::PenetrationDepth, g, "Проникновение",
                 "Насколько тела вошли друг в друга: красный отрезок длиной в глубину проникновения.", {sdk(1.0f, 0.1f, 0.1f)}});
}

// 2. Bodies - rigid/RigidDebugDraw.cpp, drawBodies and drawIslands; the joints - drawJoints.
void addBodies(std::vector<LabLayer>& v) {
    const QString g = "Тела";
    v.push_back({DrawLayer::CentreOfMass, g, "Центр масс", "Белый крестик в центре масс каждого тела.", {sdk(1, 1, 1)}});
    v.push_back({DrawLayer::InertiaAxes, g, "Оси инерции",
                 "Главные оси тела: красная, зелёная, синяя. Длина — радиус инерции √(I/m): чем длиннее ось, тем "
                 "труднее раскрутить тело вокруг неё.",
                 {sdk(1, 0.2f, 0.2f), sdk(0.2f, 1, 0.2f), sdk(0.2f, 0.2f, 1)}});
    v.push_back({DrawLayer::Velocities, g, "Скорости",
                 "Голубая стрелка — скорость (куда тело сдвинется за 0,1 с), лиловая — угловая скорость (ось вращения).",
                 {sdk(0.2f, 1, 1), sdk(1, 0.3f, 1)}});
    v.push_back({DrawLayer::Islands, g, "Острова",
                 "Тела, связанные контактами, решаются вместе: одна рамка одного цвета на весь остров.",
                 {sdk(1, 0.3f, 0.2f), sdk(0.3f, 1, 0.4f), sdk(0.3f, 0.5f, 1)}});
    v.push_back({DrawLayer::Sleeping, g, "Сон",
                 "Уснувшие тела (решатель их не считает, пока их не толкнут): серая рамка, само тело темнее.", {sdk(0.35f, 0.35f, 0.35f)}});
    v.push_back({DrawLayer::JointFrames, g, "Суставы",
                 "Точки крепления сустава (зелёная на теле A, синяя на теле B), жёлтая связь между ними, серая ось.",
                 {sdk(0.2f, 1, 0.4f), sdk(0.2f, 0.6f, 1), sdk(1, 1, 0.3f)}});
}

// 3. The broad phase - rigid/RigidDebugDraw.cpp, drawBounds and drawTrees.
void addBroadPhase(std::vector<LabLayer>& v) {
    const QString g = "Широкая фаза";
    v.push_back({DrawLayer::BodyAabbs, g, "Коробки тел (AABB)",
                 "Голубая коробка вокруг каждого не спящего тела: только тела, чьи коробки пересеклись, проверяются точно.",
                 {sdk(0.3f, 0.8f, 1)}});
    v.push_back({DrawLayer::WorldTree, g, "Дерево мира",
                 "Дерево коробок, в котором ищутся пары: корень красный, листья синие (цвет — глубина в дереве).", heatScale(), true});
    v.push_back({DrawLayer::MeshBvh, g, "BVH неподвижного меша",
                 "Коробки дерева неподвижного меша (рельефа) до 8 уровней: верх красный, глубина синяя.", heatScale(), true});
}

// 4. GJK and EPA of the watched pair - rigid/WatchedPairDraw.cpp.
void addGjkEpa(std::vector<LabLayer>& v) {
    const QString g = "GJK · EPA";
    v.push_back({DrawLayer::GjkSimplex, g, "Симплексы GJK",
                 "Как GJK ищет ближайшие точки выбранной пары: симплексы от первого (синий) к последнему (красный), "
                 "белый крест — начало координат разности Минковского. Пару выбирает кнопка «Следить за парой».",
                 heatScale(), true});
    v.push_back({DrawLayer::EpaPolytope, g, "Многогранник EPA",
                 "Последний многогранник EPA для проникших тел: оранжевые рёбра, подпись — глубина.", {sdk(1, 0.6f, 0.1f)}});
    v.push_back({DrawLayer::WitnessPoints, g, "Точки-свидетели",
                 "Ближайшие точки пары (зелёная на A, синяя на B) и зазор или проникновение между ними.",
                 {sdk(0.2f, 1, 0.3f), sdk(0.2f, 0.5f, 1), sdk(1, 0.15f, 0.15f)}});
}

// 5. Particles and cloth - particles/ParticleDebugDraw.cpp.
void addParticles(std::vector<LabLayer>& v) {
    v.push_back({DrawLayer::ParticleNeighbours, "Частицы", "Соседи частицы",
                 "Частица под курсором (белая) и линии к её соседям; подпись — сколько их и какая плотность.",
                 {sdk(1, 1, 1), sdk(0.3f, 1, 0.6f)}});
    v.push_back({DrawLayer::DensityError, "Частицы", "Ошибка плотности",
                 "Жидкость должна держать плотность воды: синие частицы разрежены, зелёные точны, красные сжаты.", heatScale(), true});
    v.push_back({DrawLayer::SoftClusters, "Частицы", "Кластеры желе",
                 "Центры кластеров мягкого тела и линии к их частицам, свой цвет у каждого кластера.",
                 {sdk(0.3f, 0.5f, 1), sdk(0.3f, 1, 0.4f), sdk(1, 0.5f, 0.2f)}});
    v.push_back({DrawLayer::ClothTension, "Ткань", "Натяжение нитей",
                 "Каждая нить ткани цветом натяжения к прочности: синяя провисла, красная вот-вот порвётся.", heatScale(), true});
}

// 6. Gas and fields - gas/GasDebugDraw.cpp and scene/Snapshot.cpp (the field lines).
void addGasAndFields(std::vector<LabLayer>& v) {
    const QString g = "Газ", f = "Поля";
    v.push_back({DrawLayer::GasGrid, g, "Сетка", "Клетки сетки газа в плоскости среза (серо-голубые линии).", {sdk(0.45f, 0.5f, 0.6f)}});
    v.push_back({DrawLayer::GasVelocity, g, "Скорость газа", "Голубые стрелки скорости воздуха в плоскости среза.", {sdk(0.3f, 0.9f, 1)}});
    v.push_back({DrawLayer::PressureGradient, g, "Градиент давления",
                 "Оранжевые стрелки −∇p/ρ: куда и с каким ускорением давление толкает воздух.", {sdk(1, 0.55f, 0.2f)}});
    v.push_back({DrawLayer::Divergence, g, "Дивергенция",
                 "div u в каждой клетке: сколько воздуха ошибочно рождается или исчезает. Проекция давления "
                 "делает её почти нулевой; синий — мало, красный — много.",
                 heatScale(), true});
    v.push_back({DrawLayer::Vorticity, g, "Завихрённость", "Лиловые стрелки ротора скорости: ось и сила вихря.", {sdk(1, 0.5f, 1)}});
    v.push_back({DrawLayer::FieldLinesB, f, "Силовые линии B",
                 "Линии магнитного поля, цвет — сила поля |B|: синий слабое, красный сильное.", heatScale(), true});
    v.push_back({DrawLayer::CurrentDensity, f, "Ток J", "Жёлтые стрелки плотности тока J = rot B / μ₀ в плоскости среза.", {sdk(1, 0.8f, 0.2f)}});
    v.push_back({DrawLayer::Misc, "Прочее", "Прочие рисунки", "Всё, что решатели нарисовали без своего слоя.", {sdk(0.8f, 0.8f, 0.8f)}});
}

uint32_t bits(std::initializer_list<DrawLayer> layers) {
    uint32_t mask = 0;
    for (DrawLayer l : layers) mask |= rf::Probe::bit(l);
    return mask;
}

} // namespace

const std::vector<LabLayer>& labLayers() {
    static const std::vector<LabLayer> all = [] {
        std::vector<LabLayer> v;
        addContacts(v);
        addBodies(v);
        addBroadPhase(v);
        addGjkEpa(v);
        addParticles(v);
        addGasAndFields(v);
        return v;
    }();
    return all;
}

const std::vector<QString>& labGroups() {
    static const std::vector<QString> order = {"Контакты", "Тела",  "Широкая фаза", "GJK · EPA", "Частицы",
                                               "Ткань",    "Газ",   "Поля",         "Прочее"};
    return order;
}

const std::vector<LabPreset>& labPresets() {
    static const std::vector<LabPreset> presets = {
        {"Контакты", "Точки, нормали, силы и проникновение всех контактов",
         bits({DrawLayer::ContactPoints, DrawLayer::ContactNormals, DrawLayer::ContactImpulses, DrawLayer::PenetrationDepth})},
        {"Всё о телах", "Центры масс, оси инерции, скорости, острова, сон, суставы и коробки",
         bits({DrawLayer::CentreOfMass, DrawLayer::InertiaAxes, DrawLayer::Velocities, DrawLayer::Islands, DrawLayer::Sleeping,
               DrawLayer::JointFrames, DrawLayer::BodyAabbs})},
        {"Газ", "Сетка, скорость, градиент давления, дивергенция и вихри в плоскости среза",
         bits({DrawLayer::GasGrid, DrawLayer::GasVelocity, DrawLayer::PressureGradient, DrawLayer::Divergence, DrawLayer::Vorticity})},
        {"Ничего", "Выключить все слои: картинка снова чистая", 0u},
    };
    return presets;
}
