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
    m_upower->getPropertyAsync("LidIsPresent")
        .onInterface(UPPOWER_IFACE)
        .uponReplyInvoke([](std::optional<sdbus::Error> e, sdbus::Variant lidPresent) {
            if (e) {
                Log::logger->log(Log::WARN, "lid: UPower unavailable ({}); lid switch idle trigger disabled", e->what());
                return;
            }

            Log::logger->log(Log::INFO, "lid: initial state lid present: {}", lidPresent.get<bool>());
        });

    m_upower->uponSignal("PropertiesChanged").onInterface(DBUS_PROPS_IFACE).call([](const std::string& interface, const std::map<std::string, sdbus::Variant>& properties) {
        if (interface != UPPOWER_IFACE.c_str())
            return;

        try {
            const bool present = properties.at("LidIsPresent").get<bool>();
            Log::logger->log(Log::INFO, "lid: lid switch, lid present: {}", present);
            if (!g_pAuth)
                return;

            if (present)
                g_pAuth->onResume();
            else
                g_pAuth->onIdle();
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
