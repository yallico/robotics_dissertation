// this #ifndef stops this file
// from being included mored than
// once by the compiler. 
#ifndef _BUMPSENSOR_H
#define _BUMPSENSOR_H

// Pin definitions
// We're going to be looping through pins
// quite often, so using an array instead.
// Using const saves on some memory, and 
// indicates we don't expect them to change.
#define NUM_B_SENSORS 2
const int bump_pins[ NUM_B_SENSORS ] = { 4, 5 };
#define BUMP_EMIT   11

// Longest sensor read we will tolerate.
// Black seems to be about 2100, but we 
// don't need to detect absolute black.
// note, microseconds (us)
#define TIMEOUT_US  50000 


#define BUMP_THRESHOLD 1700

// Class to operate the linesensor(s).
class BumpSensor_c {
  public:

    float readings[ NUM_B_SENSORS ];
    float weighted_m;
    float lp[ NUM_B_SENSORS ];
    // Constructor, must exist.
   BumpSensor_c() {
      // leaving this empty.
      // ensure initialise() is called
      // instead.
    } 

    void initialise() {
      int i;

      // Read sensors, set initial
      // readings to 0
      for( i = 0; i < NUM_B_SENSORS; i++ ) {
        pinMode( bump_pins[i], INPUT );
        readings[i] = 0;
        lp[i] = 0;
      }

      // Not weighted measurement
      weighted_m = 0;

      // Set IR LEDs off to save battery
      setEmitterOff();
    }

    // Returns whether or not the robot is on
    // the line.
    boolean isBumped() {
      boolean bumped;
      int i;
      
      bumped = false;

      for( i = 0; i < NUM_B_SENSORS; i++ ) {
        if( readings[i] > BUMP_THRESHOLD ) bumped = true;
      }

      return bumped;
    }

    void updateFilter() {
      
      for( int i = 0; i < NUM_B_SENSORS; i++ ) {
        float r = (float) readings[i];
        lp[i] = (lp[i] * 0.9) + (r * 0.1);
      }
    }

    // Pololu are using HIGH/LOW output
    // state to drive LEDs for the bumpers
    // or line sensors. to turn off, we set
    // the driving pin as an input.
    void setEmitterOff() {
        pinMode( BUMP_EMIT, INPUT );
    }


    // Note: For bump sensor we set EMIT
    // to LOW to activate the IR LEDs
    void setEmitterOn() {
        //pinMode( BUMP_EMIT, OUTPUT );
        //digitalWrite( BUMP_EMIT, LOW );
    }

    // To get a reading, we have to time how
    // long it takes for a capacitor to 
    // discharge.  The labsheet and docs has
    // some good notes. 
    // This code "solves" the parallel read
    // exerise in the labsheets.
    void getReading() {
      int i;
      int remaining;
      unsigned long timeout;
      unsigned long us_start_t;
      unsigned long us_end_t[NUM_B_SENSORS];
      unsigned long dt;

      // Activate IR LEDs
      setEmitterOn();

      // Charge capacitors
      for( i = 0; i < NUM_B_SENSORS; i++ ) {
        
        pinMode( bump_pins[i], OUTPUT );
        digitalWrite( bump_pins[i], HIGH );
      }
      
      delayMicroseconds(10);
      

      // Set sensors back to take measurement
      // Set up mesurements as timeout value and
      // we'll save any lower values.
      timeout = TIMEOUT_US;
      for( i = 0; i < NUM_B_SENSORS; i++ ) {
        pinMode( bump_pins[i], INPUT );
        us_end_t[i] = timeout;
      }

      // Get start time
      us_start_t = micros();

      // This is the suggested "parallel read" procedure.
      // Using arrays means we can keep the code efficient
      // and flexible using a loop construct.
      // To be efficient, we keep track of
      // how many sensors are yet to finish
      // with variable "remaining".
      remaining = NUM_B_SENSORS;
      do {
        
        // Get current elapsed time.
        dt = micros() - us_start_t;

        // check each sensor
        for( i = 0; i < NUM_B_SENSORS; i++ ) {

          // Only update if the sensor hasn't finished 
          // yet.  We check against the timeout value which
          // was previously set into the variable.
          // After a successful read, this should be
          // < timeout
          // Otherwise, we have simply reached timeout and
          // the do/while loop will break anyway.
          if( us_end_t[i] == timeout ) { 

            // If low, sensor has finished read.
            if( digitalRead( bump_pins[i] ) == LOW ) {
              
              us_end_t[i] = dt; // store dt for this sensor
                                // this also stops a subsequent
                                // dt being stored in the future.

              // Since we got a read here, we decrement
              // our remaining counter
              remaining--;
            } 
          }
        }

        // either we've taken too long (timeout)
        // or we've done all sensors.
      } while( dt < timeout && remaining > 0 );

      // transfer unsigned long's into float array with typecasting
      for( i = 0; i < NUM_B_SENSORS; i++ ) readings[i] = (float)us_end_t[i];

      // Turn off IR LEDs to save battery
      setEmitterOff();

      // Done, phew!
      return;
    }

    // As suggested in labsheet 4 line following
    float calcWeightedMeasurement() {
      float sum;
      float m[NUM_B_SENSORS];  // to store measurements
      int i;

      float r0,r1;
      float s0;
      float s1;

      // Debugging some hard coded values here
      r1 = (1988-680); // range
      r0 = (2036-640);

      
      m[0] = (float)readings[0];
      m[0] -= 640;
      m[0] /= r0;
      
      
      m[1] = (float)readings[1];
      m[1] -= 680;
      m[1] /= r1;
     
      return (m[0] - m[1]);
    }

    // Debug only
    void printMeasurements() {
      int i;
      for( i = 0; i < NUM_B_SENSORS; i++ ) {
          Serial.print( readings[i] );
          Serial.print(",");
      }
      Serial.print("\n");
    }

    

};



#endif
