#include "proapi.h"
// INCLUDING PROAPI.H IS NECESSARY

// KERNEL JUMPS TO FIRST BYTE SO THIS COMES FIRST
__attribute__((section(".text.start")))
void program_main(const struct proapi *api)
{
    api->printf("Hello from a program!\n");

    if (api->args[0] != '\0')
        api->printf("You said: %s\n", api->args);
}
