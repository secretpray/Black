#ifndef BLACK_INPUT_PROFILE_H
#define BLACK_INPUT_PROFILE_H
#include <stdint.h>
#include <stddef.h>
#include "input_owner.h"
/* Logical Xbox controls, in the same order as TGP's hint string IDs. */
enum { BLACK_PAD_CONTROLS = 16 };
enum { BLACK_PROMPTS_AUTO, BLACK_PROMPTS_XBOX, BLACK_PROMPTS_PLAYSTATION, BLACK_PROMPTS_KEYBOARD };
int black_SettingsInputDevice(void);
int black_SettingsSetInputDevice(int policy);
int black_SettingsButtonPrompts(void);
int black_SettingsSetButtonPrompts(int family);
int black_SettingsPadBinding(int target);
int black_SettingsSetPadBinding(int target, int source);
const char *black_PadControlId(int target);
const char *black_PadControlLabel(int target);
const char *black_PadButtonName(int source, int family);
void black_InputControllerFamily(int family);
void black_InputNoteMenuKeyboard(void);
int black_InputTakeMenuKeyboard(void);
int black_InputPromptFamily(void);
void black_InputButtonLabel(int target, int menu, char *out, size_t capacity);
/* Private-use bitmap cells. Human-readable names remain separate for AX. */
unsigned black_InputButtonGlyph(int target, int menu);
void black_InputGlyphUTF8(unsigned code, char *out, size_t capacity);
/* Capture runs on the input thread; UI only exchanges bounded state. */
void black_PadCaptureBegin(void);
void black_PadCaptureCancel(void);
int black_PadCapturePoll(uint16_t pressed, int instance, int connected);
int black_PadCaptureTake(void); /* -2 waiting/cancelled, -1 disconnected */
#endif
