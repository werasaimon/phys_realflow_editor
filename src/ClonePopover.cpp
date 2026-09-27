// The "Клонировать" popover (see ClonePopover.h).
#include "ClonePopover.h"

#include <QBoxLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSpinBox>

#include <algorithm>

ClonePopover::ClonePopover(QWidget* parent) : QFrame(parent, Qt::Popup) {
    setObjectName("clonePopover");
    setAttribute(Qt::WA_DeleteOnClose);
    setFrameShape(QFrame::StyledPanel);
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(12, 10, 12, 10);
    auto* title = new QLabel("Клонировать");
    title->setObjectName("headerName");
    col->addWidget(title);
    auto* countRow = new QHBoxLayout;
    countRow->addWidget(new QLabel("Сколько копий"));
    count_ = new QSpinBox;
    count_->setObjectName("cloneCount");
    count_->setRange(1, 1000);
    count_->setValue(1);
    count_->setToolTip("Каждая следующая копия делает тот же шаг: сдвиг d → d, 2d, 3d…");
    countRow->addWidget(count_);
    col->addLayout(countRow);
    const char* names[] = {"Копия", "Экземпляр", "Массив"};
    const char* ids[] = {"cloneCopy", "cloneInstance", "cloneArray"};
    const char* tips[] = {"Независимые объекты: каждый правится сам по себе",
                          "Общие форма и компоненты: правка одного меняет все (⧉ в списке)",
                          "Один объект «Массив»: число копий и шаг меняются одним числом"};
    for (int k = 0; k < 3; ++k) {
        kinds_[k] = new QRadioButton(names[k]);
        kinds_[k]->setObjectName(ids[k]);
        kinds_[k]->setToolTip(tips[k]);
        col->addWidget(kinds_[k]);
    }
    kinds_[0]->setChecked(true);
    auto* buttons = new QHBoxLayout;
    auto* ok = new QPushButton("OK");
    ok->setObjectName("cloneOk");
    ok->setDefault(true);
    auto* cancel = new QPushButton("Отмена");
    cancel->setObjectName("cloneCancel");
    connect(ok, &QPushButton::clicked, this, &ClonePopover::accept);
    connect(cancel, &QPushButton::clicked, this, &QWidget::close);
    buttons->addStretch(1);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    col->addLayout(buttons);
}

void ClonePopover::allowKinds(bool instance, bool array) {
    const bool allowed[] = {true, instance, array};
    for (int k = 1; k < 3; ++k) {
        kinds_[k]->setEnabled(allowed[k]);
        if (!allowed[k]) kinds_[k]->setToolTip("Масштаб повторяют только независимые копии: у экземпляров и массива размер общий");
    }
}

void ClonePopover::popup(const QPoint& globalPos) {
    adjustSize();
    QPoint at = globalPos + QPoint(12, 12);
    if (const QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect r = screen->availableGeometry();
        at.setX(std::clamp(at.x(), r.left(), std::max(r.left(), r.right() - width())));
        at.setY(std::clamp(at.y(), r.top(), std::max(r.top(), r.bottom() - height())));
    }
    move(at);
    show();
    count_->setFocus();
    count_->selectAll(); // a number typed right away replaces the 1
}

int ClonePopover::copies() const { return count_->value(); }

int ClonePopover::kind() const {
    for (int k = 0; k < 3; ++k)
        if (kinds_[k]->isChecked()) return k;
    return 0;
}

void ClonePopover::accept() {
    count_->interpretText(); // a number typed but not yet confirmed counts
    answered_ = true;
    emit accepted(copies(), kind());
    close();
}

void ClonePopover::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) return accept();
    if (e->key() == Qt::Key_Escape) return (void)close();
    QFrame::keyPressEvent(e);
}

void ClonePopover::hideEvent(QHideEvent* e) {
    if (!answered_) {
        answered_ = true;
        emit cancelled();
    }
    QFrame::hideEvent(e);
}
