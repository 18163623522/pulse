// startup_location.cpp — see startup_location.h.
#include "startup_location.h"

#include "app_prefs.h"

namespace pulse::app {

std::wstring DefaultLocation(const AppPrefs& prefs) {
    return prefs.home_folder;
}

bool RestoresLastTabs(const AppPrefs& prefs) noexcept {
    return prefs.startup_open != 1;
}

std::wstring NewTabLocation(const AppPrefs& prefs, const std::wstring& current) {
    return prefs.new_tab_open == 1 ? DefaultLocation(prefs) : current;
}

} // namespace pulse::app
