#pragma once
// The editor's main window: it owns the simulation thread (SimController), the 3D view (Viewport)
// and the scene builder, and wires them to the toolbars, menus and docks. What each part of the
// window does is written next to its functions below; the .cpp files are split by topic.

#include "ControlScheme.h"
#include "Gizmo.h"
#include "SimController.h"

#include <QMainWindow>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ParamForm;
class PlotPanel;
class SceneBuilder;
class Viewport;
class QAction;
class QComboBox;
class QGroupBox;
class QLabel;
class QTableWidget;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;

    // Automation: load a preset (-1: the scene builder's new scene), simulate `frames` frames, save
    // a screenshot and/or CSV, quit.
    void runAutomation(int preset, int frames, const QString& screenshotPath, const QString& csvPath);
    // The builder's first scene (MainWindowStart.cpp): the --scene file; on the very first start of
    // an interactive session the welcome scene with the first-minute bubble; else an empty floor.
    void startBuilder(bool interactive = false);
    // The first-minute bubble even if it was seen before (the command line's --first-start).
    void setForceFirstStart(bool on) { forceFirstStart_ = on; }
    QString firstMinuteText() const; // what the bubble says now (empty: gone)
    // "Примеры": the gallery of every ready-made scene; a click opens one.
    void openGallery();
    void runGalleryShot(const QString& file); // the gallery with all its pictures as a PNG, then quit
    // Only the 3D view, no window on the screen: the gallery's pictures (--thumbnail). Before show().
    void setThumbnailMode();
    // The builder starts on this scene file instead of an empty floor (the command line's --scene).
    void setStartScene(const QString& path) { startScene_ = path; }
    // The screenshot of the automation shows the whole window (panels too), not only the 3D view.
    void setScreenshotWholeWindow(bool on) { auto_.wholeWindow = on; }
    // The screenshot of the builder's scene is taken in edit mode (nothing simulated).
    void setEditScreenshot(bool on) { auto_.editOnly = on; }
    // The screenshot looks through the scene's active camera (the first camera when none is).
    void setScreenshotThroughCamera(bool on) { auto_.throughCamera = on; }
    // The right-click menu of the edit mode: the tools, the coordinate system, and what can be done
    // to the selected object. Shares its state with the toolbar and the keys. The caller owns it.
    class QMenu* buildEditContextMenu();
    // A frame that came back all one colour: nothing was drawn (MainWindowPlay.cpp).
    static bool looksBlank(const QImage& frame);
    // On the GPU: a blank view asks whether to restart with the software renderer (`pretend`: ask anyway).
    void checkBlankView(bool pretend = false);

protected:
    void resizeEvent(class QResizeEvent* e) override; // the top bar fits the new width

private:
    // UI construction
    void connectViewport();       // the stir, the mouse joint, the software-OpenGL restart
    void buildActions();
    void buildParameterDock();
    void buildVisualDock();
    void buildResultsDock();
    void buildSceneBuilderDock(); // the "Конструктор": shapes with roles, no code (SceneBuilder)
    void connectSceneBuilder();
    // The big "Создать" bar (MainWindowToolbar.cpp): shapes, lights, camera, run / pause / stop / step,
    // undo / redo, samples; on a narrow window it drops captions and shrinks icons, never cuts words.
    void buildMainToolbar();
    void addLightButtons(class QToolBar* tb); // "Свет ▾" (Солнце, Лампа, Прожектор) and "Камера"
    void addCompactButtons(class QToolBar* tb);
    void setToolbarLevel(int level);
    void fitMainToolbar();
    void buildToolShelf();        // the small bar at the left: select, move, rotate, scale, world / own axes
    void setTool(GizmoMode m);
    void onModeChanged();         // edit / playing / paused: the buttons, the viewport, the status line
    void showEditContextMenu(const QPointF& pos);
    void addObjectActions(class QMenu* menu);
    // Many objects at once (MainWindowControls.cpp): group, array, instance, and the selection
    // helpers - in the context menu and in "Правка"; the popover after a Shift + gizmo drag.
    void addManyActions(class QMenu* menu);
    void addSelectHelpers(class QMenu* menu);
    void showClonePopover(const QPointF& pos);
    QString modeText() const;
    void buildSamplesMenu();      // the SDK's ready-made scenes, by category
    // A beginner sees the builder and the 3D view; "Эксперт" brings back every solver parameter and
    // the visualisation settings, "Графики" the readings and plots at the bottom.
    void buildLayoutActions();
    void setExpertMode(bool on);
    void setGraphsVisible(bool on);
    void buildSceneMenu();
    ParamForm* buildObjectForm();
    ParamForm* buildFluidForm();
    ParamForm* buildGasForm();
    ParamForm* buildBrushForm();
    ParamForm* buildRigidForm();
    // The rigid form, step by step: the body buttons, the soft-body and cloth buttons, the solver rows.
    void addBodyButtons(ParamForm* f);
    void addSoftButtons(ParamForm* f);
    void addRigidSolverRows(ParamForm* f);
    void addJobButton(class QHBoxLayout* row, const QString& text, const QString& tip, std::function<void(rf::Simulation&)> job);
    void addGasExpertRows(ParamForm* f);
    void buildRunActions(); // buildActions, step by step
    void buildMenus();
    // The visualisation dock, one group each.
    void buildFieldView(class QVBoxLayout* col);
    void buildParticleView(class QVBoxLayout* col);
    void buildDisplayView(class QVBoxLayout* col);
    ParamForm* buildSceneForm(const std::vector<rf::SceneParam>& params);
    void rebuildSceneForm(const rf::RenderSnapshot& s); // when another scene is loaded
    QGroupBox* addGroup(class QVBoxLayout* col, const QString& title, ParamForm* form);

    // Runtime
    void poll();
    void onSnapshot(const std::shared_ptr<const rf::RenderSnapshot>& s);
    void updateModeVisibility(rf::SimMode m);
    void updateInfo(const rf::RenderSnapshot& s);
    void updateSensors(const rf::RenderSnapshot& s); // the probe's channels
    void togglePlay();
    void onDisturbance(rf::Vector3 pos, rf::Vector3 vel);

    // File operations
    void importMesh();
    void importModel(); // an OBJ / STL model as a new object of the builder
    void exportCsv();
    void exportSurfaceLoads();
    void screenshot();
    void showHelp();
    void showAbout();
    QImage windowImage(); // the window with the 3D view's overlay, for screenshots

    // The first minute (MainWindowStart.cpp)
    static QString examplesDir();
    void startFirstMinute();
    void showFirstMinuteStep();
    void onFirstMinuteAction();
    void finishFirstMinute();

    // Keys and mouse schemes (MainWindowControls.cpp)
    void buildEditMenu();                   // "Правка": every key as an action
    void buildViewKeys(class QMenu* menu);  // numpad views, gizmo size
    void buildControlsMenu();               // Вид -> Управление
    void buildCamerasMenu();                // Вид -> Камеры: the editor's view or one of the scene's cameras
    // The play mode made unmistakable (MainWindowPlay.cpp): the frame, the banner and big ▶ ⏸ ■ on
    // the view, K to keep the simulation's poses, Ctrl+K to find a command, Вид → Рендер на процессоре.
    void buildPlayExtras();
    void updatePlayOverlay();
    void applyControlScheme(ControlScheme s);
    void askControlScheme();                // once, at the first start
    void appendShortcutTips();
    void connectViewportControls();

    std::unique_ptr<SimController> ctrl_;
    Viewport* view_ = nullptr;
    QTimer* timer_ = nullptr;
    uint64_t lastSerial_ = 0, lastParamsVersion_ = 0, lastFrame_ = 0;
    std::string lastSceneName_;
    qint64 lastInfoUpdate_ = 0;

    class QMenu* samplesMenu_ = nullptr;
    class QActionGroup* samplesGroup_ = nullptr;
    QAction* expertAct_ = nullptr;
    QAction* graphsAct_ = nullptr;
    QAction* collidersAct_ = nullptr; // every object's collider as a green wireframe, not only the selected one's
    class QTabWidget* inspectorTabs_ = nullptr; // "Сцена" | "Объект"
    QAction* playAct_ = nullptr;
    QAction* pauseAct_ = nullptr;
    QAction* stopAct_ = nullptr;
    QAction* stepAct_ = nullptr;
    QAction* localAct_ = nullptr;
    class QActionGroup* toolGroup_ = nullptr;
    std::vector<ParamForm*> forms_;
    QGroupBox *objectBox_ = nullptr, *fluidBox_ = nullptr, *gasBox_ = nullptr, *brushBox_ = nullptr,
              *rigidBox_ = nullptr, *fieldBox_ = nullptr, *particleBox_ = nullptr, *sceneBox_ = nullptr;
    ParamForm* sceneForm_ = nullptr; // the loaded scene's knobs, inside sceneBox_
    QTableWidget* info_ = nullptr;
    QTableWidget* sensors_ = nullptr; // every probe channel, by name
    PlotPanel* plots_ = nullptr;
    SceneBuilder* builder_ = nullptr;
    QString startScene_;
    class QToolBar* createBar_ = nullptr;
    std::vector<class QToolButton*> createButtons_; // shapes, model, lights, camera: first to lose captions
    std::vector<class QToolButton*> runButtons_;    // run / pause / stop / step, undo / redo
    int toolbarLevel_ = 0;                          // how much room the bar gave up (setToolbarLevel)
    class FirstStartHint* hint_ = nullptr;
    QAction* deleteAct_ = nullptr;
    QAction* duplicateAct_ = nullptr;
    class QActionGroup* schemeGroup_ = nullptr;
    QLabel* hintLabel_ = nullptr; // the status bar: what the mouse buttons and keys do now
    int hintStep_ = 0; // 0: press Куб, 1: the tile Твёрдое, 2: ▶
    bool forceFirstStart_ = false;
    QLabel* status_ = nullptr;
    class PlayOverlay* playOverlay_ = nullptr;
    class CommandSearch* search_ = nullptr;
    QAction* keepAct_ = nullptr;   // K: keep the simulation's poses after Stop
    QAction* searchAct_ = nullptr; // Ctrl+K

    // Mouse brush (GUI-side settings, applied through Disturbance commands)
    struct Brush {
        float radius = 0.15f;   // m
        float strength = 1.0f;  // multiplier of the cursor speed
        float smoke = 0.6f;     // added per stroke sample
        float heat = 0.0f;
    } brush_;

    struct Automation {
        bool active = false;
        int preset = 0;
        int frames = 0;
        int settle = 0;
        bool wholeWindow = false;
        bool editOnly = false;
        bool throughCamera = false;
        QString shot, csv;
    } auto_;
};
