// this #ifndef stops this file
// from being included mored than
// once by the compiler. 
#ifndef _SPEED_H
#define _SPEED_H

// Class to contain generic PID algorithm.
class Speed_c {
  public:

    // Speed variables
    float speed;
    float ave_spd;
    float alpha;
    long last_enc_count;
    
    
    // To determine time elapsed.
    unsigned long ms_last_t;
  
    // Constructor, must exist.
    Speed_c() {
      // leaving this empty.
      // ensure initialise() is called
      // instead.
    } 

    // To setup
    // Assumes initialised at power on.
    // Use reset to start with a non-0
    // encoder count
    void initialise(  ) {
       alpha = 0.2;
       ave_spd = 0;
       last_enc_count = 0;
       ms_last_t = millis();
       speed = 0;
    }

    void reset( long enc_count ) {
      last_enc_count = enc_count;
      ave_spd = 0;
      ms_last_t = millis();
      speed = 0;
    }

    // Receives the encoder count.
    // Determines difference.
    // Saves speed.
    void update( long enc_count ) {
      unsigned long ms_now_t;
      unsigned long ms_dt;
      float float_dt;
      long diff_enc;
      float float_diff;
      
      // Grab time to calc elapsed time.
      ms_now_t = millis();
      ms_dt = ms_now_t - ms_last_t;

      // ms_last_t has been used, so update
      // it for the next call of this update.
      ms_last_t = millis();
      
      // typecasting the different of two
      // unsigned longs is safer.
      float_dt = (float)ms_dt;

      // NOTE: A serious error can occur
      // here if dt is 0, this causes divide
      // by zero errors.  This can happen if
      // PID.update() is called faster than 1ms.
      // Here, we catch the error by returning
      // the last feedback value.
      if( float_dt == 0 ) return;

      // Might need to inverse this.
      diff_enc = enc_count - last_enc_count;

      // Update our "last" encoder count to 
      // current for the next call to update
      last_enc_count = enc_count;

      // Speed
      float_diff = (float)diff_enc;
      speed = float_diff / float_dt;

      // A low pass filtered version
      // I found speed estimation to be a bit 
      // jumpy.  I think this might be quantisation error
      // because of low resolution (only 358.3 counts per rev)
      // Filtering risks inducing some lag
      ave_spd = (ave_spd * (1-alpha)) + ( speed * alpha );

      // Done.
      return;
    }

};



#endif
