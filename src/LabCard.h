#pragma once
// The Laboratory's card: what a click in the 3D view found - a contact point or a body - as a calm
// list of named numbers, each with its unit, as PhysX PVD's property view shows an object.
//
// Every number answers a right click with «Построить график». When the engine reports that quantity
// as a Probe channel the menu adds it to the plots at the bottom (the plotRequested signal); a quantity
// of one object only (the mass of this body, the depth of this contact) has no channel yet, so the menu
// says, greyed out, that it comes in part 2 - never a button that silently does nothing.
#include <QFrame>
#include <QString>

#include <functional>
#include <string>
#include <vector>

class QGridLayout;
class QLabel;
class QPoint;
class QPushButton;

class LabCard : public QFrame {
    Q_OBJECT
public:
    explicit LabCard(QWidget* parent = nullptr);

    // One line of the card: «Глубина» «2,1» «мм»; `channel` - the Probe channel it can be plotted
    // from ("" - none yet).
    struct Row {
        QString name;
        QString value;
        QString unit;
        std::string channel;
    };

    // The card for something: a title («Точка контакта»), a line under it (which bodies), the rows.
    void showThing(const QString& title, const QString& subtitle, const std::vector<Row>& rows);
    // The empty card: what to click to fill it.
    void showHint();
    // One button under the rows («Следить за парой»): its words and what it does. Empty text: none.
    void setButton(const QString& text, const QString& tip, std::function<void()> onClick);

    // For the self-test: the value shown on the row with this name ("" - no such row).
    QString value(const QString& name) const;
    QString title() const;

signals:
    void plotRequested(QString channel);

private:
    void addRow(int r, const Row& row);
    void showValueMenu(QLabel* value, const std::string& channel, const QPoint& at);

    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QWidget* rowsHost_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QPushButton* button_ = nullptr;
    std::function<void()> onClick_;
    std::vector<Row> rows_;
};
