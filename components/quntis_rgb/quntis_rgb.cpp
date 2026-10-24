#include "quntis_rgb.h"

#include <RF24.h>
#include <SPI.h>

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace quntis_rgb {

static const char *const TAG = "quntis_rgb";

// XN297 emulation: the nRF24 matches/sends the XN297 preamble tail as its own 3-byte address,
// and the scrambled XN297 address, payload and CRC travel as the nRF24 payload.
static const uint8_t XN297_PREAMBLE[3] = {0x55, 0x0F, 0x71};
static const uint8_t XN297_SCRAMBLE[16] = {0xe3, 0xb1, 0x4b, 0xea, 0x85, 0xbc, 0xe5, 0x66,
                                           0x0d, 0xae, 0x8c, 0x88, 0x12, 0x69, 0xee, 0x1f};
static const uint16_t XN297_CRC_XOROUT = 0xF494;  // measured for 5-byte address + 11-byte payload
static const uint8_t FRAME_LEN = 18;
static const uint8_t PAYLOAD_LEN = 11;
static const int CYCLES_PER_LOOP = 5;

static uint8_t bit_reverse(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}

static uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xB5D2;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i] << 8;
    for (int k = 0; k < 8; k++)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

void QuntisRgb::setup() {
  boot_ms_ = millis();
  counter_ = random_uint32() & 0xFF;
  mode_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("quntis_rgb_mode"));
  uint8_t mode;
  if (mode_pref_.load(&mode) && mode >= 1 && mode <= 15)
    state_.mode = mode;
  saved_mode_ = state_.mode;

  SPI.begin(clk_pin_, miso_pin_, mosi_pin_, cs_pin_);
  radio_ = new RF24(ce_pin_, cs_pin_);
  if (!radio_->begin(&SPI, ce_pin_, cs_pin_)) {
    ESP_LOGE(TAG, "nRF24L01 not found, check wiring");
    this->mark_failed();
    return;
  }
  radio_->setAutoAck(false);
  radio_->disableCRC();
  radio_->disableDynamicPayloads();
  radio_->setAddressWidth(3);
  radio_->setPayloadSize(FRAME_LEN);
  radio_->setDataRate(RF24_1MBPS);
  radio_->setRetries(0, 0);
  radio_->setPALevel(RF24_PA_LOW);
  radio_->openWritingPipe(XN297_PREAMBLE);
  radio_->openReadingPipe(0, XN297_PREAMBLE);
  radio_->closeReadingPipe(1);
  start_rx_();
}

void QuntisRgb::dump_config() {
  ESP_LOGCONFIG(TAG, "Quntis RGB light bar:");
  ESP_LOGCONFIG(TAG, "  Pins: CE=%u CS=%u CLK=%u MOSI=%u MISO=%u", ce_pin_, cs_pin_, clk_pin_, mosi_pin_, miso_pin_);
  ESP_LOGCONFIG(TAG, "  Address: %02X %02X %02X %02X %02X", address_[0], address_[1], address_[2], address_[3],
                address_[4]);
  ESP_LOGCONFIG(TAG, "  Remote ID: %02X %02X %02X %02X", remote_id_[0], remote_id_[1], remote_id_[2], remote_id_[3]);
  ESP_LOGCONFIG(TAG, "  RX channel: %u, TX repeats: %u", rx_channel_, tx_repeats_);
  if (this->is_failed())
    ESP_LOGE(TAG, "  Radio initialisation failed");
}

void QuntisRgb::build_frame_(uint8_t cmd, std::array<uint8_t, 18> &raw) {
  uint8_t p[PAYLOAD_LEN] = {remote_id_[0],
                            remote_id_[1],
                            remote_id_[2],
                            remote_id_[3],
                            cmd,
                            state_.brightness,
                            state_.cct,
                            uint8_t(state_.backlight ? 0xFF : 0x00),
                            uint8_t(0x80 | state_.mode),
                            ++counter_,
                            0};
  for (int i = 0; i < 10; i++)
    p[10] += p[i];
  for (int i = 0; i < 5; i++)
    raw[i] = address_[4 - i] ^ XN297_SCRAMBLE[i];
  for (int i = 0; i < PAYLOAD_LEN; i++)
    raw[5 + i] = bit_reverse(p[i]) ^ XN297_SCRAMBLE[5 + i];
  uint16_t crc = crc16(raw.data(), 16) ^ XN297_CRC_XOROUT;
  raw[16] = crc >> 8;
  raw[17] = crc & 0xFF;
}

bool QuntisRgb::decode_frame_(const uint8_t *raw, uint8_t *payload) {
  uint16_t crc = crc16(raw, 16) ^ XN297_CRC_XOROUT;
  if (raw[16] != (crc >> 8) || raw[17] != (crc & 0xFF))
    return false;
  for (int i = 0; i < 5; i++)
    if ((raw[i] ^ XN297_SCRAMBLE[i]) != address_[4 - i])
      return false;
  uint8_t sum = 0;
  for (int i = 0; i < PAYLOAD_LEN; i++) {
    payload[i] = bit_reverse(raw[5 + i] ^ XN297_SCRAMBLE[5 + i]);
    if (i < 10)
      sum += payload[i];
  }
  if (sum != payload[10])
    return false;
  return std::equal(remote_id_.begin(), remote_id_.end(), payload);
}

void QuntisRgb::send(uint8_t cmd) {
  if (this->is_failed())
    return;
  std::array<uint8_t, 18> raw;
  build_frame_(cmd, raw);
  ESP_LOGD(TAG, "TX cmd=%02X brightness=%02X cct=%02X backlight=%d mode=%u", cmd, state_.brightness, state_.cct,
           state_.backlight, state_.mode);
  queue_.push_back(raw);
}

void QuntisRgb::start_rx_() {
  radio_->setChannel(rx_channel_);
  radio_->startListening();
  rx_active_ = true;
}

void QuntisRgb::loop() {
  if (this->is_failed())
    return;

  if (!queue_.empty()) {
    if (rx_active_) {
      radio_->stopListening();
      rx_active_ = false;
    }
    if (cycles_left_ == 0)
      cycles_left_ = tx_repeats_;
    auto &raw = queue_.front();
    for (int k = 0; k < CYCLES_PER_LOOP && cycles_left_ > 0; k++, cycles_left_--) {
      for (uint8_t ch : channels_) {
        radio_->setChannel(ch);
        radio_->write(raw.data(), FRAME_LEN);
      }
    }
    if (cycles_left_ == 0)
      queue_.pop_front();
    if (queue_.empty())
      start_rx_();
    return;
  }

  uint8_t raw[FRAME_LEN], payload[PAYLOAD_LEN];
  while (radio_->available()) {
    radio_->read(raw, FRAME_LEN);
    if (decode_frame_(raw, payload))
      handle_remote_(payload);
  }
}

void QuntisRgb::handle_remote_(const uint8_t *p) {
  // Each press arrives as a burst of identical packets; act on the first one.
  uint16_t key = p[9] << 8 | p[4];
  if (key == last_remote_key_ && millis() - last_remote_ms_ < 2000)
    return;
  last_remote_key_ = key;
  last_remote_ms_ = millis();

  ESP_LOGD(TAG, "Remote cmd=%02X brightness=%02X cct=%02X backlight=%02X mode=%02X", p[4], p[5], p[6], p[7], p[8]);
  state_.brightness = p[5];
  state_.cct = p[6];
  state_.backlight = p[7] != 0;
  state_.mode = std::max(1, std::min(15, p[8] & 0x7F));
  save_mode();
  counter_ = std::max(counter_, p[9]);

  if (auto_switch_ != nullptr && (p[4] == CMD_AUTO_ON || p[4] == CMD_AUTO_OFF))
    auto_switch_->publish_state(p[4] == CMD_AUTO_ON);
  publish_state_();
}

void QuntisRgb::publish_state_() {
  if (light_ != nullptr)
    light_->publish_from_bar(state_);
  publish_backlight_select();
  publish_backlight_light();
}

void QuntisRgb::save_mode() {
  if (state_.mode == saved_mode_)
    return;
  saved_mode_ = state_.mode;
  mode_pref_.save(&saved_mode_);
}

void QuntisRgb::publish_backlight_select() {
  if (backlight_select_ != nullptr)
    backlight_select_->publish_from_bar(state_);
}

void QuntisRgb::publish_backlight_light() {
  if (backlight_light_ != nullptr)
    backlight_light_->publish_from_bar(state_);
}

light::LightTraits QuntisRgbLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(cold_mireds_);
  traits.set_max_mireds(warm_mireds_);
  return traits;
}

void QuntisRgbLight::write_state(light::LightState *state) {
  auto &v = state->current_values;
  uint8_t brightness = v.is_on() ? std::max(1L, std::lround(v.get_brightness() * 255.0f)) : 0;
  float mireds = clamp(v.get_color_temperature(), cold_mireds_, warm_mireds_);
  uint8_t cct = std::lround((warm_mireds_ - mireds) / (warm_mireds_ - cold_mireds_) * 255.0f);

  auto &s = parent_->state();
  if (brightness == s.brightness && cct == s.cct)
    return;  // already the bar's state (e.g. echo of a remote press)
  uint8_t old_brightness = s.brightness, old_cct = s.cct;
  s.brightness = brightness;
  s.cct = cct;
  if (parent_->booting())
    return;

  if ((old_brightness == 0) != (brightness == 0)) {
    parent_->send(CMD_POWER);
    return;
  }
  if (brightness != old_brightness)
    parent_->send(brightness > old_brightness ? CMD_BRIGHTER : CMD_DIMMER);
  if (cct != old_cct)
    parent_->send(cct > old_cct ? CMD_COOLER : CMD_WARMER);
}

void QuntisRgbLight::publish_from_bar(const BarState &s) {
  if (state_ == nullptr)
    return;
  auto call = state_->make_call();
  call.set_transition_length(0);
  call.set_state(s.brightness != 0);
  if (s.brightness != 0)
    call.set_brightness(s.brightness / 255.0f);
  call.set_color_temperature(warm_mireds_ - s.cct / 255.0f * (warm_mireds_ - cold_mireds_));
  call.perform();
}

light::LightTraits QuntisBacklightLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  return traits;
}

void QuntisBacklightLight::write_state(light::LightState *state) {
  bool on;
  state->current_values_as_binary(&on);
  auto &s = parent_->state();
  if (on == s.backlight)
    return;  // already the bar's state (e.g. echo of a remote press or select change)
  s.backlight = on;
  if (!parent_->booting())
    parent_->send(CMD_BACKLIGHT);
  parent_->publish_backlight_select();
}

void QuntisBacklightLight::publish_from_bar(const BarState &s) {
  if (state_ == nullptr || state_->remote_values.is_on() == s.backlight)
    return;
  auto call = state_->make_call();
  call.set_state(s.backlight);
  call.perform();
}

void QuntisBacklightSelect::control(size_t index) {
  auto &s = parent_->state();
  s.backlight = index > 0;
  if (index > 0) {
    s.mode = index;
    parent_->save_mode();
  }
  if (!parent_->booting())
    parent_->send(CMD_BACKLIGHT);
  this->publish_state(index);
  parent_->publish_backlight_light();
}

void QuntisAutoSwitch::write_state(bool state) {
  if (!parent_->booting())
    parent_->send(state ? CMD_AUTO_ON : CMD_AUTO_OFF);
  this->publish_state(state);
}

}  // namespace quntis_rgb
}  // namespace esphome
