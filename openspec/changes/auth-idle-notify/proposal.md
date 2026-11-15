# Proposal

## Why

On laptops, hyprlock's fingerprint auth degrades during long lock sessions: while the user is away or the lid is closed, the sensor stays claimed and "verifying", and several drivers (e.g. `goodixtls-55x4`, see hyprwm/hyprlock#702) disconnect or disable it — after which the sensor cannot be used to unlock. The existing logind sleep handling also leaves stale state after a lid-close/wake cycle, so verification never restarts. The alternative proposed in PR hyprwm/hyprlock#962 (a custom in-app inactivity timer wired into raw pointer/key callbacks) is a partial hand-rolled idle implementation that the compositor already provides. Using the compositor's `ext_idle_notify_v1` protocol (exposed by both wlroots-based sway and Hyprland) plus system lid/sleep signals gives correct pause/resume semantics without any in-app idle timer.

## What Changes

- Add an idle/resume lifecycle to the auth interface: `onIdle()` / `onResume()` on `IAuthImplementation`, fanned out by `CAuth` to all active implementations.
- Bind the `ext_idle_notify_v1` Wayland protocol (vendored protocol XML + CMake scanner target). When `auth:inactivity_timeout` is greater than 0, create one input-idle notification bound to the seat after the session lock is acquired; `idled` pauses all auth implementations, `resumed` resumes them.
- Fingerprint implementation: pause = stop verification + release device + clear prompt; resume = restart verification, re-claiming the device if it is gone. PAM implementation: no-op (hook for future use).
- Lid switch without sleep (machines configured to stay awake on AC power): watch UPower's `LidIsPresent` over the existing system D-Bus connection, gated by `auth:lid_switch_idle` (default on). Lid closed → idle transition; lid open → resume transition.
- Fix sleep/wake: route the existing logind `PrepareForSleep` handling in `CFingerprint` through `onIdle()` / `onResume()`, fixing the zombie state where a stale `verifying` flag after wake prevented verification from restarting.
- New config keys: `auth:inactivity_timeout` (INT, seconds, default `0` = idle-notify disabled) and `auth:lid_switch_idle` (INT, default `1`).
- Graceful degradation: if the `ext_idle_notify_v1` global is absent (older wlroots compositors) or UPower is unreachable, the affected trigger is disabled with a single log line; core locking behavior is unchanged.

## Capabilities

### New Capabilities

- `auth-idle`: auth implementations pause and resume in response to user inactivity (idle-notify protocol), lid switch, and session sleep/wake.

### Modified Capabilities

(none — the project has no existing specs)

## Impact

- Code: `src/auth/Auth.{hpp,cpp}` (interface + fan-out), `src/auth/Fingerprint.{hpp,cpp}` (pause/resume + logind rewiring), `src/auth/Pam.{hpp,cpp}` (no-ops), `src/core/hyprlock.{hpp,cpp}` (global binding, notification creation, event dispatch), `src/core/Dbus.{hpp,cpp}` (UPower lid watcher), `src/config/ConfigManager.cpp` (two new keys).
- Build: `protocols/ext-idle-notify-v1.xml` (vendored, MIT-licensed) plus one `protocolnew()` line in `CMakeLists.txt`.
- No new runtime dependencies: `ext_idle_notify_v1` is a standard Wayland global; logind and UPower are D-Bus services (UPower is optional at runtime).
- No event-loop changes: all new events are delivered through the existing main-thread dispatch (Wayland `wl_display_dispatch_pending` and the sdbus connection).
