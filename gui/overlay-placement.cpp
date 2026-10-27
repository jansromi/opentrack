#include "overlay-placement.hpp"

#include <algorithm>

namespace overlay_placement {

namespace {

// part: 0 start, 1 center, 2 end of the screen along one direction
int place(int start, int length, int size, int part, int offset)
{
    switch (part)
    {
    case 0: return start + offset;
    case 1: return start + (length - size) / 2 + offset;
    default: return start + length - size - offset;
    }
}

int clamp_to(int pos, int start, int length, int size)
{
    return std::clamp(pos, start, std::max(start, start + length - size));
}

int nearest_part(int start, int length, int pos, int size)
{
    if (length <= 0)
        return 0;
    const int center = pos + size / 2 - start;
    return std::clamp(center * 3 / length, 0, 2);
}

int offset_for(int start, int length, int size, int part, int pos)
{
    switch (part)
    {
    case 0: return pos - start;
    case 1: return pos - (start + (length - size) / 2);
    default: return start + length - size - pos;
    }
}

} // ns

int match_screen(const screen_identity& saved, const std::vector<screen_identity>& screens)
{
    const int count = int(screens.size());
    auto same_monitor = [&](const screen_identity& s) {
        return (!saved.manufacturer.isEmpty() || !saved.model.isEmpty()) &&
               s.manufacturer == saved.manufacturer && s.model == saved.model;
    };

    // some monitors report a placeholder serial like "0", so it only counts along with the model
    if (!saved.serial.isEmpty())
        for (int i = 0; i < count; i++)
            if (screens[i].serial == saved.serial && same_monitor(screens[i]))
                return i;

    int first_same_monitor = -1;
    for (int i = 0; i < count; i++)
    {
        if (!same_monitor(screens[i]))
            continue;
        if (!saved.name.isEmpty() && screens[i].name == saved.name)
            return i;
        if (first_same_monitor == -1)
            first_same_monitor = i;
    }
    if (first_same_monitor != -1)
        return first_same_monitor;

    if (!saved.name.isEmpty())
        for (int i = 0; i < count; i++)
            if (screens[i].name == saved.name)
                return i;

    return -1;
}

QPoint anchor_position(const QRect& screen, const QSize& window, overlay_anchor anchor, QPoint offset)
{
    const int col = int(anchor) % 3, row = int(anchor) / 3;
    const int x = place(screen.x(), screen.width(), window.width(), col, offset.x());
    const int y = place(screen.y(), screen.height(), window.height(), row, offset.y());
    return { clamp_to(x, screen.x(), screen.width(), window.width()),
             clamp_to(y, screen.y(), screen.height(), window.height()) };
}

placement snap_to_anchor(const QRect& screen, const QRect& window)
{
    const int x = clamp_to(window.x(), screen.x(), screen.width(), window.width());
    const int y = clamp_to(window.y(), screen.y(), screen.height(), window.height());
    const int col = nearest_part(screen.x(), screen.width(), x, window.width());
    const int row = nearest_part(screen.y(), screen.height(), y, window.height());

    return {
        overlay_anchor(row * 3 + col),
        { offset_for(screen.x(), screen.width(), window.width(), col, x),
          offset_for(screen.y(), screen.height(), window.height(), row, y) },
    };
}

} // ns overlay_placement
