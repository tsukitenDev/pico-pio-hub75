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

#define MIN_DEPTH 1
#define MAX_DEPTH 10


namespace{
    uint8_t DEPTH = 10;
    double GAMMA = 2.2;
    uint32_t LUT_R[256] = {0};
    uint32_t LUT_G[256] = {0};
    uint32_t LUT_B[256] = {0};

    uint32_t WIDTH = 64;
    uint32_t HEIGHT = 32;

    uint use_buffer_num = 0;
    uint8_t* frameBuffer_0 = nullptr;
    uint8_t* frameBuffer_1 = nullptr;
}


// LUT生成
void generate_lut(){
    for(int bright = 0; bright < 256; bright++){
        uint32_t pwm_v = (uint32_t)(pow(bright / 255.0, GAMMA)*((1 << DEPTH) - 1));
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

void hub75_init(uint32_t width, uint32_t height, uint8_t* frameBuffer0, uint8_t* frameBuffer1){
    WIDTH = width;
    HEIGHT = height;
    frameBuffer_0 = frameBuffer0;
    frameBuffer_1 = frameBuffer1;

    generate_lut();
}

void hub75_set_buffer_num(uint buffer_num){
    if(buffer_num == 0 || buffer_num == 1){
        use_buffer_num = buffer_num;
    }
}

void hub75_set_depth(uint8_t depth){
    if((MIN_DEPTH <= depth) && (depth <= MAX_DEPTH)){
        DEPTH = depth;
        generate_lut();
    }else{

    }
}

void hub75_set_gamma(double gamma){
    GAMMA = gamma;
    generate_lut();
}



void write_matrix(){
    // PIO ----------------------------
    PIO pio = pio0;
    //PIO pio2 = pio1;
    uint sm_data = 0; //ステートマシンの指定
    uint sm_row = 1;
    uint data_prog_offset = pio_add_program(pio, &hub75_data_program);
    uint row_prog_offset = pio_add_program(pio, &hub75_row_program);
    hub75_data_program_init(pio, sm_data, data_prog_offset, DATA_BASE_PIN, CLK_PIN);
    hub75_row_program_init(pio, sm_row, row_prog_offset, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN);


    const uint32_t HALF_HEIGHT = HEIGHT / 2;
    uint fps = 0; //frame per second
    uint count = 0;
    //std::chrono::system_clock::time_point tim_start, tim_end;
    //clock_t tim_start, tim_end;

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


    // HUB75 Output -------------------
    //tim_start = clock();
    //printf("%d", CLOCKS_PER_SEC);

    uint8_t* ptr_frameBuffer = nullptr;
    uint32_t* rowBuffer = new uint32_t[WIDTH*2 + 2](); // +2はCLK操作用のダミー
    if(rowBuffer == nullptr){
        while(1){}
    }
    while(1){
        if(use_buffer_num == 0){
            ptr_frameBuffer = frameBuffer_0;
        }else{
            ptr_frameBuffer = frameBuffer_1;
        }

        for (int i = 0; i < 1; ++i){
        for (int rowsel = 0; rowsel < 16; ++rowsel){
            gpio_put_masked(mask_abc | mask_de, 0);
            gpio_put_masked(0b111 << 20, (rowsel & 0b00111) << 20);
            gpio_put_masked(0b11 << 26, (rowsel & 0b11000) >> 3 << 26);

            // 行バッファ準備
            for(uint x = 0; x < WIDTH; x++){
                rowBuffer[2*x] = LUT_R[ptr_frameBuffer[rowsel*WIDTH*3 + 3*x + 0]] |
                                 LUT_G[ptr_frameBuffer[rowsel*WIDTH*3 + 3*x + 1]] |
                                 LUT_B[ptr_frameBuffer[rowsel*WIDTH*3 + 3*x + 2]];
                //rowBuffer[2*x] = 0xFFFFFF;
                rowBuffer[2*x+1] = LUT_R[ptr_frameBuffer[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 0]] |
                                   LUT_G[ptr_frameBuffer[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 1]] |
                                   LUT_B[ptr_frameBuffer[(rowsel+HALF_HEIGHT)*WIDTH*3 + 3*x + 2]];
            }

            //CLK操作用のダミーピクセル  念のため0にしておく
            rowBuffer[2*WIDTH] = 0;
            rowBuffer[2*WIDTH + 1] = 0;


            for (uint bit = 0; bit < DEPTH; ++bit){
                hub75_data_set_shift(pio, sm_data, data_prog_offset, bit*3);
                dma_channel_transfer_from_buffer_now(dma_chan,
                                                    rowBuffer,
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
    delete[] rowBuffer;
}