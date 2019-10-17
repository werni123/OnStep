// -----------------------------------------------------------------------------------
// Focusers

#pragma once

#include "StepperDC.h"

// time to write position to nv after last movement of Focuser 1/2, default = 5 minutes
#define FOCUSER_WRITE_DELAY 1000L*60L*5L

class focuserDC {
  public:
    void init(int stepPin, int dirPin, int enPin, int nvAddress, int nvTcfCoef, int nvTcfEn, float maxRate, double stepsPerMicro, double min, double max, double minRate) {

      // init only happens once, on the first call and is ignored otherwise
      dcMotor.init(stepPin,dirPin,enPin,maxRate);
      
      this->nvAddress=nvAddress;
      this->minRate=minRate;
      this->maxRate=maxRate;
      this->spm=stepsPerMicro;
    
      spos=readPos();
      // constrain step position
      long lmin=(long)(min*spm); if (spos<lmin) { spos=lmin; target.part.m=spos; target.part.f=0; }
      long lmax=(long)(max*spm); if (spos>lmax) { spos=lmax; target.part.m=spos; target.part.f=0; }
      target.part.m=spos; target.part.f=0;
      lastPos=spos;
      delta.fixed=0;

      // steps per second, maximum
      spsMax=(1.0/maxRate)*1000.0;
      // microns per second default
      setMoveRate(500);

      // default min/max
      setMin(lmin);
      setMax(lmax);

      nextPhysicalMove=millis()+(unsigned long)maxRate;
      lastPhysicalMove=nextPhysicalMove;
    }

    // DC motor control
    boolean isDcFocuser() { return true; }
    void initDcPower(int nvDcPower) { this->nvDcPower=nvDcPower; powerFor1mmSec=nv.read(nvPwrAxis); }
    void setDcPower(byte power) { this->powerFor1mmSec=power; nv.write(nvDcPwrAxis,powerFor1mmSec); }
    byte getDcPower() { return powerFor1mmSec; }
    void setPhase1() { phase1=true; }
    void setPhase2() { phase1=false; }

    // get step size in microns
    double getStepsPerMicro() { return spm; }

    // minimum position in steps
    void setMin(long min) { smin=min; }
    long getMin() { return smin; }

    // maximum position in steps
    void setMax(long max) { smax=max; }
    long getMax() { return smax; }

    // sets logic state for reverse motion
    void setReverseState(int reverseState) {
      if (reverseState == HIGH) reverse=true; else reverse=false;
    }

    // sets logic state for disabling stepper driver
    void setDisableState(boolean disableState) {
      dcMotor.setDisableState(disableState);
    }

    // temperature compensation
    void setTcfCoef(double coef) {
      // not supported
    }
    double getTcfCoef() {
      return 0;
    }
    void setTcfEnable(boolean enable) {
      // not supported
    }
    boolean getTcfEnable() {
      return false;
    }

    // allows enabling/disabling stepper driver
    void powerDownActive(boolean active) {
    }

    // set movement rate in microns/second, from minRate to 1000
    void setMoveRate(double rate) {
      constrain(rate,minRate,1000);
      moveRate=rate*spm;                                    // in steps per second, for a DC motor a step is 1 micron.
      if (moveRate > spsMax) moveRate=spsMax;               // limit to maxRate
    }

    // check if moving
    bool moving() {
      if ((delta.fixed != 0) || ((long)target.part.m != spos)) return true; else return false;
    }

    // move in
    void startMoveIn() {
      // rate is some fraction of 1 millimeter per second so this is the % power for 1 millimeter per second motion
      dcMotor.setPower((moveRate/1000.0)*powerFor1mmSec);
      delta.fixed=doubleToFixed(+moveRate/100.0); // in steps per centi-second
    }

    // move out
    void startMoveOut() {
      dcMotor.setPower((moveRate/1000.0)*powerFor1mmSec);
      delta.fixed=doubleToFixed(-moveRate/100.0); // in steps per centi-second
    }

    // stop move
    void stopMove() {
      delta.fixed=0;
      target.part.m=spos; target.part.f=0;
    }

    // get position in steps
    long getPosition() {
      return spos;
    }

    // sets current position in steps
    void setPosition(long pos) {
      spos=pos;
      if (spos < smin) spos=smin; if (spos > smax) spos=smax;
      target.part.m=spos; target.part.f=0;
      lastMove=millis();
    }

    // sets target position in steps
    void setTarget(long pos) {
      dcMotor.setPower((moveRate/1000.0)*powerFor1mmSec);
      target.part.m=pos; target.part.f=0;
      if ((long)target.part.m < smin) target.part.m=smin; if ((long)target.part.m > smax) target.part.m=smax;
    }

    // sets target relative position in steps
    void relativeTarget(long pos) {
      dcMotor.setPower((moveRate/1000.0)*powerFor1mmSec);
      target.part.m+=pos; target.part.f=0;
      if ((long)target.part.m < smin) target.part.m=smin; if ((long)target.part.m > smax) target.part.m=smax;
    }

    // do automatic movement
    void move() {
      target.fixed+=delta.fixed;
      // stop at limits
      if (((long)target.part.m < smin) || ((long)target.part.m > smax)) delta.fixed=0;
    }

    // follow( (trackingState == TrackingMoveTo) || guideDirAxis1 || guideDirAxis2) );
    void follow(boolean slewing) {
          
      // write position to non-volatile storage if not moving for FOCUSER_WRITE_DELAY milliseconds
      if ((spos != lastPos)) { lastMove=millis(); lastPos=spos; }
      if (!slewing && (spos != readPos())) {
        // needs updating and enough time has passed?
        if ((long)(millis()-lastMove) > FOCUSER_WRITE_DELAY) writePos(spos);
      }

      unsigned long tempMs=millis();
      if ((long)(tempMs-nextPhysicalMove) > 0) {
        nextPhysicalMove=tempMs+(unsigned long)maxRate;
        if (moving()) {
          if ((spos < (long)target.part.m) && (spos < smax)) {
            if (reverse) dcMotor.setDirectionIn(); else dcMotor.setDirectionOut();
            spos++;
            lastPhysicalMove=millis();
          } else
          if ((spos > (long)target.part.m) && (spos > smin)) {
            if (reverse) dcMotor.setDirectionOut(); else dcMotor.setDirectionIn();
            spos--;
            lastPhysicalMove=millis();
          }
        }
      }

      if (moving()) {
        if (phase1) dcMotor.setPhase1(); else dcMotor.setPhase2();
        dcMotor.enabled(true);
        dcMotor.poll();
        lastPollingTime=millis();
        wasMoving=true;
      } else if (wasMoving) if ((long)(tempMs-lastPollingTime) > maxRate+1) { dcMotor.enabled(false); wasMoving=false; }
    }

    void savePosition() {
      writePos(spos);
    }
  
  private:
    long readPos() {
      return nv.readLong(nvAddress);
    }

    void writePos(long p) {
      nv.writeLong(nvAddress,(long)p);
    }

    // parameters
    int nvAddress=-1;
    long minRate=-1;
    long maxRate=-1;
    long spsMax=-1;
    long umin=0;
    long smin=0;
    long umax=1000;
    long smax=1000;
    bool reverse=false;
    bool phase1=true;
    long nvDcPower=-1;

    // conversion
    double spm=1.0;
    double powerFor1mmSec=0.0;

    // position
    fixed_t target;
    long spos=0;
    long lastPos=0;

    // automatic movement
    double moveRate=0.0;
    fixed_t delta;
    bool wasMoving=false;

    // timing
    unsigned long lastMs=0;
    unsigned long lastMove=0;
    unsigned long lastPhysicalMove=0;
    unsigned long nextPhysicalMove=0;
    unsigned long lastPollingTime=0;
};
