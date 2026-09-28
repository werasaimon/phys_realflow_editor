#pragma once
// The lists of quantities the plots can draw, grouped as the SDK groups them - Сцена, Объект,
// Группа, Поле - with the engine's own timers and counters folded away under «Движок (для
// разработчиков)». Each row: the line's colour when it is drawn, the name in Russian, the unit, the
// value right now. A search field on top filters by the words of the name (and, unseen, by the
// channel's id, for those who know it). Two uses:
//   - a chart's list (the ▾ after its title): a click on a row shows that quantity on the chart
//     instead; the ＋ at the row's end adds it as one more line (✓: it is there - a click takes it off);
//   - the checklist («Ещё величины…», for those who want everything): a checkbox per row - drawn or
//     not, on whichever chart fits its unit; three presets on top.
// The list opens as a panel over the window, not as a popup window of its own: it stays open whatever
// program is in front (a popup closes the moment its application is not the active one), and a
// click outside it or Esc closes it. The list only shows and reports: the panel (PlotPanel) owns the
// charts and hands it the rows.
#include "PlotChannels.h"

#include <QColor>
#include <QFrame>

#include <map>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class ChannelList : public QFrame {
    Q_OBJECT
public:
    enum class Mode { Chart, Checklist };

    // One row of the list.
    struct Row {
        PlotChannel channel;
        bool on = false;                    // on this chart (Chart) / drawn anywhere (Checklist)
        QColor color;                       // the line's colour, when it is drawn
        Qt::PenStyle style = Qt::SolidLine;
        QString value;                      // the latest value, in words
    };

    ChannelList(Mode mode, QWidget* parent);

    void setRows(const std::vector<Row>& rows);
    void updateValues(const std::map<std::string, QString>& values);
    // Shown under `rect` of `anchor` (above it when there is no room below), the search field ready.
    void popupAt(QWidget* anchor, const QRect& rect);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // a click outside, Esc: closed
    void hideEvent(class QHideEvent* event) override;

public:

    // What a click does (the self-test presses the rows this way too).
    void choose(const std::string& id); // Chart: this quantity instead of the chart's lines
    void toggle(const std::string& id); // Chart: the ＋ / ✓; Checklist: the checkbox

    // For the self-test.
    QLineEdit* search() const { return search_; }
    QTreeWidget* tree() const { return tree_; }
    std::vector<std::string> visibleChannels() const; // the rows the filter lets through, top to bottom
    QTreeWidgetItem* rowOf(const std::string& id) const;
    QString groupOfRow(const std::string& id) const;  // the title of the group a channel is listed in
    std::vector<QString> shownTexts() const;          // every word the open list shows

signals:
    void chosen(std::string id);
    void toggled(std::string id, bool on);
    void presetChosen(QString name);

private:
    void applyFilter();
    QTreeWidgetItem* groupItem(PlotGroup group);

    Mode mode_;
    QLabel* hint_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    std::map<PlotGroup, QTreeWidgetItem*> groups_;
    std::map<PlotGroup, bool> expanded_; // what the reader folded or unfolded (while not searching)
};
