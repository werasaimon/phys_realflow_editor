#pragma once
// The role bar: the quick palette of components at the top of the "Объект" tab - big picture
// buttons that say what the selected shape is. The top row is what it is MADE OF - rigid, soft,
// liquid or cloth - one at a time (clicking the lit one turns it off: the shape is then only
// drawn). The bottom row is the collider and what it ALSO DOES - magnet, smoke, burns, heat - any
// number at once. One click adds or removes a component; the same components are listed below as
// cards, and "+ Добавить компонент" offers them too.
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
void setRole(rf::Entity& e, RoleIcon role, bool on);
QString roleName(RoleIcon role);           // "Твёрдое", "Магнит" ...
QString roleTitle(RoleIcon role);          // the card's title: "Твёрдое тело", "Коллайдер" ...
QString roleTip(RoleIcon role);            // one sentence: what the component does
