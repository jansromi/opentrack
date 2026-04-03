#pragma once

#include <vector>

#include "compat/timer.hpp"
#include "api/plugin-support.hpp"
#include "mappings.hpp"
#include "compat/euler.hpp"
#include "compat/dquat.hpp"
#include "compat/enum-operators.hpp"
#include "runtime-libraries.hpp"

#include "spline/spline.hpp"
#include "main-settings.hpp"
#include "options/options.hpp"
#include "tracklogger.hpp"

#ifdef _WIN32
#include "input/win32-joystick.hpp"
#endif

#include <QMutex>
#include <QThread>

#include <atomic>
#include <array>
#include <cmath>
#include <utility>

#include "export.hpp"

namespace pipeline_impl {

using namespace euler;
using namespace time_units;

using vec6_bool = Mat<bool, 6, 1>;
using vec3_bool = Mat<bool, 6, 1>;

class reltrans
{
    Pose_ interp_pos;
    Timer interp_timer;

    // this implements smooth transition into reltrans mode
    // once not aiming anymore. see `apply_pipeline()'.
    Timer interp_phase_timer;
    unsigned RC_stage = 0;

    bool moving_to_reltans = false;
    bool in_zone = false;

public:
    reltrans();

    void on_center();

    Pose_ rotate(const rmat& rmat, const Pose_& in, vec3_bool disable) const;
    Pose_ apply_neck(const rmat& R, int nz, bool disable_tz, bool deferred_yaw) const;
    Pose apply_pipeline(reltrans_state state, const Pose& value,
                        const vec6_bool& disable, bool neck_enable, int neck_z,
                        bool neck_deferred_yaw);
};

enum bit_flags : unsigned {
    f_none           = 0,
    f_center         = 1 << 0,
    f_held_center    = 1 << 1,
    f_enabled_h      = 1 << 2,
    f_enabled_p      = 1 << 3,
    f_zero           = 1 << 4,
    f_precision      = 1 << 5,
};

struct OTR_LOGIC_EXPORT bits
{
    bit_flags flags{0};
    mutable QMutex lock;

    void set(bit_flags flag, bool val);
    void negate(bit_flags flag);
    bool get(bit_flags flag) const;
    bits();
};

DEFINE_ENUM_OPERATORS(bit_flags);

class OTR_LOGIC_EXPORT manual_translation final
{
    std::array<std::atomic_bool, 3> negative_held, positive_held;
    std::array<double, 3> positions {};
    bool timer_started = false;
    Timer timer;
#ifdef _WIN32
    win32_joy_ctx joy_ctx;
#endif

    static int axis_index(Axis axis);
    static std::pair<double, double> limits(const manual_translation_axis_settings& axis);
#ifdef _WIN32
    bool poll_analog_axes(const main_settings& s, int* axes);
#endif

public:
    manual_translation();

    void set_input(Axis axis, bool positive, bool held);
    void reset();
    Pose apply(const main_settings& s, const Pose& value, bool frozen);
};

class OTR_LOGIC_EXPORT pipeline : private QThread
{
    Q_OBJECT

    mutable QMutex mtx;
    main_settings s;
    const Mappings& m;

    Timer t;
    // Pose members are prefixed to avoid confusion since there are so many
    // pose variables.
    Pose m_output_pose, m_raw_6dof, m_last_value, m_newpose;

    runtime_libraries const& libs;
    // The owner of the reference is the main window.
    // This design might be useful if we decide later on to swap out
    // the logger while the tracker is running.
    TrackLogger& logger;

    reltrans rel;
    manual_translation manual;

    struct {
        Pose P;
        dquat QC, QR, camera;
    } center;

    struct {
        bool was_active = false;
        Pose input_anchor, output_anchor, committed_offset;
    } precision;

    time_units::ms backlog_time {};

    bool tracking_started = false;

    static double map(double pos, const Map& axis);
    void logic();
    void run() override;
    bool maybe_enable_center_on_tracking_started();
    void maybe_set_center_pose(const centering_state mode, const Pose& value, bool own_center_logic);
    void clear_precision();
    Pose apply_center(const centering_state mode, Pose value) const;
    Pose apply_camera_offset(Pose value) const;
    std::tuple<Pose, Pose, vec6_bool> get_selected_axis_values(const Pose& newpose) const;
    Pose maybe_apply_filter(const Pose& value) const;
    Pose apply_reltrans(Pose value, vec6_bool disabled, bool centerp);
    Pose apply_precision(Pose value);
    Pose apply_zero_pos(Pose value) const;

    bits b;

public:
    pipeline(const Mappings& m, const runtime_libraries& libs, TrackLogger& logger);
    ~pipeline() override;

    void raw_and_mapped_pose(double* mapped, double* raw) const;
    void start() { QThread::start(QThread::HighPriority); }

    void toggle_zero();
    bool is_zero() const;
    void toggle_enabled();
    bool is_enabled() const;

    void set_center(bool value);
    void set_held_center(bool value);
    void set_enabled(bool value);
    void set_zero(bool value);
    void set_precision(bool value);
    void set_manual_translation_input(Axis axis, bool positive, bool held);
};

} // ns pipeline_impl

using pipeline = pipeline_impl::pipeline;
