/* Stable key IDs in hookshot_options.ini. Values retain existing bindings on all
   platforms; Linux translates them to SDL scancodes. */
#ifndef HOS_INPUT_H
#define HOS_INPUT_H
#define HOS_KEY_LBUTTON 0x01
#define HOS_KEY_RBUTTON 0x02
#define HOS_KEY_MBUTTON 0x04
#define HOS_KEY_BACK 0x08
#define HOS_KEY_TAB 0x09
#define HOS_KEY_RETURN 0x0D
#define HOS_KEY_SHIFT 0x10
#define HOS_KEY_CONTROL 0x11
#define HOS_KEY_MENU 0x12
#define HOS_KEY_PAUSE 0x13
#define HOS_KEY_CAPITAL 0x14
#define HOS_KEY_ESCAPE 0x1B
#define HOS_KEY_SPACE 0x20
#define HOS_KEY_PRIOR 0x21
#define HOS_KEY_NEXT 0x22
#define HOS_KEY_END 0x23
#define HOS_KEY_HOME 0x24
#define HOS_KEY_LEFT 0x25
#define HOS_KEY_UP 0x26
#define HOS_KEY_RIGHT 0x27
#define HOS_KEY_DOWN 0x28
#define HOS_KEY_INSERT 0x2D
#define HOS_KEY_DELETE 0x2E
#define HOS_KEY_NUMPAD0 0x60
#define HOS_KEY_MULTIPLY 0x6A
#define HOS_KEY_ADD 0x6B
#define HOS_KEY_SUBTRACT 0x6D
#define HOS_KEY_DECIMAL 0x6E
#define HOS_KEY_DIVIDE 0x6F
#define HOS_KEY_F1 0x70
#define HOS_KEY_LSHIFT 0xA0
#define HOS_KEY_RSHIFT 0xA1
#define HOS_KEY_LCONTROL 0xA2
#define HOS_KEY_RCONTROL 0xA3
#define HOS_KEY_LMENU 0xA4
#define HOS_KEY_RMENU 0xA5
#define HOS_KEY_OEM_1 0xBA
#define HOS_KEY_OEM_PLUS 0xBB
#define HOS_KEY_OEM_COMMA 0xBC
#define HOS_KEY_OEM_MINUS 0xBD
#define HOS_KEY_OEM_PERIOD 0xBE
#define HOS_KEY_OEM_2 0xBF
#define HOS_KEY_OEM_3 0xC0
#define HOS_KEY_OEM_4 0xDB
#define HOS_KEY_OEM_5 0xDC
#define HOS_KEY_OEM_6 0xDD
#define HOS_KEY_OEM_7 0xDE
#define HOS_KEY_F2 0x71
#define HOS_KEY_F3 0x72
#define HOS_KEY_F4 0x73
#define HOS_KEY_F5 0x74
#define HOS_KEY_F6 0x75
#define HOS_KEY_F7 0x76
#define HOS_KEY_F8 0x77
#define HOS_KEY_F9 0x78
#define HOS_KEY_F10 0x79
#define HOS_KEY_F11 0x7A
#define HOS_KEY_F12 0x7B
#ifdef __cplusplus
extern "C" {
#endif
int hos_linux_key_down(int key);
int hos_linux_has_focus(void);
void hos_linux_screen_size(int *width, int *height);
#ifdef __cplusplus
}
#endif
#endif
