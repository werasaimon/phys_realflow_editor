#pragma once
// OpenGL viewport: orbit camera, particles as ray-traced sphere impostors, meshes, field slice,
// streamlines, ray-marched smoke volume and a colour legend overlay. The scene's lights shade the
// meshes (the first sun with shadows casts them from a shadow map), lights and cameras are drawn as
// wireframes, and the view can look through a camera of the scene (ViewportLights.cpp).
// Needs only OpenGL 3.0 (GLSL 1.30): runs on any GPU of the last ~15 years and on the CPU through
// Mesa llvmpipe (--software-gl). A 3.3 core context is used when the driver offers one.

#include "ControlScheme.h"
#include "EditView.h"
#include "FluidSurfaceRenderer.h"
#include "Gizmo.h"
#include "NavCube.h"
#include "OrbitCamera.h"
#include "SceneMarkers.h"
#include "ViewportTools.h"
#include "scene/Simulation.h"

#include <QColor>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QVector3D>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

class QTimer;

class Viewport : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT
public:
    enum Colormap { Turbo, Viridis, CoolWarm, Gray };

    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    void setSnapshot(std::shared_ptr<const rf::RenderSnapshot> s);
    const std::shared_ptr<const rf::RenderSnapshot>& snapshot() const { return snap_; }
    void setColormap(int c) { colormap_ = c; update(); }
    void setShowDomain(bool on) { showDomain_ = on; update(); }
    void setShowFloor(bool on) { showFloor_ = on; update(); }
    // Draw non-convex bodies as their convex decomposition instead of the smooth model.
    void setShowConvexParts(bool on) { showConvexParts_ = on; update(); }
    void setParticleScale(float s) { particleScale_ = s; update(); }
    void setSmokeDensity(float d) { smokeDensity_ = d; update(); }
    void setSliceOpacity(float a) { sliceOpacity_ = a; update(); }
    void frameScene();
    // What "Показать всё" (F) and a new scene frame: the scene builder's objects instead of the whole
    // world box (an invalid box: the world box again).
    void setFocusBox(const rf::AABB& box) { focusBox_ = box; }

    static QColor colormapColor(int map, float t);
    float renderFps() const { return fps_; }

    // Interaction (used by the ViewportTool strategies; ViewportInput.cpp)
    OrbitCamera& camera() { return camera_; }
    // Which mouse button moves the camera, and how (ControlScheme.h).
    void setControlScheme(ControlScheme s) { scheme_ = s; updateHint(); }
    ControlScheme controlScheme() const { return scheme_; }
    CameraMove cameraMoveFor(Qt::MouseButton b, Qt::KeyboardModifiers m) const { return ::cameraMoveFor(scheme_, b, m); }
    // What the cursor is on (an object, a body), else the floor: the camera orbits and zooms around it.
    QVector3D pointUnderCursor(const QPointF& pos) const;
    bool flying() const { return rightHeld_ && !flyKeys_.empty(); } // the right button held with W A S D
    bool flewThisPress() const { return flew_; } // W A S D were used while the right button was down (no menu then)
    // Looks along ball k of the navigation cube (+X, -X, +Y, -Y, +Z, -Z): the numpad views too.
    void viewAlong(int k);
    const NavCube& navCube() const { return navCube_; }
    // What the mouse buttons and keys do right now, for the status bar (hintChanged when it changes).
    QString mouseHint() const;
    // The time step and frame rate line under the title: for the expert mode only.
    void setShowStats(bool on) { showStats_ = on; update(); }
    // Point on the cursor ray inside the domain: on the slice plane if hit, else mid-domain.
    bool pickPoint(const Ray& ray, rf::Vector3& hit) const;
    // Nearest rigid body hit by the ray (exact ray cast against its convex shape).
    bool pickBody(const Ray& ray, int& body, rf::Vector3& hit) const;
    // Nearest particle (liquid, soft body) or cloth vertex hit by the ray; hit = its centre.
    bool pickParticle(const Ray& ray, rf::Vector3& hit) const;
    // Any body under the cursor, static ones too (the scene builder selects walls and floors).
    bool pickAnyBody(const Ray& ray, int& body, rf::Vector3& hit) const;
    // The scene builder's feedback: the bodies of the selected thing get an outline; locked ones
    // are invisible to the mouse (it passes through them to what is behind). In edit mode the body
    // under the mouse gets a lighter outline and geometry without a role a faint one.
    void setHighlightBodies(const std::vector<int>& bodies) { highlight_ = bodies; update(); }
    void setHoverBodies(const std::vector<int>& bodies) { hoverBodies_ = bodies; update(); }
    // Scenery drawn next to the simulation's bodies: geometry without a role takes no part in the
    // simulation but stays in the picture while the scene plays.
    void setSceneryBodies(std::vector<rf::RenderSnapshot::Body> bodies) { scenery_ = std::move(bodies); update(); }
    // The scene builder's own scene (not a ready-made sample): the world box is only a faint floor
    // outline, and the solver's markers (the gas's heat source) are not drawn - its objects show that.
    void setAuthoringScene(bool on) { authoring_ = on; update(); }
    void setGhostBodies(const std::vector<int>& bodies) { ghostBodies_ = bodies; update(); }
    // The collider wireframes: pairs of points, x y z state (ColliderGuides::State: moving, fixed, asleep).
    void setColliderGuides(std::vector<float> lines);
    // The 2D text over the view (the scene's name, the legends, the axes); off for the gallery's pictures.
    void setOverlayVisible(bool on) { overlayVisible_ = on; update(); }
    const std::vector<float>& colliderGuides() const { return colliderGuides_; }
    void setUnpickableBodies(const std::vector<char>& flags) { unpickable_ = flags; }
    bool unpickable(int body) const { return body >= 0 && body < int(unpickable_.size()) && unpickable_[size_t(body)]; }
    void showProbe(const Ray& ray, const rf::Vector3& hit);

    // Edit mode: the scene as authored, nothing simulated. The mouse selects objects through the
    // picker the scene builder gives (every object on a ray, nearest first) and drags the gizmo.
    void setEditMode(bool on);
    bool editMode() const { return editMode_; }
    void setEditCaption(const QString& text) { editCaption_ = text; update(); } // the line under the title
    // While the scene plays: a coloured frame round the view (ViewportPlay.cpp; PlayOverlay.h says why).
    void setPlayFrame(bool on);
    bool playFrame() const { return playFrame_; }
    // Drawn on the CPU (Mesa llvmpipe / softpipe), not on a GPU.
    bool softwareRenderer() const {
        return renderer_.contains("llvmpipe", Qt::CaseInsensitive) || renderer_.contains("softpipe", Qt::CaseInsensitive);
    }
    // While the camera orbits: a small mark on the point it turns around (on = false: none).
    void setOrbitPivot(const QVector3D& pivot, bool on);
    bool orbitPivotShown() const { return orbiting_; }
    void setEditPicker(std::function<std::vector<PickHit>(const Ray&)> picker) { picker_ = std::move(picker); }
    std::vector<PickHit> pickAll(const Ray& ray) const { return picker_ ? picker_(ray) : std::vector<PickHit>(); }
    uint32_t pickEntity(const Ray& ray, rf::Vector3* hit = nullptr) const;
    // A click: the nearest object under the cursor; a second click on the same spot (or Alt+click)
    // takes the next one behind it along the ray, and so on round. Emits entityClicked.
    uint32_t clickSelect(const QPointF& pos, bool next);
    void setHoveredEntity(uint32_t id);
    Gizmo& gizmo() { return gizmo_; }
    GizmoView gizmoView() const { return GizmoView{camera_, size()}; }
    // What the gizmo sits on: the selected object (visible = there is one that may be edited).
    void setGizmoTarget(bool visible, const rf::Vector3& position, const rf::Quaternion& rotation);
    bool gizmoShown() const { return editMode_ && gizmoVisible_ && gizmo_.mode() != GizmoMode::Select; }
    // Keyboard transforms (G, Shift+R, Shift+S): see Gizmo.h.
    void startModal(GizmoMode m);
    void finishModal();
    void cancelModal();
    void cancelGizmoDrag(); // Esc or the right button during a handle drag: the object goes back
    void setBrushRadius(float r) { brushRadius_ = r; }

    // Lights and cameras drawn as wireframes (SceneMarkers.h), from the scene builder.
    void setSceneMarkers(std::vector<SceneMarker> markers) { markers_ = std::move(markers); update(); }
    const std::vector<SceneMarker>& sceneMarkers() const { return markers_; }
    // Looking through a camera of the scene: the view is that camera - its eye, its turn (roll too)
    // and its lens. Moving the view then moves the camera (lookThroughMoved), as Blender's "lock
    // camera to view". Off: the editor's own view comes back as it was.
    void setLookThrough(bool on, const rf::Vector3& eye, const rf::Vector3& forward, const rf::Vector3& up, float fovDeg,
                        float nearClip, float farClip);
    bool lookingThrough() const { return lookThrough_; }
    bool sunShadowDrawn() const { return sunShadowDrawn_; } // the last frame had a sun's shadow map
    // Shadows are drawn for at most this many lights a frame: each costs a render pass (a lamp six).
    static constexpr int kMaxShadowLights = 4;
    // The lights (indices into the snapshot's lights) that asked for a shadow and got none in the last
    // frame, because more than kMaxShadowLights were ticked.
    const std::vector<int>& lightsWithoutShadow() const { return shadowLess_; }

signals:
    // World-space point and velocity of a mouse stroke (velocity is zero on the first click).
    void disturbanceRequested(rf::Vector3 position, rf::Vector3 velocity);
    void grabStarted(int body, rf::Vector3 point);
    void particleGrabStarted(rf::Vector3 point); // cloth / soft body / liquid particle under the cursor
    void grabMoved(rf::Vector3 target);
    void grabReleased();
    void bodyClicked(int body);                       // left click on any body: select it
    void entityClicked(uint32_t id);                  // edit mode: a click selected this object (0: empty space)
    void entityHovered(uint32_t id);                  // edit mode: the object under the mouse changed
    void gizmoStarted();                              // a handle, a Shift+drag or a keyboard transform began
    void gizmoMoved(const GizmoPose& pose);           // where it has taken the object so far
    void gizmoFinished();                             // confirmed: one step of the undo history
    void gizmoCancelled();                            // Esc / right button: put the object back
    void cloneDragStarted();                          // Shift was held when the handle drag began
    void cloneDragFinished(QPointF pos);              // ... and it ended here: ask how many copies
    void editContextMenu(QPointF pos);                // edit mode: a right click without a drag
    void entityToggled(uint32_t id);                  // edit mode: Shift / Ctrl + click on an object
    void boxSelected(QRectF rect, Qt::KeyboardModifiers modifiers); // edit mode: the selection box, released
    void frameSelectedRequested();                    // edit mode: a double click (F)
    void hintChanged(QString hint);                   // what the buttons and keys do now changed
    // Looking through a camera, the view was moved (orbit, pan, zoom, fly, an axis view): the camera goes there.
    void lookThroughMoved(rf::Vector3 eye, rf::Vector3 forward, rf::Vector3 up);
    // The GPU / driver cannot do OpenGL 3.0: the window offers a restart in software mode.
    void openGLUnsupported(QString renderer);
    // The lights left without a shadow changed (indices into the snapshot's lights; empty: none).
    void shadowsLeftOut(std::vector<int> lights);

protected:
    void initializeGL() override;
    void createUnitCubes();      // initializeGL, step by step
    void createDynamicBuffers();
    void createFieldTextures();
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool event(QEvent* e) override; // a keyboard transform takes the keys its shortcuts would get

private:
    struct Buffer {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        int count = 0;
    };

    void buildShaders();
    void beginFrame();
    void drawFloorAndDomain(const QMatrix4x4& vp);
    void drawScene(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawOverlays3D(const QMatrix4x4& vp);
    void drawColliderGuides(const QMatrix4x4& vp);
    void countFrame();
    void uploadObstacle(const rf::TriMesh& mesh);
    void drawBackground();
    void drawLines(const std::vector<float>& data, GLenum mode, const QMatrix4x4& vp, const QVector4D& color, float width);
    void drawObstacle(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawVesselGlass(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawBodies(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawBoxBodies(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawParticles(const QMatrix4x4& view, const QMatrix4x4& proj);
    void drawCloths(const QMatrix4x4& view, const QMatrix4x4& proj); // cloth sheets + soft body surfaces
    void ensureClothMesh();
    void drawSlice(const QMatrix4x4& vp);
    void drawStreamlines(const QMatrix4x4& vp);
    void drawFieldLines(const QMatrix4x4& vp); // magnetic field lines
    void drawVoxelGrid(const QMatrix4x4& vp);
    void drawVectors(const QMatrix4x4& vp, const QVector3D& eye);
    void drawHeatSource(const QMatrix4x4& vp);
    void drawProbe(const QMatrix4x4& vp);
    void drawDebugProbe(const QMatrix4x4& vp); // the engine's Probe drawing (lines, points, boxes)
    void drawGrab(const QMatrix4x4& vp);
    // Silhouette outlines: selection (orange), hover (faint white), geometry without a role (fainter).
    void drawHighlight(const QMatrix4x4& vp);
    bool ensureMaskTarget(int w, int h);
    void buildMaskTriangles();
    void appendBodyTriangles(const rf::RenderSnapshot::Body& b, std::vector<float>& out) const;
    void drawGizmo(const QMatrix4x4& vp);
    // ViewportLights.cpp: the wireframes of lights and cameras, the view through a camera, the lights.
    void drawSceneMarkers(const QMatrix4x4& vp);
    void syncLookThrough();                    // the view moved while looking through: say where to
    void applySceneLights(const QMatrix4x4& view); // the mesh shader's lights for this frame
    // ViewportShadows.cpp: the shadow maps of the lights with "Отбрасывает тени" ticked.
    void renderShadows();                      // choose the lights, draw their maps
    std::vector<int> pickShadowLights(std::vector<int>& without) const;
    bool renderDepthMap(int slot, const rf::RenderSnapshot::LightInfo& light);
    bool renderDistanceCube(int slot, const rf::RenderSnapshot::LightInfo& light);
    bool ensureDepthMap(int slot);
    bool ensureDistanceCube(int slot);
    void drawShadowCasters(QOpenGLShaderProgram& program);
    void bindShadowMaps(const QMatrix4x4& view); // the maps and their matrices, into the mesh shader
    void releaseShadowMaps();
    void uploadClothMesh();                    // this frame's cloth and soft surfaces (shadows, then drawing)
    void drawEditLabel(class QPainter& p); // the live amount of a drag next to the cursor
    bool modalKey(QKeyEvent* e);
    void dragKey(QKeyEvent* e);        // X / Y / Z during a handle drag
    bool flyKey(QKeyEvent* e, bool down);
    void flyTick();
    void stopFlying();
    bool navPress(QMouseEvent* e);     // the navigation cube takes the press
    void updateHint();
    void drawSelectionBox(class QPainter& p);
    void modalDrag(); // the keyboard changed the transform: recompute it at the cursor
    void drawJoints(const QMatrix4x4& vp);
    ViewportTool& toolFor(Qt::MouseButton button, const QPointF& pos);
    void drawVolume(const QMatrix4x4& vp, const QVector3D& eye);
    void drawOverlay();
    void drawAxesAndHints(class QPainter& p);
    void drawPlayFrame(class QPainter& p);  // ViewportPlay.cpp
    void drawOrbitPivot(class QPainter& p);
    void drawLegend(class QPainter& p, const QRect& r, float lo, float hi, const QString& label, int map);
    QMatrix4x4 viewMatrix() const { return camera_.view(); }
    QMatrix4x4 projMatrix() const { return camera_.projection(float(width()) / std::max(1, height())); }
    QVector3D eyePosition() const { return camera_.eye(); }

    std::shared_ptr<const rf::RenderSnapshot> snap_;

    QOpenGLShaderProgram meshProg_, sphereProg_, lineProg_, sliceProg_, volumeProg_, bgProg_, planetProg_, outlineProg_;
    Buffer bg_, obstacle_, cube_, particles_, lines_, slice_, volumeBox_;
    QOpenGLBuffer obstacleScalar_{QOpenGLBuffer::VertexBuffer};
    std::vector<uint32_t> obstacleVertexMap_; // expanded vertex -> original vertex
    struct GpuMesh {
        std::unique_ptr<QOpenGLVertexArrayObject> vao;
        std::unique_ptr<QOpenGLBuffer> vbo;
        int count = 0;
        // Hull cache: the mesh is held here too, so its address (the cache key) cannot be taken by a
        // new mesh while the entry exists; the entry is dropped once only the cache holds the mesh.
        std::shared_ptr<const rf::TriMesh> mesh;
    };
    std::map<const rf::TriMesh*, GpuMesh> hullCache_;
    void pruneHullCache(); // needs the GL context current (paintGL)
    GpuMesh clothMesh_; // rebuilt every frame
    FluidSurfaceRenderer fluidSurface_; // liquid as a water surface (screen space)
    GpuMesh& hullMesh(const std::shared_ptr<const rf::TriMesh>& m); // auto-smooth normals
    uint64_t obstacleVersion_ = 0;
    GLuint sliceTex_ = 0, volumeTex_ = 0;
    // Copy of the scene's depth (after the opaque pass): the volume rays stop at solid surfaces.
    GLuint depthTex_ = 0, depthFbo_ = 0;
    int depthW_ = 0, depthH_ = 0;
    bool copySceneDepth(int w, int h);
    // Every snapshot gets a new serial; the sprite, slice and smoke buffers are rebuilt once per new
    // snapshot (the frame number is no key: a paused reset or preset switch stays at frame 0).
    uint64_t snapSerial_ = 0, particleSerial_ = ~0ull, sliceSerial_ = ~0ull, volumeSerial_ = ~0ull;

    QString renderer_;   // OpenGL renderer (GPU name, or llvmpipe on the CPU)
    bool glOk_ = true;   // OpenGL 3.0 or newer available
    QString glslHeader_; // "#version 330 core" on 3.3 core contexts, "#version 130" on 3.0
    OrbitCamera camera_;
    CameraTool cameraTool_;
    DisturbTool disturbTool_;
    GrabTool grabTool_;
    EditTool editTool_;
    ViewportTool* grabbed_ = nullptr; // tool that received the press, keeps the drag
    Ray probeRay_;
    rf::Vector3 probeHit_;
    qint64 probeTime_ = 0;
    float brushRadius_ = 0.15f;
    bool framedOnce_ = false;
    rf::AABB focusBox_;            // what to frame when valid (the builder's objects)
    std::vector<int> highlight_;   // bodies outlined as selected
    std::vector<char> unpickable_; // per body: the mouse passes through
    std::vector<int> hoverBodies_, ghostBodies_;
    std::vector<rf::RenderSnapshot::Body> scenery_;
    std::vector<float> colliderGuides_;
    bool overlayVisible_ = true;
    bool showStats_ = false;
    ControlScheme scheme_ = ControlScheme::Simple;
    NavCube navCube_;
    int navHover_ = NavCube::kOutside, navPressHit_ = NavCube::kOutside;
    bool navPressed_ = false, navDragged_ = false;
    QPoint navLast_;
    bool rightHeld_ = false, flew_ = false; // the right button is down; W A S D were used during it
    std::set<int> flyKeys_;
    QTimer* flyTimer_ = nullptr;
    QElapsedTimer flyClock_;
    QString lastHint_;
    std::vector<float> guidesByState_[3]; // the same lines sorted by state: one colour per draw
    bool authoring_ = false;
    std::vector<const rf::RenderSnapshot::Body*> drawnBodies_;
    bool editMode_ = false, gizmoVisible_ = false;
    Gizmo gizmo_;
    std::function<std::vector<PickHit>(const Ray&)> picker_;
    uint32_t hoveredEntity_ = 0;
    QPointF lastMouse_;
    QString typed_; // the number typed during a keyboard transform
    bool playFrame_ = false;   // the play mode's frame round the view
    bool orbiting_ = false;    // the camera orbits: the pivot is marked
    QVector3D orbitPivot_;
    QString editCaption_ = "Правка: физика стоит. Двигайте, вращайте, масштабируйте; ▶ Пуск оживит сцену.";
    std::vector<SceneMarker> markers_;
    std::vector<float> markerSolid_, markerFaint_; // reused every frame
    bool lookThrough_ = false;
    OrbitCamera editorCamera_;                 // the editor's own view while looking through a camera
    QVector3D lookEye_, lookForward_, lookUp_; // the view as last set or reported (moves are the difference)
    // Shadows: one slot per light that casts one this frame (ViewportShadows.cpp).
    struct ShadowMap {
        int light = -1;                   // which of the snapshot's lights (-1: the slot is unused)
        bool cube = false;                // a lamp: a cube of distances; a sun or a spotlight: a depth map
        QMatrix4x4 viewProj;              // world -> the light's clip space (sun, spotlight)
        float nearClip = 0, farClip = 0;  // a spotlight's lens (0 0 for a sun); a lamp's reach in farClip
        rf::Vector3 lampAt;               // a lamp's position
        GLuint fbo = 0, depth = 0;        // the depth map
        GLuint cubeFbo = 0, cubeTex = 0, cubeDepth = 0; // the cube of distances and its depth buffer
    };
    ShadowMap shadowMaps_[kMaxShadowLights];
    std::vector<int> shadowLess_;              // ticked, but over the budget in the last frame
    bool sunShadowDrawn_ = false;
    QString meshSlots_;                        // the shadow slots compiled into the mesh shader (M map, C cube, - none)
    void buildMeshShader(const QString& slotKinds);
    QOpenGLShaderProgram shadowProg_, distanceProg_;
    std::vector<std::pair<int, int>> clothRanges_; // first vertex and count of each cloth / soft surface
    std::vector<GizmoPiece> gizmoPieces_; // reused every frame: drawing the gizmo allocates nothing
    // The outline mask: an offscreen target the outlined objects are drawn into, and their triangles
    // in world space (selected, hovered, without a role), rebuilt only when the scene or the lists change.
    GLuint maskFbo_ = 0, maskTex_ = 0;
    int maskW_ = 0, maskH_ = 0;
    std::vector<float> maskTriangles_[3];
    uint64_t maskSerial_ = ~0ull;
    std::vector<int> maskLists_[3];
    std::shared_ptr<const rf::TriMesh> unitSphere_;
    // Cycling through the objects on one ray: the last click and what it found.
    QPointF lastClickPos_;
    QElapsedTimer lastClickTime_;
    QMatrix4x4 lastClickView_;
    std::vector<uint32_t> lastClickHits_;
    int lastClickIndex_ = -1;
    std::string lastSceneName_; // the scene is framed again when another one is loaded

    int colormap_ = Turbo;
    bool showDomain_ = true, showFloor_ = true, showConvexParts_ = false;
    float particleScale_ = 1.0f, smokeDensity_ = 16.0f, sliceOpacity_ = 0.92f;

    // fps
    qint64 fpsLast_ = 0;
    int fpsFrames_ = 0;
    float fps_ = 0;
};
