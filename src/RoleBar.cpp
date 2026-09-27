// The role bar (see RoleBar.h): picture buttons in two rows - four "made of", then the collider
// and the four "also does" - and the helpers that read and set a component on an entity.
#include "RoleBar.h"

#include <QBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>

namespace {

const char* kTips[] = {
    "Твёрдое тело: не гнётся; падает, сталкивается, катится (сталкивается своим коллайдером)",
    "Мягкое тело: гнётся и пружинит, как желе",
    "Жидкость: форма заполняется водой",
    "Ткань: плоскость становится полотном — висит, развевается, рвётся",
    "Магнит: притягивает и поворачивает другие магниты",
    "Дым: оставляет дымный след, куда бы ни летела форма (включит газ)",
    "Горит: загорается от пламени и горит (включит газ)",
    "Тепло: горячее пятно, воздух над ним поднимается (включит газ)",
    "Коллайдер: чем форма сталкивается. Один — неподвижное препятствие; вместе с «Твёрдым» — движущееся тело",
};

QToolButton* roleButton(RoleIcon role) {
    auto* b = new QToolButton;
    b->setCheckable(true);
    b->setIcon(roleIcon(role, 36));
    b->setIconSize(QSize(36, 36));
    b->setText(roleName(role));
    b->setToolTip(roleTip(role));
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setObjectName("roleButton");
    return b;
}

} // namespace

QString roleName(RoleIcon role) {
    const char* names[] = {"Твёрдое", "Мягкое", "Жидкость", "Ткань", "Магнит", "Дым", "Горит", "Тепло", "Коллайдер"};
    return names[int(role)];
}

QString roleTitle(RoleIcon role) {
    const char* titles[] = {"Твёрдое тело", "Мягкое тело", "Жидкость", "Ткань", "Магнит", "Излучатель", "Горит", "Тепло", "Коллайдер"};
    return titles[int(role)];
}

QString roleTip(RoleIcon role) { return kTips[int(role)]; }

bool isMadeOfRole(RoleIcon role) { return int(role) <= int(RoleIcon::Cloth); }

bool roleEnabled(const rf::Entity& e, RoleIcon role) {
    switch (role) {
    case RoleIcon::Rigid: return e.rigid.enabled;
    case RoleIcon::Soft: return e.soft.enabled;
    case RoleIcon::Liquid: return e.liquid.enabled;
    case RoleIcon::Cloth: return e.cloth.enabled;
    case RoleIcon::Magnet: return e.magnet.enabled;
    case RoleIcon::Smoke: return e.emitter.enabled;
    case RoleIcon::Flame: return e.flammable.enabled;
    case RoleIcon::Heat: return e.heat.enabled;
    case RoleIcon::Collider: return e.collider.enabled;
    case RoleIcon::Count: break;
    }
    return false;
}

void setRole(rf::Entity& e, RoleIcon role, bool on) {
    switch (role) {
    case RoleIcon::Rigid: e.rigid.enabled = on; break;
    case RoleIcon::Soft: e.soft.enabled = on; break;
    case RoleIcon::Liquid: e.liquid.enabled = on; break;
    case RoleIcon::Cloth: e.cloth.enabled = on; break;
    case RoleIcon::Magnet: e.magnet.enabled = on; break;
    case RoleIcon::Smoke: e.emitter.enabled = on; break;
    case RoleIcon::Flame: e.flammable.enabled = on; break;
    case RoleIcon::Heat: e.heat.enabled = on; break;
    case RoleIcon::Collider: e.collider.enabled = on; break;
    case RoleIcon::Count: break;
    }
}

RoleBar::RoleBar(QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    auto row = [this, col](const QString& title, std::initializer_list<RoleIcon> roles) {
        auto* label = new QLabel(title);
        label->setObjectName("roleRowLabel");
        col->addWidget(label);
        auto* line = new QHBoxLayout;
        line->setSpacing(4);
        for (RoleIcon role : roles) {
            QToolButton* b = roleButton(role);
            connect(b, &QToolButton::clicked, this, [this, role] { emit roleClicked(role); });
            buttons_[int(role)] = b;
            line->addWidget(b);
        }
        col->addLayout(line);
    };
    row("из чего", {RoleIcon::Rigid, RoleIcon::Soft, RoleIcon::Liquid, RoleIcon::Cloth});
    row("чем сталкивается и что ещё делает", {RoleIcon::Collider, RoleIcon::Magnet, RoleIcon::Smoke, RoleIcon::Flame, RoleIcon::Heat});
}

void RoleBar::setRoles(const rf::Entity& e) {
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        const QSignalBlocker quiet(buttons_[k]);
        buttons_[k]->setChecked(roleEnabled(e, RoleIcon(k)));
    }
    // Cloth is a sheet: made from a plane or the top face of a shape, not from a model (yet).
    QToolButton* cloth = buttons_[int(RoleIcon::Cloth)];
    const bool model = e.shape == rf::ShapeKind::Mesh;
    cloth->setEnabled(!model);
    cloth->setToolTip(model ? "Ткань пока только из плоскости или верхней грани формы" : kTips[int(RoleIcon::Cloth)]);
}
