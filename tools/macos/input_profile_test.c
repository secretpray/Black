/* Production mapping, persisted preferences, ownership names and capture.
 * All storage and inputs are isolated; no physical pad is qualified here. */
#include "../../ports/macos/overrides/input_profile.h"
#include "../../ports/macos/overrides/pad_mapping.h"
#include "../../ports/macos/overrides/keyboard_mouse.h"
#include "../../ports/macos/overrides/port_menu.h"
#undef NDEBUG
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
int main(int argc,char **argv)
{
    if (argc>1) {
        int cli = !strcmp(argv[1],"cli");
        if (cli) setenv("BLACK_INPUT_DEVICE","gamepad",1);
        assert(black_SettingsInputDevice()==(cli ? BLACK_INPUT_GAMEPAD : BLACK_INPUT_KEYBOARD));
        if (cli) {
            assert(black_SettingsSetFpsCounter(1));
            assert(black_SettingsInputDevice()==BLACK_INPUT_GAMEPAD);
            assert(black_SettingsSetInputDevice(BLACK_INPUT_KEYBOARD));
            assert(black_SettingsInputDevice()==BLACK_INPUT_KEYBOARD);
        }
        assert(black_SettingsButtonPrompts()==BLACK_PROMPTS_PLAYSTATION);
        assert(black_SettingsPadBinding(7)==0 && black_SettingsPadBinding(0)==7);
        assert(black_SettingsBinding(9,0)==18);
        assert(black_SettingsRenderScale()==3);
        return 0;
    }
    char folder[]="/tmp/black-profile-XXXXXX", path[512]; assert(mkdtemp(folder));
    snprintf(path,sizeof path,"%s/settings.ini",folder); setenv("BLACK_SETTINGS_FILE",path,1);
    unsetenv("BLACK_INPUT_DEVICE");
    assert(black_SettingsInputDevice()==BLACK_INPUT_AUTO);
    for(int i=0;i<16;++i) assert(black_SettingsPadBinding(i)==i);
    XBOX_GAMEPAD pad={0};
    for(int i=0;i<16;++i) {
        memset(&pad,0,sizeof pad);
        if(i<8) pad.bAnalogButtons[i]=i==6?117:255;
        else pad.wButtons=black_pad_bits[i-8];
        pad.sThumbLX=-1234; pad.sThumbRY=29999;
        XBOX_GAMEPAD before=pad; black_pad_remap(&pad);
        assert(!memcmp(&before,&pad,sizeof pad));
    }
    assert(black_SettingsSetPadBinding(7,0)); assert(black_SettingsSetPadBinding(0,7));
    pad=(XBOX_GAMEPAD){0}; pad.bAnalogButtons[0]=255; pad.bAnalogButtons[7]=67;
    black_pad_remap(&pad); assert(pad.bAnalogButtons[7]==255 && pad.bAnalogButtons[0]==67);
    assert(black_SettingsSetPadBinding(1,-1));
    assert(!black_SettingsSetPadBinding(16,0) && !black_SettingsSetPadBinding(0,16));
    assert(black_SettingsSetInputDevice(BLACK_INPUT_KEYBOARD));
    assert(black_SettingsSetButtonPrompts(BLACK_PROMPTS_PLAYSTATION));
    assert(!black_SettingsSetInputDevice(3) && !black_SettingsSetButtonPrompts(4));
    char label[32]; black_InputButtonLabel(0,1,label,sizeof label); assert(!strcmp(label,"×"));
    black_InputButtonLabel(0,0,label,sizeof label); assert(!strcmp(label,"R2"));
    assert(black_SettingsSetButtonPrompts(BLACK_PROMPTS_KEYBOARD));
    assert(black_SettingsSetBinding(9,0,18));
    black_InputButtonLabel(0,0,label,sizeof label); assert(!strcmp(label,"1"));
    black_InputButtonLabel(0,1,label,sizeof label); assert(!strcmp(label,"ENTER"));
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_AUTO); black_InputControllerFamily(BLACK_PROMPTS_PLAYSTATION);
    black_InputSetKeyboardActive(0); assert(black_InputPromptFamily()==BLACK_PROMPTS_PLAYSTATION);
    black_InputSetKeyboardActive(1); assert(black_InputPromptFamily()==BLACK_PROMPTS_KEYBOARD);
    black_SettingsSetButtonPrompts(BLACK_PROMPTS_PLAYSTATION); black_SettingsSetRenderScale(3);
    const char *modes[]={"reload","cli","reload"};
    for (int i=0;i<3;++i) {
        pid_t pid=fork(); assert(pid>=0); if(!pid){execl(argv[0],argv[0],modes[i],NULL);_exit(99);}
        int status; assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
    /* Opening A must be released. Ambiguous chords do not pick a button. */
    black_PadCaptureBegin(); assert(black_PadCapturePoll(1,10,1)); assert(black_PadCaptureTake()==-2);
    black_PadCapturePoll(0,10,1); black_PadCapturePoll(3,10,1); assert(black_PadCaptureTake()==-2);
    black_PadCapturePoll(0,10,1); black_PadCapturePoll(4,10,1); assert(black_PadCaptureTake()==2);
    assert(black_PadCapturePoll(4,10,1));
    assert(black_PadCapturePoll(0,10,1)); assert(!black_PadCapturePoll(0,10,1));
    black_PadCaptureBegin(); black_PadCapturePoll(0,10,1); black_PadCapturePoll(0,11,1); assert(black_PadCaptureTake()==-1);
    black_PadCaptureBegin(); black_PadCapturePoll(0,10,0); assert(black_PadCaptureTake()==-1);
    black_PadCaptureBegin(); black_PadCaptureCancel(); assert(black_PadCapturePoll(1,10,1));
    assert(black_PadCapturePoll(0,10,1)); assert(!black_PadCapturePoll(1,10,1));
    black_InputNoteMenuKeyboard(); assert(black_InputTakeMenuKeyboard()); assert(!black_InputTakeMenuKeyboard());
    black_SettingsResetInput(); assert(black_SettingsInputDevice()==BLACK_INPUT_AUTO);
    assert(black_SettingsButtonPrompts()==BLACK_PROMPTS_AUTO && black_SettingsRenderScale()==3);
    assert(black_SettingsBinding(9,0)==18);
    for(int i=0;i<16;++i) assert(black_SettingsPadBinding(i)==i);
    unlink(path); rmdir(folder); puts("Input preferences, remap, prompt names and capture: PASS");
}
