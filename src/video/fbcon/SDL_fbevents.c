/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    Modified for Powkiddy X-series (X39 Pro, X45, X51, X70) - 2026

    POWKIDDY INPUT ARCHITECTURE
    ----------------------------
    All keyboard/button input is read directly from evdev nodes.
    No VT, no KD_RAW, no TTY, no tty/console dependency whatsoever.

    event0 : dpad + some buttons (declared as kbd by kernel)
    event1 : remaining buttons   (declared as joystick by kernel)

    Both are merged here and translated to SDL keyboard events.
    This means any SDL app gets correct keyboard events for all
    physical buttons without any VT setup or setsid() requirement.

    Crash-safe: since we never put any TTY in raw mode, there is
    nothing to restore on crash. No blocked terminal, ever.

    Button → SDLKey mapping (matches OpenDingux convention):
        Dpad Up/Down/Left/Right → SDLK_UP/DOWN/LEFT/RIGHT
        A       → SDLK_LCTRL
        B       → SDLK_LALT
        X       → SDLK_SPACE
        Y       → SDLK_LSHIFT
        L1      → SDLK_TAB
        R1      → SDLK_BACKSPACE
        L2      → SDLK_PAGEDOWN
        R2      → SDLK_PAGEUP
        Select  → SDLK_ESCAPE
        Start   → SDLK_RETURN
        Menu    → SDLK_HOME
        Power   → SDLK_END
*/

#define POWKIDDY

#include "SDL_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <ctype.h>

/* evdev */
#include <linux/input.h>

/* Keep vt.h/kd.h only for the non-Powkiddy path */
#ifndef POWKIDDY
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#endif

#include "SDL_timer.h"
#include "SDL_mutex.h"
#include "../SDL_sysvideo.h"
#include "../../events/SDL_sysevents.h"
#include "../../events/SDL_events_c.h"
#include "SDL_fbvideo.h"
#include "SDL_fbevents_c.h"

#ifndef POWKIDDY
#include "SDL_fbkeys.h"
#include "SDL_fbelo.h"
#endif

#ifndef GPM_NODE_FIFO
#define GPM_NODE_FIFO "/dev/gpmdata"
#endif

/* ============================================================
 * POWKIDDY — evdev direct input
 * ============================================================ */
#ifdef POWKIDDY

/* evdev file descriptors — both opened O_NONBLOCK */
static int evdev_fd[2] = { -1, -1 };

/* Translate Linux input keycode → SDL keysym
 * Covers dpad (EV_KEY from gpio-keys) and all buttons (BTN_*).
 * Extend this table if your device has extra buttons. */
static SDLKey powkiddy_translate_key(int code)
{
	fprintf(stderr, "POWKIDDY TRANSLATE KEY Code %d\n", code);
    switch (code) {
    /* D-pad — gpio-keys, standard keycodes */
    case KEY_UP:            return SDLK_UP;
    case KEY_DOWN:          return SDLK_DOWN;
    case KEY_LEFT:          return SDLK_LEFT;
    case KEY_RIGHT:         return SDLK_RIGHT;

    /* Face buttons */
    case 352:             return SDLK_LCTRL;    /* 304 A */
    case 158:             return SDLK_LALT;     /* 305 B */
    case 308:             return SDLK_SPACE;    /* X  307 - check your device */
    case 139:             return SDLK_LSHIFT;   /* Y 308 - check your device */

    /* Shoulder */
    case 412:            return SDLK_TAB;       /* 310 L1 */
    case 312:            return SDLK_BACKSPACE; /* 311 R1 */
    case 407:           return SDLK_PAGEDOWN;  /* 312 L2 */
    case 313:           return SDLK_PAGEUP;    /* 313 R2 */

    /* Select / Start */
    case 314:        return SDLK_ESCAPE;   /* 314 select */
    case 315:         return SDLK_RETURN;   /* 315 start */

    /* Menu / Power — adjust codes to match your device */
    case 174:      return SDLK_HOME;     /* 174 menu */
    case 116:         return SDLK_END;      /* 116 power*/

    /* Volume (optional — map or ignore) */
    case 115:      return SDLK_PLUS;     /* 115 vol up*/
    case 114:    return SDLK_MINUS;    /* 114 vol down */

    default: 
		{
		fprintf(stderr, "POWKIDDY Unkwnown code %d\n", code);
		return SDLK_UNKNOWN;
		}
    }
}

/* Open both evdev nodes non-blocking */
static void powkiddy_open_evdev(void)
{
    const char *paths[2] = {
        "/dev/input/event0",
        "/dev/input/event1",
    };
    int i;
    for (i = 0; i < 2; i++) {
        evdev_fd[i] = open(paths[i], O_RDONLY | O_NONBLOCK);
        if (evdev_fd[i] < 0) {
            fprintf(stderr, "[SDL] Powkiddy: cannot open %s: %s\n",
                    paths[i], strerror(errno));
        }
    }
}

static void powkiddy_close_evdev(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        if (evdev_fd[i] >= 0) {
            close(evdev_fd[i]);
            evdev_fd[i] = -1;
        }
    }
}

/* Read all pending events from one evdev fd, post SDL keyboard events */
static int powkiddy_handle_evdev(int fd)
{
    struct input_event ev;
    SDL_keysym keysym;
    int posted = 0;

    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        /* Only care about key press/release, ignore repeat (value==2) */
        if (ev.type != EV_KEY) continue;
        //if (ev.value == 2)     continue;
		
        SDL_memset(&keysym, 0, sizeof(keysym));
        keysym.scancode = (Uint8)(ev.code & 0xFF);
        keysym.sym      = powkiddy_translate_key(ev.code);
        keysym.mod      = KMOD_NONE;
        keysym.unicode  = 0;
		fprintf(stderr, "Keycode scancode %d sym %d\n", keysym.scancode, keysym.sym);
        if (keysym.sym == SDLK_UNKNOWN) continue;

        posted += SDL_PrivateKeyboard(
            ev.value,
            &keysym);
    }
    return posted;
}

/* ---- Stubs for the keyboard open/close/graphics API ---- */

int FB_InGraphicsMode(_THIS)    { return 1; }
int FB_EnterGraphicsMode(_THIS) { return 0; }
void FB_LeaveGraphicsMode(_THIS){ }

void FB_CloseKeyboard(_THIS)
{
    powkiddy_close_evdev();
    keyboard_fd = -1;
}

int FB_OpenKeyboard(_THIS)
{
    powkiddy_open_evdev();
    /* keyboard_fd is unused on Powkiddy but set to a valid value
     * so callers that check (keyboard_fd >= 0) are satisfied */
    keyboard_fd = evdev_fd[0];
    return keyboard_fd;
}

void FB_InitOSKeymap(_THIS)
{
    /* Nothing to do — we use our own TranslateKey, not the VGA keymap */
}

/* ---- Mouse (no mouse on Powkiddy, stubs) ---- */

void FB_CloseMouse(_THIS)
{
    if (mouse_fd > 0) close(mouse_fd);
    mouse_fd = -1;
}

int FB_OpenMouse(_THIS)
{
    mouse_fd = -1;
    return mouse_fd;
}

/* ---- PumpEvents ---- */

void FB_PumpEvents(_THIS)
{
    fd_set fdset;
    int max_fd = -1;
    static struct timeval zero;
    int posted;
    int i;

    do {
        posted = 0;

        FD_ZERO(&fdset);

        for (i = 0; i < 2; i++) {
            if (evdev_fd[i] >= 0) {
                FD_SET(evdev_fd[i], &fdset);
                if (evdev_fd[i] > max_fd) max_fd = evdev_fd[i];
            }
        }

        if (max_fd < 0) break;

        /* Non-blocking poll — zero timeout */
        if (select(max_fd + 1, &fdset, NULL, NULL, &zero) > 0) {
            for (i = 0; i < 2; i++) {
                if (evdev_fd[i] >= 0 && FD_ISSET(evdev_fd[i], &fdset)) {
                    posted += powkiddy_handle_evdev(evdev_fd[i]);
                }
            }
        }
    } while (posted);
}

#else /* !POWKIDDY — original SDL code below, untouched */

/* ============================================================
 * ORIGINAL SDL fbevents code (non-Powkiddy platforms)
 * ============================================================ */

/*#define DEBUG_KEYBOARD*/
/*#define DEBUG_MOUSE*/

#define NUM_VGAKEYMAPS (1<<KG_CAPSSHIFT)
static Uint16 vga_keymap[NUM_VGAKEYMAPS][NR_KEYS];
static SDLKey keymap[128];
static Uint16 keymap_temp[128];
static SDL_keysym *TranslateKey(int scancode, SDL_keysym *keysym);

static void FB_vgainitkeymaps(int fd)
{
    struct kbentry entry;
    int map, i;

    if (fd < 0) return;

    for (map = 0; map < NUM_VGAKEYMAPS; ++map) {
        SDL_memset(vga_keymap[map], 0, NR_KEYS*sizeof(Uint16));
        for (i = 0; i < NR_KEYS; ++i) {
            entry.kb_table = map;
            entry.kb_index = i;
            if (ioctl(fd, KDGKBENT, &entry) == 0) {
                if ((map == 0) && (i < 128))
                    keymap_temp[i] = entry.kb_value;
                if (entry.kb_value == K_ENTER)
                    entry.kb_value = K(KT_ASCII, 13);
                if (KTYP(entry.kb_value) == KT_PAD) {
                    switch (entry.kb_value) {
                    case K_P0: case K_P1: case K_P2: case K_P3: case K_P4:
                    case K_P5: case K_P6: case K_P7: case K_P8: case K_P9:
                        vga_keymap[map][i] = entry.kb_value + '0'; break;
                    case K_PPLUS:  vga_keymap[map][i]=K(KT_ASCII,'+'); break;
                    case K_PMINUS: vga_keymap[map][i]=K(KT_ASCII,'-'); break;
                    case K_PSTAR:  vga_keymap[map][i]=K(KT_ASCII,'*'); break;
                    case K_PSLASH: vga_keymap[map][i]=K(KT_ASCII,'/'); break;
                    case K_PENTER: vga_keymap[map][i]=K(KT_ASCII,'\r');break;
                    case K_PCOMMA: vga_keymap[map][i]=K(KT_ASCII,','); break;
                    case K_PDOT:   vga_keymap[map][i]=K(KT_ASCII,'.'); break;
                    default: break;
                    }
                }
                if ((KTYP(entry.kb_value) == KT_LATIN) ||
                    (KTYP(entry.kb_value) == KT_ASCII) ||
                    (KTYP(entry.kb_value) == KT_LETTER))
                    vga_keymap[map][i] = entry.kb_value;
            }
        }
    }
}

int FB_InGraphicsMode(_THIS)
{
    return ((keyboard_fd >= 0) && (saved_kbd_mode >= 0));
}

int FB_EnterGraphicsMode(_THIS)
{
    struct termios keyboard_termios;

    if ((keyboard_fd >= 0) && !FB_InGraphicsMode(this)) {
        if (current_vt > 0) {
            struct vt_stat vtstate;
            if (ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0)
                saved_vt = vtstate.v_active;
            if (ioctl(keyboard_fd, VT_ACTIVATE, current_vt) == 0)
                ioctl(keyboard_fd, VT_WAITACTIVE, current_vt);
        }
        if (tcgetattr(keyboard_fd, &saved_kbd_termios) < 0) {
            SDL_SetError("Unable to get terminal attributes");
            if (keyboard_fd > 0) close(keyboard_fd);
            keyboard_fd = -1;
            return -1;
        }
        if (ioctl(keyboard_fd, KDGKBMODE, &saved_kbd_mode) < 0) {
            SDL_SetError("Unable to get current keyboard mode");
            if (keyboard_fd > 0) close(keyboard_fd);
            keyboard_fd = -1;
            return -1;
        }
        keyboard_termios = saved_kbd_termios;
        keyboard_termios.c_lflag &= ~(ICANON|ECHO|ISIG);
        keyboard_termios.c_iflag &= ~(ISTRIP|IGNCR|ICRNL|INLCR|IXOFF|IXON);
        keyboard_termios.c_cc[VMIN]  = 0;
        keyboard_termios.c_cc[VTIME] = 0;
        if (tcsetattr(keyboard_fd, TCSAFLUSH, &keyboard_termios) < 0) {
            FB_CloseKeyboard(this);
            SDL_SetError("Unable to set terminal attributes");
            return -1;
        }
        if (ioctl(keyboard_fd, KDSKBMODE, K_MEDIUMRAW) < 0) {
            FB_CloseKeyboard(this);
            SDL_SetError("Unable to set keyboard in raw mode");
            return -1;
        }
        if (ioctl(keyboard_fd, KDSETMODE, KD_GRAPHICS) < 0) {
            FB_CloseKeyboard(this);
            SDL_SetError("Unable to set keyboard in graphics mode");
            return -1;
        }
        ioctl(keyboard_fd, VT_LOCKSWITCH, 1);
    }
    return keyboard_fd;
}

void FB_LeaveGraphicsMode(_THIS)
{
    if (FB_InGraphicsMode(this)) {
        ioctl(keyboard_fd, KDSETMODE, KD_TEXT);
        ioctl(keyboard_fd, KDSKBMODE, saved_kbd_mode);
        tcsetattr(keyboard_fd, TCSAFLUSH, &saved_kbd_termios);
        saved_kbd_mode = -1;
        ioctl(keyboard_fd, VT_UNLOCKSWITCH, 1);
        if (saved_vt > 0)
            ioctl(keyboard_fd, VT_ACTIVATE, saved_vt);
    }
}

void FB_CloseKeyboard(_THIS)
{
    if (keyboard_fd >= 0) {
        FB_LeaveGraphicsMode(this);
        if (keyboard_fd > 0) close(keyboard_fd);
    }
    keyboard_fd = -1;
}

int FB_OpenKeyboard(_THIS)
{
    if (keyboard_fd < 0) {
        static const char * const tty0[] = { "/dev/tty0", "/dev/vc/0", NULL };
        static const char * const vcs[]  = { "/dev/vc/%d", "/dev/tty%d", NULL };
        int i, tty0_fd;

        tty0_fd = -1;
        for (i = 0; tty0[i] && (tty0_fd < 0); ++i)
            tty0_fd = open(tty0[i], O_WRONLY, 0);
        if (tty0_fd < 0) tty0_fd = dup(0);
        ioctl(tty0_fd, VT_OPENQRY, &current_vt);
        close(tty0_fd);

        if (current_vt > 0) {
            for (i = 0; vcs[i] && (keyboard_fd < 0); ++i) {
                char vtpath[12];
                SDL_snprintf(vtpath, SDL_arraysize(vtpath), vcs[i], current_vt);
                keyboard_fd = open(vtpath, O_RDWR, 0);
            }
        }
        if (keyboard_fd < 0) {
            struct vt_stat vtstate;
            keyboard_fd = open("/dev/tty", O_RDWR);
            if (ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0)
                current_vt = vtstate.v_active;
            else
                current_vt = 0;
        }
        saved_kbd_mode = -1;
        { int dummy;
          if (ioctl(keyboard_fd, KDGKBMODE, &dummy) < 0) {
              close(keyboard_fd);
              keyboard_fd = -1;
              SDL_SetError("Unable to open a console terminal");
          }
        }
        FB_vgainitkeymaps(keyboard_fd);
    }
    return keyboard_fd;
}

/* ---- Mouse (original) ---- */

static enum {
    MOUSE_NONE = -1, MOUSE_MSC, MOUSE_PS2, MOUSE_IMPS2,
    MOUSE_MS, MOUSE_BM, MOUSE_ELO, MOUSE_TSLIB, NUM_MOUSE_DRVS
} mouse_drv = MOUSE_NONE;

void FB_CloseMouse(_THIS)
{
#if SDL_INPUT_TSLIB
    if (ts_dev != NULL) { ts_close(ts_dev); ts_dev = NULL; mouse_fd = -1; }
#endif
    if (mouse_fd > 0) close(mouse_fd);
    mouse_fd = -1;
}

static int find_pid(DIR *proc, const char *wanted_name)
{
    struct dirent *entry;
    int pid = 0;
    while ((pid == 0) && ((entry = readdir(proc)) != NULL)) {
        if (isdigit(entry->d_name[0])) {
            FILE *status;
            char path[PATH_MAX], name[PATH_MAX];
            SDL_snprintf(path, SDL_arraysize(path), "/proc/%s/status", entry->d_name);
            status = fopen(path, "r");
            if (status) {
                int matches = 0;
                name[0] = '\0';
                matches = fscanf(status, "Name: %s", name);
                if ((matches == 1) && (SDL_strcmp(name, wanted_name) == 0))
                    pid = SDL_atoi(entry->d_name);
                fclose(status);
            }
        }
    }
    return pid;
}

static int gpm_available(char *proto, size_t protolen)
{
    int available = 0;
    DIR *proc;
    int pid;
    int cmdline, len, arglen;
    char path[PATH_MAX], args[PATH_MAX], *arg;

    if (access(GPM_NODE_FIFO, F_OK) < 0) return 0;

    proc = opendir("/proc");
    if (proc) {
        char raw_proto[10]    = { '\0' };
        char repeat_proto[10] = { '\0' };
        while (!available && (pid = find_pid(proc, "gpm")) > 0) {
            SDL_snprintf(path, SDL_arraysize(path), "/proc/%d/cmdline", pid);
            cmdline = open(path, O_RDONLY, 0);
            if (cmdline >= 0) {
                len = read(cmdline, args, sizeof(args));
                arg = args;
                while (len > 0) {
                    arglen = SDL_strlen(arg) + 1;
                    if (SDL_strcmp(arg, "-t") == 0) {
                        char *t = arg + arglen, *s = SDL_strchr(t, ' ');
                        if (s) *s = 0;
                        SDL_strlcpy(raw_proto, t, SDL_arraysize(raw_proto));
                        if (s) *s = ' ';
                    }
                    if (SDL_strncmp(arg, "-R", 2) == 0) {
                        char *t = arg + 2, *s = SDL_strchr(t, ' ');
                        available = 1;
                        if (s) *s = 0;
                        SDL_strlcpy(repeat_proto, t, SDL_arraysize(repeat_proto));
                        if (s) *s = ' ';
                    }
                    len -= arglen; arg += arglen;
                }
                close(cmdline);
            }
        }
        closedir(proc);
        if (available) {
            if (SDL_strcmp(repeat_proto, "raw") == 0)
                SDL_strlcpy(proto, raw_proto, protolen);
            else if (*repeat_proto)
                SDL_strlcpy(proto, repeat_proto, protolen);
            else
                SDL_strlcpy(proto, "msc", protolen);
        }
    }
    return available;
}

static int set_imps2_mode(int fd)
{
    Uint8 set_imps2[] = { 0xf3, 200, 0xf3, 100, 0xf3, 80 };
    fd_set fdset;
    struct timeval tv;
    if (write(fd, &set_imps2, sizeof(set_imps2)) != sizeof(set_imps2))
        return 0;
    FD_ZERO(&fdset); FD_SET(fd, &fdset);
    tv.tv_sec = 0; tv.tv_usec = 0;
    while (select(fd+1, &fdset, 0, 0, &tv) > 0) {
        char temp[32];
        if (read(fd, temp, sizeof(temp)) <= 0) break;
    }
    return 1;
}

static int detect_imps2(int fd)
{
    int imps2 = 0;
    if (SDL_getenv("SDL_MOUSEDEV_IMPS2")) return 1;
    {
        Uint8 query_ps2 = 0xF2;
        fd_set fdset;
        struct timeval tv;
        FD_ZERO(&fdset); FD_SET(fd, &fdset);
        tv.tv_sec = 0; tv.tv_usec = 0;
        while (select(fd+1, &fdset, 0, 0, &tv) > 0) {
            char temp[32];
            if (read(fd, temp, sizeof(temp)) <= 0) break;
        }
        if (write(fd, &query_ps2, sizeof(query_ps2)) == sizeof(query_ps2)) {
            Uint8 ch = 0;
            do {
                FD_ZERO(&fdset); FD_SET(fd, &fdset);
                tv.tv_sec = 1; tv.tv_usec = 0;
                if (select(fd+1, &fdset, 0, 0, &tv) < 1) break;
            } while ((read(fd, &ch, sizeof(ch)) == sizeof(ch)) &&
                     ((ch == 0xFA) || (ch == 0xAA)));
            if ((ch == 3) || (ch == 4)) imps2 = 1;
        }
    }
    return imps2;
}

int FB_OpenMouse(_THIS)
{
    int i;
    const char *mousedev = SDL_getenv("SDL_MOUSEDEV");
    const char *mousedrv = SDL_getenv("SDL_MOUSEDRV");
    mouse_fd = -1;

#if SDL_INPUT_TSLIB
    if (mousedrv && (SDL_strcmp(mousedrv, "TSLIB") == 0)) {
        if (mousedev == NULL) mousedev = SDL_getenv("TSLIB_TSDEVICE");
        if (mousedev != NULL) {
            ts_dev = ts_open(mousedev, 1);
            if ((ts_dev != NULL) && (ts_config(ts_dev) >= 0)) {
                mouse_drv = MOUSE_TSLIB;
                mouse_fd = ts_fd(ts_dev);
                return mouse_fd;
            }
        }
        mouse_drv = MOUSE_NONE;
        return mouse_fd;
    }
#endif

    if (mousedrv && (SDL_strcmp(mousedrv, "ELO") == 0)) {
        mouse_fd = open(mousedev, O_RDWR);
        if (mouse_fd >= 0 && eloInitController(mouse_fd))
            mouse_drv = MOUSE_ELO;
        else
            mouse_drv = MOUSE_NONE;
        return mouse_fd;
    }

    if (mousedev == NULL) {
        static const char *ps2mice[] = {
            "/dev/input/mice", "/dev/usbmouse", "/dev/psaux", NULL
        };
        if (mouse_fd < 0) {
            char proto[10];
            if (gpm_available(proto, SDL_arraysize(proto))) {
                mouse_fd = open(GPM_NODE_FIFO, O_RDONLY, 0);
                if (mouse_fd >= 0) {
                    if      (SDL_strcmp(proto,"msc")  == 0) mouse_drv = MOUSE_MSC;
                    else if (SDL_strcmp(proto,"ps2")  == 0) mouse_drv = MOUSE_PS2;
                    else if (SDL_strcmp(proto,"imps2")== 0) mouse_drv = MOUSE_IMPS2;
                    else if (SDL_strcmp(proto,"ms")   == 0 ||
                             SDL_strcmp(proto,"bare") == 0) mouse_drv = MOUSE_MS;
                    else if (SDL_strcmp(proto,"bm")   == 0) mouse_drv = MOUSE_BM;
                    else { close(mouse_fd); mouse_fd = -1; }
                }
            }
        }
        for (i = 0; (mouse_fd < 0) && ps2mice[i]; ++i) {
            mouse_fd = open(ps2mice[i], O_RDWR, 0);
            if (mouse_fd < 0) mouse_fd = open(ps2mice[i], O_RDONLY, 0);
            if (mouse_fd >= 0) {
                set_imps2_mode(mouse_fd);
                mouse_drv = detect_imps2(mouse_fd) ? MOUSE_IMPS2 : MOUSE_PS2;
            }
        }
        if (mouse_fd < 0) {
            mouse_fd = open("/dev/adbmouse", O_RDONLY, 0);
            if (mouse_fd >= 0) mouse_drv = MOUSE_BM;
        }
    }
    if (mouse_fd < 0) {
        if (mousedev == NULL) mousedev = "/dev/mouse";
        mouse_fd = open(mousedev, O_RDONLY, 0);
        if (mouse_fd >= 0) {
            struct termios mt;
            tcgetattr(mouse_fd, &mt);
            mt.c_iflag = IGNBRK|IGNPAR;
            mt.c_oflag = mt.c_lflag = mt.c_line = 0;
            mt.c_cc[VTIME] = 0; mt.c_cc[VMIN] = 1;
            mt.c_cflag = CREAD|CLOCAL|HUPCL|CS8|B1200;
            tcsetattr(mouse_fd, TCSAFLUSH, &mt);
            mouse_drv = (mousedrv && SDL_strcmp(mousedrv,"PS2")==0)
                        ? MOUSE_PS2 : MOUSE_MS;
        }
    }
    if (mouse_fd < 0) mouse_drv = MOUSE_NONE;
    return mouse_fd;
}

static int posted = 0;

void FB_vgamousecallback(int button, int relative, int dx, int dy)
{
    int button_1, button_3, button_state, state_changed, i;
    Uint8 state;

    if (dx || dy)
        posted += SDL_PrivateMouseMotion(0, relative, dx, dy);

    button_1 = (button & 0x04) >> 2;
    button_3 = (button & 0x01) << 2;
    button &= ~0x05;
    button |= (button_1|button_3);

    button_state  = SDL_GetMouseState(NULL, NULL);
    state_changed = button_state ^ button;
    for (i = 0; i < 8; ++i) {
        if (state_changed & (1<<i)) {
            state = (button & (1<<i)) ? SDL_PRESSED : SDL_RELEASED;
            posted += SDL_PrivateMouseButton(state, i+1, 0, 0);
        }
    }
}

#if SDL_INPUT_TSLIB
static void handle_tslib(_THIS)
{
    struct ts_sample sample;
    int button;
    while (ts_read(ts_dev, &sample, 1) > 0) {
        button = (sample.pressure > 0) ? 1 : 0;
        button <<= 2;
        FB_vgamousecallback(button, 0, sample.x, sample.y);
    }
}
#endif

static void handle_mouse(_THIS)
{
    static int start = 0;
    static unsigned char mousebuf[BUFSIZ];
    static int relative = 1;
    int i, nread, button = 0, dx = 0, dy = 0, packetsize = 0;
    int realx, realy;

    switch (mouse_drv) {
    case MOUSE_NONE:    break;
    case MOUSE_MSC:     packetsize = 5; break;
    case MOUSE_IMPS2:   packetsize = 4; break;
    case MOUSE_PS2: case MOUSE_MS: case MOUSE_BM: packetsize = 3; break;
    case MOUSE_ELO:
        if (eloReadPosition(this, mouse_fd, &dx, &dy, &button, &realx, &realy)) {
            button = (button & 0x01) << 2;
            FB_vgamousecallback(button, 0, dx, dy);
        }
        return;
    case MOUSE_TSLIB:
#if SDL_INPUT_TSLIB
        handle_tslib(this);
#endif
        return;
    default: packetsize = 0; break;
    }

    nread = read(mouse_fd, &mousebuf[start], BUFSIZ-start);
    if (nread < 0) return;
    if (mouse_drv == MOUSE_NONE) return;
    nread += start;

    for (i = 0; i < (nread-(packetsize-1)); i += packetsize) {
        switch (mouse_drv) {
        case MOUSE_MSC:
            if ((mousebuf[i] & 0xF8) != 0x80) { i -= (packetsize-1); continue; }
            button =   (~mousebuf[i]) & 0x07;
            dx =   (signed char)(mousebuf[i+1]) + (signed char)(mousebuf[i+3]);
            dy = -((signed char)(mousebuf[i+2]) + (signed char)(mousebuf[i+4]));
            break;
        case MOUSE_PS2:
            if ((mousebuf[i] & 0xC0) != 0) { i -= (packetsize-1); continue; }
            button = (mousebuf[i]&0x04)>>1 | (mousebuf[i]&0x02)>>1 | (mousebuf[i]&0x01)<<2;
            dx = (mousebuf[i]&0x10) ? mousebuf[i+1]-256 : mousebuf[i+1];
            dy = (mousebuf[i]&0x20) ? -(mousebuf[i+2]-256) : -mousebuf[i+2];
            break;
        case MOUSE_IMPS2:
            button = (mousebuf[i]&0x04)>>1|(mousebuf[i]&0x02)>>1|(mousebuf[i]&0x01)<<2|
                     (mousebuf[i]&0x40)>>3|(mousebuf[i]&0x80)>>3;
            dx = (mousebuf[i]&0x10) ? mousebuf[i+1]-256 : mousebuf[i+1];
            dy = (mousebuf[i]&0x20) ? -(mousebuf[i+2]-256) : -mousebuf[i+2];
            switch (mousebuf[i+3]&0x0F) {
            case 0x0F: FB_vgamousecallback(button|(1<<3), 1, 0, 0); break;
            case 0x01: FB_vgamousecallback(button|(1<<4), 1, 0, 0); break;
            default: break;
            }
            break;
        case MOUSE_MS:
            if ((mousebuf[i]&0x40) != 0x40) { i -= (packetsize-1); continue; }
            button = ((mousebuf[i]&0x20)>>3)|((mousebuf[i]&0x10)>>4);
            dx = (signed char)(((mousebuf[i]&0x03)<<6)|(mousebuf[i+1]&0x3F));
            dy = (signed char)(((mousebuf[i]&0x0C)<<4)|(mousebuf[i+2]&0x3F));
            break;
        case MOUSE_BM:
            if ((mousebuf[i]&0xF8) != 0x80) { i -= (packetsize-1); continue; }
            button = (~mousebuf[i]) & 0x07;
            dx =  (signed char)mousebuf[i+1];
            dy = -(signed char)mousebuf[i+2];
            break;
        default: dx = dy = 0; break;
        }
        FB_vgamousecallback(button, relative, dx, dy);
    }
    if (i < nread) {
        SDL_memcpy(mousebuf, &mousebuf[i], (nread-i));
        start = nread-i;
    } else {
        start = 0;
    }
}

/* ---- VT switch helpers ---- */

static void switch_vt_prep(_THIS)
{
    SDL_Surface *screen = SDL_VideoSurface;
    SDL_PrivateAppActive(0, SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS);
    wait_idle(this);
    screen_arealen = (screen->h + (2*this->offset_y)) * screen->pitch;
    screen_contents = (Uint8 *)SDL_malloc(screen_arealen);
    if (screen_contents) SDL_memcpy(screen_contents, screen->pixels, screen_arealen);
    FB_SavePaletteTo(this, 256, screen_palette);
    ioctl(console_fd, FBIOGET_VSCREENINFO, &screen_vinfo);
    ioctl(keyboard_fd, KDSETMODE, KD_TEXT);
    ioctl(keyboard_fd, VT_UNLOCKSWITCH, 1);
}

static void switch_vt_done(_THIS)
{
    SDL_Surface *screen = SDL_VideoSurface;
    ioctl(keyboard_fd, VT_LOCKSWITCH, 1);
    ioctl(keyboard_fd, KDSETMODE, KD_GRAPHICS);
    ioctl(console_fd, FBIOPUT_VSCREENINFO, &screen_vinfo);
    FB_RestorePaletteFrom(this, 256, screen_palette);
    if (screen_contents) {
        SDL_memcpy(screen->pixels, screen_contents, screen_arealen);
        SDL_free(screen_contents);
        screen_contents = NULL;
    }
    if (SDL_ShadowSurface) SDL_UpdateRect(SDL_ShadowSurface, 0, 0, 0, 0);
    SDL_PrivateAppActive(1, SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS);
}

static void switch_vt(_THIS, unsigned short which)
{
    struct vt_stat vtstate;
    if ((ioctl(keyboard_fd, VT_GETSTATE, &vtstate) < 0) ||
        (which == vtstate.v_active)) return;
    SDL_mutexP(hw_lock);
    switch_vt_prep(this);
    if (ioctl(keyboard_fd, VT_ACTIVATE, which) == 0) {
        ioctl(keyboard_fd, VT_WAITACTIVE, which);
        switched_away = 1;
    } else {
        switch_vt_done(this);
    }
    SDL_mutexV(hw_lock);
}

static void handle_keyboard(_THIS)
{
    unsigned char keybuf[BUFSIZ];
    int i, nread, pressed, scancode;
    SDL_keysym keysym;

    nread = read(keyboard_fd, keybuf, BUFSIZ);
    for (i = 0; i < nread; ++i) {
        scancode = keybuf[i] & 0x7F;
        pressed  = (keybuf[i] & 0x80) ? SDL_RELEASED : SDL_PRESSED;
        TranslateKey(scancode, &keysym);
        switch (keysym.sym) {
        case SDLK_F1:  case SDLK_F2:  case SDLK_F3:  case SDLK_F4:
        case SDLK_F5:  case SDLK_F6:  case SDLK_F7:  case SDLK_F8:
        case SDLK_F9:  case SDLK_F10: case SDLK_F11: case SDLK_F12:
            if ((SDL_GetModState()&KMOD_CTRL) && (SDL_GetModState()&KMOD_ALT)) {
                if (pressed) switch_vt(this, (keysym.sym-SDLK_F1)+1);
                break;
            }
            /* fall through */
        default:
            posted += SDL_PrivateKeyboard(pressed, &keysym);
            break;
        }
    }
}

void FB_PumpEvents(_THIS)
{
    fd_set fdset;
    int max_fd = 0;
    static struct timeval zero;

    do {
        if (switched_away) {
            struct vt_stat vtstate;
            SDL_mutexP(hw_lock);
            if ((ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0) &&
                vtstate.v_active == current_vt) {
                switched_away = 0;
                switch_vt_done(this);
            }
            SDL_mutexV(hw_lock);
        }

        posted = 0;
        FD_ZERO(&fdset);
        if (keyboard_fd >= 0) {
            FD_SET(keyboard_fd, &fdset);
            if (max_fd < keyboard_fd) max_fd = keyboard_fd;
        }
        if (mouse_fd >= 0) {
            FD_SET(mouse_fd, &fdset);
            if (max_fd < mouse_fd) max_fd = mouse_fd;
        }
        if (select(max_fd+1, &fdset, NULL, NULL, &zero) > 0) {
            if (keyboard_fd >= 0 && FD_ISSET(keyboard_fd, &fdset))
                handle_keyboard(this);
            if (mouse_fd >= 0 && FD_ISSET(mouse_fd, &fdset))
                handle_mouse(this);
        }
    } while (posted);
}

void FB_InitOSKeymap(_THIS)
{
    int i;
    for (i = 0; i < SDL_arraysize(keymap); ++i) {
        switch (i) {
        case SCANCODE_PRINTSCREEN:      keymap[i] = SDLK_PRINT;  break;
        case SCANCODE_BREAK:            keymap[i] = SDLK_BREAK;  break;
        case SCANCODE_BREAK_ALTERNATIVE:keymap[i] = SDLK_PAUSE;  break;
        case SCANCODE_LEFTSHIFT:        keymap[i] = SDLK_LSHIFT; break;
        case SCANCODE_RIGHTSHIFT:       keymap[i] = SDLK_RSHIFT; break;
        case SCANCODE_LEFTCONTROL:      keymap[i] = SDLK_LCTRL;  break;
        case SCANCODE_RIGHTCONTROL:     keymap[i] = SDLK_RCTRL;  break;
        case SCANCODE_RIGHTWIN:         keymap[i] = SDLK_RSUPER; break;
        case SCANCODE_LEFTWIN:          keymap[i] = SDLK_LSUPER; break;
        case SCANCODE_LEFTALT:          keymap[i] = SDLK_LALT;   break;
        case SCANCODE_RIGHTALT:         keymap[i] = SDLK_RALT;   break;
        case 127:                       keymap[i] = SDLK_MENU;   break;
        default: keymap[i] = KVAL(vga_keymap[0][i]); break;
        }
    }
    for (i = 0; i < SDL_arraysize(keymap); ++i) {
        switch (keymap_temp[i]) {
        case K_F1:  keymap[i]=SDLK_F1;  break; case K_F2:  keymap[i]=SDLK_F2;  break;
        case K_F3:  keymap[i]=SDLK_F3;  break; case K_F4:  keymap[i]=SDLK_F4;  break;
        case K_F5:  keymap[i]=SDLK_F5;  break; case K_F6:  keymap[i]=SDLK_F6;  break;
        case K_F7:  keymap[i]=SDLK_F7;  break; case K_F8:  keymap[i]=SDLK_F8;  break;
        case K_F9:  keymap[i]=SDLK_F9;  break; case K_F10: keymap[i]=SDLK_F10; break;
        case K_F11: keymap[i]=SDLK_F11; break; case K_F12: keymap[i]=SDLK_F12; break;
        case K_DOWN:  keymap[i]=SDLK_DOWN;  break;
        case K_LEFT:  keymap[i]=SDLK_LEFT;  break;
        case K_RIGHT: keymap[i]=SDLK_RIGHT; break;
        case K_UP:    keymap[i]=SDLK_UP;    break;
        case K_P0: keymap[i]=SDLK_KP0; break; case K_P1: keymap[i]=SDLK_KP1; break;
        case K_P2: keymap[i]=SDLK_KP2; break; case K_P3: keymap[i]=SDLK_KP3; break;
        case K_P4: keymap[i]=SDLK_KP4; break; case K_P5: keymap[i]=SDLK_KP5; break;
        case K_P6: keymap[i]=SDLK_KP6; break; case K_P7: keymap[i]=SDLK_KP7; break;
        case K_P8: keymap[i]=SDLK_KP8; break; case K_P9: keymap[i]=SDLK_KP9; break;
        case K_PPLUS:  keymap[i]=SDLK_KP_PLUS;     break;
        case K_PMINUS: keymap[i]=SDLK_KP_MINUS;    break;
        case K_PSTAR:  keymap[i]=SDLK_KP_MULTIPLY; break;
        case K_PSLASH: keymap[i]=SDLK_KP_DIVIDE;   break;
        case K_PENTER: keymap[i]=SDLK_KP_ENTER;    break;
        case K_PDOT:   keymap[i]=SDLK_KP_PERIOD;   break;
        case K_SHIFT:  keymap[i]=(keymap[i]!=SDLK_RSHIFT)?SDLK_LSHIFT:SDLK_RSHIFT; break;
        case K_SHIFTL: keymap[i]=SDLK_LSHIFT; break;
        case K_SHIFTR: keymap[i]=SDLK_RSHIFT; break;
        case K_CTRL:   keymap[i]=(keymap[i]!=SDLK_RCTRL)?SDLK_LCTRL:SDLK_RCTRL; break;
        case K_CTRLL:  keymap[i]=SDLK_LCTRL;  break;
        case K_CTRLR:  keymap[i]=SDLK_RCTRL;  break;
        case K_ALT:    keymap[i]=SDLK_LALT;   break;
        case K_ALTGR:  keymap[i]=SDLK_RALT;   break;
        case K_INSERT: keymap[i]=SDLK_INSERT;   break;
        case K_REMOVE: keymap[i]=SDLK_DELETE;   break;
        case K_PGUP:   keymap[i]=SDLK_PAGEUP;   break;
        case K_PGDN:   keymap[i]=SDLK_PAGEDOWN; break;
        case K_FIND:   keymap[i]=SDLK_HOME;     break;
        case K_SELECT: keymap[i]=SDLK_END;      break;
        case K_NUM:    keymap[i]=SDLK_NUMLOCK;  break;
        case K_CAPS:   keymap[i]=SDLK_CAPSLOCK; break;
        case K_F13:    keymap[i]=SDLK_PRINT;    break;
        case K_HOLD:   keymap[i]=SDLK_SCROLLOCK;break;
        case K_PAUSE:  keymap[i]=SDLK_PAUSE;    break;
        case 127:      keymap[i]=SDLK_BACKSPACE; break;
        default: break;
        }
    }
}

static SDL_keysym *TranslateKey(int scancode, SDL_keysym *keysym)
{
    keysym->scancode = scancode;
    keysym->sym      = keymap[scancode];
    keysym->mod      = KMOD_NONE;
    keysym->unicode  = 0;
    if (SDL_TranslateUNICODE) {
        int map = 0;
        SDLMod modstate = SDL_GetModState();
        if (modstate & KMOD_SHIFT) map |= (1<<KG_SHIFT);
        if (modstate & KMOD_CTRL)  map |= (1<<KG_CTRL);
        if (modstate & KMOD_LALT)  map |= (1<<KG_ALT);
        if (modstate & KMOD_RALT)  map |= (1<<KG_ALTGR);
        if (KTYP(vga_keymap[map][scancode]) == KT_LETTER)
            if (modstate & KMOD_CAPS) map ^= (1<<KG_SHIFT);
        if (KTYP(vga_keymap[map][scancode]) == KT_PAD) {
            if (modstate & KMOD_NUM)
                keysym->unicode = KVAL(vga_keymap[map][scancode]);
        } else {
            keysym->unicode = KVAL(vga_keymap[map][scancode]);
        }
    }
    return keysym;
}

#endif /* !POWKIDDY */