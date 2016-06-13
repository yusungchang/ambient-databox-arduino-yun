/*
   Data Collector for Arduino Yun

   Compatible with:

    1. Board
      - Arduino Yun
      - Arduino Uno/Mega/etc. with Yun Shield

    2. Sensors
      - SparkFun Weather Sheild (https://www.sparkfun.com/products/12081)
      - MP3115A2 pressure sensor breakout, HTU21D humidity sensor breaout, and TEMT6000 light sensor breakout
      - ADXL335 accelerometer breakout
      - [Future] SparkFun LSM9DS1 IMU breakout (9 degree)
      - [Future] UV Sensor breakout (e.g. http://www.waveshare.com/uv-sensor.htm)

    3. Data Repository
      - Wolfram Data Drop (http://datadrop.wolframcloud.com/)
*/

#include <LiquidCrystal.h>
#include <Process.h>
#include <FileIO.h>
#include <Wire.h> //I2C
#include "MPL3115A2.h" //Pressure sensor
#include "HTU21D.h" //Humidity sensor

//
// Configure your hardware by comment/uncomment appropriate sensors
// Note: Be careful with analog pin usage. Make sure that each pin only connects to one sensor/shields.
// 
#define _OSEPP_LCD_01_ // If you are using OSEPP LCD 01 panel (w/ keypads)
#define _WEATHER_SHIELD_ // If you are using SparkFun Weather Shield
//#define _ADXL335_
//#define _LSM9DS1
//#define _UV_SENSOR_

#ifdef _LSM9DS1_
//#include <SPI.h>
//#include <SparkFunLSM9DS1.h>
#endif

//
// LCD related declarations
//
#ifdef _OSEPP_LCD_01_
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);
#else
LiquidCrystal lcd(12, 11, 10, 5, 4, 3, 2);
#endif

#define CHAR_TEMP byte(0) // Character for temperature icon
static byte temp_icon[8] = {
  B01110,
  B01010,
  B01010,
  B01110,
  B11111,
  B11111,
  B01110,
};

#define CHAR_HUMID byte(1) // Character for humidity icon
static byte humid_icon[8] = {
  B00100,
  B01010,
  B01010,
  B10001,
  B11111,
  B10001,
  B01110,
};

#define CHAR_DEG byte(2) // Character for degree symbol
static byte degree_icon[8] = {
  B00100,
  B01010,
  B00100,
  B00000,
  B00000,
  B00000,
  B00000,
};

#define CHAR_LIGHT byte(3) // Character for light icon
static byte light_icon[8] = {
  B00010,
  B00110,
  B01100,
  B11111,
  B00110,
  B01100,
  B01000,
};

#define CHAR_PRESS byte(4) // Character for pressure icon
static byte pressure_icon[8] = {
  B01110,
  B01110,
  B01110,
  B11111,
  B01110,
  B00100,
  B11111,
};

#define CHAR_WOLFRAM byte(5) // Character for Wolfram icon
static byte wolfram_icon[8] = {
  B00100,
  B11111,
  B01110,
  B11111,
  B01110,
  B11111,
  B00100,
};

/*
  //
  // OSEPP-LCD-01 keypad  specific declarations
  //
  #ifdef _OSEPP_LCD_01_
  int adc_key_in  = 0;
  #define btnRIGHT  0
  #define btnUP     1
  #define btnDOWN   2
  #define btnLEFT   3
  #define btnSELECT 4
  #define btnNONE   5

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
*/

//
// Bridge related declarations
//
Process processDate; // Process for date command
Process processCurl; // Process for curl command
#if defined(_ADXL335_) || defined(_LSM9DS1_)
Process processUnixTime; // Process for date +%s command (unix timestamp for event record)
#endif

//
// Sensor related declarations
//
MPL3115A2 sensorPressure;
HTU21D sensorHumidity;

#ifdef _WEATHER_SHIELD_
#define WS_STAT1      7 // Status LED pins for Weather Shield
#define WS_STAT2      8 // Status LED pins for Weather Shield
#define WS_OP_VOL     A3 // Reference operational voltage of Weather Shield 
#define SENSOR_LIGHT  A1 // analog I/O pins for light sensor (for Weather Shield)
#else
#define SENSOR_LIGHT  A4 // analog I/O pins for light sensor (for TEMT6000 or other discrete breakouts)
#endif

#ifdef _ADXL335_
// Assign them based on actual placement of the breakout.
// X-Y for horizontal plane, Z for vertical (height)
#define SENSOR_ACCEL_X  A1
#define SENSOR_ACCEL_Y  A2
#define SENSOR_ACCEL_Z  A3
#endif

#ifdef _LSM9DS1_
LSM9DS1 sensorIMU;
#define LSM9DS1_M   0x1E // Would be 0x1C if SDO_M is LOW
#define LSM9DS1_AG  0x6B // Would be 0x6A if SDO_AG is LOW
#endif


//
// Wolfram Data Drop related declarations
//
#define wolfram_databin_id_file   "/mnt/sda1/wolframdatabin.id" // A file containing Databin ID in plain text. Store it in Atheros (Linino) side.
#define wolfram_datadrop_log_file "/mnt/sda1/wolframdatadrop.log"
#define wolfram_datadrop_api_url  "http://datadrop.wolframcloud.com/api" // Data Drop API URL
#define wolfram_datadrop_api_ver  "v1.0" // Data Drop API version
#define wolfram_datadrop_api_add  "Add" // Data Drop Add command
#define wolfram_datadrop_sampling 300000 // Sample rate (default: every 300 seconds (5 min) at most)
#define wolfram_datadrop_timeout  60000 // Upload timeout (default: 60 seconds)

//
// UI related declarations
//
#define SCROLL_DELAY    1000 // How long each screen will stay in milliseconds
#if defined (_ADXL335_) || defined(_LSM9DS1_)
#define SCREEN_LINE_MAX 6
#else
#define SCREEN_LINE_MAX 5
#endif

int screen = 0;

//
// Data related declarations
//

String time_string;
float temp_f = 0;
float temp_c = 0;
float humidity = 0;
float pressure = 0;
float light = 0;

// Min/max for sensor data
#define TEMP_C_MIN   -99
#define TEMP_C_MAX    99
#define HUMIDITY_MIN  0
#define HUMIDITY_MAX  150
#define PRESSURE_MIN  0
#define PRESSURE_MAX  999999
#define LIGHT_MIN     0
#define LIGHT_MAX     100

// Flags for invalid sensor data
byte valid_flags = B0000000;

// Validate sensor data
#define VALID_TEMP        valid_flags |= B00000001
#define VALID_HUMIDITY    valid_flags |= B00000010
#define VALID_PRESSURE    valid_flags |= B00000100
#define VALID_LIGHT       valid_flags |= B00001000

// Invalidate sensor data
#define INVALID_TEMP      valid_flags &= B11111110
#define INVALID_HUMIDITY  valid_flags &= B11111101
#define INVALID_PRESSURE  valid_flags &= B11111011
#define INVALID_LIGHT     valid_flags &= B11110111

// Tell whether sensor data is valid or not
#define IS_VALID_TEMP       (valid_flags & B00000001)
#define IS_VALID_HUMIDITY   (valid_flags & B00000010)
#define IS_VALID_PRESSURE   (valid_flags & B00000100)
#define IS_VALID_LIGHT      (valid_flags & B00001000)

// Last time sensor data is collected
unsigned long last_updated = 0;


//
// Weather sensor related functions
//
float get_light_level()
{
#ifdef _WEATHER_SHIELD_
  float operatingVoltage = analogRead(WS_OP_VOL);
#else
  float operatingVoltage = 1023;
#endif
  float lightSensor = analogRead(SENSOR_LIGHT);

  // Normalize sensor input with operational voltage
  return (lightSensor / operatingVoltage * 100);
}

void get_weather()
{
  pressure = sensorPressure.readPressure();
  if ((pressure < PRESSURE_MIN) || (pressure > PRESSURE_MAX))
    INVALID_PRESSURE;
  else
    VALID_PRESSURE;
    
  temp_c = sensorPressure.readTemp();
  if ((temp_c < TEMP_C_MIN) || (temp_c > TEMP_C_MAX))
    INVALID_TEMP;
  else {
    VALID_TEMP;
    temp_f = sensorPressure.readTempF();
  }

  humidity = sensorHumidity.readHumidity();
  if ((humidity < HUMIDITY_MIN) || (humidity > HUMIDITY_MAX))
    INVALID_HUMIDITY;
  else
    VALID_HUMIDITY;

  light = get_light_level();
  if ((light < LIGHT_MIN) || (light > LIGHT_MAX))
    INVALID_LIGHT;
  else
    VALID_LIGHT;

  last_updated = millis();
}


//
// Acceleromter data record functions
//
#ifdef _ADXL335_
#define accel_event_log_file_location   "/mnt/sdb1/"
#define accel_event_log_file_prefix     "accel_data_"
#define accel_event_log_file_ext        ".log"

void record_accel_data()
{
  if (!processUnixTime.running()) {
    processUnixTime.begin("date");
    processUnixTime.addParameter("+%s"); // Unit time (seconds since 1970)
    processUnixTime.run();
  }
  else
    return;
}
#endif


//
// Wolfram Data Drop functions
//
#define WOLFRAM_DATADROP_PARAM_MAX    256
#define WOLFRAM_DATABIN_ID_MAX        16
#define WOLFRAM_DATADROP_LOG_MAX      25000000 // Max size for log file (approx. 7 days). Afterward, it starts again.
#define WOLFRAM_DATADROP_LOG_LINEMAX  70

bool wolfram_datadrop_ready = false;
bool wolfram_datadrop_log_new = true;
char wolfram_datadrop_params[WOLFRAM_DATADROP_PARAM_MAX];
char wolfram_databin_id[WOLFRAM_DATABIN_ID_MAX];
unsigned long wolfram_datadrop_timer = 0;
unsigned long wolfram_datadrop_timer_last = 0;

#define MILLITOSEC(x)   (unsigned long)(x / 1000) // Convert milliseconds to seconds

// Initialize Databin
bool wolfram_datadrop_init()
{
  File id_file = FileSystem.open(wolfram_databin_id_file, FILE_READ);
  int i = 0;

  lcd.blink();

  if (id_file)
  {
    lcd.clear();
    lcd.print("Wolfram Databin ID");

    lcd.setCursor(0, 1);
    lcd.print("Reading...");
    delay(1000);

    while (id_file.available() && i < WOLFRAM_DATABIN_ID_MAX)
    {
      byte c = id_file.read();

      if (c < 32) {
        wolfram_databin_id[i] = 0;
        break;
      }

      wolfram_databin_id[i++] = c;
    }

    if (i)
      wolfram_datadrop_ready = true;
    else
      wolfram_datadrop_ready = false;
  }
  else
    wolfram_datadrop_ready = false;

  if (id_file)
    id_file.close();

  lcd.clear();
  lcd.print("Wolfram Databin ID");
  lcd.setCursor(0, 1);

  if (wolfram_datadrop_ready)
    lcd.print("Successful.");
  else
    lcd.print("Not available.");

  delay(1000);

  lcd.clear();
  lcd.noBlink();

  return wolfram_datadrop_ready;
}

// Update Databin
void wolfram_datadrop_update()
{
  char temp_c_str[7];
  char humidity_str[7];
  char pressure_str[10];
  char light_str[7];

  File log_file = FileSystem.open(wolfram_datadrop_log_file, FILE_APPEND);
  
  // If the log file is too big, start it over
  if ((log_file.size() > WOLFRAM_DATADROP_LOG_MAX) || (wolfram_datadrop_log_new))
  {
    log_file.close();
    FileSystem.remove(wolfram_datadrop_log_file);
    wolfram_datadrop_log_new = false;
    return;
  }

  // Write header
  if (log_file.size() == 0)
  {
    log_file.println(":: Wolfram Data Drop Post Log");
    log_file.print(":: Wolfram Datbin ID: "); log_file.println(wolfram_databin_id);
    log_file.print(":: Wolfram Data Drop API Version: "); log_file.println(wolfram_datadrop_api_ver);
    log_file.print(":: Post started at: "); log_file.println(time_string);
  }

  if (wolfram_datadrop_ready && last_updated)
  {
    if (millis() < wolfram_datadrop_timer_last) // If millis() overflow, reset timer. You might lose few samplings
    {
      wolfram_datadrop_timer_last = millis();
      return;
    }
    else if ((millis() - wolfram_datadrop_timer_last > wolfram_datadrop_sampling) || (wolfram_datadrop_timer_last == 0)) // If timer is up, upload the data
    {
      //
      // Convert data to string
      //
      if (IS_VALID_TEMP)
        dtostrf(temp_c, 4, 2, temp_c_str);
      else
        temp_c_str[0] = 0;

      if (IS_VALID_HUMIDITY)
        dtostrf(humidity, 4, 2, humidity_str);
      else
        humidity_str[0] = 0;

      if (IS_VALID_PRESSURE)
        dtostrf(pressure, 4, 2, pressure_str);
      else
        pressure_str[0] = 0;

      if (IS_VALID_LIGHT)  
        dtostrf(light, 4, 2, light_str);
      else
        light_str[0] = 0;

      // Construct API call
      sprintf(wolfram_datadrop_params,
        "curl --data \"bin=%s\&temperature=%s\&humidity=%s\&pressure=%s\&light=%s\" %s/%s/%s",
        wolfram_databin_id, temp_c_str, humidity_str, pressure_str, light_str,
        wolfram_datadrop_api_url, wolfram_datadrop_api_ver, wolfram_datadrop_api_add);

      if (!processCurl.running())
      {
        processCurl.flush();
        processCurl.runShellCommandAsynchronously(wolfram_datadrop_params);
        wolfram_datadrop_timer_last = millis();
        log_file.print("["); log_file.print(MILLITOSEC(wolfram_datadrop_timer_last)); log_file.println("] Posted.");
      }
    }
    else if ((millis() - wolfram_datadrop_timer_last > wolfram_datadrop_timeout) && processCurl.running()) // Terminate process if timeout
    {
      log_file.print("["); log_file.print(MILLITOSEC(millis())); log_file.println("] Terminated.");
      processCurl.flush();
      processCurl.close();      
    }
    else if (processCurl.available())
    {
      log_file.print("["); log_file.print(MILLITOSEC(millis())); log_file.print("] ");
      
      int i = 0;
      
      while (processCurl.available() && i < WOLFRAM_DATADROP_LOG_LINEMAX)
      {
        log_file.write(processCurl.read());
        i++;
      }
      
      while (processCurl.available())
        processCurl.read();

      log_file.println("...");
      
      processCurl.flush();
      processCurl.close();
    }

    log_file.close();
  }
}


//
// UI functions
//

// Emulating backspace (to remove the last digit from float value)
#define lcd_backspace { lcd.rightToLeft(); lcd.write(" "); lcd.leftToRight(); }

void display_line(int i, int line)
{
  lcd.setCursor(0, line);

  switch (i) {
    case 0:
      // Shorten day names
      time_string.replace("Mon", "Mo");
      time_string.replace("Tue", "Tu");
      time_string.replace("Wed", "We");
      time_string.replace("Thu", "Th");
      time_string.replace("Fri", "Fr");
      time_string.replace("Sat", "Sa");
      time_string.replace("Sun", "Su");
      
      lcd.print("@ ");
      lcd.print(time_string.substring(0, 14)); // Removing byte(0) at the end (without it, it displays as byte(0) icon)
      lcd.write(" ");
      break;
      
    case 1: // Temperature (C & F)
      lcd.write(CHAR_TEMP);
      if (IS_VALID_TEMP)
      {
        if ((temp_c < 100) && (temp_c >= 0)) // No space for 100th digit and minus
          lcd.write(" ");
        lcd.print(temp_c);
        lcd_backspace;
        }
      else
        lcd.print(" --.-");
      
      lcd.write(CHAR_DEG);
      lcd.write("C");
      lcd.write(" ");

      if (IS_VALID_TEMP)
      {
        lcd.print(temp_f);
        lcd_backspace;
      }
      else
        lcd.print(" --.-");
        
      lcd.write(CHAR_DEG);
      lcd.write("F");
       break;

    case 2: // Humidity and ambient light
      lcd.write(CHAR_HUMID);
      if (IS_VALID_HUMIDITY)
      {
        if (humidity < 100) // No space for 100th digit
          lcd.write(" ");
        lcd.print(humidity);
        lcd_backspace;
      }
      else
        lcd.print(" --.-");
        
      lcd.print("%  ");

      lcd.write(CHAR_LIGHT);
      if (IS_VALID_LIGHT)
      {
        if (light < 100) // No space for 100th digit
          lcd.write(" ");
        lcd.print(light);
        lcd_backspace;        
      }
      else
        lcd.print(" --.-");
      
      lcd.write("%");
      break;

    case 3: // Pressure
      lcd.write(CHAR_PRESS);
      lcd.write(" ");
      if (IS_VALID_PRESSURE)
        lcd.print(pressure);
      else
        lcd.print("-----.--");
        
      lcd.print(" Pa");
      break;

#if defined(_AXDL335_) || defined(_LSM9DS1_)
    case 4: // Acceleration events
      lcd.print("~ ");
      lcd.print("No event");
      break;
#endif
    
    case (SCREEN_LINE_MAX - 1): // Data Drop info
      lcd.write(CHAR_WOLFRAM);
      lcd.write(" ");
      if (wolfram_datadrop_ready)
      {
        lcd.print((unsigned long)((millis() - wolfram_datadrop_timer_last) / 1000));
        lcd.print("s ago");
      }
      else
        lcd.print("Not available"); 
  }
}

void setup()
{
  //
  // Configure LCD
  //
  lcd.begin(16, 2); // LCD's number of columns and rows:

  // Set up LCD special characters
  lcd.createChar(CHAR_TEMP, temp_icon);
  lcd.createChar(CHAR_HUMID, humid_icon);
  lcd.createChar(CHAR_DEG, degree_icon);
  lcd.createChar(CHAR_LIGHT, light_icon);
  lcd.createChar(CHAR_PRESS, pressure_icon);
  lcd.createChar(CHAR_WOLFRAM, wolfram_icon);
  lcd.clear();

  // Initiate I2C
  Wire.begin();

  //
  // Configure weather sensors
  //
#ifdef _WEATHER_SHIELD_
  pinMode(WS_STAT1, OUTPUT); //Status LED Blue
  pinMode(WS_STAT2, OUTPUT); //Status LED Green
  pinMode(WS_OP_VOL, INPUT);
#endif

  // Configure light sensor
  pinMode(SENSOR_LIGHT, INPUT); // Light sensor I/O

  //Configure the pressure sensor
  sensorPressure.begin(); // Get sensor online
  sensorPressure.setModeBarometer(); // Measure pressure in Pascals from 20 to 110 kPa
  sensorPressure.setOversampleRate(7); // Set Oversample to the recommended 128
  sensorPressure.enableEventFlags(); // Enable all three pressure and temp event flags

  //Configure the humidity sensor
  sensorHumidity.begin();

#ifdef _AXDL335_
  pinMode(SENSOR_ACCEL_X, INPUT);
  pinMode(SENSOR_ACCEL_Y, INPUT);
  pinMode(SENSOR_ACCEL_Z, INPUT);  
#endif

#ifdef _LSM9DS1_
  // Configure IMU
  sensorIMU.settings.device.commInterface = IMU_MODE_I2C;
  sensorIMU.settings.device.mAddress = LSM9DS1_M;
  sensorIMU.settings.device.agAddress = LSM9DS1_AG;
#endif

  // Display initialization message
  lcd.setCursor(0, 0);
  lcd.print("Data Collector");
  lcd.setCursor(0, 1);
  lcd.print("Initializing..");
  lcd.blink();

  // Start bridge
  Bridge.begin();
  FileSystem.begin();

  // Initialize Wolfrma Data Drop 
  wolfram_datadrop_init();

  lcd.noBlink();
}

void loop()
{
  if (!processDate.running()) {
    processDate.begin("date");
    processDate.addParameter("+%R %a %m/%d"); // "23:00 Mon 06/12" format
    processDate.runAsynchronously();
  }

  while (processDate.available())
  {
    time_string = processDate.readString();
    get_weather();

    lcd.clear();
    display_line(screen % SCREEN_LINE_MAX, 0);
    display_line((screen + 1) % SCREEN_LINE_MAX, 1);

    screen = (screen + 1) % SCREEN_LINE_MAX;
    wolfram_datadrop_update();
    delay(SCROLL_DELAY);
  }
}
