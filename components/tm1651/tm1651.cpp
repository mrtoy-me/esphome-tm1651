#include "tm1651.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tm1651 {

static const char *const TAG = "tm1651.display";

static const bool LINE_HIGH                      = true;
static const bool LINE_LOW                       = false;

// TM1651 maximum frequency is 500 kHz (duty ratio 50%) = 2 microseconds / cycle
// choose appropriate clock cycle in microseconds
static const uint8_t CLOCK_CYCLE                 = 8;

static const uint8_t HALF_CLOCK_CYCLE            = CLOCK_CYCLE / 2;
static const uint8_t QUARTER_CLOCK_CYCLE         = CLOCK_CYCLE / 4;

static const uint8_t ADDR_FIXED                  = 0x44; // fixed address mode
static const uint8_t ADDR_START                  = 0xC0; // address of the display register

static const uint8_t DISPLAY_OFF                 = 0x80;
static const uint8_t DISPLAY_ON                  = 0x88;

static const uint8_t MAX_DISPLAY_LEVELS          = 7;

static const uint8_t PERCENT100                  = 100;
static const uint8_t PERCENT50                   = 50;

static const uint8_t TM1651_BRIGHTNESS_DARKEST   = 0;
static const uint8_t TM1651_BRIGHTNESS_TYPICAL   = 2;
static const uint8_t TM1651_BRIGHTNESS_BRIGHTEST = 7;

static const uint8_t TM1651_LEVEL_TAB[]          = { 0b00000000,
                                                     0b00000001,
                                                     0b00000011,
                                                     0b00000111,
                                                     0b00001111,
                                                     0b00011111,
                                                     0b00111111,
                                                     0b01111111 };

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
  this->display_level();
  this->update_brightness(DISPLAY_ON);
}

void TM1651Display::dump_config() {
  ESP_LOGCONFIG(TAG, "TM1651 Mini Battery Display");
  LOG_PIN("  CLK: ", clk_pin_);
  LOG_PIN("  DIO: ", dio_pin_);
}

void TM1651Display::set_brightness(uint8_t new_brightness) {
  this->brightness_ = this->remap_brightness(new_brightness);
  if (this->display_on_) this->update_brightness(DISPLAY_ON);
}

void TM1651Display::set_level(uint8_t new_level) {
  if (new_level > MAX_DISPLAY_LEVELS) new_level = MAX_DISPLAY_LEVELS;
  this->level_ = new_level;
  if (this->display_on_) this->display_level();
}

void TM1651Display::set_level_percent(uint8_t percentage) {
  this->level_ = this->calculate_level(percentage);
  if (this->display_on_) this->display_level();
}

void TM1651Display::turn_off() {
  this->display_on_ = false;
  this->update_brightness(DISPLAY_OFF);
}

void TM1651Display::turn_on() {
  this->display_on_ = true;
  this->display_level(); // level could have been changed when display turned off
  this->update_brightness(DISPLAY_ON);
}

// protected

uint8_t TM1651Display::remap_brightness(uint8_t new_brightness) {
  if (new_brightness <= 1) return TM1651_BRIGHTNESS_DARKEST;
  if (new_brightness == 2) return TM1651_BRIGHTNESS_TYPICAL;

  // new_brightness >= 3
  return TM1651_BRIGHTNESS_BRIGHTEST;
}

uint8_t TM1651Display::calculate_level(uint8_t percentage) {
  if (percentage > PERCENT100) percentage = PERCENT100;
  // scale 0-100% to 0-7 display levels
  // use integer arithmetic
  // round by adding half maximum percent before division by maximum percent
  uint16_t initial_scaling = (percentage * MAX_DISPLAY_LEVELS) + PERCENT50;
  return (uint8_t)(initial_scaling / PERCENT100);
}

void TM1651Display::display_level() {
  this->start();
  if (!this->write_byte(ADDR_FIXED));
  this->stop();

  this->start();
  if (!this->write_byte(ADDR_START));
  if (!this->write_byte(TM1651_LEVEL_TAB[this->level_]));
  this->stop();
}

void TM1651Display::update_brightness(uint8_t on_off_control) {
  this->start();
  this->write_byte(on_off_control | this->brightness_);
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
