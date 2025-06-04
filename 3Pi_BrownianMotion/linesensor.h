// this #ifndef stops this file
// from being included mored than
// once by the compiler. 
#ifndef _LINESENSOR_H
#define _LINESENSOR_H

// Pin definitions
// We're going to be looping through pins
// quite often, so using an array instead.
// Using const saves on some memory, and 
// indicates we don't expect them to change.
#define NUM_SENSORS 5
const int sensor_pins[ NUM_SENSORS ] = { 12, A0, A2, A3, A4 };
#define LINE_EMIT   11

// Longest sensor read we will tolerate.
// Black seems to be about 2100, but we 
// don't need to detect absolute black.
// note, microseconds (us)
#define TIMEOUT_US  2000 


#define LINE_THRESHOLD 1700

// Class to operate the linesensor(s).
class LineSensor_c {
  public:

    float readings[ NUM_SENSORS ];
    float weighted_m;
  
    // Constructor, must exist.
    LineSensor_c() {
      // leaving this empty.
      // ensure initialise() is called
      // instead.
    } 

    void initialise() {
      int i;

      // Read sensors, set initial
      // readings to 0
      for( i = 0; i < NUM_SENSORS; i++ ) {
        pinMode( sensor_pins[i], INPUT );
        readings[i] = 0;
      }

      // Not weighted measurement
      weighted_m = 0;

      // Set IR LEDs off to save battery
      setEmitterOff();
    }

    // Returns whether or not the robot is on
    // the line.
    boolean onLine() {
      boolean on_line;
      int i;
      
      on_line = false;

      for( i = 0; i < NUM_SENSORS; i++ ) {
        if( readings[i] > LINE_THRESHOLD ) on_line = true;
      }

      return on_line;
    }

    // Pololu are using HIGH/LOW output
    // state to drive LEDs for the bumpers
    // or line sensors. to turn off, we set
    // the driving pin as an input.
    void setEmitterOff() {
        pinMode( LINE_EMIT, INPUT );
    }
    
    void setEmitterOn() {
        pinMode( LINE_EMIT, OUTPUT );
        digitalWrite( LINE_EMIT, HIGH );
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
      unsigned long us_end_t[NUM_SENSORS];
      unsigned long dt;

      // Activate IR LEDs
      setEmitterOn();

      // Charge capacitors
      for( i = 0; i < NUM_SENSORS; i++ ) {
        
        pinMode( sensor_pins[i], OUTPUT );
        digitalWrite( sensor_pins[i], HIGH );
      }
      
      delayMicroseconds(10);
      

      // Set sensors back to take measurement
      // Set up mesurements as timeout value and
      // we'll save any lower values.
      timeout = TIMEOUT_US;
      for( i = 0; i < NUM_SENSORS; i++ ) {
        pinMode( sensor_pins[i], INPUT );
        us_end_t[i] = TIMEOUT_US;
      }

      // Get start time
      us_start_t = micros();

      // This is the suggested "parallel read" procedure.
      // Using arrays means we can keep the code efficient
      // and flexible using a loop construct.
      // To be efficient, we keep track of
      // how many sensors are yet to finish
      // with variable "remaining".
      remaining = NUM_SENSORS;
      do {
        
        // Get current elapsed time.
        dt = micros() - us_start_t;

        // check each sensor
        for( i = 0; i < NUM_SENSORS; i++ ) {  // 0 4

          // Only update if the sensor hasn't finished 
          // yet.  We check against the timeout value which
          // was previously set into the variable.
          // After a successful read, this should be
          // < timeout
          // Otherwise, we have simply reached timeout and
          // the do/while loop will break anyway.
          if( us_end_t[i] == TIMEOUT_US ) { 

            // If low, sensor has finished read.
            if( digitalRead( sensor_pins[i] ) == LOW ) {
              
              us_end_t[i] = dt; // store dt for this sensor
                                // this also stops a subsequent
                                // dt being stored in the future.

              // Since we got a read here, we decrement
              // our remaining counter
              remaining--;
            } else {
                // the sensor has been read previously.
                // so we do nothing here (essentially, skip).

            }
          }
        }

        // either we've taken too long (timeout)
        // or we've done all sensors.
      } while( dt < timeout && remaining > 0 );

      // transfer unsigned long's into float array with typecasting
      for( i = 0; i < NUM_SENSORS; i++ ) readings[i] = (float)us_end_t[i];

      // Turn off IR LEDs to save battery
      setEmitterOff();

      // Done, phew!
      return;
    }

    // As suggested in labsheet 4 line following
    float calcWeightedMeasurement() {
      float sum;
      float m[NUM_SENSORS];  // to store measurements
      int i;
     
      // Extreme sensors!
      // If an extreme sensor is triggered, we
      // over-ride the true measurement and provide
      // a high value to force a sharp turn.
      if( readings[0] > LINE_THRESHOLD ) {
       
        weighted_m = 0.6;
        return weighted_m;
      }
      if( readings[4] > LINE_THRESHOLD ) {
        
        weighted_m = -0.6;
        return weighted_m;
      }


      // ...otherwise, use true weighted measurement

      sum = 0;
      for( i = 0; i < 3; i++ ) {

        // I'm hardcoding +1 here because we want sensors
        // DN 2, 3, 4.
        sum += readings[i+1];
        m[i] = readings[i+1];
      } 

      // Proportional normalisation
      for( i = 0; i < 3; i++ ) m[i] /= sum;

      // I've hard coded this bit - it could be improved.
      weighted_m = (m[0] + ( m[1] * 0.5)) - ((m[1] * 0.5) + m[2] );
      return weighted_m;
    }


    // Debug only
    void printMeasurements() {
      int i;
      for( i = 0; i < NUM_SENSORS; i++ ) {
          Serial.print( readings[i] );
          Serial.print(",");
      }
      Serial.print("\n");
    }

    

};



#endif
