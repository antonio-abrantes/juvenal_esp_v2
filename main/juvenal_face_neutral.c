/*
 * Juvenal Face V2 - animacao natural e reacoes do assistente.
 * ESP32-S3 N16R8 / GC9A01 240x240 (SPI2) / LVGL 9.4
 * WS2812 onboard GPIO48 (RMT, nao compartilha SPI2).
 *
 * LVGL roda na sua task; RGB e publicado ao unico controlador do LED.
 * Outras tasks publicam somente um pequeno evento atomico.
 * Nenhuma operacao de display/LED entra na task I2S ou do wake word.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lvgl_port.h"
#include "juvenal_led.h"
#include "juvenal_speaker.h"
#include "lvgl.h"
#include "juvenal_face_neutral.h"

#define LCD_W                  240
#define LCD_H                  240
#define LCD_HOST               SPI2_HOST
#define LCD_CLOCK_HZ           (20 * 1000 * 1000)
#define LCD_PIN_SCLK           14
#define LCD_PIN_MOSI           13
#define LCD_PIN_DC             9
#define LCD_PIN_CS             8
#define LCD_PIN_RST            7
#define LCD_BUFFER_LINES       20
#define LED_GPIO               48
#define TICK_MS                50
#define SLEEP_AFTER_MS         (90U * 1000U)
#define WAKE_SURPRISE_MS       640U
#define RESULT_DISPLAY_MS      1500U

#define CLR_BG         0x07151F
#define CLR_OUTLINE    0x164052
#define CLR_EYES       0x7AF4E7
#define CLR_PUPIL      0x11364A
#define CLR_SHINE      0xFFFFFF
#define CLR_MOUTH      0xD8FFF8
#define CLR_ACCENT     0x30DCD1
#define CLR_SLEEP      0x2F777B
#define CLR_SAD        0x75BDBD

static const char *TAG = "JUVENAL_FACE";

typedef enum {
    POSE_IDLE = 0,
    POSE_SLEEP,
    POSE_WAKE,
    POSE_SOUND_WAKE,
    POSE_LISTENING,
    POSE_SENDING,
    POSE_SUCCESS,
    POSE_ERROR
} face_pose_t;

static portMUX_TYPE s_event_mux = portMUX_INITIALIZER_UNLOCKED;
static int s_event;
static bool s_is_sleeping;
static face_pose_t s_pose = POSE_IDLE;   /* Somente task LVGL */
static uint32_t s_pose_since;
static uint32_t s_idle_since;
static uint32_t s_blink_at;
static uint32_t s_last_blink_start;
static uint32_t s_double_blink_at;
static uint32_t s_gaze_at;
static uint32_t s_brow_at;
static uint32_t s_mouth_at;
static float s_gaze_x, s_gaze_y;
static int s_target_x, s_target_y;
static int s_brow_offset;
static int s_mouth_offset;
static float s_audio_open;
static lv_obj_t *s_talking_mouth;

static lv_obj_t *s_screen;
static lv_obj_t *s_eyes[2];
static lv_obj_t *s_pupils[2];
static lv_obj_t *s_shines[2];
static lv_obj_t *s_brows[2];
static lv_obj_t *s_mouth;
static lv_obj_t *s_ring;

/* LVGL guarda os ponteiros recebidos: estes arrays precisam durar para sempre. */
static lv_point_precise_t s_mouth_pts[13];
static lv_point_precise_t s_brow_pts[2][2];

void juvenal_face_event(juvenal_face_event_t event)
{
    /* Apenas uma troca de int em secao critica muito curta. Sem chamadas LVGL. */
    taskENTER_CRITICAL(&s_event_mux);
    if (event == JUVENAL_FACE_SOUND_WAKE) {
        /* Prioridade para eventos reais de wake e envio. */
        if (s_is_sleeping && s_event == 0) s_event = (int)event;
    } else {
        s_event = (int)event;
    }
    taskEXIT_CRITICAL(&s_event_mux);
}

bool juvenal_face_is_sleeping(void)
{
    taskENTER_CRITICAL(&s_event_mux);
    bool result = s_is_sleeping;
    taskEXIT_CRITICAL(&s_event_mux);
    return result;
}

static uint32_t random_between(uint32_t min, uint32_t max)
{
    /* RNG Espressif; chamamos somente para sortear os proximos intervalos. */
    return min + (esp_random() % (max - min + 1));
}

static int max_int(int a, int b) { return a > b ? a : b; }
static int min_int(int a, int b) { return a < b ? a : b; }

static void hide_obj(lv_obj_t *obj, bool hide)
{
    if (hide) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_rect(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t fill, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) return NULL;
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void set_led(int r, int g, int b)
{
    juvenal_led_set_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static void update_led(uint32_t since_pose)
{
    switch (s_pose) {
    case POSE_WAKE: {
        /* 3 piscadas verdes: 90ms aceso / 95ms apagado. */
        const uint32_t step = since_pose / 185U;
        if (step < 3 && (since_pose % 185U) < 90U) set_led(0, 46, 7);
        else if (step < 3) set_led(0, 0, 0);
        else set_led(0, 10, 43); /* Azul enquanto escuta */
        break;
    }
    case POSE_LISTENING:
    case POSE_SENDING:
        set_led(0, 10, 43);
        break;
    case POSE_SUCCESS:
        set_led(0, 46, 8);
        break;
    case POSE_ERROR:
        set_led(52, 0, 0);
        break;
    case POSE_SOUND_WAKE:
        set_led(43, 22, 0); /* Nao altera o LED de espera */
        break;
    case POSE_SLEEP:
        set_led(12, 6, 0); /* Amarelo discreto */
        break;
    case POSE_IDLE:
    default:
        set_led(43, 22, 0); /* Amarelo aguardando */
        break;
    }
}

static void mouth_curve(int curve, int shift)
{
    for (int i = 0; i < 13; ++i) {
        int p = i - 6;
        s_mouth_pts[i].x = i * 6;
        s_mouth_pts[i].y = 20 + (curve * (36 - p * p)) / 36;
    }
    lv_line_set_points(s_mouth, s_mouth_pts, 13);
    lv_obj_set_pos(s_mouth, 84 + shift, 159);
}

static void brow_shape(int i, int y0, int y1, bool visible)
{
    s_brow_pts[i][0].x = 0;
    s_brow_pts[i][0].y = y0;
    s_brow_pts[i][1].x = 55;
    s_brow_pts[i][1].y = y1;
    lv_line_set_points(s_brows[i], s_brow_pts[i], 2);
    hide_obj(s_brows[i], !visible);
}

static void set_pose(face_pose_t pose, uint32_t now)
{
    s_pose = pose;
    s_pose_since = now;
    if (pose == POSE_IDLE) {
        s_idle_since = now;
        s_blink_at = now + random_between(1900, 4800);
        s_gaze_at = now + random_between(700, 1800);
        s_brow_at = now + random_between(1200, 3600);
        s_mouth_at = now + random_between(1400, 3100);
    }
    if (pose == POSE_WAKE || pose == POSE_SOUND_WAKE || pose == POSE_LISTENING) {
        s_target_x = 0;
        s_target_y = 0;
    }
    if (pose == POSE_WAKE) {
        s_double_blink_at = 0;
    }
}

static int eyelid_height(uint32_t elapsed, int normal_h)
{
    const uint32_t D = 170;
    if (elapsed >= D) return normal_h;
    if (elapsed < 65U) return normal_h - (int)((normal_h - 7) * elapsed / 65U);
    if (elapsed < 105U) return 7;
    return 7 + (int)((normal_h - 7) * (elapsed - 105U) / (D - 105U));
}

static void update_pose(uint32_t now)
{
    const uint32_t elapsed = now - s_pose_since;
    if (s_pose == POSE_WAKE && elapsed >= WAKE_SURPRISE_MS) set_pose(POSE_LISTENING, now);
    else if (s_pose == POSE_SOUND_WAKE && elapsed >= 760U) set_pose(POSE_IDLE, now);
    else if ((s_pose == POSE_SUCCESS || s_pose == POSE_ERROR) &&
             elapsed >= RESULT_DISPLAY_MS) set_pose(POSE_IDLE, now);
    else if (s_pose == POSE_IDLE && (now - s_idle_since) >= SLEEP_AFTER_MS)
        set_pose(POSE_SLEEP, now);
}

static void update_idle_random(uint32_t now)
{
    if (s_pose != POSE_IDLE) return;
    if ((int32_t)(now - s_gaze_at) >= 0) {
        static const int positions[][2] = {
            {0, 0}, {0, 0}, {-8, 0}, {8, 1}, {-6, -5}, {5, -6},
            {0, -7}, {-3, 3}, {3, 2}
        };
        unsigned index = (unsigned)(esp_random() % (sizeof(positions) / sizeof(positions[0])));
        s_target_x = positions[index][0];
        s_target_y = positions[index][1];
        s_gaze_at = now + random_between(1500, 4300);
    }
    if ((int32_t)(now - s_brow_at) >= 0) {
        s_brow_offset = (int)(esp_random() % 5U) - 2;
        s_brow_at = now + random_between(1600, 5100);
    }
    if ((int32_t)(now - s_mouth_at) >= 0) {
        s_mouth_offset = (int)(esp_random() % 5U) - 2;
        s_mouth_at = now + random_between(1700, 4100);
    }
    /* Piscar com duracao variavel, e ocasionalmente uma piscada dupla. */
    if ((int32_t)(now - s_blink_at) >= 0) {
        if (esp_random() % 7U == 0U) s_double_blink_at = s_blink_at + 220U;
        s_blink_at = now + random_between(2600, 6900);
    }
}

static void animate_face(uint32_t now)
{
    update_idle_random(now);
    int eye_w = 63;
    int eye_h = 71;
    int eye_y = 72;
    int brow_y = 8;
    int mouth = 2;
    int mouth_shift = 0;
    int gx = 0, gy = 0;
    bool show_brows = true;
    bool sad_brows = false;
    bool sleeping = false;
    bool wink_left = false;
    const uint32_t elapsed = now - s_pose_since;

    /* Movimento ocular continuo por aproximacao, sem saltos mecanicos. */
    if (s_pose == POSE_IDLE) {
        s_gaze_x += ((float)s_target_x - s_gaze_x) * 0.19f;
        s_gaze_y += ((float)s_target_y - s_gaze_y) * 0.19f;
        gx = (int)(s_gaze_x + (s_gaze_x >= 0 ? 0.5f : -0.5f));
        gy = (int)(s_gaze_y + (s_gaze_y >= 0 ? 0.5f : -0.5f));
        brow_y += s_brow_offset;
        mouth = 2 + s_mouth_offset;
        mouth_shift = s_mouth_offset / 2;
    } else {
        s_gaze_x *= 0.65f;
        s_gaze_y *= 0.65f;
        gx = (int)s_gaze_x;
        gy = (int)s_gaze_y;
    }

    switch (s_pose) {
    case POSE_SLEEP:
        sleeping = true;
        show_brows = false;
        eye_h = 7;
        eye_y = 107;
        mouth = 1;
        break;
    case POSE_SOUND_WAKE:
        eye_w = 65;
        eye_h = 80;
        eye_y = 65;
        brow_y = 0;
        gx = (elapsed / 90U) % 2U ? 4 : -4; /* Leve sustinho */
        mouth = 0;
        break;
    case POSE_WAKE:
        eye_w = 66;
        eye_h = 84;
        eye_y = 63;
        brow_y = 1;
        mouth = 0; /* Sustinho sem abrir a boca */
        break;
    case POSE_LISTENING:
        eye_h = 76;
        eye_y = 68;
        brow_y = 3;
        mouth = 1;
        break;
    case POSE_SENDING:
        eye_h = 68;
        eye_y = 74;
        gx = 0;
        gy = -3;
        brow_y = 9;
        mouth = 0;
        break;
    case POSE_SUCCESS:
        eye_h = 63;
        eye_y = 77;
        mouth = 13;
        brow_y = 5;
        wink_left = elapsed >= 180U && elapsed < 420U;
        break;
    case POSE_ERROR:
        eye_h = 57;
        eye_y = 79;
        mouth = -11;
        sad_brows = true;
        break;
    case POSE_IDLE:
    default:
        break;
    }

    int blink_h = eye_h;
    if (s_pose == POSE_IDLE) {
        /* Atualizacao de blink: janela em torno do ultimo disparo.
           s_blink_at aponta para a PROXIMA piscada, por isso precisamos
           guardar inicio da ultima em variavel separada (abaixo). */
        if (s_last_blink_start && now - s_last_blink_start < 170U)
            blink_h = eyelid_height(now - s_last_blink_start, eye_h);
        if (s_double_blink_at && (int32_t)(now - s_double_blink_at) >= 0 &&
            now - s_double_blink_at < 170U)
            blink_h = eyelid_height(now - s_double_blink_at, eye_h);
    }

    for (int i = 0; i < 2; ++i) {
        int this_h = (wink_left && i == 0) ? 7 : blink_h;
        int this_y = eye_y + (eye_h - this_h) / 2;
        lv_obj_set_pos(s_eyes[i], i == 0 ? 42 : 135, this_y);
        lv_obj_set_size(s_eyes[i], eye_w, this_h);
        hide_obj(s_pupils[i], sleeping || this_h < 22);
        hide_obj(s_shines[i], sleeping || this_h < 22);
        if (!sleeping && this_h >= 22) {
            const int px = min_int(eye_w - 21, max_int(3, eye_w / 2 - 8 + gx));
            const int py = min_int(this_h - 30, max_int(2, this_h / 2 - 10 + gy));
            lv_obj_set_pos(s_pupils[i], px, py);
            lv_obj_set_pos(s_shines[i], px + 3, py + 3);
        }
        if (sad_brows) {
            if (i == 0) brow_shape(i, 17, 3, true);
            else brow_shape(i, 3, 17, true);
        } else {
            brow_shape(i, brow_y, brow_y, show_brows);
        }
    }
    lv_obj_set_style_bg_color(s_eyes[0], lv_color_hex(sleeping ? CLR_SLEEP : CLR_EYES), 0);
    lv_obj_set_style_bg_color(s_eyes[1], lv_color_hex(sleeping ? CLR_SLEEP : CLR_EYES), 0);
    lv_obj_set_style_line_color(s_mouth, lv_color_hex(s_pose == POSE_ERROR ? CLR_SAD : CLR_MOUTH), 0);
    bool playing = false;
    const unsigned amplitude = juvenal_speaker_played_level(&playing);
    float target = amplitude < 80 ? 0.0f : (float)amplitude / 6000.0f;
    if (target > 1.0f) target = 1.0f;
    if (!playing) s_audio_open = 0.0f;
    else s_audio_open += (target - s_audio_open) * (target > s_audio_open ? 0.70f : 0.45f);
    const int opening = (int)(s_audio_open * 26.0f);
    const bool open = playing && opening > 1;
    hide_obj(s_mouth, open);
    hide_obj(s_talking_mouth, !open);
    if (open) {
        int width = 40 + opening;
        lv_obj_set_size(s_talking_mouth, width, opening + 3);
        lv_obj_set_pos(s_talking_mouth, 120 - width / 2, 179 - opening / 2);
    }
    mouth_curve(mouth, mouth_shift);
}

static void face_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t now = lv_tick_get();
    taskENTER_CRITICAL(&s_event_mux);
    const int event = s_event;
    s_event = 0;
    taskEXIT_CRITICAL(&s_event_mux);
    if (event != 0) {
        switch (event) {
        case JUVENAL_FACE_WAKE: set_pose(POSE_WAKE, now); break;
        case JUVENAL_FACE_SOUND_WAKE:
            if (s_pose == POSE_SLEEP) set_pose(POSE_SOUND_WAKE, now);
            break;
        case JUVENAL_FACE_TEST_RECORDING:
        case JUVENAL_FACE_LISTENING: set_pose(POSE_LISTENING, now); break;
        case JUVENAL_FACE_IDLE: set_pose(POSE_IDLE, now); break;
        case JUVENAL_FACE_SENDING: set_pose(POSE_SENDING, now); break;
        case JUVENAL_FACE_SUCCESS: set_pose(POSE_SUCCESS, now); break;
        case JUVENAL_FACE_ERROR: set_pose(POSE_ERROR, now); break;
        default: break;
        }
    }
    /* A ultima piscada e atualizada somente ao agendar a proxima. */
    if (s_pose == POSE_IDLE && (int32_t)(now - s_blink_at) >= 0) {
        s_last_blink_start = now;
    }
    update_pose(now);
    taskENTER_CRITICAL(&s_event_mux);
    s_is_sleeping = (s_pose == POSE_SLEEP);
    taskEXIT_CRITICAL(&s_event_mux);
    animate_face(now);
    update_led(now - s_pose_since);
}

static bool show_face(void)
{
    s_screen = lv_screen_active();
    if (!s_screen) return false;
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_ring = lv_obj_create(s_screen);
    if (!s_ring) return false;
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, 216, 216);
    lv_obj_center(s_ring);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(CLR_OUTLINE), 0);
    lv_obj_set_style_border_width(s_ring, 3, 0);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_SCROLLABLE);
    if (!make_rect(s_screen, 116, 19, 8, 8, CLR_ACCENT, LV_RADIUS_CIRCLE)) return false;

    for (int i = 0; i < 2; ++i) {
        s_eyes[i] = make_rect(s_screen, i ? 135 : 42, 72, 63, 71, CLR_EYES, 25);
        if (!s_eyes[i]) return false;
        s_pupils[i] = make_rect(s_eyes[i], 23, 25, 17, 25, CLR_PUPIL, LV_RADIUS_CIRCLE);
        s_shines[i] = make_rect(s_eyes[i], 25, 28, 6, 7, CLR_SHINE, LV_RADIUS_CIRCLE);
        if (!s_pupils[i] || !s_shines[i]) return false;
        s_brows[i] = lv_line_create(s_screen);
        if (!s_brows[i]) return false;
        lv_obj_set_style_line_color(s_brows[i], lv_color_hex(CLR_ACCENT), 0);
        lv_obj_set_style_line_width(s_brows[i], 5, 0);
        lv_obj_set_style_line_rounded(s_brows[i], true, 0);
        brow_shape(i, 8, 8, true);
        lv_obj_set_pos(s_brows[i], i ? 139 : 46, 46);
    }

    s_mouth = lv_line_create(s_screen);
    if (!s_mouth) return false;
    lv_obj_set_style_line_color(s_mouth, lv_color_hex(CLR_MOUTH), 0);
    lv_obj_set_style_line_width(s_mouth, 6, 0);
    lv_obj_set_style_line_rounded(s_mouth, true, 0);
    mouth_curve(2, 0);
    s_talking_mouth = make_rect(s_screen, 100, 178, 40, 3, CLR_MOUTH, LV_RADIUS_CIRCLE);
    if (!s_talking_mouth) return false;
    hide_obj(s_talking_mouth, true);

    /* Sem label inferior: somente rosto, aro e ponto decorativo superior. */
    const uint32_t now = lv_tick_get();
    set_pose(POSE_IDLE, now);
    return lv_timer_create(face_timer_cb, TICK_MS, NULL) != NULL;
}

#define FACE_INIT_CHECK(call) do { esp_err_t result = (call); if (result != ESP_OK) { ESP_LOGE(TAG, "Inicializacao da face: %s", esp_err_to_name(result)); return result; } } while (0)

esp_err_t juvenal_face_neutral_init(void)
{
    ESP_LOGI(TAG, "Iniciando rosto vivo GC9A01 + WS2812 GPIO48");
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t panel = NULL;
    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_PIN_SCLK, .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_W * LCD_BUFFER_LINES * 2 + 64,
    };
    FACE_INIT_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS, .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0, .pclk_hz = LCD_CLOCK_HZ, .trans_queue_depth = 10,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    FACE_INIT_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    FACE_INIT_CHECK(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &panel));
    FACE_INIT_CHECK(esp_lcd_panel_reset(panel));
    FACE_INIT_CHECK(esp_lcd_panel_init(panel));
    FACE_INIT_CHECK(esp_lcd_panel_invert_color(panel, true));
    FACE_INIT_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    FACE_INIT_CHECK(lvgl_port_init(&port_cfg));
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = io, .panel_handle = panel,
        .buffer_size = LCD_W * LCD_BUFFER_LINES,
        .double_buffer = true, .hres = LCD_W, .vres = LCD_H,
        .monochrome = false, .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = true, .mirror_y = false },
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    lv_display_t *display = lvgl_port_add_disp(&display_cfg);
    FACE_INIT_CHECK(display ? ESP_OK : ESP_ERR_NO_MEM);
    if (!lvgl_port_lock(5000)) {
        ESP_LOGE(TAG, "Falha no mutex LVGL");
        return ESP_ERR_TIMEOUT;
    }
    const bool created = show_face();
    lvgl_port_unlock();
    FACE_INIT_CHECK(created ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Rosto vivo inicializado; repouso apos %u s", (unsigned)(SLEEP_AFTER_MS / 1000U));
    return ESP_OK;
}
