// SPDX-License-Identifier: MIT

#include "pch.h"
#include "notification.h"
#include "core/logging.h"

namespace Fallout4HT {

void ShowNotification(const char* message) {
    Log::Line("Notification: %s", message);
}

} // namespace Fallout4HT
