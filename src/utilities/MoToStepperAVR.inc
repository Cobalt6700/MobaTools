// MoToStepper: ISR functions and Methods that are not used in ESP8266
// The difference in these functions is too big to do it with function internal ifdef's


#pragma GCC optimize "O3"   // optimize ISR for speed
void stepperISR(uint8_t cyclesLastIRQ) {
    SET_TP4;
    stepperData_t *stepperDataP;         // actual stepper data in IRQ
    uint8_t spiChanged, changedPins, bitNr;
    //SET_TP1;SET_TP4; // Oszimessung Dauer der ISR-Routine
    spiChanged = false;
    #ifdef __AVR_MEGA__
    interrupts(); // allow nested interrupts, because this IRQ may take long
    #endif
    stepperDataP = stepperRootP;
    // ---------------Stepper motors ---------------------------------------------
    while ( stepperDataP != NULL ) {
        //CLR_TP1;    // spike for recognizing start of each stepper
        if ( stepperDataP->output == A4988_PINS ) {
            // reset step pulse - pulse is max one cycle lenght
            #ifdef FAST_PORTWRT
            noInterrupts();
            *stepperDataP->portPins[0].Adr &= ~stepperDataP->portPins[0].Mask;
            interrupts();
            #else
            digitalWrite( stepperDataP->pins[0], LOW );
            #endif
        }
        if ( stepperDataP->rampState >= rampStat::CRUISING ) {
            // only active motors
            //SET_TP2;
            stepperDataP->cycCnt+=cyclesLastIRQ;
            if ( stepperDataP->cycCnt >= stepperDataP->aCycSteps ) {
                // Do one step
                SET_TP2;
                stepperDataP->cycCnt = 0 ;
                // update position for absolute positioning
                stepperDataP->stepsFromZero += stepperDataP->patternIxInc;
                
                // sign of patternIxInc defines direction
                int8_t _patIx;
                _patIx = stepperDataP->patternIx + stepperDataP->patternIxInc;
                if ( _patIx > 7 ) _patIx = 0;
                if ( _patIx < 0 ) _patIx += 8;
                stepperDataP->patternIx = _patIx;
                CLR_TP2;SET_TP2;
                // store pattern data
                #ifdef __AVR_MEGA__
                noInterrupts(); // because of read modify write actions in setting outputs
                #endif
                switch ( stepperDataP->output ) {
                  #ifdef __AVR_MEGA__
                  case PIN4_7:
                    PORTD = (PORTD & 0x0f) | ( stepPattern[ _patIx ] <<4 );   
                    break;
                  case PIN8_11:
                    PORTB = (PORTB & 0xf0) | ( stepPattern[ _patIx ] );   
                    break;
                  #endif
                  case SPI_1:
                    //SET_TP2;
                    spiData[0] = (spiData[0] & 0xf0) | ( stepPattern[ _patIx ] );
                    spiChanged = true; 
                    //CLR_TP2;
                    break;
                  case SPI_2:
                    spiData[0] = (spiData[0] & 0x0f) | ( stepPattern[ _patIx ] <<4 );
                    spiChanged = true;
                    //CLR_TP2;
                    break;
                  case SPI_3:
                    spiData[1] = (spiData[1] & 0xf0) | ( stepPattern[ _patIx ] );   
                    spiChanged = true;
                    break;
                  case SPI_4:
                    spiData[1] = (spiData[1] & 0x0f) | ( stepPattern[ _patIx ] <<4 );
                    spiChanged = true;
                    break;
                  case SINGLE_PINS : // Outpins are individually defined
                    changedPins = stepPattern[ _patIx ] ^ stepperDataP->lastPattern;
                    for ( bitNr = 0; bitNr < 4; bitNr++ ) {
                        if ( changedPins & (1<<bitNr ) ) {
                            // bit Changed, write to pin
                            if ( stepPattern[ _patIx ] & (1<<bitNr) ) {
                                #ifdef FAST_PORTWRT
                                *stepperDataP->portPins[bitNr].Adr |= stepperDataP->portPins[bitNr].Mask;
                                #else
                                digitalWrite( stepperDataP->pins[bitNr], HIGH );
                                #endif
                            } else {
                                #ifdef FAST_PORTWRT
                                *stepperDataP->portPins[bitNr].Adr &= ~stepperDataP->portPins[bitNr].Mask;
                                #else    
                                digitalWrite( stepperDataP->pins[bitNr], LOW );
                                #endif    
                            }
                        }
                    }
                    stepperDataP->lastPattern = stepPattern[ _patIx ];
                    break;
                  case A4988_PINS : // output step-pulse and direction
                    // direction first
                    //SET_TP2;
                    if ( stepperDataP->patternIxInc > 0 ) {
                        // turn forward 
                        #ifdef FAST_PORTWRT
                        *stepperDataP->portPins[1].Adr |= stepperDataP->portPins[1].Mask;
                        #else
                        digitalWrite( stepperDataP->pins[1], HIGH );
                        #endif
                    } else {
                        // turn backwards
                        #ifdef FAST_PORTWRT
                        *stepperDataP->portPins[1].Adr &= ~stepperDataP->portPins[1].Mask;
                        #else
                        digitalWrite( stepperDataP->pins[1], LOW );
                        #endif
                    }    
                    // Set step pulse 
                    nextCycle = MIN_STEP_CYCLE/2; // will be resettet in max 2 cycles
                    #ifdef FAST_PORTWRT
                    *stepperDataP->portPins[0].Adr |= stepperDataP->portPins[0].Mask;
                    #else
                    digitalWrite( stepperDataP->pins[0], HIGH );
                    #endif
                    break;
                  default:
                    // should never be reached
                    break;
                }
                #ifdef __AVR_MEGA__
                interrupts();
                #endif
                CLR_TP2;
                // ------------------ check if last step -----------------------------------
                if ( --stepperDataP->stepCnt == 0 ) {
                    // this was the last step.
                    if (stepperDataP->stepCnt2 > 0 ) { // check if we have to start a movement backwards
                        // yes, change Direction and go stpCnt2 Steps
                        stepperDataP->patternIxInc = -stepperDataP->patternIxInc;
                        stepperDataP->stepCnt = stepperDataP->stepCnt2;
                        stepperDataP->stepCnt2 = 0;
                        stepperDataP->rampState = rampStat::RAMPACCEL;
                    } else {    
                        if (stepperDataP->enablePin != 255) {
                            // enable is active, wait for disabling
                            stepperDataP->aCycSteps = stepperDataP->cycDelay;
                            stepperDataP->rampState = rampStat::STOPPING;
                        } else {    
                        stepperDataP->aCycSteps = ISR_IDLETIME;    // no more Interrupts for this stepper needed
                        stepperDataP->rampState = rampStat::STOPPED;
                        //CLR_TP2;
                        }
                    }
                }
                // --------------- compute nexte steplength ------------------------------------
                SET_TP2;
                // ramp state machine
                switch ( stepperDataP->rampState ) {
                  case  rampStat::RAMPACCEL:
                    // we are accelerating the motor
                    if (stepperDataP->stepsInRamp > stepperDataP->stepRampLen ) {
                        // we reached the end of the ramp
                        stepperDataP->aCycSteps = stepperDataP->tCycSteps;
                        stepperDataP->aCycRemain = stepperDataP->tCycRemain;
                        stepperDataP->stepsInRamp = stepperDataP->stepRampLen;
                        stepperDataP->rampState = rampStat::CRUISING;
                    } else {
                        stepperDataP->aCycSteps = stepperDataP->cyctXramplen / (stepperDataP->stepsInRamp + RAMPOFFSET) ;//+1;
                        stepperDataP->aCycRemain += stepperDataP->cyctXramplen % (stepperDataP->stepsInRamp + RAMPOFFSET);
                       if ( stepperDataP->aCycRemain > (stepperDataP->stepsInRamp + RAMPOFFSET) ) {
                            stepperDataP->aCycSteps++;
                            stepperDataP->aCycRemain -= (stepperDataP->stepsInRamp + RAMPOFFSET);
                        }
                    }
                    // do we have to start deceleration ( remaining steps < steps in ramp so far )
                    // Ramp must be same length in accelerating and decelerating!
                    if ( stepperDataP->stepCnt <= (long)( stepperDataP->stepsInRamp  ) ) {
                        //CLR_TP2;
                        stepperDataP->rampState = rampStat::RAMPDECEL;
                        //DB_PRINT( "scnt=%ld, sIR=%u\n\r", stepperDataP->stepCnt, stepperDataP->stepsInRamp );
                        //SET_TP2;
                    } else {
                        // still in ramp
                        stepperDataP->stepsInRamp ++;
                    }    
                    break;
                  case rampStat::RAMPDECEL:
                    // we are stopping the motor
                    if ( stepperDataP->stepCnt > (long)( stepperDataP->stepsInRamp ) ) {
                        //CLR_TP2; // ToDo: check whether this in necessary ( schould be done in method that changes steps to  move)
                        //steps to move has changed, accelerate again with next step
                        stepperDataP->rampState = rampStat::RAMPACCEL;
                        //DB_PRINT( "scnt=%ld, sIR=%u\n\r", stepperDataP->stepCnt, stepperDataP->stepsInRamp );
                        //SET_TP2;
                    }
                    stepperDataP->aCycSteps = stepperDataP->cyctXramplen / ( --stepperDataP->stepsInRamp + RAMPOFFSET ) ;// +1 ;
                    stepperDataP->aCycRemain += stepperDataP->cyctXramplen % (stepperDataP->stepsInRamp + RAMPOFFSET);
                    if ( stepperDataP->aCycRemain > (stepperDataP->stepsInRamp + RAMPOFFSET) ) {
                        stepperDataP->aCycSteps++;
                        stepperDataP->aCycRemain -= (stepperDataP->stepsInRamp + RAMPOFFSET);
                    }
                    break;
                
                  case rampStat::SPEEDDECEL:
                    // lower speed to new value 
                    stepperDataP->aCycSteps = stepperDataP->cyctXramplen / ( --stepperDataP->stepsInRamp + RAMPOFFSET ) ;//+1 ;
                    stepperDataP->aCycRemain += stepperDataP->cyctXramplen % (stepperDataP->stepsInRamp + RAMPOFFSET);
                    if ( stepperDataP->aCycRemain > (stepperDataP->stepsInRamp + RAMPOFFSET) ) {
                        stepperDataP->aCycSteps++;
                        stepperDataP->aCycRemain -= (stepperDataP->stepsInRamp + RAMPOFFSET);
                    }
                    //if ( stepperDataP->aCycSteps >= stepperDataP->tCycSteps2 || stepperDataP->stepsInRamp == 0 ) {
                    if (  stepperDataP->stepsInRamp <=  stepperDataP->stepRampLen ) {
                        // new targestspeed reached
                        //SET_TP3;
                        stepperDataP->rampState = rampStat::CRUISING;
                        stepperDataP->stepsInRamp =  stepperDataP->stepRampLen;
                        //CLR_TP3;
                    }
                    //ToDo - do we have to stop the motor
                    break;
                    
                  case rampStat::CRUISING:
                    // Not in ramp, targetspeed reached - or without ramp at all
                    //CLR_TP2;
                    stepperDataP->aCycSteps = stepperDataP->tCycSteps;
                    stepperDataP->aCycRemain += stepperDataP->tCycRemain;
                    if  ( stepperDataP->aCycRemain > CYCLETIME ) {
                        stepperDataP->aCycRemain -= CYCLETIME;
                        stepperDataP->aCycSteps++;
                    }
                    // do we have to start the deceleration
                    if ( stepperDataP->stepCnt <= stepperDataP->stepRampLen ) {
                        // in mode without ramp ( stepRampLen == 0 ) , this can never be true
                        stepperDataP->rampState = rampStat::RAMPDECEL;
                    }
                    
                    break;
                    
                  default:
                    //stepper does not move -> nothing to do
                    //CLR_TP2;
                    break;
                } // End of ramp-statemachine
                CLR_TP2;
            } // End of do one step
            nextCycle = min ( nextCycle, stepperDataP->aCycSteps-stepperDataP->cycCnt );
            //SET_TP1;
        } // end of 'if stepper active AND moving'
        else if ( stepperDataP->rampState == rampStat::STARTING ) {
            // we start with enablepin active ( cycCnt is already set to 0 )
            stepperDataP->aCycSteps = stepperDataP->cycDelay;
            if ( stepperDataP->stepRampLen > 0 ) stepperDataP->rampState = rampStat::RAMPACCEL;
            else                                stepperDataP->rampState = rampStat::CRUISING;
            nextCycle = min ( nextCycle, stepperDataP->aCycSteps );
        } else if ( stepperDataP->rampState == rampStat::STOPPING  ) {
            stepperDataP->cycCnt+=cyclesLastIRQ;
            if ( stepperDataP->cycCnt >= stepperDataP->aCycSteps ) {
                stepperDataP->cycCnt = 0;
                digitalWrite( stepperDataP->enablePin, !stepperDataP->enable );
                stepperDataP->rampState = rampStat::STOPPED;
            }
        }

        //CLR_TP1;
        stepperDataP = stepperDataP->nextStepperDataP;
        //SET_TP1; //CLR_TP3;
    } // end of stepper-loop
    
    // shift out spiData, if SPI is active
    //SET_TP2;
    if ( spiInitialized && spiChanged ) {
        digitalWrite( SS, LOW );
        #ifdef __AVR_MEGA__
        spiByteCount = 0;
        SPDR = spiData[1];
        #elif defined __STM32F1__
        #ifdef USE_SPI2
        digitalWrite(BOARD_SPI2_NSS_PIN,LOW);
        spi_tx_reg(SPI2, (spiData[1]<<8) + spiData[0] );
        #else
        digitalWrite(BOARD_SPI1_NSS_PIN,LOW);
        spi_tx_reg(SPI1, (spiData[1]<<8) + spiData[0] );
        #endif
        #endif
    }
    CLR_TP4;
} // ==================== End of stepper ISR ======================================
#pragma GCC optimize "Os"
// ---------- SPI interupt used for output stepper motor data -------------
extern "C" {
#ifdef __AVR_MEGA__
ISR ( SPI_STC_vect ) { 
    // output step-pattern on SPI, set SS when ready
    if ( spiByteCount++ == 0 ) {
        // end of shifting out high Byte, shift out low Byte
        SPDR = spiData[0];
    } else {
        // end of data shifting
        digitalWrite( SS, HIGH );
        spiByteCount = 0;
    }
}
#elif defined __STM32F1__
    #ifdef USE_SPI2
void __irq_spi2(void) {// STM32
    static int rxData;
    rxData = spi_rx_reg(SPI2);            // Get dummy data (Clear RXNE-Flag)
    digitalWrite(BOARD_SPI2_NSS_PIN,HIGH);
}
    #else
void __irq_spi1(void) {// STM32
    rxData = spi_rx_reg(SPI1);            // Get dummy data (Clear RXNE-Flag)
    digitalWrite(BOARD_SPI1_NSS_PIN,HIGH);
}
    #endif
#endif
} // end of extern "C"

static void initSPI() {
    // initialize SPI hardware.
    // MSB first, default Clk Level is 0, shift on leading edge
#ifdef __AVR_MEGA__
    uint8_t oldSREG = SREG;
    cli();
    pinMode( MOSI, OUTPUT );
    pinMode( SCK, OUTPUT );
    pinMode( SS, OUTPUT );
    SPCR = (1<<SPIE)    // Interrupt enable
         | (1<<SPE )    // SPI enable
         | (0<<DORD)    // MSB first
         | (1<<MSTR)    // Master Mode
         | (0<<CPOL)    // Clock is low when idle
         | (0<<CPHA)    // Data is sampled on leading edge
         | (0<<SPR1) | (1<<SPR0);    // fosc/16
    digitalWrite( SS, LOW );
    SREG = oldSREG;  // undo cli() 
    
#elif defined __STM32F1__
    #ifdef USE_SPI2// use SPI 2 interface
    spi_init(SPI2);
    spi_config_gpios(SPI2, 1,  // initialize as master
                     PIN_MAP[BOARD_SPI2_NSS_PIN].gpio_device, PIN_MAP[BOARD_SPI2_NSS_PIN].gpio_bit,        
                     PIN_MAP[BOARD_SPI2_SCK_PIN].gpio_device, PIN_MAP[BOARD_SPI2_SCK_PIN].gpio_bit,
                     PIN_MAP[BOARD_SPI2_MISO_PIN].gpio_bit,
                     PIN_MAP[BOARD_SPI2_MOSI_PIN].gpio_bit);

    uint32 flags = (SPI_FRAME_MSB | SPI_CR1_DFF_16_BIT | SPI_SW_SLAVE | SPI_SOFT_SS);
    spi_master_enable(SPI2, (spi_baud_rate)SPI_BAUD_PCLK_DIV_64, (spi_mode)SPI_MODE_0, flags);
    spi_irq_enable(SPI2, SPI_RXNE_INTERRUPT);
    pinMode( BOARD_SPI2_NSS_PIN, OUTPUT);
    digitalWrite( BOARD_SPI2_NSS_PIN, LOW );

    #else// use SPI 1 interface
    spi_init(SPI1);
    spi_config_gpios(SPI1, 1,  // initialize as master
                     PIN_MAP[BOARD_SPI1_NSS_PIN].gpio_device, PIN_MAP[BOARD_SPI1_NSS_PIN].gpio_bit,        
                     PIN_MAP[BOARD_SPI1_SCK_PIN].gpio_device, PIN_MAP[BOARD_SPI1_SCK_PIN].gpio_bit,
                     PIN_MAP[BOARD_SPI1_MISO_PIN].gpio_bit,
                     PIN_MAP[BOARD_SPI1_MOSI_PIN].gpio_bit);

    uint32 flags = (SPI_FRAME_MSB | SPI_CR1_DFF_16_BIT | SPI_SW_SLAVE | SPI_SOFT_SS);
    spi_master_enable(SPI1, (spi_baud_rate)SPI_BAUD_PCLK_DIV_64, (spi_mode)SPI_MODE_0, flags);
    spi_irq_enable(SPI1, SPI_RXNE_INTERRUPT);
    pinMode( BOARD_SPI1_NSS_PIN, OUTPUT);
    digitalWrite( BOARD_SPI1_NSS_PIN, LOW );
    #endif

#endif
    spiInitialized = true;  
}

uint16_t MoToStepper::setSpeedSteps( uint16_t speed10, int16_t rampLen ) {
    // Set speed and length of ramp to reach speed ( from stop )
    // neagtive ramplen means it was set automatically
    rampStat newRampState;      // State of acceleration/deceleration
    uint16_t tCycSteps;         // nbr of IRQ cycles per step ( new target value of motorspeed  )
    uint16_t tCycRemain;        // Remainder of division when computing tCycSteps
    long     tMicroSteps;       // Microseconds per step
    uint16_t newCyctXramplen;      // precompiled  tCycSteps*rampLen*RAMPOFFSET
    int16_t newRampLen;         // new ramplen
    int16_t newStepsInRamp;     // new stepcounter in ramp - according to new speed and ramplen
    uint16_t newSpeed10;        // new target speed

    if ( _stepperData.output == NO_OUTPUT ) return 0; // --------------->>>>>>>>>>>>>>>>not attached
    
    // compute new speed values, adjust length of ramp if necessary
    //actSpeed10 = oldSpeed10 = _stepSpeed10;
        
    newRampLen = abs(rampLen);    // negative values are invalid ( indicate automatically adjusted length )
    if (rampLen<0) newRampLen--;
    if (newRampLen > MAXRAMPLEN ) newRampLen = MAXRAMPLEN;
    newSpeed10 = min( 1000000L / MIN_STEPTIME * 10, speed10 );
	if ( newSpeed10 == 0 ) newSpeed10 = 1; // minimum speed
    
    //DB_PRINT( "rampLen-new=%u, ramplenParam=%u", newRampLen, rampLen );
    // compute target steplength and check whether speed and ramp fit together: 
    tMicroSteps = ( 1000000L * 10  / newSpeed10 );
    tCycSteps = tMicroSteps / CYCLETIME; 
    tCycRemain = tMicroSteps % CYCLETIME; 
    // tcyc * (rapmlen+RAMPOFFSET) must be less then 65000, otherwise ramplen is adjusted accordingly
    long tmp =  tMicroSteps * ( newRampLen + RAMPOFFSET ) / CYCLETIME ;
    if ( tmp > 65000L ) {
        // adjust ramplen
        newRampLen = 65000L * CYCLETIME / tMicroSteps - RAMPOFFSET;
        if( newRampLen < 0 ) newRampLen = 0;
        newCyctXramplen = tMicroSteps * ( newRampLen + RAMPOFFSET ) / CYCLETIME;
    } else {
        newCyctXramplen = tmp;
    }

    if (rampLen >= 0) {
        // ramplength was set by user, update reference-values
        _lastRampSpeed = newSpeed10;
        _lastRampLen   = newRampLen;
    }
    
    // recompute all relevant rampvalues according to actual speed and ramplength
    // This needs to be done only, if a ramp is defined, the stepper is moving
    // and the speed an ramp values changed
    // In all other cases the new speed/ramp values will get active immediately
    //DB_PRINT( "actRampLen=%u, cXr-new=%u, xCr-old=%u", newRampLen, newCyctXramplen, _stepperData.cyctXramplen );
    _noStepIRQ(); SET_TP2;
    if ( (_stepperData.stepRampLen + newRampLen ) != 0
        && _chkRunning() 
        &&  newCyctXramplen != _stepperData.cyctXramplen ) {
        // local variables to hold data that might change in IRQ:
        // If there was a step during recomputing the rampvalues, we must recompute again
        // recomputing the rampvalues lasts too long to stop the IRQ during the whole time
        long        __stepCnt;
        long        __newStepCnt;
        long        __newStepCnt2;
        
        //DB_PRINT("Speed changed! New: tcyc=%u, ramp=%u, cXr=%u",tCycSteps,newRampLen,newCyctXramplen );
        //Serial.print(_stepperData.rampState); Serial.print(" ( ");Serial.print( _stepperData.stepsInRamp );Serial.print("->");
         do {
            // read actual ISR values
            newRampState = _stepperData.rampState;
            __stepCnt       =  _stepperData.stepCnt; 
            __newStepCnt    = 0;    // if stepcnt is to be changed
            __newStepCnt2   = 0;
            _stepIRQ(); CLR_TP2;
            //with ramp and ramp or speed changed 
            // compute new 'steps in Ramp' according to new speed and ramp values. This maybe greater
            // than ramplen, if speed changed to slower
            newStepsInRamp = ( (long)newCyctXramplen * (_stepperData.stepsInRamp + RAMPOFFSET ) / _stepperData.cyctXramplen ) -RAMPOFFSET;
            if ( newStepsInRamp < 0 ) newStepsInRamp = 0; 
            //Serial.print( newStepsInRamp );
            
            if ( newSpeed10 != _stepSpeed10 ) {
                // speed changed!
                if ( newStepsInRamp > newRampLen ) {
                    //  ==========  we are too fast ============================
                        //Serial.print(" --");
                        //DB_PRINT ( "Slower: %u/%u -> %u/%u", _stepSpeed10,_stepperData.stepRampLen,  newSpeed10, newRampLen );
                        newRampState = rampStat::SPEEDDECEL;
                        //DB_PRINT("State->%s,  actStep=%u",rsC[_stepperData.rampState], _stepperData.stepsInRamp );
                    
                } else  {
                    //  ==========  we are too slow ============================
                    //Serial.print(" ++"); 
                    //DB_PRINT ( "Faster: %u/%u -> %u/%u", _stepSpeed10,_stepperData.stepRampLen, newSpeed10 , newRampLen );
                    newRampState = rampStat::RAMPACCEL;
                }
            } else {
                //Serial.print(" ==");
            }

            // Check whether we can reach targetposition with new values
            if ( newStepsInRamp > (__stepCnt - _stepperData.stepCnt2) ) {
                // we cannot reach the tagetposition, so we go beyond the targetposition and than back.
                // This works even if we are already beyond the target position
                //Serial.print( " ><");
                __newStepCnt2 = newStepsInRamp - (__stepCnt - _stepperData.stepCnt2);
                __newStepCnt = newStepsInRamp;
                newRampState = rampStat::RAMPDECEL;
            }
            _noStepIRQ(); SET_TP2;
            //Serial.print(" ) ");Serial.print(_stepperData.rampState);
        } while ( __stepCnt != _stepperData.stepCnt ); // if there was a step during computing, do it again
        _stepperData.rampState = newRampState;
        _stepperData.stepsInRamp = newStepsInRamp;
        if ( __newStepCnt != 0 ) {
            _stepperData.stepCnt = __newStepCnt;
            _stepperData.stepCnt2 = __newStepCnt2;
        }
    }
    
    _stepperData.tCycSteps = tCycSteps;
    _stepperData.tCycRemain = tCycRemain;
    _stepperData.cyctXramplen = newCyctXramplen;
    _stepperData.stepRampLen = newRampLen;
    _stepIRQ(); CLR_TP2;
    _stepSpeed10 = newSpeed10;
    
    DB_PRINT( "RampValues:, Spd=%u, rmpLen=%u, tcyc=%u, trest=%u, acyc=%u", _stepSpeed10, _stepperData.stepRampLen,
                   _stepperData.tCycSteps, _stepperData.tCycRemain, _stepperData.aCycSteps );
    DB_PRINT( "   - State=%s, Rampsteps=%u" , rsC[(int)_stepperData.rampState], _stepperData.stepsInRamp );
    return _stepperData.stepRampLen;
}