#include <cstdio>
#include <SDL2/SDL.h>

int main() {
    SDL_Init(SDL_INIT_EVERYTHING);

    SDL_CreateWindow("Gamepad ID", 50, 50, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_QUIT) {
            printf("Quit requested\n");
            break;
        } else if (event.type == SDL_KEYUP) {
            if (event.key.keysym.scancode == SDL_SCANCODE_Q) {
                printf("Quit requested\n");
                break;
            }
        } else if (event.type == SDL_JOYAXISMOTION) {
            printf("Joystick axis %d value %d\n", (int) event.jaxis.axis, (int) event.jaxis.value);
        } else if (event.type == SDL_JOYHATMOTION) {
            printf("Joystick hat %d value %d\n", (int) event.jhat.hat, (int) event.jhat.value);
        } else if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) {
            printf("Joystick button %d state %d\n", (int) event.jbutton.button,
                   (int) event.jbutton.state);
        } else if (event.type == SDL_CONTROLLERAXISMOTION) {
            printf("Controller axis %d value %d\n", (int) event.caxis.axis,
                   (int) event.caxis.value);
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
            printf("Controller button %d state %d\n", (int) event.cbutton.button,
                   (int) event.cbutton.state);
        } else if (event.type == SDL_JOYDEVICEADDED) {
            int joy_index = event.jdevice.which;
            SDL_Joystick* joystick = SDL_JoystickOpen(joy_index);
            printf("Added joystick %s \n", SDL_JoystickName(joystick));
        } else if (event.type == SDL_JOYDEVICEREMOVED) {
            SDL_JoystickID joystickID = event.cdevice.which;
            SDL_Joystick* joystick = SDL_JoystickFromInstanceID(joystickID);
            printf("Removing joystick %s\n", SDL_JoystickName(joystick));
            SDL_JoystickClose(joystick);
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            int joy_index = event.jdevice.which;
            SDL_GameController* controller = SDL_GameControllerOpen(joy_index);
            printf("Added controller %s\n", SDL_GameControllerName(controller));
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            SDL_JoystickID joystickID = event.cdevice.which;
            SDL_GameController* controller = SDL_GameControllerFromInstanceID(joystickID);
            printf("Removing controller %s\n", SDL_GameControllerName(controller));
            SDL_GameControllerClose(controller);
        }
    }

    SDL_Quit();
    return 0;
}
