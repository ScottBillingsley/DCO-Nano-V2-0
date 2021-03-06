/*
                        DCO_Nano V2.0
                        Vernon Billingsley 2021

                        Create a ramp wave using the nano to 
                        provide a square wave for timing pulse and 
                        the dac to provide a current source for an 
                        intergrator circuit....

                        Schematic at 
                        https://github.com/ScottBillingsley/DCO-Nano-V2-0

                        MIDI In
                        Gate output on pin D2
                        Square Wave ouput on pin D8

                        DAC out on I2C 




    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission
    notice shall be included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.

*/

#include <MIDI.h>
#include <Wire.h>

/************************* Defines ********************************/
#define DEBUG 0

#if DEBUG == 1
#define dprint(expression) Serial.print("# "); Serial.print( #expression ); Serial.print( ": " ); Serial.println( expression )
#define dshow(expression) Serial.println( expression )
#else
#define dprint(expression)
#define dshow(expression)
#endif

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

//create an instance of MIDI receive on hardware serial
MIDI_CREATE_INSTANCE(HardwareSerial, Serial, myMidi);



/************************** Variables *****************************/
uint16_t counter = 0;
volatile boolean new_count = false;

uint8_t oct_bit;
volatile byte clk_arg = 0;
volatile boolean new_clk = false;

uint16_t dac_value = 0;
volatile boolean new_dac = false;

uint8_t key_ary[3];



/* (16,000,000 Hz divided by */
/* (C7, 2093.005 Hz to C8, 4186.009 Hz  */
/*  times 2^5)) - 1    */
volatile uint16_t clk[] = {
  238, 224, 212, 200, 189, 178, 168, 158, 149, 141, 133, 125, 118,
};



/**************************  Functions ****************************/
void handleNoteOn(byte inChannel, byte inNote, byte inVelocity)
{
  /*Scan the array for the fisrt empty column */
  for (byte a = 0; a < 4; a++) {
    if (key_ary[a] == 0) {
      /*Store the note */
      key_ary[a] = inNote;
      /*Break out of the for loop */
      a = 5;
    }
  }
  /*Octave range C1 to C7 */
  oct_bit = (inNote - 24) / 12;
  /*Clock setting for the note */
  clk_arg = inNote - (24 + (oct_bit * 12));
  /*Calculate DAC value */
  uint16_t base = (float)(17.5 * pow(2, oct_bit));
  dac_value = base + (float)(base / 11) * clk_arg;
  /*Set the new clock boolean */
  new_clk = true;
}

void handleNoteOff(byte inChannel, byte inNote, byte inVelocity)
{

  /*Scan the key array for the note off */
  for (byte a = 0; a < 4; a++) {
    if (inNote == key_ary[a]) {
      key_ary[a] = 0;
    }
  }
}

void dac_write(int cc) {

  Wire.beginTransmission(0x61);
  Wire.write(64);                     // cmd to update the DAC
  Wire.write(cc >> 4);        // the 8 most significant bits...
  Wire.write((cc & 15) << 4); // the 4 least significant bits...
  Wire.endTransmission();
  new_dac = false;
}

/******************************************************************/
/*************************** Setup ********************************/
/******************************************************************/
void setup() {
  if (DEBUG) {
    Serial.begin(115200);
  }
  /************************* Setup Pins ***************************/
  /*Gate out on pin D2 */
  DDRD |= _BV (2);

  /*Square wave out on pin D8 */
  DDRB |= _BV (0); ;

  //Setup the handlers for the  midi
  myMidi.setHandleNoteOn(handleNoteOn);
  myMidi.setHandleNoteOff(handleNoteOff);
  //Start the midi channel 10
  myMidi.begin(MIDI_CHANNEL_OMNI);

  /***********************  Start Wire ******************************/
  Wire.begin();
  Wire.setClock(400000);


  /*************************  Setup Timer1 ************************/
  cli();                //stop interrupts
  //set timer1
  TCCR1A = 0;// set entire TCCR1A register to 0
  TCCR1B = 0;// same for TCCR1B
  TCNT1  = 0;//initialize counter value to 0
  // preset compare match register to C1
  OCR1A = clk[0];
  // turn on CTC mode
  sbi(TCCR1B, WGM12);
  /*Set prescaler to 1 */
  cbi(TCCR1B, CS12);
  cbi(TCCR1B, CS11);
  sbi(TCCR1B, CS10);
  // enable timer compare interrupt
  sbi(TIMSK1, OCIE1A);
  sei();                //allow interrupts

  /*Preset the dac voltage */
  dac_write(17);

}/**************************  End Setup **************************/

ISR(TIMER1_COMPA_vect) {
  /*If there is a new note, set the clock to new count */
  if (new_clk) {
    OCR1A = clk[clk_arg];
    /*Clear the boolean */
    new_clk = false;
    /*Set the dac boolean */
    new_dac = true;
  }
  /*Increment the counter */
  counter ++;
  /*Set the boolean */
  new_count = true;

}

/******************************************************************/
/**************************** Loop ********************************/
/******************************************************************/
void loop() {
  /*Read the midi on ever loop */
  myMidi.read();

  /*Left adjust the counter for the current octave */
  uint8_t left_adj = 10 - oct_bit;

  /*If there is a new count, toggle the pins */
  if (new_count) {
    /*Toggle pin D8 */
    ((counter >> left_adj) & 0b0000000000000001) == 0 ? PORTB &= ~_BV (0) : PORTB |= _BV (0);
    /*Clear the boolaen */
    new_count = false;
  }

  /************** Scan the key array *************************/
  uint8_t notes_remain = 0;
  /*If there is a note in the array increment the count */
  for (uint8_t b = 0; b < 4; b++) {
    if (key_ary[b] != 0) {
      notes_remain ++;
    }
  }
  /*If the key array is empty, set the gate pin D2 LOW */
  if (notes_remain == 0) {
    PORTD &= ~_BV (2);
  }

  /*If there is a new note set the dac */
  if (new_dac) {
    /*Set the dac */
    dac_write(dac_value);
    /*Set the D2, gate pin HIGH */
    PORTD |= _BV (2);
  }

}/*************************** End Loop *****************************/
