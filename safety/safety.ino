//
// Hello :) visit hackmelbourne.org
//
// updated by Tim E 06/04/2016
//

//CONFIG START

#define hv_interlock_pin 10 //the output pin driving the relay to interrupt the interlock circuit. HIGH closes the relay
#define ready_led_pin 12 //LED to show HV supply is READY (no interlocks opened) (not used on laser cutter V2 at the moment)

#define bypass_interlocks true //set true to ignore the state of the interlock switches
//(currently set true because LCV2 does not have suitable switches)
#define interlock_pin 6 //interlock circuit pin (+5V to pin when CLOSED - should have PULL DOWN resistor)
#define estop_pin 7 //emergency stop button/switch (+5V to pin when CLOSED - should have PULL DOWN resistor)
#define key_pin 8 //arm/disarm key (switch) (+5V to pin when CLOSED - should have PULL DOWN resistor)

#define bypass_sensors false //set true to ignore the following sensors

#define use_flow_sensor true //enable the flow sensor
#define flow_sensor_pin 2 //flow rate sensor pin
#define flow_rate_upper_limit 15.0 //(litres per minute)upper limit of flow rate
#define flow_rate_lower_limit 1.5 //(litres per minute)lower limit of flow rate

#define use_temp_sensor_1 true //enable the temp sensor 1
#define temp_sensor_pin_1 3 //temperature sensor pin
#define water_temp_upper_limit_1 24 //(degrees C) water temp upper limit
#define water_temp_lower_limit_1 3 //(degrees C) water temp lower limit

#define use_temp_sensor_2 false //use temp sensor 2 (currently disabled as it is not wired properly on LCV2)
#define temp_sensor_pin_2 4 //temperature sensor pin
#define water_temp_upper_limit_2 24 //(degrees C) water temp upper limit
#define water_temp_lower_limit_2 3 //(degrees C) water temp lower limit

//CONFIG END

#include "Wire.h" //I2C
#include "LiquidCrystal_I2C.h" //LCD
#include "DallasTemperature.h" //temp sensor

//create the LCD object. firs param is the address. no idea what other params are, they were in the tronixlabs's example code. seems to work.
LiquidCrystal_I2C lcd(0x3f,2,1,0,4,5,6,7); //0x27 is the default I2C bus address for an unmodified backpack, others use 0x3f

//set up the temp sensors objects
OneWire tempWire1(temp_sensor_pin_1);
OneWire tempWire2(temp_sensor_pin_2);
DallasTemperature temp_sensor_1(&tempWire1);
DallasTemperature temp_sensor_2(&tempWire2);

volatile uint16_t flow_pulses = 0; //stores number of pulses
volatile uint8_t lastflowpinstate; //track the state of the pulse pin
SIGNAL(TIMER0_COMPA_vect) { //this interrupt is called once a millisecond
  uint8_t x = digitalRead(flow_sensor_pin); //read the state of the flow sensor pin
  if (x == lastflowpinstate) {
    return; //state not changed
  }
  if (x == HIGH) {
    flow_pulses++; //low to high transition - increment pulse count
  }
  lastflowpinstate = x; //update the last known state of the pin
}

void useInterrupt(boolean v) { //not sure what this does... it's from the adafruit example. see comments below
  if (v) {
    // Timer0 is already used for millis() - we'll just interrupt somewhere
    // in the middle and call the "Compare A" function above
    OCR0A = 0xAF;
    TIMSK0 |= _BV(OCIE0A);
  } else {
    // do not call the interrupt function COMPA anymore
    TIMSK0 &= ~_BV(OCIE0A);
  }
}

void setup () {
  
  //===interlock pins setup===
  pinMode(interlock_pin, INPUT);
  pinMode(estop_pin, INPUT);
  pinMode(key_pin, INPUT);
  pinMode(hv_interlock_pin, OUTPUT);
  pinMode(hv_interlock_pin + 1, OUTPUT); //the second pin
  digitalWrite(hv_interlock_pin, LOW);
  digitalWrite(hv_interlock_pin + 1, LOW); //the second pin
  
  //===flow sensor setup===
  if (use_flow_sensor) {
    pinMode(flow_sensor_pin, INPUT); //set the pin mode
    digitalWrite(flow_sensor_pin, HIGH); //pull it high
    lastflowpinstate = digitalRead(flow_sensor_pin); //set an initial value
    useInterrupt(true); //do whatever this does
  }
  
  //===temp sensor setup===
  if (use_temp_sensor_1) {
    temp_sensor_1.begin();
  }
  if (use_temp_sensor_2) {
    temp_sensor_2.begin();
  }
  
  //===LCD setup====
  lcd.begin (20,4); //for 20 x 4 LCD module
  lcd.setBacklightPin(3,POSITIVE);
  lcd.setBacklight(HIGH);
  lcd.home();
  lcd.print("Loading..."); 
}

//sets the HV interlock output on or off (sets state of pins and the LED)
boolean enable_hv_interlock(boolean state) {

  if (state) {
    digitalWrite(hv_interlock_pin, HIGH);
    digitalWrite(hv_interlock_pin + 1, HIGH);
    digitalWrite(ready_led_pin, HIGH);
  }
  else {
    digitalWrite(hv_interlock_pin, LOW);
    digitalWrite(hv_interlock_pin + 1, LOW);
    digitalWrite(ready_led_pin, LOW);
  }
}

//the messages to display on the LCD for each fault condition - each string should be 20 chars long to suit LCD. Empty spaces are needed so remnants of longer strings don't stick around
String fault_messages[] = {
"READY TO CUT        " ,
"CLOSE COVER(S)      " ,
"E-STOP PRESSED      " ,
"KEY OFF             " ,
"CHECK FLOW RATE     " ,
"CHECK TEMPERATURE 1 " ,
"CHECK TEMPERATURE 2 " };

//characters to animate to show the controller is responding
const String display_anim[] = { "-   ", " -  ", "  - ", "   -" };
int cur_anim = 0;

//use these values to flip flop between displaying temp 1 and temp 2 on the LCD
boolean temp_display_alt = false; //display value 2
int long last_displayed_temp = millis(); //time since last changed

//average out the flow sensor values
int currentPulseValue = 0; //which was the last value to be updated
int flow_pulses_avg[] = { 0,0,0,0,0,0 }; //stores 6 pulse counts for averaging out

//should be called every x ms by the main loop
int long loop_last_flow_update = 0; //track the ms since updated last
void flow_pulse_update () {
  flow_pulses_avg[currentPulseValue] = flow_pulses; //get value from the interrupt function
  flow_pulses = 0; //reset the interrupt count
  if (currentPulseValue == 5) {
    currentPulseValue = 0; //reset the value to update
  } else {
    currentPulseValue++; //increment the array value we are saving into
  }
}

//returns the average of the last six flow sensor pulses we have stored
float flow_pulse_average () {
  return (flow_pulses_avg[0] + flow_pulses_avg[1] + flow_pulses_avg[2] + flow_pulses_avg[3] + flow_pulses_avg[4] + flow_pulses_avg[5]) / 6.0;
}

int long loop_last_millis = 0; //tracks the exact ms length of the loop so we calculate exact flow rate values
void loop () {

  if ((millis() - loop_last_millis) < 1000) {
    return;
  }
  else {
    loop_last_millis = millis();
  }

  flow_pulse_update(); //update the flow pulse count
  float flow_rate = flow_pulse_average(); //calculate averages

  //track which fault has ocurred
  int current_faults = 0; //0 = all good, 1 = interlocks, 2 = estop, 3 = key off, 4 = flow rate, 5 = temperature

  //display some static info on the LCD
  lcd.home(); //set cursor to 0,0
  lcd.print("Laser Cutter V2 " + display_anim[cur_anim]);
  cur_anim++;
  if (cur_anim == 4) {
    cur_anim = 0;
  }
  
  //get water temp and display it
  lcd.setCursor(0,1); //go to start of 2nd line
  float water_temp_1; //get the values from the temp sensors
  float water_temp_2;
  water_temp_1 = temp_sensor_1.getTempCByIndex(0);
  temp_sensor_1.requestTemperatures();
  water_temp_2 = temp_sensor_2.getTempCByIndex(0);
  temp_sensor_2.requestTemperatures();
  if (!use_temp_sensor_1){
    lcd.print("Temp: ");
    lcd.print((int)water_temp_2); //print reading
    lcd.print(" deg C  ");
  }
  else if( !use_temp_sensor_2) {
    lcd.print("Temp: ");
    lcd.print((int)water_temp_1); //print reading
    lcd.print(" deg C  ");
  }
  else {
    if (temp_display_alt) {
      lcd.print("Temp(1): ");
      lcd.print((int)water_temp_1); //print reading
    }
    else {
      lcd.print("Temp(2): ");
      lcd.print((int)water_temp_2); //print reading
    }
    lcd.print(" deg C  ");
    if ((millis() - last_displayed_temp) > 3000) {
      temp_display_alt = !temp_display_alt; //show the other value next loop
      last_displayed_temp = millis();
    }
  }

  //display the flow rate
  if (use_flow_sensor) {
    lcd.setCursor(0,2); //3rd line
    lcd.print("Flow: ");
    lcd.print(flow_rate);
    lcd.print(" (l/pm)  ");
  }

  //fault values:
  //0 = all good, 1 = interlocks, 2 = estop, 3 = key off, 4 = flow rate, 5 = temperature, 6 = wait for startup delay
  
  //skip interlocks if true
  if (!bypass_interlocks) {
    //if interlock switches were opened
    if (digitalRead(interlock_pin) == LOW) {
      current_faults = 1; //interlocks opened
    }
    if (digitalRead(estop_pin) == LOW) {
      current_faults = 2; //estop pressed
    }
    if (digitalRead(key_pin) == LOW) {
      current_faults = 3; //key not turned on
    }
  }
  //skip these two sensors if true
  if (!bypass_sensors) {
    if (use_flow_sensor) {
      if (flow_rate > flow_rate_upper_limit || flow_rate < flow_rate_lower_limit) {
        current_faults = 4; //flow rate outside acceptable limits
      }
    }
    if (use_temp_sensor_1) {
      if (water_temp_1 > water_temp_upper_limit_1 || water_temp_1 < water_temp_lower_limit_1) {
        current_faults = 5; //temp outside acceptable limits
      }
    }
    if (use_temp_sensor_2) {
      if (water_temp_2 > water_temp_upper_limit_2 || water_temp_2 < water_temp_lower_limit_2) {
        current_faults = 6; //temp outside acceptable limits
      }
    }
  }
  
  //enable or disable the interlock output
  if (current_faults != 0) {
    enable_hv_interlock(false);
  }
  else {
    enable_hv_interlock(true);
  }
  
  //display the active fault
  lcd.setCursor(0,3); //4th line
  lcd.print(fault_messages[current_faults]);
  
}
