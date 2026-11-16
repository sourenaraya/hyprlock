# Spec Delta

## Purpose

Pause and resume of hyprlock's authentication implementations (fingerprint, PAM) in response to user inactivity, laptop lid switch, and session sleep/wake, so a fingerprint sensor is not held in a claimed "verifying" state while the user is absent (which breaks several sensor drivers) and is immediately ready to unlock when the user returns.

## ADDED Requirements

### Requirement: Auth idle/resume lifecycle
The lock screen SHALL notify every active authentication implementation when user activity transitions to idle, and SHALL notify all active implementations again when activity resumes. Implementations MUST tolerate receiving the same transition repeatedly (transitions are idempotent).

#### Scenario: Transition to idle
- **WHEN** an idle trigger source reports that the user has been inactive
- **THEN** every active authentication implementation receives exactly one idle notification for that transition

#### Scenario: Transition to resume
- **WHEN** an idle trigger source reports that user activity has resumed
- **THEN** every active authentication implementation receives exactly one resume notification for that transition

### Requirement: Compositor idle notification
When `auth:inactivity_timeout` (integer seconds) is greater than 0 and the compositor exposes the `ext_idle_notify_v1` Wayland protocol, hyprlock SHALL create, after the session lock is acquired, an input-only idle notification bound to the active seat with a timeout equal to the configured value. The notification MUST ignore idle-inhibit requests from other clients. An `idled` event SHALL pause all active authentication implementations and a `resumed` event SHALL resume them.

#### Scenario: Idle after inactivity
- **WHEN** no keyboard or pointer input occurs for the configured `auth:inactivity_timeout` while locked
- **THEN** all active authentication implementations are paused

#### Scenario: Resume on input
- **WHEN** any keyboard or pointer input occurs after an idle transition
- **THEN** all active authentication implementations are resumed

#### Scenario: Idle timeout disabled
- **WHEN** `auth:inactivity_timeout` is 0
- **THEN** no idle notification is created and no inactivity-based pause occurs

#### Scenario: Protocol unavailable
- **WHEN** the compositor does not expose `ext_idle_notify_v1`
- **THEN** hyprlock logs a single warning, continues to lock, and all other idle trigger sources remain active

### Requirement: Fingerprint pause and resume
While idle, the fingerprint implementation SHALL stop verification, release the sensor, and clear the displayed fingerprint prompt. On resume, if authentication has not completed and has not been aborted, it SHALL restart verification, re-claiming the sensor if the device is gone.

#### Scenario: Pause releases the sensor
- **WHEN** an idle transition occurs while fingerprint verification is in progress
- **THEN** verification is stopped, the sensor is released, and the fingerprint prompt is cleared

#### Scenario: Resume after device loss
- **WHEN** a resume transition occurs and the sensor device is no longer claimed
- **THEN** the device is re-claimed and verification restarts, so a scan unlocks immediately

#### Scenario: Resume after final outcome
- **WHEN** a resume transition occurs after authentication succeeded or was aborted (e.g. too many failed attempts)
- **THEN** no verification is restarted

### Requirement: Lid switch trigger
When `auth:lid_switch_idle` (integer, 1) is enabled and the UPower service is reachable on the system D-Bus, hyprlock SHALL treat a lid-closed transition as an idle transition and a lid-open transition as a resume transition, regardless of whether the session suspends. Lid transitions MUST be detected on state changes, and the initial state at lock time MUST NOT synthesize a transition; if the initial state indicates the lid is closed, hyprlock MUST pause authentication as soon as the state is read, so the fingerprint sensor is not left claimed while the lid is closed. If UPower is unreachable, hyprlock SHALL log a warning and disable this trigger without affecting other triggers.

#### Scenario: Lid closed while the machine stays awake
- **WHEN** the lid is closed on a machine configured not to sleep (e.g. on AC power)
- **THEN** authentication implementations are paused immediately

#### Scenario: Lid open after inactivity
- **WHEN** the lid is opened after any period of inactivity while locked
- **THEN** authentication implementations resume immediately and the fingerprint sensor is ready to be used without any other input

#### Scenario: Lock screen starts with the lid closed
- **WHEN** hyprlock starts while the lid is already closed (e.g. launched by a compositor idle lock after the lid was closed)
- **THEN** the fingerprint sensor is not left in the verifying state, and opening the lid resumes authentication with the sensor ready to use

#### Scenario: UPower unavailable
- **WHEN** the UPower service is not reachable while `auth:lid_switch_idle` is enabled
- **THEN** a warning is logged, lid transitions are ignored, and all other trigger sources continue to work

#### Scenario: Lid trigger disabled
- **WHEN** `auth:lid_switch_idle` is 0
- **THEN** lid state changes have no effect on authentication implementations

### Requirement: Sleep and wake trigger
hyprlock SHALL treat logind's `PrepareForSleep(true)` as an idle transition and `PrepareForSleep(false)` as a resume transition. On wake, the fingerprint implementation SHALL restart verification even if the pre-sleep state indicated verification was in progress. Verification status signals received while the session is asleep SHALL be ignored.

#### Scenario: Sleep while verifying
- **WHEN** the system begins preparing for sleep while fingerprint verification is in progress
- **THEN** verification is stopped and the sensor is released before suspension

#### Scenario: Wake resumes verification
- **WHEN** the system wakes from sleep while locked
- **THEN** fingerprint verification restarts and the sensor is ready to be used immediately

### Requirement: PAM idle no-op
The PAM implementation SHALL implement the idle and resume notifications without side effects on its authentication conversation, reserving the hook for future use.

#### Scenario: Transitions while PAM is active
- **WHEN** an idle or resume transition occurs while PAM authentication is active
- **THEN** the PAM conversation state is unchanged
