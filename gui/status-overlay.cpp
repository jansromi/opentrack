#include "status-overlay.hpp"

#include <algorithm>
#include <cmath>

#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScreen>

using namespace pipeline_impl;
using namespace overlay_placement;

namespace {

namespace colors {
const QColor background(12, 14, 18, 214);
const QColor modifier_background(255, 255, 255, 13);
const QColor fg(0xe7, 0xe9, 0xec);
const QColor muted(0x85, 0x8d, 0x98);
const QColor dim(0x4a, 0x51, 0x5b);
const QColor precision(0x5c, 0xc8, 0xff);
const QColor detent(0xf5, 0xb9, 0x42);
const QColor stop(0xff, 0x6b, 0x5e);
const QColor joystick(0xb9, 0xa3, 0xff);
const QColor view(0x6e, 0xe7, 0xa8);
const QColor frozen(0xd0, 0xd5, 0xdc);
const QColor place(0xe7, 0xe9, 0xec, 150);
} // ns colors

constexpr int refresh_ms = 80;

// table order, top to bottom
constexpr Axis row_order[] = { Yaw, Pitch, Roll, TX, TY, TZ };

QString axis_name(Axis axis)
{
    switch (axis)
    {
    case TX: return status_overlay::tr("X");
    case TY: return status_overlay::tr("Y");
    case TZ: return status_overlay::tr("Z");
    case Yaw: return status_overlay::tr("YAW");
    case Pitch: return status_overlay::tr("PITCH");
    case Roll: return status_overlay::tr("ROLL");
    default: return {};
    }
}

QString minus_sign(QString text)
{
    return text.replace('-', QChar(0x2212));
}

QString format_value(double value)
{
    if (std::fabs(value) < 0.05)
        value = 0;
    return minus_sign(QString::number(value, 'f', 1));
}

QString format_point(double value)
{
    return minus_sign(QString::number(value, 'g', 4));
}

QString format_scale(double scale)
{
    return QString::number(scale, 'g', 3);
}

bool is_analog(axis_source source)
{
    return source == axis_source::analog || source == axis_source::tracked_analog;
}

} // ns

status_overlay::status_overlay(source_fn source) :
    QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                     Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus),
    source(std::move(source)), fake(qgetenv("OPENTRACK_OVERLAY") == "fake")
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // tool windows otherwise hide on macOS when another app is in front
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setWindowTitle(tr("opentrack status"));

    font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    modifier_font = font;
    modifier_font.setWeight(QFont::DemiBold);
    last_enabled = s.enabled;

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &status_overlay::place_later);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &status_overlay::place_later);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &status_overlay::place_later);
    // also on reload, e.g. a profile switch or cancelling the options dialog
    connect(&*s.b, &options::bundle_::changed, this, &status_overlay::apply_settings);

    connect(&timer, &QTimer::timeout, this, &status_overlay::refresh);
    apply_settings();
}

bool status_overlay::active() const
{
    return fake || placing || s.enabled != toggled;
}

void status_overlay::toggle_visible()
{
    toggled = !toggled;
    apply_settings();
}

void status_overlay::apply_settings()
{
    // the hotkey only overrides the setting until the setting itself changes
    if (s.enabled != last_enabled)
    {
        last_enabled = s.enabled;
        toggled = false;
    }

    constexpr int text_sizes[] = { 12, 14, 18, 24 };
    text_size = text_sizes[std::clamp(int(s.text_size), 0, 3)];
    font.setPixelSize(text_size);
    modifier_font.setPixelSize(int(std::lround(text_size * 1.15)));

    background = colors::background;
    background.setAlpha(std::clamp(int(s.opacity), 0, 100) * 255 / 100);

    if (!active())
        timer.stop();
    else if (!timer.isActive())
        timer.start(refresh_ms);

    // relayout even if the content is the same, the fonts may have changed
    current = {};
    refresh();
    update();
    place_later();
}

screen_identity status_overlay::identity(const QScreen* screen)
{
    return { screen->name(), screen->manufacturer(), screen->model(), screen->serialNumber() };
}

QString status_overlay::screen_label(const QScreen* screen, int index)
{
    QString name = QStringLiteral("%1 %2").arg(screen->manufacturer(), screen->model()).simplified();
    if (name.isEmpty())
        name = screen->name();
    // the monitor's own resolution is easier to recognize than the scaled one
    const QSize size = screen->geometry().size() * screen->devicePixelRatio();
    return QStringLiteral("%1: %2 (%3×%4)").arg(index + 1).arg(name).arg(size.width()).arg(size.height());
}

void status_overlay::identify_screens()
{
    constexpr int box = 240;
    const QList<QScreen*> screens = QGuiApplication::screens();

    for (int i = 0; i < int(screens.size()); i++)
    {
        auto* window = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                                            Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setAttribute(Qt::WA_TranslucentBackground);
        window->setAttribute(Qt::WA_ShowWithoutActivating);
        window->setAttribute(Qt::WA_TransparentForMouseEvents);
        window->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
        window->setFixedSize(box, box);

        // a child, since a translucent top-level window skips the style sheet background
        auto* label = new QLabel(QString::number(i + 1), window);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("QLabel { background: rgba(12, 14, 18, 220); color: #e7e9ec;"
                                            " border-radius: 16px; font-size: 160px; font-weight: 600; }"));
        label->setGeometry(0, 0, box, box);

        QScreen* screen = screens[i];
        window->setScreen(screen);
        window->move(screen->geometry().center() - QPoint(box / 2, box / 2));
        window->show();
        QTimer::singleShot(2000, window, &QWidget::close);
    }
}

status_overlay::model status_overlay::make_model(const pipeline_status& status)
{
    model m;
    auto& mods = m.modifiers;
    const auto& axes = status.axes;
    const bool tailview = status.tailview != 0;
    const QString tailview_arrow = status.tailview < 0 ? QStringLiteral("◄") : QStringLiteral("►");

    if (status.frozen)
        mods.push_back({ tr("FROZEN"), {}, colors::frozen });
    if (status.zeroed)
        mods.push_back({ tr("ZEROED"), {}, colors::frozen });

    if (status.precision)
    {
        QStringList scales;
        for (Axis i : { Yaw, Pitch, Roll })
            scales << format_scale(axes[i].precision_scale);
        scales.removeDuplicates();
        mods.push_back({ tr("PRECISION"), QStringLiteral("×") + scales.join('/'), colors::precision });
    }

    if (tailview)
        mods.push_back({ tr("TAIL VIEW"), tailview_arrow, colors::view });
    if (status.upview)
        mods.push_back({ tr("UP VIEW"), QStringLiteral("▲"), colors::view });

    {
        QStringList names;
        for (Axis i : row_order)
            if (is_analog(axes[i].source))
                names << axis_name(i);
        if (!names.isEmpty())
            mods.push_back({ tr("JOYSTICK"),
                             names.join(' ') + (status.joystick_connected ? QString() : QStringLiteral(" ✕")),
                             status.joystick_connected ? colors::joystick : colors::stop });
    }

    for (Axis i : row_order)
    {
        const axis_status& a = axes[i];
        if (a.hard_stop)
            mods.push_back({ axis_name(i) + ' ' + tr("HARD STOP"), format_point(a.detent_value), colors::stop });
        else if (a.on_detent)
        {
            QString value = format_point(a.detent_value);
            if (a.detent_remaining > 0)
                value += QStringLiteral(" · %1s").arg(a.detent_remaining, 0, 'f', 1);
            mods.push_back({ axis_name(i) + ' ' + tr("DETENT"), value, colors::detent });
        }
    }

    if (mods.empty())
        mods.push_back({ tr("TRACKING"), {}, colors::dim });

    for (Axis i : row_order)
    {
        const axis_status& a = axes[i];
        if (a.source == axis_source::disabled)
            continue;

        row r;
        r.name = axis_name(i);
        r.unit = i >= Yaw ? QStringLiteral("°") : tr("cm");

        switch (a.source)
        {
        case axis_source::tracked: r.source = { tr("TRK"), colors::muted }; break;
        case axis_source::keys: r.source = { tr("KEY"), colors::fg }; break;
        case axis_source::analog: r.source = { tr("JOY"), colors::joystick }; break;
        case axis_source::tracked_keys: r.source = { tr("TRK+KEY"), colors::fg }; break;
        case axis_source::tracked_analog: r.source = { tr("TRK+JOY"), colors::joystick }; break;
        case axis_source::disabled: break;
        }

        QColor value_color = colors::fg;

        if (a.hard_stop)
        {
            value_color = colors::stop;
            r.marker = { QStringLiteral("■ ") + tr("STOP") + ' ' + format_point(a.detent_value), colors::stop };
        }
        else if (a.on_detent)
        {
            value_color = colors::detent;
            r.marker = { QStringLiteral("◆ ") + tr("DET") + ' ' + format_point(a.detent_value), colors::detent };
        }
        else if (i == Yaw && tailview)
        {
            value_color = colors::view;
            r.marker = { tr("TAIL") + ' ' + tailview_arrow, colors::view };
        }
        else if (i == Pitch && status.upview)
        {
            value_color = colors::view;
            r.marker = { tr("UP") + QStringLiteral(" ▲"), colors::view };
        }
        else if (is_analog(a.source))
            value_color = colors::joystick;
        else if (a.precision)
        {
            value_color = colors::precision;
            r.marker = { QStringLiteral("×") + format_scale(a.precision_scale), colors::precision };
        }
        else if (status.frozen)
            value_color = colors::frozen;

        r.value = { format_value(a.value), value_color };
        m.rows.push_back(r);
    }

    return m;
}

void status_overlay::refresh()
{
    if (!active())
    {
        hide();
        return;
    }

    std::optional<pipeline_status> status;

    if (fake)
    {
        fake_time += refresh_ms * 1e-3;
        status = fake_status(fake_time);
    }
    else if (source)
        status = source();

    model m;
    if (status)
        m = make_model(*status);
    // there has to be something to drag, at about the size it will be
    else if (placing)
        m = make_model(pipeline_status {});
    else if (!s.only_while_tracking)
        m.modifiers.push_back({ tr("STOPPED"), {}, colors::dim });

    if (!s.show_modifiers)
        m.modifiers.clear();
    if (!s.show_axes)
        m.rows.clear();
    if (placing)
        m.modifiers.insert(m.modifiers.begin(), { tr("DRAG TO PLACE"), {}, colors::fg });

    const bool changed = m != current;
    current = std::move(m);

    if (current.modifiers.empty() && current.rows.empty())
    {
        hide();
        return;
    }

    if (changed)
    {
        relayout();
        update();
    }

    if (!isVisible())
    {
        place();
        show();
    }
}

namespace {

struct metrics final
{
    int pad_x, pad_y, gap, modifier_gap, modifier_pad, modifier_bar;
    int name_w, value_w, unit_w, source_w, marker_w, col_gap;
    int row_h, modifier_h;
};

metrics compute_layout(const QFontMetrics& fm, const QFontMetrics& mfm, int size)
{
    metrics l {};
    auto em = [size](double x) { return int(std::lround(size * x)); };

    l.pad_x = em(0.9);
    l.pad_y = em(0.7);
    l.gap = em(0.6);
    l.modifier_gap = em(0.3);
    l.modifier_pad = em(0.6);
    l.modifier_bar = 3;
    l.col_gap = em(0.7);

    // widest possible text per column, so the panel doesn't change width while values move
    l.name_w = fm.horizontalAdvance(QStringLiteral("PITCH"));
    l.value_w = fm.horizontalAdvance(QStringLiteral("−180.0"));
    l.unit_w = fm.horizontalAdvance(QStringLiteral("cm"));
    l.source_w = fm.horizontalAdvance(QStringLiteral("TRK+JOY"));
    l.marker_w = fm.horizontalAdvance(QStringLiteral("◆ DET −100"));

    l.row_h = fm.height() + em(0.15);
    l.modifier_h = mfm.height() + em(0.3);
    return l;
}

} // ns

void status_overlay::relayout()
{
    const QFontMetrics fm(font), mfm(modifier_font);
    const metrics l = compute_layout(fm, mfm, text_size);

    int table_w = l.name_w + l.value_w + l.unit_w + l.source_w + l.marker_w + 4 * l.col_gap;
    int modifiers_w = 0;
    for (const modifier& mod : current.modifiers)
    {
        const QString text = mod.value.isEmpty() ? mod.name : mod.name + ' ' + mod.value;
        modifiers_w = std::max(modifiers_w, l.modifier_bar + 2 * l.modifier_pad + mfm.horizontalAdvance(text));
    }

    const int w = 2 * l.pad_x + std::max(table_w, modifiers_w);
    const int mods_h = int(current.modifiers.size()) * l.modifier_h +
                       std::max(0, int(current.modifiers.size()) - 1) * l.modifier_gap;
    const int table_h = current.rows.empty() ? 0 :
                        (current.modifiers.empty() ? 0 : l.gap) + int(current.rows.size()) * l.row_h;
    const int h = 2 * l.pad_y + mods_h + table_h;

    if (size() != QSize(w, h))
    {
        setFixedSize(w, h);
        // keeps the anchored edge in place
        place();
    }
}

QScreen* status_overlay::target_screen() const
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    std::vector<screen_identity> ids;
    ids.reserve(size_t(screens.size()));
    for (const QScreen* screen : screens)
        ids.push_back(identity(screen));

    const int i = match_screen({ s.screen_name, s.screen_manufacturer, s.screen_model, s.screen_serial }, ids);
    // the saved monitor stays in the settings, so the overlay moves back once it's connected again
    return i >= 0 ? screens[i] : QGuiApplication::primaryScreen();
}

void status_overlay::place_later()
{
    // coalesces a burst of hotplug signals or setting writes, and lets the screen list settle
    if (place_pending)
        return;
    place_pending = true;
    QTimer::singleShot(0, this, [this] {
        place_pending = false;
        place();
    });
}

void status_overlay::place()
{
    if (dragging)
        return;

    QScreen* screen = target_screen();
    if (!screen)
        return;

    if (screen != screen_)
    {
        disconnect(screen_geometry_conn);
        screen_ = screen;
        screen_geometry_conn = connect(screen, &QScreen::geometryChanged, this, &status_overlay::place_later);
    }

    // before move(), so Windows doesn't scale the window for the monitor it's leaving
    if (QWidget::screen() != screen)
        setScreen(screen);

    // geometry(), not availableGeometry(): a game covers the taskbar
    move(anchor_position(screen->geometry(), size(), s.anchor, { s.offset_x, s.offset_y }));
}

void status_overlay::set_place_mode(bool enable)
{
    if (enable == placing)
        return;

    placing = enable;
    dragging = false;

    const bool was_visible = isVisible();
    // the attribute first: Qt puts the flag back on a window with it set
    setAttribute(Qt::WA_TransparentForMouseEvents, !enable);
    // recreates the native window, which hides it
    setWindowFlag(Qt::WindowTransparentForInput, !enable);
    if (enable)
        setCursor(Qt::OpenHandCursor);
    else
        unsetCursor();

    if (was_visible)
    {
        place();
        show();
    }
    apply_settings();
}

void status_overlay::mousePressEvent(QMouseEvent* e)
{
    if (!placing || e->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(e);

    dragging = true;
    drag_start = e->globalPosition().toPoint() - pos();
    setCursor(Qt::ClosedHandCursor);
}

void status_overlay::mouseMoveEvent(QMouseEvent* e)
{
    if (!dragging)
        return QWidget::mouseMoveEvent(e);

    move(e->globalPosition().toPoint() - drag_start);
}

void status_overlay::mouseReleaseEvent(QMouseEvent* e)
{
    if (!dragging || e->button() != Qt::LeftButton)
        return QWidget::mouseReleaseEvent(e);

    dragging = false;
    setCursor(Qt::OpenHandCursor);
    drop();
}

void status_overlay::drop()
{
    const QRect frame = geometry();
    QScreen* screen = QGuiApplication::screenAt(frame.center());
    if (!screen)
        screen = QWidget::screen();
    if (!screen)
        return;

    const placement p = snap_to_anchor(screen->geometry(), frame);
    const screen_identity id = identity(screen);

    s.screen_name = id.name;
    s.screen_manufacturer = id.manufacturer;
    s.screen_model = id.model;
    s.screen_serial = id.serial;
    s.anchor = p.anchor;
    s.offset_x = p.offset.x();
    s.offset_y = p.offset.y();

    place();
}

void status_overlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QFontMetrics fm(font), mfm(modifier_font);
    const metrics l = compute_layout(fm, mfm, text_size);

    p.setPen(Qt::NoPen);
    p.setBrush(background);
    p.drawRoundedRect(rect(), 6, 6);

    if (placing)
    {
        p.setPen(QPen(colors::place, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    }

    const int inner_w = width() - 2 * l.pad_x;
    int y = l.pad_y;

    p.setFont(modifier_font);
    for (const modifier& mod : current.modifiers)
    {
        const QRect box(l.pad_x, y, inner_w, l.modifier_h);
        p.setPen(Qt::NoPen);
        p.setBrush(colors::modifier_background);
        p.drawRoundedRect(box, 3, 3);
        p.setBrush(mod.color);
        p.drawRect(QRect(box.left(), box.top(), l.modifier_bar, box.height()));

        p.setPen(mod.color);
        const QString text = mod.value.isEmpty() ? mod.name : mod.name + ' ' + mod.value;
        p.drawText(box.adjusted(l.modifier_bar + l.modifier_pad, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, text);

        y += l.modifier_h + l.modifier_gap;
    }

    if (current.rows.empty())
        return;

    if (!current.modifiers.empty())
    {
        y += l.gap - l.modifier_gap;
        p.setPen(colors::dim);
        p.drawLine(l.pad_x, y - l.gap / 2, width() - l.pad_x, y - l.gap / 2);
    }

    p.setFont(font);
    for (const row& r : current.rows)
    {
        int x = l.pad_x;
        auto draw = [&](int w, const QString& text, const QColor& color, Qt::Alignment align = Qt::AlignLeft) {
            p.setPen(color);
            p.drawText(QRect(x, y, w, l.row_h), align | Qt::AlignVCenter, text);
            x += w + l.col_gap;
        };

        draw(l.name_w, r.name, colors::muted);
        draw(l.value_w, r.value.text, r.value.color, Qt::AlignRight);
        x -= l.col_gap / 2;
        draw(l.unit_w, r.unit, colors::dim);
        draw(l.source_w, r.source.text, r.source.color);
        draw(l.marker_w, r.marker.text, r.marker.color);

        y += l.row_h;
    }
}

pipeline_status status_overlay::fake_status(double t)
{
    // cycles through the states the overlay can show, a few seconds each
    constexpr double phase_time = 3;
    const int phase = int(t / phase_time) % 8;
    const double phase_t = std::fmod(t, phase_time);

    pipeline_status s;
    auto& a = s.axes;
    // frozen output holds the values from when it froze
    const double wave_t = phase == 6 ? t - phase_t : t;
    auto wave = [wave_t](double amp, double freq, double offset = 0) { return amp * std::sin(wave_t * freq + offset); };

    a[Yaw].value = wave(18, 0.6) + wave(4, 1.7);
    a[Pitch].value = wave(6, 0.8, 1) + wave(1.5, 2.3);
    a[Roll].value = wave(2.2, 0.5, 2);
    a[TX].value = wave(1.4, 0.7, 0.5);
    a[TY].value = wave(0.9, 0.9, 1.3);
    a[TZ].value = 15;
    a[TZ].source = axis_source::keys;
    s.joystick_connected = true;

    auto precision = [&] {
        s.precision = true;
        for (Axis i : { Yaw, Pitch, Roll })
        {
            a[i].precision = true;
            a[i].precision_scale = 0.35;
            a[i].value *= 0.35;
        }
    };
    auto detent = [&] {
        a[TZ].value = 30;
        a[TZ].on_detent = true;
        a[TZ].detent_value = 30;
        a[TZ].detent_remaining = std::max(0.0, 0.6 - phase_t);
    };

    switch (phase)
    {
    case 0: break;
    case 1: precision(); break;
    case 2: detent(); break;
    case 3:
        a[TZ].value = 45;
        a[TZ].hard_stop = true;
        a[TZ].detent_value = 45;
        break;
    case 4:
        a[Yaw].source = axis_source::tracked_analog;
        a[Yaw].value += 42;
        break;
    case 5:
        s.tailview = -1;
        a[Yaw].value = -135 + wave(1.2, 0.6);
        break;
    case 6:
        s.frozen = true;
        break;
    case 7:
        precision();
        detent();
        a[Yaw].source = axis_source::tracked_analog;
        a[Yaw].value += 42;
        break;
    }

    return s;
}
