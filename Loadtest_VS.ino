// Load Tester by Adam Oakley
// This program controls the load tester hardware. It handles checking pushbutton inputs,
// outputting information to a 16x2 character LCD and controlling the constant-current load
// circuit and analog data sampling

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h>


#define SW_VERSION "2.0"
#define LOOP_INTERVAL 10		// Main code loop period in milliseconds
#define DISPLAY_INTERVAL 1000	// Display refresh period in milliseconds 
#define DEBOUNCE_TIME 20		// How many milliseconds must pass before the button is considered stable
#define REPEAT_INTERVAL 50		// When the button is held down, it takes this long between inc/dec in milliseconds
#define NUM_SAMPLES	32			// Size of circular buffers for analog samples
#define BUFFER_OPERATOR	0x1F	// Mask used to limit buffer index (0x1F: 0-31)

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
#define DEFAULT_CUTOFF 3000			// Default cutoff votlage in millivolts
#define DEFAULT_CURRENT 100			// Default test current in milliamps

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

Bounce yesBtn(YES_BTN, DEBOUNCE_TIME);
Bounce upBtn(UP_BTN, DEBOUNCE_TIME);
Bounce downBtn(DOWN_BTN, DEBOUNCE_TIME);

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
double capacity;


void setup() {

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

	yesBtn.retriggerinterval(REPEAT_INTERVAL);
	upBtn.retriggerinterval(REPEAT_INTERVAL);
	downBtn.retriggerinterval(REPEAT_INTERVAL);

	// Initialize LCD
	lcd.init();
	lcd.backlight();
	lcd.blink();

	// Check pushbuttons
	yesBtn.update();
	upBtn.update();
	downBtn.update();

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

		// Check pushbuttons. If their states have changed, update the display

		yesBtn.update();
		upBtn.update();
		downBtn.update();



		switch (state)
		{
		case INTRO:
			if (enterFlag)
			{
				enterFlag = 0;

				// Make sure PWM output is disabled
				analogWrite(PWMOUT, 0);
				//digitalWrite(PWMOUT, LOW);

				auxTime = millis();
				drawInitScreen();

			}

			if (millis() - auxTime >= 3000)
			{
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
				cutoffV = DEFAULT_CUTOFF;
				testCurrent = DEFAULT_CURRENT;

				drawConfigScreen(cutoffV, testCurrent, selectedItem);
			}

			// Handle button actions when the CUTOFF item is selected
			if (selectedItem == CUTOFF)
			{
				if (yesBtn.fell())
				{
					selectedItem = CURRENT;
					updateFlag = 1;
				}

				if (cutoffV < MAX_CUTOFF && upBtn.fell())
				{
					cutoffV += 50;
					updateFlag = 1;
				}
				else if (cutoffV > 0 && downBtn.fell())
				{
					cutoffV -= 50;
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
				if (testCurrent < MAX_CURRENT && upBtn.fell())
				{
					testCurrent += 5;
					updateFlag = 1;
				}
				else if (testCurrent > MIN_CURRENT && downBtn.fell())
				{
					testCurrent -= 5;
					updateFlag = 1;
				}
			}

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
				auxTime = millis();

				// Reset analog sample buffers
				clearBuffer(loadVoltages);
				clearBuffer(shuntVoltages);

				drawTestScreen(0, 0, 0);
				capacity = 0.0;

				// Set the analog output so the load draws the set current (0-2V = 0-1000mA)
				analogWrite(PWMOUT, map(testCurrent, 0, MAX_CURRENT, 0, _2V));

				//********************************* Start load, start timer
			}

			if (millis() - auxTime >= 250)
			{
				auxTime += 250;

				loadV = (averageBuffer(loadVoltages) / 1.023) * MAX_LOAD_V;		// Average the buffer of samples and convert ADC counts into millivolts
				shuntV = (averageBuffer(shuntVoltages) / 1.023) * MAX_SHUNT_V;

				float current = shuntV / SHUNT_R + 0.5f;
				capacity += current / 4;	// Add to the capacity the amount of current that has been drained in 1/4 of a second (250ms)

				// Capacity (mAh) = Capacity (mAs) / 3600
				drawTestScreen(loadV, current, capacity / 3600);

				if (loadV < cutoffV)
				{
					// Disable load
					analogWrite(PWMOUT, 0);
					digitalWrite(PWMOUT, LOW);
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






//*****************************************************************************//

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