#pragma once

#include "logic/main-settings.hpp"
#include "logic/pipeline.hpp"
#include "overlay-placement.hpp"
#include "export.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <QColor>
#include <QFont>
#include <QMetaObject>
#include <QPoint>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QWidget>

// glanceable panel with the active modifiers and per-axis values, see docs/overlay-plan.md
class OTR_GUI_EXPORT status_overlay final : public QWidget
{
    Q_OBJECT

public:
    // empty while there's nothing to show, e.g. tracking is stopped
    using source_fn = std::function<std::optional<pipeline_status>()>;

    // OPENTRACK_OVERLAY=fake shows it regardless of the settings, cycling through example states
    explicit status_overlay(source_fn source);

    // the hotkey: flips the enabled setting until restart, without saving it
    void toggle_visible();

    // the overlay takes the mouse and can be dragged; on release it snaps to the nearest anchor
    // of the monitor it was dropped on and stores that placement, for the options dialog to save
    void set_place_mode(bool enable);
    bool place_mode() const { return placing; }

    static overlay_placement::screen_identity identity(const QScreen* screen);
    // "N: manufacturer model (WxH)", N counting from 1 in QGuiApplication::screens() order
    static QString screen_label(const QScreen* screen, int index);
    // briefly shows each screen's number from screen_label() in big type on that screen
    static void identify_screens();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    struct cell final
    {
        QString text;
        QColor color;
        bool operator==(const cell&) const = default;
    };

    struct modifier final
    {
        QString name, value;
        QColor color;
        bool operator==(const modifier&) const = default;
    };

    struct row final
    {
        QString name, unit;
        cell value, source, marker;
        bool operator==(const row&) const = default;
    };

    struct model final
    {
        std::vector<modifier> modifiers;
        std::vector<row> rows;
        bool operator==(const model&) const = default;
    };

    static model make_model(const pipeline_status& status);
    static pipeline_status fake_status(double t);

    bool active() const;
    void apply_settings();
    void refresh();
    void relayout();
    QScreen* target_screen() const;
    void place();
    void place_later();
    void drop();

    source_fn source;
    bool fake = false;
    double fake_time = 0;
    QTimer timer;
    model current;
    QFont font, modifier_font;
    int text_size = 14;
    QColor background;
    bool toggled = false;
    bool last_enabled = false;

    overlay_settings s;
    QPointer<QScreen> screen_;
    QMetaObject::Connection screen_geometry_conn;
    bool place_pending = false;
    bool placing = false;
    bool dragging = false;
    QPoint drag_start;
};
