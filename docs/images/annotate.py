"""Draws the pictures of docs/ui-guide.md: numbered callouts on the editor's screenshots.

    python annotate.py                 redraw every picture from raw/
    python annotate.py --import DIR    first copy the screenshots from DIR into raw/
    python annotate.py --measure NAME  print where the top bar's buttons are in raw/NAME
    python annotate.py laboratory      redraw only the pictures named (the functions below)

The screenshots come from the editor itself (run.cmd --screenshot file.png --window ...; the gizmo
close-ups from --gizmo-shots). Each picture below is one function: which screenshot, what to crop,
and a list of callouts. A callout is a rectangle in the screenshot's own pixels plus the number
drawn next to it; with a label, the number and the text go into a margin beside the crop.
Needs Pillow; the fonts are Segoe UI from Windows (any TTF with Cyrillic will do).

The callout rectangles are pixels of the 1600 x 950 screenshots of the current layout (the top
bar fits its width: at 1600 px the captions stay, the buttons sit closer together). After a layout
change, import fresh screenshots and measure the rectangles again (--measure prints the top bar's
button groups of a screenshot).
"""
import os
import shutil
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "raw")
MAX_WIDTH = 1400                      # the published width limit

ACCENT = (255, 77, 109)               # callouts: a coral that reads on the dark UI and the grey floor
ACCENT_FILL = (255, 77, 109, 38)      # the soft highlight inside a rectangle
MARGIN_BG = (24, 26, 32)              # the label margin, a shade darker than the editor
LABEL = (232, 234, 240)
LABEL_DIM = (150, 156, 170)

FONT_DIR = "C:/Windows/Fonts"
BOLD = os.path.join(FONT_DIR, "segoeuib.ttf")
REGULAR = os.path.join(FONT_DIR, "segoeui.ttf")

# raw/ name -> the file of the editor's screenshot run it is copied from. Most come from
#   run.cmd --software-gl --self-test --shots DIR            (1600 x 950 each)
# three from the automation into the same DIR:
#   run.cmd --software-gl --scene examples/welcome.rfscene --window --edit --size 1600x950 --screenshot DIR/window-edit.png --frames 1
#   run.cmd --software-gl --scene examples/welcome.rfscene --window --size 1600x950 --screenshot DIR/playing-welcome.png --frames 600
#   run.cmd --software-gl --gallery-shot DIR/examples-gallery.png
IMPORTS = {
    "window.png": "window-edit.png",
    "object-bare.png": "object-tab-bare-cube.png",
    "object-rigid-capsule.png": "object-tab-cube-rigid-capsule.png",
    "collider-sphere-capsule.png": "collider-sphere-capsule-edit.png",
    "add-component-menu.png": "add-component-menu.png",
    "playing.png": "playing-welcome.png",
    "clone-popover.png": "clone-popover.png",
    "clone-row.png": "clone-row.png",
    "array.png": "array-card.png",
    "groups.png": "groups-tree.png",
    "multi-edit.png": "multi-edit.png",
    "box-select.png": "controls-box-select.png",
    "examples.png": "examples-gallery.png",
    "gizmo-w.png": "gizmo-w-close.png",
    "gizmo-e.png": "gizmo-e-close.png",
    "gizmo-e-drag.png": "gizmo-e-drag-close.png",
    "gizmo-r.png": "gizmo-r-close.png",
    "lab-contacts.png": "lab-contacts.png",
    "lab-timeline.png": "lab-timeline.png",
    "plots.png": "plots-3-cube.png",
    "plots-menu.png": "plots-menu.png",
}


def font(path, size):
    return ImageFont.truetype(path, size)


def load(name):
    return Image.open(os.path.join(RAW, name)).convert("RGBA")


def save(img, name):
    """Scales down to MAX_WIDTH and writes an optimised PNG next to this script."""
    if img.width > MAX_WIDTH:
        h = round(img.height * MAX_WIDTH / img.width)
        img = img.resize((MAX_WIDTH, h), Image.LANCZOS)
    img.convert("RGB").save(os.path.join(HERE, name), optimize=True)
    print("wrote", name, img.size)


# --- The three marks: a highlight rectangle, a leader line, a numbered badge ---------------------

def closest_point(rect, x, y):
    """The point of the rectangle (border or inside) nearest to (x, y)."""
    x0, y0, x1, y1 = rect
    return min(max(x, x0), x1), min(max(y, y0), y1)


def highlight(img, rect):
    """A rounded outline; small rectangles are also softly filled (a big fill would tint the scene)."""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    small = (rect[2] - rect[0]) * (rect[3] - rect[1]) < 80000
    ImageDraw.Draw(layer).rounded_rectangle(rect, radius=6, fill=ACCENT_FILL if small else None,
                                            outline=ACCENT, width=3)
    img.alpha_composite(layer)


def leader(img, start, rect):
    end = closest_point(rect, *start)
    draw = ImageDraw.Draw(img)
    draw.line([start, end], fill=ACCENT, width=2)
    draw.ellipse([end[0] - 4, end[1] - 4, end[0] + 4, end[1] + 4], fill=ACCENT)


def badge(img, x, y, number, radius=17):
    draw = ImageDraw.Draw(img)
    draw.ellipse([x - radius - 2, y - radius - 2, x + radius + 2, y + radius + 2], fill=(255, 255, 255))
    draw.ellipse([x - radius, y - radius, x + radius, y + radius], fill=ACCENT)
    draw.text((x, y + 1), str(number), font=font(BOLD, int(radius * 1.25)), fill="white", anchor="mm")


def callouts(img, marks):
    """marks: (number, rect, badge position), all in the image's pixels. Highlights first, then
    lines, then badges, so no badge is covered by another callout's line."""
    for n, rect, pos in marks:
        highlight(img, rect)
    for n, rect, pos in marks:
        leader(img, pos, rect)
    for n, rect, pos in marks:
        badge(img, pos[0], pos[1], n)


# --- Pictures with a label margin -----------------------------------------------------------------

def wrap(text, fnt, width, draw):
    """Splits text into lines no wider than width pixels."""
    lines, line = [], ""
    for word in text.split():
        trial = (line + " " + word).strip()
        if draw.textlength(trial, font=fnt) <= width:
            line = trial
        else:
            lines.append(line)
            line = word
    lines.append(line)
    return lines


def labelled(src, crop, items, margin=420, title=None, zoom=1.0):
    """The crop of src with a margin of numbered labels on the left; each label points at its
    rectangle. items: (rect in src pixels, headline, explanation). A label sits at the height of its
    rectangle, pushed down when it would overlap the label above. A rectangle of zero size is a
    point: the line ends there without a box. zoom enlarges a small crop."""
    shot = load(src).crop(crop)
    if zoom != 1.0:
        shot = shot.resize((round(shot.width * zoom), round(shot.height * zoom)), Image.LANCZOS)
    top = 60 if title else 12
    head, body = font(BOLD, 21), font(REGULAR, 18)
    measure = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    def place(x, y):  # screenshot pixels -> picture pixels
        return round((x - crop[0]) * zoom) + margin, round((y - crop[1]) * zoom) + top
    text_x, text_w = 76, margin - 110

    # Layout first: where each label goes, so the canvas can grow to fit the last one.
    labels, next_free = [], top
    for rect, headline, explanation in items:
        r = place(rect[0], rect[1]) + place(rect[2], rect[3])
        lines = wrap(explanation, body, text_w, measure) if explanation else []
        y = max(next_free, (r[1] + r[3]) // 2 - 14)
        next_free = y + 34 + 24 * len(lines) + 18
        labels.append((r, headline, lines, y))

    height = max(shot.height + top + 12, next_free)
    img = Image.new("RGBA", (shot.width + margin, height), MARGIN_BG + (255,))
    img.alpha_composite(shot, (margin, top))
    draw = ImageDraw.Draw(img)
    if title:
        draw.text((28, 16), title, font=font(BOLD, 25), fill=LABEL)
    for r, headline, lines, y in labels:
        draw.text((text_x, y), headline, font=head, fill=LABEL)
        for i, line in enumerate(lines):
            draw.text((text_x, y + 32 + 24 * i), line, font=body, fill=LABEL_DIM)
    for r, headline, lines, y in labels:
        if r[0] != r[2]:
            highlight(img, r)
    for r, headline, lines, y in labels:
        # the line starts after the headline, runs to the margin's edge, then to the rectangle
        after_text = (text_x + draw.textlength(headline, font=head) + 12, y + 16)
        corner = (margin - 8, y + 16)
        if after_text[0] < corner[0]:
            ImageDraw.Draw(img).line([after_text, corner], fill=ACCENT, width=2)
        leader(img, corner, r)
    for n, (r, headline, lines, y) in enumerate(labels, 1):
        badge(img, text_x - 36, y + 15, n)
    return img


# --- The pictures ---------------------------------------------------------------------------------

def overview():
    """The whole window; the guide's table explains the numbers."""
    img = load("window.png")
    callouts(img, [
        (1, (6, 28, 544, 100), (562, 128)),         # create: Куб ... Модель, Свет, Камера
        (2, (568, 28, 812, 100), (690, 132)),       # Пуск / Пауза / Стоп / Шаг
        (3, (832, 28, 986, 100), (909, 132)),       # Отменить / Повторить
        (4, (1092, 46, 1598, 86), (1052, 64)),      # Примеры / Коллайдеры / Лаборатория / Графики / Эксперт
        (5, (6, 112, 52, 334), (92, 300)),          # tools Q W E R and Мир
        (6, (66, 116, 530, 154), (580, 176)),       # the viewport's headline and hint
        (7, (1118, 120, 1238, 240), (1085, 264)),   # the nav cube
        (8, (1256, 134, 1404, 162), (1440, 148)),   # Сцена / Объект
        (9, (1266, 174, 1590, 412), (1222, 450)),   # the scene list
        (10, (2, 932, 764, 950), (800, 900)),       # status bar: what the mouse does now
        (11, (1268, 932, 1580, 950), (1222, 900)),  # status bar: Правка or Игра
        (12, (70, 300, 1180, 850), (140, 800)),     # the viewport
    ])
    save(img, "overview.png")


def first_body():
    """Three clicks: Куб, Твёрдое, Пуск."""
    img = load("object-bare.png")
    callouts(img, [
        (1, (6, 28, 60, 100), (130, 200)),
        (2, (1264, 242, 1342, 312), (1210, 340)),
        (3, (568, 28, 616, 100), (592, 176)),
    ])
    save(img, "first-body.png")


def object_tab():
    img = labelled("object-rigid-capsule.png", (1256, 160, 1600, 925), [
        ((1262, 168, 1590, 216), "Заголовок", "имя и значки того, что уже добавлено"),
        ((1262, 226, 1580, 312), "Из чего", "одна плитка: Твёрдое, Мягкое, Жидкость или Ткань"),
        ((1262, 318, 1580, 406), "Что ещё делает", "сколько угодно: Коллайдер, Магнит, Дым, Горит, Тепло"),
        ((1262, 418, 1580, 570), "Объект", "имя, глаз, замок, цвет; где, поворот, размер"),
        ((1262, 582, 1580, 660), "Геометрия", "как объект выглядит; физика её не меняет"),
        ((1262, 700, 1580, 920), "Компоненты", "только добавленные; карточка складывается, крестик убирает"),
    ], title="Вкладка «Объект»")
    save(img, "object-tab.png")


def add_component():
    img = labelled("add-component-menu.png", (1180, 600, 1600, 900), [
        ((1266, 714, 1390, 884), "Меню", "только то, чего у объекта ещё нет"),
        ((1392, 802, 1592, 836), "+ Добавить компонент", "внизу вкладки «Объект»"),
    ], margin=380)
    save(img, "add-component.png")

def geometry_collider():
    img = labelled("collider-sphere-capsule.png", (540, 262, 770, 492), [
        ((688, 296, 688, 296), "Коллайдер: капсула", "тонкий зелёный каркас: так объект сталкивается"),
        ((632, 420, 632, 420), "Геометрия: сфера", "так объект выглядит"),
    ], margin=400, zoom=2.0)
    save(img, "geometry-collider.png")

def playing():
    img = load("playing.png")
    callouts(img, [
        (1, (568, 28, 616, 100), (520, 190)),
        (2, (628, 28, 750, 100), (690, 190)),
        (3, (70, 138, 134, 154), (170, 196)),
        (4, (1336, 932, 1580, 950), (1222, 895)),
        (5, (428, 120, 880, 158), (940, 196)),      # the play banner with K
        (6, (572, 858, 734, 914), (520, 840)),      # the big buttons at the bottom of the view
    ])
    save(img, "playing.png")


def gizmos():
    """The four gizmo close-ups side by side, each with its key under it."""
    names = [("gizmo-w.png", "W", "двигать"), ("gizmo-e.png", "E", "вращать"),
             ("gizmo-e-drag.png", "E", "во время поворота"), ("gizmo-r.png", "R", "масштаб")]
    shots = [load(n) for n, _, _ in names]
    gap, caption = 16, 64
    w = sum(s.width for s in shots) + gap * (len(shots) + 1)
    h = max(s.height for s in shots) + caption + gap
    img = Image.new("RGBA", (w, h), MARGIN_BG + (255,))
    draw = ImageDraw.Draw(img)
    x = gap
    for shot, (_, key, text) in zip(shots, names):
        img.alpha_composite(shot, (x, gap))
        cx, cy = x + shot.width // 2, gap + shot.height + caption // 2
        key_w = 44
        draw.rounded_rectangle([cx - 110, cy - 20, cx - 110 + key_w, cy + 20], radius=7,
                               fill=(52, 56, 68), outline=(110, 116, 132), width=2)
        draw.text((cx - 110 + key_w // 2, cy), key, font=font(BOLD, 22), fill=LABEL, anchor="mm")
        draw.text((cx - 54, cy), text, font=font(REGULAR, 22), fill=LABEL, anchor="lm")
        x += shot.width + gap
    save(img, "gizmos.png")


def clone():
    img = labelled("clone-popover.png", (580, 450, 1060, 810), [
        ((690, 470, 860, 640), "Shift + тянуть ручку", "за гизмо едет копия; отпустили — окошко"),
        ((944, 648, 1030, 672), "Сколько копий", "1–1000; копии встают на d, 2d, 3d…"),
        ((848, 678, 1000, 758), "Копия / Экземпляр / Массив", "что получится, см. таблицу ниже"),
        ((858, 761, 1030, 788), "OK / Отмена", "Enter / Esc; щелчок мимо — тоже отмена"),
    ], margin=440, title="Окошко «Клонировать»")
    save(img, "clone.png")


def clone_row():
    img = load("clone-row.png")
    callouts(img, [
        (1, (470, 404, 940, 666), (440, 372)),
        (2, (1262, 168, 1596, 216), (1222, 290)),
        (3, (2, 930, 300, 950), (340, 900)),
    ])
    save(img, "clone-row.png")


def array():
    img = load("array.png")
    callouts(img, [
        (1, (1262, 170, 1446, 216), (1222, 282)),
        (2, (1276, 394, 1452, 460), (1234, 426)),
        (3, (1272, 464, 1588, 582), (1234, 520)),
        (4, (1276, 622, 1586, 648), (1234, 680)),
    ])
    save(img, "array.png")


def groups():
    img = load("groups.png")
    callouts(img, [
        (1, (1268, 236, 1590, 264), (1230, 250)),
        (2, (1300, 266, 1590, 322), (1230, 330)),
        (3, (500, 382, 712, 578), (460, 350)),
        (4, (2, 930, 434, 950), (474, 900)),
    ])
    save(img, "groups.png")


def multi_edit():
    img = load("multi-edit.png")
    callouts(img, [
        (1, (1262, 170, 1462, 216), (1222, 282)),
        (2, (1350, 446, 1545, 470), (1216, 440)),
        (3, (1328, 483, 1410, 507), (1216, 500)),
        (4, (1380, 740, 1570, 764), (1216, 752)),
    ])
    save(img, "multi-edit.png")


def box_select():
    img = load("box-select.png")
    callouts(img, [(1, (472, 372, 668, 550), (430, 340))])
    save(img, "box-select.png")


def examples():
    save(load("examples.png"), "examples.png")


def laboratory():
    """The Laboratory (F8) open on a cube that has just landed, a contact point clicked: the guide's
    table explains the numbers."""
    img = load("lab-contacts.png")
    callouts(img, [
        (1, (1306, 48, 1413, 82), (1440, 120)),     # the «Лаборатория» button
        (2, (64, 206, 392, 280), (440, 250)),       # the presets
        (3, (64, 290, 392, 496), (440, 400)),       # the layers, each with its colours
        (4, (66, 500, 392, 758), (440, 560)),       # the card of the clicked contact point
        (5, (80, 714, 380, 750), (440, 700)),       # «Следить за парой»
        (6, (64, 776, 392, 922), (440, 800)),       # the profiler: the step by stages, 5 s of history
        (7, (712, 530, 940, 655), (1010, 520)),     # the contact dots and the ring of the clicked one
        (8, (420, 880, 1238, 914), (600, 848)),     # the timeline strip
    ])
    save(img, "laboratory.png")


def lab_timeline():
    """The timeline strip while it shows a kept frame (the scene paused, «Вживую» lit)."""
    left, top = 470, 380
    img = load("lab-timeline.png").crop((left, top, 1250, 930))
    def at(*xy):  # screenshot pixels -> the crop's
        return tuple(v - (left if i % 2 == 0 else top) for i, v in enumerate(xy))
    callouts(img, [
        (1, at(506, 886, 530, 906), at(518, 836)),      # |< one frame back (>| one on)
        (2, at(542, 886, 922, 906), at(660, 846)),      # the scrubber
        (3, at(964, 886, 1146, 906), at(1010, 846)),    # which frame, its time, the memory
        (4, at(1148, 883, 1230, 911), at(1196, 836)),   # «Вживую»
    ])
    save(img, "lab-timeline.png")


def plots():
    """The plots a second after ▶ with the cube clicked: the scene's energy, the cube's height and
    speed, the quiet controls; the guide's table explains the numbers."""
    left, top = 40, 690
    img = load("plots.png").crop((left, top, 1262, 1008))
    def at(*xy):  # screenshot pixels -> the crop's (the 1600 x 1022 window of the self-test)
        return tuple(v - (left if i % 2 == 0 else top) for i, v in enumerate(xy))
    callouts(img, [
        (1, at(62, 876, 420, 962), at(96, 730)),         # the scene's energy: three lines
        (2, at(362, 838, 382, 856), at(404, 730)),       # the ▾ after the title: this chart's list
        (3, at(62, 857, 362, 873), at(250, 730)),        # the legend: a click hides, a double click isolates
        (4, at(466, 817, 1250, 965), at(860, 730)),      # «Объект: Куб 1»: height and speed
        (5, at(428, 836, 454, 965), at(530, 730)),       # the «+»: one more chart
        (6, at(60, 976, 464, 1000), at(510, 988)),       # «Ещё величины…» and the layouts
        (7, at(1156, 976, 1250, 1000), at(1040, 988)),   # «Сохранить CSV»
    ])
    save(img, "plots.png")


def plots_menu():
    """The right click on a chart: everything that may be done to it."""
    save(load("plots-menu.png").crop((716, 644, 980, 892)), "plots-menu.png")


def measure(name, top=24, bottom=104, gap=10):
    """Prints the x ranges of the things on the top bar of a screenshot (columns that differ from
    the bar's own colour), grouped where they are closer than gap pixels: the rectangles to use."""
    img = load(name).convert("RGB")
    bg = img.getpixel((img.width // 2 + 3, top + 2))
    busy = []
    for x in range(img.width):
        busy.append(any(max(abs(a - b) for a, b in zip(img.getpixel((x, y)), bg)) > 40 for y in range(top, bottom)))
    groups, start, last = [], None, None
    for x, on in enumerate(busy):
        if on:
            if start is None:
                start = x
            elif x - last > gap:
                groups.append((start, last))
                start = x
            last = x
    if start is not None:
        groups.append((start, last))
    for g in groups:
        print(name, "x", g[0], "..", g[1])


def import_screenshots(folder):
    os.makedirs(RAW, exist_ok=True)
    for name, source in IMPORTS.items():
        shutil.copyfile(os.path.join(folder, source), os.path.join(RAW, name))
        print("copied", source, "->", "raw/" + name)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--measure":
        measure(sys.argv[2])
        sys.exit(0)
    if len(sys.argv) == 3 and sys.argv[1] == "--import":
        import_screenshots(sys.argv[2])
    pictures = (overview, first_body, object_tab, add_component, geometry_collider, playing,
                gizmos, clone, clone_row, array, groups, multi_edit, box_select, examples,
                laboratory, lab_timeline, plots, plots_menu)
    named = [p for p in pictures if p.__name__ in sys.argv[1:]]
    for picture in named or pictures:
        picture()
