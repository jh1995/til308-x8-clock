// -------------------------------------------------------------
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

// PIN DEFINITIONS - don't touch unless your layout is different

int inputs[4] = {A0, A1, A2, A3}; // A, B, C, D outputs
int latches[8] = {12, 9, 13, 8, 7, 6, 4, 1}; // latches LSD to MSD
int latchesReversed[8] = {1, 4, 6, 7, 8, 13, 9, 12}; // as above but MSD to LSD, used by updateDisplayFromVector
int blanking[4] = {10, 11, 5, 3}; // blanking line for brightness control via PWM (must be PWM pins)


// DATA DEFINITIONS

#define DASH 0x0B
#define DASHDP 0x1B
#define OFFDIGIT 0x0D
#define OFFDIGITDP 0x1D

// The displayVector carries information of decimal point and symbol for each position
// The higher nibble represents the DP off (0) or on (1).
// The lower nibble is the digit/symbol to be displayed
unsigned int displayVector[8] = {DASH, OFFDIGIT, 0x0C, 0x01, 0x0A, 0x00, OFFDIGIT, DASH};
unsigned int brightnessVector[4] = {255, 255, 255, 255}; // 0 min to 255 max

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

// *************************
// You can add your words to the vector "words" below
// You need to update the WORDS constant with the correct number of
// words included in the array
// Available letters are:   ABCEFIOSG-
// Mapped to these symbols: A8CEF1059B  (mind the mix of letters and numbers!)
// Plus space ("D") and all the numbers are at your disposal!
// *************************

#define WORDS 10  // yes, I know, it could be computed dymanically
int words[][4] = {
  0xDD, 0xF1, 0x90, 0xDD, // figo
  0x81, 0xFA, 0xCE, 0x5D, // bifaces
  0xDD, 0x50, 0xFA, 0xDD, // sofa
  0xDD, 0xDD, 0xCA, 0xFE, // cafe
  0x51, 0xD0, 0xD5, 0x1D, // si o si
  0x73, 0xD5, 0x1D, 0x88, // 73 51 88
  0xD5, 0x0F, 0x1A, 0xDD, // sofia
  0xDD, 0xD4, 0x2D, 0xDD, // THE ANSWER!
  0xDD, 0xBA, 0xC1, 0x0D, // bacio
  0x5E, 0x1D, 0xF1, 0x90  // sei figo
};



// DO NOT TOUCH BELOW THIS LINE

#define DSTADDRESS 1 // EEPROM position for DST status
#define NIGHTMODEEE 10 // EEPROM position to store night mode setting
#define NIGHTMODESTARTEE 12 // EEPROM position to store night mode start time
#define NIGHTMODEENDEE 16 // // EEPROM position to store night mode end time
#define ENDOFNIGHTMODE 9 // end night mode at 9 AM. Integer
#define INTENSITYTHRESHOLDMID 150 // below this is night, above is day light
#define INTENSITYTHRESHOLDDAY 320 // below this is night, above is day light


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
#define DIMBRI 50
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

// update the display using the two vectors:
// - data
// - brightness
void updateDisplayFromVector() {

  // looping on the vector MSD to LSD
  for (int p = 0; p < 8; p++) {
    
    // check if DP is ON
    if ((0xF0 & displayVector[p]) == 0B00010000) {
      digitalWrite(decimalPoint, HIGH);
    } else {
      digitalWrite(decimalPoint, LOW);  
    }

    
    for(int c = 0; c < 4; c++){
      digitalWrite(inputs[c], BCD[ 0x0F & displayVector[p] ][c]);
    }


    // send a signal to the latch so that the display loads the data
    digitalWrite(latchesReversed[p], HIGH);
    digitalWrite(latchesReversed[p], LOW); 
    digitalWrite(latchesReversed[p], HIGH);     
  }
    
 
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

void setDisplayVector (unsigned int v0, unsigned int v1, unsigned int v2, unsigned int v3, unsigned int v4, unsigned int v5, unsigned int v6, unsigned int v7, int offset=0 ) {
    displayVector[offset+0] = v0;
    displayVector[offset+1] = v1;
    displayVector[offset+2] = v2;
    displayVector[offset+3] = v3;
    displayVector[offset+4] = v4;
    displayVector[offset+5] = v5;
    displayVector[offset+6] = v6;
    displayVector[offset+7] = v7;
}


// simple button press, no short-long detection (TODO)
int buttonPressed() {
   if (analogRead(sensorPin) < 1) {
    delay(50);
    if (analogRead(sensorPin) < 1) {
    return 1;
    } else {
      return 0;
    }
   } else {
    return 0;
   }
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
    blankControl(0, 50, 0, 50);
    printBCD(4, 0xE0);
    printBCD(0, 0x01);
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
  

//  printBCD(4, 0xC1);
//  printBCD(2, 0xA0);
//  blankControl(OFFBRI, MAXBRI, MAXBRI, OFFBRI);
  updateDisplayFromVector();
  blankControl(MAXBRI, MAXBRI, MAXBRI, MAXBRI);
  delay(670);


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
  static int shortPress;
  static int myWeekday;
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
  static int randomWordDisplay=0; // display the random word or not, randomized every 60".
  static int timePosOffset=0;


   if (secondElapsed == 1) {
      blinker = secondsElapsed % 2; // blink stuff once a second (0.5 Hz)

      // 0 to about 1020
      //      lightIntensity = analogRead(sensorPin);
      //      Serial.print("Light value: ");
      //      Serial.println(lightIntensity);
      
      int ADCreading = analogRead(sensorPin);
      //Serial.println(ADCreading);

      if (ADCreading == 0) {
        // button pressed
      } else {
        if (ADCreading > INTENSITYTHRESHOLDDAY) {
          lightIntensity = (lightIntensity + MAXBRI) / 2;
        } else if (ADCreading < INTENSITYTHRESHOLDMID) {
          lightIntensity = (lightIntensity + MINBRI) / 2;
        } else {
          lightIntensity = (lightIntensity + (MINBRI+MAXBRI)/2) / 2;
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
      
      secondElapsed = 0;

    }


 
    // once a minute update the data from RTC
    if (secondsElapsed >= 60) {
      RTCnow = rtc.now();
      secondsElapsed = RTCnow.second();
      timePosOffset = random(-1,1);
      randomWord = random(0, WORDS);
      myWeekday = RTCnow.dayOfTheWeek();
      randomWordDisplay = random(0, 10); // display the random word 10% of times
    }

  
//    displayVector[0] = OFFDIGIT;
//    displayVector[1] = decToBcd(RTCnow.hour()/10);
//    displayVector[2] = decToBcd(RTCnow.hour()%10);
//    displayVector[3] = (decToBcd(RTCnow.minute()/10) | 0B00010000);
//    displayVector[4] = decToBcd(RTCnow.minute()%10);
//    displayVector[5] = (decToBcd(secondsElapsed/10) | 0B00010000);
//    displayVector[6] = decToBcd(secondsElapsed%10);    
//    displayVector[7] = OFFDIGIT;


// ***** SET ROUTINE *****

  if ( buttonPressed() ) {
      
      nightModeStayOn = 1;
      shortPress = 0; // reset short press
  
      newHours = RTCnow.hour();
      newMinutes = RTCnow.minute();
      newDay = RTCnow.day();
      newMonth = RTCnow.month();
      newYear = RTCnow.year() % 100;
      newDoW = RTCnow.dayOfTheWeek();
  
      // set hours
      oldValue = map(analogRead(potPin), 0, 1000, 0, 23);
      do {
        shortPress = buttonPressed();
        setDisplayVector( OFFDIGIT,
                          OFFDIGIT,
                          decToBcd(newHours/10),
                          decToBcd(newHours%10),
                          (decToBcd(newMinutes/10) | 0B00010000),
                          decToBcd(newMinutes%10),
                          OFFDIGIT,
                          OFFDIGIT );        
        updateDisplayFromVector();
        
        tempValue = map(analogRead(potPin), 0, 1000, 0, 23);
        // if the pot has moved
        if (tempValue != oldValue) {
          newHours = tempValue;
          oldValue = tempValue;
        }

        blankControl(OFFBRI, MAXBRI, DIMBRI, OFFBRI); // blank rightmost two digits
      } while (shortPress == 0);
      shortPress = 0;
  
//      if (longPress == 1) {
//        updateDpLeft(1);
//        updateDpRight(1);
//  
//      }
  
      // set minutes
      oldValue = map(analogRead(potPin), 0, 1015, 0, 59);
      do {
        shortPress = buttonPressed();
        setDisplayVector( OFFDIGIT,
                          OFFDIGIT,
                          decToBcd(newHours/10),
                          decToBcd(newHours%10),
                          (decToBcd(newMinutes/10) | 0B00010000),
                          decToBcd(newMinutes%10),
                          OFFDIGIT,
                          OFFDIGIT );        
        updateDisplayFromVector();
  
        tempValue = map(analogRead(potPin), 0, 1015, 0, 59);
        // if the pot has moved
        if (tempValue != oldValue) {
          newMinutes = tempValue;
          oldValue = tempValue;
        }
              
        blankControl(OFFBRI, DIMBRI, MAXBRI, OFFBRI); // blank rightmost two digits
      } while (shortPress == 0);
      shortPress = 0;
  
//      if (longPress == 1) {
//        updateDpLeft(1);
//        updateDpRight(1);
//  
//      }
  
  
      // set month
      oldValue = map(analogRead(potPin), 0, 1000, 1, 12);
      do {
        shortPress = buttonPressed();
        setDisplayVector( decToBcd(newDay/10),
                          decToBcd(newDay%10),
                          (decToBcd(newMonth/10) | 0B00010000),
                          decToBcd(newMonth%10),
                          (decToBcd(0x02) | 0B00010000),
                          decToBcd(0),
                          decToBcd(newYear/10),
                          decToBcd(newYear%10));        
        updateDisplayFromVector();

        tempValue = map(analogRead(potPin), 0, 1000, 1, 12);
        // if the pot has moved
        if (tempValue != oldValue) {
          newMonth = tempValue;
          oldValue = tempValue;
        }
   
        
        blankControl(DIMBRI, MAXBRI, DIMBRI, DIMBRI); // blank leftmost two digits
      } while (shortPress == 0);
      shortPress = 0;
  
//      if (longPress == 1) {
//        updateDpLeft(1);
//        updateDpRight(1);
//  
//      }
  
  
      // set day
      switch (newMonth) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
          maxDayOfMonth = 31;
          break;
        case 4:
        case 6:
        case 9:
        case 11:
          maxDayOfMonth = 30;
          break;
        case 2: // alright, please don't set the time on THAT DAY of leap years!
          maxDayOfMonth = 28;
          break;
      }
        
      oldValue = map(analogRead(potPin), 0, 1000, 1, maxDayOfMonth);    
      do {
        shortPress = buttonPressed();
        setDisplayVector( decToBcd(newDay/10),
                          decToBcd(newDay%10),
                          (decToBcd(newMonth/10) | 0B00010000),
                          decToBcd(newMonth%10),
                          (decToBcd(0x02) | 0B00010000),
                          decToBcd(0),
                          decToBcd(newYear/10),
                          decToBcd(newYear%10));        
        updateDisplayFromVector();
          
        tempValue = map(analogRead(potPin), 0, 1000, 1, maxDayOfMonth);
        // if the pot has moved
        if (tempValue != oldValue) {
          newDay = tempValue;
          oldValue = tempValue;
        }
        
        blankMSD = 0;
        blankControl(MAXBRI, DIMBRI, DIMBRI, DIMBRI); // blank rightmost two digits
      } while (shortPress == 0);
      shortPress = 0;
  
//      if (longPress == 1) {
//        updateDpLeft(1);
//        updateDpRight(1);
//  
//      }
  
      // set year
      oldValue = map(analogRead(potPin), 0, 1000, 20, 50);    
      do {
        shortPress = buttonPressed();
        setDisplayVector( decToBcd(newDay/10),
                          decToBcd(newDay%10),
                          (decToBcd(newMonth/10) | 0B00010000),
                          decToBcd(newMonth%10),
                          (decToBcd(2) | 0B00010000),
                          decToBcd(0),
                          decToBcd(newYear/10),
                          decToBcd(newYear%10));        
        updateDisplayFromVector();
        
        tempValue = map(analogRead(potPin), 0, 1000, 20, 50);
        // if the pot has moved
        if                 (tempValue != oldValue) {
          newYear = tempValue;
          oldValue = tempValue;
        }
        
        blankControl(DIMBRI, DIMBRI, DIMBRI, MAXBRI); // no blanking for the year
      } while (shortPress == 0);
      shortPress = 0;
  
//      if (longPress == 1) {
//        updateDpLeft(1);
//        updateDpRight(1);
//  
//      }
  
      // set END OF NIGHT MODE
      oldValue = map(analogRead(potPin), 0, 1015, 0, 23);
      do {
        shortPress = buttonPressed();
        setDisplayVector( OFFDIGIT,
                          OFFDIGIT,
                          OFFDIGIT,
                          OFFDIGIT,
                          0x0B,
                          0x0E,
                          decToBcd(nightModeEndHour/10),
                          decToBcd(nightModeEndHour%10));        
        updateDisplayFromVector();

        tempValue = map(analogRead(potPin), 0, 1015, 0, 23);
        // if the pot has moved
        if (tempValue != oldValue) {
          nightModeEndHour = tempValue;
          oldValue = tempValue;
        }
              
        blankControl(DIMBRI, DIMBRI, MAXBRI, MAXBRI); // blank rightmost two digits
      } while (shortPress == 0);
      shortPress = 0;
  
//TODO      if (longPress == 1) {
//        updateDpLeft(1);
//        updateDpRight(1);
//  
//      }
  
  
      // TODO update the RTC only if the button was not longpressed during previous steps

//      if (longPress == 0) {
        rtc.adjust(DateTime(newYear, newMonth, newDay, newHours, newMinutes, 0));
        EEPROM.write(NIGHTMODEENDEE, nightModeEndHour);
        setDisplayVector( OFFDIGIT,
                          OFFDIGIT,
                          OFFDIGIT,
                          OFFDIGIT,
                          0x0C,
                          0x0F,
                          OFFDIGIT,
                          decToBcd(1));        
        updateDisplayFromVector();

//      } else {
        setDisplayVector( OFFDIGIT,
                          OFFDIGIT,
                          OFFDIGIT,
                          OFFDIGIT,
                          0x0C,
                          0x0F,
                          OFFDIGIT,
                          decToBcd(0));        
        updateDisplayFromVector();

//      }
      blankControl(MAXBRI, MAXBRI, MAXBRI, MAXBRI);
      delay(1000);
  
      // exit
      shortPress = 0;
//      longPress = 0;
      
      RTCnow = rtc.now(); // update current time
  }
// ***** END SET ROUTINE *****


    // daytime roll, between 7 AM and midnight
    if (RTCnow.hour() > 6) {

      switch (secondsElapsed) {
        case 28:
        case 29:
          // during the day show a random word
          if (randomWordDisplay == 0) {
            printBCD(0, words[randomWord][3]);
            printBCD(2, words[randomWord][2]);
            printBCD(4, words[randomWord][1]);
            printBCD(6, words[randomWord][0]);
          } else {
            setDisplayVector( OFFDIGIT,
                    decToBcd(RTCnow.hour()/10),
                    decToBcd(RTCnow.hour()%10),
                    (decToBcd(RTCnow.minute()/10) | 0B00010000),
                    decToBcd(RTCnow.minute()%10),
                    (decToBcd(secondsElapsed/10) | 0B00010000),
                    decToBcd(secondsElapsed%10),
                    OFFDIGIT );
            updateDisplayFromVector();
          }
          blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);

          break;

        case 56:
        case 57:
          // show month-day and day-of-the-week number
          printBCD(6, decToBcd(RTCnow.day()));
          updateDisplay(5, 0x0B);
          printBCD(3, decToBcd(RTCnow.month()));
          updateDisplay(2, 0x0B);
          printBCD(0, decToBcd(RTCnow.year() % 100));
          blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity); 
          lightIntensityOld = lightIntensity;
          break;
//        case 56:
//          lightIntensityOld = fadeOut(lightIntensityOld);
//          delay(200);
////          updateDpRight(0);
////          updateDpLeft(0);
//          break;

        case 58:
        case 59:
          // let's display the day of the week in some form using 7 displays and different brightness
          switch (myWeekday) {
            case 1: // monday
              setDisplayVector(OFFDIGIT, 0x01, DASH, DASH, DASH, DASH, DASH, DASH);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;
            case 2: // tuesday
              setDisplayVector(DASH, 0x02, DASH, DASH, DASH, DASH, DASH, OFFDIGIT);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;
            case 3: // wednesday
              setDisplayVector(DASH, DASH, 0x03, DASH, DASH, DASH, DASH, OFFDIGIT);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;
            case 4: // thursday
              setDisplayVector(OFFDIGIT, DASH, DASH, DASH, 0x04, DASH, DASH, DASH);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;
            case 5: // friday
              setDisplayVector(DASH, DASH, DASH, DASH, 0x05, DASH, DASH, OFFDIGIT);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;            
            case 6: // saturday
              setDisplayVector(OFFDIGIT, DASH, DASH, DASH, DASH, DASH, 0x06, DASH);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;            
            case 0: // sunday
              setDisplayVector(DASH, DASH, DASH, DASH, DASH, DASH, 0x07, OFFDIGIT);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;
            default:
              setDisplayVector(0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01);
              updateDisplayFromVector();
              blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
              break;
          }
          break;

        
        default:
          setDisplayVector( OFFDIGIT,
                    decToBcd(RTCnow.hour()/10),
                    decToBcd(RTCnow.hour()%10),
                    (decToBcd(RTCnow.minute()/10) | 0B00010000),
                    decToBcd(RTCnow.minute()%10),
                    (decToBcd(secondsElapsed/10) | 0B00010000),
                    decToBcd(secondsElapsed%10),
                    OFFDIGIT );
          updateDisplayFromVector();
          blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
          break;
      } // end switch

    // between 00H00 and 05H59 do the night mode display
    // which has a limited amount of movement and displays
    // only HH:MM
    } else {

      displayVector[0] = OFFDIGIT;
      displayVector[1] = OFFDIGIT;
      displayVector[2] = decToBcd(RTCnow.hour()/10);
      displayVector[3] = decToBcd(RTCnow.hour()%10);
      displayVector[4] = (decToBcd(RTCnow.minute()/10) | 0B00010000);
      displayVector[5] = decToBcd(RTCnow.minute()%10);
      displayVector[6] = OFFDIGIT;
      displayVector[7] = OFFDIGIT;         
      updateDisplayFromVector();
          
      blankControl(lightIntensity, lightIntensity, lightIntensity, lightIntensity);
    }
    

}
