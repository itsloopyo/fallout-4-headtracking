// SPDX-License-Identifier: MIT

#include "pch.h"
#include "mod.h"
#include "logging.h"
#include "path_utils.h"
#include "session_end.h"
#include "diagnostics/isolated_input.h"
#include "game/weapon_debris.h"
#include "hooks/camera_hook.h"
#include "ui/notification.h"

#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/hooks/hook_manager.h>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/memory/pe_fingerprint.h>
#include <cameraunlock/tracking/tracking_mode.h>

namespace Fallout4HT {

namespace {

// Per-frame cache window - if GetProcessedRotation is called twice within this
// interval (e.g. when PlayerCamera::Update fires multiple times per frame for
// shadow/reflection cameras) the second call returns the cached result.
constexpr uint64_t kProcessCacheWindowMicros = 1000;

uint64_t GetTimeMicros() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // QuadPart * 1000000 overflows int64 after ~10 days of system uptime
    // (10 MHz QPC). Split into whole-second and remainder terms so the
    // product never exceeds 64 bits.
    const uint64_t q = static_cast<uint64_t>(now.QuadPart);
    const uint64_t f = static_cast<uint64_t>(freq.QuadPart);
    return (q / f) * 1000000ULL + ((q % f) * 1000000ULL) / f;
}

// The first thing a "it does not work" report needs is which copy of the game is
// running. The Steam EXE's FileVersion is the SteamStub wrapper's rather than
// the game's, so it is not the honest identifier; the PE fingerprint is, and the
// module path separates a Steam install from a GOG or Game Pass one at a glance.
void LogGameBuild() {
    HMODULE game = GetModuleHandleA(GAME_EXE);
    if (!game) {
        Log::Line("ERROR: %s is not loaded", GAME_EXE);
        return;
    }

    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(game, path, MAX_PATH) == 0) {
        strcpy_s(path, GAME_EXE);
    }

    cameraunlock::memory::PeFingerprint fingerprint{};
    if (!cameraunlock::memory::ReadPeFingerprint(game, fingerprint)) {
        Log::Line("WARN: could not read the PE headers of %s", path);
        return;
    }

    Log::Line("game build: %s (TimeDateStamp 0x%08X, SizeOfImage 0x%08X, CheckSum 0x%08X)",
              path, fingerprint.TimeDateStamp, fingerprint.SizeOfImage, fingerprint.CheckSum);
}

const char* DofModeName(cameraunlock::TrackingMode mode) {
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition: return "6DOF (rotation + position)";
        case cameraunlock::TrackingMode::RotationOnly:        return "3DOF rotation only";
        case cameraunlock::TrackingMode::PositionOnly:        return "3DOF position only";
        default:                                              return "unknown";
    }
}

const char* AxisIsolationName(AxisIsolation mode) {
    switch (mode) {
        case AxisIsolation::PitchOnly: return "PITCH only";
        case AxisIsolation::YawOnly:   return "YAW only";
        case AxisIsolation::RollOnly:  return "ROLL only";
        case AxisIsolation::None:      break;
    }
    return "normal";
}

// Spelled out rather than counted modulo a mode total, so adding a mode is one
// edit and the compiler flags the switch that has not kept up.
AxisIsolation NextAxisIsolation(AxisIsolation mode) {
    switch (mode) {
        case AxisIsolation::None:      return AxisIsolation::PitchOnly;
        case AxisIsolation::PitchOnly: return AxisIsolation::YawOnly;
        case AxisIsolation::YawOnly:   return AxisIsolation::RollOnly;
        case AxisIsolation::RollOnly:  break;
    }
    return AxisIsolation::None;
}

} // namespace

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) {
        Log::Line("WARN: Mod already initialized");
        return true;
    }

    Log::Line("Fallout 4 Head Tracking v%s initializing...", VERSION);

    LogGameBuild();

    if (!LoadConfig()) {
        Log::Line("WARN: Using default configuration");
    }

    ConfigureSession();

    if (!InitializeHooks()) {
        Log::Line("WARN: Some hooks failed to initialize - mod may have limited functionality");
    }

    m_udpReceiver.SetLog([](const std::string& msg) {
        Log::Line("%s", msg.c_str());
    });
    if (m_udpReceiver.Start(static_cast<uint16_t>(m_config.udp_port))) {
        Log::Line("UDP receiver started on port %d", m_config.udp_port);
    } else {
        Log::Line("WARN: UDP port %d is held by another process - receiver will retry in the background",
                  m_config.udp_port);
    }

    m_enabled.store(m_config.enable_on_startup);
    Log::Line("%s", m_config.enable_on_startup
                        ? "Head tracking enabled at startup"
                        : "Head tracking disabled at startup (EnableOnStartup is off)");

    m_initialized.store(true);

    Log::Line("Initialization complete (camera:%s)",
              m_cameraHookInstalled ? "OK" : "FAILED");

    std::string startupMsg = "Fallout 4 Head Tracking v";
    startupMsg += VERSION;
    startupMsg += " - ";
    startupMsg += m_enabled.load() ? "ENABLED" : "DISABLED";
    Notify(startupMsg.c_str());

    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) {
        return;
    }

    Log::Line("Shutting down...");
    m_udpReceiver.Stop();
    ShutdownHooks();
    m_initialized.store(false);
    Log::Line("Shutdown complete");
}

bool Mod::LoadConfig() {
    namespace cfg = cameraunlock::config;
    const std::wstring folder = GetModuleDirectoryW();
    if (folder.empty()) {
        // Module directory lookup failed - refuse to fall back to a CWD-relative
        // config, since that would silently read/write the wrong file.
        Log::Line("ERROR: Could not resolve module directory for CameraUnlock.ini - using built-in "
                  "defaults, and nothing is saved this session");
        m_config = MakeConfigTable().defaults();
        return false;
    }

    cfg::ConfigOwnerOptions<Config> options = MakeConfigOwnerOptions(folder, cfg::DefaultsFile::PerUser());
    // The mod has no overlay, so a message for the player goes where every other
    // notice of this mod goes.
    options.status_sink = [](const std::string& message) { ShowNotification(message.c_str()); };
    m_configOwner.emplace(std::move(options));

    const cfg::ConfigLoadResult<Config> loaded = m_configOwner->Load();
    for (const std::string& line : loaded.log) Log::Line("%s", line.c_str());
    Log::Line("Config: %s", cfg::ConfigLoadStatusName(loaded.status));
    // Every status hands back the settings to run on.
    m_config = loaded.config;
    return loaded.status == cfg::ConfigLoadStatus::Canonical || loaded.status == cfg::ConfigLoadStatus::Migrated ||
           loaded.status == cfg::ConfigLoadStatus::Created;
}

void Mod::SaveConfig(const std::function<void(Config&)>& change) {
    namespace cfg = cameraunlock::config;
    if (!m_configOwner) {
        Log::Line("WARN: not saved: CameraUnlock.ini has no known folder this session");
        return;
    }
    const cfg::ConfigSaveResult saved = m_configOwner->Save(change);
    for (const std::string& line : saved.log) Log::Line("%s", line.c_str());
    if (saved.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("WARN: config save %s: %s", cfg::ConfigSaveStatusName(saved.status), saved.reason.c_str());
    }
}

void Mod::ConfigureSession() {
    Log::Line("Smoothing: local=%.2f remote=%.2f", m_config.local_smoothing, m_config.remote_smoothing);

    m_worldSpaceYaw.store(m_config.world_space_yaw);
    Log::Line("Yaw mode: %s", m_worldSpaceYaw.load() ? "horizon-locked (world)" : "camera-local");

    m_aimMode.store(cameraunlock::ads::DecodeAimMode(m_config.true_free_look, m_config.free_look_marker));
    Log::Line("%s", cameraunlock::ads::AimModeLabel(m_aimMode.load()));

    // The table reads a pair that names no mode as its defaults, so every
    // loaded pair decodes.
    m_session.SetMode(cameraunlock::DecodeTrackingMode(m_config.rotation_enabled, m_config.position_enabled).value());
    m_session.GetPositionProcessor().SetSettings(m_config.position);

    // After SetSettings, never before: the session hands both values to the
    // rotation and the position processor, and the connection flag that picks
    // between them is fed from the receiver inside Update().
    m_session.SetLocalSmoothing(m_config.local_smoothing);
    m_session.SetRemoteSmoothing(m_config.remote_smoothing);
    const cameraunlock::PositionSettings& limits = m_config.position;
    Log::Line("Position processor initialized (%s, limits x=%.2f up=%.2f down=%.2f forward=%.2f back=%.2f)",
              DofModeName(m_session.GetMode()), limits.limit_x, limits.limit_y, limits.limit_y_down,
              limits.limit_z, limits.limit_z_back);
}

bool Mod::InitializeHooks() {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    using cameraunlock::hooks::HookStatusToString;

    HookStatus status = HookManager::Instance().Initialize();
    if (status != HookStatus::Ok) {
        Log::Line("ERROR: MinHook initialization failed: %s", HookStatusToString(status));
        return false;
    }

    // Before the camera hook, and not gated on it: a session whose camera hook
    // failed still has to be able to say whether it ended or died, because
    // that is precisely the log someone will be reading.
    InstallSessionEndMarker();
    WeaponDebris::InstallPrefsWriteGuard();

    m_cameraHookInstalled = InstallCameraHook();
    if (m_cameraHookInstalled) {
        Log::Line("Camera hook installed");
    } else {
        Log::Line("WARN: Camera hook failed - head tracking disabled");
    }

#if FALLOUT4_DEV_HOTKEYS
    StartIsolatedInputIfAsked();
#endif
    if (!m_hotkeys.Start(m_config)) {
        Log::Line("WARN: Hotkey setup failed - hotkeys won't work");
    }

    status = HookManager::Instance().EnableAllHooks();
    if (status != HookStatus::Ok) {
        Log::Line("WARN: Failed to enable some hooks: %s", HookStatusToString(status));
    }

    return m_cameraHookInstalled;
}

void Mod::ShutdownHooks() {
    m_hotkeys.Stop();

    if (m_cameraHookInstalled) {
        RemoveCameraHook();
        m_cameraHookInstalled = false;
    }

    cameraunlock::hooks::HookManager::Instance().Shutdown();
}

void Mod::Notify(const char* message) const {
    if (m_config.show_notifications) {
        ShowNotification(message);
    }
}

// The session re-reads the receiver's source-address check every update, so a
// player who switches from a local OpenTrack instance to a phone on WiFi
// mid-session gets the other smoothing parameter without restarting the game.
// Nothing recorded that until now, so "the camera feels different than the
// value I set" had no answer in the log. Change-gated: one line per switch, not
// one per frame.
void Mod::LogConnectionLocality() {
    const bool isRemote = m_session.IsRemoteConnection();
    if (m_remoteConnectionKnown && isRemote == m_remoteConnection) return;

    m_remoteConnection = isRemote;
    m_remoteConnectionKnown = true;
    Log::Line("Tracker source is %s - smoothing=%.2f",
              isRemote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(
                  m_config.local_smoothing, m_config.remote_smoothing, isRemote));
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        Log::Line("Head tracking %s", enabled ? "enabled" : "disabled");
        Notify(enabled ? "Head Tracking: ON" : "Head Tracking: OFF");
    }
}

void Mod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void Mod::CycleDofMode() {
    const cameraunlock::TrackingMode mode = m_session.CycleMode();
    const char* name = DofModeName(mode);
    Log::Line("DOF mode: %s", name);

    std::string msg = "Mode: ";
    msg += name;
    Notify(msg.c_str());

    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    SaveConfig([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

void Mod::CycleAxisIsolation() {
    const AxisIsolation mode = NextAxisIsolation(m_axisIsolation.load());
    m_axisIsolation.store(mode);

    const char* name = AxisIsolationName(mode);
    Log::Line("Axis isolation: %s", name);

    std::string msg = "Axis isolation: ";
    msg += name;
    Notify(msg.c_str());
}

Mod::PipelineSample Mod::LastPipelineSample() const {
    PipelineSample out{};
    out.deltaTime = m_lastDeltaTime;
    out.rawYaw = m_session.GetLastRaw().yaw;
    out.rawPitch = m_session.GetLastRaw().pitch;
    out.interpolatedYaw = m_session.GetLastInterpolated().yaw;
    out.processedYaw = m_cachedYaw;
    out.processedPitch = m_cachedPitch;
    out.newSample = m_session.WasNewSample();
    out.rejectedPackets = m_udpReceiver.GetRejectedPacketCount();
    out.frozenPackets = m_udpReceiver.GetFrozenPacketCount();
    return out;
}

void Mod::ToggleExtrapolation() {
    const bool wasOn = m_session.GetMaxExtrapolationFraction() > 0.0f;
    m_session.SetMaxExtrapolationFraction(wasOn ? 0.0f : kDefaultExtrapolationFraction);

    Log::Line("Extrapolation: %s", wasOn ? "OFF" : "ON");
    Notify(wasOn ? "Extrapolation: OFF" : "Extrapolation: ON");
}

void Mod::ToggleYawMode() {
    const bool newValue = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(newValue);

    Log::Line("Yaw mode: %s", newValue ? "horizon-locked (world)" : "camera-local");
    Notify(newValue ? "Yaw Mode: Horizon-locked" : "Yaw Mode: Camera-local");

    SaveConfig([newValue](Config& c) { c.world_space_yaw = newValue; });
}

void Mod::CycleAimMode() {
    const cameraunlock::ads::AimMode mode = cameraunlock::ads::NextAimMode(m_aimMode.load());
    m_aimMode.store(mode);

    Log::Line("%s", cameraunlock::ads::AimModeLabel(mode));
    Notify(cameraunlock::ads::AimModeLabel(mode));

    const cameraunlock::ads::AimModePair pair = cameraunlock::ads::EncodeAimMode(mode);
    SaveConfig([pair](Config& c) {
        c.true_free_look = pair.trueFreeLook;
        c.free_look_marker = pair.freeLookMarker;
    });
}

void Mod::CycleTrackerSource() {
    m_udpReceiver.CycleSource();
    Log::Line("cycling to the next tracker source");
    Notify("Switching tracker source");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    const uint64_t now = GetTimeMicros();
    if (m_lastProcessTime > 0 && (now - m_lastProcessTime) < kProcessCacheWindowMicros) {
        yaw = m_cachedYaw;
        pitch = m_cachedPitch;
        roll = m_cachedRoll;
        return m_cachedValid;
    }
    m_lastProcessTime = now;

    // Keep saying it. A second tracker app on the same port makes the head pose
    // alternate between two sources, and two apps rarely agree on scaling, so it
    // reads as the view jumping between two different amounts of head rotation -
    // indistinguishable from a mod bug, and it has been mistaken for one at
    // length. Once at startup is not enough: the message scrolls away before the
    // player is in the game, and by the time they see the symptom there is
    // nothing on screen connecting it to their setup.
    const uint64_t rejected = m_udpReceiver.GetRejectedPacketCount();
    if (rejected > 0) {
        const uint64_t nowMs = GetTickCount64();
        if (!m_warnedSecondSource || nowMs - m_lastSecondSourceWarnMs >= kSecondSourceWarnIntervalMs) {
            m_warnedSecondSource = true;
            m_lastSecondSourceWarnMs = nowMs;
            const std::string& sourceKeys = m_config.cycle_tracker_source_key_name;
            Log::Line("WARNING: more than one app is sending head tracking to port %d"
                      " (%llu packets ignored). Two sources make the view jump between two"
                      " poses, and the ignored one reaches the game with nothing at all."
                      " Close every tracker app but one - OpenTrack"
                      " and a vendor tool like Tobii Game Hub both send here - or %s%s%s.",
                      m_config.udp_port, static_cast<unsigned long long>(rejected),
                      sourceKeys.empty() ? "bind [Hotkeys] CycleTrackerSourceKey in CameraUnlock.ini" : "press ",
                      sourceKeys.c_str(),
                      sourceKeys.empty() ? "" : " to drive from the other one");
            Notify("Two tracker apps sending - close one");
        }
    }

    m_lastDeltaTime = m_frameClock.Tick();
    if (!m_session.Update(m_lastDeltaTime)) {
        // Hold the last pose rather than reporting nothing. The session returns
        // false the moment the receiver has no fresh rotation, which a single
        // dropped packet burst is enough to cause - and dropping the rotation
        // for those frames snaps the view to the untracked orientation and back,
        // which reads as something fighting the camera. Holding is also what the
        // tracking-loss doctrine asks for: never snap to centre.
        static uint64_t s_gaps = 0;
        if (m_cachedValid && ((++s_gaps & (s_gaps - 1)) == 0)) {
            Log::Line("tracker gap - holding last pose (%llu so far)",
                      static_cast<unsigned long long>(s_gaps));
        }
        yaw = m_cachedYaw;
        pitch = m_cachedPitch;
        roll = m_cachedRoll;
        return m_cachedValid;
    }
    LogConnectionLocality();

    m_cachedValid = m_session.GetRotation(yaw, pitch, roll);

    switch (m_axisIsolation.load(std::memory_order_relaxed)) {
        case AxisIsolation::PitchOnly: yaw = 0.0f; roll = 0.0f; break;
        case AxisIsolation::YawOnly:   pitch = 0.0f; roll = 0.0f; break;
        case AxisIsolation::RollOnly:  yaw = 0.0f; pitch = 0.0f; break;
        case AxisIsolation::None:      break;
    }

    m_cachedYaw = yaw;
    m_cachedPitch = pitch;
    m_cachedRoll = roll;

    return m_cachedValid;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    // A mode without position is not a tracker gap. Holding the last offset is
    // right when packets stop, and wrong here: cycling to rotation-only would
    // otherwise leave the camera leaning at whatever the final sample was, with
    // no way back short of cycling position on again.
    if (!m_session.IsPositionActive()) {
        m_cachedPosX = m_cachedPosY = m_cachedPosZ = 0.0f;
        m_cachedPosValid = false;
        x = y = z = 0.0f;
        return false;
    }

    if (m_session.GetPositionOffset(x, y, z)) {
        m_cachedPosX = x;
        m_cachedPosY = y;
        m_cachedPosZ = z;
        m_cachedPosValid = true;
        return true;
    }
    x = m_cachedPosX;
    y = m_cachedPosY;
    z = m_cachedPosZ;
    return m_cachedPosValid;
}

bool Mod::GetLastRotation(float& yaw, float& pitch, float& roll) const {
    yaw = m_cachedYaw;
    pitch = m_cachedPitch;
    roll = m_cachedRoll;
    return m_cachedValid;
}

bool Mod::GetLastPositionOffset(float& x, float& y, float& z) const {
    x = m_cachedPosX;
    y = m_cachedPosY;
    z = m_cachedPosZ;
    return m_cachedPosValid;
}

} // namespace Fallout4HT
