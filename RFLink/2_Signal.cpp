// ************************************* //
// * Arduino Project RFLink-esp        * //
// * https://github.com/couin3/RFLink  * //
// * 2018..2020 Marc RIVES             * //
// * More details in RFLink.ino file   * //
// ************************************* //

#include <Arduino.h>
#include "2_Signal.h"
#include "5_Plugin.h"

RawSignalStruct RawSignal = {0, 0, 0, 0, 0UL};
byte PKSequenceNumber = 0;      // 1 byte packet counter
unsigned long SignalCRC = 0L;   // holds the bitstream value for some plugins to identify RF repeats
unsigned long SignalCRC_1 = 0L; // holds the previous SignalCRC (for mixed burst protocols)
byte SignalHash = 0L;           // holds the processed plugin number
byte SignalHashPrevious = 0L;   // holds the last processed plugin number
unsigned long RepeatingTimer = 0L;

/*********************************************************************************************/
boolean ScanEvent(void)
{ // Deze routine maakt deel uit van de hoofdloop en wordt iedere 125uSec. doorlopen
  unsigned long Timer = millis() + SCAN_HIGH_TIME_MS;

  while (Timer > millis() || RepeatingTimer > millis())
  {
    delay(1); // For Modem Sleep
    if (FetchSignal())
    { // RF: *** data start ***
      if (PluginRXCall(0, 0))
      { // Check all plugins to see which plugin can handle the received signal.
        RepeatingTimer = millis() + SIGNAL_REPEAT_TIME_MS;
        return true;
      }
    }
  } // while
  return false;
}

// ***********************************************************************************
boolean FetchSignal()
{
  // *********************************************************************************
  static bool Toggle;
  static unsigned long timeStartSeek_ms;
  static unsigned long timeStartLoop_us;
  static unsigned int RawCodeLength;
  static unsigned long PulseLength_us;
  static const bool Start_Level = LOW;
  // *********************************************************************************

#define RESET_SEEKSTART timeStartSeek_ms = millis();
#define RESET_TIMESTART timeStartLoop_us = micros();
#define CHECK_RF ((digitalRead(PIN_RF_RX_DATA) == Start_Level) ^ Toggle)
#define CHECK_TIMEOUT ((millis() - timeStartSeek_ms) < SIGNAL_SEEK_TIMEOUT_MS)
#define GET_PULSELENGTH PulseLength_us = micros() - timeStartLoop_us
#define SWITCH_TOGGLE Toggle = !Toggle
#define STORE_PULSE RawSignal.Pulses[RawCodeLength++] = PulseLength_us / RAWSIGNAL_SAMPLE_RATE

  // ***   Init Vars   ***
  Toggle = true;
  RawCodeLength = 0;
  PulseLength_us = 0;

  // ***********************************
  // ***   Scan for Preamble Pulse   ***
  // ***********************************
  RESET_SEEKSTART;

  while (PulseLength_us < SIGNAL_MIN_PREAMBLE_US)
  {
    while (CHECK_RF && CHECK_TIMEOUT)
      ;
    RESET_TIMESTART;
    SWITCH_TOGGLE;
    while (CHECK_RF && CHECK_TIMEOUT)
      ;
    GET_PULSELENGTH;
    SWITCH_TOGGLE;
    if (!CHECK_TIMEOUT)
      return false;
  }
  //Serial.print ("PulseLength: "); Serial.println (PulseLength);
  STORE_PULSE;

  // ************************
  // ***   Message Loop   ***
  // ************************
  while (RawCodeLength < RAW_BUFFER_SIZE)
  {

    // ***   Time Pulse   ***
    RESET_TIMESTART;
    while (CHECK_RF)
    {
      GET_PULSELENGTH;
      if (PulseLength_us > SIGNAL_END_TIMEOUT_US)
        break;
    }

    // ***   Too short Pulse Check   ***
    if (PulseLength_us < MIN_PULSE_LENGTH_US)
    {
      // NO RawCodeLength++;
      return false; // Or break; instead, if you think it may worth it.
    }

    // ***   Ending Pulse Check   ***
    if (PulseLength_us > SIGNAL_END_TIMEOUT_US) // Again, in main while this time
    {
      RawCodeLength++;
      break;
    }

    // ***   Prepare Next   ***
    SWITCH_TOGGLE;

    // ***   Store Pulse   ***
    STORE_PULSE;
  }
  //Serial.print ("RawCodeLength: ");
  //Serial.println (RawCodeLength);

  if (RawCodeLength >= MIN_RAW_PULSES)
  {
    RawSignal.Pulses[RawCodeLength] = 0;  // Last element contains the timeout.
    RawSignal.Number = RawCodeLength - 1; // Number of received pulse times (pulsen *2)
    RawSignal.Multiply = RAWSIGNAL_SAMPLE_RATE;
    RawSignal.Time = millis(); // Time the RF packet was received (to keep track of retransmits
    //Serial.print ("D");
    //Serial.print (RawCodeLength);
    return true;
  }
  else
  {
    RawSignal.Number = 0;
  }

  return false;
}

/*********************************************************************************************/
/*
  // RFLink Board specific: Generate a short pulse to switch the Aurel Transceiver from TX to RX mode.
  void RFLinkHW( void) {
     delayMicroseconds(36);
     digitalWrite(PIN_BSF_0,LOW);
     delayMicroseconds(16);
     digitalWrite(PIN_BSF_0,HIGH);
     return;
  }
*/
/*********************************************************************************************\
   Send rawsignal buffer to RF  * DEPRICATED * DO NOT USE
  \*********************************************************************************************/
/*
  void RawSendRF(void) {                                                    // * DEPRICATED * DO NOT USE *
  int x;
  digitalWrite(PIN_RF_RX_VCC,LOW);                                        // Spanning naar de RF ontvanger uit om interferentie met de zender te voorkomen.
  digitalWrite(PIN_RF_TX_VCC,HIGH);                                       // zet de 433Mhz zender aan
  delayMicroseconds(TRANSMITTER_STABLE_DELAY);                            // short delay to let the transmitter become stable (Note: Aurel RTX MID needs 500µS/0,5ms)

  RawSignal.Pulses[RawSignal.Number]=1;                                   // due to a bug in Arduino 1.0.1

  for(byte y=0; y<RawSignal.Repeats; y++) {                               // herhaal verzenden RF code
     x=1;
     noInterrupts();
     while(x<RawSignal.Number) {
        digitalWrite(PIN_RF_TX_DATA,HIGH);
        delayMicroseconds(RawSignal.Pulses[x++]*RawSignal.Multiply-5);    // min een kleine correctie
        digitalWrite(PIN_RF_TX_DATA,LOW);
        delayMicroseconds(RawSignal.Pulses[x++]*RawSignal.Multiply-7);    // min een kleine correctie
     }
     interrupts();
     if (y+1 < RawSignal.Repeats) delay(RawSignal.Delay);                 // Delay buiten het gebied waar de interrupts zijn uitgeschakeld! Anders werkt deze funktie niet.
  }

  delayMicroseconds(TRANSMITTER_STABLE_DELAY);                            // short delay to let the transmitter become stable (Note: Aurel RTX MID needs 500µS/0,5ms)
  digitalWrite(PIN_RF_TX_VCC,LOW);                                        // zet de 433Mhz zender weer uit
  digitalWrite(PIN_RF_RX_VCC,HIGH);                                       // Spanning naar de RF ontvanger weer aan.
  // RFLinkHW();
  }
*/
/*********************************************************************************************/
