#include "tm1651.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tm1651 {

static const char *const TAG = "tm1651.display";

static const bool LINE_HIGH = true;
static const bool LINE_LOW = false;

// TM1651 maximum frequency is 500 kHz (duty ratio 50%) = 2 microseconds / cycle
// choose appropriate clock cycle in microseconds
static const uint8_t CLOCK_CYCLE = 48;

static const uint8_t HALF_CLOCK_CYCLE = CLOCK_CYCLE / 2;
static const uint8_t QUARTER_CLOCK_CYCLE = CLOCK_CYCLE / 4;

static const uint8_t ADDR_FIXED = 0x44; // fixed address mode
static const uint8_t ADDR_START = 0xC0; // address of the display register

static const uint8_t DISPLAY_OFF = 0x80;
static const uint8_t DISPLAY_ON = 0x88;

static const uint8_t MAX_DISPLAY_LEVELS = 7;

static const uint8_t PERCENT100 = 100;
static const uint8_t PERCENT50 = 50;

static const uint8_t TM1651_BRIGHTNESS_DARKEST = 0;
static const uint8_t TM1651_BRIGHTNESS_TYPICAL = 2;
static const uint8_t TM1651_BRIGHTNESS_BRIGHTEST = 7;

static const uint8_t TM1651_LEVEL_TAB[]          = { 0b00000000, 0b00000001, 0b00000011, 0b00000111,
                                                     0b00001111, 0b00011111, 0b00111111, 0b01111111 };

// public

void TM1651Display::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  this->clk_pin_->setup();
  this->clk_pin_->pin_mode(gpio::FLAG_OUTPUT);

  this->dio_pin_->setup();
  this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);

  // initialise brightness to TYPICAL
  this->brightness_ = TM1651_BRIGHTNESS_TYPICAL;

  // initialised already
  // display_on_ = true
  // level_ = 0

  // clear display
  this->display_level_();
  this->update_brightness_(DISPLAY_ON);
}

void TM1651Display::dump_config() {
  ESP_LOGCONFIG(TAG, "TM1651 Battery Display");
  LOG_PIN("  CLK: ", clk_pin_);
  LOG_PIN("  DIO: ", dio_pin_);
}

void TM1651Display::set_brightness(uint8_t new_brightness) {
  this->brightness_ = this->remap_brightness_(new_brightness);
  if (this->display_on_) {
    // add for testing
    this->display_level_();
    this->update_brightness_(DISPLAY_ON);
  }
}

void TM1651Display::set_level(uint8_t new_level) {
  if (new_level > MAX_DISPLAY_LEVELS) new_level = MAX_DISPLAY_LEVELS;
  this->level_ = new_level;
  if (this->display_on_) this->display_level_();
}

void TM1651Display::set_level_percent(uint8_t percentage) {
  this->level_ = this->calculate_level_(percentage);
  if (this->display_on_) this->display_level_();
}

void TM1651Display::turn_off() {
  this->display_on_ = false;
  this->update_brightness_(DISPLAY_OFF);
}

void TM1651Display::turn_on() {
  this->display_on_ = true;
  // display level as level could have been changed when display turned off
  this->display_level_();
  this->update_brightness_(DISPLAY_ON);
}

// protected

uint8_t TM1651Display::remap_brightness_(uint8_t new_brightness) {
  if (new_brightness <= 1) return TM1651_BRIGHTNESS_DARKEST;
  if (new_brightness == 2) return TM1651_BRIGHTNESS_TYPICAL;

  // new_brightness >= 3
  return TM1651_BRIGHTNESS_BRIGHTEST;
}

uint8_t TM1651Display::calculate_level_(uint8_t percentage) {
  if (percentage > PERCENT100) percentage = PERCENT100;
  // scale 0-100% to 0-7 display levels
  // use integer arithmetic
  // round before division by 100 percent
  uint16_t initial_scaling = (percentage * MAX_DISPLAY_LEVELS) + PERCENT50;
  return (uint8_t)(initial_scaling / PERCENT100);
}

void TM1651Display::display_level_() {
  bool ok;

  this->start_();
  ok = this->write_byte_(ADDR_FIXED);
  this->stop_();
  if (!ok) ESP_LOGD("", "error: addr fixed");

  this->start_();
  ok = this->write_byte_(ADDR_START);
  if (!ok) ESP_LOGD("", "error: addr start");
  ok = this->write_byte_(TM1651_LEVEL_TAB[this->level_]);
  if (!ok) ESP_LOGD("", "error: level");
  this->stop_();
}


void TM1651Display::update_brightness_(uint8_t on_off_control) {
  this->start_();
  bool ok = this->write_byte_(on_off_control | this->brightness_);
  if (!ok) ESP_LOGD("", "error: brightness");
  this->stop_();
}

// low level functions

void TM1651Display::delineate_transmission_(bool dio_state) {
  // delineate data transmission
  // DIO changes its value while CLK is high
  // used by start and stop

  this->dio_pin_->digital_write(dio_state);
  delayMicroseconds(HALF_CLOCK_CYCLE);

  this->clk_pin_->digital_write(LINE_HIGH);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);

  this->dio_pin_->digital_write(!dio_state);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);
}

void TM1651Display::half_cycle_clock_high_() {
  // start second half cycle
  this->clk_pin_->digital_write(LINE_HIGH);
  delayMicroseconds(HALF_CLOCK_CYCLE);
}

// bool TM1651Display::half_cycle_clock_high_ack_() {
//   // start second half cycle when clock is high and check for ack
//   // returns ack if received = false, since ack is DIO low

//   this->clk_pin_->digital_write(LINE_HIGH);
//   delayMicroseconds(QUARTER_CLOCK_CYCLE);

//   this->dio_pin_->pin_mode(gpio::FLAG_INPUT);
//   bool ack = this->dio_pin_->digital_read();

//   this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);

//   // ack should be set DIO low by now
//   // its not so set DIO low before the next cycle
//   if (!ack) {
//     this->dio_pin_->digital_write(LINE_LOW);
//     this->error_count_ = this->error_count_ + 1
//   }

//   delayMicroseconds(QUARTER_CLOCK_CYCLE);
//   // begin next cycle
//   this->clk_pin_->digital_write(LINE_LOW);

//   return ack;
// }

void TM1651Display::half_cycle_clock_low_(bool data_bit) {
   // start first half cycle when CLK low and write data bit
  this->clk_pin_->digital_write(LINE_LOW);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);

  this->dio_pin_->digital_write(data_bit);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);
}

void TM1651Display::start_() {
  // start data transmission
  this->delineate_transmission_(LINE_HIGH);
}

void TM1651Display::stop_() {
  // stop data transmission
  this->delineate_transmission_(LINE_LOW);
}
void TM1651Display::reset_errors() {
  this->error_count_= 0;
}

bool TM1651Display::write_byte_(uint8_t data) {
  // returns true if ack sent after write

  // send 8 data bits
  // data bit can only be written to DIO when CLK is low
  for (uint8_t i = 0; i < 8; i++) {
    this->half_cycle_clock_low_((bool)(data & 0x01));
    this->half_cycle_clock_high_();
    // next bit
    data >>= 1;
  }

  // during the 9th cycle
  // DIO set high, should get ack by DIO low
  this->half_cycle_clock_low_(LINE_HIGH);
  bool ok = (!this->half_cycle_clock_high_ack_());
  if (!ok) ESP_LOGD(TAG, "ack not received %i", this->error_count_);
  // return true if ack low
  return ok;
}

bool TM1651Display::half_cycle_clock_high_ack_() {
  // start second half cycle when clock is high and check for ack
  // returns ack if received = false, since ack is DIO low
  uint8_t count1{0};
  bool ack{false};

  this->clk_pin_->digital_write(LINE_HIGH);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);

  this->dio_pin_->pin_mode(gpio::FLAG_INPUT);

  while (this->dio_pin_->digital_read()) {
    count1 += 1;
    if (count1 == 200) {
      this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);
      this->dio_pin_->digital_write(LINE_LOW);
      count1 = 0;
      this->error_count_ = this->error_count_ + 1
      ack = true;
    }
    this->dio_pin_->pin_mode(gpio::FLAG_INPUT);
  }
  this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);

  // ack should be set DIO low by now
  // its not so set DIO low before the next cycle
  //if (!ack) this->dio_pin_->digital_write(LINE_LOW);

  delayMicroseconds(QUARTER_CLOCK_CYCLE);
  // begin next cycle
  this->clk_pin_->digital_write(LINE_LOW);

  return ack;
}

}  // namespace tm1651
}  // namespace esphome
