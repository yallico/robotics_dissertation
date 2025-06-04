#include <math.h>

#include "encoders.h"
#include "motors.h"
#include "pid.h"
#include "speed.h"
#include "kinematics.h"
#include "linesensor.h"
#include "bumpsensors.h"
#include "i2c_slave.h"

#define I2C_SLAVE_ADDR  0x04 // I2C address for the pololu
volatile bool should_run = false;  // Flag for Brownian motion

unsigned long ms_update_ts;  // A timestamp in ms to schedule a general update of the system.
#define UPDATE_MS 50      // updates at 20ms intervals

unsigned long h_update_ts;   // A timestamp to schedule a recalculation of heading
#define HEADING_UPDATE 500 // update every 0.5 seconds

unsigned long spd_update_ts;  // a timestamp to estimate the wheel speeds
#define SPD_UPDATE 5      // fast, could be slower?

// Instances of classes, from header files.
Motors_c motors;    // Both motors controlled.

PID_c spd_pid_left; // PID for l/r respectively.
PID_c spd_pid_right;

Speed_c spd_left; // I created a class to keep track
Speed_c spd_right;// of left/right wheel speed

Kinematics_c pose; // to calculate position + rotation.
PID_c heading_pid;  // to perform turn manouvers/drive straight
LineSensor_c line_sensors; // handles weighted measurement of line.
BumpSensor_c bump_sensors; // Reads the bump sensors.


// To drive in a constant direction, we need a persistent
// variable to set a demand theta.  E.g., if starting
// at 90*, keep pursuing 90* even though theta deviates.
// So we declare this outside loop()
float demand_theta;

void onReceiveI2C(int numBytes) {
  if (numBytes < 1) return;
  char command = Wire.read();
  if (command == 'S') {
    should_run = true;
    spd_pid_left.reset();
    spd_pid_right.reset();
    heading_pid.reset();
  } else if (command == 'X') {
    should_run = false;
    motors.setMotorsPWM(0, 0);
  }
}

  // put your setup code here, to run once:
void setup() {

  setupEncoder0();
  setupEncoder1();

  // Setup motors.
  motors.initialise();

  // init PID.
  spd_pid_left.initialise( 20.0, 0.1, 0.0 );
  spd_left.initialise();
  spd_pid_right.initialise( 20.0, 0.1, 0.0 );
  spd_right.initialise();

  // Heading controller.
  // Use to follow the line, and to operate
  // some specific motion control
  heading_pid.initialise( 0.5, 0.0001, 0.03 );

  // Odometry
  pose.initialise(0, 0, 0);

  // I2C
  i2c_slave_init( I2C_SLAVE_ADDR );

  // Serial for debugging.
  Serial.begin(9600);
  delay(1000);
  Serial.println("***RESTART***");

  ms_update_ts = millis();  // General system update clock

  // reset PID because we have delayed
  // by 1000ms!
  spd_pid_left.reset();
  spd_pid_right.reset();
  heading_pid.reset();

  // Not currently using bump sensors
  //bump_sensors.initialise();
  //bump_sensors.getReading();
  //bump_sensors.lp[0] = (float)bump_sensors.readings[0];
  //bump_sensors.lp[1] = (float)bump_sensors.readings[1];

  // Not currently using line sensors
  //line_sensors.initialise();

}




// put your main code here, to run repeatedly:
void loop() {
  // Run if I2C has set the flag.
  if (!should_run) {
    delay(100);  // Idle if not running
    return;
  }

  // Update heading, direction of movement.
  // 250ms, could be more or less.
  if ( millis() - h_update_ts > HEADING_UPDATE ) {
    h_update_ts = millis();


    // Adjust heading.
    // Gaussian means most often a small adjustment
    // but sometimes big.
    // randGaussian( <mean>, <standard deviation> )
    // mean = values centre on
    // standard deviation = spread, likelihood of big values
    // Can be positive or negative.
    float heading_adjust = randGaussian( 0.0, 0.5);

    // Set a persistent demand for rotation based on
    // the current kinematic theta
    demand_theta = pose.theta + heading_adjust;

  }
  
  if( millis() - spd_update_ts > SPD_UPDATE ) {
    spd_update_ts = millis();
    
    // This is the speed estimate not the 
    // pid routines. 
    spd_left.update( count_e1 );
    spd_right.update( count_e0 );

    /*
    Serial.print( count_e1);
    Serial.print(",");
    
    Serial.print( count_e0 );
    Serial.print(",");
    */
    Serial.print( pose.theta );
    Serial.print(",");
    Serial.println( demand_theta );
  }

  // General update
  if ( millis() - ms_update_ts > UPDATE_MS ) {
    ms_update_ts = millis();

    
    
    // Update robot position (kinematics)
    pose.update( count_e1, count_e0 );

    // Tell robot to turn - does PID motor control
    turnToDemandTheta( 0.3, demand_theta, pose.theta );
  }

  //Serial.println( spd_left.speed );
  delay(2);
}

/*
 *  From: http://www.taygeta.com/random/gaussian.html
 *  
 *  This routine is a little troubling because it is 
 *  non-deterministic (we don't know when it will solve)
 *  and computationally expensive.
 *  However, using gaussian distribution is useful for 
 *  creating "controllable" random motion, Brownian Motion.
 */
float randGaussian( float mean, float sd ) {
   float x1, x2, w, y;
   
   do {
     // Adaptation here because arduino random() returns a long
     x1 = random(0,2000) - 1000;
     x1 *= 0.001;
     x2 = random(0,2000) - 1000;
     x2 *= 0.001;
     w = (x1 * x1) + (x2 * x2); 
     
   } while( w >= 1.0 );
   
   w = sqrt( (-2.0 * log( w ) )/w );
   y = x1 * w;
   
   return mean + y * sd;
   
}

// Returns whether the operation is complete.
boolean turnToDemandTheta( float fwd_bias, float th_demand, float th_measurement ) {
  // https://stackoverflow.com/questions/1878907/the-smallest-difference-between-2-angles
  // Some crazy atan2 magic.
  // Treats the difference in angle as cartesian x,y components.
  // Cos and Sin are effectively wrapping the values between -1, +1, with a 90 degree phase.
  // So we can pass in values larger than TWO_PI or less than -TWO_PI fine.
  // atan2 returns -PI/+PI, giving us an indication of direction to turn.
  float diff = atan2( sin( ( th_demand - th_measurement) ), cos( (th_demand - th_measurement) ) );

  // Use heading PID to minimise error in orientation to 0
  float steer_fb = heading_pid.update( 0, diff );
  
  // Limit magnitude of steering
  if ( steer_fb > 0.4 ) steer_fb = 0.4;
  if ( steer_fb < -0.4 ) steer_fb = -0.4;
  //Serial.println( steer_fb );
  
  // Reduce the forward bias (movement) depending on
  // how much turning is required.
  // It turns out allowing the robots to reverse a little
  // (negative speed) actually improves their ability to
  // face the same way, as it seems to stop collisions
  float fwd = fwd_bias - abs(steer_fb);
  
  // Get appropriate PID speed feedback for left and right
  // wheel by combining the forward bias and steering component
  float fb_l, fb_r;
  fb_l = spd_pid_left.update( fwd + steer_fb, spd_left.ave_spd );
  fb_r = spd_pid_right.update( fwd - steer_fb, spd_right.ave_spd );
  motors.setMotorsPWM( fb_l, fb_r );
  return false;

}
