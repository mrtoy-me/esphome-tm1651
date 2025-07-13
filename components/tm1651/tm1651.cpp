#include "tm1651.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tm1651 {

static const char *const TAG = "tm1651.display";

static const bool LINE_HIGH  = true;
static const bool LINE_LOW   = false;

// TM1651 maximum frequency is 450 kHz
// use conservative clock cycle in microseconds
static const uint8_t CLOCK_CYCLE           = 20;
static const uint8_t HALF_CLOCK_CYCLE      = CLOCK_CYCLE / 2;
static const uint8_t QUARTER_CLOCK_CYCLE   = CLOCK_CYCLE / 4;

static const uint8_t ADDR_AUTO             = 0x40;
static const uint8_t ADDR_FIXED            = 0x44; // fixed address mode
static const uint8_t ADDR_START            = 0xC0; // address of the display register
static const uint8_t FRAME_START           = 0xC1;

static const uint8_t DISPLAY_OFF      = 0x80; // bit 3 off
static const uint8_t DISPLAY_ON       = 0x88; // bit 3 on

static const uint8_t TM1651_MAX_LEVEL      = 7;

static const uint8_t MAX_PERCENT           = 100;
static const uint8_t HALF_MAX_PERCENT      = MAX_PERCENT / 2;

// tm1651 chip brightness values
static const uint8_t HARDWARE_BRIGHTNESS_DARKEST   = 0;
static const uint8_t HARDWARE_BRIGHTNESS_TYPICAL   = 2;
static const uint8_t HARDWARE_BRIGHTNESS_BRIGHTEST = 7;

static const uint8_t TM1651_LEVEL_TAB[]     = { 0b00000000,
                                                0b00000001,
                                                0b00000011,
                                                0b00000111,
                                                0b00001111,
                                                0b00011111,
                                                0b00111111,
                                                0b01111111 };


void TM1651Display::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  this->clk_pin_->setup();
  this->clk_pin_->pin_mode(gpio::FLAG_OUTPUT);

  this->dio_pin_->setup();
  this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);

  // initialise brightness to TYPICAL
  this->brightness_ = HARDWARE_BRIGHTNESS_TYPICAL;

  // clear display
  this->set_level(0);
  this->write_brightness(DISPLAY_ON);
  this->frame(false);
}

void TM1651Display::dump_config() {
  ESP_LOGCONFIG(TAG, "TM1651 Battery Display");
  LOG_PIN("  CLK: ", clk_pin_);
  LOG_PIN("  DIO: ", dio_pin_);
}

void TM1651Display::set_brightness(uint8_t new_brightness) {
  this->brightness_ = this->calculate_brightness(new_brightness);
  if (this->display_on_) this->write_brightness(DISPLAY_ON);
}

void TM1651Display::set_level(uint8_t new_level) {
  if (new_level > TM1651_MAX_LEVEL) new_level = TM1651_MAX_LEVEL;
  this->level_ = new_level;
  if (this->display_on_) this->write_level();
}

void TM1651Display::set_level_percent(uint8_t percentage) {
  this->level_ = this->calculate_level(percentage);
  if (this->display_on_) this->write_level();
}

void TM1651Display::turn_off() {
  this->display_on_ = false;
  this->write_brightness(DISPLAY_OFF);
}

void TM1651Display::turn_on() {
  this->display_on_ = true;
  this->write_level();
  this->write_brightness(DISPLAY_ON);
}


// protected

uint8_t TM1651Display::calculate_brightness(uint8_t new_brightness) {
  if (new_brightness <= 1) return HARDWARE_BRIGHTNESS_DARKEST;
  if (new_brightness == 2) return HARDWARE_BRIGHTNESS_TYPICAL;

  // new_brightness >= 3
  return HARDWARE_BRIGHTNESS_BRIGHTEST;
}

uint8_t TM1651Display::calculate_level(uint8_t percentage) {
  if (percentage > MAX_PERCENT) percentage = MAX_PERCENT;
  // scale 0-100% to 0-7 display levels
  // use integer arithmetic
  // round by adding half maximum percent before division by maximum percent
  uint16_t initial_scaling = (percentage * TM1651_MAX_LEVEL) + HALF_MAX_PERCENT;
  return (uint8_t)(initial_scaling / MAX_PERCENT);
}

void TM1651Display::write_level() {
  this->start();
  if (!this->write_byte(ADDR_FIXED)) ESP_LOGD(TAG, "  Ack not received");
  this->stop();

  this->start();
  if (!this->write_byte(ADDR_START)) ESP_LOGD(TAG, "  Ack not received");
  if (!this->write_byte(TM1651_LEVEL_TAB[this->level_])) ESP_LOGD(TAG, "  Ack not received");
  this->stop();

  // this->start();
  // if (!this->write_byte(this->brightness_control_)) ESP_LOGD(TAG, "  Ack not received");
  // this->stop();
}

void TM1651Display::frame(bool state) {
  uint8_t segment_data = state ? 0x40 : 0x00;

  this->start();
  this->write_byte(ADDR_AUTO);
  this->stop();

  this->start();
  this->write_byte(FRAME_START);
  for (uint8_t i = 0; i < 3; i++) {
    this->write_byte(segment_data);
  }
  this->stop();

  // this->start();
  // this->write_byte(this->brightness_control_);
  // this->stop();
}

void TM1651Display::write_brightness(uint8_t control) {
  this->start();
  this->write_byte(control | this->brightness_);
  this->stop();
}

// low level functions

void TM1651Display::delineate_transmission(bool dio_state) {
  // delineate a data transmission
  // used by start and stop transmission

  // DIO changes its value while CLK is high
  this->dio_pin_->digital_write(dio_state);
  delayMicroseconds(HALF_CLOCK_CYCLE);

  this->clk_pin_->digital_write(LINE_HIGH);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);

  this->dio_pin_->digital_write(!dio_state);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);
}

void TM1651Display::half_cycle_clock_high() {
  // start the second half cycle when the clock is high
  this->clk_pin_->digital_write(LINE_HIGH);
  delayMicroseconds(HALF_CLOCK_CYCLE);
}

bool TM1651Display::half_cycle_clock_high_ack() {
  // start the second half cycle when the clock is high and check for the ack
  // returns the ack bit - should be low (false))

  // set CLK high
  this->clk_pin_->digital_write(LINE_HIGH);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);

  // set DIO to input mode and check the ack
  this->dio_pin_->pin_mode(gpio::FLAG_INPUT);
  bool ack = this->dio_pin_->digital_read();

  // DIO should be low, ack = false
  // now set DIO to low before data line
  // releases at the next clock cycle
  this->dio_pin_->pin_mode(gpio::FLAG_OUTPUT);
  if (!ack) this->dio_pin_->digital_write(LINE_LOW);

  delayMicroseconds(QUARTER_CLOCK_CYCLE);
  // set CLK to low again to begin the next cycle
  this->clk_pin_->digital_write(LINE_LOW);

  return ack;
}

void TM1651Display::half_cycle_clock_low(bool data_bit) {
   // start the first half cycle when the clock is low and write a data bit
  this->clk_pin_->digital_write(LINE_LOW);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);

  this->dio_pin_->digital_write(data_bit);
  delayMicroseconds(QUARTER_CLOCK_CYCLE);
}

void TM1651Display::start() {
  // start data transmission
  // DIO changes from high to low while CLK is high
  this->delineate_transmission(LINE_HIGH);
}

void TM1651Display::stop() {
  // stop data transmission
  // DIO changes from low to high while CLK is high
  this->delineate_transmission(LINE_LOW);
}

bool TM1651Display::write_byte(uint8_t data) {
  // returns true if ack sent after write

  // send 8 data bits, LSB first
  // data bit can only be written to DIO when CLK is low
  for (uint8_t i = 0; i < 8; i++) {
    this->half_cycle_clock_low((bool)(data & 0x01));
    this->half_cycle_clock_high();
    // next bit
    data >>= 1;
  }

  // after writing 8 bits, start a 9th clock cycle
  // during the 9th half-cycle of CLK when low,
  // DIO set high, giving an ack by pulling DIO low
  // set CLK low, DIO high
  this->half_cycle_clock_low(LINE_HIGH);
  // return true if ack low
  return !this->half_cycle_clock_high_ack();
}

}  // namespace tm1651
}  // namespace esphome
