#include <lvgl.h>
#include "LCD_1in28.h"
#include "CST816S.h"
#include "ui.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

#define LVGL_TICK_PERIOD 15
#define BLACK 0x0000

int hour = 10;
int minute = 8;

unsigned long last_time_update = 0;  // Track last time update in millis

int dma_chan;
dma_channel_config dma_cfg;
volatile bool dma_done = false;

lv_disp_drv_t disp_drv;

void dma_handler()
{
    if (dma_channel_get_irq0_status(dma_chan))
    {
        dma_channel_acknowledge_irq0(dma_chan);
        lv_disp_flush_ready(&disp_drv);
    }
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t width = area->x2 - area->x1 + 1;
    uint16_t height = area->y2 - area->y1 + 1;
    uint32_t size = width * height * 2;

    LCD_1IN28_SendCommand(0x2A);
    LCD_1IN28_SendData_16Bit(area->x1);
    LCD_1IN28_SendData_16Bit(area->x2);
    LCD_1IN28_SendCommand(0x2B);
    LCD_1IN28_SendData_16Bit(area->y1);
    LCD_1IN28_SendData_16Bit(area->y2);
    LCD_1IN28_SendCommand(0x2C);
    DEV_Digital_Write(LCD_DC_PIN, 1);

    dma_channel_set_read_addr(dma_chan, color_p, false);
    dma_channel_set_trans_count(dma_chan, size, true);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    if (CST816S_Get_Point())
    {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = Touch_CTS816.x_point;
        data->point.y = Touch_CTS816.y_point;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    DEV_Module_Init();
    LCD_1IN28_Init(HORIZONTAL);
    //DEV_SET_PWM(100);
    LCD_1IN28_Clear(BLACK);

    lv_init();

    dma_chan = dma_claim_unused_channel(true);
    dma_cfg = dma_channel_get_default_config(dma_chan);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, spi_get_dreq(spi1, true));

    dma_channel_configure(
        dma_chan,
        &dma_cfg,
        &spi_get_hw(spi1)->dr,
        NULL,
        0,
        false
    );

    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    static lv_disp_draw_buf_t draw_buf;
    
    // Full-screen double buffering (adjust if you hit RAM limits)
    static lv_color_t buf1[240 * 120];
    static lv_color_t buf2[240 * 120];
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 240 * 120);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    CST816S_init(CST816S_Point_Mode);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    ui_init();
    attach_brightness_slider_event();  // Already good

    // Read slider's initial value and set hardware brightness accordingly
    int slider_val = lv_slider_get_value(ui_Slider1); 
    int pwm_val = map(slider_val, 10, 100, 25, 255); 
    DEV_SET_PWM(pwm_val);


}
void attach_brightness_slider_event()
{
    lv_obj_add_event_cb(ui_Slider1, brightness_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}
void brightness_slider_event_cb(lv_event_t * e)
{
    int slider_val = lv_slider_get_value(lv_event_get_target(e)); // 10 - 100
    int pwm_val = map(slider_val, 10, 100, 25, 255); // map to full PWM range
    DEV_SET_PWM(pwm_val);
}

void update_time()
{
    // Simple minute increment logic
    minute++;
    if (minute >= 60)
    {
        minute = 0;
        hour++;
        if (hour >= 24)
        {
            hour = 0;
        }
    }

    // Format time string
    char time_str[6];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", hour, minute);

    // Update label if screen2 is active
    if (lv_scr_act() == ui_Screen2)
    {
        lv_label_set_text(ui_Label2, time_str);
    }
}

void check_time_update()
{
    if (millis() - last_time_update >= 60000)  // 1 minute
    {
        last_time_update = millis();
        minute++;
        if (minute >= 60)
        {
            minute = 0;
            hour++;
            if (hour >= 24)
            {
                hour = 0;
            }
        }
    }
}
void refresh_time_label()
{
    char time_str[6];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", hour, minute);
    lv_label_set_text(ui_Label2, time_str);
}


void loop()
{
    lv_tick_inc(LVGL_TICK_PERIOD);
    lv_task_handler();
    delay(5);

    check_time_update();

    // If on screen2, continuously refresh label to reflect latest time
    if (lv_scr_act() == ui_Screen2)
    {
        refresh_time_label();
    }
}


