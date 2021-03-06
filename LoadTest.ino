// Load Tester by Adam Oakley
// This program controls the load tester hardware. It handles checking pushbutton inputs,
// outputting information to a 16x2 character LCD and controlling the constant-current load
// circuit and analog data sampling
//
// Schematic: https://github.com/Addy771/LoadTest/blob/master/schematic.png


#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h>	// Albert Phan's fork of Bounce2 library by thomasfredericks https://github.com/AlbertPhan/Bounce2
#include <EEPROM.h>


#define SW_VERSION "2.3"
#define LOOP_INTERVAL 10		// Main code loop period in milliseconds
#define DISPLAY_INTERVAL 1000	// Display refresh period in milliseconds 
#define DEBOUNCE_TIME 20		// How many milliseconds must pass before the button is considered stable
#define REPEAT_INTERVAL 200		// When the button is held down, it takes this long between inc/dec in milliseconds
#define NUM_SAMPLES	32			// Size of circular buffers for analog samples
#define BUFFER_OPERATOR	0x1F	// Mask used to limit buffer index (0x1F: 0-31)
#define CURRENT_STEP 50			// Amount to step current (mA) per button press
#define VOLTAGE_STEP 50			// Amount to step voltage (mV) per button press

// I/O connections
#define LOAD A2
#define SHUNT A1
#define PWMOUT 6
#define DOWN_BTN 8
#define UP_BTN 9
#define YES_BTN 2

// Measured hardware parameters
#define VREF	1.078f
#define VCC		4.64f
#define SHUNT_R 2.01f

#define _2V ((2.0f / VCC) * 255)	// PWM value equivalent to 2V
#define MAX_LOAD_V (VREF / 0.0753)	// Maximum measureable voltage of load terminals ******** explain magic number
#define MAX_SHUNT_V (VREF / 0.549)	// Maximum measureable voltage of shunt resistor
#define MIN_CURRENT 50				// Minimum allowed test current in milliamps
#define MAX_CURRENT 1000			// Maximum allowed test current in milliamps
#define MAX_CUTOFF	14000			// Maximum cutoff voltage in millivolts
#define DEFAULT_CUTOFF 11400			// Default cutoff voltage in millivolts
#define DEFAULT_CURRENT 1000			// Default test current in milliamps

// EEPROM addresses
#define EE_IN_USE 0
#define EE_CUTOFF_V 2
#define EE_TEST_CURR 4

#define MILLIS_SEC 1000UL // how many ms in a second
#define MILLIS_MIN (MILLIS_SEC * 60UL) // how many ms in a minute
#define MILLIS_HOUR (MILLIS_MIN * 60UL) // How many ms in an hour


LiquidCrystal_I2C lcd(0x27, 16, 2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

enum { INTRO, CONFIG, CONFIRM, TEST, RESULT };	// State machine values
enum { CUTOFF, CURRENT };						// Selected field values
enum { YES, NO };								// Other selectable values
enum { NEW, RESTART };

void drawInitScreen();
void drawConfigScreen(unsigned int batteryMV, unsigned int testCurrent, unsigned char selectedField);
void drawConfirmScreen(unsigned int batteryMV, unsigned int testCurrent, unsigned char selectedField);
void drawTestScreen(unsigned int batteryMV, unsigned int testCurrent, unsigned int capacity);
void drawResultScreen(unsigned int capacity, unsigned char selectedField);

void clearBuffer(unsigned int * buffer);

Bounce yesBtn(YES_BTN, DEBOUNCE_TIME, REPEAT_INTERVAL);
Bounce upBtn(UP_BTN, DEBOUNCE_TIME, REPEAT_INTERVAL);
Bounce downBtn(DOWN_BTN, DEBOUNCE_TIME, REPEAT_INTERVAL);

unsigned long int loopTime;		// Saves the time when the main loop ran
unsigned long int auxTime;		// Other timers
unsigned char state;			// State machine state
unsigned char enterFlag;		// Flag set when entering a state

unsigned int loadVoltages[NUM_SAMPLES];		// Buffer for load terminal voltage samples
unsigned int shuntVoltages[NUM_SAMPLES];	// Buffer for shunt resistor voltage samples
unsigned char loadIndex;					// Index for load voltage buffer
unsigned char shuntIndex;					// Index for shunt voltage buffer
unsigned int loadV;							// Load terminal voltage in mV
unsigned int shuntV;						// Shunt resistor voltage in mV
unsigned int testCurrent;					// Load current setpoint in mA
unsigned int cutoffV;						// Cutoff voltage in mV
unsigned char selectedItem;					// Indicates which item is being edited
unsigned char updateFlag;					// The display is updated when this flag is set
unsigned char confirmCount;
double capacity;							// Battery capacity in milliamp-seconds


void setup() {
  unsigned char eepromUsed;

  analogReference(INTERNAL);	// The internal 1.1V reference is needed for this circuit

  pinMode(PWMOUT, OUTPUT);	// Digital PWM output to R/C filter. Basic DAC

  // Pushbutton inputs
  pinMode(YES_BTN, INPUT_PULLUP);
  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(DOWN_BTN, INPUT_PULLUP);

  loadIndex = 0;
  shuntIndex = 0;

  // Disable load
  analogWrite(PWMOUT, 0);
  digitalWrite(PWMOUT, LOW);

  // Initialize LCD
  lcd.init();
  lcd.backlight();

  EEPROM.get(EE_IN_USE, eepromUsed);  // Read the "EEPROM in use" flag

  if (eepromUsed != 1)
  {
    EEPROM.put(EE_IN_USE, 1); // Set the "EEPROM in use" flag
    EEPROM.put(EE_CUTOFF_V, DEFAULT_CUTOFF);  // Initialize the cutoff threshold by storing the default value
    EEPROM.put(EE_TEST_CURR, DEFAULT_CURRENT);  // Initialize the load current by storing the default value
  }


  // Check pushbuttons
  yesBtn.update();
  upBtn.update();
  downBtn.update();

  // Begin with the INTRO state
  state = INTRO;
  enterFlag = 1;
  loopTime = millis();


}



void loop() {


  if (millis() - loopTime >= LOOP_INTERVAL)
  {
    loopTime += LOOP_INTERVAL;	// The timer will go off in LOOP_INTERVAL milliseconds

    loadVoltages[loadIndex] = analogRead(LOAD);			// Take a load voltage sample and store it in the circular buffer
    loadIndex = (loadIndex + 1) & BUFFER_OPERATOR;		// Increment the buffer index and wrap around

    shuntVoltages[shuntIndex] = analogRead(SHUNT);		// Take a shunt voltage sample and store it in the circular buffer
    shuntIndex = (shuntIndex + 1) & BUFFER_OPERATOR;	// Increment the shunt index and wrap around

    yesBtn.update();
    upBtn.update();
    downBtn.update();	// Update button states

    switch (state)
    {
      case INTRO:
        if (enterFlag)
        {
          enterFlag = 0;

          // Make sure PWM output is disabled
          analogWrite(PWMOUT, 0);

          auxTime = millis();
          drawInitScreen();

        }

        // After 3s have passed, change to the CONFIG state
        if (millis() - auxTime >= 3000)
        {
          lcd.blink();	// Turn on blink after Battery Test screen splash.
          state = CONFIG;
          enterFlag = 1;
        }

        break;


      case CONFIG:
        if (enterFlag)
        {
          enterFlag = 0;
          selectedItem = CUTOFF;

          // init config variables
          EEPROM.get(EE_CUTOFF_V, cutoffV);   // Read the cutoff voltage stored in eeprom
          EEPROM.get(EE_TEST_CURR, testCurrent);  // Read the test current stored in eeprom

          updateFlag = 1;
        }

        // Handle button actions when the CUTOFF item is selected
        if (selectedItem == CUTOFF)
        {
          if (yesBtn.fell() || yesBtn.retrigger())
          {
            selectedItem = CURRENT;
            updateFlag = 1;
          }

          if (cutoffV < MAX_CUTOFF && (upBtn.fell() || upBtn.retrigger()))
          {
            cutoffV += VOLTAGE_STEP;
            updateFlag = 1;
          }
          else if (cutoffV > 0 && (downBtn.fell() || downBtn.retrigger()))
          {
            cutoffV -= VOLTAGE_STEP;
            updateFlag = 1;
          }
        }
        // Handle button actions when the CURRENT item is selected
        else if (selectedItem == CURRENT)
        {

          if (yesBtn.fell())
          {
            enterFlag = 1;
            state = CONFIRM;
          }
          if (testCurrent < MAX_CURRENT && (upBtn.fell() || upBtn.retrigger()))
          {
            testCurrent += CURRENT_STEP;
            updateFlag = 1;
          }
          else if (testCurrent > MIN_CURRENT && (downBtn.fell() || downBtn.retrigger()))
          {
            testCurrent -= CURRENT_STEP;
            updateFlag = 1;
          }
        }

        // If the update flag is set, redraw the screen
        if (updateFlag)
        {
          updateFlag = 0;
          drawConfigScreen(cutoffV, testCurrent, selectedItem);
        }

        break;


      case CONFIRM:
        if (enterFlag)
        {
          enterFlag = 0;
          selectedItem = CUTOFF;
          updateFlag = 1;

          // Stored the selected values in EEPROM for next time
          EEPROM.put(EE_CUTOFF_V, cutoffV);
          EEPROM.put(EE_TEST_CURR, testCurrent);

        }

        // Handle button actions when the YES item is selected
        if (selectedItem == YES)
        {
          if (upBtn.fell() || downBtn.fell())
          {
            selectedItem = NO;
            updateFlag = 1;
          }

          if (yesBtn.fell())
          {
            enterFlag = 1;
            state = TEST;
          }
        }
        // Handle button actions when the NO item is selected
        else if (selectedItem == NO)
        {
          if (upBtn.fell() || downBtn.fell())
          {
            selectedItem = YES;
            updateFlag = 1;
          }

          if (yesBtn.fell())
          {
            enterFlag = 1;
            state = CONFIG;
          }
        }

        // If the update flag is set, redraw the screen
        if (updateFlag)
        {
          updateFlag = 0;
          drawConfirmScreen(cutoffV, testCurrent, selectedItem);
        }

        break;


      case TEST:
        if (enterFlag)
        {
          enterFlag = 0;
          confirmCount = 0;
          auxTime = millis();

          // Reset analog sample buffers
          clearBuffer(loadVoltages);
          clearBuffer(shuntVoltages);

          drawTestScreen(0, 0, 0);
          capacity = 0.0;	// Set the capacity measurement to 0 when the test starts

          // Set the analog output so the load draws the set current (0-2V = 0-1000mA)
          analogWrite(PWMOUT, map(testCurrent, 0, MAX_CURRENT, 0, _2V));

        }

        // Execute this code every 250ms
        if (millis() - auxTime >= 250)
        {
          auxTime += 250;

          // Average the buffer of samples and convert ADC counts into millivolts
          loadV = (averageBuffer(loadVoltages) / 1.023) * MAX_LOAD_V;
          shuntV = (averageBuffer(shuntVoltages) / 1.023) * MAX_SHUNT_V;

          float current = shuntV / SHUNT_R + 0.5f;
          capacity += current / 4;	// Add to the capacity the amount of current that has been drained in 1/4 of a second (250ms)

          // Capacity (mAh) = Capacity (mAs) / 3600
          drawTestScreen(loadV, current, capacity / 3600);

          // Increment a counter each consecutive time the battery is below the threshold
          if (loadV < cutoffV)
          {
            confirmCount++;
          }
          else
          {
            confirmCount = 0;
          }

          // If the battery voltage was below the threshold 5 times in a row, end the test
          if (confirmCount >= 5)
          {
            // Disable load
            analogWrite(PWMOUT, 0);
            enterFlag = 1;
            state = RESULT;
          }


        }

        break;


      case RESULT:
        if (enterFlag)
        {
          enterFlag = 0;
          selectedItem = NEW;
          updateFlag = 1;

        }

        // Handle button actions when the YES item is selected
        if (selectedItem == NEW)
        {
          if (downBtn.fell() || upBtn.fell())
          {
            selectedItem = RESTART;
            updateFlag = 1;
          }

          if (yesBtn.fell())
          {
            enterFlag = 1;
            state = CONFIG;
          }
        }
        // Handle button actions when the NO item is selected
        else if (selectedItem == RESTART)
        {
          if (downBtn.fell() || upBtn.fell())
          {
            selectedItem = NEW;
            updateFlag = 1;
          }

          if (yesBtn.fell())
          {
            enterFlag = 1;
            state = CONFIRM;
          }
        }

        if (updateFlag)
        {
          updateFlag = 0;
          drawResultScreen(capacity / 3600, selectedItem);
        }

        break;

    }
  }

}



// Battery Tester
//      V2.0
//0123456789ABCDEF
void drawInitScreen()
{
  char sBuffer[16];

  sprintf(sBuffer, "      V%s", SW_VERSION);

  lcd.clear();
  lcd.noCursor();  // Disable cursor (underscore)

  lcd.print(" Battery Tester ");
  lcd.setCursor(0, 1);    // Move cursor to second line
  lcd.print(sBuffer);
}

//Cutoff: XX.XXV
//Load: XXXXmA
//0123456789ABCDEF
void drawConfigScreen(unsigned int batteryMV, unsigned int testCurrent, unsigned char selectedField)
{
  char sBuffer1[16];
  char sBuffer2[16];

  unsigned int volts = batteryMV / 1000;
  batteryMV -= volts * 1000;

  // Create string of cutoff voltage by splitting into volts and tens of millivolts
  sprintf(sBuffer1, "Cutoff: %i.%02iV", volts, batteryMV / 10);

  // Create load current string
  sprintf(sBuffer2, "Load: %imA", testCurrent);

  lcd.clear();
  lcd.cursor();  // Enable cursor (underscore)

  lcd.print(sBuffer1);
  lcd.setCursor(0, 1);    // Move cursor to second line
  lcd.print(sBuffer2);

  if (selectedField == 0)
    lcd.setCursor(6, 0);
  else if (selectedField == 1)
    lcd.setCursor(4, 1);
  else
    lcd.noCursor();  // Disable cursor (underscore)
}

//Cutoff: XX.XXV @
//XXXXmA. OK? Y/N
//0123456789ABCDEF
void drawConfirmScreen(unsigned int batteryMV, unsigned int testCurrent, unsigned char selectedField)
{
  char sBuffer1[16];
  char sBuffer2[16];

  unsigned int volts = batteryMV / 1000;
  batteryMV -= volts * 1000;

  // Create string of cutoff voltage by splitting into volts and tens of millivolts
  sprintf(sBuffer1, "Cutoff: %2i.%02iV @", volts, batteryMV / 10);

  // Create load current string
  sprintf(sBuffer2, "%imA.", testCurrent);

  lcd.clear();
  lcd.cursor();  // Enable cursor (underscore)

  lcd.print(sBuffer1);
  lcd.setCursor(0, 1);    // Move cursor to second line
  lcd.print(sBuffer2);
  lcd.setCursor(8, 1);
  lcd.print("OK? Y/N");

  if (selectedField == 0)
    lcd.setCursor(12, 1);
  else if (selectedField == 1)
    lcd.setCursor(14, 1);
  else
    lcd.noCursor();  // Disable cursor (underscore)
}

//XX.XXV, XXXXmA
//XXXXmAh.
//0123456789ABCDEF
void drawTestScreen(unsigned int batteryMV, unsigned int testCurrent, unsigned int capacity)
{
  char sBuffer1[16];
  char sBuffer2[16];

  unsigned int volts = batteryMV / 1000;
  batteryMV -= volts * 1000;

  // Create string of battery voltage and current
  sprintf(sBuffer1, "%i.%02iV, %imA", volts, batteryMV / 10, testCurrent);

  // Create capacity string
  sprintf(sBuffer2, "%imAh. ", capacity);

  lcd.clear();


  lcd.print(sBuffer1);
  lcd.setCursor(0, 1);    // Move cursor to second line
  lcd.print(sBuffer2);
  lcd.noCursor();  // Disable cursor (underscore)
}

//Done! XXXXmAh
//New/Restart
//0123456789ABCDEF
void drawResultScreen(unsigned int capacity, unsigned char selectedField)
{
  char sBuffer[16];

  // Create capacity result string
  sprintf(sBuffer, "Done. %imAh", capacity);

  lcd.cursor();  // Enable cursor (underscore)
  lcd.clear();



  lcd.print(sBuffer);
  lcd.setCursor(0, 1);    // Move cursor to second line
  lcd.print("New/Restart");

  if (selectedField == 0)
    lcd.setCursor(0, 1);
  else if (selectedField == 1)
    lcd.setCursor(4, 1);
  else
    lcd.setCursor(16, 0); // Hide cursor

}

// Fill a sample buffer with zeroes
void clearBuffer(unsigned int * buffer)
{
  unsigned int i;

  for (i = 0; i < NUM_SAMPLES; i++)
    buffer[i] = 0;
}

// Return the average of a buffer
unsigned int averageBuffer(unsigned int * buffer)
{
  unsigned int i;
  unsigned long int sum;

  for (i = 0; i < NUM_SAMPLES; i++)
    sum += buffer[i];

  return sum / NUM_SAMPLES;
}
