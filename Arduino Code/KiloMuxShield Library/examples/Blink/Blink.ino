#include <KiloMux.h>          // Import class declaration
#include <KiloMuxDefs.h>      // Import KiloMux defines

KiloMux KMShield;             // KiloMux Shield   

#define MS_ON  40             // Number of milliseconds the LED will be on 
#define MS_OFF 4              // Number of milliseconds the LED will be off

unsigned int ledOutput = 1;     // KiloMux output where we already connected an LED (1-16)

void setup() {
  // Nothing here  
}

void loop() {
  KMShield.digitalWriteKM(ledOutput, HIGH);     // Turn led on
  delay(MS_ON);                                 // Wait. LED is on.
  KMShield.digitalWriteKM(ledOutput, LOW);      // Now turn it off. 
  delay(MS_OFF);                                // Wait. It's off.
}

