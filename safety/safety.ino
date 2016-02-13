//
// Hello :) visit hackmelbourne.org
//
// TODO: data logging using SD card
//
// updated by Tim E 13/02/2016

//CONFIG START
const int mega_reset_pin = 9; //the output pin to pull low for resetting the RAMPS
const int hv_interlock_pin = 10; //the output pin driving the relay to interrupt the interlock circuit
const int ready_led_pin = 11; //LED to show HV supply is READY (no interlocks opened)
const int interlock_pin = 6; //interlock circuit pin (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int estop_pin = 7; //emergency stop button/switch (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int key_pin = 8; //security key (switch) (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int flow_sensor_pin = 2; //flow rate sensor pin
const float flow_rate_upper_limit = 10.0; //(litres per minute)upper limit of flow rate
const float flow_rate_lower_limit = 3.0; //(litres per minute)lower limit of flow rate
const int temp_sensor_pin_1 = 3; //temperature sensor pin
const float water_temp_upper_limit_1 = 35.0; //(degrees C) water temp upper limit
const float water_temp_lower_limit_1 = 8.0; //(degrees C) water temp lower limit
const int temp_sensor_pin_2 = 4; //temperature sensor pin
const float water_temp_upper_limit_2 = 35.0; //(degrees C) water temp upper limit
const float water_temp_lower_limit_2 = 8.0; //(degrees C) water temp lower limit
const boolean bypass_sensors = false; //ignore temp and flow sensors - for testing switches with machine off
const boolean bypass_interlocks = true; //don't disable the laser if the interlocks are open (set true to not monitor the interlock circuit)
const float startup_wait_time = 1.0; //(minutes) wait time before enabling output. ensures the coolant is flowing
const boolean bypass_wait_time = true; //used for testing
//CONFIG END

//Date and time functions using the DS1307 RTC connected via I2C and Wire lib
#include "Wire.h" //I2C
#include "RTClib.h" //RTC
#include "LiquidCrystal_I2C.h" //LCD
#include "DallasTemperature.h" //temp sensor

//create the LCD object. firs param is the address. no idea what other params are, they were in the tronixlabs's example code. seems to work.
LiquidCrystal_I2C	lcd(0x3f,2,1,0,4,5,6,7); //0x27 is the default I2C bus address for an unmodified backpack [mine uses 0x3f]

//create the RTC object
RTC_DS1307 RTC;

//set up the temp sensor
OneWire tempWire1(temp_sensor_pin_1);
OneWire tempWire2(temp_sensor_pin_2);
DallasTemperature temp_sensor_1(&tempWire1);
DallasTemperature temp_sensor_2(&tempWire2);

//the flow sensor hardware interrupt function
volatile uint16_t flow_pulses = 0; //stores the number of pulses between calls of the main loop
void flow_sense() {
  flow_pulses++;
}

void setup () {
	
  Serial.begin(9600); //for debug purposes
  
  //===interlocks setup===
  pinMode(interlock_pin, INPUT);
  pinMode(estop_pin, INPUT);
  pinMode(key_pin, INPUT);
  pinMode(hv_interlock_pin, OUTPUT);
  digitalWrite(4, LOW);
  
  //===flow sensor setup===
  attachInterrupt(digitalPinToInterrupt(flow_sensor_pin), flow_sense, RISING); //set up the hardware interrupt to call 'flow_sense' on the RISING
  
  //===RTCsetup=====
  Wire.begin();
  RTC.begin();
  //set the clock, if not already set
  if (! RTC.isrunning()) { 
    Serial.println("RTC is NOT running!");
    //the following line sets the RTC to the date & time this sketch was compiled
    //uncomment it & upload to set the time and start the RTC!
    //RTC.adjust(DateTime(__DATE__, __TIME__));
  } 
  
  //===temp sensor setup===
  temp_sensor_1.begin();
  temp_sensor_2.begin();
  
  //===LCD setup====
  lcd.begin (20,4); //for 20 x 4 LCD module
  lcd.setBacklightPin(3,POSITIVE);
  lcd.setBacklight(HIGH);
  //show a loading message. the sketch pauses for a while while the SD card module loads at power-on
  //(the first delay(); will go for longer while this happens)
  lcd.home();
  lcd.print("Loading..."); 
}

//adds a leading '0' to a number less than 10. to keep the datetime string a consistent length
String digAdd(int number) {
  if (number >= 0 && number < 10) {
    return "0" + (String)number;
  }
  else {
    return (String)number;
  }
}

//returns a string of the current date/time
String getTime() {
  DateTime now = RTC.now();
  return digAdd(now.day()) + "/" + digAdd(now.month()) + "/" + now.year() + " " + digAdd(now.hour()) + ":" + digAdd(now.minute()) + ":" + digAdd(now.second());
}

//set the HV interlock output on or off
boolean enable_hv_interlock(boolean state) {

  if (state) {
    digitalWrite(hv_interlock_pin, HIGH);
    digitalWrite(mega_reset_pin, HIGH);
    digitalWrite(ready_led_pin, HIGH);
  }
  else {
    digitalWrite(hv_interlock_pin, LOW);
    digitalWrite(mega_reset_pin, LOW);
    digitalWrite(ready_led_pin, LOW);
  }
}

//the messages to display for each fault condition - each string should be 20 chars long to suit LCD
String fault_messages[] = {
"READY TO CUT        " ,
"CLOSE COVER(S)      " ,
"E-STOP PRESSED      " ,
"KEY OFF             " ,
"CHECK FLOW RATE     " ,
"CHECK TEMPERATURE 1 " ,
"CHECK TEMPERATURE 2 " ,
"STARTING... WAIT    " };

//characters to animate to show the controller is responding
String display_anim[] = { "-   ", " -  ", "  - ", "   -" };
int cur_anim = 0;

//tracks startup time waited so far
int waited_time = 0;

//values to flip flop between temp 1 and temp 2 valeus display
boolean temp_display_alt = false; //display value 2
int long last_displayed_temp = millis(); //time since last changed

int long loop_last_millis = 0; //track the exact ms length of the loop so we calculate exact flow rate values
float loop_duration = 250; //delay between loop runs, in ms
void loop () {
   
  //track exactly how long since loop last ran and make sure we run it every x ms exactly
  if ((millis() - loop_last_millis) < loop_duration) {
    return;
  }
  else {
    loop_last_millis = millis();
  }

  //450 pulses per litre as per https://www.adafruit.com/products/828
  //convert to L/min based on number of pulses since the loop last ran
  float flow_rate = (flow_pulses / 450.00) * 60.00 * (1000.00 / loop_duration);
  flow_pulses = 0; //now that the value is read, reset the pulses until next read

  //track which fault has ocurred
  int current_faults = 0; //0 = all good, 1 = interlocks, 2 = estop, 3 = key off, 4 = flow rate, 5 = temperature

  //display the date / time
  lcd.home(); //set cursor to 0,0
  lcd.print("Laser Cutter V2 " + display_anim[cur_anim]);
  cur_anim++;
  if (cur_anim == 4) {
    cur_anim = 0;
  }
  
  //get water temp
  float water_temp_1 = temp_sensor_1.getTempCByIndex(0);
  float water_temp_2 = temp_sensor_2.getTempCByIndex(0);
  
  //display temp
  lcd.setCursor(0,1); //go to start of 2nd line
  temp_sensor_1.requestTemperatures();
  temp_sensor_2.requestTemperatures();
  if (temp_display_alt) {
    lcd.print("Temp(1): ");
    lcd.print(water_temp_1); //print reading
  }
  else {
    lcd.print("Temp(2): ");
    lcd.print(water_temp_2); //print reading
  }
  lcd.print("degC  ");
  if ((millis() - last_displayed_temp) > 3000) {
    temp_display_alt = !temp_display_alt; //show the other value next loop
    last_displayed_temp = millis();
  }

  //display the flow rate
  lcd.setCursor(0,2); //3rd line
  lcd.print("Flow: ");
  lcd.print(flow_rate);
  lcd.print(" (l/pm)  ");

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
    if (flow_rate > flow_rate_upper_limit || flow_rate < flow_rate_lower_limit) {
      current_faults = 4; //flow rate outside acceptable limits
    }
    if (water_temp_1 > water_temp_upper_limit_1 || water_temp_1 < water_temp_lower_limit_1) {
      current_faults = 5; //temp outside acceptable limits
    }
    if (water_temp_2 > water_temp_upper_limit_2 || water_temp_2 < water_temp_lower_limit_2) {
      current_faults = 6; //temp outside acceptable limits
    }
  }
  
  //skip waiting for startup delay if true
  if (!bypass_wait_time) {
    //wait for the startup time before enabling outputs
    if ((millis() - waited_time) < (startup_wait_time * 60000.0)) {
      if (waited_time == 0) {
        waited_time = millis();
      }
      float lcd_value = (startup_wait_time * 60000.0 - (millis() - waited_time)) / 1000.0;
      fault_messages[7] = "STARTING... WAIT " + (String)(int)lcd_value;
      current_faults = 7; //waiting for startup delay
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
