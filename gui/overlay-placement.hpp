#pragma once

// placement math for the status overlay, kept free of QScreen and widgets so it can be tested
// without the monitors it's meant for

#include "logic/main-settings.hpp"
#include "export.hpp"

#include <vector>

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

namespace overlay_placement {

struct screen_identity final
{
    QString name, manufacturer, model, serial;
    bool operator==(const screen_identity&) const = default;
};

struct placement final
{
    overlay_anchor anchor;
    QPoint offset;
    bool operator==(const placement&) const = default;
};

// index into screens of the saved monitor, or -1 when it isn't connected. tries, in order:
// the same serial number, the same manufacturer and model (the same name breaks a tie between
// identical monitors), the same name
OTR_GUI_EXPORT int match_screen(const screen_identity& saved, const std::vector<screen_identity>& screens);

// top-left of a window anchored inside screen, kept inside screen
OTR_GUI_EXPORT QPoint anchor_position(const QRect& screen, const QSize& window, overlay_anchor anchor, QPoint offset);

// nearest anchor for a window dropped at window, with the offset that keeps it where it was dropped
OTR_GUI_EXPORT placement snap_to_anchor(const QRect& screen, const QRect& window);

} // ns overlay_placement
