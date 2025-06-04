// this #ifndef stops this file
// from being included mored than
// once by the compiler. 
#ifndef _KINEMATICS_H
#define _KINEMATICS_H

#include <math.h>

const float count_per_rev = 358.3; 
const float wheel_radius  = 16.5;    // mm
const float wheel_sep     = 42.5;    // mm, radius of robot
const float mm_per_count  = ( 2 * wheel_radius * PI ) / count_per_rev;

// Class to track robot position.
class Kinematics_c {
  public:
    float x,y,theta;
    long last_enc_left;
    long last_enc_right;
  
    // Constructor, must exist.
    Kinematics_c() {

    } 

    void initialise( float start_x, float start_y, float start_th ) {
      last_enc_left = 0;
      last_enc_right = 0;
      x = start_x;
      y = start_y;
      theta = start_th;
    }

    void reset( float start_x, float start_y, float start_th, long enc_left, long enc_right ) {
      last_enc_left = enc_left;
      last_enc_right = enc_right;
      x = start_x;
      y = start_y;
      theta = start_th;
    }

    // Here I have opted to use encoder counts rather than 
    // wheel velocity.  Either way will work.  This way, I
    // ignore time.  With velocity, the difference in time
    // between updates is important (distance = speed / time )
    void update( long enc_left, long enc_right ) {
        long delta_left;
        long delta_right;
        float mean_delta;
        float x_contribution;
        float th_contribution;
               
        delta_left = enc_left - last_enc_left;
        delta_right = enc_right - last_enc_right;

        // Used last encoder values, so now update to
        // current for next iteration
        last_enc_left = enc_left;
        last_enc_right = enc_right;
        
        // Work out x contribution in local frame.
        mean_delta = (float)delta_left;
        mean_delta += (float)delta_right;
        mean_delta /= 2;

        x_contribution = mean_delta * mm_per_count;

        // Work out theta in local frame
        th_contribution = (float)delta_right;
        th_contribution -= (float)delta_left;
        th_contribution *= mm_per_count;
        th_contribution /= (wheel_sep *2);


        // Update global frame
        x += x_contribution * cos( theta );
        y += x_contribution * sin( theta );
        theta += th_contribution;
       
    }

    float getDistanceFromOrigin() {
      float d;
      d = sqrt( pow( x, 2) + pow( y, 2) );
      return d;
    }

    void print() {
      Serial.print( x );
      Serial.print( "," );
      Serial.print( y );
      Serial.print( "," );
      Serial.println( theta );

      return;
      
    }

};



#endif
