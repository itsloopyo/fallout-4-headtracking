// SPDX-License-Identifier: MIT

#pragma once

namespace Fallout4HT {

// Finds the game's VATS singleton and dumps it, so the field that says "VATS is
// active" can be identified by comparing a dump taken in gameplay against one
// taken with the targeting menu open.
//
// A positive signal is needed because the absence of one is not good enough: the
// pause watchdog only notices VATS 40 ms after the game stops updating, and by
// then the menu has already laid its body-part widgets out through the camera as
// it was - head pose included.
// Ctrl+Shift+V records a sample taken in ordinary gameplay, Ctrl+Shift+X one
// taken with the VATS menu open. Alternate them a few times and the candidate
// set collapses from thousands to a handful.
void ProbeVats();
void ProbeVatsInVats();


} // namespace Fallout4HT
