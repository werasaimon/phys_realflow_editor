// The "Объект" block (see ObjectInspector.h): one row with the eye, the lock, the name and the
// colour swatch, then position and rotation.
#include "ObjectInspector.h"

#include "Icons.h"
#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <QColorDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

namespace {

QToolButton* toggle(const QString& tip) {
    auto* b = new QToolButton;
    b->setCheckable(true);
    b->setAutoRaise(true);
    b->setIconSize(QSize(20, 20));
    b->setToolTip(tip);
    return b;
}

QString swatchStyle(const QColor& c) {
    return QString("QPushButton { background: %1; border: 1px solid #555b66; border-radius: 4px; min-width: 28px; }").arg(c.name());
}

} // namespace

ObjectInspector::ObjectInspector(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(8);
    col->addWidget(sectionTitle("Объект"));

    auto* top = new QHBoxLayout;
    top->setSpacing(4);
    visible_ = toggle("Видимый: скрытое не участвует в расчёте");
    locked_ = toggle("Замок: мышь не выделяет и не двигает (пол, фон)");
    name_ = new QLineEdit;
    name_->setPlaceholderText("имя");
    colour_ = new QPushButton;
    colour_->setToolTip("Цвет");
    top->addWidget(visible_);
    top->addWidget(locked_);
    top->addWidget(name_, 1);
    top->addWidget(colour_);
    col->addLayout(top);

    auto* form = new QFormLayout;
    form->setSpacing(6);
    position_ = new Vec3Row(-50, 50, 0.05, 2);
    rotation_ = new Vec3Row(-360, 360, 5, 0);
    form->addRow("Где, м", position_);
    form->addRow("Поворот, °", rotation_);
    col->addLayout(form);

    connect(name_, &QLineEdit::textEdited, this, &ObjectInspector::edited);
    connect(position_, &Vec3Row::edited, this, &ObjectInspector::edited);
    connect(rotation_, &Vec3Row::edited, this, &ObjectInspector::edited);
    connect(colour_, &QPushButton::clicked, this, &ObjectInspector::pickColour);
    for (QToolButton* b : {visible_, locked_})
        connect(b, &QToolButton::toggled, this, [this] {
            updateToggleIcons();
            emit edited();
        });
}

void ObjectInspector::setObject(const rf::SceneObject& o) {
    const QSignalBlocker a(visible_), b(locked_);
    name_->setText(QString::fromStdString(o.name));
    name_->setPlaceholderText("имя");
    visible_->setChecked(o.visible);
    locked_->setChecked(o.locked);
    colourValue_ = QColor::fromRgbF(o.color.x, o.color.y, o.color.z);
    colour_->setStyleSheet(swatchStyle(colourValue_));
    position_->setValue(o.position);
    rotation_->setValue(o.rotationDeg);
    updateToggleIcons();
}

void ObjectInspector::writeTo(rf::SceneObject& o) const {
    o.name = name_->text().toStdString();
    o.visible = visible_->isChecked();
    o.locked = locked_->isChecked();
    o.color = rf::Vector3(float(colourValue_.redF()), float(colourValue_.greenF()), float(colourValue_.blueF()));
    o.position = position_->value();
    o.rotationDeg = rotation_->value();
}

void ObjectInspector::setPosition(const rf::Vector3& p) { position_->setValue(p); }

void ObjectInspector::showMixed(bool name, int positionMask, int rotationMask) {
    if (name) {
        name_->clear();
        name_->setPlaceholderText("— разные имена —");
    }
    for (int k = 0; k < 3; ++k) {
        if (positionMask & (1 << k)) ::showMixed(position_->spin(k));
        if (rotationMask & (1 << k)) ::showMixed(rotation_->spin(k));
    }
}

void ObjectInspector::pickColour() {
    const QColor c = QColorDialog::getColor(colourValue_, this, "Цвет");
    if (!c.isValid()) return;
    colourValue_ = c;
    colour_->setStyleSheet(swatchStyle(c));
    emit edited();
}

void ObjectInspector::updateToggleIcons() {
    visible_->setIcon(controlIcon(visible_->isChecked() ? ControlIcon::Visible : ControlIcon::Hidden, 20));
    locked_->setIcon(controlIcon(locked_->isChecked() ? ControlIcon::Locked : ControlIcon::Unlocked, 20));
}
