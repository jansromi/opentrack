/* Copyright (c) 2015, Stanislaw Halik <sthalik@misaki.pl>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */

#include "options-dialog.hpp"
#include "listener.h"
#include "status-overlay.hpp"

#include <utility>

#include <QPushButton>
#include <QLayout>
#include <QDialog>
#include <QFileDialog>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#ifdef _WIN32
#include "input/win32-joystick.hpp"
#endif

using namespace options;
using namespace options::globals;

namespace {

bool uses_tracking(translation_control_mode mode)
{
    return mode == translation_tracked ||
           mode == translation_tracked_manual_keys ||
           mode == translation_tracked_manual_analog;
}

bool uses_manual_keys(translation_control_mode mode)
{
    return mode == translation_manual_keys ||
           mode == translation_tracked_manual_keys;
}

bool uses_manual_analog(translation_control_mode mode)
{
    return mode == translation_manual_analog ||
           mode == translation_tracked_manual_analog;
}

} // ns

QString options_dialog::kopts_to_string(const key_opts& kopts)
{
    using namespace Qt::Literals::StringLiterals;
    if (!kopts.guid->isEmpty())
    {
        const int btn = kopts.button & ~Qt::KeyboardModifierMask;
        const int mods = kopts.button & Qt::KeyboardModifierMask;
        QString mm;
        if (mods & Qt::ControlModifier) mm += "Control+";
        if (mods & Qt::AltModifier) mm += "Alt+";
        if (mods & Qt::ShiftModifier) mm += "Shift+";
        const auto& str = kopts.guid == "mouse"_L1
                          ? tr("Mouse %1")
                          : kopts.guid->startsWith("GI!"_L1)
                          ? tr("Gamepad button %1")
                          : tr("Joy button %1");
        return mm + str.arg(QString::number(btn));
    }
    if (kopts.keycode->isEmpty())
        return tr("None");
    return kopts.keycode;
}

void options_dialog::set_disable_translation_state(bool value)
{
    with_global_settings_object([&](QSettings& s)
    {
        s.setValue("disable-translation", value);
        mark_global_ini_modified();
    });
}

void options_dialog::connect_binding_controls(key_opts& kopts, QLabel* label, QPushButton* button)
{
    auto* opt = &kopts;
    label->setText(kopts_to_string(kopts));
    connect(&kopts.keycode,
            static_cast<void (value_::*)(const QString&) const>(&value_::valueChanged),
            label,
            [label, opt](const QString&) { label->setText(kopts_to_string(*opt)); });
    connect(button, &QPushButton::clicked, this, [this, opt, label] { bind_key(*opt, label); });
}

void options_dialog::add_manual_shortcut_row(QGridLayout* layout, int row, const QString& label_text,
                                             QLabel*& text, QPushButton*& button)
{
    auto* label = new QLabel(label_text, this);
    text = new QLabel(this);
    button = new QPushButton(tr("Bind"), this);

    text->setMinimumWidth(100);

    layout->addWidget(label, row, 0);
    layout->addWidget(text, row, 1);
    layout->addWidget(button, row, 2);
}

void options_dialog::setup_manual_translation_ui()
{
    auto* output_layout = findChild<QVBoxLayout*>("verticalLayout_4");
    auto* shortcuts_layout = findChild<QVBoxLayout*>("verticalLayout");

    if (!output_layout || !shortcuts_layout)
        return;

    auto* output_group = new QGroupBox(tr("Manual translation"), this);
    auto* output_group_layout = new QVBoxLayout(output_group);

#ifdef _WIN32
    {
        auto* device_layout = new QHBoxLayout;
        manual_analog_device = new QComboBox(this);
        device_layout->addWidget(new QLabel(tr("Analog device"), this));
        device_layout->addWidget(manual_analog_device);
        output_group_layout->addLayout(device_layout);

        manual_analog_device->addItem(tr("None"), QString{});

        win32_joy_ctx joy_ctx;
        for (const auto& joy : joy_ctx.get_joy_info())
            manual_analog_device->addItem(joy.name + " " + joy.guid, joy.guid);

        tie_setting(main.manual_analog_guid, manual_analog_device,
                    [cb = manual_analog_device](const QString& guid) {
                        const int idx = cb->findData(guid);
                        return idx >= 0 ? idx : 0;
                    },
                    [](int, const QVariant& data) { return data.toString(); });
    }
#endif

    auto* output_grid = new QGridLayout;

    output_grid->addWidget(new QLabel(tr("Axis"), this), 0, 0);
    output_grid->addWidget(new QLabel(tr("Control"), this), 0, 1);
    output_grid->addWidget(new QLabel(tr("Min"), this), 0, 2);
    output_grid->addWidget(new QLabel(tr("Max"), this), 0, 3);
    output_grid->addWidget(new QLabel(tr("Speed"), this), 0, 4);
    output_grid->addWidget(new QLabel(tr("Detents"), this), 0, 5);
    output_grid->addWidget(new QLabel(tr("Points"), this), 0, 6);
    output_grid->addWidget(new QLabel(tr("Delay"), this), 0, 7);
    output_grid->addWidget(new QLabel(tr("Press again"), this), 0, 8);
    output_grid->addWidget(new QLabel(tr("Analog axis"), this), 0, 9);
    output_grid->addWidget(new QLabel(tr("Invert"), this), 0, 10);
    output_grid->addWidget(new QLabel(tr("Deadzone"), this), 0, 11);

    struct row_def
    {
        const char* label;
        manual_translation_axis_settings* axis;
        bool rotational;
    };
    const row_def rows[] = {
        { "X", &main.manual_x, false },
        { "Y", &main.manual_y, false },
        { "Z", &main.manual_z, false },
        { "Yaw", &main.manual_yaw, true },
    };

    for (int i = 0; i < int(sizeof(rows) / sizeof(rows[0])); i++)
    {
        const auto& row = rows[i];
        auto& widgets = manual_axes[i];

        widgets.mode = new QComboBox(this);
        widgets.mode->addItem(tr("Tracked"), translation_tracked);
        widgets.mode->addItem(tr("Tracked + manual keys"), translation_tracked_manual_keys);
        widgets.mode->addItem(tr("Manual keys"), translation_manual_keys);
#ifdef _WIN32
        widgets.mode->addItem(tr("Tracked + manual analog"), translation_tracked_manual_analog);
        widgets.mode->addItem(tr("Manual analog"), translation_manual_analog);
#endif
        widgets.mode->addItem(tr("Disabled"), translation_disabled);

        widgets.min = new QDoubleSpinBox(this);
        widgets.max = new QDoubleSpinBox(this);
        widgets.speed = new QDoubleSpinBox(this);
        widgets.detents_enabled = new QCheckBox(this);
        widgets.detent_positions = new QLineEdit(this);
        widgets.detent_delay = new QDoubleSpinBox(this);
        widgets.detent_press_to_pass = new QCheckBox(this);
        widgets.analog_axis = new QComboBox(this);
        widgets.analog_invert = new QCheckBox(this);
        widgets.analog_deadzone = new QDoubleSpinBox(this);

        for (QDoubleSpinBox* spin : { widgets.min, widgets.max, widgets.speed })
        {
            spin->setDecimals(1);
            spin->setSingleStep(1.0);
            spin->setRange(-600.0, 600.0);
            spin->setSuffix(row.rotational ? tr("°") : tr(" cm"));
        }

        if (row.rotational)
        {
            widgets.min->setRange(-180.0, 0.0);
            widgets.max->setRange(0.0, 180.0);
            widgets.speed->setRange(0.0, 360.0);
            widgets.speed->setSuffix(tr("°/s"));
            widgets.detent_positions->setPlaceholderText(tr("-135, 0, 135"));
            widgets.detent_positions->setToolTip(tr("Comma, semicolon, or space separated detent points in degrees; end a point with ! to make it a hard stop (e.g. 135!)"));
        }
        else
        {
            widgets.min->setRange(-600.0, 0.0);
            widgets.max->setRange(0.0, 600.0);
            widgets.speed->setRange(0.0, 600.0);
            widgets.speed->setSuffix(tr(" cm/s"));
            widgets.detent_positions->setPlaceholderText(tr("-33, 0, 33"));
            widgets.detent_positions->setToolTip(tr("Comma, semicolon, or space separated detent points in cm; end a point with ! to make it a hard stop (e.g. 33!)"));
        }

        widgets.detent_delay->setDecimals(2);
        widgets.detent_delay->setSingleStep(0.05);
        widgets.detent_delay->setRange(0.0, 10.0);
        widgets.detent_delay->setSuffix(tr(" s"));

        widgets.detent_press_to_pass->setToolTip(tr("Stop at each detent while held; release and press again to move to the next one"));

        widgets.analog_axis->addItem(tr("Disabled"), 0);
        for (int axis_idx = 1; axis_idx <= 8; axis_idx++)
            widgets.analog_axis->addItem(tr("Joystick axis #%1").arg(axis_idx), axis_idx);

        widgets.analog_deadzone->setDecimals(2);
        widgets.analog_deadzone->setSingleStep(0.01);
        widgets.analog_deadzone->setRange(0.0, 1.0);

        output_grid->addWidget(new QLabel(tr(row.label), this), i + 1, 0);
        output_grid->addWidget(widgets.mode, i + 1, 1);
        output_grid->addWidget(widgets.min, i + 1, 2);
        output_grid->addWidget(widgets.max, i + 1, 3);
        output_grid->addWidget(widgets.speed, i + 1, 4);
        output_grid->addWidget(widgets.detents_enabled, i + 1, 5);
        output_grid->addWidget(widgets.detent_positions, i + 1, 6);
        output_grid->addWidget(widgets.detent_delay, i + 1, 7);
        output_grid->addWidget(widgets.detent_press_to_pass, i + 1, 8);
        output_grid->addWidget(widgets.analog_axis, i + 1, 9);
        output_grid->addWidget(widgets.analog_invert, i + 1, 10);
        output_grid->addWidget(widgets.analog_deadzone, i + 1, 11);

        tie_setting(row.axis->mode, widgets.mode);
        tie_setting(row.axis->min, widgets.min);
        tie_setting(row.axis->max, widgets.max);
        tie_setting(row.axis->speed, widgets.speed);
        tie_setting(row.axis->detents_enabled, widgets.detents_enabled);
        tie_setting(row.axis->detent_positions, widgets.detent_positions);
        tie_setting(row.axis->detent_delay, widgets.detent_delay);
        tie_setting(row.axis->detent_press_to_pass, widgets.detent_press_to_pass);
        tie_setting(row.axis->analog_axis, widgets.analog_axis);
        tie_setting(row.axis->analog_invert, widgets.analog_invert);
        tie_setting(row.axis->analog_deadzone, widgets.analog_deadzone);
        connect(widgets.detents_enabled, &QCheckBox::toggled, this, [this](bool) { refresh_manual_translation_ui(); });
        connect(widgets.detent_press_to_pass, &QCheckBox::toggled, this, [this](bool) { refresh_manual_translation_ui(); });
        connect(widgets.mode,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this,
                [this](int) { refresh_manual_translation_ui(); });
    }

    output_group_layout->addLayout(output_grid);

    {
        auto* lean_grid = new QGridLayout;
        auto* lean_start = new QDoubleSpinBox(this);
        auto* lean_full = new QDoubleSpinBox(this);
        auto* lean_x = new QDoubleSpinBox(this);
        auto* lean_y = new QDoubleSpinBox(this);
        auto* lean_z = new QDoubleSpinBox(this);
        auto* lean_invert_x = new QCheckBox(tr("Invert sideways lean"), this);

        for (QDoubleSpinBox* spin : { lean_start, lean_full })
        {
            spin->setDecimals(1);
            spin->setSingleStep(1.0);
            spin->setRange(0.0, 180.0);
            spin->setSuffix(tr("°"));
        }

        for (QDoubleSpinBox* spin : { lean_x, lean_y, lean_z })
        {
            spin->setDecimals(1);
            spin->setSingleStep(1.0);
            spin->setRange(-50.0, 50.0);
            spin->setSuffix(tr(" cm"));
        }

        lean_grid->addWidget(new QLabel(tr("Yaw lean"), this), 1, 0);
        lean_grid->addWidget(new QLabel(tr("Lean start"), this), 0, 1);
        lean_grid->addWidget(new QLabel(tr("Lean full"), this), 0, 2);
        lean_grid->addWidget(new QLabel(tr("Lean sideways"), this), 0, 3);
        lean_grid->addWidget(new QLabel(tr("Lean up"), this), 0, 4);
        lean_grid->addWidget(new QLabel(tr("Lean forward"), this), 0, 5);
        lean_grid->addWidget(lean_start, 1, 1);
        lean_grid->addWidget(lean_full, 1, 2);
        lean_grid->addWidget(lean_x, 1, 3);
        lean_grid->addWidget(lean_y, 1, 4);
        lean_grid->addWidget(lean_z, 1, 5);
        lean_grid->addWidget(lean_invert_x, 1, 6);

        tie_setting(main.manual_yaw_lean.start_deg, lean_start);
        tie_setting(main.manual_yaw_lean.full_deg, lean_full);
        tie_setting(main.manual_yaw_lean.x_cm, lean_x);
        tie_setting(main.manual_yaw_lean.y_cm, lean_y);
        tie_setting(main.manual_yaw_lean.z_cm, lean_z);
        tie_setting(main.manual_yaw_lean.invert_x, lean_invert_x);

        output_group_layout->addLayout(lean_grid);
    }
    output_layout->insertWidget(1, output_group);

    auto* shortcuts_group = new QGroupBox(tr("Manual translation shortcuts"), this);
    auto* shortcuts_grid = new QGridLayout(shortcuts_group);

    shortcuts_grid->addWidget(new QLabel(tr("Action"), this), 0, 0);
    shortcuts_grid->addWidget(new QLabel(tr("Binding"), this), 0, 1);

    add_manual_shortcut_row(shortcuts_grid, 1, tr("Move X-"), manual_axes[0].negative_text, manual_axes[0].negative_bind);
    add_manual_shortcut_row(shortcuts_grid, 2, tr("Move X+"), manual_axes[0].positive_text, manual_axes[0].positive_bind);
    add_manual_shortcut_row(shortcuts_grid, 3, tr("Move Y-"), manual_axes[1].negative_text, manual_axes[1].negative_bind);
    add_manual_shortcut_row(shortcuts_grid, 4, tr("Move Y+"), manual_axes[1].positive_text, manual_axes[1].positive_bind);
    add_manual_shortcut_row(shortcuts_grid, 5, tr("Move Z-"), manual_axes[2].negative_text, manual_axes[2].negative_bind);
    add_manual_shortcut_row(shortcuts_grid, 6, tr("Move Z+"), manual_axes[2].positive_text, manual_axes[2].positive_bind);
    add_manual_shortcut_row(shortcuts_grid, 7, tr("Move Yaw-"), manual_axes[3].negative_text, manual_axes[3].negative_bind);
    add_manual_shortcut_row(shortcuts_grid, 8, tr("Move Yaw+"), manual_axes[3].positive_text, manual_axes[3].positive_bind);

    shortcuts_layout->insertWidget(1, shortcuts_group);
}

void options_dialog::setup_overlay_ui()
{
    auto* tab = new QWidget(this);
    auto* tab_layout = new QVBoxLayout(tab);

    {
        auto* group = new QGroupBox(tr("Status overlay"), tab);
        auto* form = new QFormLayout(group);

        auto* enabled = new QCheckBox(tr("Show the status overlay"), group);
        auto* only_while_tracking = new QCheckBox(tr("Only while tracking"), group);
        auto* show_modifiers = new QCheckBox(tr("Active modifiers"), group);
        auto* show_axes = new QCheckBox(tr("Axis table"), group);

        auto* text_size = new QComboBox(group);
        text_size->addItem(tr("Small"), overlay_text_small);
        text_size->addItem(tr("Medium"), overlay_text_medium);
        text_size->addItem(tr("Large"), overlay_text_large);
        text_size->addItem(tr("Extra large"), overlay_text_xl);

        auto* opacity = new QSpinBox(group);
        opacity->setRange(0, 100);
        opacity->setSingleStep(5);
        opacity->setSuffix(tr("%"));

        auto* contents = new QHBoxLayout;
        contents->addWidget(show_modifiers);
        contents->addWidget(show_axes);
        contents->addStretch();

        form->addRow(enabled);
        form->addRow(only_while_tracking);
        form->addRow(tr("Show"), contents);
        form->addRow(tr("Text size"), text_size);
        form->addRow(tr("Background opacity"), opacity);

        tie_setting(overlay.enabled, enabled);
        tie_setting(overlay.only_while_tracking, only_while_tracking);
        tie_setting(overlay.show_modifiers, show_modifiers);
        tie_setting(overlay.show_axes, show_axes);
        tie_setting(overlay.text_size, text_size);
        tie_setting(overlay.opacity, opacity);

        tab_layout->addWidget(group);
    }

    {
        auto* group = new QGroupBox(tr("Placement"), tab);
        auto* form = new QFormLayout(group);

        overlay_screen = new QComboBox(group);
        auto* identify = new QPushButton(tr("Identify"), group);
        identify->setToolTip(tr("Show each monitor's number on it"));
        auto* screen_row = new QHBoxLayout;
        screen_row->addWidget(overlay_screen, 1);
        screen_row->addWidget(identify);

        auto* anchor = new QComboBox(group);
        const std::pair<QString, overlay_anchor> anchors[] = {
            { tr("Top left"), overlay_top_left },
            { tr("Top"), overlay_top },
            { tr("Top right"), overlay_top_right },
            { tr("Left"), overlay_left },
            { tr("Center"), overlay_center },
            { tr("Right"), overlay_right },
            { tr("Bottom left"), overlay_bottom_left },
            { tr("Bottom"), overlay_bottom },
            { tr("Bottom right"), overlay_bottom_right },
        };
        for (const auto& [name, value] : anchors)
            anchor->addItem(name, value);

        auto* offset_x = new QSpinBox(group);
        auto* offset_y = new QSpinBox(group);
        for (QSpinBox* spin : { offset_x, offset_y })
        {
            spin->setRange(-9999, 9999);
            spin->setSuffix(tr(" px"));
            spin->setToolTip(tr("Distance from the anchored edge of the monitor; for a centered anchor, a shift right or down"));
        }
        auto* offset_row = new QHBoxLayout;
        offset_row->addWidget(new QLabel(tr("X"), group));
        offset_row->addWidget(offset_x);
        offset_row->addWidget(new QLabel(tr("Y"), group));
        offset_row->addWidget(offset_y);
        offset_row->addStretch();

        overlay_place = new QPushButton(tr("Drag to place"), group);
        overlay_place->setCheckable(true);
        overlay_place->setToolTip(tr("Shows the overlay so it can be dragged with the mouse; it snaps to the nearest anchor of the monitor it's dropped on"));

        form->addRow(tr("Monitor"), screen_row);
        form->addRow(tr("Anchor"), anchor);
        form->addRow(tr("Offset"), offset_row);
        form->addRow(QString(), overlay_place);

        tie_setting(overlay.anchor, anchor);
        tie_setting(overlay.offset_x, offset_x);
        tie_setting(overlay.offset_y, offset_y);

        connect(identify, &QPushButton::clicked, this, [] { status_overlay::identify_screens(); });
        connect(overlay_place, &QPushButton::toggled, this, [this](bool on) {
            if (overlay_place_mode)
                overlay_place_mode(on);
        });
        connect(overlay_screen,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this,
                [this](int idx) {
                    const QList<QScreen*> screens = QGuiApplication::screens();
                    const int i = overlay_screen->itemData(idx).toInt();
                    // -1 is a saved monitor that isn't connected; keep it
                    if (i < 0 || i >= int(screens.size()))
                        return;
                    const auto id = status_overlay::identity(screens[i]);
                    overlay.screen_name = id.name;
                    overlay.screen_manufacturer = id.manufacturer;
                    overlay.screen_model = id.model;
                    overlay.screen_serial = id.serial;
                });

        connect(qGuiApp, &QGuiApplication::screenAdded, this, &options_dialog::refresh_overlay_screens);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &options_dialog::refresh_overlay_screens);
        connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &options_dialog::refresh_overlay_screens);
        // dropping the overlay in place mode, or a reload, can change the monitor
        connect(&*overlay.b, &options::bundle_::changed, this, &options_dialog::refresh_overlay_screens);
        refresh_overlay_screens();

        tab_layout->addWidget(group);
    }

    {
        auto* group = new QGroupBox(tr("Shortcuts"), tab);
        auto* grid = new QGridLayout(group);
        QLabel* text1 = nullptr;
        QLabel* text2 = nullptr;
        QPushButton* bind1 = nullptr;
        QPushButton* bind2 = nullptr;

        add_manual_shortcut_row(grid, 0, tr("Show/hide overlay"), text1, bind1);
        add_manual_shortcut_row(grid, 1, tr("Show/hide overlay (alt)"), text2, bind2);
        grid->setColumnStretch(1, 1);
        connect_binding_controls(overlay.key_toggle1, text1, bind1);
        connect_binding_controls(overlay.key_toggle2, text2, bind2);

        tab_layout->addWidget(group);
    }

    tab_layout->addStretch();
    ui.tabWidget->addTab(tab, tr("Overlay"));
}

void options_dialog::refresh_overlay_screens()
{
    if (!overlay_screen)
        return;

    const QSignalBlocker blocker(overlay_screen);
    overlay_screen->clear();

    const QList<QScreen*> screens = QGuiApplication::screens();
    std::vector<overlay_placement::screen_identity> ids;
    for (int i = 0; i < int(screens.size()); i++)
    {
        overlay_screen->addItem(status_overlay::screen_label(screens[i], i), i);
        ids.push_back(status_overlay::identity(screens[i]));
    }

    const overlay_placement::screen_identity saved {
        overlay.screen_name, overlay.screen_manufacturer, overlay.screen_model, overlay.screen_serial,
    };
    int idx = overlay_placement::match_screen(saved, ids);

    if (idx < 0 && saved != overlay_placement::screen_identity {})
    {
        // the overlay is on the primary monitor meanwhile, but goes back once this one is connected
        QString name = QStringLiteral("%1 %2").arg(saved.manufacturer, saved.model).simplified();
        if (name.isEmpty())
            name = saved.name;
        overlay_screen->addItem(tr("%1 (not connected)").arg(name), -1);
        idx = overlay_screen->count() - 1;
    }
    else if (idx < 0)
        idx = int(screens.indexOf(QGuiApplication::primaryScreen()));

    overlay_screen->setCurrentIndex(idx);
}

void options_dialog::refresh_manual_translation_ui()
{
    QComboBox* sources[] { ui.src_x, ui.src_y, ui.src_z, ui.src_yaw };
    QCheckBox* preinvert[] { ui.invert_x_pre, ui.invert_y_pre, ui.invert_z_pre, ui.invert_yaw_pre };
    bool any_manual_analog = false;

    for (int i = 0; i < int(manual_axes.size()); i++)
    {
        const auto mode = translation_control_mode((*main.manual_translation_axes[i]).mode());
        const bool tracked = uses_tracking(mode);
        const bool manual_keys = uses_manual_keys(mode);
        const bool manual_analog = uses_manual_analog(mode);
        any_manual_analog = any_manual_analog || manual_analog;

        sources[i]->setEnabled(tracked);
        preinvert[i]->setEnabled(tracked);

        manual_axes[i].min->setEnabled(manual_keys || manual_analog);
        manual_axes[i].max->setEnabled(manual_keys || manual_analog);
        manual_axes[i].speed->setEnabled(manual_keys);
        manual_axes[i].detents_enabled->setEnabled(manual_keys);
        manual_axes[i].detent_positions->setEnabled(manual_keys && (*main.manual_translation_axes[i]).detents_enabled());
        manual_axes[i].detent_delay->setEnabled(manual_keys && (*main.manual_translation_axes[i]).detents_enabled() &&
                                                !(*main.manual_translation_axes[i]).detent_press_to_pass());
        manual_axes[i].detent_press_to_pass->setEnabled(manual_keys && (*main.manual_translation_axes[i]).detents_enabled());
        manual_axes[i].analog_axis->setEnabled(manual_analog);
        manual_axes[i].analog_invert->setEnabled(manual_analog);
        manual_axes[i].analog_deadzone->setEnabled(manual_analog);
        manual_axes[i].negative_text->setEnabled(manual_keys);
        manual_axes[i].negative_bind->setEnabled(manual_keys);
        manual_axes[i].positive_text->setEnabled(manual_keys);
        manual_axes[i].positive_bind->setEnabled(manual_keys);
    }

    if (manual_analog_device)
        manual_analog_device->setEnabled(any_manual_analog);
}

options_dialog::options_dialog(std::unique_ptr<ITrackerDialog>& tracker_dialog_,
                               std::unique_ptr<IProtocolDialog>& proto_dialog_,
                               std::unique_ptr<IFilterDialog>& filter_dialog_,
                               std::function<void(bool)> pause_keybindings,
                               std::function<void(bool)> overlay_place_mode) :
    pause_keybindings(std::move(pause_keybindings)),
    overlay_place_mode(std::move(overlay_place_mode))
{
    ui.setupUi(this);

    tie_setting(main.tray_enabled, ui.trayp);
    tie_setting(main.tray_start, ui.tray_start);

    tie_setting(main.center_at_startup, ui.center_at_startup);

    const centering_state centering_modes[] = {
        center_disabled,
        center_point,
        center_vr360,
        center_roll_compensated,
    };
    for (int k = 0; k < 4; k++)
        ui.cbox_centering->setItemData(k, centering_modes[k]);
    tie_setting(main.centering_mode, ui.cbox_centering);

    const reltrans_state reltrans_modes[] = {
        reltrans_disabled,
        reltrans_enabled,
        reltrans_non_center,
    };

    for (int k = 0; k < 3; k++)
        ui.reltrans_mode->setItemData(k, reltrans_modes[k]);

    tie_setting(main.apply_mapping_curves, ui.apply_mapping_curves);

    tie_setting(main.reltrans_mode, ui.reltrans_mode);

    tie_setting(main.reltrans_disable_tx, ui.tcomp_tx_disable);
    tie_setting(main.reltrans_disable_ty, ui.tcomp_ty_disable);
    tie_setting(main.reltrans_disable_tz, ui.tcomp_tz_disable);

    tie_setting(main.reltrans_disable_src_yaw, ui.tcomp_src_yaw_disable);
    tie_setting(main.reltrans_disable_src_pitch, ui.tcomp_src_pitch_disable);
    tie_setting(main.reltrans_disable_src_roll, ui.tcomp_src_roll_disable);

    tie_setting(main.neck_z, ui.neck_z);
    tie_setting(main.precision_yaw_scale, ui.precision_yaw_scale);
    tie_setting(main.precision_pitch_scale, ui.precision_pitch_scale);
    tie_setting(main.precision_roll_scale, ui.precision_roll_scale);
    tie_setting(main.tailview.min_yaw_deg, ui.tailview_min_yaw_deg);
    tie_setting(main.tailview.center_yaw_deg, ui.tailview_center_yaw_deg);
    tie_setting(main.tailview.max_yaw_deg, ui.tailview_max_yaw_deg);
    tie_setting(main.tailview.center_deadzone_deg, ui.tailview_center_deadzone_deg);
    tie_setting(main.tailview.precision_yaw_scale, ui.tailview_precision_yaw_scale);
    tie_setting(main.tailview.precision_pitch_scale, ui.tailview_precision_pitch_scale);
    tie_setting(main.tailview.precision_roll_scale, ui.tailview_precision_roll_scale);
    tie_setting(main.tailview.lean_x_cm, ui.tailview_lean_x_cm);
    tie_setting(main.tailview.lean_y_cm, ui.tailview_lean_y_cm);
    tie_setting(main.tailview.lean_z_cm, ui.tailview_lean_z_cm);
    tie_setting(main.tailview.lean_time_ms, ui.tailview_lean_time_ms);
    tie_setting(main.tailview.lean_invert_x, ui.tailview_lean_invert_x);
    tie_setting(main.upview.center_pitch_deg, ui.upview_center_pitch_deg);
    tie_setting(main.upview.center_deadzone_deg, ui.upview_center_deadzone_deg);
    tie_setting(main.upview.precision_yaw_scale, ui.upview_precision_yaw_scale);
    tie_setting(main.upview.precision_pitch_scale, ui.upview_precision_pitch_scale);
    tie_setting(main.upview.precision_roll_scale, ui.upview_precision_roll_scale);

    tie_setting(main.a_x.zero, ui.pos_tx);
    tie_setting(main.a_y.zero, ui.pos_ty);
    tie_setting(main.a_z.zero, ui.pos_tz);
    tie_setting(main.a_yaw.zero, ui.pos_rx);
    tie_setting(main.a_pitch.zero, ui.pos_ry);
    tie_setting(main.a_roll.zero, ui.pos_rz);

    tie_setting(main.a_yaw.invert_pre, ui.invert_yaw_pre);
    tie_setting(main.a_pitch.invert_pre, ui.invert_pitch_pre);
    tie_setting(main.a_roll.invert_pre, ui.invert_roll_pre);
    tie_setting(main.a_x.invert_pre, ui.invert_x_pre);
    tie_setting(main.a_y.invert_pre, ui.invert_y_pre);
    tie_setting(main.a_z.invert_pre, ui.invert_z_pre);

    tie_setting(main.a_yaw.invert_post, ui.invert_yaw_post);
    tie_setting(main.a_pitch.invert_post, ui.invert_pitch_post);
    tie_setting(main.a_roll.invert_post, ui.invert_roll_post);
    tie_setting(main.a_x.invert_post, ui.invert_x_post);
    tie_setting(main.a_y.invert_post, ui.invert_y_post);
    tie_setting(main.a_z.invert_post, ui.invert_z_post);

    tie_setting(main.a_yaw.src, ui.src_yaw);
    tie_setting(main.a_pitch.src, ui.src_pitch);
    tie_setting(main.a_roll.src, ui.src_roll);
    tie_setting(main.a_x.src, ui.src_x);
    tie_setting(main.a_y.src, ui.src_y);
    tie_setting(main.a_z.src, ui.src_z);

    tie_setting(main.enable_camera_offset, ui.enable_camera_offset);
    tie_setting(main.camera_offset_yaw,   ui.camera_offset_yaw);
    tie_setting(main.camera_offset_pitch, ui.camera_offset_pitch);
    tie_setting(main.camera_offset_roll,  ui.camera_offset_roll);
    tie_setting(main.camera_offset_x,  ui.camera_offset_x);
    tie_setting(main.camera_offset_y,  ui.camera_offset_y);
    tie_setting(main.camera_offset_z,  ui.camera_offset_z);

    //tie_setting(main.center_method, ui.center_method);

    tie_setting(main.tracklogging_enabled, ui.tracklogging_enabled);

    tie_setting(main.neck_enable, ui.neck_enable);
    tie_setting(main.neck_deferred_yaw, ui.neck_deferred_yaw);

    setup_manual_translation_ui();

    const bool is_translation_disabled = with_global_settings_object([] (QSettings& s) {
        return s.value("disable-translation", false).toBool();
    });
    ui.disable_translation->setChecked(is_translation_disabled);

    struct tmp
    {
        key_opts& opt;
        QLabel* label;
        QPushButton* button;
    } tuples[] =
    {
        { main.key_center1, ui.center_text, ui.bind_center },
        { main.key_center2, ui.center_text_2, ui.bind_center_2 },

        { main.key_toggle1, ui.toggle_text, ui.bind_toggle },
        { main.key_toggle2, ui.toggle_text_2, ui.bind_toggle_2 },

        { main.key_toggle_press1, ui.toggle_held_text, ui.bind_toggle_held },
        { main.key_toggle_press2, ui.toggle_held_text_2, ui.bind_toggle_held_2 },

        { main.key_zero1, ui.zero_text, ui.bind_zero },
        { main.key_zero2, ui.zero_text_2, ui.bind_zero_2 },

        { main.key_zero_press1, ui.zero_held_text, ui.bind_zero_held },
        { main.key_zero_press2, ui.zero_held_text_2, ui.bind_zero_held_2 },

        { main.key_precision1, ui.precision_text, ui.bind_precision },
        { main.key_precision2, ui.precision_text_2, ui.bind_precision_2 },

        { main.key_precision_toggle1, ui.precision_toggle_text, ui.bind_precision_toggle },
        { main.key_precision_toggle2, ui.precision_toggle_text_2, ui.bind_precision_toggle_2 },

        { main.key_tailview_left1, ui.tailview_left_text, ui.bind_tailview_left },
        { main.key_tailview_left2, ui.tailview_left_text_2, ui.bind_tailview_left_2 },

        { main.key_tailview_right1, ui.tailview_right_text, ui.bind_tailview_right },
        { main.key_tailview_right2, ui.tailview_right_text_2, ui.bind_tailview_right_2 },

        { main.key_upview1, ui.upview_text, ui.bind_upview },
        { main.key_upview2, ui.upview_text_2, ui.bind_upview_2 },

        { main.key_start_tracking1, ui.start_tracking_text, ui.bind_start },
        { main.key_start_tracking2, ui.start_tracking_text_2, ui.bind_start_2 },

        { main.key_stop_tracking1, ui.stop_tracking_text , ui.bind_stop },
        { main.key_stop_tracking2, ui.stop_tracking_text_2 , ui.bind_stop_2 },

        { main.key_toggle_tracking1, ui.toggle_tracking_text, ui.bind_toggle_tracking },
        { main.key_toggle_tracking2, ui.toggle_tracking_text_2, ui.bind_toggle_tracking_2 },

        { main.key_restart_tracking1, ui.restart_tracking_text, ui.bind_restart_tracking },
        { main.key_restart_tracking2, ui.restart_tracking_text_2, ui.bind_restart_tracking_2 },
    };

    for (const tmp& val_ : tuples)
    {
        tmp val = val_;
        connect_binding_controls(val.opt, val.label, val.button);
    }

    connect_binding_controls(main.manual_x.negative_key, manual_axes[0].negative_text, manual_axes[0].negative_bind);
    connect_binding_controls(main.manual_x.positive_key, manual_axes[0].positive_text, manual_axes[0].positive_bind);
    connect_binding_controls(main.manual_y.negative_key, manual_axes[1].negative_text, manual_axes[1].negative_bind);
    connect_binding_controls(main.manual_y.positive_key, manual_axes[1].positive_text, manual_axes[1].positive_bind);
    connect_binding_controls(main.manual_z.negative_key, manual_axes[2].negative_text, manual_axes[2].negative_bind);
    connect_binding_controls(main.manual_z.positive_key, manual_axes[2].positive_text, manual_axes[2].positive_bind);
    connect_binding_controls(main.manual_yaw.negative_key, manual_axes[3].negative_text, manual_axes[3].negative_bind);
    connect_binding_controls(main.manual_yaw.positive_key, manual_axes[3].positive_text, manual_axes[3].positive_bind);
    refresh_manual_translation_ui();
    setup_overlay_ui();

    auto add_module_tab = [this] (auto& place, auto&& dlg, const QString& label) {
        if (dlg && dlg->embeddable())
        {
            using BaseDialog = plugin_api::detail::BaseDialog;

            dlg->set_buttons_visible(false);
            place = dlg.release();
            ui.tabWidget->addTab(place, label);
            QObject::connect(place, &BaseDialog::closing, this, &QDialog::close);
        }
    };

    add_module_tab(tracker_dialog, tracker_dialog_, tr("Tracker"));
    add_module_tab(proto_dialog, proto_dialog_, tr("Output"));
    add_module_tab(filter_dialog, filter_dialog_, tr("Filter"));

    connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &options_dialog::accept);
    connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &options_dialog::reject);
    connect(this, &options_dialog::accepted, this, &options_dialog::doAccept);
    connect(this, &options_dialog::rejected, this, &options_dialog::doReject);
}

void options_dialog::bind_key(key_opts& kopts, QLabel* label)
{
    kopts.button = -1;
    kopts.guid = {};
    kopts.keycode = {};

    auto* k = new keyboard_listener;
    k->deleteLater();
    k->setWindowModality(Qt::ApplicationModal);

    {
        QObject obj;
        connect(&*k, &keyboard_listener::key_pressed,
                &obj,
                [&](const QKeySequence& s)
                {
                    kopts.keycode = s.toString(QKeySequence::PortableText);
                    kopts.guid = {};
                    kopts.button = -1;
                    k->close();
                });
        connect(&*k, &keyboard_listener::joystick_button_pressed,
                &obj,
                [&](const QString& guid, int idx, bool held)
                {
                    if (!k)
                        std::abort();
                    if (!held)
                    {
                        kopts.guid = guid;
                        kopts.keycode = {};
                        kopts.button = idx;
                        k->close();
                    }
                });
        connect(&*main.b, &options::detail::bundle::reloading, &*k, &QDialog::close);
        pause_keybindings(false);
        k->exec();
        k = nullptr;
        pause_keybindings(true);
    }
    const bool is_crap = progn(
        for (const QChar& c : kopts.keycode())
            if (!c.isPrint())
                return true;
        return false;
    );
    if (is_crap)
    {
        kopts.keycode = {};
        kopts.guid = {};
        kopts.button = -1;
        label->setText(tr("None"));
    }
    else
        label->setText(kopts_to_string(kopts));
    //qDebug() << "bind_key done" << kopts.guid << kopts.button << kopts.keycode;
}

void options_dialog::switch_to_tracker_tab()
{
    if (tracker_dialog)
        ui.tabWidget->setCurrentWidget(tracker_dialog);
    else
        eval_once(qDebug() << "options: asked for tracker tab widget with old-style widget dialog!");
}

void options_dialog::switch_to_proto_tab()
{
    if (proto_dialog)
        ui.tabWidget->setCurrentWidget(proto_dialog);
    else
        eval_once(qDebug() << "options: asked for proto tab widget with old-style widget dialog!");
}

void options_dialog::switch_to_filter_tab()
{
    if (filter_dialog)
        ui.tabWidget->setCurrentWidget(filter_dialog);
    else
        eval_once(qDebug() << "options: asked for filter tab widget with old-style widget dialog!");
}

void options_dialog::tracker_module_changed()
{
    if (tracker_dialog)
    {
        unregister_tracker();
        reload();
        delete tracker_dialog;
        tracker_dialog = nullptr;
    }
}

void options_dialog::proto_module_changed()
{
    if (proto_dialog)
    {
        unregister_protocol();
        reload();
        delete proto_dialog;
        proto_dialog = nullptr;
    }
}

void options_dialog::filter_module_changed()
{
    if (filter_dialog)
    {
        unregister_filter();
        reload();
        delete filter_dialog;
        filter_dialog = nullptr;
    }
}

void options_dialog::register_tracker(ITracker* t)
{
    if (tracker_dialog)
        tracker_dialog->register_tracker(t);
}

void options_dialog::unregister_tracker()
{
    if (tracker_dialog)
        tracker_dialog->unregister_tracker();
}

void options_dialog::register_protocol(IProtocol* p)
{
    if (proto_dialog)
        proto_dialog->register_protocol(p);
}

void options_dialog::unregister_protocol()
{
    if (proto_dialog)
        proto_dialog->unregister_protocol();
}

void options_dialog::register_filter(IFilter* f)
{
    if (filter_dialog)
        filter_dialog->register_filter(f);
}

void options_dialog::unregister_filter()
{
    if (filter_dialog)
        filter_dialog->unregister_filter();
}

using module_list = std::initializer_list<plugin_api::detail::BaseDialog*>;

void options_dialog::save()
{
    qDebug() << "options_dialog: save";
    main.b->save();
    overlay.b->save();
    ui.game_detector->save();
    set_disable_translation_state(ui.disable_translation->isChecked());

    for (auto* dlg : module_list{ tracker_dialog, proto_dialog, filter_dialog })
        if (dlg)
            dlg->save();
}

void options_dialog::reload()
{
    qDebug() << "options_dialog: reload";
    ui.game_detector->revert();

    main.b->reload();
    overlay.b->reload();
    for (auto* dlg : module_list{ tracker_dialog, proto_dialog, filter_dialog })
        if (dlg)
            dlg->reload();
}

void options_dialog::closeEvent(QCloseEvent *)
{
    qDebug() << "options_dialog: closeEvent";
    reject();
    emit closing();
}

void options_dialog::doOK()
{
    qDebug() << "options_dialog: doOK";
    accept();
}
void options_dialog::doCancel()
{
    qDebug() << "options_dialog: doCancel";
    reject();
}

void options_dialog::doAccept()
{
    qDebug() << "options_dialog: doAccept";
    if (overlay_place)
        overlay_place->setChecked(false);
    save();
}

void options_dialog::doReject()
{
    qDebug() << "options_dialog: doReject";
    if (overlay_place)
        overlay_place->setChecked(false);
    reload();
}

options_dialog::~options_dialog()
{
    if (tracker_dialog)
        tracker_dialog->unregister_tracker();
    if (proto_dialog)
        proto_dialog->unregister_protocol();
    if (filter_dialog)
        filter_dialog->unregister_filter();
}
