#include "Icons.h"
#include "MainWindow.h"
#include "SelfTest.h"

#include "core/Probe.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QPalette>
#include <QStyleFactory>
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

static void applyDarkTheme(QApplication& app) {
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
    app.setStyleSheet(
        "QGroupBox { border: 1px solid #3a3f48; border-radius: 6px; margin-top: 14px; padding-top: 6px; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #8fc1ff; }"
        "QDockWidget::title { background: #2c3038; padding: 5px; font-weight: 600; }"
        "QToolBar { spacing: 6px; padding: 4px; border: none; }"
        "QToolButton { padding: 4px 8px; border-radius: 4px; }"
        "QToolButton:hover { background: #3a404b; }"
        "QToolButton:checked { background: #2d5c99; }"
        "QTableWidget { gridline-color: #333842; }"
        "QHeaderView::section { background: #2c3038; padding: 4px; border: none; }"
        // The scene builder and the big toolbar: airy, 8 px rhythm, readable contrast.
        "QToolBar#createBar { spacing: 4px; padding: 6px 8px; background: #2a2e36; border-bottom: 1px solid #3a3f48; }"
        "QToolBar#createBar QToolButton { padding: 4px 10px; border-radius: 8px; min-width: 64px; }"
        "QToolBar#createBar QToolButton:hover { background: #394150; }"
        "QToolBar#createBar QToolButton:pressed { background: #2d5c99; }"
        "QToolBar#createBar QToolButton#compactButton { min-width: 0; padding: 6px 12px; margin-left: 4px; border: 1px solid #3a3f48; }"
        "QToolBar#createBar QToolButton#compactButton:checked { background: #24466f; border: 1px solid #4096ff; }"
        "QToolBar#createBar QToolButton#compactButton::menu-indicator { image: none; }"
        "QWidget#builderPanel QLabel#sectionTitle { color: #8fc1ff; margin-top: 6px; }"
        "QWidget#builderPanel QLabel#headerName { font-size: 15px; font-weight: 600; }"
        "QWidget#builderPanel QLabel#emptyHint { color: #8b93a2; padding: 24px; }"
        "QWidget#builderPanel QLabel#roleRowLabel { color: #8b93a2; }"
        "QWidget#builderPanel QToolButton#roleButton { padding: 4px 2px; border: 1px solid #3a3f48; border-radius: 8px; background: #2c3038; }"
        "QWidget#builderPanel QToolButton#roleButton:hover { background: #363c47; }"
        "QWidget#builderPanel QToolButton#roleButton:checked { background: #24466f; border: 1px solid #4096ff; }"
        "QWidget#builderPanel QPushButton#sectionHeader { border: none; text-align: left; padding: 5px 4px; font-weight: 600; }"
        "QWidget#builderPanel QPushButton#sectionHeader:hover { background: #333842; border-radius: 4px; }"
        "QWidget#builderPanel QTreeWidget { border: 1px solid #3a3f48; border-radius: 6px; padding: 2px; }"
        "QWidget#builderPanel QTreeWidget::item { padding: 3px 0; }");
}

// Software rendering for PCs without a usable GPU / OpenGL driver: Qt's Mesa llvmpipe
// (opengl32sw.dll, an OpenGL 3.0 context) runs the same shaders on the CPU. Chosen by --software-gl or the
// environment variable RF_SOFTWARE_GL=1; it must be decided before the application object exists.
static bool wantsSoftwareGL(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--software-gl") return true;
    return qEnvironmentVariableIntValue("RF_SOFTWARE_GL") != 0;
}

int main(int argc, char** argv) {
    const bool software = wantsSoftwareGL(argc, argv);
    if (software) QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(software ? 0 : 4); // multisampling is costly on the CPU
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("PhysRealFlow");
    QApplication::setOrganizationName("PhysRealFlow");
    applyDarkTheme(app);

    QCommandLineParser cli;
    cli.setApplicationDescription("PhysRealFlow");
    cli.addHelpOption();
    QCommandLineOption presetOpt("preset", "Scene preset index to load.", "index");
    QCommandLineOption framesOpt("frames", "Frames to simulate before the screenshot.", "n", "120");
    QCommandLineOption shotOpt("screenshot", "Save a PNG of the viewport and quit.", "file");
    QCommandLineOption csvOpt("csv", "Save the plotted time series as CSV and quit.", "file");
    QCommandLineOption sizeOpt("size", "Window size WxH.", "size", "1600x950");
    QCommandLineOption softOpt("software-gl", "Render on the CPU (Mesa llvmpipe) - for PCs without a usable GPU.");
    QCommandLineOption iconsOpt("dump-icons", "Write every icon of the editor as a PNG into the directory and quit.", "dir");
    QCommandLineOption windowOpt("window", "The screenshot shows the whole window, panels too.");
    QCommandLineOption sceneOpt("scene", "Open a scene file (*.rfscene) in the scene builder.", "file");
    QCommandLineOption selfTestOpt("self-test", "Press the scene builder's buttons, check the results, quit (exit code = failures).");
    QCommandLineOption shotsOpt("shots", "With --self-test: also save screenshots (gizmo, edit and play) into the directory.", "dir");
    QCommandLineOption gizmoShotsOpt("gizmo-shots", "Save screenshots of the gizmo (move, rotate, scale) into the directory and quit.", "dir");
    QCommandLineOption editOpt("edit", "The builder's screenshot in edit mode: the scene as authored, nothing simulated.");
    cli.addOptions({presetOpt, framesOpt, shotOpt, csvOpt, sizeOpt, softOpt, iconsOpt, windowOpt, sceneOpt, selfTestOpt, shotsOpt,
                    gizmoShotsOpt, editOpt});
    cli.process(app);
    if (cli.isSet(iconsOpt)) return dumpIcons(cli.value(iconsOpt)) > 0 ? 0 : 1;

    MainWindow w;
    QStringList wh = cli.value(sizeOpt).split('x');
    w.resize(wh.value(0).toInt() > 0 ? wh.value(0).toInt() : 1600, wh.value(1).toInt() > 0 ? wh.value(1).toInt() : 950);
    w.show();
    if (cli.isSet(selfTestOpt)) return runBuilderSelfTest(w, cli.value(shotsOpt));
    if (cli.isSet(gizmoShotsOpt)) return runGizmoShots(w, cli.value(gizmoShotsOpt));
    // Without --preset the editor opens on the scene builder: a floor, ready for the first shape.
    const int preset = cli.isSet(presetOpt) ? cli.value(presetOpt).toInt() : -1;
    w.setScreenshotWholeWindow(cli.isSet(windowOpt));
    w.setEditScreenshot(cli.isSet(editOpt));
    if (cli.isSet(sceneOpt)) w.setStartScene(cli.value(sceneOpt));
    if (cli.isSet(shotOpt) || cli.isSet(csvOpt))
        w.runAutomation(preset, cli.value(framesOpt).toInt(), cli.value(shotOpt), cli.value(csvOpt));
    else if (preset >= 0)
        w.runAutomation(preset, 0, {}, {});
    else
        w.startBuilder();
    return app.exec();
}
