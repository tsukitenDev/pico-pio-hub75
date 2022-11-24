#ifndef HUB75_H_
#define HUB_H_

void generate_lut(double gamma=2.2);

void hub75_init(uint32_t width, uint32_t height, uint8_t frameBuffer0[], uint8_t frameBuffer1[]);
void hub75_set_buffer_num(uint buffer_num);
void hub75_set_depth(uint8_t depth);
void hub75_set_gamma(double gamma);
void write_matrix();


#endif  // HUB75_H_