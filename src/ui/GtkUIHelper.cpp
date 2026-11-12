#include "GtkUIHelper.h"

#include <QCoreApplication>
#include <QLibrary>
#include <QLocalSocket>
#include <QTimer>
#include <QDataStream>
#include <QFile>
#include <lilv/lilv.h>
#include "../audio/LilvUtil.h"
#include <suil/suil.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/options/options.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {
std::unordered_map<std::string, LV2_URID> urids;
LV2_URID nextUrid = 1;

LV2_URID mapUri(LV2_URID_Map_Handle, const char* uri) {
    const auto [it, inserted] = urids.emplace(uri, nextUrid);
    if (inserted) ++nextUrid;
    return it->second;
}

const char* unmapUri(LV2_URID_Unmap_Handle, LV2_URID urid) {
    for (const auto& [uri, id] : urids) if (id == urid) return uri.c_str();
    return nullptr;
}

static std::vector<uint8_t> buildPatchSet(const std::string& propertyUri, const std::string& path) {
    LV2_URID patchSetUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/patch#Set");
    LV2_URID patchPropertyUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/patch#property");
    LV2_URID patchValueUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/patch#value");
    LV2_URID propertyUrid = mapUri(nullptr, propertyUri.c_str());
    LV2_URID pathUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/atom#Path");
    LV2_URID uridUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/atom#URID");
    LV2_URID objectUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/atom#Object");

    auto align8 = [](size_t size) { return (size + 7) & ~7; };
    size_t pathLenAligned = align8(path.size() + 1);
    size_t objBodySize = 8  // otype + context
                       + 8  // Property 1 key + context
                       + 8  // Property 1 value size + type
                       + 8  // Property 1 value (aligned)
                       + 8  // Property 2 key + context
                       + 8  // Property 2 value size + type
                       + pathLenAligned; // Property 2 value (aligned)

    std::vector<uint8_t> buffer(8 + objBodySize, 0);
    uint32_t* p = reinterpret_cast<uint32_t*>(buffer.data());
    
    p[0] = objBodySize;
    p[1] = objectUrid;
    p[2] = patchSetUrid;
    p[3] = 0; // context

    p[4] = patchPropertyUrid;
    p[5] = 0; // context
    p[6] = 4; // size
    p[7] = uridUrid;
    p[8] = propertyUrid;
    p[9] = 0; // padding

    p[10] = patchValueUrid;
    p[11] = 0; // context
    p[12] = path.size() + 1; // size
    p[13] = pathUrid;

    uint8_t* pathPtr = buffer.data() + 56;
    std::memcpy(pathPtr, path.c_str(), path.size() + 1);

    return buffer;
}

// Xlib ends the program on any X error by default. A plugin window can
// disappear between asking for it and asking for its size (BadWindow); that
// is reported and ignored instead.
static int ignoreXError(Display* display, XErrorEvent* error) {
    char text[160] = {};
    XGetErrorText(display, error->error_code, text, sizeof(text) - 1);
    std::fprintf(stderr, "RigRoom: ignored X error: %s (request %d, window 0x%lx)\n", text,
                 int(error->request_code), static_cast<unsigned long>(error->resourceid));
    return 0;
}

struct Helper {
    using GtkInitCheck = int (*)(int*, char***);
    using GtkWindowNew = void* (*)(int);
    using GtkWindowSetTitle = void (*)(void*, const char*);
    using GtkWindowSetKeepAbove = void (*)(void*, int);
    using GtkContainerAdd = void (*)(void*, void*);
    using GtkWidgetAction = void (*)(void*);
    using GtkWidgetGetWindow = void* (*)(void*);
    using SignalConnectData = unsigned long (*)(void*, const char*, void (*)(), void*, void (*)(void*, void*), int);
    // For UIs larger than the screen: a scrolled window around the plugin.
    struct GtkRequisitionData { int width; int height; };
    using GtkWidgetSizeRequest = void (*)(void*, GtkRequisitionData*);
    using GtkScrolledWindowNew = void* (*)(void*, void*);
    using GtkScrolledWindowSetPolicy = void (*)(void*, int, int);
    using GtkScrolledWindowAddWithViewport = void (*)(void*, void*);
    using GtkWindowSetDefaultSize = void (*)(void*, int, int);
    // A Close button above the plugin: without a window manager the window has
    // no title bar.
    using GtkVBoxNew = void* (*)(int, int);
    using GtkButtonNewWithLabel = void* (*)(const char*);
    using GtkBoxPackStart = void (*)(void*, void*, int, int, unsigned int);
    static constexpr int kCloseBarHeight = 64;

    static void closeClicked(void*, void*) {
        QCoreApplication::quit();
    }

    // Full size of the window's content (plugin and Close button), sent to
    // RigRoom, which zooms the screen so it fills the panel.
    int contentWidth = 0;
    int contentHeight = 0;

    void requestZoom() {
        if (contentWidth <= 0 || contentHeight <= 0) return;
        QByteArray message;
        QDataStream out(&message, QIODevice::WriteOnly);
        out << quint8('Z') << quint32(contentWidth) << quint32(contentHeight);
        socket.write(message);
        socket.flush();
    }

    void applyZoom(int screenW, int screenH) {
        using GtkWindowResize = void (*)(void*, int, int);
        using GtkWindowMove = void (*)(void*, int, int);
        auto resizeWindow = reinterpret_cast<GtkWindowResize>(gtk.resolve("gtk_window_resize"));
        auto moveWindow = reinterpret_cast<GtkWindowMove>(gtk.resolve("gtk_window_move"));
        if (!window || screenW <= 0 || screenH <= 0) return;
        // The whole screen, black, with the plugin centred in it.
        if (resizeWindow) resizeWindow(window, screenW, screenH);
        if (moveWindow) moveWindow(window, 0, 0);
    }

    Helper(const char* gtkLibrary, const char* containerType, const char* uiType, QString titlePrefix)
        : gtk(gtkLibrary), containerType(containerType), uiType(uiType), titlePrefix(std::move(titlePrefix)) {}

    QLibrary gtk;
    const char* containerType;
    const char* uiType;
    QString titlePrefix;
    QLocalSocket socket;
    LilvWorld* world = nullptr;
    const LilvPlugin* plugin = nullptr;
    SuilHost* host = nullptr;
    SuilInstance* instance = nullptr;
    void* window = nullptr;
    GtkWidgetAction destroyWidget = nullptr;
    const LV2UI_Idle_Interface* idle = nullptr;
    QTimer idleTimer;
    QByteArray incoming;

    LV2_URID_Map map{nullptr, mapUri};
    LV2_URID_Unmap unmap{nullptr, unmapUri};
    LV2_Feature mapFeature{LV2_URID__map, &map};
    LV2_Feature unmapFeature{LV2_URID__unmap, &unmap};
    LV2_Feature residentFeature{"http://lv2plug.in/ns/extensions/ui#makeResident", nullptr};
    const LV2_Feature* features[4]{&mapFeature, &unmapFeature, &residentFeature, nullptr};

    static void portWrite(SuilController controller, uint32_t index, uint32_t size,
                          uint32_t protocol, const void* buffer) {
        auto* self = static_cast<Helper*>(controller);
        if (!self) return;
        QByteArray message;
        QDataStream stream(&message, QIODevice::WriteOnly);
        if (protocol == 0 && size == sizeof(float)) {
            stream << quint8('P') << quint32(index) << *static_cast<const float*>(buffer);
        } else {
            stream << quint8('A') << quint32(index) << quint32(protocol) << QByteArray(static_cast<const char*>(buffer), static_cast<int>(size));
        }
        self->socket.write(message);
        self->socket.flush();
    }

    static int closeWindow(void*, void*, void*) {
        QCoreApplication::quit();
        return 1;
    }

    bool start(const QString& serverName, const QString& pluginUri, const QString& uiUri, unsigned long parentWindow) {
        socket.connectToServer(serverName);
        if (!socket.waitForConnected(5000)) return false;

        // Avoid optional desktop integration modules which are not required by LV2 UIs.
        qputenv("GTK_MODULES", "");
        if (!gtk.load()) return false;
        auto initCheck = reinterpret_cast<GtkInitCheck>(gtk.resolve("gtk_init_check"));
        auto windowNew = reinterpret_cast<GtkWindowNew>(gtk.resolve("gtk_window_new"));
        auto setTitle = reinterpret_cast<GtkWindowSetTitle>(gtk.resolve("gtk_window_set_title"));
        auto setKeepAbove = reinterpret_cast<GtkWindowSetKeepAbove>(gtk.resolve("gtk_window_set_keep_above"));
        auto containerAdd = reinterpret_cast<GtkContainerAdd>(gtk.resolve("gtk_container_add"));
        auto showAll = reinterpret_cast<GtkWidgetAction>(gtk.resolve("gtk_widget_show_all"));
        auto getWindow = reinterpret_cast<GtkWidgetGetWindow>(gtk.resolve("gtk_widget_get_window"));
        destroyWidget = reinterpret_cast<GtkWidgetAction>(gtk.resolve("gtk_widget_destroy"));
        auto connectSignal = reinterpret_cast<SignalConnectData>(gtk.resolve("g_signal_connect_data"));
        if (!initCheck || !windowNew || !setTitle || !setKeepAbove || !containerAdd || !showAll ||
            !getWindow || !destroyWidget || !connectSignal) return false;
        int gtkArgc = 0;
        char** gtkArgv = nullptr;
        if (!initCheck(&gtkArgc, &gtkArgv)) return false;

        world = lilv_world_new();
        lilv_world_load_all(world);
        LilvNode* pluginNode = lilv_new_uri(world, pluginUri.toUtf8().constData());
        plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), pluginNode);
        lilv_node_free(pluginNode);
        if (!plugin) return false;

        const LilvUI* selectedUi = nullptr;
        const LilvUIs* uis = lilv_plugin_get_uis(plugin);
        if (uis) {
            LILV_FOREACH(uis, i, uis) {
                const LilvUI* candidate = lilv_uis_get(uis, i);
                if (uiUri == QString::fromUtf8(lilvUriText(lilv_ui_get_uri(candidate)))) {
                    selectedUi = candidate;
                    break;
                }
            }
        }
        if (!selectedUi) return false;

        host = suil_host_new(portWrite, nullptr, nullptr, nullptr);
        const std::string bundle = lilvFilePath(lilv_ui_get_bundle_uri(selectedUi));
        std::string binary = lilvFilePath(lilv_ui_get_binary_uri(selectedUi));
        QByteArray binaryOverride;
        if ((binary.empty() || !QFile::exists(QString::fromLocal8Bit(binary.c_str()))) &&
            pluginUri.startsWith("http://calf.sourceforge.net/plugins/")) {
            const QString fedoraCalfUi = "/usr/lib64/calf/libcalflv2gui.so";
            if (QFile::exists(fedoraCalfUi)) {
                binaryOverride = QFile::encodeName(fedoraCalfUi);
                binary.clear();
            }
        }
        const QByteArray pluginBytes = pluginUri.toUtf8();
        const QByteArray uiBytes = uiUri.toUtf8();
        instance = suil_instance_new(host, this, containerType, pluginBytes.constData(),
                                     uiBytes.constData(), uiType, bundle.c_str(),
                                     binaryOverride.isEmpty()
                                         ? (binary.empty() ? nullptr : binary.c_str())
                                         : binaryOverride.constData(), features);
        if (!instance) return false;

        void* pluginWidget = suil_instance_get_widget(instance);
        if (!pluginWidget) return false;
        window = windowNew(0);
        const QByteArray title = (titlePrefix + " - " + pluginUri).toUtf8();
        setTitle(window, title.constData());
        setKeepAbove(window, 1);
        // A UI larger than the screen (a small touchscreen) scrolls inside a
        // window that fits; anything else is added as it is.
        bool scrolled = false;
        {
            auto sizeRequest = reinterpret_cast<GtkWidgetSizeRequest>(gtk.resolve("gtk_widget_size_request"));
            auto scrolledNew = reinterpret_cast<GtkScrolledWindowNew>(gtk.resolve("gtk_scrolled_window_new"));
            auto setPolicy = reinterpret_cast<GtkScrolledWindowSetPolicy>(gtk.resolve("gtk_scrolled_window_set_policy"));
            auto addWithViewport = reinterpret_cast<GtkScrolledWindowAddWithViewport>(gtk.resolve("gtk_scrolled_window_add_with_viewport"));
            auto setDefaultSize = reinterpret_cast<GtkWindowSetDefaultSize>(gtk.resolve("gtk_window_set_default_size"));
            int screenWidth = 0, screenHeight = 0;
            if (Display* display = XOpenDisplay(nullptr)) {
                screenWidth = DisplayWidth(display, DefaultScreen(display));
                screenHeight = DisplayHeight(display, DefaultScreen(display));
                XCloseDisplay(display);
            }
            GtkRequisitionData wanted{0, 0};
            if (sizeRequest) sizeRequest(pluginWidget, &wanted);
            contentWidth = wanted.width;
            contentHeight = wanted.height > 0 ? wanted.height + kCloseBarHeight : 0;
            const int roomHeight = screenHeight - kCloseBarHeight;
            void* content = pluginWidget;
            if (sizeRequest && scrolledNew && setPolicy && addWithViewport && setDefaultSize &&
                screenWidth > 0 && roomHeight > 0 &&
                (wanted.width > screenWidth || wanted.height > roomHeight)) {
                void* scrolledWindow = scrolledNew(nullptr, nullptr);
                constexpr int kPolicyAutomatic = 1; // GTK_POLICY_AUTOMATIC
                setPolicy(scrolledWindow, kPolicyAutomatic, kPolicyAutomatic);
                addWithViewport(scrolledWindow, pluginWidget);
                setDefaultSize(window, qMin(wanted.width, screenWidth), qMin(wanted.height, roomHeight) + kCloseBarHeight);
                content = scrolledWindow;
                scrolled = true;
            }
            auto vboxNew = reinterpret_cast<GtkVBoxNew>(gtk.resolve("gtk_vbox_new"));
            auto buttonNew = reinterpret_cast<GtkButtonNewWithLabel>(gtk.resolve("gtk_button_new_with_label"));
            auto packStart = reinterpret_cast<GtkBoxPackStart>(gtk.resolve("gtk_box_pack_start"));
            // Black around the plugin, the plugin centred in the screen.
            struct GdkColorData { quint32 pixel; quint16 red, green, blue; };
            using GtkWidgetModifyBg = void (*)(void*, int, const GdkColorData*);
            using GtkAlignmentNew = void* (*)(float, float, float, float);
            using GtkHBoxNew = void* (*)(int, int);
            using GtkLabelNew = void* (*)(const char*);
            using GtkLabelSetMarkup = void (*)(void*, const char*);
            using GtkBinGetChild = void* (*)(void*);
            using GtkWidgetSetSizeRequest = void (*)(void*, int, int);
            auto modifyBg = reinterpret_cast<GtkWidgetModifyBg>(gtk.resolve("gtk_widget_modify_bg"));
            auto alignmentNew = reinterpret_cast<GtkAlignmentNew>(gtk.resolve("gtk_alignment_new"));
            auto hboxNew = reinterpret_cast<GtkHBoxNew>(gtk.resolve("gtk_hbox_new"));
            auto labelNew = reinterpret_cast<GtkLabelNew>(gtk.resolve("gtk_label_new"));
            auto setMarkup = reinterpret_cast<GtkLabelSetMarkup>(gtk.resolve("gtk_label_set_markup"));
            auto binChild = reinterpret_cast<GtkBinGetChild>(gtk.resolve("gtk_bin_get_child"));
            auto setSizeRequest = reinterpret_cast<GtkWidgetSetSizeRequest>(gtk.resolve("gtk_widget_set_size_request"));
            const GdkColorData black{0, 0, 0, 0};
            if (modifyBg) modifyBg(window, 0 /* GTK_STATE_NORMAL */, &black);
            if (alignmentNew) {
                const float scale = scrolled ? 1.0f : 0.0f;
                void* centre = alignmentNew(0.5f, 0.5f, scale, scale);
                containerAdd(centre, content);
                content = centre;
            }
            if (vboxNew && buttonNew && packStart) {
                void* box = vboxNew(0, 0);
                void* closeButton = buttonNew("\u25C0  BACK");
                if (binChild && setMarkup)
                    if (void* label = binChild(closeButton))
                        setMarkup(label, "<span size='xx-large' weight='bold'>\u25C0  BACK</span>");
                if (setSizeRequest) setSizeRequest(closeButton, 150, kCloseBarHeight - 8);
                connectSignal(closeButton, "clicked", reinterpret_cast<void (*)()>(&Helper::closeClicked),
                              this, nullptr, 0);
                void* bar = hboxNew ? hboxNew(0, 12) : nullptr;
                if (bar) {
                    packStart(bar, closeButton, 0, 0, 4);
                    if (labelNew && setMarkup) {
                        void* name = labelNew("");
                        QByteArray markup = "<span size='xx-large' weight='bold' foreground='#FFFFFF'>";
                        markup += QString::fromStdString(lilvTakeString(lilv_plugin_get_name(plugin)))
                                      .toHtmlEscaped().toUtf8();
                        markup += "</span>";
                        setMarkup(name, markup.constData());
                        packStart(bar, name, 1, 1, 0);
                    }
                    if (setSizeRequest) setSizeRequest(bar, -1, kCloseBarHeight);
                    packStart(box, bar, 0, 1, 0);
                } else {
                    packStart(box, closeButton, 0, 1, 0);
                }
                packStart(box, content, 1, 1, 0);
                containerAdd(window, box);
            } else {
                containerAdd(window, content);
            }
            if (setDefaultSize && screenWidth > 0 && screenHeight > 0) setDefaultSize(window, screenWidth, screenHeight);
        }
        (void)scrolled;
        connectSignal(window, "delete-event", reinterpret_cast<void (*)()>(&Helper::closeWindow),
                       this, nullptr, 0);
        showAll(window);
        requestZoom();
        if (parentWindow) {
            QLibrary gdk{"libgdk-x11-2.0.so.0"};
            using GdkWindowGetXid = unsigned long (*)(void*);
            auto getXid = gdk.load()
                ? reinterpret_cast<GdkWindowGetXid>(gdk.resolve("gdk_x11_window_get_xid"))
                : nullptr;
            if (getXid) {
                if (Display* display = XOpenDisplay(nullptr)) {
                    XSetTransientForHint(display, getXid(getWindow(window)), parentWindow);
                    XFlush(display);
                    XCloseDisplay(display);
                }
            }
        }

        idle = static_cast<const LV2UI_Idle_Interface*>(
            suil_instance_extension_data(instance, LV2_UI__idleInterface));
        if (idle) {
            QObject::connect(&idleTimer, &QTimer::timeout, [&]() {
                idle->idle(suil_instance_get_handle(instance));
            });
            idleTimer.start(16);
        }

        QObject::connect(&socket, &QLocalSocket::readyRead, [&]() {
            incoming += socket.readAll();
            QDataStream stream(&incoming, QIODevice::ReadOnly);
            while (true) {
                stream.startTransaction();
                quint8 command = 0;
                stream >> command;
                if (command == 'Z') {
                    // RigRoom zoomed the screen for this plugin: show all of
                    // it, centred.
                    quint32 screenW = 0;
                    quint32 screenH = 0;
                    stream >> screenW >> screenH;
                    if (!stream.commitTransaction()) break;
                    applyZoom(int(screenW), int(screenH));
                } else if (command == 'P') {
                    quint32 index = 0;
                    float value = 0.0f;
                    stream >> index >> value;
                    if (!stream.commitTransaction()) break;
                    suil_instance_port_event(instance, index, sizeof(float), 0, &value);
                } else if (command == 'S') {
                    QString uri;
                    QString filePath;
                    stream >> uri >> filePath;
                    if (!stream.commitTransaction()) break;

                    int atomInputPortIndex = -1;
                    LilvNode* atomPortClass = lilv_new_uri(world, "http://lv2plug.in/ns/ext/atom#AtomPort");
                    LilvNode* inputPortClass = lilv_new_uri(world, "http://lv2plug.in/ns/lv2core#InputPort");
                    uint32_t numPorts = lilv_plugin_get_num_ports(plugin);
                    for (uint32_t i = 0; i < numPorts; ++i) {
                        const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
                        if (lilv_port_is_a(plugin, port, atomPortClass) && lilv_port_is_a(plugin, port, inputPortClass)) {
                            atomInputPortIndex = i;
                            break;
                        }
                    }
                    lilv_node_free(atomPortClass);
                    lilv_node_free(inputPortClass);

                    if (atomInputPortIndex != -1) {
                        std::vector<uint8_t> patchBuffer = buildPatchSet(uri.toStdString(), filePath.toStdString());
                        LV2_URID seqUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
                        suil_instance_port_event(instance, atomInputPortIndex, patchBuffer.size(), seqUrid, patchBuffer.data());
                    }
                } else {
                    stream.rollbackTransaction();
                    break;
                }
            }
            incoming.remove(0, static_cast<int>(stream.device()->pos()));
        });
        QObject::connect(&socket, &QLocalSocket::disconnected, &QCoreApplication::quit);
        return true;
    }

    ~Helper() {
        idleTimer.stop();
        if (instance) suil_instance_free(instance);
        if (window && destroyWidget) destroyWidget(window);
        if (host) suil_host_free(host);
        if (world) lilv_world_free(world);
    }
};

struct X11Helper {
    QLocalSocket socket;
    LilvWorld* world = nullptr;
    const LilvPlugin* plugin = nullptr;
    SuilHost* host = nullptr;
    SuilInstance* instance = nullptr;
    Display* display = nullptr;
    Window window = 0;
    Window child = 0;
    Window viewport = 0;
    int sizeCheckTicks = 0;
    int pendingWidth = 0;
    int pendingHeight = 0;  // the part of the screen below the bar that shows the plugin
    XFontStruct* barFont = nullptr;
    Atom closeMessage = None;
    const LV2UI_Idle_Interface* idle = nullptr;
    const LV2UI_Resize* resize = nullptr;
    QTimer idleTimer;
    QTimer eventTimer;
    QByteArray incoming;
    int requestedWidth = 0;
    int requestedHeight = 0;
    int lastResizeWidth = 0;
    int lastResizeHeight = 0;
    bool uiReady = false;

    LV2_URID_Map map{nullptr, mapUri};
    LV2_URID_Unmap unmap{nullptr, unmapUri};
    LV2_Feature mapFeature{LV2_URID__map, &map};
    LV2_Feature unmapFeature{LV2_URID__unmap, &unmap};
    LV2_Feature parentFeature{LV2_UI__parent, nullptr};
    LV2UI_Resize hostResize{this, resizeRequest};
    LV2_Feature resizeFeature{LV2_UI__resize, &hostResize};
    LV2_Feature residentFeature{"http://lv2plug.in/ns/extensions/ui#makeResident", nullptr};
    // ui:scaleFactor, so UIs that support it (DPF plugins such as Dragonfly)
    // can draw smaller on a small screen.
    float scaleFactor = 1.0f;
    LV2_Options_Option options[2]{};
    LV2_Feature optionsFeature{LV2_OPTIONS__options, options};
    const LV2_Feature* features[7]{&mapFeature, &unmapFeature, &parentFeature, &resizeFeature, &residentFeature, &optionsFeature, nullptr};

    // A bar above the plugin with a Close button, and arrow buttons when the
    // plugin is larger than the screen: without a window manager the window
    // has no title bar, so this is the only way to close it.
    static constexpr int kBarHeight = 64;
    static constexpr int kCloseWidth = 150;
    static constexpr int kArrowWidth = 64;
    int screenWidth = 0;
    int screenHeight = 0;
    int uiWidth = 0;    // plugin view
    int uiHeight = 0;
    int viewWidth = 1;  // the part of it the window shows
    int viewHeight = 1;
    int panX = 0;
    int panY = 0;
    GC gc = nullptr;
    QByteArray barTitle;
    struct BarButton { int x; int width; char action; };
    std::vector<BarButton> barButtons;

    bool pannable() const { return uiWidth > viewWidth || uiHeight > viewHeight; }

    // Ask RigRoom to zoom the screen so the whole plugin fills the panel.
    void requestZoom() {
        const int contentWidth = qMax(kCloseWidth + 4 * kArrowWidth + 16, uiWidth);
        const int contentHeight = uiHeight + kBarHeight;
        QByteArray message;
        QDataStream out(&message, QIODevice::WriteOnly);
        out << quint8('Z') << quint32(contentWidth) << quint32(contentHeight);
        socket.write(message);
        socket.flush();
    }

    // RigRoom zoomed the screen: it now holds the whole plugin.
    void applyZoom(int screenW, int screenH) {
        if (screenW <= 0 || screenH <= 0) return;
        screenWidth = screenW;
        screenHeight = screenH;
        layoutView();
    }

    // The window covers the whole screen in black: the bar at the top, the
    // plugin centred below it (shifted by the pan offset when it is larger).
    void layoutView() {
        if (!display || !window) return;
        const int roomWidth = qMax(1, screenWidth);
        const int roomHeight = qMax(1, screenHeight - kBarHeight);
        viewWidth = qMax(1, qMin(uiWidth, roomWidth));
        viewHeight = qMax(1, qMin(uiHeight, roomHeight));
        panX = qBound(0, panX, qMax(0, uiWidth - viewWidth));
        panY = qBound(0, panY, qMax(0, uiHeight - viewHeight));
        XMoveResizeWindow(display, window, 0, 0, roomWidth, roomHeight + kBarHeight);
        if (viewport)
            XMoveResizeWindow(display, viewport, (roomWidth - viewWidth) / 2,
                              kBarHeight + (roomHeight - viewHeight) / 2, viewWidth, viewHeight);
        if (child) XMoveResizeWindow(display, child, -panX, -panY, qMax(1, uiWidth), qMax(1, uiHeight));
        drawBar();
        XFlush(display);
    }

    // A large font for the bar (twice the X default), whatever is installed.
    void loadBarFont() {
        const char* names[] = {"-*-dejavu sans-bold-r-normal--26-*-*-*-*-*-iso8859-1",
                               "-*-helvetica-bold-r-normal--24-*-*-*-*-*-iso8859-1",
                               "-*-*-bold-r-normal--24-*-*-*-*-*-iso8859-1",
                               "12x24", "-*-*-*-r-*--24-*-*-*-*-*-iso8859-1", "10x20", "9x15bold"};
        for (const char* name : names) {
            barFont = XLoadQueryFont(display, name);
            if (barFont) break;
        }
        if (barFont && gc) XSetFont(display, gc, barFont->fid);
    }

    int textWidth(const char* text, int length) const {
        return barFont ? XTextWidth(barFont, text, length) : length * 6;
    }

    int textBaseline() const {
        if (!barFont) return kBarHeight / 2 + 4;
        return (kBarHeight - (barFont->ascent + barFont->descent)) / 2 + barFont->ascent;
    }

    void drawButton(int x, int width, const char* label, unsigned long colour) {
        XSetForeground(display, gc, colour);
        XFillRectangle(display, window, gc, x, 6, width, kBarHeight - 12);
        XSetForeground(display, gc, 0xFFFFFF);
        const int length = static_cast<int>(std::strlen(label));
        XDrawString(display, window, gc, x + (width - textWidth(label, length)) / 2, textBaseline(), label, length);
    }

    void drawBar() {
        if (!gc) return;
        barButtons.clear();
        const int barWidth = qMax(1, screenWidth);
        XSetForeground(display, gc, 0x000000);
        XFillRectangle(display, window, gc, 0, 0, barWidth, qMax(1, screenHeight));
        XSetForeground(display, gc, 0x1A1A1E);
        XFillRectangle(display, window, gc, 0, 0, barWidth, kBarHeight);
        XSetForeground(display, gc, 0x00B0FF);
        XFillRectangle(display, window, gc, 0, kBarHeight - 2, barWidth, 2);
        // BACK at the top left, like RigRoom's own pages.
        drawButton(6, kCloseWidth, "< BACK", 0x00897B);
        barButtons.push_back({6, kCloseWidth, 'c'});
        int right = barWidth;
        if (pannable()) {
            const struct { const char* label; char action; } arrows[] = {{">", 'r'}, {"v", 'd'}, {"^", 'u'}, {"<", 'l'}};
            for (const auto& arrow : arrows) {
                right -= kArrowWidth + 6;
                drawButton(right, kArrowWidth, arrow.label, 0x3A3A44);
                barButtons.push_back({right, kArrowWidth, arrow.action});
            }
        }
        // The plugin's name, large, centred in the room between the buttons.
        XSetForeground(display, gc, 0xFFFFFF);
        const int left = 6 + kCloseWidth + 16;
        const int room = qMax(0, right - left - 16);
        int length = static_cast<int>(barTitle.size());
        while (length > 0 && textWidth(barTitle.constData(), length) > room) --length;
        const int width = textWidth(barTitle.constData(), length);
        const int centred = (barWidth - width) / 2;
        const int titleX = qBound(left, centred, qMax(left, right - 16 - width));
        XDrawString(display, window, gc, titleX, textBaseline(), barTitle.constData(), length);
    }

    void barPressed(int x) {
        for (const BarButton& button : barButtons) {
            if (x < button.x || x >= button.x + button.width) continue;
            const int stepX = qMax(40, viewWidth / 2);
            const int stepY = qMax(40, viewHeight / 2);
            switch (button.action) {
            case 'c': QCoreApplication::quit(); return;
            case 'l': panX -= stepX; break;
            case 'r': panX += stepX; break;
            case 'u': panY -= stepY; break;
            case 'd': panY += stepY; break;
            }
            layoutView();
            return;
        }
    }

    static int resizeRequest(LV2UI_Feature_Handle handle, int width, int height) {
        auto* self = static_cast<X11Helper*>(handle);
        if (!self || width <= 0 || height <= 0) return 1;
        self->requestedWidth = width;
        self->requestedHeight = height;
        if (self->uiReady && self->display && self->window) {
            // Never larger than the screen; the arrows pan over the rest.
            self->uiWidth = width;
            self->uiHeight = height;
            self->layoutView();
        }
        return 0;
    }

    static void portWrite(SuilController controller, uint32_t index, uint32_t size,
                          uint32_t protocol, const void* buffer) {
        auto* self = static_cast<X11Helper*>(controller);
        if (!self) return;
        QByteArray message;
        QDataStream stream(&message, QIODevice::WriteOnly);
        if (protocol == 0 && size == sizeof(float)) {
            stream << quint8('P') << quint32(index) << *static_cast<const float*>(buffer);
        } else {
            stream << quint8('A') << quint32(index) << quint32(protocol) << QByteArray(static_cast<const char*>(buffer), static_cast<int>(size));
        }
        self->socket.write(message);
        self->socket.flush();
    }

    bool start(const QString& serverName, const QString& pluginUri, const QString& uiUri, unsigned long parentWindow) {
        socket.connectToServer(serverName);
        if (!socket.waitForConnected(5000)) return false;

        XSetErrorHandler(ignoreXError);
        display = XOpenDisplay(nullptr);
        if (!display) return false;
        const int screen = DefaultScreen(display);
        window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, 1, 1, 0,
                                     BlackPixel(display, screen), BlackPixel(display, screen));
        if (!window) return false;
        const QByteArray title = (QString("Plugin GUI - ") + pluginUri).toUtf8();
        XStoreName(display, window, title.constData());
        closeMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display, window, &closeMessage, 1);
        XSelectInput(display, window, StructureNotifyMask | ExposureMask | ButtonPressMask);
        if (parentWindow) XSetTransientForHint(display, window, parentWindow);
        screenWidth = DisplayWidth(display, screen);
        screenHeight = DisplayHeight(display, screen);
        // The plugin is created inside its own area below the bar. Some
        // plugins make themselves as large as the window they are created in;
        // created in the full-screen window they would cover the bar and grow
        // with every zoom.
        viewport = XCreateSimpleWindow(display, window, 0, kBarHeight, qMax(1, screenWidth),
                                       qMax(1, screenHeight - kBarHeight), 0,
                                       BlackPixel(display, screen), BlackPixel(display, screen));
        XMapWindow(display, viewport);
        parentFeature.data = reinterpret_cast<void*>(static_cast<uintptr_t>(viewport));

        world = lilv_world_new();
        lilv_world_load_all(world);
        LilvNode* pluginNode = lilv_new_uri(world, pluginUri.toUtf8().constData());
        plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), pluginNode);
        lilv_node_free(pluginNode);
        if (!plugin) return false;

        const LilvUI* selectedUi = nullptr;
        const LilvUIs* uis = lilv_plugin_get_uis(plugin);
        if (uis) {
            LILV_FOREACH(uis, i, uis) {
                const LilvUI* candidate = lilv_uis_get(uis, i);
                if (uiUri == QString::fromUtf8(lilvUriText(lilv_ui_get_uri(candidate)))) {
                    selectedUi = candidate;
                    break;
                }
            }
        }
        if (!selectedUi) return false;

        host = suil_host_new(portWrite, nullptr, nullptr, nullptr);
        const std::string bundle = lilvFilePath(lilv_ui_get_bundle_uri(selectedUi));
        const std::string binary = lilvFilePath(lilv_ui_get_binary_uri(selectedUi));
        const QByteArray pluginBytes = pluginUri.toUtf8();
        const QByteArray uiBytes = uiUri.toUtf8();
        options[0] = {LV2_OPTIONS_INSTANCE, 0, mapUri(nullptr, LV2_UI__scaleFactor), sizeof(float),
                      mapUri(nullptr, "http://lv2plug.in/ns/ext/atom#Float"), &scaleFactor};
        options[1] = {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr};

        auto instantiate = [&]() -> bool {
            requestedWidth = 0;
            requestedHeight = 0;
            instance = suil_instance_new(host, this, LV2_UI__X11UI, pluginBytes.constData(),
                                         uiBytes.constData(), LV2_UI__X11UI, bundle.c_str(),
                                         binary.empty() ? nullptr : binary.c_str(), features);
            if (!instance) return false;
            child = reinterpret_cast<uintptr_t>(suil_instance_get_widget(instance));
            XWindowAttributes attributes{};
            if (!child || !XGetWindowAttributes(display, child, &attributes)) return false;
            uiWidth = requestedWidth > 0 ? requestedWidth : attributes.width;
            uiHeight = requestedHeight > 0 ? requestedHeight : attributes.height;
            return true;
        };
        if (!instantiate()) return false;

        // Larger than the screen (a small touchscreen): start the UI again at
        // a smaller ui:scaleFactor, which DPF-based plugins honour.
        const int roomWidth = qMax(1, screenWidth);
        const int roomHeight = qMax(1, screenHeight - kBarHeight);
        auto tooBig = [&]() { return screenWidth > 0 && (uiWidth > roomWidth || uiHeight > roomHeight); };
        if (tooBig()) {
            const double fit = qMin(roomWidth / double(uiWidth), roomHeight / double(uiHeight));
            suil_instance_free(instance);
            instance = nullptr;
            child = 0;
            scaleFactor = static_cast<float>(fit);
            if (!instantiate()) {
                if (instance) suil_instance_free(instance);
                instance = nullptr;
                scaleFactor = 1.0f;
                if (!instantiate()) return false;
            }
            std::fprintf(stderr, "RigRoom UI helper: plugin GUI at scale %.2f is %dx%d\n", scaleFactor, uiWidth, uiHeight);
        }
        // Still too big: a resizable UI is asked for a size that fits. A fixed
        // one keeps its size and the arrow buttons pan over it.
        if (tooBig()) {
            const auto* uiResize = static_cast<const LV2UI_Resize*>(
                suil_instance_extension_data(instance, LV2_UI__resize));
            if (uiResize && uiResize->ui_resize) {
                const double fit = qMin(roomWidth / double(uiWidth), roomHeight / double(uiHeight));
                uiWidth = qMax(1, int(uiWidth * fit));
                uiHeight = qMax(1, int(uiHeight * fit));
                uiResize->ui_resize(suil_instance_get_handle(instance), uiWidth, uiHeight);
            }
        }

        barTitle = QString::fromStdString(lilvTakeString(lilv_plugin_get_name(plugin))).toLatin1();
        gc = XCreateGC(display, window, 0, nullptr);
        loadBarFont();
        // The plugin sits in the black viewport below the bar, so a plugin
        // larger than the screen never covers the bar while it is panned.
        XReparentWindow(display, child, viewport, 0, 0);
        XSizeHints sizeHints{};
        sizeHints.flags = PMinSize | PPosition;
        sizeHints.min_width = 1;
        sizeHints.min_height = kBarHeight + 1;
        XSetWMNormalHints(display, window, &sizeHints);
        XMapWindow(display, child);
        uiReady = true;
        layoutView();
        XMapRaised(display, window);
        XFlush(display);
        requestZoom();

        idle = static_cast<const LV2UI_Idle_Interface*>(
            suil_instance_extension_data(instance, LV2_UI__idleInterface));
        resize = static_cast<const LV2UI_Resize*>(
            suil_instance_extension_data(instance, LV2_UI__resize));
        if (idle) {
            QObject::connect(&idleTimer, &QTimer::timeout, [&]() {
                idle->idle(suil_instance_get_handle(instance));
            });
            idleTimer.start(16);
        }

        QObject::connect(&socket, &QLocalSocket::readyRead, [&]() {
            incoming += socket.readAll();
            QDataStream stream(&incoming, QIODevice::ReadOnly);
            while (true) {
                stream.startTransaction();
                quint8 command = 0;
                stream >> command;
                if (command == 'Z') {
                    // RigRoom zoomed the screen for this plugin: show all of
                    // it, centred.
                    quint32 screenW = 0;
                    quint32 screenH = 0;
                    stream >> screenW >> screenH;
                    if (!stream.commitTransaction()) break;
                    applyZoom(int(screenW), int(screenH));
                } else if (command == 'P') {
                    quint32 index = 0;
                    float value = 0.0f;
                    stream >> index >> value;
                    if (!stream.commitTransaction()) break;
                    suil_instance_port_event(instance, index, sizeof(float), 0, &value);
                } else if (command == 'S') {
                    QString uri;
                    QString filePath;
                    stream >> uri >> filePath;
                    if (!stream.commitTransaction()) break;

                    int atomInputPortIndex = -1;
                    LilvNode* atomPortClass = lilv_new_uri(world, "http://lv2plug.in/ns/ext/atom#AtomPort");
                    LilvNode* inputPortClass = lilv_new_uri(world, "http://lv2plug.in/ns/lv2core#InputPort");
                    uint32_t numPorts = lilv_plugin_get_num_ports(plugin);
                    for (uint32_t i = 0; i < numPorts; ++i) {
                        const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
                        if (lilv_port_is_a(plugin, port, atomPortClass) && lilv_port_is_a(plugin, port, inputPortClass)) {
                            atomInputPortIndex = i;
                            break;
                        }
                    }
                    lilv_node_free(atomPortClass);
                    lilv_node_free(inputPortClass);

                    if (atomInputPortIndex != -1) {
                        std::vector<uint8_t> patchBuffer = buildPatchSet(uri.toStdString(), filePath.toStdString());
                        LV2_URID seqUrid = mapUri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
                        suil_instance_port_event(instance, atomInputPortIndex, patchBuffer.size(), seqUrid, patchBuffer.data());
                    }
                } else {
                    stream.rollbackTransaction();
                    break;
                }
            }
            incoming.remove(0, static_cast<int>(stream.device()->pos()));
        });
        QObject::connect(&socket, &QLocalSocket::disconnected, &QCoreApplication::quit);
        QObject::connect(&eventTimer, &QTimer::timeout, [&]() {
            // Some plugins make their window larger (or smaller) than they
            // said, without asking: follow the window's real size.
            if (++sizeCheckTicks >= 25 && child) {
                sizeCheckTicks = 0;
                XWindowAttributes attributes{};
                // Only once the plugin has kept the new size for a moment, so a
                // plugin still settling does not make the screen flicker.
                const bool grew = XGetWindowAttributes(display, child, &attributes) && attributes.width > 1 &&
                                  attributes.height > 1 &&
                                  (attributes.width != uiWidth || attributes.height != uiHeight);
                const bool settled = grew && attributes.width == pendingWidth && attributes.height == pendingHeight;
                pendingWidth = grew ? attributes.width : 0;
                pendingHeight = grew ? attributes.height : 0;
                if (settled) {
                    pendingWidth = pendingHeight = 0;
                    std::fprintf(stderr, "RigRoom UI helper: plugin window is %dx%d (was %dx%d)\n",
                                 attributes.width, attributes.height, uiWidth, uiHeight);
                    uiWidth = attributes.width;
                    uiHeight = attributes.height;
                    // Only shown in the new size (centred, or panned with the
                    // arrows when larger than the room): zooming again would
                    // make a plugin that follows the screen size grow forever.
                    layoutView();
                }
            }
            while (XPending(display)) {
                XEvent event{};
                XNextEvent(display, &event);
                if (event.type == ClientMessage && event.xclient.data.l[0] == static_cast<long>(closeMessage)) {
                    QCoreApplication::quit();
                } else if (event.type == Expose && event.xexpose.window == window && event.xexpose.count == 0) {
                    drawBar();
                    XFlush(display);
                } else if (event.type == ButtonPress && event.xbutton.window == window && event.xbutton.y < kBarHeight) {
                    barPressed(event.xbutton.x);
                } else if (event.type == ConfigureNotify && event.xconfigure.window == window && resize &&
                           !(event.xconfigure.width == screenWidth && event.xconfigure.height == screenHeight)) {
                    // A window manager resized the window: a resizable UI
                    // follows, within the screen.
                    const int wantedWidth = qMin(event.xconfigure.width, qMax(1, screenWidth));
                    const int wantedHeight = qMin(event.xconfigure.height - kBarHeight, qMax(1, screenHeight - kBarHeight));
                    if (wantedWidth > 0 && wantedHeight > 0 &&
                        (wantedWidth != uiWidth || wantedHeight != uiHeight) &&
                        (wantedWidth != lastResizeWidth || wantedHeight != lastResizeHeight)) {
                        lastResizeWidth = wantedWidth;
                        lastResizeHeight = wantedHeight;
                        uiWidth = wantedWidth;
                        uiHeight = wantedHeight;
                        resize->ui_resize(suil_instance_get_handle(instance), uiWidth, uiHeight);
                        layoutView();
                    }
                }
            }
        });
        eventTimer.start(16);
        return true;
    }

    ~X11Helper() {
        eventTimer.stop();
        idleTimer.stop();
        if (instance) suil_instance_free(instance);
        if (display && barFont) XFreeFont(display, barFont);
        if (display && gc) XFreeGC(display, gc);
        if (display && window) XDestroyWindow(display, window);
        if (display) XCloseDisplay(display);
        if (host) suil_host_free(host);
        if (world) lilv_world_free(world);
    }
};
}

int runGtkUIHelper(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 5) return 2;
    suil_init(&argc, &argv, SUIL_ARG_NONE);
    Helper helper("libgtk-x11-2.0.so.0", LV2_UI__GtkUI, LV2_UI__GtkUI, "Calf");
    const unsigned long parentWindow = argc > 5 ? QString::fromUtf8(argv[5]).toULongLong() : 0;
    if (!helper.start(QString::fromUtf8(argv[2]), QString::fromUtf8(argv[3]), QString::fromUtf8(argv[4]), parentWindow)) {
        return 3;
    }
    return app.exec();
}

int runX11UIHelper(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 5) return 2;
    suil_init(&argc, &argv, SUIL_ARG_NONE);
    X11Helper helper;
    const unsigned long parentWindow = argc > 5 ? QString::fromUtf8(argv[5]).toULongLong() : 0;
    if (!helper.start(QString::fromUtf8(argv[2]), QString::fromUtf8(argv[3]), QString::fromUtf8(argv[4]), parentWindow)) {
        return 3;
    }
    return app.exec();
}
