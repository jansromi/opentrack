/* Copyright (c) 2015, Stanislaw Halik <sthalik@misaki.pl>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */

#include "options-dialog.hpp"
#include "listener.h"

#include <utility>

#include <QPushButton>
#include <QLayout>
#include <QDialog>
#include <QFileDialog>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#ifdef _WIN32
#include "input/win32-joystick.hpp"
#endif

using namespace options;
using namespace options::globals;

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
    output_grid->addWidget(new QLabel(tr("Analog axis"), this), 0, 5);
    output_grid->addWidget(new QLabel(tr("Invert"), this), 0, 6);
    output_grid->addWidget(new QLabel(tr("Deadzone"), this), 0, 7);

    struct row_def { const char* label; manual_translation_axis_settings* axis; };
    const row_def rows[] = {
        { "X", &main.manual_x },
        { "Y", &main.manual_y },
        { "Z", &main.manual_z },
    };

    for (int i = 0; i < 3; i++)
    {
        const auto& row = rows[i];
        auto& widgets = manual_axes[i];

        widgets.mode = new QComboBox(this);
        widgets.mode->addItem(tr("Tracked"), translation_tracked);
        widgets.mode->addItem(tr("Manual keys"), translation_manual_keys);
#ifdef _WIN32
        widgets.mode->addItem(tr("Manual analog"), translation_manual_analog);
#endif
        widgets.mode->addItem(tr("Disabled"), translation_disabled);

        widgets.min = new QDoubleSpinBox(this);
        widgets.max = new QDoubleSpinBox(this);
        widgets.speed = new QDoubleSpinBox(this);
        widgets.analog_axis = new QComboBox(this);
        widgets.analog_invert = new QCheckBox(this);
        widgets.analog_deadzone = new QDoubleSpinBox(this);

        for (QDoubleSpinBox* spin : { widgets.min, widgets.max, widgets.speed })
        {
            spin->setDecimals(1);
            spin->setSingleStep(1.0);
            spin->setRange(-600.0, 600.0);
            spin->setSuffix(tr(" cm"));
        }

        widgets.min->setRange(-600.0, 0.0);
        widgets.max->setRange(0.0, 600.0);
        widgets.speed->setRange(0.0, 600.0);
        widgets.speed->setSuffix(tr(" cm/s"));

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
        output_grid->addWidget(widgets.analog_axis, i + 1, 5);
        output_grid->addWidget(widgets.analog_invert, i + 1, 6);
        output_grid->addWidget(widgets.analog_deadzone, i + 1, 7);

        tie_setting(row.axis->mode, widgets.mode);
        tie_setting(row.axis->min, widgets.min);
        tie_setting(row.axis->max, widgets.max);
        tie_setting(row.axis->speed, widgets.speed);
        tie_setting(row.axis->analog_axis, widgets.analog_axis);
        tie_setting(row.axis->analog_invert, widgets.analog_invert);
        tie_setting(row.axis->analog_deadzone, widgets.analog_deadzone);
        tie_setting(row.axis->mode, this, [this](translation_control_mode) { refresh_manual_translation_ui(); });
    }

    output_group_layout->addLayout(output_grid);
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

    shortcuts_layout->insertWidget(1, shortcuts_group);
}

void options_dialog::refresh_manual_translation_ui()
{
    QComboBox* sources[] { ui.src_x, ui.src_y, ui.src_z };
    QCheckBox* preinvert[] { ui.invert_x_pre, ui.invert_y_pre, ui.invert_z_pre };
    bool any_manual_analog = false;

    for (int i = 0; i < 3; i++)
    {
        const auto mode = (*main.manual_translation_axes[i]).mode();
        const bool tracked = mode == translation_tracked;
        const bool manual_keys = mode == translation_manual_keys;
        const bool manual_analog = mode == translation_manual_analog;
        any_manual_analog = any_manual_analog || manual_analog;

        sources[i]->setEnabled(tracked);
        preinvert[i]->setEnabled(tracked);

        manual_axes[i].min->setEnabled(manual_keys || manual_analog);
        manual_axes[i].max->setEnabled(manual_keys || manual_analog);
        manual_axes[i].speed->setEnabled(manual_keys);
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
                               std::function<void(bool)> pause_keybindings) :
    pause_keybindings(std::move(pause_keybindings))
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
    refresh_manual_translation_ui();

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
    save();
}

void options_dialog::doReject()
{
    qDebug() << "options_dialog: doReject";
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
