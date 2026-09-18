// Host-build shim. The real settings.h is part of the device build's
// settings/menu system. Provide just enough struct depth that the
// FDS_AutoInsertEnabled macro in InfoNES_FDS.h and the sprite limit in
// InfoNES_DrawLine compile and read sensibly.
#pragma once

struct HostSettingsFlags {
    int autoSwapFDS;
    int autoInsertDiskA;
    int removeSpriteLimit; // set from NES_NO_SPRITE_LIMIT in host_main.cpp
};

struct HostSettings {
    HostSettingsFlags flags;
};

extern HostSettings settings;
