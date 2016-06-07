/*
 * Ambient Databox for Arduino Yun   
 * 
 * Compatible with:
 * 
 *  1. Device
 *    - Arduino Yun
 *    - Arduino Uno/Mega/etc. with Yun Shield
 *    
 *  2. Sensors
 *    - SparkFun Weather Sheild (https://www.sparkfun.com/products/12081)
 *    - MP3115A2 pressure sensor breakout, HTU21D humidity sensor breaout, and TEMT6000 light sensor breakout
 * 
 *  3. Data Repository
 *    - Wolfram Data Drop (http://datadrop.wolframcloud.com/)
 * 
 */

#include <LiquidCrystal.h>
#include <Process.h>
#include <Wire.h> //I2C needed for sensors
#include "MPL3115A2.h" //Pressure sensor
#include "HTU21D.h" //Humidity sensor

#define OSEPP_LCD_01
#define WEATHER_SHIELD

#ifdef OSEPP_LCD_01
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
#else
LiquidCrystal lcd(12, 11, 10, 5, 4, 3, 2);
#endif

#define CHAR_TEMP byte(0) // Character for temperature icon
byte temp_icon[8] = {
  B01110,
  B01010,
  B01010,
  B01110,
  B11111,
  B11111,
  B01110,
};

#define CHAR_HUMID byte(1) // Character for humidity icon
byte humid_icon[8] = {
  B00100,
  B01010,
  B01010,
  B10001,
  B11111,
  B10001,
  B01110,
};

#define CHAR_DEG byte(2) // Character for degree symbol
byte degree_icon[8] = {
  B00100,
  B01010,
  B00100,
  B00000,
  B00000,
  B00000,
  B00000,
};

#define CHAR_LIGHT byte(3) // Character for light icon
byte light_icon[8] = {
  B00010,
  B00110,
  B01100,
  B11111,
  B00110,
  B01100,
  B01000,
};

#define CHAR_PRESS byte(4) // Character for pressyre icon
byte pressure_icon[8] = {
  B01110,
  B01110,
  B01110,
  B11111,
  B01110,
  B00100,
  B11111,
};

//
// Process related
//
Process date;

//
// Weather related
//
MPL3115A2 myPressure;
HTU21D myHumidity;

// Status LED pins for Weather Shield
#ifdef WEATHER_SHIELD
const byte STAT1 = 7;
const byte STAT2 = 8;
const byte OP_VOL = A3; // Reference operational voltage of Weather Shield 
#endif

// analog I/O pins for light sensor
const byte LIGHT = A1;


#define SCREEN_TIME 1000 // How long each screen will stay in milliseconds
#define SCREEN_LINE_MAX 4

int screen = 0;
int screen_prev = -1;
bool screen_lock = false;
bool screen_lock_prev = true;
long timer_current = 0;
long timer_start = 0;

#ifdef OSEPP_LCD_01
// Key definitions for OSEPP-LCD-01
int adc_key_in  = 0;
#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5

int keypressed = btnNONE;
int keypressed_prev = btnNONE;
 
// Read the buttons for OSEPP-LCD-01
int read_lcd_buttons()
{
  adc_key_in = analogRead(0);      // read the value from the sensor
  // my buttons when read are centered at these valies: 0, 144, 329, 504, 741
  // we add approx 50 to those values and check to see if we are close
  if (adc_key_in > 1000) return btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
  if (adc_key_in < 50)   return btnRIGHT; 
  if (adc_key_in < 195)  return btnUP;
  if (adc_key_in < 380)  return btnDOWN;
  if (adc_key_in < 555)  return btnLEFT;
  if (adc_key_in < 790)  return btnSELECT;  
  return btnNONE;  // when all others fail, return this...
}
#endif

String time_string;
float temp_f = 0;
float temp_c = 0;
float humidity = 0;
float pressure = 0;
float light = 0;

float get_light_level()
{
#ifdef WEATHER_SHIELD
  float operatingVoltage = analogRead(OP_VOL);
#else
  float operatingVolatage = 1023;
#endif
  float lightSensor = analogRead(LIGHT);
  
  return (lightSensor / operatingVoltage * 100);
}

void get_weather()
{
  pressure = myPressure.readPressure();
  temp_c = myPressure.readTemp();
  temp_f = myPressure.readTempF();
  humidity = myHumidity.readHumidity();
  light = get_light_level();
}

// Emulating backspace (to remove the last digit from float value)
#define lcd_backspace { lcd.rightToLeft(); lcd.write(" "); lcd.leftToRight(); }

void display_line(int i, int line)
{
  lcd.setCursor(0, line);
  
  switch (i) {
    case 0: // Clock
      lcd.print(time_string.substring(0, 15)); // For some reason, byte(0) at the end of string is spilling. Manually cutting it off
      lcd.write(" ");
      break;
      
    case 1: // Temperature (C & F)
      lcd.write(CHAR_TEMP);
      if ((temp_c < 100) && (temp_c >= 0)) // No space for 100th digit and minus
        lcd.write(" ");
      lcd.print(temp_c);
      lcd_backspace;
      lcd.write(CHAR_DEG);
      lcd.write("C");
      lcd.write(" ");
      
      lcd.print(temp_f);
      lcd_backspace;
      lcd.write(CHAR_DEG);
      lcd.write("F");
      break;
    
    case 2: // Humidity and ambient light
      lcd.write(CHAR_HUMID);
      if (humidity < 100) // No space for 100th digit
        lcd.write(" ");
      lcd.print(humidity);
      lcd_backspace;
      lcd.print("%  ");

      lcd.write(CHAR_LIGHT);
      if (humidity < 100) // No space for 100th digit
        lcd.write(" ");
      lcd.print(light);
      lcd_backspace;
      lcd.write("%");
      break;
    
    case 3: // Pressure
      lcd.write(CHAR_PRESS);
      lcd.write(" ");
      lcd.print(pressure);
      lcd.print(" Pa");
      break;
  }
}

void setup()
{
  //
  // LCD set up
  //
  lcd.begin(16, 2); // LCD's number of columns and rows:
  
  // Set up for LCD special characters
  lcd.createChar(CHAR_TEMP, temp_icon);
  lcd.createChar(CHAR_HUMID, humid_icon);
  lcd.createChar(CHAR_DEG, degree_icon);
  lcd.createChar(CHAR_LIGHT, light_icon);
  lcd.createChar(CHAR_PRESS, pressure_icon);
  lcd.clear();

  //
  // Weather sensors set up
  //
#ifdef WEATHER_SHIELD
  pinMode(STAT1, OUTPUT); //Status LED Blue
  pinMode(STAT2, OUTPUT); //Status LED Green
  pinMode(OP_VOL, INPUT);
#endif

  pinMode(LIGHT, INPUT); // Light sensor I/O

  //Configure the pressure sensor
  myPressure.begin(); // Get sensor online
  myPressure.setModeBarometer(); // Measure pressure in Pascals from 20 to 110 kPa
  myPressure.setOversampleRate(7); // Set Oversample to the recommended 128
  myPressure.enableEventFlags(); // Enable all three pressure and temp event flags 

  //Configure the humidity sensor
  myHumidity.begin();

  //
  // Initialization screen
  //
  lcd.setCursor(0, 0);
  lcd.print("Ambient Databox");
  lcd.setCursor(0, 1);
  lcd.print("Initializing..");
  lcd.blink();

  // Start bridge
  Bridge.begin();

  lcd.noBlink();
}

bool colon = true;

void loop()
{
  if (!date.running()) {
    date.begin("date");
    date.addParameter("+%R %a %m/%d");
    date.runAsynchronously();
  }

  while (date.available())
  {
    time_string = date.readString();
    get_weather();

    lcd.clear();
    display_line(screen % SCREEN_LINE_MAX, 0);
    display_line((screen + 1) % SCREEN_LINE_MAX, 1);
    
    screen = (screen + 1) % SCREEN_LINE_MAX; 
    delay(SCREEN_TIME);
  }
}

