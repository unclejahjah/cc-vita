// Vita trophies for the game's achievements. Works only with Rinnegatamante's NoTrpDrm plugin,
// which lets an unsigned app use its own trophy pack (sce_sys/trophy/CCVITA001_00/TROPHY.TRP,
// made by tools/scripts/make_trp.py). Without the plugin the context fails and only the in-game
// achievements list is kept. Trophy id = achievement id - 1.
#include <psp2/common_dialog.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <stdint.h>
#include <string.h>
#include <vita2d.h>
#include "trophies.h"

typedef struct {
    char data[9];
    char term;
    uint8_t num;
    char dummy;
} NpCommId;

typedef struct {
    int sdkVersion;
    SceCommonDialogParam commonParam;
    int context;
    int options;
    uint8_t reserved[128];
} NpTrophySetupDialogParam;

int sceNpTrophyInit(void *unk);
int sceNpTrophyCreateContext(int *context, const NpCommId *id, const uint8_t *sign, uint64_t options);
int sceNpTrophySetupDialogInit(NpTrophySetupDialogParam *param);
SceCommonDialogStatus sceNpTrophySetupDialogGetStatus(void);
int sceNpTrophySetupDialogTerm(void);
int sceNpTrophyCreateHandle(int *handle);
int sceNpTrophyDestroyHandle(int handle);
int sceNpTrophyUnlockTrophy(int context, int handle, int id, int *platinum);

static const NpCommId COMM_ID = {"CCVITA001", 0, 0, 0};
static uint8_t signature[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};   // NoTrpDrm skips the check

static int ctx, ready;
static volatile unsigned pending;   // trophy ids waiting for the unlock thread
static SceUID wake;

// unlocking talks to the system for a while, so it runs off the game's thread
static int unlocker(SceSize args, void *argp) {
    (void)args; (void)argp;
    for (;;) {
        sceKernelWaitSema(wake, 1, NULL);
        unsigned todo = __atomic_exchange_n(&pending, 0, __ATOMIC_SEQ_CST);
        for (int id = 0; id < 32; id++) {
            if (!(todo >> id & 1)) continue;
            int handle, platinum = -1;
            if (sceNpTrophyCreateHandle(&handle) < 0) continue;
            sceNpTrophyUnlockTrophy(ctx, handle, id, &platinum);
            sceNpTrophyDestroyHandle(handle);
        }
    }
    return 0;
}

int trophies_init(void) {
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY) < 0) return 0;
    sceNpTrophyInit(NULL);
    if (sceNpTrophyCreateContext(&ctx, &COMM_ID, signature, 0) < 0) return 0;

    // installs the trophy pack on first run; the dialog draws over our frames
    NpTrophySetupDialogParam setup;
    memset(&setup, 0, sizeof setup);
    _sceCommonDialogSetMagicNumber(&setup.commonParam);
    setup.sdkVersion = 0x03570011;   // PSP2_SDK_VERSION of 3.57
    setup.context = ctx;
    if (sceNpTrophySetupDialogInit(&setup) >= 0) {
        while (sceNpTrophySetupDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
            vita2d_start_drawing();
            vita2d_clear_screen();
            vita2d_end_drawing();
            vita2d_common_dialog_update();
            vita2d_swap_buffers();
        }
        sceNpTrophySetupDialogTerm();
    }

    wake = sceKernelCreateSema("trophies", 0, 0, 1, NULL);
    SceUID th = sceKernelCreateThread("trophies", unlocker, 0x10000100, 0x10000, 0, 0, NULL);
    if (wake < 0 || th < 0) return 0;
    sceKernelStartThread(th, 0, NULL);
    ready = 1;
    return 1;
}

void trophies_unlock(int achievement) {
    if (!ready || achievement < 1 || achievement > 32) return;
    __atomic_or_fetch(&pending, 1u << (achievement - 1), __ATOMIC_SEQ_CST);
    sceKernelSignalSema(wake, 1);
}
