// The gallery of examples (see ExamplesGallery.h): tiles, the kind filter, and the pictures made in
// the background by the editor's own program.
#include "ExamplesGallery.h"

#include "samples/Samples.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTabBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QSize kThumb(240, 150);
const char* kEditorCategory = "Сцены редактора";

// The names of the editor's own scenes; another file is shown by its file name.
QString sceneTitle(const QString& baseName) {
    if (baseName == "welcome") return "Горка и башня";
    if (baseName == "cradle-and-cubes") return "Колыбель и кубики";
    if (baseName == "magnets") return "Магниты";
    if (baseName == "midas") return "Касание Мидаса";
    return baseName;
}

QIcon placeholder(const QString& title) {
    QPixmap pm(kThumb);
    pm.fill(QColor(44, 48, 56));
    QPainter p(&pm);
    p.setPen(QColor(120, 128, 140));
    p.drawText(pm.rect().adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, title + "\n\nкартинка готовится…");
    return QIcon(pm);
}

} // namespace

ExamplesGallery::ExamplesGallery(const QString& examplesDir, QWidget* parent) : QDialog(parent) {
    setWindowTitle("Примеры");
    resize(1100, 720);
    auto* col = new QVBoxLayout(this);
    auto* hint = new QLabel("Нажмите на картинку — сцена откроется. Сцены редактора можно менять и сохранять.");
    hint->setObjectName("galleryHint");
    col->addWidget(hint);
    tabs_ = new QTabBar;
    tabs_->setObjectName("galleryTabs");
    col->addWidget(tabs_);
    list_ = new QListWidget;
    list_->setObjectName("galleryList");
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(kThumb);
    list_->setGridSize(QSize(kThumb.width() + 24, kThumb.height() + 44));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setUniformItemSizes(true);
    col->addWidget(list_, 1);
    collect(examplesDir);
    buildTiles();
    connect(tabs_, &QTabBar::currentChanged, this, &ExamplesGallery::filter);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        chosen_ = item->data(Qt::UserRole).toInt();
        accept();
    });
    watchdog_ = new QTimer(this);
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, [this] { finishRunning(false); });
    startNext();
}

ExamplesGallery::~ExamplesGallery() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(2000);
    }
}

QString ExamplesGallery::thumbnailDir() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
}

// The editor's scenes first (the welcome scene at the very front), then the SDK's by category.
void ExamplesGallery::collect(const QString& examplesDir) {
    QStringList files = QDir(examplesDir).entryList({"*.rfscene"}, QDir::Files, QDir::Name);
    files.sort();
    if (files.removeOne("welcome.rfscene")) files.prepend("welcome.rfscene");
    categories_ << "Все" << kEditorCategory;
    for (const QString& f : files)
        examples_.push_back({sceneTitle(QFileInfo(f).completeBaseName()), kEditorCategory, examplesDir + "/" + f, -1});
    for (const rf::SampleEntry& s : rf::samples()) {
        const QString category = QString::fromUtf8(s.category);
        if (!categories_.contains(category)) categories_ << category;
        examples_.push_back({QString::fromUtf8(s.name), category, QString(), int(s.id)});
    }
}

void ExamplesGallery::buildTiles() {
    for (const QString& c : categories_) tabs_->addTab(c);
    for (size_t i = 0; i < examples_.size(); ++i) {
        const Example& e = examples_[i];
        const QString picture = thumbnailPath(e);
        auto* item = new QListWidgetItem(QFileInfo::exists(picture) ? QIcon(picture) : placeholder(e.title), e.title, list_);
        item->setData(Qt::UserRole, int(i));
        item->setToolTip(e.category + (e.sceneFile.isEmpty() ? " — готовая сцена SDK" : " — можно менять и сохранять"));
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        items_.push_back(item);
        if (!QFileInfo::exists(picture)) queue_.push_back(int(i));
    }
}

void ExamplesGallery::filter(int tab) {
    const QString category = tab > 0 ? categories_.value(tab) : QString();
    for (size_t i = 0; i < items_.size(); ++i) items_[i]->setHidden(!category.isEmpty() && examples_[i].category != category);
}

// One file per example; "v1" changes when the pictures are made differently.
QString ExamplesGallery::thumbnailPath(const Example& e) const {
    if (e.sceneFile.isEmpty()) return thumbnailDir() + QString("/sample-%1-v1.png").arg(e.preset);
    const QFileInfo info(e.sceneFile);
    return thumbnailDir() + QString("/scene-%1-%2-v1.png").arg(info.completeBaseName()).arg(info.lastModified().toSecsSinceEpoch());
}

// An editor scene as it was authored; a sample after a moment of running (water poured, fire lit).
QStringList ExamplesGallery::thumbnailArguments(const Example& e, const QString& out) const {
    QStringList args{"--thumbnail", "--size", "480x300", "--screenshot", out};
    if (e.sceneFile.isEmpty()) args << "--preset" << QString::number(e.preset) << "--frames" << "60";
    else args << "--scene" << e.sceneFile << "--edit";
    return args;
}

void ExamplesGallery::startNext() {
    if (running_ >= 0) return;
    if (queue_.empty()) {
        emit thumbnailsDone();
        return;
    }
    running_ = queue_.front();
    queue_.pop_front();
    QDir().mkpath(thumbnailDir());
    process_ = new QProcess(this);
#ifdef Q_OS_WIN
    process_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= 0x00004000; // BELOW_NORMAL_PRIORITY_CLASS: the editor in front stays smooth
    });
#endif
    connect(process_, &QProcess::finished, this, [this](int code) { finishRunning(code == 0); });
    process_->start(QCoreApplication::applicationFilePath(), thumbnailArguments(examples_[size_t(running_)],
                                                                                thumbnailPath(examples_[size_t(running_)])));
    watchdog_->start(90000); // a scene that takes longer keeps its placeholder
}

void ExamplesGallery::finishRunning(bool saved) {
    if (running_ < 0) return;
    watchdog_->stop();
    const QString picture = thumbnailPath(examples_[size_t(running_)]);
    if (saved && QFileInfo::exists(picture)) items_[size_t(running_)]->setIcon(QIcon(picture));
    if (process_) {
        process_->disconnect(this);
        if (process_->state() != QProcess::NotRunning) process_->kill();
        process_->deleteLater();
        process_ = nullptr;
    }
    running_ = -1;
    startNext();
}
