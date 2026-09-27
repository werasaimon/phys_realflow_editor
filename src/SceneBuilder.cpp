// The scene builder panel (see SceneBuilder.h): buttons that add shapes, the list of what is in the
// scene, and an inspector that says what each thing is. The panel owns an rf::SceneGraph - the
// scene as data - and after every change sends a copy to the simulation thread as a GraphScene.
#include "SceneBuilder.h"

#include <QPointer>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>

#include <memory>

using namespace rf;

namespace {

const char* kShapeNames[] = {"Куб", "Сфера", "Цилиндр", "Конус", "Плоскость"};

// Colours for new shapes, in turn, so neighbours differ at a glance.
const Vector3 kPalette[] = {{0.90f, 0.45f, 0.25f}, {0.30f, 0.60f, 0.95f}, {0.45f, 0.80f, 0.35f}, {0.95f, 0.80f, 0.30f},
                            {0.75f, 0.45f, 0.90f}, {0.35f, 0.85f, 0.85f}, {0.95f, 0.40f, 0.55f}, {0.70f, 0.70f, 0.70f}};

// What the entity is, in words, for the list: "Куб 1 — твёрдое, магнит".
QString rolesText(const Entity& e) {
    QStringList roles;
    if (e.rigid.enabled) roles << (e.rigid.fixed ? "неподвижное" : "твёрдое");
    if (e.soft.enabled) roles << "мягкое";
    if (e.liquid.enabled) roles << "жидкость";
    if (e.magnet.enabled) roles << "магнит";
    if (e.heat.enabled) roles << "тепло";
    const QString name = QString::fromStdString(e.name);
    return roles.isEmpty() ? name + "  (только форма)" : name + " — " + roles.join(", ");
}

void setVec(const SceneBuilder::Vec3Edit& e, const Vector3& v) {
    e.x->setValue(v.x);
    e.y->setValue(v.y);
    e.z->setValue(v.z);
}

Vector3 getVec(const SceneBuilder::Vec3Edit& e) { return Vector3(float(e.x->value()), float(e.y->value()), float(e.z->value())); }

} // namespace

SceneBuilder::SceneBuilder(SimController* ctrl, QWidget* parent) : QWidget(parent), ctrl_(ctrl) {
    reloadTimer_ = new QTimer(this);
    reloadTimer_->setSingleShot(true);
    reloadTimer_->setInterval(150);
    connect(reloadTimer_, &QTimer::timeout, this, &SceneBuilder::reload);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(6);
    buildToolbar(col);
    buildOutliner(col);
    inspector_ = new QWidget;
    auto* inspectorCol = new QVBoxLayout(inspector_);
    inspectorCol->setContentsMargins(0, 0, 0, 0);
    buildEntityGroup(inspectorCol);
    buildRigidGroup(inspectorCol);
    buildSoftLiquidGroups(inspectorCol);
    buildMagnetHeatGroups(inspectorCol);
    col->addWidget(inspector_);
    buildWorldGroup(col);
    col->addStretch(1);
    fillWorld();
    fillInspector();
}

// ---------------------------------------------------------------------------
// Building the panel
// ---------------------------------------------------------------------------
void SceneBuilder::buildToolbar(QVBoxLayout* col) {
    auto* hint = new QLabel("Добавить в сцену:");
    col->addWidget(hint);
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    const ShapeKind kinds[] = {ShapeKind::Box, ShapeKind::Sphere, ShapeKind::Cylinder, ShapeKind::Cone, ShapeKind::Plane};
    for (ShapeKind k : kinds) {
        auto* b = new QPushButton(kShapeNames[int(k)]);
        b->setToolTip(k == ShapeKind::Plane ? "Неподвижная плита 3 × 3 м: пол, стол, наклонная плоскость"
                                            : "Новое твёрдое тело 20 см над полом");
        connect(b, &QPushButton::clicked, this, [this, k] { addEntity(k); });
        row->addWidget(b);
    }
    col->addLayout(row);
}

void SceneBuilder::buildOutliner(QVBoxLayout* col) {
    outliner_ = new QListWidget;
    outliner_->setMinimumHeight(120);
    outliner_->setToolTip("Всё, что есть в сцене. Выберите строку — ниже появятся её свойства. Delete — удалить.");
    connect(outliner_, &QListWidget::currentRowChanged, this, &SceneBuilder::onSelectionChanged);
    auto* del = new QShortcut(QKeySequence::Delete, outliner_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &SceneBuilder::removeSelected);
    col->addWidget(outliner_);

    auto* row = new QHBoxLayout;
    auto* dup = new QPushButton("Дублировать");
    auto* rem = new QPushButton("Удалить");
    connect(dup, &QPushButton::clicked, this, &SceneBuilder::duplicateSelected);
    connect(rem, &QPushButton::clicked, this, &SceneBuilder::removeSelected);
    row->addWidget(dup);
    row->addWidget(rem);
    col->addLayout(row);
}

QDoubleSpinBox* SceneBuilder::spin(double min, double max, double step, int decimals) {
    auto* s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setKeyboardTracking(false);
    connect(s, &QDoubleSpinBox::valueChanged, this, &SceneBuilder::onEdited);
    return s;
}

QWidget* SceneBuilder::vec3Row(Vec3Edit& e, double min, double max, double step, int decimals) {
    auto* w = new QWidget;
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    e.x = spin(min, max, step, decimals);
    e.y = spin(min, max, step, decimals);
    e.z = spin(min, max, step, decimals);
    row->addWidget(e.x);
    row->addWidget(e.y);
    row->addWidget(e.z);
    return w;
}

// A role is a checkable group: ticking it says "you are this", its fields say how.
QGroupBox* SceneBuilder::roleGroup(QVBoxLayout* col, const QString& title, QFormLayout*& form) {
    auto* box = new QGroupBox(title);
    box->setCheckable(true);
    box->setChecked(false);
    form = new QFormLayout(box);
    form->setContentsMargins(6, 4, 6, 4);
    connect(box, &QGroupBox::toggled, this, [this, box](bool on) { onRoleToggled(box, on); });
    col->addWidget(box);
    return box;
}

void SceneBuilder::buildEntityGroup(QVBoxLayout* col) {
    auto* box = new QGroupBox("Форма и место");
    auto* f = new QFormLayout(box);
    f->setContentsMargins(6, 4, 6, 4);
    name_ = new QLineEdit;
    connect(name_, &QLineEdit::textEdited, this, &SceneBuilder::onEdited);
    f->addRow("Имя", name_);
    shape_ = new QComboBox;
    for (const char* n : kShapeNames) shape_->addItem(n);
    connect(shape_, &QComboBox::currentIndexChanged, this, &SceneBuilder::onEdited);
    f->addRow("Форма", shape_);
    f->addRow("Размер, м", vec3Row(size_, 0.005, 20, 0.05, 3));
    f->addRow("Положение, м", vec3Row(position_, -50, 50, 0.05, 3));
    f->addRow("Поворот, °", vec3Row(rotation_, -360, 360, 5, 1));
    colorButton_ = new QPushButton("Цвет…");
    connect(colorButton_, &QPushButton::clicked, this, &SceneBuilder::pickColor);
    f->addRow("Цвет", colorButton_);
    col->addWidget(box);
}

void SceneBuilder::buildRigidGroup(QVBoxLayout* col) {
    QFormLayout* f = nullptr;
    rigidBox_ = roleGroup(col, "Твёрдое тело", f);
    rigidBox_->setToolTip("Не гнётся; сталкивается, катится, скользит (RigidWorld)");
    rigidDensity_ = spin(1, 30000, 50, 0);
    friction_ = spin(0, 2, 0.05, 2);
    restitution_ = spin(0, 1, 0.05, 2);
    fixed_ = new QCheckBox("неподвижное (стена, стол, пол)");
    connect(fixed_, &QCheckBox::toggled, this, &SceneBuilder::onEdited);
    f->addRow("Плотность, кг/м³", rigidDensity_);
    f->addRow("Трение", friction_);
    f->addRow("Упругость удара", restitution_);
    f->addRow(fixed_);
    f->addRow("Скорость, м/с", vec3Row(velocity_, -100, 100, 0.5, 2));
    f->addRow("Вращение, рад/с", vec3Row(spinRate_, -200, 200, 0.5, 2));
}

void SceneBuilder::buildSoftLiquidGroups(QVBoxLayout* col) {
    QFormLayout* f = nullptr;
    softBox_ = roleGroup(col, "Мягкое тело", f);
    softBox_->setToolTip("Гнётся и пружинит: частицы, держащие форму (shape matching)");
    softDensity_ = spin(1, 5000, 10, 0);
    stiffness_ = spin(0.01, 1, 0.05, 2);
    f->addRow("Плотность, кг/м³", softDensity_);
    f->addRow("Жёсткость", stiffness_);

    liquidBox_ = roleGroup(col, "Жидкость", f);
    liquidBox_->setToolTip("Объём формы заполняется водой (частицы PBF)");
    f->addRow(new QLabel("Форма заполняется водой"));
}

void SceneBuilder::buildMagnetHeatGroups(QVBoxLayout* col) {
    QFormLayout* f = nullptr;
    magnetBox_ = roleGroup(col, "Магнит", f);
    magnetBox_->setToolTip("Магнитный диполь: магниты притягиваются и поворачиваются друг к другу; "
                           "в плазме толкает газ");
    f->addRow("Момент, А·м²", vec3Row(moment_, -1000, 1000, 0.1, 2));

    heatBox_ = roleGroup(col, "Источник тепла", f);
    heatBox_->setToolTip("Горячее пятно в газе: тёплый воздух поднимается и несёт дым (нужен газ)");
    temperature_ = spin(0, 3000, 10, 0);
    smoke_ = spin(0, 10, 0.1, 2);
    f->addRow("Перегрев, K", temperature_);
    f->addRow("Дым", smoke_);
}

void SceneBuilder::buildWorldGroup(QVBoxLayout* col) {
    auto* box = new QGroupBox("Мир");
    auto* f = new QFormLayout(box);
    f->setContentsMargins(6, 4, 6, 4);
    f->addRow("Гравитация, м/с²", vec3Row(gravity_, -50, 50, 0.5, 2));
    f->addRow("Размер коробки, м", vec3Row(worldSize_, 0.5, 50, 0.5, 2));
    gas_ = new QCheckBox("Газ (воздух на сетке: дым, тепло, сопротивление)");
    plasma_ = new QCheckBox("Плазма (МГД): магниты толкают газ");
    connect(gas_, &QCheckBox::toggled, this, &SceneBuilder::onEdited);
    connect(plasma_, &QCheckBox::toggled, this, &SceneBuilder::onEdited);
    f->addRow(gas_);
    f->addRow(plasma_);
    col->addWidget(box);
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
void SceneBuilder::addEntity(ShapeKind shape) {
    Entity e;
    e.shape = shape;
    int same = 0;
    for (const Entity& o : graph_.entities) same += o.shape == shape;
    e.name = QString("%1 %2").arg(kShapeNames[int(shape)]).arg(same + 1).toStdString();
    const int n = int(graph_.entities.size());
    e.color = kPalette[n % 8];
    e.rigid.enabled = true;
    if (shape == ShapeKind::Plane) {
        e.size = Vector3(3.0f, 0.02f, 3.0f);
        e.position = Vector3(0.0f, 0.01f, 0.0f);
        e.color = Vector3(0.6f, 0.62f, 0.66f);
        e.rigid.fixed = true;
    } else {
        // New shapes on a 5 x 5 grid around the centre, a layer higher every 25: they do not overlap.
        e.position = Vector3(0.35f * float(n % 5 - 2), 0.4f + 0.3f * float(n / 25), 0.35f * float((n / 5) % 5 - 2));
    }
    graph_.entities.push_back(e);
    refreshOutliner();
    outliner_->setCurrentRow(n);
    scheduleReload();
}

void SceneBuilder::removeSelected() {
    const int i = selected();
    if (i < 0) return;
    graph_.entities.erase(graph_.entities.begin() + i);
    refreshOutliner();
    outliner_->setCurrentRow(std::min(i, int(graph_.entities.size()) - 1));
    scheduleReload();
}

void SceneBuilder::duplicateSelected() {
    const int i = selected();
    if (i < 0) return;
    Entity e = graph_.entities[size_t(i)];
    e.name += " копия";
    e.position.y += e.size.y + 0.05f; // on top of the original
    graph_.entities.push_back(e);
    refreshOutliner();
    outliner_->setCurrentRow(int(graph_.entities.size()) - 1);
    scheduleReload();
}

int SceneBuilder::selected() const {
    const int i = outliner_ ? outliner_->currentRow() : -1;
    return i >= 0 && i < int(graph_.entities.size()) ? i : -1;
}

void SceneBuilder::onSelectionChanged() { fillInspector(); }

void SceneBuilder::onEdited() {
    if (filling_) return;
    readWorld();
    readInspector();
    const int i = selected();
    if (i >= 0) outliner_->item(i)->setText(rolesText(graph_.entities[size_t(i)]));
    scheduleReload();
}

// Rigid, soft and liquid are what the shape is made of: one at a time. Magnet and heat add to it.
void SceneBuilder::onRoleToggled(QGroupBox* box, bool on) {
    if (filling_) return;
    if (on && (box == rigidBox_ || box == softBox_ || box == liquidBox_)) {
        filling_ = true;
        for (QGroupBox* other : {rigidBox_, softBox_, liquidBox_})
            if (other != box) other->setChecked(false);
        filling_ = false;
    }
    onEdited();
}

void SceneBuilder::pickColor() {
    const int i = selected();
    if (i < 0) return;
    Vector3& c = graph_.entities[size_t(i)].color;
    const QColor now = QColor::fromRgbF(c.x, c.y, c.z);
    const QColor picked = QColorDialog::getColor(now, this, "Цвет");
    if (!picked.isValid()) return;
    c = Vector3(float(picked.redF()), float(picked.greenF()), float(picked.blueF()));
    colorButton_->setStyleSheet(QString("background-color: %1").arg(picked.name()));
    scheduleReload();
}

// ---------------------------------------------------------------------------
// Graph <-> widgets
// ---------------------------------------------------------------------------
void SceneBuilder::fillInspector() {
    const int i = selected();
    inspector_->setEnabled(i >= 0);
    if (i < 0) return;
    const Entity& e = graph_.entities[size_t(i)];
    filling_ = true;
    name_->setText(QString::fromStdString(e.name));
    shape_->setCurrentIndex(int(e.shape));
    setVec(size_, e.size);
    setVec(position_, e.position);
    setVec(rotation_, e.rotationDeg);
    colorButton_->setStyleSheet(QString("background-color: %1").arg(QColor::fromRgbF(e.color.x, e.color.y, e.color.z).name()));
    rigidBox_->setChecked(e.rigid.enabled);
    rigidDensity_->setValue(e.rigid.density);
    friction_->setValue(e.rigid.friction);
    restitution_->setValue(e.rigid.restitution);
    fixed_->setChecked(e.rigid.fixed);
    setVec(velocity_, e.rigid.velocity);
    setVec(spinRate_, e.rigid.angularVelocity);
    softBox_->setChecked(e.soft.enabled);
    softDensity_->setValue(e.soft.density);
    stiffness_->setValue(e.soft.stiffness);
    liquidBox_->setChecked(e.liquid.enabled);
    magnetBox_->setChecked(e.magnet.enabled);
    setVec(moment_, e.magnet.moment);
    heatBox_->setChecked(e.heat.enabled);
    temperature_->setValue(e.heat.temperature);
    smoke_->setValue(e.heat.smoke);
    filling_ = false;
}

void SceneBuilder::readInspector() {
    const int i = selected();
    if (i < 0) return;
    Entity& e = graph_.entities[size_t(i)];
    e.name = name_->text().toStdString();
    e.shape = ShapeKind(shape_->currentIndex());
    e.size = getVec(size_);
    e.position = getVec(position_);
    e.rotationDeg = getVec(rotation_);
    e.rigid.enabled = rigidBox_->isChecked();
    e.rigid.density = float(rigidDensity_->value());
    e.rigid.friction = float(friction_->value());
    e.rigid.restitution = float(restitution_->value());
    e.rigid.fixed = fixed_->isChecked();
    e.rigid.velocity = getVec(velocity_);
    e.rigid.angularVelocity = getVec(spinRate_);
    e.soft.enabled = softBox_->isChecked();
    e.soft.density = float(softDensity_->value());
    e.soft.stiffness = float(stiffness_->value());
    e.liquid.enabled = liquidBox_->isChecked();
    e.magnet.enabled = magnetBox_->isChecked();
    e.magnet.moment = getVec(moment_);
    e.heat.enabled = heatBox_->isChecked();
    e.heat.temperature = float(temperature_->value());
    e.heat.smoke = float(smoke_->value());
}

void SceneBuilder::fillWorld() {
    filling_ = true;
    setVec(gravity_, graph_.world.gravity);
    setVec(worldSize_, graph_.world.size);
    gas_->setChecked(graph_.world.gas);
    plasma_->setChecked(graph_.world.magneticGas);
    filling_ = false;
}

void SceneBuilder::readWorld() {
    graph_.world.gravity = getVec(gravity_);
    graph_.world.size = getVec(worldSize_);
    graph_.world.gas = gas_->isChecked();
    graph_.world.magneticGas = plasma_->isChecked();
}

void SceneBuilder::refreshOutliner() {
    const bool was = outliner_->blockSignals(true);
    outliner_->clear();
    for (const Entity& e : graph_.entities) outliner_->addItem(rolesText(e));
    outliner_->blockSignals(was);
    fillInspector();
}

// ---------------------------------------------------------------------------
// Scene, files, picking
// ---------------------------------------------------------------------------
void SceneBuilder::scheduleReload() { reloadTimer_->start(); }

void SceneBuilder::reload() {
    const SceneGraph g = graph_;
    const std::string name = fileName_.isEmpty() ? std::string("Конструктор") : ("Конструктор: " + fileName_.toStdString());
    ctrl_->post([g, name](Simulation& s) {
        auto scene = std::make_unique<GraphScene>(g);
        scene->name = name;
        s.load(std::move(scene));
    });
}

void SceneBuilder::newScene() {
    graph_ = SceneGraph();
    fileName_.clear();
    fillWorld();
    refreshOutliner();
    reload();
}

bool SceneBuilder::openFile(const QString& path, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = f.errorString();
        return false;
    }
    SceneGraph g;
    std::string err;
    if (!g.load(QString::fromUtf8(f.readAll()).toStdString(), err)) {
        error = QString::fromStdString(err);
        return false;
    }
    graph_ = std::move(g);
    fileName_ = QFileInfo(path).fileName();
    fillWorld();
    refreshOutliner();
    if (!graph_.entities.empty()) outliner_->setCurrentRow(0);
    reload();
    return true;
}

bool SceneBuilder::saveFile(const QString& path, QString& error) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = f.errorString();
        return false;
    }
    f.write(QString::fromStdString(graph_.save()).toUtf8());
    return true;
}

// The viewport reports the index of the picked rigid body; the running GraphScene knows which
// entity made it (GraphScene::entityOfBody). The scene lives on the simulation thread, so the
// question is posted there and the answer comes back to the GUI thread.
void SceneBuilder::selectBody(int body) {
    QPointer<SceneBuilder> self(this);
    ctrl_->post([self, body](Simulation& s) {
        const auto* scene = dynamic_cast<const GraphScene*>(s.scene());
        const int entity = scene ? scene->entityOfBody(body) : -1;
        if (entity < 0) return;
        QMetaObject::invokeMethod(self, [self, entity] {
            if (self) self->outliner_->setCurrentRow(entity);
        }, Qt::QueuedConnection);
    });
}
