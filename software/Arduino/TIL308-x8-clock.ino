// *** REMEMBER ///
// RTC operations in setup() are DISABLED!!

// (c) by Paolo CRAVERO IK1ZYW 2020. All rights reserved.
//
// No responsibility is taken for any use of this code,
// which is provided as an academic guidance to fellow builders.
// -------------------------------------------------------------

#include <Wire.h>
#include <EEPROM.h> // used to store DST status
#include <RTClib.h>

RTC_DS3231 rtc;
DateTime RTCnow;

// PIN DEFINITIONS
int inputs[4] = {A0, A1, A2, A3}; // A, B, C, D outputs

int latches[8] = {12, 9, 13, 8, 7, 6, 4, 1}; // latches LSD to MSD

int blanking[4] = {10, 11, 5, 3}; // blanking line for brightness control via PWM (must be PWM pins)

#define decimalPoint 0  

#define oneSecondInterruptPin 2
#define sensorPin A6 // LDR for intensity control
#define potPin A7 // pull to ground to set hours

// how many digits are present on the board?
//     [7] [6] [5] [4] [3] [2] [1] [0] 
#define DIGITMINPOS 0 // 0 is the first rightmost
#define DIGITMAXPOS 8 // total number of displays


// BEHAV OPTIONS
int blankMSD = 0;


// DO NOT TOUCH BELOW THIS LINE


#define DSTADDRESS 1 // EEPROM position for DST status
#define NIGHTMODEEE 10 // EEPROM position to store night mode setting
#define NIGHTMODESTARTEE 12 // EEPROM position to store night mode start time
#define NIGHTMODEENDEE 16 // // EEPROM position to store night mode end time
#define ENDOFNIGHTMODE 9 // end night mode at 9 AM. Integer
#define INTENSITYTHRESHOLDMID 20 // below this is night, above is day light
#define INTENSITYTHRESHOLDDAY 25 // below this is night, above is day light


byte seconds;
byte minutes;
byte hours;
byte weekday;
byte month_day;
byte month_nr;
byte year_nr;

bool secondElapsed = 0;
int  secondsElapsed = 30;
bool blinker = 0;

#define OFFBRI 0
#define MINBRI 20
#define MIDBRI 150
#define MAXBRI 255
#define RESETONTIMEDIFF 7
#define RESETONTIME 20
#define MAXONTIME 80
int  timerCountdown = MAXONTIME;

bool nightMode = 0; // set at power up, if set to 1 activates timed display during the day ****TBD
bool nightModeStayOn = 1;
int  nightModeStartHour;
int  nightModeEndHour;

byte BCD[16][4] = { // LSB to MSB
  {0, 0, 0, 0}, // 0
  {1, 0, 0, 0}, // 1
  {0, 1, 0, 0}, // 2
  {1, 1, 0, 0}, // 3
  {0, 0, 1, 0}, // 4
  {1, 0, 1, 0}, // 5
  {0, 1, 1, 0}, // 6
  {1, 1, 1, 0}, // 7
  {0, 0, 0, 1}, // 8
  {1, 0, 0, 1}, // 9
  {0, 1, 0, 1}, // A
  {1, 1, 0, 1}, // -
  {0, 0, 1, 1}, // C
  {1, 0, 1, 1}, // blank
  {0, 1, 1, 1}, // E
  {1, 1, 1, 1}  // F
};


int decToBcd(int val)
{
  return ( ((val/10)*16) + (val%10) );
}


int bcdToDec(int val)
{
  return ( val / 16 * 10 + val % 16 );
}


void updateDisplay(int myPosition, int myBCD, int dpOnOff=0) {

  
  if ((myPosition >= DIGITMINPOS) && (myPosition < DIGITMAXPOS)) { // just make sure we're not addressing a non-existant display
    for(int c = 0; c < 4; c++){
      digitalWrite(inputs[c], BCD[myBCD][c]);
    }
  
    if (dpOnOff == 0) {
      digitalWrite(decimalPoint, LOW);
    } else {
      digitalWrite(decimalPoint, HIGH);
    }
    
    // send a signal to the latch so that the display loads the data
    digitalWrite(latches[myPosition], HIGH);
    digitalWrite(latches[myPosition], LOW); 
    digitalWrite(latches[myPosition], HIGH);
  }
}



int fadeOut(int startingValue=250, int fadeStep=10, int fadeSpeed=25, int fadeStop=0)
{
  for (int j=startingValue; j>=fadeStop; j=j-fadeStep) {
    blankControl(j, j, j, j);
    delay(fadeSpeed);
  }

  if (fadeStop == 0) {
    blankControl(OFFBRI, OFFBRI, OFFBRI, OFFBRI);
  }

  return 0;
}


void fadeIn(int startingValue=0, int fadeStep=10, int fadeSpeed=25, int fadeStop=255)
{
  for (int j=startingValue; j<fadeStop; j=j+fadeStep) {
    blankControl(j, j, j, j);
    delay(fadeSpeed);
  }
}


void blankControl (int b3, int b2, int b1, int b0) {

  if (((nightModeStayOn==1)&&(nightMode == 1))||(nightMode == 0)) {
    if ( blankMSD == 1) {
      analogWrite(blanking[3], 255);
    } else {
      analogWrite(blanking[3], b3);
    }
    analogWrite(blanking[2], b2);
    analogWrite(blanking[0], b1);
    analogWrite(blanking[1], b0);
  } else {
    analogWrite(blanking[3], 255);
    analogWrite(blanking[2], 255);
    analogWrite(blanking[1], 255);
    analogWrite(blanking[0], 255);
  }
  
}


void printBCD(int myPosition, int myBCD, int myDPh=0, int myDPl=0) {
  int myDigit;
 
  // get lower digit and display it
  myDigit = myBCD & 0x0F;
  updateDisplay(myPosition, myDigit, myDPl);

  // get higher digit and display it; remember to move 1 position upwards
  myDigit = myBCD >> 4;
  updateDisplay(myPosition+1, myDigit, myDPh);

}


void oneSecondISR() {
  secondElapsed = 1;
  secondsElapsed += 1;
  //timerCountdown -= 1;
 }


void setup() {

  //Serial.begin(9600);

  pinMode(sensorPin, INPUT);
  pinMode(potPin, INPUT);
  pinMode(oneSecondInterruptPin, INPUT); // DS3231 square wave output. Does it need pullup?

  for (int a = 0; a < 4; a++) {
    pinMode(blanking[a], OUTPUT);  // set blanking/PWM outputs
    analogWrite(blanking[a], 100);
  }
  
  for (int a = 0; a < 4; a++) {
    pinMode(inputs[a], OUTPUT);  //set data lines outputs
    digitalWrite(inputs[a], LOW);
  }

  pinMode(decimalPoint, OUTPUT);
  digitalWrite(decimalPoint, LOW); // DP off

  for (int a = 0; a < 8; a++) {
    pinMode(latches[a], OUTPUT);  //set data latches outputs
    digitalWrite(latches[a], HIGH);
    digitalWrite(latches[a], LOW);
    digitalWrite(latches[a], HIGH);
  }


  // ***** RTC OPERATIONS *****
  // Initialize I2C RTC
  if (! rtc.begin()) {
//    Serial.println("Couldn't find RTC");
    // TODO: display an error on the display E001
    blankControl(50, 50, 50, 50);
    printBCD(4, 0xE0);
    printBCD(2, 0x01);
    delay(1000);

    while (1);
  }

  if (rtc.lostPower()) {
//    Serial.println("RTC lost power, you need to set the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    blankControl(50, 50, 50, 50);
    printBCD(4, 0xE0);
    printBCD(2, 0x02);
    delay(1000);
  }

  rtc.writeSqwPinMode(DS3231_SquareWave1Hz);

  // for future use, handle start hour of night mode in EEPROM
  // set to 0 if an invalid  value comes up
  nightModeStartHour = EEPROM.read(NIGHTMODESTARTEE);
  if (nightModeStartHour > 0) {
    nightModeStartHour = 0;
    EEPROM.write(NIGHTMODESTARTEE, 0);
  }
  
  // handle end hour of night mode in EEPROM;
  // set to 8 AM if an invalid value comes up
  nightModeEndHour = EEPROM.read(NIGHTMODEENDEE);
  if (nightModeEndHour > 23) {
    nightModeEndHour = 8;
    EEPROM.write(NIGHTMODEENDEE, 8);
  }
  

  printBCD(4, 0xC1);
  printBCD(2, 0xA0);
  blankControl(OFFBRI, MAXBRI, MAXBRI, OFFBRI);
  delay(1000);


  RTCnow = rtc.now();
  seconds = RTCnow.second();
  secondsElapsed = seconds;
  randomSeed(seconds+RTCnow.day()+RTCnow.month());

  attachInterrupt(digitalPinToInterrupt(oneSecondInterruptPin), oneSecondISR, FALLING);

  // ***** END RTC OPERATIONS *****
  
}


void loop() {

  static int lightIntensity;
  static int lightIntensityOld;
  int newHours;
  int newMinutes;
  int newDay;
  int newMonth;
  int newYear;
  int newDoW;
  int tempValue;
  int oldValue;
  int maxDayOfMonth;
  static int randomWord;


   if (secondElapsed == 1) {
      blinker = secondsElapsed % 2; // blink stuff once a second (0.5 Hz)

      // 0 to about 1020
      //      lightIntensity = analogRead(sensorPin);
      //      Serial.print("Light value: ");
      //      Serial.println(lightIntensity);
      
      int ADCreading = analogRead(sensorPin);

      if (ADCreading == 0) {
        // button pressed
      } else {
        if (ADCreading > INTENSITYTHRESHOLDDAY) {
          lightIntensity = (lightIntensity + MAXBRI) / 2;
        } else {
          lightIntensity = (lightIntensity + MINBRI) / 2;
        }
      }
      if (nightMode == 1) {
        if (abs(lightIntensity-lightIntensityOld)>RESETONTIMEDIFF) {
          if (timerCountdown<RESETONTIME) {
            timerCountdown = MAXONTIME;
          }
          nightModeStayOn = 1;
        }
        if (timerCountdown<1) {
          nightModeStayOn = 0;
        }

        if (RTCnow.hour()<nightModeEndHour) { // between 00:00 and 08:00 stay on regardless
          nightModeStayOn = 1;          
        }
        
      lightIntensityOld = lightIntensity;
      }

      // Control blinking dots. Since there is no PWM on them,
      // turn them off in darkness or during DD/MM/YY display
      // at the end of the minute
//      if ( (secondsElapsed > 56) || (lightIntensity > 200) ) {
//        updateDpLeft(0);
//        updateDpRight(0);
//      } else {
//        updateDpLeft(blinker);
//        updateDpRight(!blinker);
//      }
      
      
      secondElapsed = 0;

    }



    if (secondsElapsed >= 60) {

//***TODO      randomWord = random(0, WORDS) * 2;
      
      // once a minute update the data from RTC
      RTCnow = rtc.now();
      secondsElapsed = RTCnow.second();
    }

    // daytime roll, between 7 AM and midnight
    if (RTCnow.hour() > 6) {

      switch (secondsElapsed) {
// ********TODO
//        case 18:
//        case 19:
          // during the day show a random word

//          printBCD(2, words[randomWord]);
//          printBCD(0, words[randomWord+1]);
//          blankMSD=0;
//          if (randomWord > (WORDS3LETTER*2)) { // we're beyond 4-letter words
//            blankControl(255, lightIntensity, lightIntensity, 255);
//          } else if (randomWord > (WORDS4LETTER*2)) { // we're beyond 4-letter words
//            blankControl(lightIntensity, lightIntensity, lightIntensity, 255);
//          } else {
//            blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
//          }
// **** TODO END
//          updateDpRight(0);
//          updateDpLeft(0);
          break;        
        case 59:
        case 58:
          // show month-day and day-of-the-week number
          printBCD(6, decToBcd(RTCnow.day()));
          updateDisplay(5, 0x0B);
          printBCD(3, decToBcd(RTCnow.month()));
          updateDisplay(2, 0x0B);
          printBCD(0, decToBcd(RTCnow.year() % 100));
          blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity); 
          lightIntensityOld = lightIntensity;
          break;
        case 57:
          lightIntensityOld = fadeOut(lightIntensityOld);
          delay(200);
//          updateDpRight(0);
//          updateDpLeft(0);
          break;
        default:
          updateDisplay(7, 0x0D);
          updateDisplay(0, 0x0D);
          printBCD(5, decToBcd(RTCnow.hour()));
          printBCD(3, decToBcd(RTCnow.minute()), 1, 0);
          printBCD(1, decToBcd(secondsElapsed), 1, 0);
          blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
          break;
      } // end switch
      
    } else {
          updateDisplay(7, 0x0B);
          updateDisplay(0, 0x0B);
          printBCD(5, decToBcd(RTCnow.hour()));
          printBCD(3, decToBcd(RTCnow.minute()));
          printBCD(1, decToBcd(secondsElapsed));
        blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
    }
    

}
