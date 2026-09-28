// The Laboratory's card of a contact point or a body (see LabCard.h).
#include "LabCard.h"

#include <QGridLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

// The frame: the title, the line under it, the rows in a two-column grid, the one button.
LabCard::LabCard(QWidget* parent) : QFrame(parent) {
    setObjectName("labCard");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 10, 12, 12);
    col->setSpacing(6);
    title_ = new QLabel;
    title_->setObjectName("labCardTitle");
    subtitle_ = new QLabel;
    subtitle_->setObjectName("labCardSubtitle");
    subtitle_->setWordWrap(true);
    rowsHost_ = new QWidget;
    grid_ = new QGridLayout(rowsHost_);
    grid_->setContentsMargins(0, 4, 0, 0);
    grid_->setHorizontalSpacing(10);
    grid_->setVerticalSpacing(3);
    grid_->setColumnStretch(1, 1);
    button_ = new QPushButton;
    button_->setObjectName("labCardButton");
    button_->setCursor(Qt::PointingHandCursor);
    connect(button_, &QPushButton::clicked, this, [this] {
        if (onClick_) onClick_();
    });
    col->addWidget(title_);
    col->addWidget(subtitle_);
    col->addWidget(rowsHost_);
    col->addWidget(button_);
    showHint();
}

// Nothing picked yet: the card says what to click, and that a right click on a number plots it.
void LabCard::showHint() {
    showThing("Карточка", "Щёлкните в виде по точке контакта (включите слой «Точки контакта») или по телу — "
                          "здесь появятся его числа. Правый щелчок по числу — график.",
              {});
    setButton(QString(), QString(), nullptr);
}

// 1. The title and the line under it. 2. The old rows go. 3. The new rows, one per line.
void LabCard::showThing(const QString& title, const QString& subtitle, const std::vector<Row>& rows) {
    title_->setText(title);
    subtitle_->setText(subtitle);
    subtitle_->setVisible(!subtitle.isEmpty());
    while (QLayoutItem* item = grid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    rows_ = rows;
    for (int r = 0; r < int(rows.size()); ++r) addRow(r, rows[size_t(r)]);
    rowsHost_->setVisible(!rows.empty());
}

// One row: the name at the left; the value with its unit at the right, selectable (to copy it), and
// a right click on it that offers its graph.
void LabCard::addRow(int r, const Row& row) {
    auto* name = new QLabel(row.name);
    name->setObjectName("labCardName");
    auto* value = new QLabel(row.value + (row.unit.isEmpty() ? QString() : " " + row.unit));
    value->setObjectName("labCardValue");
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setContextMenuPolicy(Qt::CustomContextMenu);
    const std::string channel = row.channel;
    connect(value, &QLabel::customContextMenuRequested, this,
            [this, value, channel](const QPoint& at) { showValueMenu(value, channel, at); });
    grid_->addWidget(name, r, 0);
    grid_->addWidget(value, r, 1);
}

// «Построить график» of the value's channel. A number with no channel yet (one object's own numbers)
// keeps the item, greyed, and says when it comes: never a button that silently does nothing.
void LabCard::showValueMenu(QLabel* value, const std::string& channel, const QPoint& at) {
    QMenu menu(value);
    QAction* plot = menu.addAction("Построить график");
    if (channel.empty()) {
        plot->setEnabled(false);
        menu.addAction("числа одного объекта — появится в части 2")->setEnabled(false);
    }
    if (menu.exec(value->mapToGlobal(at)) == plot && !channel.empty()) emit plotRequested(QString::fromStdString(channel));
}

void LabCard::setButton(const QString& text, const QString& tip, std::function<void()> onClick) {
    button_->setText(text);
    button_->setToolTip(tip);
    button_->setVisible(!text.isEmpty());
    onClick_ = std::move(onClick);
}

QString LabCard::value(const QString& name) const {
    for (const Row& r : rows_)
        if (r.name == name) return r.value;
    return QString();
}

QString LabCard::title() const { return title_->text(); }
