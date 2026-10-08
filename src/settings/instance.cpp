#include "instance.h"

namespace md {

InstanceNames settingsInstance(bool testMode) {
    if (testMode) return {L"", L"MacDockSettingsTestWindow"};
    return {L"MacDockSettings.Instance", L"MacDockSettingsWindow"};
}

}  // namespace md
