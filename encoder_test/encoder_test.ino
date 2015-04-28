/* Rotary encoder read example */
#define ENC_A 8
#define ENC_B 9
#define ENC_PORT PINB
 
void setup()
{
  /* Setup encoder pins as inputs */
  pinMode(ENC_A, INPUT);

  pinMode(ENC_B, INPUT);

  Serial.begin (115200);
  Serial.println("Start");
}

unsigned long int startTime = 0;
 
void loop()
{
  static uint8_t counter = 0;      //this variable will be changed by encoder input
  static int8_t subcnt = 0;
  static uint8_t displayFlag = 0;
 
 
  int8_t tmpdata;
 
  tmpdata = read_encoder();
  counter += tmpdata;
  
  
  // Records how much time passed since the subcount was 0. If too much time passed, reset
  if (subcnt == 0)
    startTime = millis();
  else if (subcnt != 0 && (millis() - startTime) > 200) 
    subcnt = 0;
  
  
  if (subcnt >= 2)
  {
    subcnt = 0;
    counter++;
    displayFlag = 1;
  }
  else if (subcnt <= -2)
  {
    subcnt = 0;
    counter--;
    displayFlag = 1;
  }  
  
  if( tmpdata != 0 ) {
    Serial.print("Counter value: ");
    Serial.println(counter, DEC);
    displayFlag = 0;
  }
}
 
/* returns change in encoder state (-1,0,1) */
int8_t read_encoder()
{
  static int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  static uint8_t old_AB = 0;
  /**/
  old_AB <<= 2;                   //remember previous state
  old_AB |= ( ENC_PORT & 0x03 );  //add current state
  return ( enc_states[( old_AB & 0x0f )]);
}
