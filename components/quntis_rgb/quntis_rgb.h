#pragma once

#include <array>
#include <deque>
#include <vector>

#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

class RF24;

namespace esphome {
namespace quntis_rgb {

// Command byte (payload[4]) as sent by the Quntis curved RGB remote.
enum Command : uint8_t {
  CMD_POWER = 0x31,
  CMD_BACKLIGHT = 0x32,
  CMD_BRIGHTER = 0x35,
  CMD_DIMMER = 0x36,
  CMD_COOLER = 0x39,
  CMD_WARMER = 0x3A,
  CMD_AUTO_OFF = 0x3E,
  CMD_AUTO_ON = 0x40,
};

// Full bar state; every packet carries all of it.
struct BarState {
  uint8_t brightness{0};  // 0 = main light off, 1..255 stepless
  uint8_t cct{0x80};      // 0 = warmest, 255 = coolest
  bool backlight{false};
  uint8_t mode{1};  // RGB backlight mode 1..15
};

class QuntisRgbLight;
class QuntisBacklightSelect;
class QuntisAutoSwitch;

class QuntisRgb : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_pins(uint8_t ce, uint8_t cs, uint8_t clk, uint8_t mosi, uint8_t miso) {
    ce_pin_ = ce;
    cs_pin_ = cs;
    clk_pin_ = clk;
    mosi_pin_ = mosi;
    miso_pin_ = miso;
  }
  void set_address(const std::vector<uint8_t> &a) { std::copy(a.begin(), a.end(), address_.begin()); }
  void set_remote_id(const std::vector<uint8_t> &id) { std::copy(id.begin(), id.end(), remote_id_.begin()); }
  void set_channels(const std::vector<int> &c) { channels_.assign(c.begin(), c.end()); }
  void set_rx_channel(uint8_t c) { rx_channel_ = c; }
  void set_tx_repeats(uint8_t r) { tx_repeats_ = r; }
  void set_light(QuntisRgbLight *l) { light_ = l; }
  void set_backlight_select(QuntisBacklightSelect *s) { backlight_select_ = s; }
  void set_auto_switch(QuntisAutoSwitch *s) { auto_switch_ = s; }

  BarState &state() { return state_; }
  // Queue a packet carrying the current state with the given command.
  void send(uint8_t cmd);
  // Writes during boot come from state restoration; they update state but are not transmitted.
  bool booting() const { return millis() - boot_ms_ < 3000; }

 protected:
  void build_frame_(uint8_t cmd, std::array<uint8_t, 18> &raw);
  bool decode_frame_(const uint8_t *raw, uint8_t *payload);
  void start_rx_();
  void handle_remote_(const uint8_t *payload);
  void publish_state_();

  RF24 *radio_{nullptr};
  uint8_t ce_pin_, cs_pin_, clk_pin_, mosi_pin_, miso_pin_;
  std::array<uint8_t, 5> address_{};
  std::array<uint8_t, 4> remote_id_{};
  std::vector<uint8_t> channels_;
  uint8_t rx_channel_{53};
  uint8_t tx_repeats_{40};

  BarState state_;
  uint8_t counter_{0};
  uint32_t boot_ms_{0};

  std::deque<std::array<uint8_t, 18>> queue_;
  int cycles_left_{0};
  bool rx_active_{false};

  uint16_t last_remote_key_{0xFFFF};
  uint32_t last_remote_ms_{0};

  QuntisRgbLight *light_{nullptr};
  QuntisBacklightSelect *backlight_select_{nullptr};
  QuntisAutoSwitch *auto_switch_{nullptr};
};

class QuntisRgbLight : public light::LightOutput, public Parented<QuntisRgb> {
 public:
  void set_mireds(float cold, float warm) {
    cold_mireds_ = cold;
    warm_mireds_ = warm;
  }
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { state_ = state; }
  void write_state(light::LightState *state) override;
  // Reflect a state change made by the physical remote.
  void publish_from_bar(const BarState &s);

 protected:
  light::LightState *state_{nullptr};
  float cold_mireds_{153.8f};
  float warm_mireds_{333.3f};
};

class QuntisBacklightSelect : public select::Select, public Parented<QuntisRgb> {
 public:
  void publish_from_bar(const BarState &s) { this->publish_state(s.backlight ? s.mode : 0); }

 protected:
  void control(size_t index) override;
};

class QuntisAutoSwitch : public switch_::Switch, public Parented<QuntisRgb> {
 protected:
  void write_state(bool state) override;
};

}  // namespace quntis_rgb
}  // namespace esphome
