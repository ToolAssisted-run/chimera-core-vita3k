// DialogTest: the system's message dialog, asked for and left open (CC0).
//
// The program draws a plain picture through GXM (vita2d): a dark background
// and an orange disc. On its 30th frame it asks the system for a message
// dialog with one line of text and an OK button, and then goes on drawing.
// Nobody presses OK, so from then on every frame is the picture with the
// dialog over it.
//
// What it is for: the dialog is the system's to draw, and an emulator draws
// it with a renderer of its own, one for each graphics API. A game's own
// picture can be right while the dialog over it is not (chimera issue 255:
// under Vulkan only the outline of the OK button was drawn).
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/message_dialog.h>
#include <string.h>
#include <vita2d.h>

int main(int argc, char *argv[]) {
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    memset(&init, 0, sizeof init);
    memset(&boot, 0, sizeof boot);
    sceAppUtilInit(&init, &boot);

    SceCommonDialogConfigParam config;
    sceCommonDialogConfigParamInit(&config);
    sceCommonDialogSetConfigParam(&config);

    vita2d_init();
    vita2d_set_clear_color(RGBA8(16, 24, 48, 255));

    SceMsgDialogUserMessageParam message;
    memset(&message, 0, sizeof message);
    message.msg = (const SceChar8 *)"This is the system's message dialog.";
    message.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &message;

    for (int frame = 0;; frame++) {
        if (frame == 30)
            sceMsgDialogInit(&param);
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_draw_fill_circle(200.0f, 150.0f, 71.0f, RGBA8(255, 160, 0, 255));
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
    }
    return 0;
}
