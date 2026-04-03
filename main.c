#include "drivers.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"
#include "jiang.h"

int main(void)
{
    sys_init();

    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();

    wildlife_app_start();

    while(1) {
        delay_us(2000);
        lv_timer_handler();
    }
}
