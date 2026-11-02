/*
 *  Copyright (C) 2026 Stephen Ferrell
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "PvrClient.h"

#include <kodi/AddonBase.h>

class ATTR_DLL_LOCAL CDirectTunerAddon : public kodi::addon::CAddonBase
{
public:
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& hdl) override
  {
    if (!instance.IsType(ADDON_INSTANCE_PVR))
      return ADDON_STATUS_UNKNOWN;
    hdl = new CPvrClient(instance);
    return ADDON_STATUS_OK;
  }

  ADDON_STATUS SetSetting(const std::string& settingName,
                          const kodi::addon::CSettingValue& settingValue) override
  {
    // Margins are read fresh; the rest take effect when the add-on restarts.
    if (settingName == "margin_before" || settingName == "margin_after")
      return ADDON_STATUS_OK;
    return ADDON_STATUS_NEED_RESTART;
  }
};

ADDONCREATOR(CDirectTunerAddon)
