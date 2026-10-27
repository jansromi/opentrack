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

    // OPENTRACK_OVERLAY=1 shows the pipeline status, =fake cycles through example states;
    // OPENTRACK_OVERLAY_PLACE=1 starts in place mode
    static std::unique_ptr<status_overlay> maybe_create(source_fn source);

    explicit status_overlay(source_fn source, bool fake = false);

    // the overlay takes the mouse and can be dragged; on release it snaps to the nearest anchor
    // of the monitor it was dropped on and saves that placement
    void set_place_mode(bool enable);
    bool place_mode() const { return placing; }

    static overlay_placement::screen_identity identity(const QScreen* screen);

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

    overlay_settings s;
    QPointer<QScreen> screen_;
    QMetaObject::Connection screen_geometry_conn;
    bool place_pending = false;
    bool placing = false;
    bool dragging = false;
    QPoint drag_start;
};
