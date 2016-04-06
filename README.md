# Laser Cutter V2 Safety Controller

*This controller monitors interlock switches, coolant flow rate and coolant temperature for up to two one-wire dallas temp sensors. It opens a relay if any of the conditions are in a non-ready state and displays its status on a 20x4 character LCD. This has been designed and built for the Connected Community Hacker Space's Laser Cutter "V2".*

It uses the following hardware:

 * Arduino Uno
 * Serial I2C backpack for HD44780-compatible LCD modules (PCF8574AT controller IC)
 * 20x4 character LCD with parallel interface
 * Adafruit plastic liquid flow sensor (450 pulses / L)
 * 2x DS18B20 1-wire temp sensors (temp 1 and temp 2)

See code for configuration values applicable to the CCHS laser cutter. This code can be applied to other projects if needed but keep in mind it was written with this laser cutter in mind.

TO DO: 

 * increase the accuracy of the flow sensor by sampling pulses for a longer period than 250ms (seems to only get 3-4 pulses in that time)
 
ISSUES:

 * none i'm aware of
