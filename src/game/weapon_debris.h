// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT::WeaponDebris {

// Switches the game's weapon debris (NVIDIA FleX) off for this session by
// clearing the engine's live bNVFlexEnable setting. Fallout4Prefs.ini is not
// written. Scans .data, so init thread only.
void DisableForSession();

// Keeps the cleared setting out of Fallout4Prefs.ini when the game saves its
// settings. A no-op when DisableForSession found nothing to clear. If the guard
// cannot be installed the setting is put back as the game loaded it. Needs the
// hook manager initialised; the hook is enabled with the rest.
void InstallPrefsWriteGuard();

} // namespace Fallout4HT::WeaponDebris
