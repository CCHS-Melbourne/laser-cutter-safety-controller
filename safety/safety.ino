//
// Hello :) visit hackmelbourne.org
//
// TODO: data logging using SD card
//

//CONFIG START
const int mega_reset_pin = 9; //the output pin to pull low for resetting the RAMPS
const int hv_interlock_pin = 10; //the output pin driving the relay to interrupt the interlock circuit
const int ready_led_pin = 11; //LED to show HV supply is READY (no interlocks opened)
const int interlock_pin = 6; //interlock circuit pin (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int estop_pin = 7; //emergency stop button/switch (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int key_pin = 8; //security key (switch) (+5V to pin when CLOSED - should have PULL DOWN resistor)
const int flow_sensor_pin = 2; //flow rate sensor pin
const float flow_rate_upper_limit = 3.0; //upper limit of flow rate (litres per minute)
const float flow_rate_lower_limit = 0.5; //lower limit of flow rate (litres per minute)
const int temp_sensor_pin = 3; //temperature sensor pin
const float water_temp_upper_limit = 35.0; //water temp upper limit in degC
const float water_temp_lower_limit = 8.0; //water temp lower limit in degC

const boolean bypass_sensors = true; //ignore temp and flow sensors - for testing switches with machine off
const boolean bypass_interlocks = false; //don't disable the laser if the interlocks are open (set true to not monitor the interlock circuit)
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
OneWire tempWire(temp_sensor_pin);
DallasTemperature temp_sensor(&tempWire);

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
  temp_sensor.begin();
  
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

//the messages to display for each fault condition - each string should be 12 chars long ("STATUS: " + message = 20 chars)
String fault_messages[] = { "READY       " , "CLOSE COVERS" , "E-STOP PRESS" , "KEY OFF     " , "CHECK FLOW  " , "CHECK TEMP  " };

//characters to animate to show the controller is responding
String display_anim[] = { "-   ", " -  ", "  - ", "   -" };
int cur_anim = 0;

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
  float water_temp = temp_sensor.getTempCByIndex(0);
  
  //display temp
  lcd.setCursor(0,1); //go to start of 2nd line
  temp_sensor.requestTemperatures();
  lcd.print("Temp: ");
  lcd.print(water_temp); //print reading
  lcd.print("degC    ");

  //display the flow rate
  lcd.setCursor(0,2); //3rd line
  lcd.print("Flow: ");
  lcd.print(flow_rate);
  lcd.print(" (l/pm)");

  //fault values:
  //0 = all good, 1 = interlocks, 2 = estop, 3 = key off, 4 = flow rate, 5 = temperature
  
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
    if (water_temp > water_temp_upper_limit || water_temp < water_temp_lower_limit) {
      current_faults = 5; //temp outside acceptable limits
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
  lcd.setCursor(0,3); //3rd line
  lcd.print("STATUS: " + fault_messages[current_faults]);
  
}
