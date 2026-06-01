#include "VolumeMixerController.h"

#include <iostream>
#include <unistd.h>

int main()
{
    VolumeMixerController controller;
    while (true) {
        controller.HrReadLoop();
        sleep(1);
    }
    return 0;
}
