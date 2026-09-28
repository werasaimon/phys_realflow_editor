// The plots' lists of quantities (see ChannelList.h): the popup, its search, its rows.
#include "ChannelList.h"

#include <QApplication>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kIdRole = Qt::UserRole + 1; // a channel row: its id; a group row: none
constexpr int kOnRole = Qt::UserRole + 2; // a channel row: on the chart / drawn
constexpr int kAddColumn = 3;             // the chart list's ＋ / ✓

// The popup in the editor's dark theme; the rows airy, the group rows quiet.
const char* kStyle =
    "QFrame#channelList { background: #23262c; border: 1px solid #3a3f48; border-radius: 8px; }"
    "QFrame#channelList QLineEdit { background: #1c1e23; border: 1px solid #3a3f48; border-radius: 6px;"
    " padding: 5px 8px; color: #e1e4eb; }"
    "QFrame#channelList QLineEdit:focus { border-color: #4096ff; }"
    "QFrame#channelList QPushButton { background: #2c3038; border: 1px solid #3a3f48; border-radius: 6px;"
    " padding: 3px 10px; color: #e1e4eb; }"
    "QFrame#channelList QPushButton:hover { background: #363c47; }"
    "QFrame#channelList QLabel { color: #8b93a2; }"
    "QFrame#channelList QTreeWidget { background: #1c1e23; border: 1px solid #3a3f48; border-radius: 6px; color: #e1e4eb; }"
    "QFrame#channelList QTreeWidget::item { padding: 2px 0; }"
    "QFrame#channelList QTreeWidget::item:hover { background: #2a3140; }";

// The swatch beside a drawn quantity's name: a short stroke of its own line, as in the legend.
QIcon swatch(const QColor& color, Qt::PenStyle style) {
    QPixmap pm(18, 12);
    pm.fill(Qt::transparent);
    if (color.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 2.5, style, Qt::FlatCap));
        p.drawLine(QPointF(1, 6), QPointF(17, 6));
    }
    return QIcon(pm);
}

} // namespace

// The popup: a line of help, the search field, the presets (checklist only), the list.
ChannelList::ChannelList(Mode mode, QWidget* parent) : QFrame(parent), mode_(mode) {
    setObjectName("channelList");
    setStyleSheet(kStyle);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(10, 10, 10, 10);
    col->setSpacing(8);

    hint_ = new QLabel(mode == Mode::Chart ? "Щелчок по строке — показать эту величину на графике; ＋ — добавить ещё одну линию"
                                           : "Галочка — нарисовать величину; величины одной единицы делят один график");
    hint_->setWordWrap(true);
    col->addWidget(hint_);
    search_ = new QLineEdit;
    search_->setObjectName("channelSearch");
    search_->setPlaceholderText("Найти: «энергия», «скорость», «высота»…");
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, &ChannelList::applyFilter);
    col->addWidget(search_);

    if (mode == Mode::Checklist) {
        auto* presets = new QHBoxLayout;
        presets->addWidget(new QLabel("Сразу:"));
        for (const char* name : {"Энергия", "Движок", "Сбросить"}) {
            auto* b = new QPushButton(name);
            b->setObjectName(QString("channelPreset_") + name);
            connect(b, &QPushButton::clicked, this, [this, name] { emit presetChosen(QString(name)); });
            presets->addWidget(b);
        }
        presets->addStretch(1);
        col->addLayout(presets);
    }

    tree_ = new QTreeWidget;
    tree_->setObjectName("channelTree");
    tree_->setColumnCount(mode == Mode::Chart ? 4 : 3);
    tree_->setHeaderHidden(true);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < tree_->columnCount(); ++c) tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    tree_->header()->setStretchLastSection(false);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::NoSelection);
    tree_->setIconSize(QSize(18, 12));
    tree_->setMouseTracking(true);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        const QString id = item->data(0, kIdRole).toString();
        if (mode_ == Mode::Checklist && column == 0 && !id.isEmpty())
            emit toggled(id.toStdString(), item->checkState(0) == Qt::Checked);
    });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        const std::string id = item->data(0, kIdRole).toString().toStdString();
        if (mode_ != Mode::Chart || id.empty()) return;
        if (column == kAddColumn) toggle(id);
        else choose(id);
    });
    auto remember = [this](QTreeWidgetItem* item, bool on) { // the reader's folding, kept while not searching
        for (const auto& [group, g] : groups_)
            if (g == item && search_->text().isEmpty()) expanded_[group] = on;
    };
    connect(tree_, &QTreeWidget::itemExpanded, this, [remember](QTreeWidgetItem* i) { remember(i, true); });
    connect(tree_, &QTreeWidget::itemCollapsed, this, [remember](QTreeWidgetItem* i) { remember(i, false); });
    col->addWidget(tree_, 1);
    expanded_ = {{PlotGroup::Scene, true}, {PlotGroup::Object, true}, {PlotGroup::Group, true},
                 {PlotGroup::Field, true}, {PlotGroup::Engine, false}}; // the engine's internals folded away
    resize(460, mode == Mode::Chart ? 380 : 440);
    hide();
}

// A panel over the window, under the rect (above it when the window has no room below), kept inside
// the window; from now on a click anywhere else, or Esc, closes it.
void ChannelList::popupAt(QWidget* anchor, const QRect& rect) {
    QWidget* window = anchor->window();
    if (parentWidget() != window) setParent(window);
    QPoint at = anchor->mapTo(window, rect.bottomLeft() + QPoint(0, 4));
    if (at.y() + height() > window->height()) at.setY(anchor->mapTo(window, rect.topLeft()).y() - height() - 4);
    at.setX(std::clamp(at.x(), 0, std::max(0, window->width() - width())));
    at.setY(std::max(at.y(), 0));
    move(at);
    raise();
    show();
    qApp->installEventFilter(this);
    search_->setFocus();
    search_->selectAll();
}

// While open: a mouse press outside the list closes it; Esc closes it too (and only it - the
// window's Esc, «stop the scene», waits for the next press).
bool ChannelList::eventFilter(QObject* watched, QEvent* event) {
    const auto* key = event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress ? static_cast<QKeyEvent*>(event) : nullptr;
    if (key && key->key() == Qt::Key_Escape) {
        if (event->type() == QEvent::KeyPress) hide();
        event->accept();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress)
        if (auto* w = qobject_cast<QWidget*>(watched); w && w != this && !isAncestorOf(w)) hide();
    return false;
}

void ChannelList::hideEvent(QHideEvent* event) {
    qApp->removeEventFilter(this);
    QFrame::hideEvent(event);
}

void ChannelList::choose(const std::string& id) {
    hide();
    emit chosen(id);
}

void ChannelList::toggle(const std::string& id) {
    QTreeWidgetItem* row = rowOf(id);
    if (!row) return;
    const bool on = !row->data(0, kOnRole).toBool();
    if (mode_ == Mode::Checklist) row->setCheckState(0, on ? Qt::Checked : Qt::Unchecked); // itemChanged reports it
    else emit toggled(id, on);
}

QTreeWidgetItem* ChannelList::groupItem(PlotGroup group) {
    auto it = groups_.find(group);
    if (it != groups_.end()) return it->second;
    auto* g = new QTreeWidgetItem(tree_, {plotGroupTitle(group)});
    g->setFlags(Qt::ItemIsEnabled);
    g->setFirstColumnSpanned(true);
    QFont bold = g->font(0);
    bold.setBold(true);
    g->setFont(0, bold);
    g->setForeground(0, QColor(143, 193, 255));
    groups_[group] = g;
    return g;
}

// The whole list again (a new quantity appeared, a line was switched on or off):
//   1. the groups in their fixed order - Сцена, Объект, Группа, Поле, Движок - the empty ones left out;
//   2. in each, the quantities by name, each with its colour when drawn, its unit and its value;
//      the chart list's rows end in ＋ (add it) or ✓ (it is on this chart), the checklist's rows
//      start with a checkbox;
//   3. the folding, the filter and the scroll position as they were.
void ChannelList::setRows(const std::vector<Row>& rows) {
    const QSignalBlocker quiet(tree_);
    const int scroll = tree_->verticalScrollBar()->value();
    tree_->clear();
    groups_.clear();
    std::vector<const Row*> sorted;
    for (const Row& r : rows) sorted.push_back(&r);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Row* a, const Row* b) {
        if (a->channel.group != b->channel.group) return a->channel.group < b->channel.group;
        return QString::localeAwareCompare(a->channel.name, b->channel.name) < 0;
    });
    for (const Row* r : sorted) {
        auto* item = new QTreeWidgetItem(groupItem(r->channel.group), {r->channel.name, r->channel.unit, r->value});
        item->setData(0, kIdRole, QString::fromStdString(r->channel.id));
        item->setData(0, kOnRole, r->on);
        item->setIcon(0, swatch(r->on ? r->color : QColor(), r->style));
        item->setToolTip(0, r->channel.unit.isEmpty() ? r->channel.name : r->channel.name + ", " + r->channel.unit);
        item->setForeground(1, QColor(150, 157, 170));
        item->setForeground(2, QColor(150, 157, 170));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        if (mode_ == Mode::Checklist) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(0, r->on ? Qt::Checked : Qt::Unchecked);
        } else {
            item->setFlags(Qt::ItemIsEnabled);
            item->setText(kAddColumn, r->on ? "✓" : "＋");
            item->setToolTip(kAddColumn, r->on ? "Убрать эту линию с графика" : "Добавить линию на этот график");
            item->setForeground(kAddColumn, r->on ? QColor(124, 255, 154) : QColor(143, 193, 255));
        }
    }
    applyFilter();
    tree_->verticalScrollBar()->setValue(scroll);
}

// Only the values change: the rows stay (cheap enough for every refresh while the list is open).
void ChannelList::updateValues(const std::map<std::string, QString>& values) {
    const QSignalBlocker quiet(tree_);
    for (const auto& [group, g] : groups_)
        for (int i = 0; i < g->childCount(); ++i) {
            QTreeWidgetItem* item = g->child(i);
            const auto it = values.find(item->data(0, kIdRole).toString().toStdString());
            if (it != values.end() && item->text(2) != it->second) item->setText(2, it->second);
        }
}

// The search: a quantity stays when its name (or, unseen, its id) contains the words, in any case.
// A group with nothing left hides; while searching every group is open, so nothing found is folded.
void ChannelList::applyFilter() {
    const QString words = search_->text().trimmed();
    for (const auto& [group, g] : groups_) {
        int left = 0;
        for (int i = 0; i < g->childCount(); ++i) {
            QTreeWidgetItem* item = g->child(i);
            const bool match = words.isEmpty() || item->text(0).contains(words, Qt::CaseInsensitive) ||
                               item->data(0, kIdRole).toString().contains(words, Qt::CaseInsensitive);
            item->setHidden(!match);
            left += match ? 1 : 0;
        }
        g->setHidden(left == 0);
        g->setText(0, QString("%1 · %2").arg(plotGroupTitle(group)).arg(left));
        const QSignalBlocker quiet(tree_); // folding by the filter is not the reader's folding
        g->setExpanded(!words.isEmpty() || expanded_[group]);
    }
}

std::vector<std::string> ChannelList::visibleChannels() const {
    std::vector<std::string> ids;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* g = tree_->topLevelItem(i);
        if (g->isHidden()) continue;
        for (int k = 0; k < g->childCount(); ++k)
            if (!g->child(k)->isHidden()) ids.push_back(g->child(k)->data(0, kIdRole).toString().toStdString());
    }
    return ids;
}

QTreeWidgetItem* ChannelList::rowOf(const std::string& id) const {
    for (const auto& [group, g] : groups_)
        for (int k = 0; k < g->childCount(); ++k)
            if (g->child(k)->data(0, kIdRole).toString().toStdString() == id) return g->child(k);
    return nullptr;
}

QString ChannelList::groupOfRow(const std::string& id) const {
    for (const auto& [group, g] : groups_)
        if (QTreeWidgetItem* row = rowOf(id); row && row->parent() == g) return plotGroupTitle(group);
    return QString();
}

// The words a reader sees in the open list: the help line, the group titles, and the rows of the
// groups that are open.
std::vector<QString> ChannelList::shownTexts() const {
    std::vector<QString> texts = {hint_->text()};
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* g = tree_->topLevelItem(i);
        if (g->isHidden()) continue;
        texts.push_back(g->text(0));
        for (int k = 0; g->isExpanded() && k < g->childCount(); ++k)
            if (!g->child(k)->isHidden())
                for (int c = 0; c < tree_->columnCount(); ++c) texts.push_back(g->child(k)->text(c));
    }
    return texts;
}
