#pragma once
// The role bar: big picture buttons that say what the selected shape is. The top row is what it is
// MADE OF - rigid, soft, liquid or cloth - one at a time (clicking the lit one turns it off: the
// shape is then only drawn). The bottom row is what it ALSO DOES - magnet, smoke, burns, heat - any
// number at once. One click turns a shape into a physics model; a second role makes two models
// interact on the same shape.
#include "Icons.h"

#include <QWidget>

class QToolButton;

class RoleBar : public QWidget {
    Q_OBJECT
public:
    explicit RoleBar(QWidget* parent = nullptr);
    void setRoles(const rf::Entity& e); // lights the buttons of the entity's roles; not a click

signals:
    void roleClicked(RoleIcon role);

private:
    QToolButton* buttons_[int(RoleIcon::Count)] = {};
};

bool isMadeOfRole(RoleIcon role);          // rigid, soft, liquid, cloth
bool roleEnabled(const rf::Entity& e, RoleIcon role);
QString roleName(RoleIcon role);           // "Твёрдое", "Магнит" ...
