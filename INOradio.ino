/**********************************************************************************************************
  10kHz to 225MHz VFO / RF Generator with Si5351 and Arduino Nano, with Intermediate Frequency (IF_Frequency_kHz) offset
  (+ or -), RX/TX Selector for QRP Transceivers, Band Presets and Bargraph S-Meter. See the schematics for
  wiring and README.txt for details. By J. CesarSound - ver 2.0 - Feb/2021.
***********************************************************************************************************/

//Libraries
#include <Wire.h>                 // IDE Standard
#include <Rotary.h>               // Ben Buxton https://github.com/brianlow/Rotary
#include <Adafruit_SSD1306.h>     // Adafruit SSD1306 https://github.com/adafruit/Adafruit_SSD1306
#include <si5351.h>               // Etherkit https://github.com/etherkit/Si5351Arduino
#include <Adafruit_GFX.h>         // Adafruit GFX https://github.com/adafruit/Adafruit-GFX-Library
                                  // GFX requires BusIO https://github.com/adafruit/Adafruit_BusIO

// User preferences ------------------------------------------------------
#define IF_Frequency_kHz         455  // IF_Frequency_kHz frequency in kHz, 0 = for direct, (+/-).
#define BAND_INIT                  7  // Startup Band (0-12) at startup 
#define XT_CAL_F               33000  // Si5351 calibration factor, adjust to get exatcly 10MHz. Inverse action.
#define S_GAIN                   303  // Adjust sensitivity of Signal Meter A/D input: 
                                      //     101 = 500mv; 202 = 1v; 303 = 1.5v; 404 = 2v; 505 = 2.5v; 1010 = 5v (max).
// Analog pins -----------------------------------------------------------
#define tunestep_pin              A0  // Tune step push button pin.
#define bandsw_pin                A1  // Band selector push button pin.
#define rx_tx_pin                 A2  // RX-TX switch pin, RX = open, TX = grounded. When in TX, the IF_Frequency_kHz value is not used.
#define signal_meter_adc_pin      A3  // The pin used by Signal Meter A/D input.

// digital pins ----------------------------------------------------------
#define rotary_pin1                2  // For use with rotary dial
#define rotary_pin2                3  // For use with rotary dial
#define rx_led_pin                 4  // Bi-directional Red/Green LED w/ 270 ohm resistor
#define tx_led_pin                 5  // Bi-directional Red/Green LED

#define step_up                    1  // For use with rotary dial
#define step_down                 -1  // For use with rotary dial

//------------------------------------------------------------------------------------------------------------
Rotary dial = Rotary(rotary_pin1, rotary_pin2);
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &Wire);
Si5351 si5351(0x60); //Si5351 I2C Address 0x60

unsigned long freq, freqold, fstep;
long interfreq = IF_Frequency_kHz, interfreqold = 0;
long cal = XT_CAL_F;
unsigned int smval;
byte encoder = 1; // What is this.. it is always 1, never changed.
byte stp, n = 1;
byte band, x, xo;
bool sts = 0;
unsigned int period = 100;  // i think this can be removed
unsigned long time_now = 0;

bool transmit = false;
String bandName = "";
String stepName = "";

ISR(PCINT2_vect) {
  unsigned char result = dial.process();
  if (result == DIR_CW) change_frequency(step_up);
  else if (result == DIR_CCW) change_frequency(step_down);
}

void setup() {
  Wire.begin();
  
  pinMode(rotary_pin1, INPUT_PULLUP);
  pinMode(rotary_pin2, INPUT_PULLUP);
  pinMode(tunestep_pin, INPUT_PULLUP);
  pinMode(bandsw_pin, INPUT_PULLUP);
  pinMode(rx_tx_pin, INPUT_PULLUP);
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.display();

  startup_text();  //If you hang on startup, comment

  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  si5351.set_correction(cal, SI5351_PLL_INPUT_XO);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
  si5351.output_enable(SI5351_CLK0, 1);                  //1 - Enable / 0 - Disable CLK
  si5351.output_enable(SI5351_CLK1, 0);
  si5351.output_enable(SI5351_CLK2, 0);

  // dial.begin();  // is this missing?
  PCICR |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT18) | (1 << PCINT19);
  sei();

  band = BAND_INIT;
  bandpresets();
  stp = 4;
  setstep();
}

void loop() {
  if (freqold != freq) {
    time_now = millis();
    tunegen();
    freqold = freq;
  }

  if (interfreqold != interfreq) {
    time_now = millis();
    tunegen();
    interfreqold = interfreq;
  }

  if (xo != x) {
    time_now = millis();
    xo = x;
  }

  if (digitalRead(tunestep_pin) == LOW) {
    time_now = (millis() + 300);
    setstep();
    delay(300);
  }

  if (digitalRead(bandsw_pin) == LOW) {
    time_now = (millis() + 300);
    inc_preset();
    delay(300);
  }

  if (digitalRead(rx_tx_pin) == LOW) {
    time_now = (millis() + 300);
    sts = 1;
    transmit=true;
  } 
  else {
    sts = 0;
    transmit= false;
  }

  if ((time_now + period) > millis()) {
    displayfreq();
    layout();
  }
  signalread();
}


void change_frequency(short direction) {
    if (direction == step_up) freq = freq + fstep;
    if (freq >= 225000000) freq = 225000000; // set upper limit
    if (direction == step_down) freq = freq - fstep;
    if (fstep == 1000000 && freq <= 1000000) freq = 1000000;
    else if (freq < 10000) freq = 10000; // set lower limit
}

void tunegen() {
  si5351.set_freq((freq + (interfreq * 1000ULL)) * 100ULL, SI5351_CLK0);
}

void displayfreq() {
  unsigned int m = freq / 1000000;
  unsigned int k = (freq % 1000000) / 1000;
  unsigned int h = (freq % 1000) / 1;

  display.clearDisplay();
  display.setTextSize(2);

  char buffer[15] = "";
  if (m < 1) {
    display.setCursor(41, 1); sprintf(buffer, "%003d.%003d", k, h);
  }
  else if (m < 100) {
    display.setCursor(5, 1); sprintf(buffer, "%2d.%003d.%003d", m, k, h);
  }
  else if (m >= 100) {
    unsigned int h = (freq % 1000) / 10;
    display.setCursor(5, 1); sprintf(buffer, "%2d.%003d.%02d", m, k, h);
  }
  display.print(buffer);
}



void setstep() {
  switch (stp) {
    case 1: stp = 2; fstep = 1; break;
    case 2: stp = 3; fstep = 10; break;
    case 3: stp = 4; fstep = 1000; break;
    case 4: stp = 5; fstep = 5000; break;
    case 5: stp = 6; fstep = 10000; break;
    case 6: stp = 1; fstep = 1000000; break;
  }
}

void inc_preset() {
  band++;
  if (band > 12) band = 0;
  bandpresets();
  delay(50);
}

void bandpresets() {
  switch (band)  {
    case  0: freq =   100000; bandName = "GEN";  tunegen(); break;
    case  1: freq =   526500; bandName = "BC";   break;  // bottom of AM BCB
    case  2: freq =  1800000; bandName = "160m"; break;  // bottom of 160m
    case  3: freq =  3500000; bandName = "80m";  break;  // bottom of 80m
    case  4: freq =  5000000; bandName = "60m";  break;  // bottom of 60m
    case  5: freq =  7000000; bandName = "40m";  break;  // bottom of 40m
    case  6: freq = 10000000; bandName = "30m";  break;  // bottom of 30m
    case  7: freq = 14000000; bandName = "20m";  break;  // bottom of 20m
    case  8: freq = 18068000; bandName = "17m";  break;  // bottom of 17m
    case  9: freq = 21000000; bandName = "15m";  break;  // bottom of 15m
    case 10: freq = 24890000; bandName = "12m";  break;  // bottom of 12m
    case 11: freq = 28000000; bandName = "10m";  break;  // bottom of 10m
    case 12: freq = 50000000; bandName = "6m";   break;  // bottom of 6m
  }
  si5351.pll_reset(SI5351_PLLA);
  stp = 4; setstep();
}

void layout() {
  display.setTextColor(WHITE);
  display.drawLine(0, 20, 127, 20, WHITE);
  display.drawLine(0, 43, 127, 43, WHITE);
  display.drawLine(105, 24, 105, 39, WHITE);
  display.drawLine(87, 24, 87, 39, WHITE);
  display.drawLine(87, 48, 87, 63, WHITE);
  display.drawLine(15, 55, 82, 55, WHITE);
  display.setTextSize(1);
  display.setCursor(59, 23);
  display.print("STEP");
  display.setCursor(54, 33);
  if (stp == 2) display.print("  1Hz"); if (stp == 3) display.print(" 10Hz"); if (stp == 4) display.print(" 1kHz");
  if (stp == 5) display.print(" 5kHz"); if (stp == 6) display.print("10kHz"); if (stp == 1) display.print(" 1MHz");
  display.setTextSize(1);
  display.setCursor(92, 48);
  display.print("IF_Frequency_kHz:");
  display.setCursor(92, 57);
  display.print(interfreq);
  display.print("k");
  display.setTextSize(1);
  display.setCursor(110, 23);
  if (freq < 1000000) display.print("kHz");
  if (freq >= 1000000) display.print("MHz");
  display.setCursor(110, 33);
  if (interfreq == 0) display.print("VFO");
  if (interfreq != 0) display.print("L O");
  display.setCursor(91, 28);
  if (!sts) display.print("RX"); if (!sts) interfreq = IF_Frequency_kHz;
  if (sts) display.print("TX"); if (sts) interfreq = 0;
  bandlist(); drawbargraph();
  display.display();
}

void bandlist() {
  display.setTextSize(2);
  display.setCursor(0, 25);
  display.print(bandName);
  if (band == 0) interfreq = 0; else if (!sts) interfreq = IF_Frequency_kHz;
}

void signalread() {
  smval = analogRead(signal_meter_adc_pin); x = map(smval, 0, S_GAIN, 1, 14); if (x > 14) x = 14;
}

void drawbargraph() {
  byte y = map(n, 1, 42, 1, 14);
  display.setTextSize(1);

  //Pointer
  display.setCursor(0, 48); display.print("TU");
  switch (y) {
    case 1: display.fillRect(15, 48, 2, 6, WHITE); break;
    case 2: display.fillRect(20, 48, 2, 6, WHITE); break;
    case 3: display.fillRect(25, 48, 2, 6, WHITE); break;
    case 4: display.fillRect(30, 48, 2, 6, WHITE); break;
    case 5: display.fillRect(35, 48, 2, 6, WHITE); break;
    case 6: display.fillRect(40, 48, 2, 6, WHITE); break;
    case 7: display.fillRect(45, 48, 2, 6, WHITE); break;
    case 8: display.fillRect(50, 48, 2, 6, WHITE); break;
    case 9: display.fillRect(55, 48, 2, 6, WHITE); break;
    case 10: display.fillRect(60, 48, 2, 6, WHITE); break;
    case 11: display.fillRect(65, 48, 2, 6, WHITE); break;
    case 12: display.fillRect(70, 48, 2, 6, WHITE); break;
    case 13: display.fillRect(75, 48, 2, 6, WHITE); break;
    case 14: display.fillRect(80, 48, 2, 6, WHITE); break;
  }

  //Bargraph
  display.setCursor(0, 57); display.print("SM");
  switch (x) {
    case 14: display.fillRect(80, 58, 2, 6, WHITE);
    case 13: display.fillRect(75, 58, 2, 6, WHITE);
    case 12: display.fillRect(70, 58, 2, 6, WHITE);
    case 11: display.fillRect(65, 58, 2, 6, WHITE);
    case 10: display.fillRect(60, 58, 2, 6, WHITE);
    case 9: display.fillRect(55, 58, 2, 6, WHITE);
    case 8: display.fillRect(50, 58, 2, 6, WHITE);
    case 7: display.fillRect(45, 58, 2, 6, WHITE);
    case 6: display.fillRect(40, 58, 2, 6, WHITE);
    case 5: display.fillRect(35, 58, 2, 6, WHITE);
    case 4: display.fillRect(30, 58, 2, 6, WHITE);
    case 3: display.fillRect(25, 58, 2, 6, WHITE);
    case 2: display.fillRect(20, 58, 2, 6, WHITE);
    case 1: display.fillRect(15, 58, 2, 6, WHITE);
  }
}

void startup_text() {
  display.setTextSize(1); display.setCursor(13, 18);
  display.print("Si5351 VFO/RF GEN");
  display.setCursor(6, 40);
  display.print("JCR RADIO - Ver 2.0");
  display.display(); delay(2000);
}
