#pragma once
// "Найти команду" (Ctrl+K), as the command palettes of VS Code and Blender's F3 search: a field at
// the top of the window and below it every command of the editor that matches what is typed -
// with where it lives in the menus and its key, so the search also teaches the keys
// (docs/ui-research.md, item 3). Enter or a click runs the command.
//
// The commands are the window's own actions (QAction): the menus give each its path
// ("Правка › Сгруппировать"), the toolbars' actions come as "Панель". Matching ignores case: a
// name that starts with the text comes first, then one that contains it, and only then - below a
// grey line "Похожие по буквам" - one whose letters appear in that order ("сгр" finds "Сгруппировать").
#include <QFrame>

#include <vector>

class QAction;
class QLineEdit;
class QListWidget;
class QMainWindow;
class QMenu;

class CommandSearch : public QFrame {
    Q_OBJECT
public:
    explicit CommandSearch(QMainWindow* window);
    void open();                      // above the 3D view, the field empty and focused
    void setQuery(const QString& text);
    std::vector<QAction*> results() const; // what the list shows now, best first
    int substringCount() const { return substringCount_; } // the first results that hold the text itself
    void runSelected();               // the highlighted command (Enter)

protected:
    void keyPressEvent(class QKeyEvent* e) override;

private:
    struct Command {
        QAction* action;
        QString name, path, key;
    };
    void collect();                   // every action of the window, with its menu path
    void collectMenu(QMenu* menu, const QString& path);
    void refill();                    // the list for the field's text

    QMainWindow* window_;
    QLineEdit* field_ = nullptr;
    QListWidget* list_ = nullptr;
    std::vector<Command> commands_;
    std::vector<QAction*> shown_;
    std::vector<QAction*> rowAction_; // per list row: its command, or null for the "Похожие" line
    int substringCount_ = 0;
};
