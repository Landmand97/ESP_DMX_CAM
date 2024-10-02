#include <Arduino.h>
#include <logger.h>

// put function declarations here:
void blink();





void setup() {
  // put your setup code here, to run once:
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  set_log_level(log_levels::INFO);
}

void loop() {
  // put your main code here, to run repeatedly:
  blink();
}

// blink function
void blink() {
  while (true) {
    log(log_levels::INFO, "Blinking LED\n", 0);
    // turn the LED on (HIGH is the voltage level)
    digitalWrite(LED_BUILTIN, HIGH);
    // wait for a second
    delay(500);
    // turn the LED off by making the voltage LOW
    digitalWrite(LED_BUILTIN, LOW);
    // wait for a second
    delay(10000);
    log(log_levels::INFO, "LED off\n", 0);
  }
}