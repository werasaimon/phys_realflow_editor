// The component tests of the self-test (see SelfTest.h): the "Объект" tab shows only what an object
// has, "Твёрдое тело" comes with a collider, a collider alone is a fixed obstacle, the collider never
// changes the geometry, a soft body's material is one click, and three clicks make a cube fall. Each
// check prints one line.
#include "SelfTestSupport.h"

#include "ColliderGuides.h"
#include "InspectorWidgets.h"
#include "MainWindow.h"
#include "SoftPanel.h"

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>

#include <algorithm>
#include <cmath>

using namespace rf;
using namespace selftest;

namespace {

// The component cards the "Объект" tab shows now, by title.
QStringList visibleCards(const SceneBuilder& b) {
    QStringList titles;
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>())
        if (card->isVisibleTo(b.inspectorPanel())) titles << card->title();
    return titles;
}

ComponentCard* cardTitled(const SceneBuilder& b, const QString& title) {
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>())
        if (card->title() == title) return card;
    return nullptr;
}

// The soft body's material buttons, in their order: Желе, Резина, Мягкий пластик, Своё.
QList<QToolButton*> softPresets(const SceneBuilder& b) {
    ComponentCard* card = cardTitled(b, "Мягкое тело");
    return card ? card->findChildren<QToolButton*>("softPreset") : QList<QToolButton*>();
}

// Presses the collider kind button (Авто, Коробка, Сфера, Капсула, Выпуклая оболочка, Точно).
void clickColliderKind(const SceneBuilder& b, ColliderKind kind) {
    const QList<QToolButton*> buttons = b.inspectorPanel()->findChildren<QToolButton*>("colliderButton");
    if (int(kind) < buttons.size()) buttons[int(kind)]->click();
}

// The window with an open popup menu drawn where it stands (a popup is a window of its own).
QImage windowWithMenu(QMainWindow& w, QMenu* menu) {
    QImage shot = windowShot(w);
    QPainter p(&shot);
    p.drawPixmap(w.mapFromGlobal(menu->pos()), menu->grab());
    return shot;
}

void testBareObject(Checker& c, QMainWindow& w, SceneBuilder& b, const QString& dir) {
    b.newScene();
    b.createActions()[0]->trigger(); // Куб
    b.frameSelected();
    pump(300);
    auto* tabs = w.findChild<QTabWidget*>("inspectorTabs");
    c.check(tabs && tabs->currentIndex() == 1 && tabs->tabText(1) == "Объект", "a new cube is selected and the panel turns to «Объект»");
    const QStringList cards = visibleCards(b);
    std::printf("  bare cube: %lld component card(s)\n", static_cast<long long>(cards.size()));
    c.check(cards.isEmpty() && entityIsGeometryOnly(last(b)), "a bare cube shows «Объект» and «Геометрия» only: no component cards");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/object-tab-bare-cube.png");
}

void testRigidBringsCollider(Checker& c, QMainWindow& w, SceneBuilder& b) {
    clickRole(w, RoleIcon::Rigid);
    pump(100);
    const QStringList cards = visibleCards(b);
    std::printf("  after «Твёрдое»: cards %s\n", qPrintable(cards.join(", ")));
    c.check(cards.size() == 2 && cards.contains("Твёрдое тело") && cards.contains("Коллайдер") && last(b).collider.enabled &&
                last(b).collider.kind == ColliderKind::Auto,
            "+Твёрдое: two cards, the rigid body and an automatic collider");
    ComponentCard* collider = cardTitled(b, "Коллайдер");
    if (auto* remove = collider ? collider->findChild<QToolButton*>("cardRemove") : nullptr) remove->click();
    pump(50);
    ComponentCard* rigid = cardTitled(b, "Твёрдое тело");
    InlineBanner* banner = rigid ? rigid->findChild<InlineBanner*>() : nullptr;
    const QString warning = banner ? banner->message() : QString();
    std::printf("  collider removed; the rigid card says: %s\n", qPrintable(warning));
    c.check(!last(b).collider.enabled && last(b).rigid.enabled && warning.contains("провалится"),
            "✕ on the collider: the rigid card warns that the body falls through everything");
    if (auto* fix = banner ? banner->findChild<QPushButton*>("bannerButton") : nullptr) fix->click();
    pump(50);
    c.check(last(b).collider.enabled && banner && banner->message().isEmpty(), "[Добавить коллайдер] puts the collider back");
}

void testAddComponentMenu(Checker& c, QMainWindow& w, SceneBuilder& b, const QString& dir) {
    auto* menu = b.inspectorPanel()->findChild<QMenu*>("addComponentMenu");
    auto* button = b.inspectorPanel()->findChild<QPushButton*>("addComponent");
    if (!menu || !button) return c.check(false, "the «+ Добавить компонент» button and its menu exist");
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>()) card->setOpen(false); // the menu fits below
    for (QWidget* p = button->parentWidget(); p; p = p->parentWidget()) // scrolled into view, as a person would
        if (auto* scroll = qobject_cast<QScrollArea*>(p)) scroll->ensureWidgetVisible(button);
    pump(50);
    emit menu->aboutToShow(); // filled, so its height is known: it opens upwards, inside the window
    menu->popup(button->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
    pump(250);
    QStringList items;
    for (QAction* a : menu->actions())
        if (!a->isSeparator()) items << a->text();
    std::printf("  «+ Добавить компонент» offers: %s\n", qPrintable(items.join(", ")));
    if (!dir.isEmpty()) windowWithMenu(w, menu).save(dir + "/add-component-menu.png");
    QAction* magnet = nullptr;
    for (QAction* a : menu->actions())
        if (a->text() == "Магнит") magnet = a;
    menu->hide();
    for (ComponentCard* card : b.inspectorPanel()->findChildren<ComponentCard*>()) card->setOpen(true);
    c.check(!items.contains("Твёрдое тело") && !items.contains("Коллайдер") && magnet, "the menu offers only what the object does not have");
    if (magnet) magnet->trigger();
    pump(50);
    c.check(last(b).magnet.enabled && visibleCards(b).contains("Магнит"), "choosing «Магнит» adds its card");
    b.undoAction()->trigger();
}

// A soft cube: four materials in a row at a glance, «Желе» pressed, the numbers folded away.
// «Резина» gives it rubber's Young's modulus at one click and stays pressed; undo takes back only
// the material, the body stays soft.
void testSoftPresets(Checker& c, QMainWindow& w, SceneBuilder& b) {
    b.newScene();
    b.createActions()[0]->trigger(); // Куб
    clickRole(w, RoleIcon::Soft);
    pump(100);
    const QList<QToolButton*> presets = softPresets(b);
    QStringList names;
    for (QToolButton* p : presets) names << p->text();
    auto* details = b.inspectorPanel()->findChild<CollapsibleSection*>("softDetails");
    std::printf("  soft cube: materials %s; «Подробнее» %s\n", qPrintable(names.join(", ")),
                details && details->isOpen() ? "open" : "folded");
    c.check(names == QStringList({"Желе", "Резина", "Мягкий пластик", "Своё"}) && presets[0]->isVisibleTo(b.inspectorPanel()) &&
                presets[0]->isChecked() && details && !details->isOpen(),
            "+Мягкое: four materials in view (Желе, Резина, Мягкий пластик, Своё), «Желе» pressed, the numbers folded");
    if (presets.size() != 4) return;
    presets[1]->click(); // Резина
    pump(50);
    const SoftRole now = last(b).soft, rubber = softPreset(SoftPreset::Rubber);
    std::printf("  «Резина»: E = %.0f Pa (rubber: %.0f Pa), ν = %.2f, %.0f kg/m³\n", double(now.youngModulus),
                double(rubber.youngModulus), double(now.poissonRatio), double(now.density));
    c.check(now.youngModulus == rubber.youngModulus && softPresetOf(now) == SoftPreset::Rubber && presets[1]->isChecked(),
            "«Резина»: one click gives the body rubber's Young's modulus, and the button stays pressed");
    b.undoAction()->trigger();
    pump(50);
    c.check(last(b).soft.enabled && softPresetOf(last(b).soft) == SoftPreset::Jelly && presets[0]->isChecked(),
            "undo after «Резина»: jelly again and the body still soft (the click is an undo step of its own)");
}

// A typed Young's modulus that is no material's presses «Своё», typed back to jelly's «Желе»;
// «Своё» itself only opens «Подробнее».
void testSoftNumbers(Checker& c, QMainWindow& w, SceneBuilder& b, const QString& dir) {
    const QList<QToolButton*> presets = softPresets(b);
    auto* details = b.inspectorPanel()->findChild<CollapsibleSection*>("softDetails");
    auto* panel = b.inspectorPanel()->findChild<SoftPanel*>();
    if (presets.size() != 4 || !details || !panel) return c.check(false, "the soft card has its materials and «Подробнее»");
    panel->youngField()->setValue(2.0e4); // as typed and Enter
    pump(50);
    const float typed = last(b).soft.youngModulus;
    const bool custom = presets[3]->isChecked();
    panel->youngField()->setValue(softPreset(SoftPreset::Jelly).youngModulus);
    pump(50);
    std::printf("  typed E = 20 000 Pa: the body has %.0f Pa, «Своё» %s; typed back: «Желе» %s\n", double(typed),
                custom ? "pressed" : "not pressed", presets[0]->isChecked() ? "pressed" : "not pressed");
    c.check(typed == 2.0e4f && custom && presets[0]->isChecked() && softPresetOf(last(b).soft) == SoftPreset::Jelly,
            "a typed Young's modulus goes to the body and presses «Своё»; jelly's number presses «Желе» again");
    presets[3]->click(); // Своё
    pump(50);
    c.check(details->isOpen() && softPresetOf(last(b).soft) == SoftPreset::Jelly, "«Своё» opens «Подробнее» and changes nothing");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/object-tab-soft-materials.png");
    details->setOpen(false);
}

// A cube with a collider only hangs in the air as a fixed obstacle; a rigid ball dropped on it
// bounces off, and the cube does not move.
void testStaticCollider(Checker& c, SceneBuilder& b, Viewport* v) {
    b.newScene();
    b.createActions()[0]->trigger();
    const uint32_t cube = last(b).id;
    b.editEntity(cube, [](Entity& e) {
        e.size = Vector3(0.6f, 0.2f, 0.6f);
        e.position = Vector3(0.0f, 0.5f, 0.0f);
        e.collider.enabled = true;
    });
    b.createActions()[1]->trigger(); // Сфера
    const uint32_t ball = last(b).id;
    b.editEntity(ball, [](Entity& e) {
        e.size = Vector3(0.2f);
        e.position = Vector3(0.05f, 1.4f, 0.0f);
        e.rigid.enabled = true;
        e.rigid.restitution = 0.8f;
        e.collider.enabled = true;
    });
    b.play();
    float lowest = 1e9f, riseAfterLowest = 0.0f, cubeDrift = 0.0f;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 20000 && (!v->snapshot() || v->snapshot()->frame < 120)) {
        pump(15);
        const auto& s = *v->snapshot();
        const int cb = b.bodyOfEntity(cube), bb = b.bodyOfEntity(ball);
        if (cb < 0 || bb < 0 || size_t(std::max(cb, bb)) >= s.bodies.size()) continue;
        cubeDrift = std::max(cubeDrift, length(s.bodies[size_t(cb)].pos - Vector3(0.0f, 0.5f, 0.0f)));
        const float y = s.bodies[size_t(bb)].pos.y;
        if (y < lowest) lowest = y, riseAfterLowest = 0.0f;
        riseAfterLowest = std::max(riseAfterLowest, y - lowest);
    }
    std::printf("  ball on a collider-only cube: lowest y = %.3f m (cube top 0.6 + radius 0.1), bounced up %.3f m; cube moved %.6f m\n",
                double(lowest), double(riseAfterLowest), double(cubeDrift));
    c.check(cubeDrift < 1e-5f && lowest > 0.66f && lowest < 1.0f, "a collider alone is a fixed obstacle: the ball stops on it, the cube stays");
    c.check(riseAfterLowest > 0.02f, "the ball bounces off the collider-only cube");
    b.stop();
    pump(200);
}

// The sphere's mesh: every vertex at the same distance from the centre (within 5 %).
bool looksLikeSphere(const TriMesh& m) {
    if (m.positions.empty()) return false;
    Vector3 centre(0.0f);
    for (const Vector3& p : m.positions) centre += p;
    centre = centre / float(m.positions.size());
    float lo = 1e9f, hi = 0.0f;
    for (const Vector3& p : m.positions) {
        lo = std::min(lo, length(p - centre));
        hi = std::max(hi, length(p - centre));
    }
    return hi > 0.0f && (hi - lo) / hi < 0.05f;
}

// A sphere with a rigid body and a capsule collider: the geometry stays a sphere, the green lines
// are a capsule, the simulated body collides as a capsule and is drawn as the sphere.
void testSphereCapsule(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    b.newScene();
    b.createActions()[1]->trigger(); // Сфера
    clickRole(w, RoleIcon::Rigid);
    clickColliderKind(b, ColliderKind::Capsule);
    v->setFocusBox(AABB(last(b).position - Vector3(0.6f, 0.9f, 0.6f), last(b).position + Vector3(0.6f, 0.2f, 0.6f)));
    v->frameScene(); // the ball and the floor it lands on
    pump(300);
    const Entity e = last(b);
    const EntityCollider ec = entityCollider(e, b.graph().baseDirectory);
    size_t expected = 0;
    if (ec.shape && ec.shape->type() == ShapeType::Capsule) {
        const auto& cap = static_cast<const CapsuleShape&>(*ec.shape);
        expected = capsuleGuide(cap.radius(), cap.halfHeight()).size();
    }
    const size_t drawn = v->colliderGuides().size() / 4;
    std::printf("  sphere + Твёрдое + Капсула: shape %d, collider %d, guide points %zu (a capsule has %zu)\n", int(e.shape),
                ec.shape ? int(ec.shape->type()) : -1, drawn, expected);
    c.check(e.shape == ShapeKind::Sphere && e.collider.kind == ColliderKind::Capsule && expected > 0 && drawn == expected,
            "the capsule button: the geometry stays a sphere, the green wireframe is a capsule");
    // The user's case: a capsule taller than the ball (fit off, typed size) - a barrel around a sphere.
    b.editEntity(e.id, [](Entity& x) {
        x.collider.fitToGeometry = false;
        x.collider.size = Vector3(0.22f, 0.5f, 0.22f);
    });
    pump(300);
    c.check(last(b).shape == ShapeKind::Sphere && v->colliderGuides().size() / 4 == expected,
            "a typed capsule size changes the wireframe only: the geometry stays a sphere");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/collider-sphere-capsule-edit.png");
    b.play();
    waitFrames(v, 40, 15000);
    const int body = b.bodyOfEntity(e.id);
    const auto& s = *v->snapshot();
    const bool ok = body >= 0 && size_t(body) < s.bodies.size() && s.bodies[size_t(body)].collisionShape &&
                    s.bodies[size_t(body)].collisionShape->type() == ShapeType::Capsule && s.bodies[size_t(body)].mesh &&
                    looksLikeSphere(*s.bodies[size_t(body)].mesh);
    c.check(ok && !v->colliderGuides().empty(), "play: the body collides as a capsule, is drawn as the sphere, the wireframe follows it");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/collider-sphere-capsule-play.png");
    b.stop();
    pump(200);
}

// The cube of the coordinator's picture: rigid, with a capsule collider standing in it.
void cubeCapsuleShot(QMainWindow& w, SceneBuilder& b, const QString& dir) {
    b.newScene();
    b.createActions()[0]->trigger();
    b.editEntity(last(b).id, [](Entity& e) { e.size = Vector3(0.4f, 0.6f, 0.4f); });
    clickRole(w, RoleIcon::Rigid);
    clickColliderKind(b, ColliderKind::Capsule);
    b.frameSelected();
    pump(400);
    windowShot(w).save(dir + "/object-tab-cube-rigid-capsule.png");
}

// The first minute: Куб, the tile «Твёрдое», ▶ - three clicks and the cube falls onto the floor.
void testThreeClicks(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v) {
    b.newScene();
    int clicks = 0;
    b.createActions()[0]->trigger(), ++clicks;
    const float start = last(b).position.y;
    clickRole(w, RoleIcon::Rigid), ++clicks;
    if (auto* play = w.findChild<QAction*>("actionPlay")) play->trigger(), ++clicks;
    waitFrames(v, 90, 15000);
    float y = 1e9f;
    const int body = b.bodyOfEntity(last(b).id);
    if (body >= 0 && size_t(body) < v->snapshot()->bodies.size()) y = v->snapshot()->bodies[size_t(body)].pos.y;
    std::printf("  %d clicks: the cube went from y = %.2f m to y = %.3f m\n", clicks, double(start), double(y));
    c.check(clicks == 3 && y < 0.2f, "three clicks (Куб, Твёрдое, ▶): the cube falls and lands on the floor");
    b.stop();
    pump(200);
}

// The very first start: the welcome scene and the bubble that leads through Куб, Твёрдое, ▶. The
// user's own "seen it" flag is put back afterwards.
void testFirstMinute(Checker& c, QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& dir) {
    auto* mw = dynamic_cast<MainWindow*>(&w);
    if (!mw) return c.check(false, "the window is the editor's MainWindow");
    const QVariant seen = QSettings().value("hints/firstMinuteDone");
    mw->setForceFirstStart(true);
    mw->startBuilder(true);
    pump(600);
    const bool welcome = std::any_of(b.graph().entities.begin(), b.graph().entities.end(), [](const Entity& e) { return e.name == "Горка"; });
    const QString first = mw->firstMinuteText();
    std::printf("  first start: %zu objects; the bubble says: %s\n", b.graph().entities.size(), qPrintable(first));
    c.check(welcome && first.contains("Куб"), "first start: the welcome scene and a bubble pointing at «Куб»");
    if (!dir.isEmpty()) windowShot(w).save(dir + "/first-start.png");
    b.createActions()[0]->trigger();
    pump(150);
    const QString second = mw->firstMinuteText();
    clickRole(w, RoleIcon::Rigid);
    pump(150);
    const QString third = mw->firstMinuteText();
    if (!dir.isEmpty()) windowShot(w).save(dir + "/first-start-step3.png");
    if (auto* play = w.findChild<QAction*>("actionPlay")) play->trigger();
    pump(150);
    c.check(second.contains("Твёрдое") && third.contains("Пуск") && mw->firstMinuteText().isEmpty() &&
                QSettings().value("hints/firstMinuteDone").toBool(),
            "the bubble moves to «Твёрдое», then to ▶, and is gone after ▶ for good");
    waitFrames(v, 60, 15000);
    b.stop();
    pump(200);
    mw->setForceFirstStart(false);
    if (seen.isValid()) QSettings().setValue("hints/firstMinuteDone", seen);
    else QSettings().remove("hints/firstMinuteDone");
}

} // namespace

int runComponentTests(QMainWindow& w, SceneBuilder& b, Viewport* v, const QString& shotsDir) {
    Checker c;
    if (!shotsDir.isEmpty()) QDir().mkpath(shotsDir);
    testBareObject(c, w, b, shotsDir);
    testRigidBringsCollider(c, w, b);
    testAddComponentMenu(c, w, b, shotsDir);
    testSoftPresets(c, w, b);
    testSoftNumbers(c, w, b, shotsDir);
    testStaticCollider(c, b, v);
    testSphereCapsule(c, w, b, v, shotsDir);
    testThreeClicks(c, w, b, v);
    testFirstMinute(c, w, b, v, shotsDir);
    if (!shotsDir.isEmpty()) cubeCapsuleShot(w, b, shotsDir);
    return c.failures;
}
