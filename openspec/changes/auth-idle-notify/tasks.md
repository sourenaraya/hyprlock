# Tasks

## 1. Protocol and config groundwork

- [x] 1.1 Vendor `protocols/ext-idle-notify-v1.xml` (copy from `/usr/share/wayland-protocols/staging/ext-idle-notify/ext-idle-notify-v1.xml`, keeping its MIT copyright header) and add `protocolnew("protocols" "ext-idle-notify-v1" true)` to `CMakeLists.txt`; verify a fresh CMake configure/build generates `protocols/ext-idle-notify-v1.{hpp,cpp}` and hyprlock compiles
- [x] 1.2 Add `auth:inactivity_timeout` (INT, default 0) and `auth:lid_switch_idle` (INT, default 1) in `CConfigManager::init()` next to the other `auth:` keys; verify a config containing both keys starts hyprlock without unknown-value errors (the same config fails before the change)
- [x] 1.3 Bind the `ext_idle_notify_v1` global in the registry callback of `CHyprlock::run()` (store the manager in `m_sWaylandState`, like `screencopy`); verify the log shows the bound global on the reference machine and a single "not available" line when the global is absent (e.g. point at a compositor without it, or temporarily rename the interface match)

## 2. Auth idle/resume interface

- [x] 2.1 Add pure-virtual `onIdle()` and `onResume()` to `IAuthImplementation` in `src/auth/Auth.hpp` and add `CAuth::onIdle()` / `CAuth::onResume()` that fan out to every implementation; verify the project compiles (implementations get empty bodies in this task, filled in groups 3 and 4)

## 3. Fingerprint pause/resume and idle-notify wiring

- [ ] 3.1 Implement `CFingerprint::onIdle()` (stop verification, release device, clear prompt, force widget update; no-op when done/aborted) and `CFingerprint::onResume()` (restart verification — including re-claim via the existing `!device` path — unless done/aborted); verify manually with a 5 s `auth:inactivity_timeout`: log shows pause (device released, prompt cleared) after 5 s idle and resume on keypress, and a fingerprint scan unlocks after resume
- [ ] 3.2 After a successful `acquireSessionLock()`, create one input-idle notification (`get_input_idle_notification`, seat from `g_pSeatManager`, timeout = `auth:inactivity_timeout` in ms) only when the timeout is > 0, and dispatch `idled`/`resumed` to `g_pAuth->onIdle()` / `g_pAuth->onResume()` with a `g_pAuth` null guard; verify: with timeout > 0 the pause/resume cycle from 3.1 fires with no manual help, and with timeout 0 no notification is created (log) and no inactivity pause ever occurs
- [ ] 3.3 Rewire the logind `PrepareForSleep` handler in `CFingerprint::init()` to call `onIdle()` on sleep start and `onResume()` on wake (keep the `sleeping` guard that suppresses `VerifyStatus` handling); verify on a battery-powered sleep cycle: log shows stop+release before suspend and re-verify after wake, and a scan unlocks immediately after wake with no keypress (this also fixes the stale-`verifying` zombie state)

## 4. PAM no-op and lid switch trigger

- [ ] 4.1 Implement `CPam::onIdle()` / `CPam::onResume()` as no-ops with a comment marking the hook for future use; verify a PAM-only configuration (fingerprint disabled) locks and unlocks normally with idle/lid/sleep transitions occurring (log lines, unchanged PAM conversation)
- [ ] 4.2 Add a UPower lid watcher to `CDbus` (where the logind proxy already lives): proxy to `org.freedesktop.UPower` on `/org/freedesktop/UPower`, read the initial `LidIsPresent` without synthesizing a transition, and subscribe to `PropertiesChanged` so lid-closed → `g_pAuth->onIdle()` and lid-open → `g_pAuth->onResume()`, gated by `auth:lid_switch_idle` and `g_pAuth` availability; if UPower is unreachable, log one warning and disable the trigger; verify on the reference machine (AC power, lid close does not sleep): closing the lid pauses immediately, opening it resumes and a scan unlocks with no other input; `systemctl stop upower` then locking logs the warning and everything else still works; `auth:lid_switch_idle = 0` ignores lid changes

## 5. Docs and integration

- [x] 5.1 Document `auth:inactivity_timeout` and `auth:lid_switch_idle` in `assets/example.conf` (commented, with the seconds/boolean semantics); verify the built hyprlock accepts the shipped example.conf
- [ ] 5.2 Run the full verification matrix on the laptop: (a) timeout > 0, away with lid open → pause after timeout, resume on input; (b) close lid on AC → immediate pause, open → immediate resume + scan works; (c) battery sleep cycle → release before suspend, resume + scan after wake; (d) timeout 0 → no idle-notify behavior at all; (e) fingerprint disabled (PAM only) → all triggers are no-ops; (f) long lock (>30 min, lid cycled) → no "verify-disconnected" aborts (regression check for #702); verify each step by logs and successful fingerprint unlock
- [x] 5.3 Review the diff for compositor-specific assumptions (binding must key off the `ext_idle_notify_v1` global name only; no sway/Hyprland-specific paths); verify by code inspection that a Hyprland session would exercise the identical code path

## Workflow follow-up

- Archive the change (`openspec-archive-change`) after tasks 1–5 are complete and verified.
