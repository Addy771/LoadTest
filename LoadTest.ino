#define POTPIN A0
#define BATTERY A2
#define LOAD A1
#define PWMOUT 6
#define LEDPIN 13

#define VREF	1.078f
#define VCC		4.64f 
#define SHUNT_R 2.01f

// Cutoff voltages for each battery chemistry. Load testing will end when the battery reaches this voltage
#define LIPO_CUTOFF     3.2f  // 3.2V per cell 
#define LIFE_CUTOFF     2.5f  // 2.5V per cell 
#define LEADACID_CUTOFF 5.25f // 5.25V for a 6V battery, equal to 1.75V per cell



#define DOWN_BTN 8
#define UP_BTN 9
#define YES_BTN 2

#define DEBOUNCE_TIME 20   // How many ms must pass before the button is considered stable
#define REPEAT_INTERVAL 50 // When the button is held down, it takes this long between inc/dec
#define FLASH_TIME 500     // How often to flash the time when the test is done 

#define MILLIS_SEC 1000UL // how many ms in a second
#define MILLIS_MIN (MILLIS_SEC * 60UL) // how many ms in a minute
#define MILLIS_HOUR (MILLIS_MIN * 60UL) // How many ms in an hour

#define _2V ((2.0f / VCC) * 255)  // (2V / 4.64V) * 255
#define MAXCELLS 3
#define MINCURRENT 50
#define MAXCURRENT 1000
#define LOOPDIV 200 // How many ms between main loop execution

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h>


LiquidCrystal_I2C lcd(0x27,16,2);  // set the LCD address to 0x27 for a 16 chars and 2 line display

void drawInitMenu(uint8_t chemistry_s, uint8_t cells_s, uint16_t current_s);
void drawConfirmMenu(uint8_t chemistry_s, uint8_t cells_s, uint16_t current_s);

enum { lipo, sla, life };
enum { chemistry, cells, mA };	
enum { go, quit };	
enum { InitMenu, ConfirmMenu, TestScreen, QuitScreen, FinishScreen };	
	
char *bStrings[] =
{
	"LiPo     ",
	"Lead Acid",
	"LiFe     "
};

uint8_t batteryType = lipo;
uint8_t numCells = 1;
uint16_t testCurrent = 500;


Bounce yesBtn(YES_BTN, DEBOUNCE_TIME);
Bounce upBtn(UP_BTN, DEBOUNCE_TIME);
Bounce downBtn(DOWN_BTN, DEBOUNCE_TIME);

void setup()
{
	analogReference(INTERNAL);

	pinMode(PWMOUT, OUTPUT);
	pinMode(YES_BTN, INPUT);
	pinMode(LEDPIN, OUTPUT);
	pinMode(UP_BTN, INPUT);
	pinMode(DOWN_BTN, INPUT);
	
	
	lcd.init();                      // initialize the lcd
	
	// Print a message to the LCD.
	lcd.backlight();
	//lcd.cursor();
	lcd.blink();
	
	Serial.begin(57600);
}

float current;
float battV;
float loadV;
float cutoffV = 0.0f;
double capacity;


uint32_t battSum;
uint32_t loadSum;
uint8_t menuScreen = InitMenu;    // Current menu
uint8_t initMenuSel = chemistry;   // What item on the init menu is selected, default is first
uint8_t confirmSel = 1;    // What item on the confirm menu is selected

uint16_t loopDiv = 0;
uint8_t updateFlag = 1;
uint32_t previous_millis = 0;
uint32_t testStart;
uint32_t dispTime;
uint32_t flashTimer = 0;
uint8_t timeFlag = 0;
uint8_t testDone = 0;
uint8_t enableLoad = 0;

uint8_t IncFlag = 0;
uint8_t DecFlag = 0;

void loop()
{
	
	if (loopDiv >= LOOPDIV)
	{
		// This section of code runs once every 100ms
		loopDiv = 0;

        // Calculates average ADC reading
		battV = (battSum / (LOOPDIV+1)) / 1023.0 * (VREF / 0.0753);
		//battV = analogRead(A2) / 1023.0 * (VREF / 0.0753);
		loadV = (loadSum / (LOOPDIV+1)) / 1023.0 * (VREF / 0.549);
		
		
	
	


			

		if (enableLoad)
		{
			analogWrite(PWMOUT, map(testCurrent, 0, MAXCURRENT, 0, _2V) - 1);
			/*
			Serial.print("Cutoff: ");
			Serial.print(cutoffV);
			Serial.print(" Capacity: ");
			Serial.print(capacity);
			Serial.println(" mA*s");	 */		
		}
		else
		{
			analogWrite(PWMOUT, 0); 
			digitalWrite(PWMOUT, LOW);
		}

		//analogSum = 0; // Reset the sums so we can start a new average calculation
		battSum = 0;
		loadSum = 0;
		
	}
	else
	{
		loopDiv++;
	}
	
	// This section of code runs every 1ms
	
	
	/////////////// Pushbuttons /////////////////
	yesBtn.update();
	upBtn.update();
	downBtn.update();

	if (upBtn.held() || downBtn.held() && initMenuSel == mA)
	{
		if (previous_millis != 0)
		{
			if (millis() - previous_millis >= REPEAT_INTERVAL)
			{
				if (upBtn.held())
					IncFlag = 1;
				else if (downBtn.held())
					DecFlag = 1;
					
				previous_millis = millis();
			}
		}
		else
		{
			previous_millis = millis();
		}
	}

	////////////// LCD drawing section //////////////
		

	if (menuScreen == InitMenu)  // init screen
	{
		if (initMenuSel == chemistry) // Battery Type
		{
				
				
			if (upBtn.rose() && batteryType < life)
			{
				updateFlag = 1;
				batteryType++; // increment battery type when encoder is rotated right
			}
			else if (downBtn.rose() && batteryType > 0)
			{
				updateFlag = 1;
				batteryType--; // decrement battery type when encoder is rotated right
			}
		}
			
		if (initMenuSel == cells) // # of cells
		{

			if (batteryType == sla)
			{
				if (upBtn.rose() && numCells == 1)
				{
					updateFlag = 1;
					numCells = 2;
				}
				else if (downBtn.rose() && numCells == 2)
				{
					updateFlag = 1;
					numCells = 1;
				}	
				
				if (numCells > 2)	
					numCells = 2;
			}
			else
			{
				if (upBtn.rose() && numCells < MAXCELLS)
				{
					updateFlag = 1;
					numCells++;
				}
				else if (downBtn.rose() && numCells > 1)
				{
					updateFlag = 1;
					numCells--;
				}			
			}
			
				

		}

			
		if (initMenuSel == mA) // Load current
		{
				
				
			if ((upBtn.rose() || IncFlag) && testCurrent < MAXCURRENT)
			{
				updateFlag = 1;
				testCurrent += 10;
			}
			else if ((downBtn.rose() || DecFlag) && testCurrent > MINCURRENT)
			{
				updateFlag = 1;
				testCurrent -= 10;
			}
			
			IncFlag = 0;
			DecFlag = 0;
		}
		
		if (updateFlag)
		{
			updateFlag = 0;		
			drawInitMenu(batteryType, numCells, testCurrent);
		}
		
		if (yesBtn.rose() && initMenuSel < mA)
		{
			updateFlag = 1;  // When the button was pressed, move the cursor to the next
			initMenuSel++;   // field as long as it's not at the end yet
		} else if (yesBtn.rose() && initMenuSel == mA)
		{
			menuScreen = ConfirmMenu;
			initMenuSel = chemistry;
			lcd.clear();
			//lcd.noBlink();
			updateFlag = 1;
		}		
			
		switch (initMenuSel)
		{
			case 0:
			lcd.setCursor(4, 0); // put the cursor before the Type field
			break;
			case 1:
			lcd.setCursor(5, 1); // put the cursor before the Cells field
			break;
			case 2:
			lcd.setCursor(11, 1); // put the cursor before the mA field
			break;
		}
			
	}
	else if (menuScreen == ConfirmMenu) // confirmation screen
	{
		
		if (upBtn.rose() && confirmSel == go)
		{
			confirmSel = quit;
			updateFlag = 1;
		}
			
		if (downBtn.rose() && confirmSel == quit)
		{
			confirmSel = go;
			updateFlag = 1;
		}
			
		if (yesBtn.rose() && confirmSel == quit)
		{
			menuScreen =InitMenu;
			lcd.clear();
			updateFlag = 1;			
		}
		
		if (yesBtn.rose() && confirmSel == go) // Start test confirmed
		{
			menuScreen = TestScreen;
			lcd.clear();
			updateFlag = 1;
			testDone = 0;
			capacity = 0.0;
			lcd.noCursor();
			lcd.noBlink();	
			
			Serial.println("Elapsed Time (ms),Battery Voltage (V), Current Draw (mA)");
					
			testStart = millis();
			enableLoad = 1;
			
			switch(batteryType)
			{
				case lipo:
					cutoffV = LIPO_CUTOFF * numCells;
					break;
				case sla:
					cutoffV = LEADACID_CUTOFF * numCells;
					break;
				case life:
					cutoffV = LIFE_CUTOFF * numCells;
					break;				
			}

		}

		if (updateFlag && menuScreen == ConfirmMenu)
		{
			updateFlag = 0;	
			
			drawConfirmMenu(batteryType, numCells, testCurrent);
		}
		
		if (confirmSel == 0)   // move the cursor over the "Go/Quit" options
			lcd.setCursor(8, 1);
		else
			lcd.setCursor(11, 1);		
	} 
	else if (menuScreen == TestScreen)
	{
/////////////////////////////////////////////// LOAD TESTING ////////////////////////////////////////////////////////		
		
		if (enableLoad)
			dispTime = millis() - testStart;
			
		if (battV < cutoffV && enableLoad)
		{
			enableLoad = 0;
			testDone = 1;
			current = capacity / 3600;
		}
		
		if (testDone && (millis() - flashTimer) > FLASH_TIME) // flashes the time when the test is done
		{
			updateFlag = 1;
			flashTimer = millis();
			timeFlag = !timeFlag;
		}
		
		if (enableLoad && millis() - flashTimer > 1000)
		{
			current = (loadV / (SHUNT_R / 1000)) + 0.5f;
			capacity += current * ((millis() - flashTimer) / 1000);  // Add to the sum of milliamp-seconds
			updateFlag = 1;
			flashTimer = millis();
			
			// Write out a line of data, separated with commas
			Serial.print(dispTime);
			Serial.print(',');
			Serial.print(battV);
			Serial.print(',');	
			Serial.println(current);
			
			
		}
		
		if (updateFlag)
		{
			drawTestMenu(battV, current, dispTime, testDone, timeFlag);
			updateFlag = 0;
		}

	}
		
	/////////////////////////////////////////////////	
	
	// Sum up a bunch of samples for averaging
	
	battSum += analogRead(A2);
	
	loadSum += analogRead(A1);

	
	delay(1);
	
}

// prints out the init menu and fills in the data fields
void drawInitMenu(uint8_t chemistry_s, uint8_t cells_s, uint16_t current_s)
{
	lcd.home();
	//lcd.clear();
	lcd.print("Type:");
	lcd.print(bStrings[chemistry_s]);

	lcd.setCursor(0, 1); // col 0, row 1
	
	if (chemistry_s == sla) // if it's an SLA we need to display volts instead of cells
	{
		lcd.print("Volts:  ");
		lcd.setCursor(6, 1);
		lcd.print(cells_s * 6);
	}
	else
	{
		lcd.print("Cells:  ");
		lcd.setCursor(6, 1);		
		lcd.print(cells_s);
	}
	
	lcd.setCursor(9, 1);
	lcd.print("mA:");
	lcd.print("    ");    // Blank the space where current goes to clear previous number
	lcd.setCursor(12, 1);
	lcd.print(current_s);
}

void drawConfirmMenu(uint8_t chemistry_s, uint8_t cells_s, uint16_t current_s)
{
	lcd.home();
	if (chemistry_s == sla)
	{
		lcd.print(cells_s * 6);
		lcd.print("V ");
	}
	else
	{
		lcd.print(cells_s);
		lcd.print(" cell ");
	}
	
	//lcd.setCursor(7, 0);
	lcd.print(bStrings[chemistry_s]);
	
	lcd.setCursor(0, 1);
	lcd.print("@");
	lcd.print(current_s);
	lcd.print("ma ");
	
	lcd.setCursor(8, 1);
	lcd.print("Go/Quit?");
}

//XX.XXV xxxxmA
//xxh xxm xxs
//0123456789ABCDEF
void drawTestMenu(float batteryV, uint16_t current, uint32_t elapsed, uint8_t done, uint8_t showTime)
{

	char sBuffer[16];
	char vBuffer[7];
	uint8_t hours, mins, secs;
	
	hours = (elapsed / MILLIS_HOUR);
	
	mins = (elapsed / MILLIS_MIN) % 60;
	
	secs = (elapsed / MILLIS_SEC) % 60;
	
	lcd.home();
	
	dtostrf(batteryV, 5, 2, vBuffer);
	sprintf(sBuffer, "%sV %imA", vBuffer, (current));
	
	lcd.print(sBuffer);
	if (done)
		lcd.print("h");

	lcd.setCursor(0, 1);
	sprintf(sBuffer, "%02ih %02im %02is", hours, mins, secs);
	
	if (!done || showTime)
		lcd.print(sBuffer);
	else
		lcd.print("           ");
		
	
	
}




