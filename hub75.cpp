#include <cmath>
#include <iostream>



#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "hub75.pio.h"

#define DATA_BASE_PIN 0
#define DATA_N_PINS 6
#define ROWSEL_BASE_PIN 20
#define ROWSEL_N_PINS 5
#define CLK_PIN 6
#define STROBE_PIN 14
#define OEN_PIN 15




#define DEPTH 10


#define HEIGHT 32
#define WIDTH 64

namespace{
    uint32_t LUT_R[256] = {0};
    uint32_t LUT_G[256] = {0};
    uint32_t LUT_B[256] = {0};
}

extern uint use_buffer_num;
extern uint8_t frameBuffer_0[];
extern uint8_t frameBuffer_1[];

const uint8_t* frameBuffer0 = frameBuffer_0;
const uint8_t* frameBuffer1 = frameBuffer_1;

// LUT生成
void generate_lut(double gamma=2.2){
    for(int bright = 0; bright < 256; bright++){
        uint32_t pwm_v = (uint32_t)(pow(bright / 255.0, gamma)*((1 << DEPTH) - 1));
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



void write_matrix(){
    PIO pio = pio0;
    //PIO pio2 = pio1;
    uint sm_data = 0;
    uint sm_row = 1;
    const uint32_t HALF_HEIGHT = HEIGHT / 2;
    int count = 0;
    //std::chrono::system_clock::time_point tim_start, tim_end;
    //clock_t tim_start, tim_end;
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
    //tim_start = clock();
    //printf("%d", CLOCKS_PER_SEC);

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
                rowBuffer[2*x] = LUT_R[frameBuffer0[rowsel*WIDTH*3 + 3*x + 0]] |
                                 LUT_G[frameBuffer0[rowsel*WIDTH*3 + 3*x + 1]] |
                                 LUT_B[frameBuffer0[rowsel*WIDTH*3 + 3*x + 2]];
                //rowBuffer[2*x] = 0xFFFFFF;
                rowBuffer[2*x+1] = LUT_R[frameBuffer0[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 0]] |
                                   LUT_G[frameBuffer0[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 1]] |
                                   LUT_B[frameBuffer0[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 2]];
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
                rowBuffer[2*x] = LUT_R[frameBuffer1[rowsel*WIDTH*3 + 3*x + 0]] |
                                 LUT_G[frameBuffer1[rowsel*WIDTH*3 + 3*x + 1]] |
                                 LUT_B[frameBuffer1[rowsel*WIDTH*3 + 3*x + 2]];
                //rowBuffer[2*x] = 0xFFFFFF;
                rowBuffer[2*x+1] = LUT_R[frameBuffer1[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 0]] |
                                   LUT_G[frameBuffer1[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 1]] |
                                   LUT_B[frameBuffer1[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 2]];
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
            //tim_end = clock();
            //double delta_t = (double)(tim_end - tim_start) / CLOCKS_PER_SEC;
            //std::string strfps = std::to_string(delta_t);
            // printf("%s\n", strfps.c_str());
            // printf("%s\n", strfps.c_str());
            //printf("%d\n", CLOCKS_PER_SEC);
            //fps = (uint)(delta_t);
            //count = 0;
            //tim_start = clock();
        }
    }
}