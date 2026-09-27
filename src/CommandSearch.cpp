// The command search (see CommandSearch.h).
#include "CommandSearch.h"

#include <QAction>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace {

QString plain(QString text) { return text.remove('&').trimmed(); } // "&Правка" -> "Правка"

// How well `name` matches `query` (lower is better, -1: not at all): starts with it, contains it,
// or has its letters in the same order.
int matchRank(const QString& name, const QString& query) {
    if (query.isEmpty()) return 3;
    const QString n = name.toLower(), q = query.toLower();
    if (n.startsWith(q)) return 0;
    if (n.contains(q)) return 1;
    int at = 0;
    for (const QChar c : q) {
        at = n.indexOf(c, at);
        if (at < 0) return -1;
        ++at;
    }
    return 2;
}

} // namespace

CommandSearch::CommandSearch(QMainWindow* window) : QFrame(window, Qt::Popup), window_(window) {
    setObjectName("commandSearch");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(10, 10, 10, 10);
    col->setSpacing(6);
    field_ = new QLineEdit;
    field_->setPlaceholderText("Найти команду: например «группа», «коллайдер», «сохранить»…");
    field_->setClearButtonEnabled(true);
    col->addWidget(field_);
    list_ = new QListWidget;
    list_->setObjectName("commandList");
    list_->setUniformItemSizes(true);
    col->addWidget(list_);
    auto* tip = new QLabel("Enter — выполнить · ↑ ↓ — выбрать · Esc — закрыть");
    tip->setObjectName("commandTip");
    col->addWidget(tip);
    connect(field_, &QLineEdit::textChanged, this, &CommandSearch::refill);
    connect(field_, &QLineEdit::returnPressed, this, &CommandSearch::runSelected);
    connect(list_, &QListWidget::itemActivated, this, [this] { runSelected(); });
    resize(620, 420);
}

void CommandSearch::open() {
    collect();
    field_->clear();
    refill();
    const QPoint topCentre = window_->mapToGlobal(QPoint((window_->width() - width()) / 2, 90));
    move(topCentre);
    show();
    field_->setFocus();
}

void CommandSearch::setQuery(const QString& text) {
    field_->setText(text); // refill() follows through textChanged
}

std::vector<QAction*> CommandSearch::results() const { return shown_; }

void CommandSearch::runSelected() {
    const int row = std::max(0, list_->currentRow());
    if (row >= int(rowAction_.size()) || !rowAction_[size_t(row)]) return; // the "Похожие" line runs nothing
    QAction* a = rowAction_[size_t(row)];
    hide();
    if (a->isEnabled()) a->trigger();
}

void CommandSearch::keyPressEvent(QKeyEvent* e) {
    const int row = list_->currentRow();
    if (e->key() == Qt::Key_Down) list_->setCurrentRow(std::min(row + 1, list_->count() - 1));
    else if (e->key() == Qt::Key_Up) list_->setCurrentRow(std::max(row - 1, 0));
    else QFrame::keyPressEvent(e); // Esc closes the popup
}

// The menus first (they give the paths), then the toolbars' actions nobody put in a menu.
void CommandSearch::collect() {
    commands_.clear();
    for (QAction* top : window_->menuBar()->actions())
        if (top->menu()) collectMenu(top->menu(), plain(top->text()));
    std::set<QAction*> seen;
    for (const Command& c : commands_) seen.insert(c.action);
    for (QToolBar* bar : window_->findChildren<QToolBar*>())
        for (QAction* a : bar->actions())
            if (!a->isSeparator() && !a->text().isEmpty() && !a->menu() && seen.insert(a).second)
                commands_.push_back({a, plain(a->text()), "Панель", a->shortcut().toString(QKeySequence::NativeText)});
}

void CommandSearch::collectMenu(QMenu* menu, const QString& path) {
    for (QAction* a : menu->actions()) {
        if (a->isSeparator() || a->text().isEmpty()) continue;
        if (a->menu()) {
            collectMenu(a->menu(), path + " › " + plain(a->text()));
            continue;
        }
        commands_.push_back({a, plain(a->text()), path, a->shortcut().toString(QKeySequence::NativeText)});
    }
}

// The matching commands, best first: those whose name holds the typed text (starting with it
// first), then - under a grey line "Похожие по буквам" - those that only have its letters in order.
// A disabled command is shown greyed (it says the command exists).
void CommandSearch::refill() {
    const QString query = field_->text().trimmed();
    std::vector<std::pair<int, const Command*>> ranked;
    for (const Command& c : commands_) {
        const int r = matchRank(c.name, query);
        if (r >= 0) ranked.push_back({r, &c});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    list_->clear();
    shown_.clear();
    rowAction_.clear();
    substringCount_ = 0;
    bool fuzzyLine = false;
    for (const auto& [rank, c] : ranked) {
        if (rank == 2 && !query.isEmpty() && !fuzzyLine) { // the letters-in-order matches start here
            auto* line = new QListWidgetItem("Похожие по буквам");
            line->setFlags(Qt::NoItemFlags);
            line->setForeground(QColor(120, 126, 138));
            list_->addItem(line);
            rowAction_.push_back(nullptr);
            fuzzyLine = true;
        }
        substringCount_ += rank <= 1 ? 1 : 0;
        const QString key = c->key.isEmpty() ? QString() : "   [" + c->key + "]";
        auto* item = new QListWidgetItem(c->name + key + "\n" + c->path);
        if (!c->action->isEnabled()) item->setForeground(QColor(120, 126, 138));
        item->setToolTip(c->action->toolTip());
        list_->addItem(item);
        shown_.push_back(c->action);
        rowAction_.push_back(c->action);
    }
    list_->setCurrentRow(rowAction_.empty() || rowAction_[0] ? 0 : 1);
}
