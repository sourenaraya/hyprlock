# Design

## Context

See proposal.md for motivation. Current state that shapes the approach:

- **Event loop**: hyprlock's poll thread watches the Wayland fd and the sdbus connection fd; the main thread runs `wl_display_dispatch_pending` and `processPendingEvent`. All Wayland events and D-Bus callbacks therefore execute on the main thread — no new synchronization is needed for this change.
- **Auth layer**: `CAuth` holds a vector of `IAuthImplementation` (currently `CPam`, `CFingerprint`) and already fans out lifecycle calls (`init`, `terminate`) to all of them. `CFingerprint` already talks system D-Bus (fprintd, logind) through the shared sdbus connection owned by `CDbus`, and already subscribes to logind `PrepareForSleep` — half-wired: it sets a `sleeping` flag on sleep start but never stops verification, and on wake it only restarts when `!verifying`, which is stale-`true`, leaving the sensor in a zombie state.
- **Protocol facts (verified against sources and a live session)**:
  - Both target compositors expose `ext_idle_notify_v1` v2: Hyprland (its own implementation) and wlroots-based sway — wlroots main has migrated to the staged protocol and the legacy `zwp_idle_notify_manager_v1` is gone from wlroots main. On the reference machine `wayland-info` shows `ext_idle_notifier_v1 v2` and no legacy global.
  - Protocol semantics: the client chooses the timeout (ms); the notification is bound to a `wl_seat`; `get_input_idle_notification` (v2) ignores idle inhibitors; events are `idled` / `resumed` with guaranteed alternation.
  - Both compositors count every keyboard/pointer event as activity, including input delivered to the locked surface. Neither handles lid switches nor suspend catch-up: Hyprland's `setTimers(elapsedMs)` has no callers, and wlroots timers are monotonic, so both freeze across suspend.
  - Lid state: no compositor exposes it. UPower (active on the reference machine, verified 1.91.5) tracks the `SW_LID` evdev device(s); its `UpDaemon` is a GDBus skeleton, so state changes emit `PropertiesChanged` on `/org/freedesktop/UPower`, interface `org.freedesktop.UPower`, on the system D-Bus. The state property is `LidIsClosed` (changes on toggle); `LidIsPresent` is device presence and does not change on toggle.
  - Build: CMake generates protocol bindings with `hyprwayland-scanner`; external protocols are vendored in `protocols/` (existing pattern: `wlr-screencopy-unstable-v1.xml`, `hyprland-surface-v1.xml`).

## Goals / Non-Goals

**Goals:**

- One pause/resume lifecycle for auth implementations, driven by compositor idle detection, lid state, and sleep/wake.
- Fingerprint sensor released while the user is absent (driver protection) and immediately ready on return — including "open lid after inactivity → scan to unlock with no other input".
- Fix the post-wake zombie verification state.
- Zero behavior change when `auth:inactivity_timeout` is 0 (no notification created).

**Non-Goals:**

- Changing compositor/hypridle DPMS or lock behavior.
- Making PAM act on idle/resume (no-op hook only, for future use).
- Supporting the legacy `zwp_idle_notify_manager_v1` protocol (no target compositor exposes it).
- Reading `/dev/input` directly for lid state (requires `input` group membership the reference user lacks).
- Multi-seat support (hyprlock already binds only the first seat).

## Decisions

1. **Single protocol: `ext_idle_notify_v1`.** Verified present (v2) on both target compositors; the legacy wlroots protocol is gone from wlroots main, so a dual-protocol binding would add complexity for no target. Compositors without the global degrade gracefully: one log line, idle trigger off. Alternatives considered: dual binding (rejected — no target needs the legacy one); KDE `org_kde_kwin_idle` (out of scope).
2. **Input-only notification (`get_input_idle_notification`), ignoring idle inhibitors.** A locked screen must idle on real user input; a background app holding an idle inhibitor must not keep the sensor claimed indefinitely. Alternative: the inhibitor-obeying variant (rejected — semantics wrong for a lock screen).
3. **Client-chosen timeout from `auth:inactivity_timeout` (seconds, default 0).** The protocol lets the client pick the timeout, so the pause window is decoupled from the compositor's global idle timeout (hypridle). 0 = no notification = zero behavior change.
4. **Lid via UPower on the system D-Bus**, not `/dev/input` EV_SW: no permission requirements (evdev nodes are `root:input 0660` and the reference user is not in `input`), reuses the existing sdbus system-bus connection and the exact proxy/signal pattern already used for logind and fprintd, no new dependency. Architecturally the compositor owns lid state, but neither exposes it; if one does later, this module deletes without touching the auth interface.
5. **Sleep/wake reuses the existing logind `PrepareForSleep` hook**, routed through the same `onIdle()`/`onResume()` (not a separate code path): deterministic release before suspend, guaranteed restart after wake, and it fixes the stale `verifying` flag.
6. **Interface-level `onIdle()` / `onResume()` on `IAuthImplementation`**, pure virtual, fanned out by `CAuth` to all implementations; implementations must be idempotent. `CPam` implements no-ops (the future hook the upstream maintainer asked for). Alternative: fingerprint-specific activity hooks like PR #962's `notifyActivityToFingerprint` (rejected — duplicates idle detection, unusable by PAM).
7. **No in-app idle timer** — the approach from PR #962 is dropped entirely; compositor activity tracking is strictly more complete.
8. **Vendor the protocol XML** in `protocols/` rather than consuming the system `wayland-protocols` staging dir: avoids raising the `wayland-protocols>=1.35` minimum for distros that ship older versions, and matches the existing vendoring pattern.
9. **One notification per lock session** (bound to the active seat, created after the session lock is acquired). Idle state is per-seat on both compositors; per-output notifications would add nothing.
10. **Lid watcher lives with the D-Bus code** (`CDbus`, where the logind watcher already lives) and is active whenever the system-bus connection exists (i.e. fingerprint enabled), gated by `auth:lid_switch_idle`.

## Risks / Trade-offs

- [Suspend without logind] If a machine suspends without logind delivering `PrepareForSleep` (custom `HandleLidSwitch` setups), both compositors' idle timers freeze with no catch-up, so the sensor stays claimed across the suspend → the standard logind flow is the supported path; verify against the reference machine's actual lid/sleep behavior during testing.
- [Wake with lid still closed (RTC/network wake)] Wake triggers resume, claiming the physically-blocked sensor; the thawed idle timer re-fires `idled` after the configured timeout and re-pauses → self-healing; accepted as a brief wasted claim.
- [UPower absent] Lid trigger off with a logged warning; on machines that don't sleep on lid close, the sensor stays claimed until the idle timeout fires → idle trigger still covers the "away, lid open" case.
- [Initial lid state not observed] UPower/evdev are event-based; if hyprlock started with the lid closed (rare), no transition fires → accepted; the spec explicitly forbids synthesizing an initial transition.
- [Older wlroots compositors] No `ext_idle_notify_v1` global → idle trigger off (logged); fingerprint keeps today's behavior plus the sleep fix.
- [Protocol event alternation] The compositor guarantees `idled`/`resumed` alternation; client-side state guards (done/aborted/sleeping) make the implementation idempotent regardless.

## Migration Plan

Additive change. Defaults: `auth:inactivity_timeout` = 0 (no idle-notify binding for existing users), `auth:lid_switch_idle` = 1 (new intended behavior). The sleep/wake rewiring changes existing sleep behavior — deliberately, as a bug fix. No persisted state anywhere; rollback is reverting the change.

## Open Questions

None blocking. Empirical confirmation that UPower emits `PropertiesChanged` on a physical lid toggle is covered by a test task (source analysis shows the Linux backend updates the exported property on lid events).
