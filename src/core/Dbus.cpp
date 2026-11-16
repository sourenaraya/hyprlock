#include "Dbus.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include "../auth/Auth.hpp"

static const auto UPPOWER_SERVICE = sdbus::ServiceName{"org.freedesktop.UPower"};
static const auto UPPOWER_PATH    = sdbus::ObjectPath{"/org/freedesktop/UPower"};
static const auto UPPOWER_IFACE   = sdbus::InterfaceName{"org.freedesktop.UPower"};
static const auto DBUS_PROPS_IFACE = sdbus::InterfaceName{"org.freedesktop.DBus.Properties"};

void CDbus::initLidWatcher() {
    static const auto LIDSWITCH = g_pConfigManager->getValue<Hyprlang::INT>("auth:lid_switch_idle");
    if (!*LIDSWITCH)
        return;

    try {
        m_upower = sdbus::createProxy(*m_connection, UPPOWER_SERVICE, UPPOWER_PATH);
    } catch (sdbus::Error& e) {
        Log::logger->log(Log::WARN, "lid: could not create UPower proxy ({}); lid switch idle trigger disabled", e.what());
        return;
    }

    // Read the initial state only, without synthesizing a transition
    // (LidIsPresent is device presence and never changes; LidIsClosed is the state)
    m_upower->getPropertyAsync("LidIsClosed")
        .onInterface(UPPOWER_IFACE)
        .uponReplyInvoke([](std::optional<sdbus::Error> e, sdbus::Variant lidClosed) {
            if (e) {
                Log::logger->log(Log::WARN, "lid: UPower unavailable ({}); lid switch idle trigger disabled", e->what());
                return;
            }

            Log::logger->log(Log::INFO, "lid: initial state lid closed: {}", lidClosed.get<bool>());

            // Reply is delivered in the main loop, after g_pAuth->start() claimed the
            // sensor: if the lock screen started with the lid closed, release it now
            // (it is physically blocked); the PropertiesChanged handler resumes on open.
            if (lidClosed.get<bool>() && g_pAuth)
                g_pAuth->onIdle();
        });

    m_upower->uponSignal("PropertiesChanged").onInterface(DBUS_PROPS_IFACE).call([](const std::string& interface, const std::map<std::string, sdbus::Variant>& properties) {
        if (interface != UPPOWER_IFACE.c_str())
            return;

        try {
            const bool closed = properties.at("LidIsClosed").get<bool>();
            Log::logger->log(Log::INFO, "lid: lid switch, lid closed: {}", closed);
            if (!g_pAuth)
                return;

            if (closed)
                g_pAuth->onIdle();
            else
                g_pAuth->onResume();
        } catch (std::out_of_range&) {}
    });
}

CDbus::CDbus(bool enable) {
    if (!enable)
        return;
    try {
        m_connection        = sdbus::createSystemBusConnection();
        m_freedesktopLogin1 = sdbus::createProxy(*m_connection, sdbus::ServiceName{"org.freedesktop.login1"}, sdbus::ObjectPath{"/org/freedesktop/login1"});
    } catch (sdbus::Error& e) {
        Log::logger->log(Log::ERR, "dbus: failed to create connection or login proxy ({})", e.what());
        m_freedesktopLogin1.reset();
        m_connection.reset();
        return;
    }

    initLidWatcher();
}
