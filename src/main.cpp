// The editor's entry point: the dark palette and the style sheet of the whole window, the software
// OpenGL switch (it has to be chosen before the application exists), the command line (automation,
// tests, pictures), and the counting operator new behind the probe's "allocations per frame".
#include "Icons.h"
#include "MainWindow.h"
#include "SelfTest.h"

#include "core/Probe.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QPalette>
#include <QScreen>
#include <QStyleFactory>
#include <QSettings>
#include <QSurfaceFormat>

#include <cstdlib>
#include <new>

// Counting allocations for the probe's "memory/allocations per frame": the SDK never touches the
// allocator, so the program replaces the global operator new (every variant the standard pairs
// together) and forwards to malloc. Aligned variants keep the library's own, self-consistent pair.
void* operator new(std::size_t n) {
    rf::Probe::allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    rf::Probe::allocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return operator new(n, t); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

static void applyDarkPalette(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor bg(37, 40, 46), base(28, 30, 35), text(220, 223, 228), accent(64, 150, 255);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, bg);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(48, 52, 60));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::PlaceholderText, QColor(130, 135, 145));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 124, 130));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 124, 130));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 124, 130));
    app.setPalette(p);
}

// The frame of the window: group boxes, docks, the big toolbar, the inspector's tabs, the gallery.
static QString windowStyle() {
    return "QGroupBox { border: 1px solid #3a3f48; border-radius: 6px; margin-top: 14px; padding-top: 6px; font-weight: 600; }"
           "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #8fc1ff; }"
           "QDockWidget::title { background: #2c3038; padding: 5px; font-weight: 600; }"
           "QToolBar { spacing: 6px; padding: 4px; border: none; }"
           "QToolButton { padding: 4px 8px; border-radius: 4px; }"
           "QToolButton:hover { background: #3a404b; }"
           "QToolButton:checked { background: #2d5c99; }"
           "QTableWidget { gridline-color: #333842; }"
           "QHeaderView::section { background: #2c3038; padding: 4px; border: none; }"
           // The big toolbar: airy, 8 px rhythm, readable contrast.
           "QToolBar#createBar { spacing: 4px; padding: 6px 8px; background: #2a2e36; border-bottom: 1px solid #3a3f48; }"
           "QToolBar#createBar QToolButton { padding: 4px 10px; border-radius: 8px; min-width: 64px; }"
           "QToolBar#createBar QToolButton:hover { background: #394150; }"
           "QToolBar#createBar QToolButton:pressed { background: #2d5c99; }"
           "QToolBar#createBar QToolButton#lightsButton::menu-indicator { image: none; width: 0; }"
           "QToolBar#createBar QToolButton#compactButton { min-width: 0; padding: 6px 12px; margin-left: 4px; border: 1px solid #3a3f48; }"
           "QToolBar#createBar QToolButton#compactButton:checked { background: #24466f; border: 1px solid #4096ff; }"
           "QToolBar#createBar QToolButton#compactButton::menu-indicator { image: none; }"
           "QTabWidget#inspectorTabs::pane { border: none; border-top: 1px solid #3a3f48; }"
           "QTabWidget#inspectorTabs QTabBar::tab { padding: 6px 18px; margin-right: 2px; border-top-left-radius: 6px;"
           " border-top-right-radius: 6px; background: #2c3038; color: #aab1bd; }"
           "QTabWidget#inspectorTabs QTabBar::tab:selected { background: #24466f; color: white; }"
           "QListWidget#galleryList { background: #1f2227; border: none; }"
           "QListWidget#galleryList::item { color: #dfe3e8; padding: 4px; border-radius: 8px; }"
           "QListWidget#galleryList::item:hover { background: #2f3540; }"
           "QLabel#galleryHint { color: #8b93a2; padding: 4px 2px 8px 2px; }";
}

// The scene builder's panels: the role tiles, the component cards, the collider buttons, the banner.
static QString builderStyle() {
    return "QWidget#builderPanel QLabel#sectionTitle { color: #8fc1ff; margin-top: 6px; }"
           "QWidget#builderPanel QLabel#headerName { font-size: 15px; font-weight: 600; }"
           "QWidget#builderPanel QLabel#emptyHint { color: #8b93a2; padding: 24px; }"
           "QWidget#builderPanel QLabel#roleRowLabel { color: #8b93a2; }"
           "QWidget#builderPanel QToolButton#roleButton { padding: 4px 2px; border: 1px solid #3a3f48; border-radius: 8px; background: #2c3038; }"
           "QWidget#builderPanel QToolButton#roleButton:hover { background: #363c47; }"
           "QWidget#builderPanel QToolButton#roleButton:checked { background: #24466f; border: 1px solid #4096ff; }"
           "QWidget#builderPanel QPushButton#sectionHeader { border: none; text-align: left; padding: 5px 4px; font-weight: 600; }"
           "QWidget#builderPanel QPushButton#sectionHeader:hover { background: #333842; border-radius: 4px; }"
           "QWidget#builderPanel QTreeWidget { border: 1px solid #3a3f48; border-radius: 6px; padding: 2px; }"
           "QWidget#builderPanel QTreeWidget::item { padding: 3px 0; }"
           "QWidget#builderPanel QFrame#componentCard { border: 1px solid #3a3f48; border-radius: 8px; background: #2a2e35; }"
           "QWidget#builderPanel QToolButton#cardRemove { color: #9aa3b0; padding: 2px 6px; }"
           "QWidget#builderPanel QToolButton#cardRemove:hover { color: white; background: #7a2f35; }"
           "QWidget#builderPanel QPushButton#addComponent { padding: 9px; border: 1px dashed #4a5260; border-radius: 8px;"
           " background: #262a31; font-weight: 600; }"
           "QWidget#builderPanel QPushButton#addComponent:hover { background: #303642; border-color: #4096ff; }"
           "QWidget#builderPanel QToolButton#colliderButton { padding: 3px; border: 1px solid #3a3f48; border-radius: 6px; background: #22262c; }"
           "QWidget#builderPanel QToolButton#colliderButton:checked { background: #1f4a33; border: 1px solid #7cff9a; }"
           "QWidget#builderPanel QLabel#colliderKindName { color: #7cff9a; }"
           "QFrame#playBanner { background: rgba(58, 40, 12, 225); border: 1px solid #f0a020; border-radius: 8px; }"
           "QFrame#playBanner QLabel { color: #ffd58a; font-weight: 600; }"
           "QFrame#playBanner QPushButton#keepButton { background: #f0a020; color: #1c1810; border: none; border-radius: 6px;"
           " padding: 5px 10px; font-weight: 600; }"
           "QFrame#playBanner QPushButton#keepButton:hover { background: #ffbe4a; }"
           "QFrame#bigControls { background: rgba(18, 20, 24, 170); border: 1px solid rgba(255, 255, 255, 40); border-radius: 24px; }"
           "QFrame#bigControls QToolButton#bigControl { background: transparent; border: none; border-radius: 20px; padding: 4px; }"
           "QFrame#bigControls QToolButton#bigControl:hover { background: rgba(255, 255, 255, 45); }"
           "QFrame#cameraPicker { background: rgba(18, 20, 24, 190); border: 1px solid rgba(64, 150, 255, 150); border-radius: 8px; }"
           "QFrame#cameraPicker QLabel { color: #b8c4d0; }"
           "QFrame#cameraPicker QComboBox { min-width: 150px; font-weight: 600; }"
           "QFrame#commandSearch { background: #22262c; border: 1px solid #4096ff; border-radius: 10px; }"
           "QFrame#commandSearch QLineEdit { padding: 8px; font-size: 11pt; border-radius: 6px; }"
           "QFrame#commandSearch QListWidget#commandList { border: none; background: #1d2025; }"
           "QFrame#commandSearch QListWidget#commandList::item { padding: 5px 6px; }"
           "QFrame#commandSearch QListWidget#commandList::item:selected { background: #24466f; }"
           "QFrame#commandSearch QLabel#commandTip { color: #8a93a3; }"
           "QFrame#inlineBanner { background: #4a3b16; border: 1px solid #c99a2e; border-radius: 8px; }"
           "QFrame#inlineBanner QLabel { color: #ffe3a3; }"
           "QFrame#inlineBanner QPushButton#bannerButton { background: #c99a2e; color: #1c1810; border: none; border-radius: 6px;"
           " padding: 5px 10px; font-weight: 600; }"
           "QFrame#inlineBanner QPushButton#bannerButton:hover { background: #e2b448; }";
}

// The Laboratory (LabPanel, LabCard, LabTimeline): the same dark ground, one accent blue, quiet captions.
static QString labStyle() {
    return "QWidget#labPanel QLabel#labIntro { color: #8b93a2; }"
           "QWidget#labPanel QLabel#labSection { color: #8fc1ff; font-weight: 600; margin-top: 4px; }"
           "QWidget#labPanel QLabel#labGroup { color: #aab1bd; font-weight: 600; margin-top: 8px; padding-left: 2px; }"
           "QWidget#labPanel QPushButton#labPreset { padding: 6px 8px; border: 1px solid #3a3f48; border-radius: 8px; background: #2c3038; }"
           "QWidget#labPanel QPushButton#labPreset:hover { background: #363c47; }"
           "QWidget#labPanel QPushButton#labPreset:checked { background: #24466f; border: 1px solid #4096ff; }"
           "QWidget#labPanel QCheckBox#labLayer { padding: 2px 0; }"
           "QScrollArea#labLayerScroll { background: transparent; }"
           "QWidget#labLayers { background: transparent; }"
           "QFrame#labCard { border: 1px solid #3a3f48; border-radius: 8px; background: #2a2e35; }"
           "QFrame#labCard QLabel#labCardTitle { font-size: 14px; font-weight: 600; }"
           "QFrame#labCard QLabel#labCardSubtitle { color: #8b93a2; }"
           "QFrame#labCard QLabel#labCardName { color: #aab1bd; }"
           "QFrame#labCard QLabel#labCardValue { font-family: Consolas, 'Cascadia Mono', monospace; }"
           "QFrame#labCard QPushButton#labCardButton { padding: 6px 10px; border: 1px solid #4096ff; border-radius: 6px; background: #24466f; }"
           "QFrame#labCard QPushButton#labCardButton:hover { background: #2d5c99; }"
           "QFrame#labTimeline { background: rgba(18, 20, 24, 200); border: 1px solid rgba(255, 255, 255, 40); border-radius: 10px; }"
           "QFrame#labTimeline QLabel#labTimelineText { color: #b8c4d0; }"
           "QFrame#labTimeline QToolButton#labStep { padding: 1px 7px; border-radius: 6px; font-size: 14px; color: #cfd8e3; }"
           "QFrame#labTimeline QPushButton#labLive { padding: 4px 10px; border-radius: 6px; border: 1px solid #3a3f48; }"
           "QFrame#labTimeline QPushButton#labLive[past=\"true\"] { background: #1f4a33; border: 1px solid #7cff9a; color: #d8ffe2; font-weight: 600; }";
}

// Software rendering for PCs without a usable GPU / OpenGL driver: Qt's Mesa llvmpipe
// (opengl32sw.dll, an OpenGL 3.0 context) runs the same shaders on the CPU. Chosen by --software-gl or the
// environment variable RF_SOFTWARE_GL=1; it must be decided before the application object exists.
static bool wantsSoftwareGL(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--software-gl") return true;
    if (qEnvironmentVariableIntValue("RF_SOFTWARE_GL") != 0) return true;
    // Chosen once after a white window (MainWindow::checkBlankView) or in Вид → Рендер на процессоре.
    return QSettings("PhysRealFlow", "PhysRealFlow").value("render/softwareGL", false).toBool();
}

static void setSurfaceFormat(bool software) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(software ? 0 : 4); // multisampling is costly on the CPU
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);
}

// The command line: automation (a scene, frames, a screenshot, a CSV), the tests, the pictures.
struct Options {
    QCommandLineOption preset{"preset", "Scene preset index to load.", "index"};
    QCommandLineOption frames{"frames", "Frames to simulate before the screenshot.", "n", "120"};
    QCommandLineOption shot{"screenshot", "Save a PNG of the viewport and quit.", "file"};
    QCommandLineOption csv{"csv", "Save the plotted time series as CSV and quit.", "file"};
    QCommandLineOption size{"size", "Window size WxH.", "size", "1600x950"};
    QCommandLineOption software{"software-gl", "Render on the CPU (Mesa llvmpipe) - for PCs without a usable GPU."};
    QCommandLineOption icons{"dump-icons", "Write every icon of the editor as a PNG into the directory and quit.", "dir"};
    QCommandLineOption window{"window", "The screenshot shows the whole window, panels too."};
    QCommandLineOption scene{"scene", "Open a scene file (*.rfscene) in the scene builder.", "file"};
    QCommandLineOption selfTest{"self-test", "Press the scene builder's buttons, check the results, quit (exit code = failures)."};
    QCommandLineOption shots{"shots", "With --self-test: also save screenshots (gizmo, edit and play) into the directory.", "dir"};
    QCommandLineOption gizmoShots{"gizmo-shots", "Save screenshots of the gizmo (move, rotate, scale) into the directory and quit.", "dir"};
    QCommandLineOption edit{"edit", "The builder's screenshot in edit mode: the scene as authored, nothing simulated."};
    QCommandLineOption camera{"camera", "The builder's screenshot looks through the scene's active camera (else its first)."};
    QCommandLineOption firstStart{"first-start", "Start as on the very first run: the welcome scene and the first-minute hint."};
    QCommandLineOption thumbnail{"thumbnail", "Only the 3D view, no window on the screen (the gallery's pictures)."};
    QCommandLineOption galleryShot{"gallery-shot", "Make every picture of the examples gallery, save the gallery as a PNG, quit.", "file"};

    void addTo(QCommandLineParser& cli) const {
        cli.addOptions({preset, frames, shot, csv, size, software, icons, window, scene, selfTest, shots, gizmoShots, edit, camera,
                        firstStart, thumbnail, galleryShot});
    }
};

// A run nobody watches - the self-test, the pictures, automation - never shows a window, so that
// nobody takes it for their own editor, and never takes the keyboard from the program in front:
//   - the main window stays on the screen, because Windows paints only what is on a screen (the 3D
//     view draws in its paint, and some checks watch what the paint does), but it is fully
//     transparent, has no taskbar button, is never activated and lets every click through;
//   - any other window of it (a menu, a popup, a tooltip) that would land on a screen is moved off
//     it before it is shown - the Show event comes before the window is put on the screen - or as
//     soon as it moves onto one (a tooltip placed again, a popup that places itself).
class OffscreenGuard : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* object, QEvent* event) override {
        const bool shown = event->type() == QEvent::Show, moved = event->type() == QEvent::Move;
        auto* w = qobject_cast<QWidget*>(object);
        if ((!shown && !moved) || !w || !w->isWindow() || (moved && !w->isVisible())) return false;
        if (w->windowOpacity() == 0.0) return false; // the invisible main window
        for (const QScreen* screen : QGuiApplication::screens())
            if (screen->geometry().intersects(w->frameGeometry())) {
                w->move(w->pos() + QPoint(-20000, -20000));
                break;
            }
        return false;
    }
};

static bool unattended(const QCommandLineParser& cli, const Options& o) {
    for (const QCommandLineOption* option : {&o.selfTest, &o.gizmoShots, &o.shot, &o.csv, &o.galleryShot, &o.thumbnail})
        if (cli.isSet(*option)) return true;
    return false;
}

// What the window does after it is shown: a test, pictures, automation, or the builder for a person.
static int run(QApplication& app, MainWindow& w, const QCommandLineParser& cli, const Options& o) {
    if (cli.isSet(o.selfTest)) return runBuilderSelfTest(w, cli.value(o.shots));
    if (cli.isSet(o.gizmoShots)) return runGizmoShots(w, cli.value(o.gizmoShots));
    const int preset = cli.isSet(o.preset) ? cli.value(o.preset).toInt() : -1;
    w.setScreenshotWholeWindow(cli.isSet(o.window));
    w.setEditScreenshot(cli.isSet(o.edit));
    w.setScreenshotThroughCamera(cli.isSet(o.camera));
    w.setForceFirstStart(cli.isSet(o.firstStart));
    if (cli.isSet(o.scene)) w.setStartScene(cli.value(o.scene));
    if (cli.isSet(o.galleryShot)) {
        w.startBuilder(false);
        w.runGalleryShot(cli.value(o.galleryShot));
    } else if (cli.isSet(o.shot) || cli.isSet(o.csv)) {
        w.runAutomation(preset, cli.value(o.frames).toInt(), cli.value(o.shot), cli.value(o.csv));
    } else if (preset >= 0) {
        w.runAutomation(preset, 0, {}, {});
    } else {
        w.startBuilder(true); // a person: the first start opens the welcome scene
    }
    return app.exec();
}

int main(int argc, char** argv) {
    const bool software = wantsSoftwareGL(argc, argv);
    if (software) QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    setSurfaceFormat(software);

    QApplication app(argc, argv);
    QApplication::setApplicationName("PhysRealFlow");
    QApplication::setOrganizationName("PhysRealFlow");
    applyDarkPalette(app);
    app.setStyleSheet(windowStyle() + builderStyle() + labStyle());

    QCommandLineParser cli;
    cli.setApplicationDescription("PhysRealFlow");
    cli.addHelpOption();
    const Options o;
    o.addTo(cli);
    cli.process(app);
    if (cli.isSet(o.icons)) return dumpIcons(cli.value(o.icons)) > 0 ? 0 : 1;

    MainWindow w;
    const QStringList wh = cli.value(o.size).split('x');
    w.resize(wh.value(0).toInt() > 0 ? wh.value(0).toInt() : 1600, wh.value(1).toInt() > 0 ? wh.value(1).toInt() : 950);
    if (cli.isSet(o.thumbnail)) w.setThumbnailMode();
    if (unattended(cli, o)) {
        w.setWindowFlags(w.windowFlags() | Qt::Tool | Qt::WindowTransparentForInput);
        w.setAttribute(Qt::WA_ShowWithoutActivating);
        w.setWindowOpacity(0.0);
        app.installEventFilter(new OffscreenGuard(&app));
        for (Qt::UIEffect e : {Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip, Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo})
            QApplication::setEffectEnabled(e, false); // a rolling tooltip is a window of its own, placed by Qt
    }
    w.show();
    return run(app, w, cli, o);
}
