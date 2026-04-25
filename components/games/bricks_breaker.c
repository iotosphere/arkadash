#include "bricks_breaker.h"
#include "encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"

#define BRICK_COLS 6
#define BRICK_ROWS 5
#define BRICK_W 36
#define BRICK_H 16
#define BRICK_PAD 2
#define BRICK_TOP 40
#define BRICK_LEFT 6

#define PADDLE_W 50
#define PADDLE_H 8
#define PADDLE_Y 280

#define BALL_SIZE 8

#define BALL_SPEED 4

typedef struct {
    lv_obj_t *bricks[BRICK_ROWS][BRICK_COLS];
    lv_obj_t *paddle;
    lv_obj_t *ball;
    lv_obj_t *score_label;
    lv_obj_t *game_obj;
    lv_obj_t *hint_label;
    lv_timer_t *game_timer;
    int ball_dx;
    int ball_dy;
    int score;
    int bricks_left;
    bool running;
    bool launched;
    uint32_t start_tick;
    bool key0_was_pressed;
    bool push_was_pressed;
} bricks_game_t;

static bricks_game_t game;

static lv_color_t get_brick_color(int row)
{
    switch (row) {
        case 0: return lv_color_hex(0xff0000);
        case 1: return lv_color_hex(0xff8800);
        case 2: return lv_color_hex(0xffff00);
        case 3: return lv_color_hex(0x00cc00);
        case 4: return lv_color_hex(0x0088ff);
        default: return lv_color_hex(0xffffff);
    }
}

static void update_ball_position(void)
{
    if (!game.ball) return;
    int x = lv_obj_get_x(game.ball) + game.ball_dx;
    int y = lv_obj_get_y(game.ball) + game.ball_dy;
    lv_obj_set_pos(game.ball, x, y);
}

static void check_wall_collision(void)
{
    int x = lv_obj_get_x(game.ball);
    int y = lv_obj_get_y(game.ball);
    int w = lv_obj_get_width(game.game_obj);

    // Sol duvar
    if (x <= 0) {
        x = 1;
        game.ball_dx = abs(game.ball_dx);
    }
    // Sağ duvar
    if (x + BALL_SIZE >= w) {
        x = w - BALL_SIZE - 1;
        game.ball_dx = -abs(game.ball_dx);
    }
    // Üst duvar (tavan)
    if (y <= 0) {
        y = 1;
        game.ball_dy = abs(game.ball_dy);
    }
    // Alt sınır (kayıp)
    if (y > 310) {
        game.running = false;
        if (game.score_label) {
            lv_label_set_text(game.score_label, "GAME OVER");
        }
    }
    
    lv_obj_set_pos(game.ball, x, y);
}

static void check_paddle_collision(void)
{
    // Sadece top aşağı düşerken (ball_dy > 0) paddle çarpışmasını kontrol et
    if (game.ball_dy <= 0) return;

    int bx = lv_obj_get_x(game.ball);
    int by = lv_obj_get_y(game.ball);
    int px = lv_obj_get_x(game.paddle);

    // Topun alt kenarı paddle'ın üst kenarına çarptı mı?
    // Top paddle'ın içindeyken değil, sadece çarpışma anında tepki ver
    if (by + BALL_SIZE >= PADDLE_Y && by + BALL_SIZE <= PADDLE_Y + PADDLE_H + BALL_SPEED) {
        if (bx + BALL_SIZE >= px && bx <= px + PADDLE_W) {
            game.ball_dy = -BALL_SPEED; // Yukarı yönlendir
            int hit_pos = (bx + BALL_SIZE / 2 - px) - PADDLE_W / 2;
            game.ball_dx = hit_pos / 8;
            if (game.ball_dx == 0) game.ball_dx = 1;
            // Topu paddle'ın üstüne yerleştir
            lv_obj_set_pos(game.ball, bx, PADDLE_Y - BALL_SIZE);
        }
    }
}

static void check_brick_collision(void)
{
    int bx = lv_obj_get_x(game.ball);
    int by = lv_obj_get_y(game.ball);

    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!game.bricks[r][c] || !lv_obj_is_valid(game.bricks[r][c])) continue;
            if (lv_obj_has_flag(game.bricks[r][c], LV_OBJ_FLAG_HIDDEN)) continue;

            int brick_x = BRICK_LEFT + c * (BRICK_W + BRICK_PAD);
            int brick_y = BRICK_TOP + r * (BRICK_H + BRICK_PAD);

            if (bx + BALL_SIZE > brick_x && bx < brick_x + BRICK_W &&
                by + BALL_SIZE > brick_y && by < brick_y + BRICK_H) {
                lv_obj_add_flag(game.bricks[r][c], LV_OBJ_FLAG_HIDDEN);
                game.bricks_left--;
                game.score += 10;

                char score_str[32];
                snprintf(score_str, sizeof(score_str), "Score: %d", game.score);
                lv_label_set_text(game.score_label, score_str);

                game.ball_dy = -game.ball_dy;

                if (game.bricks_left == 0) {
                    bricks_breaker_stop();
                    lv_label_set_text(game.score_label, "YOU WIN!");
                }
                return;
            }
        }
    }
}

static void game_timer_cb(lv_timer_t *timer)
{
    static int frame = 0;
    bool push_now = encoder_button_pressed();
    (void)timer;

    frame++;

    /* Encoder push button -> exit game (her durumda çalışır: GAME_OVER, YOU WIN veya normal) */
    if (push_now && !game.push_was_pressed && (lv_tick_get() - game.start_tick > 500)) {
        printf("Push button pressed, exiting game\n");
        bricks_breaker_stop();
        return;
    }
    game.push_was_pressed = push_now;

    if (!game.running) return;

    bool key0_now = key0_pressed();

    /* Encoder rotary -> move paddle (her zaman oku, tüket) */
    int32_t diff = encoder_get_diff();

    if (!game.launched) {
        if (diff != 0) {
            int px = lv_obj_get_x(game.paddle);
            int w = lv_obj_get_width(game.game_obj);
            px += diff * 8;
            if (px < 0) px = 0;
            if (px + PADDLE_W > w) px = w - PADDLE_W;
            lv_obj_set_x(game.paddle, px);
            lv_obj_set_x(game.ball, px + PADDLE_W / 2 - BALL_SIZE / 2);
        }

        /* KEY0 button rising edge -> launch ball */
        if (key0_now && !game.key0_was_pressed && !game.launched) {
            game.launched = true;
            game.ball_dy = -BALL_SPEED;
            game.ball_dx = 2;
            int px = lv_obj_get_x(game.paddle);
            lv_obj_set_pos(game.ball, px + PADDLE_W / 2 - BALL_SIZE / 2, PADDLE_Y - BALL_SIZE);
            lv_obj_set_style_bg_color(game.ball, lv_color_hex(0xff0000), LV_PART_MAIN);
            printf(">>> Ball launched!\n");
            if (game.hint_label) {
                lv_obj_add_flag(game.hint_label, LV_OBJ_FLAG_HIDDEN);
            }
        }
        game.key0_was_pressed = key0_now;
        return;
    }

    /* Launched - fizik islemleri */
    
    // Encoder rotary ile paddle hareketi (launched durumunda da oku!)
    if (diff != 0) {
        int ppx = lv_obj_get_x(game.paddle);
        int w = lv_obj_get_width(game.game_obj);
        ppx += diff * 8;
        if (ppx < 0) ppx = 0;
        if (ppx + PADDLE_W > w) ppx = w - PADDLE_W;
        lv_obj_set_x(game.paddle, ppx);
    }
    
    // Mevcut top pozisyonunu oku
    int bx = lv_obj_get_x(game.ball);
    int by = lv_obj_get_y(game.ball);
    
    // Yeni pozisyonu hesapla
    int new_x = bx + game.ball_dx;
    int new_y = by + game.ball_dy;
    
    // Duvar carpisma
    int w = lv_obj_get_width(game.game_obj);
    
    // Sol duvar
    if (new_x <= 0) {
        new_x = 1;
        game.ball_dx = abs(game.ball_dx);
    }
    // Sag duvar
    if (new_x + BALL_SIZE >= w) {
        new_x = w - BALL_SIZE - 1;
        game.ball_dx = -abs(game.ball_dx);
    }
    // Ust duvar (tavan)
    if (new_y <= 0) {
        new_y = 1;
        game.ball_dy = abs(game.ball_dy);
    }
    // Alt sinir (kayip)
    if (new_y > 310) {
        game.running = false;
        if (game.score_label) {
            lv_label_set_text(game.score_label, "GAME OVER");
        }
        printf("GAME OVER! Score: %d\n", game.score);
        return;
    }
    
    // Paddle kontrol (sadece top asagi duserken)
    if (game.ball_dy > 0) {
        int ppx = lv_obj_get_x(game.paddle);
        if (new_y + BALL_SIZE >= PADDLE_Y && new_y + BALL_SIZE <= PADDLE_Y + PADDLE_H + BALL_SPEED) {
            if (new_x + BALL_SIZE >= ppx && new_x <= ppx + PADDLE_W) {
                game.ball_dy = -BALL_SPEED;
                int hit_pos = (new_x + BALL_SIZE / 2 - ppx) - PADDLE_W / 2;
                game.ball_dx = hit_pos / 8;
                if (game.ball_dx == 0) game.ball_dx = 1;
                new_y = PADDLE_Y - BALL_SIZE;
            }
        }
    }
    
    // Tugla kontrol
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (!game.bricks[r][c] || !lv_obj_is_valid(game.bricks[r][c])) continue;
            if (lv_obj_has_flag(game.bricks[r][c], LV_OBJ_FLAG_HIDDEN)) continue;

            int brick_x = BRICK_LEFT + c * (BRICK_W + BRICK_PAD);
            int brick_y = BRICK_TOP + r * (BRICK_H + BRICK_PAD);

            if (new_x + BALL_SIZE > brick_x && new_x < brick_x + BRICK_W &&
                new_y + BALL_SIZE > brick_y && new_y < brick_y + BRICK_H) {
                lv_obj_add_flag(game.bricks[r][c], LV_OBJ_FLAG_HIDDEN);
                game.bricks_left--;
                game.score += 10;

                char score_str[32];
                snprintf(score_str, sizeof(score_str), "Score: %d", game.score);
                lv_label_set_text(game.score_label, score_str);

                game.ball_dy = -game.ball_dy;
                new_y = brick_y + BRICK_H;  // Topu tuglanin altina tasi

                if (game.bricks_left == 0) {
                    bricks_breaker_stop();
                    lv_label_set_text(game.score_label, "YOU WIN!");
                    return;
                }
                r = BRICK_ROWS;  // Disa cik
                break;
            }
        }
    }
    
    // Topu tasir
    if (lvgl_port_lock(0)) {
        lv_obj_set_pos(game.ball, new_x, new_y);
        lvgl_port_unlock();
    }
    
}

void bricks_breaker_init(lv_obj_t *parent)
{
    game.game_obj = lv_obj_create(parent);
    lv_obj_set_size(game.game_obj, 240, 320);
    lv_obj_set_pos(game.game_obj, 0, 0);
    lv_obj_set_style_bg_color(game.game_obj, lv_color_hex(0xff000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(game.game_obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(game.game_obj, 0, LV_PART_MAIN);
    lv_obj_add_flag(game.game_obj, LV_OBJ_FLAG_HIDDEN);

    game.score_label = lv_label_create(game.game_obj);
    lv_obj_set_pos(game.score_label, 10, 5);
    lv_obj_set_style_text_color(game.score_label, lv_color_hex(0xffffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(game.score_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(game.score_label, "Score: 0");

    game.hint_label = lv_label_create(game.game_obj);
    lv_obj_set_pos(game.hint_label, 30, 150);
    lv_obj_set_style_text_color(game.hint_label, lv_color_hex(0xffaaaaaa), LV_PART_MAIN);
    lv_obj_set_style_text_font(game.hint_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(game.hint_label, "Encoder: move | KEY0: launch\nPush: exit");

    game.paddle = lv_obj_create(game.game_obj);
    lv_obj_set_size(game.paddle, PADDLE_W, PADDLE_H);
    lv_obj_set_pos(game.paddle, 95, PADDLE_Y);
    lv_obj_set_style_bg_color(game.paddle, lv_color_hex(0xffffffff), LV_PART_MAIN);
    lv_obj_set_style_radius(game.paddle, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(game.paddle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(game.paddle, 0, LV_PART_MAIN);
    lv_obj_clear_flag(game.paddle, LV_OBJ_FLAG_SCROLLABLE);

    game.ball = lv_obj_create(game.game_obj);
    lv_obj_set_size(game.ball, BALL_SIZE, BALL_SIZE);
    lv_obj_set_pos(game.ball, 117, PADDLE_Y - BALL_SIZE);
    lv_obj_set_style_bg_color(game.ball, lv_color_hex(0xff888888), LV_PART_MAIN);
    lv_obj_set_style_radius(game.ball, BALL_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(game.ball, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(game.ball, 0, LV_PART_MAIN);
    lv_obj_clear_flag(game.ball, LV_OBJ_FLAG_SCROLLABLE);

    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            lv_obj_t *brick = lv_obj_create(game.game_obj);
            lv_obj_set_size(brick, BRICK_W, BRICK_H);
            lv_obj_set_pos(brick, BRICK_LEFT + c * (BRICK_W + BRICK_PAD),
                           BRICK_TOP + r * (BRICK_H + BRICK_PAD));
            lv_obj_set_style_bg_color(brick, get_brick_color(r), LV_PART_MAIN);
            lv_obj_set_style_radius(brick, 2, LV_PART_MAIN);
            lv_obj_set_style_border_width(brick, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(brick, 0, LV_PART_MAIN);
            lv_obj_clear_flag(brick, LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
            game.bricks[r][c] = brick;
        }
    }
}

void bricks_breaker_start(void)
{
    if (!game.game_obj) return;

    // Eski timer'ı temizle
    if (game.game_timer) {
        lv_timer_del(game.game_timer);
        game.game_timer = NULL;
    }
    // Frame sayacını sıfırla (game_timer_cb içindeki static)
    // Timer silindiği için static frame otomatik sıfırlanacak

    // State'i tamamen sıfırla
    game.running = false;
    game.launched = false;
    game.score = 0;
    game.bricks_left = BRICK_ROWS * BRICK_COLS;
    game.ball_dx = 0;
    game.ball_dy = 0;
    game.start_tick = lv_tick_get();
    game.key0_was_pressed = false;
    game.push_was_pressed = false;

    // Paddle pozisyonunu ortada başlat
    int game_w = lv_obj_get_width(game.game_obj);
    int paddle_start_x = (game_w - PADDLE_W) / 2;
    lv_obj_set_pos(game.paddle, paddle_start_x, PADDLE_Y);

    // Tüm tuğlaları göster
    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (game.bricks[r][c] && lv_obj_is_valid(game.bricks[r][c])) {
                lv_obj_clear_flag(game.bricks[r][c], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Top pozisyonunu paddle'ın ortasına ayarla
    lv_obj_set_pos(game.ball, paddle_start_x + PADDLE_W / 2 - BALL_SIZE / 2, PADDLE_Y - BALL_SIZE);
    lv_obj_set_style_bg_color(game.ball, lv_color_hex(0xff888888), LV_PART_MAIN);
    lv_label_set_text(game.score_label, "Score: 0");
    if (game.hint_label) {
        lv_obj_clear_flag(game.hint_label, LV_OBJ_FLAG_HIDDEN);
    }
    encoder_reset_count();

    // Oyunu göster
    lv_obj_clear_flag(game.game_obj, LV_OBJ_FLAG_HIDDEN);

    // Yeni timer oluştur ve başlat
    game.running = true;
    game.game_timer = lv_timer_create(game_timer_cb, 30, NULL);
}

void bricks_breaker_stop(void)
{
    game.running = false;
    game.launched = false;
    if (game.game_timer) {
        lv_timer_del(game.game_timer);
        game.game_timer = NULL;
    }
    lv_obj_add_flag(game.game_obj, LV_OBJ_FLAG_HIDDEN);
    /* Encoder state'ini sıfırla - oyundan çıkınca encoder düzgün çalışsın */
    encoder_reset_count();
    
    /* Encoder button'un bırakılmasını bekle - tekrar oyun başlamasın */
    printf("Waiting for encoder button release...\n");
    for (int i = 0; i < 100; i++) {  /* Max 5 saniye bekle */
        if (!encoder_button_pressed()) {
            printf("Encoder button released after %d ms\n", i * 50);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    /* Button state'ini sıfırla - art arda çıkış engellemek için */
    game.push_was_pressed = false;
    game.start_tick = lv_tick_get();  /* Yeni start_tick, debounce için */
    
    printf("Bricks Breaker stopped, encoder reset\n");
}
