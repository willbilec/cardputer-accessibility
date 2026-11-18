// SDK-only control: no speech, codec, keyboard, board glue or display library.
#include <Arduino.h>
#include <driver/i2s_std.h>
void setup(){
 Serial.begin(115200);Serial.setTimeout(100);Serial.println("I2S_CONTROL_CONFIG");delay(200);
 int port=Serial.available()?Serial.readStringUntil('\n').toInt():1;
 if(port<0||port>1)return;
 i2s_chan_handle_t tx=nullptr;
 i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(i2s_port_t(port),I2S_ROLE_MASTER);
 channel.dma_desc_num=6;channel.dma_frame_num=256;
 Serial.printf("I2S_CONTROL port=%d new=%d\n",port,i2s_new_channel(&channel,&tx,nullptr));
 i2s_std_config_t config{};
 config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(44100);
 config.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO);
 config.slot_cfg.slot_mask=I2S_STD_SLOT_BOTH;
 config.gpio_cfg.mclk=I2S_GPIO_UNUSED;config.gpio_cfg.bclk=GPIO_NUM_41;
 config.gpio_cfg.ws=GPIO_NUM_43;config.gpio_cfg.dout=GPIO_NUM_42;config.gpio_cfg.din=I2S_GPIO_UNUSED;
 Serial.printf("I2S_CONTROL std=%d\n",i2s_channel_init_std_mode(tx,&config));
 Serial.println("I2S_CONTROL before_enable");
 int result=i2s_channel_enable(tx);
 Serial.printf("I2S_CONTROL enable=%d\n",result);
 Serial.println("I2S_CONTROL_COMPLETE");
}
void loop(){delay(10);}
