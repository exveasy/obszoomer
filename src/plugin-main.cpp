/*
 * OBS Smooth Zoom (GPL-2.0-or-later)
 * A small native Qt6/libobs filter + editor wheel controller.
 * Inspired by the general cursor-anchored zoom technique; implementation original.
 */
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <mutex>

#include "zoom_math.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-smooth-zoom", "en-US")

namespace {
constexpr const char *kFilterType = "obs_smooth_zoom_filter_v1";
constexpr const char *kFilterName = "OBS Smooth Zoom";
constexpr double kPreviewMarginPx = 10.0;

struct Options {
    int modifier = int(Qt::NoModifier);
    double wheelFactor = 1.10;
    double maxZoom = 5.0;
    int smoothMs = 180;
    bool cursorAnchor = true;
};
Options options;
QAction *settingsAction = nullptr;

struct ZoomData {
    obs_source_t *context = nullptr;
    gs_effect_t *effect = nullptr;
    gs_eparam_t *offsetParam = nullptr;
    gs_eparam_t *zoomParam = nullptr;
    gs_eparam_t *imageParam = nullptr;
    gs_samplerstate_t *sampler = nullptr;
    std::mutex lock;
    smooth_zoom::View current;
    smooth_zoom::View target;
    int smoothMs = 180;
    bool initialized = false;
};

static QWidget *main_window()
{
    return static_cast<QWidget *>(obs_frontend_get_main_window());
}

static QSettings store()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("OBS-Smooth-Zoom"), QStringLiteral("Settings"));
}

static void load_options()
{
    QSettings s = store();
    options.modifier = s.value(QStringLiteral("modifier"), int(Qt::NoModifier)).toInt();
    if (options.modifier != int(Qt::NoModifier) && options.modifier != int(Qt::ControlModifier))
        options.modifier = int(Qt::NoModifier);
    options.wheelFactor = std::clamp(s.value(QStringLiteral("wheel_factor"), 1.10).toDouble(), 1.01, 1.50);
    options.maxZoom = std::clamp(s.value(QStringLiteral("max_zoom"), 5.0).toDouble(), 1.5, 10.0);
    options.smoothMs = std::clamp(s.value(QStringLiteral("smooth_ms"), 180).toInt(), 0, 500);
    options.cursorAnchor = s.value(QStringLiteral("cursor_anchor"), true).toBool();
}

static void save_options()
{
    QSettings s = store();
    s.setValue(QStringLiteral("modifier"), options.modifier);
    s.setValue(QStringLiteral("wheel_factor"), options.wheelFactor);
    s.setValue(QStringLiteral("max_zoom"), options.maxZoom);
    s.setValue(QStringLiteral("smooth_ms"), options.smoothMs);
    s.setValue(QStringLiteral("cursor_anchor"), options.cursorAnchor);
    s.sync();
}

class ZoomOptionsDialog : public QDialog {
public:
    explicit ZoomOptionsDialog(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("OBS Smooth Zoom — настройки"));
        auto *form = new QFormLayout(this);
        mode = new QComboBox(this);
        mode->addItem(QStringLiteral("Просто колесико"), int(Qt::NoModifier));
        mode->addItem(QStringLiteral("Ctrl + колесико"), int(Qt::ControlModifier));
        mode->setCurrentIndex(options.modifier == int(Qt::NoModifier) ? 0 : 1);
        form->addRow(QStringLiteral("Управление"), mode);

        factor = new QDoubleSpinBox(this);
        factor->setRange(1.01, 1.50);
        factor->setSingleStep(.01);
        factor->setDecimals(2);
        factor->setValue(options.wheelFactor);
        factor->setSuffix(QStringLiteral("× / шаг"));
        form->addRow(QStringLiteral("Чувствительность"), factor);

        limit = new QDoubleSpinBox(this);
        limit->setRange(1.5, 10.0);
        limit->setSingleStep(.5);
        limit->setDecimals(1);
        limit->setValue(options.maxZoom);
        limit->setSuffix(QStringLiteral("×"));
        form->addRow(QStringLiteral("Максимальный зум"), limit);

        duration = new QSpinBox(this);
        duration->setRange(0, 500);
        duration->setSingleStep(20);
        duration->setValue(options.smoothMs);
        duration->setSuffix(QStringLiteral(" мс (0 = без анимации)"));
        form->addRow(QStringLiteral("Плавность"), duration);

        anchor = new QCheckBox(QStringLiteral("Приближать к позиции курсора"), this);
        anchor->setChecked(options.cursorAnchor);
        form->addRow(anchor);

        auto *help = new QLabel(QStringLiteral(
            "Выделите источник камеры в OBS и наведите курсор на предпросмотр. "
            "Колесо изменяет масштаб содержимого выбранного источника. "
            "Двойной щелчок колёсиком сбрасывает масштаб. "
            "Зум работает и во время трансляции."), this);
        help->setWordWrap(true);
        form->addRow(help);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &ZoomOptionsDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &ZoomOptionsDialog::reject);
        resize(500, sizeHint().height());
    }

protected:
    void accept() override
    {
        options.modifier = mode->currentData().toInt();
        options.wheelFactor = factor->value();
        options.maxZoom = limit->value();
        options.smoothMs = duration->value();
        options.cursorAnchor = anchor->isChecked();
        save_options();
        QDialog::accept();
    }
private:
    QComboBox *mode;
    QDoubleSpinBox *factor;
    QDoubleSpinBox *limit;
    QSpinBox *duration;
    QCheckBox *anchor;
};

static void open_options()
{
    auto *window = main_window();
    if (window) ZoomOptionsDialog(window).exec();
}

static const char *filter_name(void *) { return kFilterName; }

static void filter_defaults(obs_data_t *s)
{
    obs_data_set_default_double(s, "zoom", 1.0);
    obs_data_set_default_double(s, "x", 0.0);
    obs_data_set_default_double(s, "y", 0.0);
    obs_data_set_default_int(s, "smooth_ms", 180);
}

static void filter_update(void *ptr, obs_data_t *s)
{
    auto *f = static_cast<ZoomData *>(ptr);
    std::lock_guard<std::mutex> guard(f->lock);
    const double z = std::clamp(obs_data_get_double(s, "zoom"), 1.0, 10.0);
    const double high = 1.0 - 1.0 / z;
    f->target = {z,
                 std::clamp(obs_data_get_double(s, "x"), 0.0, high),
                 std::clamp(obs_data_get_double(s, "y"), 0.0, high)};
    f->smoothMs = std::clamp(int(obs_data_get_int(s, "smooth_ms")), 0, 500);
    if (!f->initialized) {
        f->current = f->target; // Restored scenes should not zoom in during startup.
        f->initialized = true;
    }
}

static void *filter_create(obs_data_t *s, obs_source_t *context)
{
    auto *f = new ZoomData;
    f->context = context;
    char *path = obs_module_file("zoom.effect");
    char *error = nullptr;
    if (!path) {
        blog(LOG_ERROR, "OBS Smooth Zoom: missing zoom.effect");
        delete f;
        return nullptr;
    }
    gs_sampler_info info = {};
    info.filter = GS_FILTER_LINEAR;
    info.address_u = GS_ADDRESS_CLAMP;
    info.address_v = GS_ADDRESS_CLAMP;
    obs_enter_graphics();
    f->effect = gs_effect_create_from_file(path, &error);
    f->sampler = gs_samplerstate_create(&info);
    obs_leave_graphics();
    bfree(path);
    if (error) {
        blog(LOG_ERROR, "OBS Smooth Zoom shader: %s", error);
        bfree(error);
    }
    if (!f->effect || !f->sampler) {
        obs_enter_graphics();
        if (f->effect) gs_effect_destroy(f->effect);
        if (f->sampler) gs_samplerstate_destroy(f->sampler);
        obs_leave_graphics();
        delete f;
        return nullptr;
    }
    f->offsetParam = gs_effect_get_param_by_name(f->effect, "uv_offset");
    f->zoomParam = gs_effect_get_param_by_name(f->effect, "zoom");
    f->imageParam = gs_effect_get_param_by_name(f->effect, "image");
    if (!f->offsetParam || !f->zoomParam || !f->imageParam) {
        blog(LOG_ERROR, "OBS Smooth Zoom: missing shader parameters");
        obs_enter_graphics();
        gs_effect_destroy(f->effect);
        gs_samplerstate_destroy(f->sampler);
        obs_leave_graphics();
        delete f;
        return nullptr;
    }
    filter_update(f, s);
    return f;
}

static void filter_destroy(void *ptr)
{
    auto *f = static_cast<ZoomData *>(ptr);
    obs_enter_graphics();
    if (f->effect) gs_effect_destroy(f->effect);
    if (f->sampler) gs_samplerstate_destroy(f->sampler);
    obs_leave_graphics();
    delete f;
}

static void filter_tick(void *ptr, float dt)
{
    auto *f = static_cast<ZoomData *>(ptr);
    std::lock_guard<std::mutex> guard(f->lock);
    f->current = smooth_zoom::advance(f->current, f->target, std::max(0.0, double(dt)), f->smoothMs);
}

static uint32_t filter_width(void *ptr)
{
    auto *f = static_cast<ZoomData *>(ptr);
    auto *source = obs_filter_get_target(f->context);
    return source ? obs_source_get_base_width(source) : 0;
}
static uint32_t filter_height(void *ptr)
{
    auto *f = static_cast<ZoomData *>(ptr);
    auto *source = obs_filter_get_target(f->context);
    return source ? obs_source_get_base_height(source) : 0;
}

static void filter_render(void *ptr, gs_effect_t *)
{
    auto *f = static_cast<ZoomData *>(ptr);
    auto *source = obs_filter_get_target(f->context);
    if (!source || !f->effect) {
        obs_source_skip_video_filter(f->context);
        return;
    }
    const uint32_t w = obs_source_get_base_width(source);
    const uint32_t h = obs_source_get_base_height(source);
    if (!w || !h) {
        obs_source_skip_video_filter(f->context);
        return;
    }
    smooth_zoom::View current;
    {
        std::lock_guard<std::mutex> guard(f->lock);
        current = f->current;
    }
    if (!obs_source_process_filter_begin(f->context, GS_RGBA, OBS_NO_DIRECT_RENDERING)) return;
    vec2 offset;
    vec2_set(&offset, float(current.x), float(current.y));
    gs_effect_set_vec2(f->offsetParam, &offset);
    gs_effect_set_float(f->zoomParam, float(current.zoom));
    gs_effect_set_next_sampler(f->imageParam, f->sampler);
    obs_source_process_filter_end(f->context, f->effect, w, h);
}

static obs_source_info filterInfo = {};
static void register_filter()
{
    filterInfo.id = kFilterType;
    filterInfo.type = OBS_SOURCE_TYPE_FILTER;
    filterInfo.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB;
    filterInfo.get_name = filter_name;
    filterInfo.create = filter_create;
    filterInfo.destroy = filter_destroy;
    filterInfo.update = filter_update;
    filterInfo.get_defaults = filter_defaults;
    filterInfo.video_tick = filter_tick;
    filterInfo.video_render = filter_render;
    filterInfo.get_width = filter_width;
    filterInfo.get_height = filter_height;
    obs_register_source(&filterInfo);
}

static QWidget *preview_widget()
{
    auto *window = main_window();
    return window ? window->findChild<QWidget *>(QStringLiteral("preview")) : nullptr;
}

static bool map_preview_to_canvas(QWidget *preview, const QPoint &point, double &x, double &y)
{
    obs_video_info video = {};
    if (!obs_get_video_info(&video) || !video.base_width || !video.base_height) return false;
    auto *window = main_window();
    if (!window) return false;
    const double dpr = std::max(1.0, preview->devicePixelRatioF());
    const double contentW = preview->width() * dpr - 2 * kPreviewMarginPx;
    const double contentH = preview->height() * dpr - 2 * kPreviewMarginPx;
    if (contentW <= 0 || contentH <= 0) return false;

    double scale = std::min(contentW / video.base_width, contentH / video.base_height);
    double offsetX = kPreviewMarginPx + (contentW - video.base_width * scale) / 2.0;
    double offsetY = kPreviewMarginPx + (contentH - video.base_height * scale) / 2.0;
    auto *mode = window->findChild<QComboBox *>(QStringLiteral("previewScalingMode"));
    if (mode && mode->currentIndex() > 0) {
        auto *label = window->findChild<QLabel *>(QStringLiteral("previewScalePercent"));
        if (label) {
            static const QRegularExpression percent(QStringLiteral(R"(([0-9]+(?:\.[0-9]+)?)\s*%)"));
            const auto match = percent.match(label->text());
            if (match.hasMatch()) {
                const double fixedScale = match.captured(1).toDouble() / 100.0;
                if (fixedScale > 0.0) scale = fixedScale;
            }
        }
        auto *sx = window->findChild<QScrollBar *>(QStringLiteral("previewXScrollBar"));
        auto *sy = window->findChild<QScrollBar *>(QStringLiteral("previewYScrollBar"));
        offsetX = kPreviewMarginPx + (contentW - video.base_width * scale) / 2.0 - (sx ? sx->value() : 0);
        offsetY = kPreviewMarginPx + (contentH - video.base_height * scale) / 2.0 - (sy ? sy->value() : 0);
    }
    if (scale <= 0) return false;
    x = (point.x() * dpr - offsetX) / scale;
    y = (point.y() * dpr - offsetY) / scale;
    return x >= 0 && x <= video.base_width && y >= 0 && y <= video.base_height;
}

struct Pick {
    obs_sceneitem_t *item = nullptr;
};
static bool pick_selected(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
    auto *pick = static_cast<Pick *>(data);
    if (pick->item) return false;
    if (!obs_sceneitem_selected(item) || !obs_sceneitem_visible(item) ||
        obs_sceneitem_is_group(item) || obs_sceneitem_locked(item)) return true;
    obs_source_t *source = obs_sceneitem_get_source(item);
    if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) return true;
    obs_sceneitem_addref(item);
    pick->item = item;
    return false;
}

// Calculate the UV position on the selected source from a point in OBS canvas.
static bool item_uv(obs_sceneitem_t *item, double canvasX, double canvasY, double &u, double &v)
{
    obs_source_t *source = obs_sceneitem_get_source(item);
    const uint32_t w = source ? obs_source_get_width(source) : 0;
    const uint32_t h = source ? obs_source_get_height(source) : 0;
    if (!w || !h) return false;
    matrix4 transform, inverse;
    obs_sceneitem_get_box_transform(item, &transform);
    if (!matrix4_inv(&inverse, &transform)) return false;
    vec3 pos, local;
    vec3_set(&pos, float(canvasX), float(canvasY), 0.0f);
    vec3_transform(&local, &pos, &inverse);
    obs_sceneitem_crop crop = {};
    obs_sceneitem_get_crop(item, &crop);
    const double visibleW = std::max(1.0, double(w) - crop.left - crop.right);
    const double visibleH = std::max(1.0, double(h) - crop.top - crop.bottom);
    u = std::clamp((crop.left + local.x * visibleW) / double(w), 0.0, 1.0);
    v = std::clamp((crop.top + local.y * visibleH) / double(h), 0.0, 1.0);
    return true;
}

static obs_source_t *ensure_filter(obs_source_t *source)
{
    obs_source_t *f = obs_source_get_filter_by_name(source, kFilterName);
    if (f) return f;
    obs_data_t *settings = obs_data_create();
    filter_defaults(settings);
    obs_data_set_double(settings, "zoom", 1.0);
    obs_data_set_int(settings, "smooth_ms", options.smoothMs);
    obs_source_t *created = obs_source_create_private(kFilterType, kFilterName, settings);
    obs_data_release(settings);
    if (!created) return nullptr;
    obs_source_filter_add(source, created);
    obs_source_release(created);
    return obs_source_get_filter_by_name(source, kFilterName);
}

static bool zoom_selected(double canvasX, double canvasY, double factor, bool reset)
{
    obs_source_t *sceneSource = obs_frontend_get_current_preview_scene();
    if (!sceneSource) sceneSource = obs_frontend_get_current_scene();
    if (!sceneSource) return false;
    auto *scene = obs_scene_from_source(sceneSource);
    Pick pick;
    if (scene) obs_scene_enum_items(scene, pick_selected, &pick);
    bool modified = false;
    if (pick.item) {
        obs_source_t *source = obs_sceneitem_get_source(pick.item);
        double uvX = 0.5, uvY = 0.5;
        if (!options.cursorAnchor || reset || item_uv(pick.item, canvasX, canvasY, uvX, uvY)) {
            obs_source_t *f = ensure_filter(source);
            if (f) {
                obs_data_t *settings = obs_source_get_settings(f);
                smooth_zoom::View current{
                    std::clamp(obs_data_get_double(settings, "zoom"), 1.0, 10.0),
                    obs_data_get_double(settings, "x"), obs_data_get_double(settings, "y")};
                const auto next = reset ? smooth_zoom::View{} :
                    smooth_zoom::step(current, factor, uvX, uvY, options.maxZoom);
                obs_data_set_double(settings, "zoom", next.zoom);
                obs_data_set_double(settings, "x", next.x);
                obs_data_set_double(settings, "y", next.y);
                obs_data_set_int(settings, "smooth_ms", options.smoothMs);
                obs_source_update(f, settings);
                obs_data_release(settings);
                obs_source_release(f);
                modified = true;
            }
        }
        obs_sceneitem_release(pick.item);
    }
    obs_source_release(sceneSource);
    return modified;
}

class WheelHandler : public QObject {
public:
    explicit WheelHandler(QObject *parent) : QObject(parent) {}
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel && event->type() != QEvent::MouseButtonDblClick)
            return QObject::eventFilter(watched, event);
        QWidget *preview = preview_widget();
        if (!preview || !preview->isVisible() || !preview->isEnabled())
            return QObject::eventFilter(watched, event);
        QWidget *hovered = QApplication::widgetAt(QCursor::pos());
        if (!hovered || (hovered != preview && !preview->isAncestorOf(hovered)))
            return QObject::eventFilter(watched, event);
        const QPoint point = preview->mapFromGlobal(QCursor::pos());
        if (!preview->rect().contains(point))
            return QObject::eventFilter(watched, event);
        double cx = 0, cy = 0;
        if (!map_preview_to_canvas(preview, point, cx, cy))
            return QObject::eventFilter(watched, event);
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::MiddleButton && zoom_selected(cx, cy, 1.0, true)) {
                event->accept();
                return true;
            }
        } else {
            auto *wheel = static_cast<QWheelEvent *>(event);
            if (options.modifier == int(Qt::NoModifier)) {
                if (wheel->modifiers() != Qt::NoModifier)
                    return QObject::eventFilter(watched, event);
            } else if (!wheel->modifiers().testFlag(Qt::ControlModifier)) {
                return QObject::eventFilter(watched, event);
            }
            const int delta = wheel->angleDelta().y();
            const double steps = delta != 0 ? double(delta) / 120.0 : double(wheel->pixelDelta().y()) / 100.0;
            if (steps != 0 && zoom_selected(cx, cy, std::pow(options.wheelFactor, steps), false)) {
                event->accept();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
};
WheelHandler *handler = nullptr;
} // namespace

bool obs_module_load(void)
{
    load_options();
    register_filter();
    if (!qApp || !main_window()) {
        blog(LOG_WARNING, "OBS Smooth Zoom: no OBS Qt main window");
        return false;
    }
    handler = new WheelHandler(qApp);
    qApp->installEventFilter(handler);
    settingsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("OBS Smooth Zoom..."));
    if (settingsAction) QObject::connect(settingsAction, &QAction::triggered, [] { open_options(); });
    blog(LOG_INFO, "OBS Smooth Zoom v0.1 loaded");
    return true;
}

void obs_module_unload(void)
{
    if (qApp && handler) qApp->removeEventFilter(handler);
    delete handler;
    handler = nullptr;
    delete settingsAction;
    settingsAction = nullptr;
}
