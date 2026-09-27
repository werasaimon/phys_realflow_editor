// The scene builder's two panels (see SceneBuilder.h): building the widgets, and copying between
// the scene graph and the widgets. The editing logic is in SceneBuilder.cpp.
#include "SceneBuilder.h"

#include "InspectorWidgets.h"
#include "ObjectInspector.h"
#include "RoleBar.h"

#include <QAction>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTreeWidget>

using namespace rf;

namespace {

const char* kShapeNames[] = {"Куб", "Сфера", "Цилиндр", "Конус", "Плоскость"};

// What the thing is, in words, for the scene list: "Куб 1 — твёрдое, магнит".
QString rolesText(const Entity& e) {
    QStringList roles;
    for (int k = 0; k < int(RoleIcon::Count); ++k)
        if (roleEnabled(e, RoleIcon(k))) roles << roleName(RoleIcon(k)).toLower();
    if (e.rigid.enabled && e.rigid.fixed) roles.replaceInStrings("твёрдое", "неподвижное");
    const QString name = QString::fromStdString(e.name);
    return roles.isEmpty() ? name : name + " — " + roles.join(", ");
}

QVBoxLayout* column(QWidget* w, int margin) {
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(margin, margin, margin, margin);
    col->setSpacing(8);
    return col;
}

} // namespace

// ---------------------------------------------------------------------------
// Actions: the big "create" buttons and undo / redo
// ---------------------------------------------------------------------------
void SceneBuilder::buildActions() {
    const char* tips[] = {"Куб 20 см: появится над полом и упадёт", "Шар", "Цилиндр", "Конус",
                          "Плоскость 3 × 3 м: пол, стол, наклонная плоскость — или ткань"};
    for (int k = 0; k < 5; ++k) {
        const ShapeKind shape = ShapeKind(k);
        auto* a = new QAction(shapeIcon(shape, 48), kShapeNames[k], this);
        a->setToolTip(tips[k]);
        connect(a, &QAction::triggered, this, [this, shape] { addEntity(shape); });
        createActions_.push_back(a);
    }
    undoAct_ = new QAction(controlIcon(ControlIcon::Undo, 48), "Отменить", this);
    undoAct_->setShortcut(QKeySequence::Undo);
    redoAct_ = new QAction(controlIcon(ControlIcon::Redo, 48), "Повторить", this);
    redoAct_->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")});
    connect(undoAct_, &QAction::triggered, this, &SceneBuilder::undo);
    connect(redoAct_, &QAction::triggered, this, &SceneBuilder::redo);
}

// ---------------------------------------------------------------------------
// Scene list: everything in the scene, with the eye and the lock; the world below
// ---------------------------------------------------------------------------
void SceneBuilder::buildSceneList() {
    listPanel_ = new QWidget;
    listPanel_->setObjectName("builderPanel");
    auto* col = column(listPanel_, 8);
    list_ = new QTreeWidget;
    list_->setMinimumHeight(150);
    list_->setColumnCount(3);
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(false);
    list_->setIconSize(QSize(22, 22));
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c : {1, 2}) {
        list_->header()->setSectionResizeMode(c, QHeaderView::Fixed);
        list_->setColumnWidth(c, 28);
    }
    list_->setToolTip("Всё, что есть в сцене. Глаз — видимость, замок — мышь не трогает. Delete — удалить.");
    connect(list_, &QTreeWidget::itemClicked, this, &SceneBuilder::onListClicked);
    connect(list_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item && !filling_) setSelected(item->data(0, Qt::UserRole).toUInt());
    });
    auto* del = new QShortcut(QKeySequence::Delete, list_, nullptr, nullptr, Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, &SceneBuilder::removeSelected);
    col->addWidget(list_, 1);

    auto* row = new QHBoxLayout;
    auto* dup = new QPushButton("Дублировать");
    auto* rem = new QPushButton("Удалить");
    connect(dup, &QPushButton::clicked, this, &SceneBuilder::duplicateSelected);
    connect(rem, &QPushButton::clicked, this, &SceneBuilder::removeSelected);
    row->addWidget(dup);
    row->addWidget(rem);
    col->addLayout(row);
    buildWorldSection(col);
}

void SceneBuilder::buildWorldSection(QVBoxLayout* col) {
    auto* world = new CollapsibleSection("Мир: вся сцена");
    auto* f = new QFormLayout;
    world->body()->addLayout(f);
    gravity_ = new Vec3Row(-50, 50, 0.5, 2);
    worldSize_ = new Vec3Row(0.5, 50, 0.5, 1);
    f->addRow("Тяжесть, м/с²", gravity_);
    f->addRow("Коробка, м", worldSize_);
    gas_ = new QCheckBox("Газ: воздух с дымом и теплом");
    plasma_ = new QCheckBox("Плазма: магниты толкают газ");
    f->addRow(gas_);
    f->addRow(plasma_);
    for (Vec3Row* r : {gravity_, worldSize_}) connect(r, &Vec3Row::edited, this, &SceneBuilder::onWorldEdited);
    for (QCheckBox* c : {gas_, plasma_}) connect(c, &QCheckBox::toggled, this, &SceneBuilder::onWorldEdited);
    col->addWidget(world);
}

// ---------------------------------------------------------------------------
// Inspector: header, role bar, "Объект", "Форма", the details of the roles that are on
// ---------------------------------------------------------------------------
void SceneBuilder::buildInspector() {
    inspectorPanel_ = new QWidget;
    inspectorPanel_->setObjectName("builderPanel");
    auto* col = column(inspectorPanel_, 8);
    emptyHint_ = new QLabel("Выберите объект в сцене\nили создайте новый кнопкой сверху.");
    emptyHint_->setAlignment(Qt::AlignCenter);
    emptyHint_->setObjectName("emptyHint");
    col->addWidget(emptyHint_);
    inspectorBody_ = new QWidget;
    auto* body = column(inspectorBody_, 0);
    buildHeader(body);
    roleBar_ = new RoleBar;
    connect(roleBar_, &RoleBar::roleClicked, this, &SceneBuilder::toggleRole);
    body->addWidget(roleBar_);
    object_ = new ObjectInspector;
    connect(object_, &ObjectInspector::edited, this, &SceneBuilder::onObjectEdited);
    body->addWidget(object_);
    buildShapeBlock(body);
    buildMaterialSections(body);
    buildBehaviourSections(body);
    col->addWidget(inspectorBody_);
    col->addStretch(1);
}

void SceneBuilder::buildHeader(QVBoxLayout* col) {
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    headerIcon_ = new QLabel;
    headerIcon_->setFixedSize(44, 44);
    headerName_ = new QLabel;
    headerName_->setObjectName("headerName");
    headerChips_ = new QWidget;
    auto* chips = new QHBoxLayout(headerChips_);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(2);
    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    text->addWidget(headerName_);
    text->addWidget(headerChips_);
    row->addWidget(headerIcon_);
    row->addLayout(text, 1);
    col->addLayout(row);
}

void SceneBuilder::buildShapeBlock(QVBoxLayout* col) {
    col->addWidget(sectionTitle("Форма"));
    auto* f = new QFormLayout;
    f->setSpacing(6);
    shape_ = new QComboBox;
    shape_->setIconSize(QSize(20, 20));
    for (int k = 0; k < 5; ++k) shape_->addItem(shapeIcon(ShapeKind(k), 20), kShapeNames[k]);
    connect(shape_, &QComboBox::currentIndexChanged, this, &SceneBuilder::onDetailsEdited);
    f->addRow("Что", shape_);
    size_ = vectorField(f, "Размер, м", 0.005, 20, 0.05, 2);
    col->addLayout(f);
}

CollapsibleSection* SceneBuilder::roleSection(QVBoxLayout* col, RoleIcon role, QFormLayout*& form) {
    auto* s = new CollapsibleSection(roleName(role), roleIcon(role, 20));
    form = new QFormLayout;
    form->setSpacing(6);
    s->body()->addLayout(form);
    sections_[int(role)] = s;
    col->addWidget(s);
    return s;
}

QDoubleSpinBox* SceneBuilder::numberField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals) {
    QDoubleSpinBox* s = makeSpin(min, max, step, decimals);
    connect(s, &QDoubleSpinBox::valueChanged, this, &SceneBuilder::onDetailsEdited);
    f->addRow(label, s);
    return s;
}

QCheckBox* SceneBuilder::checkField(QFormLayout* f, const QString& text) {
    auto* c = new QCheckBox(text);
    connect(c, &QCheckBox::toggled, this, &SceneBuilder::onDetailsEdited);
    f->addRow(c);
    return c;
}

Vec3Row* SceneBuilder::vectorField(QFormLayout* f, const QString& label, double min, double max, double step, int decimals) {
    auto* r = new Vec3Row(min, max, step, decimals);
    connect(r, &Vec3Row::edited, this, &SceneBuilder::onDetailsEdited);
    f->addRow(label, r);
    return r;
}

void SceneBuilder::buildMaterialSections(QVBoxLayout* col) {
    col->addWidget(sectionTitle("Роли"));
    QFormLayout* f = nullptr;
    roleSection(col, RoleIcon::Rigid, f);
    rigidDensity_ = numberField(f, "Плотность, кг/м³", 1, 30000, 50, 0);
    friction_ = numberField(f, "Трение", 0, 2, 0.05, 2);
    restitution_ = numberField(f, "Упругость удара", 0, 1, 0.05, 2);
    fixed_ = checkField(f, "неподвижное (стена, стол)");
    velocity_ = vectorField(f, "Скорость, м/с", -100, 100, 0.5, 2);
    spin_ = vectorField(f, "Вращение, рад/с", -200, 200, 0.5, 2);
    roleSection(col, RoleIcon::Soft, f);
    softDensity_ = numberField(f, "Плотность, кг/м³", 1, 5000, 10, 0);
    stiffness_ = numberField(f, "Жёсткость", 0.01, 1, 0.05, 2);
    roleSection(col, RoleIcon::Liquid, f);
    f->addRow(new QLabel("Форма заполняется водой."));
    roleSection(col, RoleIcon::Cloth, f);
    clothDensity_ = numberField(f, "Плотность, кг/м²", 0.01, 5, 0.05, 2);
    bend_ = numberField(f, "Мягкость изгиба", 0, 1, 0.001, 4);
    tearable_ = checkField(f, "рвётся, если сильно потянуть");
    const char* edges[] = {"край −X", "край +X", "край −Z", "край +Z", "верх на пруте"};
    f->addRow(new QLabel("Закреплено:"));
    for (int k = 0; k < 5; ++k) pinned_[k] = checkField(f, edges[k]);
}

void SceneBuilder::buildBehaviourSections(QVBoxLayout* col) {
    QFormLayout* f = nullptr;
    roleSection(col, RoleIcon::Magnet, f);
    moment_ = vectorField(f, "Момент, А·м²", -1000, 1000, 0.1, 2);
    roleSection(col, RoleIcon::Smoke, f);
    emitSmoke_ = numberField(f, "Дым в секунду", 0, 20, 0.1, 2);
    emitTemperature_ = numberField(f, "Перегрев, K", 0, 2000, 10, 0);
    emitLiquid_ = numberField(f, "Капель в секунду", 0, 5000, 50, 0);
    emitVelocity_ = vectorField(f, "Выброс, м/с", -20, 20, 0.5, 1);
    roleSection(col, RoleIcon::Flame, f);
    f->addRow(new QLabel("Загорается от пламени и горит.\nГазу нужен кислород — он включается сам."));
    roleSection(col, RoleIcon::Heat, f);
    heatTemperature_ = numberField(f, "Перегрев, K", 0, 3000, 10, 0);
    heatSmoke_ = numberField(f, "Дым", 0, 10, 0.1, 2);
}

// ---------------------------------------------------------------------------
// Graph <-> widgets
// ---------------------------------------------------------------------------
void SceneBuilder::fillInspector() {
    const Entity* e = selectedEntity();
    emptyHint_->setVisible(!e);
    inspectorBody_->setVisible(e != nullptr);
    if (!e) return;
    filling_ = true;
    object_->setObject(*e);
    {
        const QSignalBlocker quiet(shape_);
        shape_->setCurrentIndex(int(e->shape));
    }
    size_->setValue(e->size);
    fillDetails(*e);
    roleBar_->setRoles(*e);
    filling_ = false;
    refreshSections();
    refreshHeader();
}

void SceneBuilder::fillDetails(const Entity& e) {
    rigidDensity_->setValue(e.rigid.density);
    friction_->setValue(e.rigid.friction);
    restitution_->setValue(e.rigid.restitution);
    fixed_->setChecked(e.rigid.fixed);
    velocity_->setValue(e.rigid.velocity);
    spin_->setValue(e.rigid.angularVelocity);
    softDensity_->setValue(e.soft.density);
    stiffness_->setValue(e.soft.stiffness);
    clothDensity_->setValue(e.cloth.areaDensity);
    bend_->setValue(e.cloth.bendCompliance);
    tearable_->setChecked(e.cloth.tearable);
    const int bits[] = {1, 2, 4, 8, 16};
    for (int k = 0; k < 5; ++k) pinned_[k]->setChecked(e.cloth.pinnedEdges & bits[k]);
    moment_->setValue(e.magnet.moment);
    emitSmoke_->setValue(e.emitter.smoke);
    emitTemperature_->setValue(e.emitter.temperature);
    emitLiquid_->setValue(e.emitter.liquid);
    emitVelocity_->setValue(e.emitter.velocity);
    heatTemperature_->setValue(e.heat.temperature);
    heatSmoke_->setValue(e.heat.smoke);
}

void SceneBuilder::readDetails(Entity& e) const {
    e.shape = ShapeKind(shape_->currentIndex());
    e.size = size_->value();
    e.rigid.density = float(rigidDensity_->value());
    e.rigid.friction = float(friction_->value());
    e.rigid.restitution = float(restitution_->value());
    e.rigid.fixed = fixed_->isChecked();
    e.rigid.velocity = velocity_->value();
    e.rigid.angularVelocity = spin_->value();
    e.soft.density = float(softDensity_->value());
    e.soft.stiffness = float(stiffness_->value());
    e.cloth.areaDensity = float(clothDensity_->value());
    e.cloth.bendCompliance = float(bend_->value());
    e.cloth.tearable = tearable_->isChecked();
    const int bits[] = {1, 2, 4, 8, 16};
    e.cloth.pinnedEdges = 0;
    for (int k = 0; k < 5; ++k) e.cloth.pinnedEdges |= pinned_[k]->isChecked() ? bits[k] : 0;
    e.magnet.moment = moment_->value();
    e.emitter.smoke = float(emitSmoke_->value());
    e.emitter.temperature = float(emitTemperature_->value());
    e.emitter.liquid = float(emitLiquid_->value());
    e.emitter.velocity = emitVelocity_->value();
    e.heat.temperature = float(heatTemperature_->value());
    e.heat.smoke = float(heatSmoke_->value());
}

void SceneBuilder::fillWorld() {
    const QSignalBlocker a(gas_), b(plasma_);
    gravity_->setValue(graph_.world.gravity);
    worldSize_->setValue(graph_.world.size);
    gas_->setChecked(graph_.world.gas);
    plasma_->setChecked(graph_.world.magneticGas);
}

void SceneBuilder::refreshList() {
    const QSignalBlocker quiet(list_);
    const bool wasFilling = filling_;
    filling_ = true;
    list_->clear();
    for (const Entity& e : graph_.entities) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, rolesText(e));
        item->setIcon(0, shapeIcon(e.shape, 22));
        item->setData(0, Qt::UserRole, e.id);
        item->setIcon(1, controlIcon(e.visible ? ControlIcon::Visible : ControlIcon::Hidden, 18));
        item->setIcon(2, controlIcon(e.locked ? ControlIcon::Locked : ControlIcon::Unlocked, 18));
        item->setToolTip(1, "Видимость");
        item->setToolTip(2, "Замок: мышь не выделяет и не двигает");
        if (!e.visible) item->setForeground(0, QColor(120, 124, 132)); // hidden: greyed
        if (e.id == selectedId_) list_->setCurrentItem(item);
    }
    filling_ = wasFilling;
}

void SceneBuilder::refreshSections() {
    const Entity* e = selectedEntity();
    for (int k = 0; k < int(RoleIcon::Count); ++k)
        sections_[k]->setVisible(e && roleEnabled(*e, RoleIcon(k)));
}

void SceneBuilder::refreshHeader() {
    const Entity* e = selectedEntity();
    if (!e) return;
    headerIcon_->setPixmap(shapeIcon(e->shape, 44).pixmap(44, 44));
    headerName_->setText(QString::fromStdString(e->name));
    QLayout* chips = headerChips_->layout();
    while (QLayoutItem* it = chips->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        if (!roleEnabled(*e, RoleIcon(k))) continue;
        auto* chip = new QLabel;
        chip->setPixmap(rolePixmap(RoleIcon(k), 20));
        chip->setToolTip(roleName(RoleIcon(k)));
        chips->addWidget(chip);
    }
    if (chips->count() == 0) chips->addWidget(new QLabel("только форма — выберите роль ниже"));
    static_cast<QHBoxLayout*>(chips)->addStretch(1);
}
