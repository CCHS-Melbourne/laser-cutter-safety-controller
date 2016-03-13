//
// Hello :) visit hackmelbourne.org
//
// updated by Tim E 13/03/2016

//CONFIG START
const int mega_reset_pin = 9; //the output pin to pull low for resetting the RAMPS
const int hv_interlock_pin = 10; //the output pin driving the relay to interrupt the interlock circuit
const int ready_led_pin = 11; //LED to show HV supply is READY (no interlocks opened)
const int interlock_pin = 6; //interlock circuit pin (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int estop_pin = 7; //emergency stop button/switch (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int key_pin = 8; //security key (switch) (+5V to pin when CLOSED - should have PULL DOWN resistor)

const boolean use_flow_sensor = true; //enable the flow sensor
const int flow_sensor_pin = 2; //flow rate sensor pin
const float flow_rate_upper_limit = 10.0; //(litres per minute)upper limit of flow rate
const float flow_rate_lower_limit = 1.5; //(litres per minute)lower limit of flow rate

const boolean use_temp_sensor_1 = true; //enable the temp sensor 1
const int temp_sensor_pin_1 = 3; //temperature sensor pin
const float water_temp_upper_limit_1 = 28.0; //(degrees C) water temp upper limit
const float water_temp_lower_limit_1 = 14.0; //(degrees C) water temp lower limit

const boolean use_temp_sensor_2 = false; //use temp sensor 2 (currently disabled as it is not wired properly)
const int temp_sensor_pin_2 = 4; //temperature sensor pin
const float water_temp_upper_limit_2 = 28.0; //(degrees C) water temp upper limit
const float water_temp_lower_limit_2 = 14.0; //(degrees C) water temp lower limit

//bypasses
const boolean bypass_sensors = false; //ignore temp and flow sensors - for testing switches with machine off
const boolean bypass_interlocks = true; //don't disable the laser if the interlocks are open (set true to not monitor the interlock circuit)
const float startup_wait_time = 1.0; //(minutes) wait time before enabling output. ensures the coolant is flowing
const boolean bypass_wait_time = true; //used for testing
//CONFIG END

//Date and time functions using the DS1307 RTC connected via I2C and Wire lib
#include "Wire.h" //I2C
#include "LiquidCrystal_I2C.h" //LCD
#include "DallasTemperature.h" //temp sensor

//create the LCD object. firs param is the address. no idea what other params are, they were in the tronixlabs's example code. seems to work.
LiquidCrystal_I2C	lcd(0x3f,2,1,0,4,5,6,7); //0x27 is the default I2C bus address for an unmodified backpack, others use 0x3f

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
  if (use_flow_sensor) {
    attachInterrupt(digitalPinToInterrupt(flow_sensor_pin), flow_sense, RISING); //set up the hardware interrupt to call 'flow_sense' on the RISING  
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
  //show a loading message. the sketch pauses for a while while the SD card module loads at power-on
  //(the first delay(); will go for longer while this happens)
  lcd.home();
  lcd.print("Loading..."); 
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

//average out the flow sensor values
boolean average_complete = false; //make sure we have used all values before doing the calculation or it will be skewed by the default values of 0 for the first minute.
int currentPulseValue = 0; //which was the last value to be updated
int flow_pulses_avg[] = { 0,0,0,0,0,0 }; //store 6 pulse counts - every ten seconds for a minute's worth of values

//should be called every 10 seconds by the loop
void flow_pulse_update () {
  flow_pulses_avg[currentPulseValue] = flow_pulses; //get value from the interrupt
  flow_pulses = 0; //reset the interrupt
  if (currentPulseValue == 5) {
    currentPulseValue = 0; //reset the value to update
    if (!average_complete) {
      average_complete = true; //tell the loop we have got all the values stored now
    }
  }
  else{
    currentPulseValue++; //increment the value we are saving into
  }
}

//calculate the average flowrate pulses / min upon request
float flow_pulse_average () {
  return (float)((flow_pulses_avg[0] + flow_pulses_avg[1] + flow_pulses_avg[2] + flow_pulses_avg[3] + flow_pulses_avg[4] + flow_pulses_avg[5]) / 6.00);
}

int long loop_last_millis = 0; //track the exact ms length of the loop so we calculate exact flow rate values
int long loop_last_flow_update = 0; //track the exact ms since updating the flow rate values last
float loop_duration = 1000; //delay between LCD updates, in ms
void loop () {

  //check when flow rates were last updated and update them
  if (use_flow_sensor) {
    if (!((millis() - loop_last_flow_update) < 1000)) { //every 11 sec
      loop_last_flow_update = millis();
      flow_pulse_update();
    }
  }
  
  //track exactly how long since loop last ran and make sure we run it every x ms exactly
  if ((millis() - loop_last_millis) < loop_duration) {
    return;
  }
  else {
    loop_last_millis = millis();
  }

  //450 pulses per litre as per https://www.adafruit.com/products/828
  //convert to L/min based on number of pulses since the loop last ran 
  float flow_rate; 
  if (use_flow_sensor){
    flow_rate = (flow_pulse_average() / 450.0) * 60.0 * 6.0;
  }

  //track which fault has ocurred
  int current_faults = 0; //0 = all good, 1 = interlocks, 2 = estop, 3 = key off, 4 = flow rate, 5 = temperature

  //display some values
  lcd.home(); //set cursor to 0,0
  lcd.print("Laser Cutter V2 " + display_anim[cur_anim]);
  cur_anim++;
  if (cur_anim == 4) {
    cur_anim = 0;
  }
  
  //get water temp and display it
  lcd.setCursor(0,1); //go to start of 2nd line
  float water_temp_1;
  float water_temp_2;
  if (use_temp_sensor_1) {
    water_temp_1 = temp_sensor_1.getTempCByIndex(0);
    temp_sensor_1.requestTemperatures();
  }
  if (use_temp_sensor_2) {
    water_temp_2 = temp_sensor_2.getTempCByIndex(0);
    temp_sensor_2.requestTemperatures();
  }
  if (!use_temp_sensor_1){
    lcd.print("Temp: ");
    lcd.print(water_temp_2); //print reading
  }
  else if( !use_temp_sensor_2) {
    lcd.print("Temp: ");
    lcd.print(water_temp_1); //print reading
  }
  else {
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
