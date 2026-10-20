#pragma once

// 1 when the system trophies are usable (NoTrpDrm installed); call after vita2d_init
int trophies_init(void);
// unlock the Vita trophy of an in-game achievement (1-12); does nothing without trophies
void trophies_unlock(int achievement);
