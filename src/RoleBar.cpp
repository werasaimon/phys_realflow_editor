// The role bar (see RoleBar.h): eight checkable picture buttons in two rows of four.
#include "RoleBar.h"

#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>

namespace {

const char* kTips[] = {
    "Твёрдое тело: не гнётся; падает, сталкивается, катится",
    "Мягкое тело: гнётся и пружинит, как желе",
    "Жидкость: форма заполняется водой",
    "Ткань: плоскость становится полотном — висит, развевается, рвётся",
    "Магнит: притягивает и поворачивает другие магниты",
    "Дым: оставляет дымный след, куда бы ни летела форма (включит газ)",
    "Горит: загорается от пламени и горит (включит газ)",
    "Тепло: горячее пятно, воздух над ним поднимается (включит газ)",
};

} // namespace

QString roleName(RoleIcon role) {
    const char* names[] = {"Твёрдое", "Мягкое", "Жидкость", "Ткань", "Магнит", "Дым", "Горит", "Тепло"};
    return names[int(role)];
}

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
    case RoleIcon::Count: break;
    }
    return false;
}

RoleBar::RoleBar(QWidget* parent) : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    auto* madeOf = new QLabel("из чего");
    auto* alsoDoes = new QLabel("что ещё делает");
    for (QLabel* l : {madeOf, alsoDoes}) l->setObjectName("roleRowLabel");
    grid->addWidget(madeOf, 0, 0, 1, 4);
    grid->addWidget(alsoDoes, 2, 0, 1, 4);
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        const RoleIcon role = RoleIcon(k);
        auto* b = new QToolButton;
        b->setCheckable(true);
        b->setIcon(roleIcon(role, 40));
        b->setIconSize(QSize(40, 40));
        b->setText(roleName(role));
        b->setToolTip(kTips[k]);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setObjectName("roleButton");
        connect(b, &QToolButton::clicked, this, [this, role] { emit roleClicked(role); });
        buttons_[k] = b;
        grid->addWidget(b, isMadeOfRole(role) ? 1 : 3, k % 4);
    }
}

void RoleBar::setRoles(const rf::Entity& e) {
    for (int k = 0; k < int(RoleIcon::Count); ++k) {
        const QSignalBlocker quiet(buttons_[k]);
        buttons_[k]->setChecked(roleEnabled(e, RoleIcon(k)));
    }
}
