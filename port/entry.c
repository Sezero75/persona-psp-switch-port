#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define GAME_DIR "sdmc:/switch/persona-runtime"
extern int nx_persona_driver(int argc, char **argv);

static int startup_error(const char *message) {
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    consoleInit(NULL);
    printf("Persona\n\n%s\n\nPress + to exit.\n", message);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 2;
}

int main(void) {
    if (chdir(GAME_DIR) != 0)
        return startup_error("Copy the persona-runtime folder to sdmc:/switch/.");
    if (!freopen("runtime.log", "w", stderr))
        return startup_error("Could not create the log in the persona-runtime folder.");
    setvbuf(stderr, NULL, _IOLBF, 0);
    struct stat st;
    if (stat("persona.image.bin", &st) || st.st_size != 0x51484c)
        return startup_error("The translated game needs the matching persona.image.bin beside the NRO.");
    if (stat("persona.iso", &st) || st.st_size != 820715520)
        return startup_error("Put the matching US Undub disc image beside the NRO as persona.iso.");
    if (appletGetAppletType() != AppletType_Application && appletGetAppletType() != AppletType_SystemApplication)
        fprintf(stderr, "Running in applet mode. Available RAM may be insufficient.\n");
    setenv("PSP_ISO", GAME_DIR "/persona.iso", 1);
    setenv("SR_MEMSTICK", GAME_DIR "/memstick", 1);
    setenv("SR_BOOT_PATH", "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN", 1);
    setenv("SR_FONTDIR", GAME_DIR "/font", 1);
    setenv("SR_GAME_TITLE", "Persona", 1);
    char *args[] = {"persona", "--image", "persona.image.bin", "08804000", "08804114", "none", "none", "--gui", NULL};
    return nx_persona_driver(8, args);
}

