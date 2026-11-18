#pragma once
#include "Arduino.h"
using i2s_chan_handle_t = void*;
struct i2s_chan_config_t { int id, role, dma_desc_num, dma_frame_num; bool auto_clear=false; };
struct i2s_std_config_t {
    struct Clock { int rate; } clk_cfg;
    struct Slot { int width, mode, slot_mask; } slot_cfg;
    struct GPIO { int mclk,bclk,ws,dout,din; } gpio_cfg;
};
inline i2s_std_config_t fakeI2SConfig{};
inline i2s_chan_config_t fakeI2SChannel{};
inline int i2s_new_channel(const i2s_chan_config_t *c, void **tx, void *) { fakeI2SChannel=*c; *tx=(void*)1; return 0; }
inline int i2s_channel_init_std_mode(void *, const i2s_std_config_t *c) { fakeI2SConfig=*c; return 0; }
inline int i2s_channel_enable(void *) { return 0; }
inline void (*fakeOnAudioWrite)(const int16_t*,size_t)=nullptr;
inline int i2s_channel_write(void *, const void *p, size_t n, size_t *written, unsigned) {if(fakeOnAudioWrite)fakeOnAudioWrite(static_cast<const int16_t*>(p),n/2);*written=n; return 0; }
constexpr int I2S_NUM_1=1, I2S_ROLE_MASTER=1, I2S_DATA_BIT_WIDTH_16BIT=16, I2S_SLOT_MODE_MONO=1;
constexpr int I2S_STD_SLOT_BOTH=3, I2S_GPIO_UNUSED=-1, GPIO_NUM_41=41, GPIO_NUM_42=42, GPIO_NUM_43=43;
#ifndef FAKE_ESP_OK_DEFINED
#define FAKE_ESP_OK_DEFINED
constexpr int ESP_OK=0;
#endif
#define I2S_CHANNEL_DEFAULT_CONFIG(a,b) {a,b,6,256}
#define I2S_STD_CLK_DEFAULT_CONFIG(a) {a}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(a,b) {a,b,1}
