// The inspector's building blocks (see InspectorWidgets.h).
#include "InspectorWidgets.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

QDoubleSpinBox* makeSpin(double min, double max, double step, int decimals, const QString& suffix) {
    auto* s = new QDoubleSpinBox;
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    s->setKeyboardTracking(false); // typing "1.5" is one edit, not three
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setMinimumWidth(52);
    if (!suffix.isEmpty()) s->setSuffix(" " + suffix);
    return s;
}

QLabel* sectionTitle(const QString& text) {
    auto* l = new QLabel(text);
    QFont f = l->font();
    f.setCapitalization(QFont::SmallCaps);
    f.setLetterSpacing(QFont::PercentageSpacing, 108);
    f.setBold(true);
    l->setFont(f);
    l->setObjectName("sectionTitle");
    return l;
}

Vec3Row::Vec3Row(double min, double max, double step, int decimals, QWidget* parent) : QWidget(parent) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    const char* axes[] = {"x", "y", "z"};
    const char* tints[] = {"#e8706a", "#7ccf6e", "#6aa6f0"}; // the axis colours of 3D packages
    for (int k = 0; k < 3; ++k) {
        spins_[k] = makeSpin(min, max, step, decimals);
        spins_[k]->setPrefix(QString(axes[k]) + "  ");
        spins_[k]->setStyleSheet(QString("QDoubleSpinBox { border-left: 3px solid %1; }").arg(tints[k]));
        connect(spins_[k], &QDoubleSpinBox::valueChanged, this, [this] { emit edited(); });
        row->addWidget(spins_[k], 1);
    }
}

void Vec3Row::setValue(const rf::Vector3& v) {
    for (int k = 0; k < 3; ++k) {
        const QSignalBlocker quiet(spins_[k]);
        spins_[k]->setValue(v[k]);
    }
}

rf::Vector3 Vec3Row::value() const {
    return rf::Vector3(float(spins_[0]->value()), float(spins_[1]->value()), float(spins_[2]->value()));
}

CollapsibleSection::CollapsibleSection(const QString& title, const QIcon& icon, QWidget* parent) : QWidget(parent) {
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    header_ = new QPushButton(icon, title);
    header_->setIconSize(QSize(20, 20));
    header_->setFlat(true);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setObjectName("sectionHeader");
    connect(header_, &QPushButton::clicked, this, [this] { setOpen(!open_); });
    col->addWidget(header_);
    bodyWidget_ = new QWidget;
    bodyLayout_ = new QVBoxLayout(bodyWidget_);
    bodyLayout_->setContentsMargins(26, 2, 2, 6);
    bodyLayout_->setSpacing(6);
    col->addWidget(bodyWidget_);
    setOpen(false);
}

void CollapsibleSection::setOpen(bool open) {
    open_ = open;
    bodyWidget_->setVisible(open);
    const QString t = header_->text();
    const QString bare = t.startsWith(QChar(0x25B8)) || t.startsWith(QChar(0x25BE)) ? t.mid(2) : t;
    header_->setText(QString(open ? QChar(0x25BE) : QChar(0x25B8)) + " " + bare); // ▾ open, ▸ closed
}

ComponentCard::ComponentCard(const QString& title, const QIcon& icon, QWidget* parent) : QFrame(parent), title_(title) {
    setObjectName("componentCard");
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(6, 4, 6, 6);
    col->setSpacing(4);
    auto* top = new QHBoxLayout;
    top->setSpacing(2);
    header_ = new QPushButton(icon, title);
    header_->setIconSize(QSize(22, 22));
    header_->setFlat(true);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setObjectName("sectionHeader");
    connect(header_, &QPushButton::clicked, this, [this] { setOpen(!open_); });
    remove_ = new QToolButton;
    remove_->setText(QString(QChar(0x2715))); // ✕
    remove_->setObjectName("cardRemove");
    remove_->setToolTip("Убрать компонент «" + title + "»");
    connect(remove_, &QToolButton::clicked, this, &ComponentCard::removeClicked);
    top->addWidget(header_, 1);
    top->addWidget(remove_);
    col->addLayout(top);
    bodyWidget_ = new QWidget;
    bodyLayout_ = new QVBoxLayout(bodyWidget_);
    bodyLayout_->setContentsMargins(8, 2, 2, 2);
    bodyLayout_->setSpacing(6);
    col->addWidget(bodyWidget_);
    setOpen(true);
}

void ComponentCard::setOpen(bool open) {
    open_ = open;
    bodyWidget_->setVisible(open);
    header_->setText(QString(open ? QChar(0x25BE) : QChar(0x25B8)) + " " + title_); // ▾ open, ▸ closed
}

InlineBanner::InlineBanner(QWidget* parent) : QFrame(parent) {
    setObjectName("inlineBanner");
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 6, 6, 6);
    row->setSpacing(8);
    text_ = new QLabel;
    text_->setWordWrap(true);
    button_ = new QPushButton;
    button_->setObjectName("bannerButton");
    connect(button_, &QPushButton::clicked, this, [this] {
        if (fix_) fix_();
    });
    row->addWidget(text_, 1);
    row->addWidget(button_);
    setVisible(false);
}

void InlineBanner::showMessage(const QString& text, const QString& buttonText, std::function<void()> fix) {
    text_->setText(text);
    button_->setText(buttonText);
    button_->setVisible(!buttonText.isEmpty() && fix);
    fix_ = std::move(fix);
    setVisible(true);
}

QString InlineBanner::message() const { return isHidden() ? QString() : text_->text(); }

void InlineBanner::clearMessage() {
    fix_ = nullptr;
    setVisible(false);
}
