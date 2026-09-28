# Чужой код в редакторе PhysRealFlow и его лицензии

Редактор — общее достояние: лицензия MIT-0 (файл [LICENSE](LICENSE)), без каких-либо условий.
Физика — SDK PhysRealFlow (подмодуль `extern/phys_realflow`), тоже MIT-0.

Сторонние библиотеки остаются под **своими** лицензиями:

| Что | Лицензия | Что требует |
|---|---|---|
| Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets) | LGPL-3.0 | Qt подключается динамически (DLL рядом с программой), не статически. Раздавая программу, приложите текст LGPL-3.0 и ссылку на исходники Qt (https://download.qt.io/), и не мешайте заменить DLL Qt своими. Наш собственный код при этом остаётся MIT-0. |
| Среда выполнения MinGW-w64 (libgcc, libstdc++, libwinpthread) | GPL-3.0 с исключением GCC Runtime Library Exception; winpthread — MIT/BSD | Исключение GCC разрешает раздавать программу под любой лицензией. |
