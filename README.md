# Laser Cutter V2 Safety Controller

*This controller monitors interlock circuits, coolant flow rate and coolant temperature. It opens or closes a relay if any of the interlocks are in a non-ready state and displays its status on a 20x4 character LCD.*

It uses the following hardware:

 * Arduino Uno
 * Adafruit Datlogging shield (SD card reader and DS1307 RTC)
 * Serial I2C backpack for HD44780-compatible LCD modules (PCF8574AT controller IC)
 * 20x4 character LCD with parallel interface
 * Adafruit plastic liquid flow sensor (450 pulses / L)
 * DS18B20 1-wire temp sensor

See code for configuration values applicable to the CCHS laser cutter

TO DO: 

 * calibration of sensor values
