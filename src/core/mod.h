// SPDX-License-Identifier: MIT

#pragma once

#include "config.h"
#include "hotkeys.h"

#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/time/frame_clock.h>
#include <cameraunlock/tracking/head_tracking_session.h>

namespace Fallout4HT {

// Diagnostic mode cycled by Ctrl+Shift+I: zeroes every axis but the named one,
// so a suspect axis can be watched on its own.
enum class AxisIsolation {
    None = 0,
    PitchOnly,
    YawOnly,
    RollOnly,
};

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void CycleDofMode();
    void ToggleYawMode();

    // Step to a different tracker app when more than one is sending to the port.
    void CycleTrackerSource();
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    void CycleAxisIsolation();

    // Turns the interpolators' velocity extrapolation on and off at runtime.
    //
    // Extrapolation continues past the newest known pose by up to half a sample
    // interval and then holds there until the next sample lands, so at a render
    // rate well above the tracker's sample rate every sample period ends in an
    // overshoot and a correction. That is a wobble at the tracker's rate, and it
    // is present in every camera mode - first person, third person and VATS -
    // because it happens before the camera is ever touched.
    void ToggleExtrapolation();

    // One frame's worth of the pipeline's internals, for the pose trace. The
    // stage where the signal is lost is only visible by comparing them: what
    // arrived, what the interpolator made of it, and what came out.
    struct PipelineSample {
        float deltaTime;
        float rawYaw;
        float rawPitch;
        float interpolatedYaw;
        float processedYaw;
        float processedPitch;
        bool newSample;
        // Cumulative receiver counters, so a tick with nothing new can name its
        // reason: a second tracker app, or a tracker repeating one pose.
        uint64_t rejectedPackets;
        uint64_t frozenPackets;
    };
    PipelineSample LastPipelineSample() const;

    // Per-frame, called by the camera hook: runs the tracking pipeline
    // (receiver -> interpolator -> processor) for this frame and returns the
    // processed YPR in degrees. False = no valid data this frame.
    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);

    // 6DOF positional offset (meters) computed by the same frame's pipeline
    // run; call after GetProcessedRotation. False = position disabled or no
    // data.
    bool GetPositionOffset(float& x, float& y, float& z);

    // Report what the last frame produced WITHOUT advancing the pipeline, for
    // callers that are not the render thread. The pipeline is single-threaded
    // by contract (HeadTrackingSession::Update on the render thread), so the
    // hotkey-thread diagnostics have to read rather than run it.
    bool GetLastRotation(float& yaw, float& pitch, float& roll) const;
    bool GetLastPositionOffset(float& x, float& y, float& z) const;

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();
    void ConfigureSession();
    bool InitializeHooks();
    void ShutdownHooks();

    // Surface a message to the player, honouring the ShowNotifications setting.
    // The single place that setting is consulted.
    void Notify(const char* message) const;

    // Frame dt is clamped to this ceiling so a stall (alt-tab, load hitch)
    // cannot feed a huge dt into the smoothing/extrapolation math.
    static constexpr float kMaxFrameDtSeconds = 0.1f;

    // The core library's default, restored when extrapolation is toggled back on.
    static constexpr float kDefaultExtrapolationFraction = 0.5f;

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};
    cameraunlock::time::FrameClock m_frameClock{kMaxFrameDtSeconds};

    Hotkeys m_hotkeys;

    // Yaw mode: true = horizon-locked (world), false = camera-local
    std::atomic<bool> m_worldSpaceYaw{true};

    std::atomic<AxisIsolation> m_axisIsolation{AxisIsolation::None};

    // Per-frame cache - PlayerCamera::Update can fire more than once per frame
    // (shadow/reflection passes); the pipeline must only advance once.
    uint64_t m_lastProcessTime = 0;
    float m_lastDeltaTime = 0.0f;
    float m_cachedYaw = 0.0f;
    float m_cachedPitch = 0.0f;
    float m_cachedRoll = 0.0f;
    bool m_cachedValid = false;

    // Last good 6DOF offset, held across tracker gaps for the same reason the
    // rotation is: letting it fall back to zero for a frame pops the view.
    float m_cachedPosX = 0.0f;
    float m_cachedPosY = 0.0f;
    float m_cachedPosZ = 0.0f;
    bool m_cachedPosValid = false;

    bool m_cameraHookInstalled = false;
    // Re-warned on this interval for as long as a second source keeps sending.
    static constexpr uint64_t kSecondSourceWarnIntervalMs = 30000;
    uint64_t m_lastSecondSourceWarnMs = 0;
    bool m_warnedSecondSource = false;
    uint64_t m_reportedRemoteRecenters = 0;
};

} // namespace Fallout4HT
