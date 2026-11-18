#pragma once
#include <ESP32I2SAudio.h>

// Retain the BackgroundAudio TX task and add the paired RX channel to the
// same controller. Both directions share the 44.1 kHz stereo I2S clock.
class DuplexAudio : public ESP32I2SAudio {
public:
 int failureStage=0;esp_err_t failureCode=ESP_OK;
 using ESP32I2SAudio::ESP32I2SAudio;
 bool begin() override {
  if (_running) return false;
  i2s_chan_config_t cc=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO,I2S_ROLE_MASTER);
  cc.dma_desc_num=_buffers; cc.dma_frame_num=_bufferWords;
  failureStage=1;failureCode=i2s_new_channel(&cc,&_tx_handle,&rx);if(failureCode!=ESP_OK)return false;
  i2s_std_config_t config{};
  config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(_sampleRate);
  config.slot_cfg=I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk=_mclk<0?I2S_GPIO_UNUSED:(gpio_num_t)_mclk;
  config.gpio_cfg.bclk=(gpio_num_t)_bclk; config.gpio_cfg.ws=(gpio_num_t)_ws;
  config.gpio_cfg.dout=(gpio_num_t)_dout;config.gpio_cfg.din=GPIO_NUM_46;
  config.gpio_cfg.invert_flags.mclk_inv=_mclkInv;
  config.gpio_cfg.invert_flags.bclk_inv=_bclkInv;
  config.gpio_cfg.invert_flags.ws_inv=_wsInv;
  failureStage=2;failureCode=i2s_channel_init_std_mode(_tx_handle,&config);if(failureCode!=ESP_OK)return false;
  // The codec microphone uses Philips I2S (one-bit delay). The tested
  // BackgroundAudio speaker path uses MSB-aligned output. ESP32-S3 has
  // separate TX/RX slot configuration while sharing the clock.
  config.slot_cfg.bit_shift=true;
  failureStage=3;failureCode=i2s_channel_init_std_mode(rx,&config);if(failureCode!=ESP_OK)return false;
  i2s_chan_info_t info{};failureStage=4;failureCode=i2s_channel_get_info(_tx_handle,&info);if(failureCode!=ESP_OK)return false;
  if(info.total_dma_buf_size!=_buffers*_bufferWords*4){failureCode=ESP_ERR_INVALID_SIZE;return false;}
  _totalAvailable=info.total_dma_buf_size;
  int16_t zero[2]={};size_t written=0;
  do {i2s_channel_preload_data(_tx_handle,zero,sizeof(zero),&written);} while(written);
  i2s_event_callbacks_t callbacks{};callbacks.on_sent=_onSent;
  failureStage=5;failureCode=i2s_channel_register_event_callback(_tx_handle,&callbacks,this);if(failureCode!=ESP_OK)return false;
  failureStage=6;if(xTaskCreate(_taskShim,"BackgroundAudioI2S",8192,this,2,&_taskHandle)!=pdPASS){failureCode=ESP_ERR_NO_MEM;return false;}
  failureStage=7;failureCode=i2s_channel_enable(_tx_handle);_running=failureCode==ESP_OK;
  if(_running)failureStage=0;
  return _running;
 }
 bool beginCapture(){return _running&&rx&&i2s_channel_enable(rx)==ESP_OK;}
 bool active() const {return _running;}
 esp_err_t readCapture(void *dst,size_t capacity,size_t &count,unsigned timeoutMs){
  return i2s_channel_read(rx,dst,capacity,&count,timeoutMs);
 }
 void endCapture(){if(rx)i2s_channel_disable(rx);}
private:
 i2s_chan_handle_t rx=nullptr;
};
