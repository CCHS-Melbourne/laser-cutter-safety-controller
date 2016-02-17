# Laser Cutter V2 Safety Controller

*This controller monitors interlock switches, coolant flow rate and coolant temperature. It opens a relay if any of the interlocks are in a non-ready state and displays its status on a 20x4 character LCD. This has been designed and built for the Connected Community Hacker Space's Laser Cutter "V2".*

It uses the following hardware:

 * Arduino Uno
 * Adafruit Datlogging shield (SD card reader and DS1307 RTC)
 * Serial I2C backpack for HD44780-compatible LCD modules (PCF8574AT controller IC)
 * 20x4 character LCD with parallel interface
 * Adafruit plastic liquid flow sensor (450 pulses / L)
 * 2x DS18B20 1-wire temp sensors (temp 1 and temp 2)

See code for configuration values applicable to the CCHS laser cutter

TO DO: 

 * calibration of sensor values
 * create wiring diagram
 * implement data logging to SD card
 
ISSUES:

 * faulty IDC headers between the shield and the Uno are causing intermittent power connection and sensor issues
