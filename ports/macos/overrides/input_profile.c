/* Device policy and names shared by the native panel and guest prompts.
 * TGP supplies the logical control order; Burnout is a UX reference only. */
#include "input_profile.h"
#include "port_menu.h"
#include "keyboard_mouse.h"
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static atomic_int controller_family = BLACK_PROMPTS_XBOX, menu_keyboard;
void black_InputNoteMenuKeyboard(void) { atomic_store(&menu_keyboard,1); }
int black_InputTakeMenuKeyboard(void) { return atomic_exchange(&menu_keyboard,0); }
static const char *ids[BLACK_PAD_CONTROLS] = {"a","b","x","y","black","white","lt","rt",
    "up","down","left","right","start","back","ls","rs"};
static const char *labels[BLACK_PAD_CONTROLS] = {"Reload (A)","Melee (B)","Interact (X)","Suppressor (Y)",
    "Black (RB)","Next Track (LB)","Grenade (LT)","Fire (RT)","Firing Mode (D-pad Up)",
    "Medkit (D-pad Down)","Weapon Down (D-pad Left)","Weapon Up (D-pad Right)",
    "Pause (Start)","Objectives (Back)","Crouch (LS)","Aim (RS)"};
static const char *xbox[BLACK_PAD_CONTROLS] = {"A","B","X","Y","RB","LB","LT","RT",
    "UP","DOWN","LEFT","RIGHT","START","BACK","LS","RS"};
static const char *ps[BLACK_PAD_CONTROLS] = {"×","○","□","△","R1","L1","L2","R2",
    "UP","DOWN","LEFT","RIGHT","OPTIONS","SHARE","L3","R3"};
const char *black_PadControlId(int target) { return target >= 0 && target < 16 ? ids[target] : "invalid"; }
const char *black_PadControlLabel(int target) { return target >= 0 && target < 16 ? labels[target] : "Unknown"; }
const char *black_PadButtonName(int source, int family)
{ return source < 0 || source >= 16 ? "UNBOUND" : family == BLACK_PROMPTS_PLAYSTATION ? ps[source] : xbox[source]; }
void black_InputControllerFamily(int family)
{ atomic_store(&controller_family, family == BLACK_PROMPTS_PLAYSTATION ? family : BLACK_PROMPTS_XBOX); }
int black_InputPromptFamily(void)
{
    int selected = black_SettingsButtonPrompts();
    return selected ? selected : black_InputKeyboardActive() ? BLACK_PROMPTS_KEYBOARD : atomic_load(&controller_family);
}
void black_InputButtonLabel(int target, int menu, char *out, size_t cap)
{
    const char *name = "UNBOUND";
    int family = black_InputPromptFamily();
    if (family == BLACK_PROMPTS_KEYBOARD) {
        if (menu) {
            static const char *keys[] = {"ENTER","ESC","DELETE","SPACE","E","Q","UNBOUND","UNBOUND",
                "UP","DOWN","LEFT","RIGHT","ENTER","TAB","UNBOUND","UNBOUND"};
            if (target >= 0 && target < 16) name = keys[target];
        } else {
            for (int i = 0; i < BLACK_ACTION_COUNT; ++i) if (black_KeyActions()[i].target == target) {
                int key = black_SettingsBinding(i,0);
                if (key < 0) key = black_SettingsBinding(i,1);
                name = black_KeyName(key); break;
            }
        }
    } else name = black_PadButtonName(menu ? target : black_SettingsPadBinding(target), family);
    if (cap) snprintf(out, cap, "%s", name);
}
unsigned black_InputButtonGlyph(int target, int menu)
{
    if (target < 0 || target >= 16) return 0xE1FF;
    int family=black_InputPromptFamily();
    if (family!=BLACK_PROMPTS_KEYBOARD) {
        int source=menu?target:black_SettingsPadBinding(target);
        return source<0?0xE1FF:0xE100+(family==BLACK_PROMPTS_PLAYSTATION?16:0)+source;
    }
    int key=-1;
    if (menu) {
        static const int keys[]={36,53,51,49,14,12,-1,-1,126,125,123,124,36,48,-1,-1};
        key=keys[target];
    } else for(int i=0;i<BLACK_ACTION_COUNT;++i)if(black_KeyActions()[i].target==target){
        key=black_SettingsBinding(i,0);if(key<0)key=black_SettingsBinding(i,1);break;
    }
    return key<0?0xE1FF:0xE200+black_KeyCanonical(key);
}
void black_InputGlyphUTF8(unsigned code,char *out,size_t cap)
{
    if(cap<4){if(cap)out[0]=0;return;}
    out[0]=(char)(0xE0|(code>>12));out[1]=(char)(0x80|((code>>6)&63));out[2]=(char)(0x80|(code&63));out[3]=0;
}
static pthread_mutex_t capture_lock = PTHREAD_MUTEX_INITIALIZER;
static int capture, release_gate, armed, capture_instance = -1, captured = -2;
void black_PadCaptureBegin(void)
{ pthread_mutex_lock(&capture_lock); capture = 1; release_gate = 0; armed = 0; capture_instance = -1; captured = -2; pthread_mutex_unlock(&capture_lock); }
void black_PadCaptureCancel(void)
{ pthread_mutex_lock(&capture_lock); capture = armed = 0; release_gate = 1; captured = -2; pthread_mutex_unlock(&capture_lock); }
int black_PadCapturePoll(uint16_t pressed, int instance, int connected)
{
    pthread_mutex_lock(&capture_lock);
    int consume = capture || release_gate;
    if (!connected || !pressed) release_gate = 0;
    if (capture) {
        if (!connected || (capture_instance >= 0 && instance != capture_instance)) { captured = -1; capture = 0; }
        else {
            capture_instance = instance;
            if (!pressed) armed = 1;
            else if (armed && !(pressed & (pressed - 1))) {
                for (int i = 0; i < 16; ++i) if (pressed & (1u << i)) captured = i;
                capture = 0; release_gate = 1;
            }
        }
    }
    pthread_mutex_unlock(&capture_lock);
    return consume;
}
int black_PadCaptureTake(void)
{ pthread_mutex_lock(&capture_lock); int result = captured; captured = -2; pthread_mutex_unlock(&capture_lock); return result; }
