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
static const uint8_t CLOCK_CYCLE = 8;

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

void TM1651Display::start_() {
  // start data transmission
  this->clk_pin_->digital_write(LINE_HIGH);
  this->dio_pin_->digital_write(LINE_HIGH);
  delayMicroseconds(2);
  this->dio_pin_->digital_write(LINE_LOW);
  this->clk_pin_->digital_write(LINE_LOW);
}

void TM1651Display::stop_() {
  this->clk_pin_->digital_write(LINE_LOW);
  this->dio_pin_->digital_write(LINE_LOW);
  this->clk_pin_->digital_write(LINE_HIGH);
  this->dio_pin_->digital_write(LINE_HIGH);
}
void TM1651Display::reset_errors() {
  this->error_count_= 0;
  this->total_ = 0;
}

void TM1651Display::show_errors() {
  ESP_LOGD(TAG, "ack not received %i from total %i = %3.1f", this->error_count_, this->total_, ((float)(this->error_count_)*100.0)/(float)(this->total_));
}

bool TM1651Display::write_byte_(uint8_t data) {
  uint8_t i, count1{0};
  bool ack{true};
  // sent 8bit data
  this->total_ =  this->total_ + 1;
  for (i = 0; i < 8; i++) {
    this->clk_pin_->digital_write(LINE_LOW);
    if (wr_data & 0x01) {
      // LSB first
      this->dio_pin_->digital_write(LINE_HIGH);
    } else {
      this->dio_pin_->digital_write(LINE_LOW);
    }
    delayMicroseconds(3);
    wr_data >>= 1;
    this->clk_pin_->digital_write(LINE_HIGH);
    delayMicroseconds(3);
  }

  // wait for the ACK
  this->clk_pin_->digital_write(LINE_LOW);
  this->dio_pin_->digital_write(LINE_HIGH);
  this->dio_pin_->digital_write(LINE_HIGH);
  this->dio_pin_->pin_mode(gpio::FLAG_INPUT);
  while (his->dio_pin_->digital_read()) {
    count1 += 1;
    if (count1 == 200) {
      this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);
      this->dio_pin_->digital_write(LINE_LOW);
      count1 = 0;
      this->error_count_ = this->error_count_ + 1;
      ack = false;
    }
    this->dio_pin_->pin_mode(gpio::FLAG_INPUT);
  }
  this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);
  return ack;
}

}  // namespace tm1651
}  // namespace esphome
