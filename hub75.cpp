#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <time.h>
#include <iostream>
#include <algorithm>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

#include "font_shinonome6x12.h"


#include "hub75.pio.h"
#include "img.h"


#define DATA_BASE_PIN 0
#define DATA_N_PINS 6
#define ROWSEL_BASE_PIN 20
#define ROWSEL_N_PINS 5
#define CLK_PIN 6
#define STROBE_PIN 14
#define OEN_PIN 15

#define LED_PIN 25

#define WIDTH 64
#define HEIGHT 32
#define HALF_HEIGHT 16

uint DEPTH = 10;

// フレームバッファ
uint8_t frameBuffer_0[HEIGHT][WIDTH][3] = {0};
uint8_t frameBuffer_1[HEIGHT][WIDTH][3] = {0};

uint use_buffer_num = 0;

uint32_t LUT_R[256] = {0};
uint32_t LUT_G[256] = {0};
uint32_t LUT_B[256] = {0};




static inline uint8_t pixel_correct32(uint32_t pix, uint16_t bit) {
    //R, G, Bそれぞれ8bit
    uint32_t r_gamma = pix >> 16 >> bit;
    uint32_t g_gamma = pix >>  8 >> bit;
    uint32_t b_gamma = pix >>  0 >> bit;
    r_gamma = r_gamma & 0b1;
    g_gamma = g_gamma & 0b1;
    b_gamma = b_gamma & 0b1;
    uint8_t rgb = (b_gamma << 2) | (g_gamma << 1) | (r_gamma << 0);
    return rgb;
    //return pix;
}





// LUT生成
void generate_lut(double gamma=2.2, int depth=DEPTH){
    for(int bright = 0; bright < 256; bright++){
        uint32_t pwm_v = (uint32_t)(pow(bright / 255.0, gamma)*((1 << depth) - 1));
        uint32_t pwm_value = 0;
        for(uint bit = 0; bit < 10; bit++){
            //ビットを整列させる (pwm_v & (1 << bit)) << (2*bit) でも可
            pwm_value = pwm_value | ( ((pwm_v & (1 << bit)) >> bit) << (3*bit));
        }
        LUT_R[bright] = pwm_value << 2;
        LUT_G[bright] = pwm_value << 1;
        LUT_B[bright] = pwm_value << 0;
    }
}




// アルファブレンド
uint8_t alpha_blend(uint8_t bright_front, uint8_t bright_back, uint8_t opacity){
    float alpha_front = 1.0 / 255.0 * opacity;
    float bright_out = (bright_front * alpha_front) + bright_back * (1 - alpha_front);
    return int(bright_out);
}





uint fps = 0; //frame per second

void write_matrix(){
    PIO pio = pio0;
    //PIO pio2 = pio1;
    uint sm_data = 0;
    uint sm_row = 1;
    int count = 0;
    //std::chrono::system_clock::time_point tim_start, tim_end;
    clock_t tim_start, tim_end;
    uint data_prog_offs = pio_add_program(pio, &hub75_data_program);
    uint row_prog_offs = pio_add_program(pio, &hub75_row_program);
    hub75_data_program_init(pio, sm_data, data_prog_offs, DATA_BASE_PIN, CLK_PIN);
    hub75_row_program_init(pio, sm_row, row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN);

    //行選択ピン
    const uint mask_abc = 0x0700000;
    const uint mask_de =  0xC000000;

    gpio_init_mask(mask_abc | mask_de);
    gpio_set_dir_out_masked(mask_abc | mask_de);
    gpio_put_masked(mask_abc | mask_de, 0x0);


    // DMA ----------------------------
    uint dma_chan = dma_claim_unused_channel(true); //チャンネル取得
    dma_channel_config dma_c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dma_c, DMA_SIZE_32); //32bit
    channel_config_set_read_increment(&dma_c, true); //リードはインクリメント
    channel_config_set_write_increment(&dma_c, false); //ライトはインクリメントしない
    channel_config_set_dreq(&dma_c, pio_get_dreq(pio, sm_data, true)); // DREQ
    // ? channel_config_set_chain_to(&dma_c, DMA_CB_CHANNEL);
    channel_config_set_irq_quiet(&dma_c, true);
    dma_channel_configure(dma_chan, // DMA channel
                          &dma_c, // pointer to DMA config structure
                          &pio->txf[sm_data], // initial write address
                          NULL, // initial read address
                          1, // number of transfers to perform
                          false); // true to start immediately


    // HUB75 Output ----------------------
    tim_start = clock();
    printf("%d", CLOCKS_PER_SEC);

    uint32_t rowBuffer[WIDTH*2 + 2] = {0}; // +2はCLK操作用のダミー
    while(1){

        if(use_buffer_num == 0){ // フレームバッファ0
        for (int i = 0; i < 1; ++i){
        for (int rowsel = 0; rowsel < 16; ++rowsel){
            gpio_put_masked(mask_abc | mask_de, 0);
            gpio_put_masked(0b111 << 20, (rowsel & 0b00111) << 20);
            gpio_put_masked(0b11 << 26, (rowsel & 0b11000) >> 3 << 26);

            // 行バッファ準備
            for(uint x = 0; x < WIDTH; x++){
                rowBuffer[2*x] = LUT_R[frameBuffer_0[rowsel][x][0]] |
                                 LUT_G[frameBuffer_0[rowsel][x][1]] |
                                 LUT_B[frameBuffer_0[rowsel][x][2]];
                //rowBuffer[2*x] = 0xFFFFFF;
                rowBuffer[2*x+1] = LUT_R[frameBuffer_0[rowsel+HALF_HEIGHT][x][0]] |
                                   LUT_G[frameBuffer_0[rowsel+HALF_HEIGHT][x][1]] |
                                   LUT_B[frameBuffer_0[rowsel+HALF_HEIGHT][x][2]];
            }

            //CLK操作用のダミーピクセル  念のため0にしておく
            rowBuffer[2*WIDTH] = 0;
            rowBuffer[2*WIDTH + 1] = 0;


            for (uint bit = 0; bit < DEPTH; ++bit){
                hub75_data_set_shift(pio, sm_data, data_prog_offs, bit*3);
                dma_channel_transfer_from_buffer_now(dma_chan,
                                                    &rowBuffer,
                                                    WIDTH*2+2);
                dma_channel_wait_for_finish_blocking(dma_chan);
                hub75_wait_tx_stall(pio, sm_data);
                hub75_wait_tx_stall(pio, sm_row);
                pio_sm_put_blocking(pio, sm_row, rowsel | ((1u * (1u << bit) - 1) << 5));
            }
                hub75_wait_tx_stall(pio, sm_data);
                hub75_wait_tx_stall(pio, sm_row);
        }
        }
        }else{ // フレームバッファ1
        for (int i = 0; i < 1; ++i){
        for (int rowsel = 0; rowsel < 16; ++rowsel){
            gpio_put_masked(mask_abc | mask_de, 0);
            gpio_put_masked(0b111 << 20, (rowsel & 0b00111) << 20);
            gpio_put_masked(0b11 << 26, (rowsel & 0b11000) >> 3 << 26);

            // 行バッファ準備
            for(uint x = 0; x < WIDTH; x++){
                rowBuffer[2*x] = LUT_R[frameBuffer_1[rowsel][x][0]] |
                                 LUT_G[frameBuffer_1[rowsel][x][1]] |
                                 LUT_B[frameBuffer_1[rowsel][x][2]];
                //rowBuffer[2*x] = 0xFFFFFF;
                rowBuffer[2*x+1] = LUT_R[frameBuffer_1[rowsel+HALF_HEIGHT][x][0]] |
                                   LUT_G[frameBuffer_1[rowsel+HALF_HEIGHT][x][1]] |
                                   LUT_B[frameBuffer_1[rowsel+HALF_HEIGHT][x][2]];
            }

            //CLK操作用のダミーピクセル  念のため0にしておく
            rowBuffer[2*WIDTH] = 0;
            rowBuffer[2*WIDTH + 1] = 0;


            for (uint bit = 0; bit < DEPTH; ++bit){
                hub75_data_set_shift(pio, sm_data, data_prog_offs, bit*3);
                dma_channel_transfer_from_buffer_now(dma_chan,
                                                    &rowBuffer,
                                                    WIDTH*2+2);
                dma_channel_wait_for_finish_blocking(dma_chan);
                hub75_wait_tx_stall(pio, sm_data);
                hub75_wait_tx_stall(pio, sm_row);
                pio_sm_put_blocking(pio, sm_row, rowsel | ((1u * (1u << bit) - 1) << 5));
            }
                hub75_wait_tx_stall(pio, sm_data);
                hub75_wait_tx_stall(pio, sm_row);
        }
        }
        }

        // 黒挿入
        /*
        for (int i=0; i<0;i++){
        for (int rowsel = 0; rowsel < 16; ++rowsel){

            // 列バッファ準備
            for(uint x = 0; x < WIDTH; x++){
                rowBuffer[2*x] = LUT_R[0] | LUT_G[0] | LUT_B[0];
                //rowBuffer[2*x] = 0xFFFFFF;
                rowBuffer[2*x+1] = LUT_R[0] | LUT_G[0] | LUT_B[0];
            }
            for (uint bit = 0; bit < DEPTH; ++bit){
                hub75_data_set_shift(pio, sm_data, data_prog_offs, bit*3);
                dma_channel_transfer_from_buffer_now(dma_chan,
                                                    &rowBuffer,
                                                    WIDTH*2);
                dma_channel_wait_for_finish_blocking(dma_chan);
                hub75_wait_tx_stall(pio, sm_data);
                hub75_wait_tx_stall(pio, sm_row);
                //pio_sm_put_blocking(pio, sm_row, rowsel | ((100u * (1u << bit) - 1) << 5));
            }
        }
        }*/
        count++;
        count = 0;
        if(false && count >= 100){
            tim_end = clock();
            double delta_t = (double)(tim_end - tim_start) / CLOCKS_PER_SEC;
            std::string strfps = std::to_string(delta_t);
            // printf("%s\n", strfps.c_str());
            // printf("%s\n", strfps.c_str());
            printf("%d\n", CLOCKS_PER_SEC);
            fps = (uint)(delta_t);
            count = 0;
            tim_start = clock();
        }
    }
}

void generate_pattern(uint8_t (&image)[HEIGHT][WIDTH][3],uint pattern=0){
    if(pattern == 0){
        for(int y = 0; y < 8; y++){
        for(int x = 0; x < WIDTH; x++){
            image[y][x][2] = x;
            image[y][x][1] = x;
            image[y][x][0] = x;
        }
        }
        for(int y = 24; y < 32; y++){
        for(int x = 0; x < WIDTH; x++){
            image[y][x][2] = 128-x;
            image[y][x][1] = 128-x;
            image[y][x][0] = 128-x;
        }
        }

    }
}




void write_to_buffer(uint8_t (&buffer)[HEIGHT][WIDTH][3], uint8_t (&image)[HEIGHT][WIDTH][3]){
    for(int y = 0; y < HEIGHT; y++){
        for(int x = 0; x < WIDTH; x++){
            buffer[y][x][0] = image[y][x][0];
            buffer[y][x][1] = image[y][x][1];
            buffer[y][x][2] = image[y][x][2];
        }
    }
}



void write_canvas_to_buffer(uint8_t (&image)[HEIGHT][WIDTH][3]){
    if(use_buffer_num == 0){
        write_to_buffer(frameBuffer_1, image);
        use_buffer_num = 1;
    }else{
        write_to_buffer(frameBuffer_0, image);
        use_buffer_num = 0;
    }
}



void write_to_canvas_vector(uint8_t (&canvas)[HEIGHT][WIDTH][3], const uint32_t* image){
    for(int y = 0; y < HEIGHT; y++){
        for(int x = 0; x < WIDTH; x++){
            canvas[y][x][0] = (image[y*WIDTH + x] >> 16) & 0xFF; //R
            canvas[y][x][1] = (image[y*WIDTH + x] >> 8) & 0xFF; //G
            canvas[y][x][2] = (image[y*WIDTH + x] >> 0) & 0xFF; //B
        }
    }
}
/*
void write_to_canvas565(uint32_t (&canvas)[HEIGHT][WIDTH], uint32_t (&image)[HEIGHT][WIDTH], int begin_x, int begin_y,  int size_x, int size_y){
    for(int y_rel = 0; y_rel < size_y; y_rel++){ // y_rel, x_rel: 画像左上基準の相対的な位置
        int y = begin_y + y_rel;
        if(y < 0) continue;
        if(y >= WIDTH) break;
        for(int x_rel = 0; x_rel < size_x; x_rel++){
            int x = begin_x + x_rel;
            if(x < 0) continue;
            if(x >= WIDTH) break;
            canvas[y][x] = image[y_rel][x_rel];
        }
    }
}*/

void write_to_canvas(uint8_t (&canvas)[HEIGHT][WIDTH][3], uint8_t (&image)[HEIGHT][WIDTH][3], int begin_x, int begin_y,  int size_x, int size_y){
    // imageの一部分をcanvasにコピー 範囲外にアクセスしないように行う
    for(int y_rel = 0; y_rel < size_y; y_rel++){ // y_rel, x_rel: 画像左上基準の相対的な位置
        int y = begin_y + y_rel;
        if(y < 0) continue;
        if(y >= WIDTH) break;
        for(int x_rel = 0; x_rel < size_x; x_rel++){
            int x = begin_x + x_rel;
            if(x < 0) continue;
            if(x >= WIDTH) break;
            canvas[y][x][0] = image[y_rel][x_rel][0];
            canvas[y][x][1] = image[y_rel][x_rel][1];
            canvas[y][x][2] = image[y_rel][x_rel][2];
        }
    }
}



// 文字描画関連
void write_str_to_canvas(uint8_t (&canvas)[HEIGHT][WIDTH][3], const uint8_t* font, int begin_x, int begin_y, std::string text, uint8_t color_r = 255, uint8_t color_g = 255, uint8_t color_b = 255){
    for(uint i = 0; i < text.length(); i++){
        char idx = text[i] - 32;
        // 1文字描画
        for(int y_delta = 0; y_delta < 12; y_delta++){
            int y = begin_y + y_delta;
            for(int x_delta = 0; x_delta < 6; x_delta++){
                int x = begin_x + 6*i + x_delta;
                if(((font[idx*12 + y_delta] >> (6 - x_delta)) & 0b1) == 0b1){
                    canvas[y][x][0] = color_r;
                    canvas[y][x][1] = color_g;
                    canvas[y][x][2] = color_b;
                }else{
                    canvas[y][x][0] = alpha_blend(0, canvas[y][x][0], 180);
                    canvas[y][x][1] = alpha_blend(0, canvas[y][x][1], 180);
                    canvas[y][x][2] = alpha_blend(0, canvas[y][x][2], 180);
                }
            }
        }
    }
}


int main() {
    // LUT初期化
    generate_lut(2.5);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    bool led_output = true;
    gpio_put(LED_PIN, led_output ? 1: 0);
    sleep_ms(1000);
    stdio_init_all();
    //set_sys_clock_khz(250000, true); //オーバークロックの際はRGB,CLKのトグルが遅くなるよう調整すること


    gpio_put(LED_PIN, led_output ? 1: 0);
    sleep_ms(1000);

    uint8_t canvas_blank[HEIGHT][WIDTH][3] = {0};

    uint8_t canvas_0[HEIGHT][WIDTH][3];
    memcpy(&canvas_0, &canvas_blank, sizeof(canvas_blank));



    //const uint32_t *img0 = (const uint32_t*)mountains_128x64;
    const uint32_t *img1 = (const uint32_t*)img_64x32;
    const uint32_t *img2 = (const uint32_t*)img_64x32;

    const uint8_t *font_6x12 = (const uint8_t*)shinonome6x12;
    // パーツを読み込み
    uint8_t objects_full[4][HEIGHT][WIDTH][3] = {0};
    /*
    for(int y = 0; y < HEIGHT; y++)for(int x = 0; x < WIDTH; x++){
        objects_full[0][y][x][0] = img1[(y*WIDTH + x)];
        objects_full[0][y][x][1] = img1[(y*WIDTH + x) + 1];
        objects_full[0][y][x][2] = img1[(y*WIDTH + x) + 2];
    }*/
    write_to_canvas_vector(objects_full[0], img1);
    write_to_canvas_vector(objects_full[1], img2);
    //write_to_canvas_vector(objects_full[1], img0);
    //generate_pattern(objects_full[1], 0);

    //write_to_buffer_vector(frameBuffer_0, img1);
    //write_to_buffer_vector(frameBuffer_1, img1);
    write_to_buffer(frameBuffer_1, objects_full[0]);

    // write_str_to_canvas(objects_full[1], font_6x12, 0, 0, "gamma:1");
    write_to_buffer(frameBuffer_0, objects_full[1]);

    //uint32_t pattern[HEIGHT][WIDTH];
    //memcpy(&pattern, &frame_rgb_blank, sizeof(frame_rgb_blank));
    //generate_pattern(pattern, 0);

    //generate_pattern(frameBuffer_0,0);
    //generate_pattern(frameBuffer_1,0);

    multicore_launch_core1(write_matrix);

    int x = WIDTH;
    while(1){
        DEPTH = 10;
        /*for(int i=2; i<11;i++){
            DEPTH = i;
            generate_lut(2.2);
            memcpy(&canvas_0, &canvas_blank, sizeof(canvas_blank));
            generate_pattern(canvas_0,0);
            std::string message = "GAMMA:" + std::to_string(i);
            write_str_to_canvas(canvas_0, font_6x12, 0, 10, message,255,255,255);
            write_canvas_to_buffer(canvas_0);
            sleep_ms(1000);
        }*/
        led_output = !led_output;
        gpio_put(LED_PIN, led_output ? 1: 0);
        write_canvas_to_buffer(objects_full[1]);
        //if(x == 0)break;
        sleep_ms(3000);
        for(int i=0; i<3;i++){
        memcpy(&canvas_0, &canvas_blank, sizeof(canvas_blank));

        write_to_canvas(canvas_0, objects_full[0], 0, 0, WIDTH, HEIGHT);
        write_canvas_to_buffer(canvas_0);
        sleep_ms(800);

        memcpy(&canvas_0, &canvas_blank, sizeof(canvas_blank));

        write_canvas_to_buffer(canvas_0);
        sleep_ms(300);
        }
        led_output = !led_output;
        gpio_put(LED_PIN, led_output ? 1: 0);
        memcpy(&canvas_0, &canvas_blank, sizeof(canvas_blank));

        write_to_canvas(canvas_0, objects_full[1], 0, 0, WIDTH, HEIGHT);
        write_canvas_to_buffer(canvas_0);
        x--;
        if(x < -WIDTH){
            x = WIDTH;
        }
        led_output = !led_output;
        gpio_put(LED_PIN, led_output ? 1: 0);
        //if(x == 0)break;
        sleep_ms(3000);
    }
    while(1){}
    gpio_put(LED_PIN, 0);
}
