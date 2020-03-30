/*
  TSynth Firmware Rev 1.0

  Arduino IDE
  Tools Settings:
  Board: "Teensy3.6"
  USB Type: "Serial + MIDI + Audio"
  CPU Speed: "180MHz"
*/
//#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <MIDI.h>
#include <USBHost_t36.h>
#include "MidiCC.h"
#include "AudioPatching.h"
#include "Constants.h"
#include "Parameters.h"
#include "PatchMgr.h"
#include "HWControls.h"
#include "EepromMgr.h"
#include "MenuOptions.h"

#define PARAMETER 0 //The main page for displaying the current patch and control (parameter) changes
#define RECALL 1 //Patches list
#define SAVE 2 //Save patch page
#define REINITIALISE 3 // Reinitialise message
#define PATCH 4 // Show current patch bypassing PARAMETER
#define PATCHNAMING 5 // Patch naming page
#define DELETE 6 //Delete patch page
#define MENU 7 //Menu for settings page
#define MENUVALUE 8 //Menu values for settings page

volatile unsigned int state = PARAMETER;

#define PULSE 1
#define VAR_TRI 2
#define FILTER_ENV 3
#define AMP_ENV 4
#define WAVEFORM_PARABOLIC 100
#define WAVEFORM_HARMONIC 101

#include "ST7735Display.h"

boolean cardStatus = false;

//USB HOST MIDI Class Compliant
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
MIDIDevice midi1(myusb);

//MIDI 5 Pin DIN
MIDI_CREATE_INSTANCE(HardwareSerial, Serial4, MIDI); //RX - Pin 31

struct VoiceAndNote
{
  int note;
  long timeOn;
};

struct VoiceAndNote voices[NO_OF_VOICES] = {
  { -1, 0},
  { -1, 0},
  { -1, 0},
  { -1, 0},
  { -1, 0},
  { -1, 0}
};

int prevNote = 48;               //Initialised to middle value
float previousMillis = millis(); //For MIDI Clk Sync

int count = 0;
int patchNo = 1;
int voiceToReturn = -1;       //Initialise to 'null'
long earliestTime = millis(); //For voice allocation - initialise to now

void setup()
{
  setupDisplay();
  setUpMenus();
  setupHardware();

  AudioMemory(48);
  sgtl5000_1.enable();
  sgtl5000_1.volume(SGTL_MAXVOLUME * 0.5); //Headphones - do not initialise to maximum
  sgtl5000_1.lineOutLevel(29);             //Line out

  cardStatus = SD.begin(BUILTIN_SDCARD);
  if (cardStatus)
  {
    Serial.println("SD card is connected");
    //Get patch numbers and names from SD card
    getPatches(SD.open("/"));
    if (patches.size() == 0)
    {
      //save an initialised patch to SD card
      savePatch("1", INITPATCH);
      getPatches(SD.open("/"));
    } else {
      // sortPatches();
    }
  }
  else
  {
    Serial.println("SD card is not connected or unusable");
    reinitialiseToPanel();
    showPatchPage("No SD", "conn'd / usable");
  }

  //Read MIDI Channel from EEPROM
  midiChannel = getMIDIChannel();
  Serial.println("MIDI Ch:" + String(midiChannel) + " (0 is Omni On)");

  //USB HOST MIDI Class Compliant
  delay(200); //Wait to turn on USB Host
  myusb.begin();
  midi1.setHandleControlChange(myControlChange);
  midi1.setHandleNoteOff(myNoteOff);
  midi1.setHandleNoteOn(myNoteOn);
  midi1.setHandlePitchChange(myPitchBend);
  midi1.setHandleProgramChange(myProgramChange);
  midi1.setHandleClock(myMIDIClock);
  midi1.setHandleStart(myMIDIClockStart);
  Serial.println("USB HOST MIDI Class Compliant Listening");

  //USB Client MIDI
  usbMIDI.setHandleControlChange(myControlChange);
  usbMIDI.setHandleNoteOff(myNoteOff);
  usbMIDI.setHandleNoteOn(myNoteOn);
  usbMIDI.setHandlePitchChange(myPitchBend);
  usbMIDI.setHandleProgramChange(myProgramChange);
  usbMIDI.setHandleClock(myMIDIClock);
  usbMIDI.setHandleStart(myMIDIClockStart);
  Serial.println("USB Client MIDI Listening");

  //MIDI 5 Pin DIN
  MIDI.begin();
  MIDI.setHandleNoteOn(myNoteOn);
  MIDI.setHandleNoteOff(myNoteOff);
  MIDI.setHandlePitchBend(myPitchBend);
  MIDI.setHandleControlChange(myControlChange);
  MIDI.setHandleProgramChange(myProgramChange);
  //Doesn't like continuous Midi Clock signals from DAW, works with USB Midi fine
  MIDI.setHandleClock(myMIDIClock);
  MIDI.setHandleStart(myMIDIClockStart);
  Serial.println("MIDI In DIN Listening");

  constant1Dc.amplitude(CONSTANTONE);

  noiseMixer.gain(0, CONSTANTONE);
  noiseMixer.gain(1, CONSTANTONE);
  noiseMixer.gain(2, 0);
  noiseMixer.gain(3, 0);

  voiceMixer1.gain(0, VOICEMIXERLEVEL);
  voiceMixer1.gain(1, VOICEMIXERLEVEL);
  voiceMixer1.gain(2, VOICEMIXERLEVEL);
  voiceMixer1.gain(3, 0);

  voiceMixer2.gain(0, VOICEMIXERLEVEL);
  voiceMixer2.gain(1, VOICEMIXERLEVEL);
  voiceMixer2.gain(2, VOICEMIXERLEVEL);
  voiceMixer2.gain(3, 0);

  voiceMixerM.gain(0, 0.5);
  voiceMixerM.gain(1, 0.5);
  voiceMixerM.gain(2, 0);
  voiceMixerM.gain(3, 0);

  pwmLfo.amplitude(CONSTANTONE);
  pwmLfo.begin(PWMWAVEFORM);

  waveformMod1a.frequency(440);
  waveformMod1a.amplitude(CONSTANTONE);
  waveformMod1a.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod1a.begin(vcoWaveformA);

  waveformMod1b.frequency(440);
  waveformMod1b.amplitude(CONSTANTONE);
  waveformMod1b.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod1b.begin(vcoWaveformB);

  waveformMod2a.frequency(440);
  waveformMod2a.amplitude(CONSTANTONE);
  waveformMod2a.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod2a.begin(vcoWaveformA);

  waveformMod2b.frequency(440);
  waveformMod2b.amplitude(CONSTANTONE);
  waveformMod2b.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod2b.begin(vcoWaveformB);

  waveformMod3a.frequency(440);
  waveformMod3a.amplitude(CONSTANTONE);
  waveformMod3a.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod3a.begin(vcoWaveformA);

  waveformMod3b.frequency(440);
  waveformMod3b.amplitude(CONSTANTONE);
  waveformMod3b.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod3b.begin(vcoWaveformB);

  waveformMod4a.frequency(440);
  waveformMod4a.amplitude(CONSTANTONE);
  waveformMod4a.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod4a.begin(vcoWaveformA);

  waveformMod4b.frequency(440);
  waveformMod4b.amplitude(CONSTANTONE);
  waveformMod4b.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod4b.begin(vcoWaveformB);

  waveformMod5a.frequency(440);
  waveformMod5a.amplitude(CONSTANTONE);
  waveformMod5a.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod5a.begin(vcoWaveformA);

  waveformMod5b.frequency(440);
  waveformMod5b.amplitude(CONSTANTONE);
  waveformMod5b.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod5b.begin(vcoWaveformB);

  waveformMod6a.frequency(440);
  waveformMod6a.amplitude(CONSTANTONE);
  waveformMod6a.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod6a.begin(vcoWaveformA);

  waveformMod6b.frequency(440);
  waveformMod6b.amplitude(CONSTANTONE);
  waveformMod6b.frequencyModulation(VCOLFOOCTAVERANGE);
  waveformMod6b.begin(vcoWaveformB);

  //All oscillators get parabolic arbitary waveform initially
  loadWaveform(PARABOLIC_WAVE);

  filter1.octaveControl(FILTEROCTAVERANGE);
  filter2.octaveControl(FILTEROCTAVERANGE);
  filter3.octaveControl(FILTEROCTAVERANGE);
  filter4.octaveControl(FILTEROCTAVERANGE);
  filter5.octaveControl(FILTEROCTAVERANGE);
  filter6.octaveControl(FILTEROCTAVERANGE);

  waveformMixer1.gain(2, CONSTANTONE); //Noise
  waveformMixer1.gain(3, 0);           //Osc FX
  waveformMixer2.gain(2, CONSTANTONE); //Noise
  waveformMixer2.gain(3, 0);           //Osc FX
  waveformMixer3.gain(2, CONSTANTONE); //Noise
  waveformMixer3.gain(3, 0);           //Osc FX
  waveformMixer4.gain(2, CONSTANTONE); //Noise
  waveformMixer4.gain(3, 0);           //Osc FX
  waveformMixer5.gain(2, CONSTANTONE); //Noise
  waveformMixer5.gain(3, 0);           //Osc FX
  waveformMixer6.gain(2, CONSTANTONE); //Noise
  waveformMixer6.gain(3, 0);           //Osc FX

  vcfModMixer1.gain(1, CONSTANTONE); //LFO
  vcfModMixer1.gain(3, 0);           //Not used
  vcfModMixer2.gain(1, CONSTANTONE); //LFO
  vcfModMixer2.gain(3, 0);           //Not used
  vcfModMixer3.gain(1, CONSTANTONE); //LFO
  vcfModMixer3.gain(3, 0);           //Not used
  vcfModMixer4.gain(1, CONSTANTONE); //LFO
  vcfModMixer4.gain(3, 0);           //Not used
  vcfModMixer5.gain(1, CONSTANTONE); //LFO
  vcfModMixer5.gain(3, 0);           //Not used
  vcfModMixer6.gain(1, CONSTANTONE); //LFO
  vcfModMixer6.gain(3, 0);           //Not used

  filterMixer1.gain(3, 0); //Not used
  filterMixer2.gain(3, 0); //Not used
  filterMixer3.gain(3, 0); //Not used
  filterMixer4.gain(3, 0); //Not used
  filterMixer5.gain(3, 0); //Not used
  filterMixer6.gain(3, 0); //Not used

  //This removes dc offset (mostly from unison pulse waves) before the ensemble effect
  dcOffsetFilter.octaveControl(1);
  dcOffsetFilter.frequency(20);
  dcOffsetFilter.resonance(0.707);

  ensemble.lfoRate(fxAmt);

  effectMixerL.gain(2, 0);
  effectMixerL.gain(3, 0);
  effectMixerR.gain(2, 0);
  effectMixerR.gain(3, 0);

  volumePrevious = -9999; //Force volume control to be read and set to current

  //Read Key Tracking from EEPROM, this can be set individually by each patch.
  keytrackingAmount = getKeyTracking();
  
  //Read Pitch Bend Range from EEPROM
  pitchBendRange = getPitchBendRange();

    //Read Mod Wheel Depth from EEPROM
  modWheelDepth = getModWheelDepth();

  recallPatch(String(patchNo)); //Load first patch
}

void myNoteOn(byte channel, byte note, byte velocity)
{
  //Check for out of range notes
  if (note + vcoOctaveA < 0 || note + vcoOctaveA > 127 || note + vcoOctaveB < 0 || note + vcoOctaveB > 127)
    return;

  if (glideSpeed > 0)
  {
    glide.amplitude((prevNote - note) * DIV12);   //Set glide to previous note frequency (limited to 1 octave max)
    glide.amplitude(0, glideSpeed * GLIDEFACTOR); //Glide to current note
  }

  if (vcoLfoRetrig == 1)
  {
    vcoLfo.sync();
  }
  if (vcfLfoRetrig == 1)
  {
    vcfLfo.sync();
  }

  prevNote = note;
  if (unison == 0)
  {
    switch (getVoiceNo(-1))
    {
      case 1:
        keytracking1.amplitude(note * DIV127 * keytrackingAmount);
        voices[0].note = note;
        voices[0].timeOn = millis();
        updateVoice1();
        vcfEnvelope1.noteOn();
        vcaEnvelope1.noteOn();
        voiceOn[0] = true;
        break;
      case 2:
        keytracking2.amplitude(note * DIV127 * keytrackingAmount);
        voices[1].note = note;
        voices[1].timeOn = millis();
        updateVoice2();
        vcfEnvelope2.noteOn();
        vcaEnvelope2.noteOn();
        voiceOn[1] = true;
        break;
      case 3:
        keytracking3.amplitude(note * DIV127 * keytrackingAmount);
        voices[2].note = note;
        voices[2].timeOn = millis();
        updateVoice3();
        vcfEnvelope3.noteOn();
        vcaEnvelope3.noteOn();
        voiceOn[2] = true;
        break;
      case 4:
        keytracking4.amplitude(note * DIV127 * keytrackingAmount);
        voices[3].note = note;
        voices[3].timeOn = millis();
        updateVoice4();
        vcfEnvelope4.noteOn();
        vcaEnvelope4.noteOn();
        voiceOn[3] = true;
        break;
      case 5:
        keytracking5.amplitude(note * DIV127 * keytrackingAmount);
        voices[4].note = note;
        voices[4].timeOn = millis();
        updateVoice5();
        vcfEnvelope5.noteOn();
        vcaEnvelope5.noteOn();
        voiceOn[4] = true;
        break;
      case 6:
        keytracking6.amplitude(note * DIV127 * keytrackingAmount);
        voices[5].note = note;
        voices[5].timeOn = millis();
        updateVoice6();
        vcfEnvelope6.noteOn();
        vcaEnvelope6.noteOn();
        voiceOn[5] = true;
        break;
    }
  }
  else
  {
    //Produces interesting phasing effect on note on - Could be future Osc FX option
    //    waveformMod1a.sync();
    //    waveformMod1b.sync();
    //    waveformMod2a.sync();
    //    waveformMod2b.sync();
    //    waveformMod3a.sync();
    //    waveformMod3b.sync();
    //    waveformMod4a.sync();
    //    waveformMod4b.sync();
    //    waveformMod5a.sync();
    //    waveformMod5b.sync();
    //    waveformMod6a.sync();
    //    waveformMod6b.sync();

    //UNISON MODE
    keytracking1.amplitude(note * DIV127 * keytrackingAmount);
    keytracking2.amplitude(note * DIV127 * keytrackingAmount);
    keytracking3.amplitude(note * DIV127 * keytrackingAmount);
    keytracking4.amplitude(note * DIV127 * keytrackingAmount);
    keytracking5.amplitude(note * DIV127 * keytrackingAmount);
    keytracking6.amplitude(note * DIV127 * keytrackingAmount);
    voices[0].note = note;
    voices[0].timeOn = millis();
    updateVoice1();
    voices[1].note = note;
    voices[1].timeOn = millis();
    updateVoice2();
    voices[2].note = note;
    voices[2].timeOn = millis();
    updateVoice3();
    voices[3].note = note;
    voices[3].timeOn = millis();
    updateVoice4();
    voices[4].note = note;
    voices[4].timeOn = millis();
    updateVoice5();
    voices[5].note = note;
    voices[5].timeOn = millis();
    updateVoice6();

    vcfEnvelope1.noteOn();
    vcfEnvelope2.noteOn();
    vcfEnvelope3.noteOn();
    vcfEnvelope4.noteOn();
    vcfEnvelope5.noteOn();
    vcfEnvelope6.noteOn();

    vcaEnvelope1.noteOn();
    vcaEnvelope2.noteOn();
    vcaEnvelope3.noteOn();
    vcaEnvelope4.noteOn();
    vcaEnvelope5.noteOn();
    vcaEnvelope6.noteOn();

    voiceOn[0] = true;
    voiceOn[1] = true;
    voiceOn[2] = true;
    voiceOn[3] = true;
    voiceOn[4] = true;
    voiceOn[5] = true;
  }
}

void myNoteOff(byte channel, byte note, byte velocity)
{
  if (unison == 0)
  {
    switch (getVoiceNo(note))
    {
      case 1:
        vcfEnvelope1.noteOff();
        vcaEnvelope1.noteOff();
        voices[0].note = -1;
        voiceOn[0] = false;
        break;
      case 2:
        vcfEnvelope2.noteOff();
        vcaEnvelope2.noteOff();
        voices[1].note = -1;
        voiceOn[1] = false;
        break;
      case 3:
        vcfEnvelope3.noteOff();
        vcaEnvelope3.noteOff();
        voices[2].note = -1;
        voiceOn[2] = false;
        break;
      case 4:
        vcfEnvelope4.noteOff();
        vcaEnvelope4.noteOff();
        voices[3].note = -1;
        voiceOn[3] = false;
        break;
      case 5:
        vcfEnvelope5.noteOff();
        vcaEnvelope5.noteOff();
        voices[4].note = -1;
        voiceOn[4] = false;
        break;
      case 6:
        vcfEnvelope6.noteOff();
        vcaEnvelope6.noteOff();
        voices[5].note = -1;
        voiceOn[5] = false;
        break;
    }
  }
  else
  {
    //UNISON MODE
    allNotesOff();
  }
}

void allNotesOff()
{
  vcfEnvelope1.noteOff();
  vcaEnvelope1.noteOff();
  vcfEnvelope2.noteOff();
  vcaEnvelope2.noteOff();
  vcfEnvelope3.noteOff();
  vcaEnvelope3.noteOff();
  vcfEnvelope4.noteOff();
  vcaEnvelope4.noteOff();
  vcfEnvelope5.noteOff();
  vcaEnvelope5.noteOff();
  vcfEnvelope6.noteOff();
  vcaEnvelope6.noteOff();

  voices[0].note = -1;
  voices[1].note = -1;
  voices[2].note = -1;
  voices[3].note = -1;
  voices[4].note = -1;
  voices[5].note = -1;

  voiceOn[0] = false;
  voiceOn[1] = false;
  voiceOn[2] = false;
  voiceOn[3] = false;
  voiceOn[4] = false;
  voiceOn[5] = false;
}

int getVoiceNo(int note)
{
  voiceToReturn = -1;      //Initialise to 'null'
  earliestTime = millis(); //Initialise to now
  if (note == -1)
  {
    //NoteOn() - Get the oldest free voice (recent voices may be still on release stage)
    for (int i = 0; i < NO_OF_VOICES; i++)
    {
      if (voices[i].note == -1)
      {
        if (voices[i].timeOn < earliestTime)
        {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    if (voiceToReturn == -1)
    {
      //No free voices, need to steal oldest sounding voice
      earliestTime = millis(); //Reinitialise
      for (int i = 0; i < NO_OF_VOICES; i++)
      {
        if (voices[i].timeOn < earliestTime)
        {
          earliestTime = voices[i].timeOn;
          voiceToReturn = i;
        }
      }
    }
    return voiceToReturn + 1;
  }
  else
  {
    //NoteOff() - Get voice number from note
    for (int i = 0; i < NO_OF_VOICES; i++)
    {
      if (voices[i].note == note)
      {
        return i + 1;
      }
    }
  }
  //Shouldn't get here, return voice 1
  return 1;
}

void updateVoice1()
{
  waveformMod1a.frequency(NOTEFREQS[voices[0].note + vcoOctaveA]);
  if (unison == 1)
  {
    waveformMod1b.frequency(NOTEFREQS[voices[0].note + vcoOctaveB] * (detune + ((1 - detune) * 0.09)));
  }
  else
  {
    waveformMod1b.frequency(NOTEFREQS[voices[0].note + vcoOctaveB] * detune);
  }
}

void updateVoice2()
{
  if (unison == 1)
  {
    waveformMod2a.frequency(NOTEFREQS[voices[1].note + vcoOctaveA] * (detune + ((1 - detune) * 0.18)));
    waveformMod2b.frequency(NOTEFREQS[voices[1].note + vcoOctaveB] * (detune + ((1 - detune) * 0.27)));
  }
  else
  {
    waveformMod2a.frequency(NOTEFREQS[voices[1].note + vcoOctaveA]);
    waveformMod2b.frequency(NOTEFREQS[voices[1].note + vcoOctaveB] * detune);
  }
}

void updateVoice3()
{
  if (unison == 1)
  {
    waveformMod3a.frequency(NOTEFREQS[voices[2].note + vcoOctaveA] * (detune + ((1 - detune) * 0.36)));
    waveformMod3b.frequency(NOTEFREQS[voices[2].note + vcoOctaveB] * (detune + ((1 - detune) * 0.46)));
  }
  else
  {
    waveformMod3a.frequency(NOTEFREQS[voices[2].note + vcoOctaveA]);
    waveformMod3b.frequency(NOTEFREQS[voices[2].note + vcoOctaveB] * detune);
  }
}
void updateVoice4()
{
  if (unison == 1)
  {
    waveformMod4a.frequency(NOTEFREQS[voices[3].note + vcoOctaveA] * (detune + ((1 - detune) * 0.55)));
    waveformMod4b.frequency(NOTEFREQS[voices[3].note + vcoOctaveB] * (detune + ((1 - detune) * 0.64)));
  }
  else
  {
    waveformMod4a.frequency(NOTEFREQS[voices[3].note + vcoOctaveA]);
    waveformMod4b.frequency(NOTEFREQS[voices[3].note + vcoOctaveB] * detune);
  }
}

void updateVoice5()
{
  if (unison == 1)
  {
    waveformMod5a.frequency(NOTEFREQS[voices[4].note + vcoOctaveA] * (detune + ((1 - detune) * 0.73)));
    waveformMod5b.frequency(NOTEFREQS[voices[4].note + vcoOctaveB] * (detune + ((1 - detune) * 0.82)));
  }
  else
  {
    waveformMod5a.frequency(NOTEFREQS[voices[4].note + vcoOctaveA]);
    waveformMod5b.frequency(NOTEFREQS[voices[4].note + vcoOctaveB] * detune);
  }
}

void updateVoice6()
{
  if (unison == 1)
  {
    waveformMod6a.frequency(NOTEFREQS[voices[5].note + vcoOctaveA] * (detune + ((1 - detune) * 0.90)));
  }
  else
  {
    waveformMod6a.frequency(NOTEFREQS[voices[5].note + vcoOctaveA]);
  }
  waveformMod6b.frequency(NOTEFREQS[voices[5].note + vcoOctaveB] * detune);
}

int getLFOWaveform(int value)
{
  if (value >= 0 && value < 8)
  {
    return WAVEFORM_SINE;
  }
  else if (value >= 8 && value < 30)
  {
    return WAVEFORM_TRIANGLE;
  }
  else if (value >= 30 && value < 63)
  {
    return WAVEFORM_SAWTOOTH_REVERSE;
  }
  else if (value >= 63 && value < 92)
  {
    return WAVEFORM_SAWTOOTH;
  }
  else if (value >= 92 && value < 111)
  {
    return WAVEFORM_SQUARE;
  }
  else
  {
    return WAVEFORM_SAMPLE_HOLD;
  }
}

String getWaveformStr(int value)
{
  switch (value)
  {
    case WAVEFORM_SILENT:
      return "Off";
    case WAVEFORM_SAMPLE_HOLD:
      return "Sample & Hold";
    case WAVEFORM_SINE:
      return "Sine";
    case WAVEFORM_SQUARE:
      return "Square";
    case WAVEFORM_TRIANGLE:
      return "Triangle";
    case WAVEFORM_SAWTOOTH:
      return "Sawtooth";
    case WAVEFORM_SAWTOOTH_REVERSE:
      return "Ramp";
    case WAVEFORM_PULSE:
      return "Var. Pulse";
    case WAVEFORM_TRIANGLE_VARIABLE:
      return "Var. Triangle";
    case WAVEFORM_PARABOLIC:
      return "Parabolic";
    case WAVEFORM_HARMONIC:
      return "Harmonic";
    default:
      return "ERR_WAVE";
  }
}

void loadWaveform(const int16_t * wavedata) {
  waveformMod1a.arbitraryWaveform(wavedata, 172.0);
  waveformMod1b.arbitraryWaveform(wavedata, 172.0);
  waveformMod2a.arbitraryWaveform(wavedata, 172.0);
  waveformMod2b.arbitraryWaveform(wavedata, 172.0);
  waveformMod3a.arbitraryWaveform(wavedata, 172.0);
  waveformMod3b.arbitraryWaveform(wavedata, 172.0);
  waveformMod4a.arbitraryWaveform(wavedata, 172.0);
  waveformMod4b.arbitraryWaveform(wavedata, 172.0);
  waveformMod5a.arbitraryWaveform(wavedata, 172.0);
  waveformMod5b.arbitraryWaveform(wavedata, 172.0);
  waveformMod6a.arbitraryWaveform(wavedata, 172.0);
  waveformMod6b.arbitraryWaveform(wavedata, 172.0);
}

float getLFOTempoRate(int value)
{
  lfoTempoValue = LFOTEMPO[value];
  return lfoSyncFreq * LFOTEMPO[value];
}

int getVCOWaveformA(int value)
{
  if (value >= 0 && value < 7)
  {
    //This will turn the osc off
    return WAVEFORM_SILENT;
  }
  else if (value >= 7 && value < 23)
  {
    return WAVEFORM_TRIANGLE;
  }
  else if (value >= 23 && value < 40)
  {
    return WAVEFORM_SQUARE;
  }
  else if (value >= 40 && value < 60)
  {
    return WAVEFORM_SAWTOOTH;
  }
  else if (value >= 60 && value < 80)
  {
    return WAVEFORM_PULSE;
  }
  else if (value >= 80 && value < 100)
  {
    return WAVEFORM_TRIANGLE_VARIABLE;
  }
  else if (value >= 100 && value < 120)
  {
    loadWaveform(PARABOLIC_WAVE);
    return WAVEFORM_PARABOLIC;
  }
  else
  {
    loadWaveform(HARMONIC_WAVE);
    return WAVEFORM_HARMONIC;
  }
}

int getVCOWaveformB(int value)
{
  if (value >= 0 && value < 7)
  {
    //This will turn the osc off
    return WAVEFORM_SILENT;
  }
  else if (value >= 7 && value < 23)
  {
    return WAVEFORM_SAMPLE_HOLD;
  }
  else if (value >= 23 && value < 40)
  {
    return WAVEFORM_SQUARE;
  }
  else if (value >= 40 && value < 60)
  {
    return WAVEFORM_SAWTOOTH;
  }
  else if (value >= 60 && value < 80)
  {
    return WAVEFORM_PULSE;
  }
  else if (value >= 80 && value < 100)
  {
    return WAVEFORM_TRIANGLE_VARIABLE;
  }
  else if (value >= 100 && value < 120)
  {
    loadWaveform(PARABOLIC_WAVE);
    return WAVEFORM_PARABOLIC;
  }
  else
  {
    loadWaveform(HARMONIC_WAVE);
    return WAVEFORM_HARMONIC;
  }
}

int getVCOOctave(int value)
{
  return OCTAVECONTROL[value];
}

void setPwmMixerALFO(float value)
{
  pwMixer1a.gain(0, value);
  pwMixer2a.gain(0, value);
  pwMixer3a.gain(0, value);
  pwMixer4a.gain(0, value);
  pwMixer5a.gain(0, value);
  pwMixer6a.gain(0, value);
  showCurrentParameterPage("1. PWM LFO", String(value));
}

void setPwmMixerBLFO(float value)
{
  pwMixer1b.gain(0, value);
  pwMixer2b.gain(0, value);
  pwMixer3b.gain(0, value);
  pwMixer4b.gain(0, value);
  pwMixer5b.gain(0, value);
  pwMixer6b.gain(0, value);
  showCurrentParameterPage("2. PWM LFO", String(value));
}

void setPwmMixerAPW(float value)
{
  pwMixer1a.gain(1, value);
  pwMixer2a.gain(1, value);
  pwMixer3a.gain(1, value);
  pwMixer4a.gain(1, value);
  pwMixer5a.gain(1, value);
  pwMixer6a.gain(1, value);
}

void setPwmMixerBPW(float value)
{
  pwMixer1b.gain(1, value);
  pwMixer2b.gain(1, value);
  pwMixer3b.gain(1, value);
  pwMixer4b.gain(1, value);
  pwMixer5b.gain(1, value);
  pwMixer6b.gain(1, value);
}

void setPwmMixerAFEnv(float value)
{
  pwMixer1a.gain(2, value);
  pwMixer2a.gain(2, value);
  pwMixer3a.gain(2, value);
  pwMixer4a.gain(2, value);
  pwMixer5a.gain(2, value);
  pwMixer6a.gain(2, value);
  showCurrentParameterPage("1. PWM F Env", String(value));
}

void setPwmMixerBFEnv(float value)
{
  pwMixer1b.gain(2, value);
  pwMixer2b.gain(2, value);
  pwMixer3b.gain(2, value);
  pwMixer4b.gain(2, value);
  pwMixer5b.gain(2, value);
  pwMixer6b.gain(2, value);
  showCurrentParameterPage("2. PWM F Env", String(value));
}

void updateUnison()
{
  if (unison == 0)
  {
    allNotesOff();//Avoid hanging notes
    showCurrentParameterPage("Unison", "Off");
    digitalWrite(UNISON_LED, LOW);  // LED off
  }
  else
  {
    showCurrentParameterPage("Unison", "On");
    digitalWrite(UNISON_LED, HIGH);  // LED on
  }
}

void updateVolume(float vol)
{
  showCurrentParameterPage("Volume", vol);
}

void updateGlide()
{
  showCurrentParameterPage("Glide", String(int(glideSpeed * GLIDEFACTOR)) + " ms");
}

void updateWaveformA()
{
  int newWaveform = vcoWaveformA;//To allow Arbitrary waveforms
  if (vcoWaveformA == WAVEFORM_PARABOLIC) {
    loadWaveform(PARABOLIC_WAVE);
    newWaveform = WAVEFORM_ARBITRARY;
  }
  if (vcoWaveformA == WAVEFORM_HARMONIC) {
    loadWaveform(HARMONIC_WAVE);
    newWaveform = WAVEFORM_ARBITRARY;
  }
  waveformMod1a.begin(newWaveform);
  waveformMod2a.begin(newWaveform);
  waveformMod3a.begin(newWaveform);
  waveformMod4a.begin(newWaveform);
  waveformMod5a.begin(newWaveform);
  waveformMod6a.begin(newWaveform);
  showCurrentParameterPage("1. Waveform", getWaveformStr(vcoWaveformA));
}

void updateWaveformB()
{
  int newWaveform = vcoWaveformB;//To allow Arbitrary waveforms
  if (vcoWaveformB == WAVEFORM_PARABOLIC) {
    loadWaveform(PARABOLIC_WAVE);
    newWaveform = WAVEFORM_ARBITRARY;
  }
  if (vcoWaveformB == WAVEFORM_HARMONIC) {
    loadWaveform(HARMONIC_WAVE);
    newWaveform = WAVEFORM_ARBITRARY;
  }
  waveformMod1b.begin(newWaveform);
  waveformMod2b.begin(newWaveform);
  waveformMod3b.begin(newWaveform);
  waveformMod4b.begin(newWaveform);
  waveformMod5b.begin(newWaveform);
  waveformMod6b.begin(newWaveform);
  showCurrentParameterPage("2. Waveform", getWaveformStr(vcoWaveformB));
}

void updateOctaveA()
{
  //update waveforms with new frequencies if notes are on
  if (voices[0].note != -1 || voices[1].note != -1 || voices[2].note != -1 || voices[3].note != -1 || voices[4].note != -1 || voices[5].note != -1)
  {
    updateVoice1();
    updateVoice2();
    updateVoice3();
    updateVoice4();
    updateVoice5();
    updateVoice6();
  }
  showCurrentParameterPage("1. Semitones", (vcoOctaveA > 0 ? "+" : "") + String(vcoOctaveA));
}

void updateOctaveB()
{
  //update waveforms with new frequencies if notes are on
  if (voices[0].note != -1 || voices[1].note != -1 || voices[2].note != -1 || voices[3].note != -1 || voices[4].note != -1 || voices[5].note != -1)
  {
    updateVoice1();
    updateVoice3();
    updateVoice2();
    updateVoice4();
    updateVoice5();
    updateVoice6();
  }
  showCurrentParameterPage("2. Semitones", (vcoOctaveB > 0 ? "+" : "") + String(vcoOctaveB));
}

void updateDetune()
{
  //update waveforms with new frequencies if notes are on
  if (voices[0].note != -1 || voices[1].note != -1 || voices[2].note != -1 || voices[3].note != -1 || voices[4].note != -1 || voices[5].note != -1)
  {
    updateVoice1();
    updateVoice2();
    updateVoice3();
    updateVoice4();
    updateVoice5();
    updateVoice6();
  }
  showCurrentParameterPage("Detune", String((1 - detune) * 100) + " %");
}

void updatePWMSource()
{
  if (pwmSource == PWMSOURCELFO)
  {
    setPwmMixerAFEnv(0);
    setPwmMixerBFEnv(0);
    if (pwmRate > -5)
    {
      setPwmMixerALFO(pwmAmtA);
      setPwmMixerBLFO(pwmAmtB);
    }
    showCurrentParameterPage("PWM Source", "LFO"); //Only shown when updated via MIDI
  }
  else
  {
    setPwmMixerALFO(0);
    setPwmMixerBLFO(0);
    if (pwmRate > -5)
    {
      setPwmMixerAFEnv(pwmAmtA);
      setPwmMixerBFEnv(pwmAmtB);
    }
    showCurrentParameterPage("PWM Source", "Filter Env");
  }
}

void updatePWMRate()
{
  pwmLfo.frequency(pwmRate);
  if (pwmRate < -5)
  {
    //Set to fixed PW mode
    setPwmMixerALFO(0);//LFO Source off
    setPwmMixerBLFO(0);
    setPwmMixerAFEnv(0);//Filter Env Source off
    setPwmMixerBFEnv(0);
    setPwmMixerAPW(1);//Manually adjustable pulse width on
    setPwmMixerBPW(1);
    showCurrentParameterPage("PW Mode", "On");
  }
  else if (pwmRate > -10 && pwmRate < 0) {
    //Set to Filter Env Mod source
    pwmSource = PWMSOURCEFENV;
    updatePWMSource();
    //Filter env mod - pwmRate does nothing
    setPwmMixerALFO(0);
    setPwmMixerBLFO(0);
    setPwmMixerAFEnv(pwmAmtA);
    setPwmMixerBFEnv(pwmAmtB);
    showCurrentParameterPage("PWM Source", "Filter Env");
  } else {

    pwmSource = PWMSOURCELFO;
    updatePWMSource();
    setPwmMixerAPW(0);
    setPwmMixerBPW(0);
    showCurrentParameterPage("PWM Rate", String(pwmRate) + " Hz");
  }
}

void updatePWMAmount()
{
  //MIDI only - sets both osc
  pwA = 0;
  pwB = 0;
  setPwmMixerALFO(pwmAmtA);
  setPwmMixerBLFO(pwmAmtB);
  showCurrentParameterPage("PWM Amt", String(pwmAmtA) + " " + String(pwmAmtB));
}

void updatePWA()
{
  if (pwmRate < -5)
  {
    //if PWM amount is around zero, fixed PW is enabled
    setPwmMixerALFO(0);
    setPwmMixerBLFO(0);
    setPwmMixerAFEnv(0);
    setPwmMixerBFEnv(0);
    setPwmMixerAPW(1);
    setPwmMixerBPW(1);
    if (vcoWaveformA == WAVEFORM_TRIANGLE_VARIABLE)
    {
      showCurrentParameterPage("1. PW Amt", pwA, VAR_TRI);
    }
    else
    {
      showCurrentParameterPage("1. PW Amt", pwA, PULSE);
    }
  }
  else
  {
    setPwmMixerAPW(0);
    setPwmMixerBPW(0);
    if (pwmSource == PWMSOURCELFO)
    {
      //PW alters PWM LFO amount for waveform A
      setPwmMixerALFO(pwmAmtA);
      showCurrentParameterPage("1. PWM Amt", "LFO " + String(pwmAmtA));
    }
    else
    {
      //PW alters PWM Filter Env amount for waveform A
      setPwmMixerAFEnv(pwmAmtA);
      showCurrentParameterPage("1. PWM Amt", "F. Env " + String(pwmAmtA));
    }
  }
  pwa.amplitude(pwA);
}

void updatePWB()
{
  if (pwmRate < -5)
  {
    //if PWM amount is around zero, fixed PW is enabled
    setPwmMixerALFO(0);
    setPwmMixerBLFO(0);
    setPwmMixerAFEnv(0);
    setPwmMixerBFEnv(0);
    setPwmMixerAPW(1);
    setPwmMixerBPW(1);
    if (vcoWaveformB == WAVEFORM_TRIANGLE_VARIABLE)
    {
      showCurrentParameterPage("2. PW Amt", pwB, VAR_TRI);
    }
    else
    {
      showCurrentParameterPage("2. PW Amt", pwB, PULSE);
    }
  }
  else
  {
    setPwmMixerAPW(0);
    setPwmMixerBPW(0);
    if (pwmSource == PWMSOURCELFO)
    {
      //PW alters PWM LFO amount for waveform B
      setPwmMixerBLFO(pwmAmtB);
      showCurrentParameterPage("2. PWM Amt", "LFO " + String(pwmAmtB));
    }
    else
    {
      //PW alters PWM Filter Env amount for waveform B
      setPwmMixerBFEnv(pwmAmtB);
      showCurrentParameterPage("2. PWM Amt", "F. Env " + String(pwmAmtB));
    }
  }
  pwb.amplitude(pwB);
}

void updateVcoLevelA()
{
  waveformMixer1.gain(0, VCOALevel);
  waveformMixer2.gain(0, VCOALevel);
  waveformMixer3.gain(0, VCOALevel);
  waveformMixer4.gain(0, VCOALevel);
  waveformMixer5.gain(0, VCOALevel);
  waveformMixer6.gain(0, VCOALevel);
  if (oscFX == 1)
  {
    waveformMixer1.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer2.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer3.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer4.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer5.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer6.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
  }
  showCurrentParameterPage("Osc Levels", "   " + String(VCOALevel) + " - " + String(VCOBLevel));
}

void updateVcoLevelB()
{
  waveformMixer1.gain(1, VCOBLevel);
  waveformMixer2.gain(1, VCOBLevel);
  waveformMixer3.gain(1, VCOBLevel);
  waveformMixer4.gain(1, VCOBLevel);
  waveformMixer5.gain(1, VCOBLevel);
  waveformMixer6.gain(1, VCOBLevel);
  if (oscFX == 1)
  {
    waveformMixer1.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer2.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer3.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer4.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer5.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
    waveformMixer6.gain(3, (VCOALevel + VCOBLevel) / 2.0f); //Osc FX
  }
  showCurrentParameterPage("Osc Levels", "   " + String(VCOALevel) + " - " + String(VCOBLevel));
}

void updateNoiseLevel()
{
  if (noiseLevel > 0)
  {
    pink.amplitude(noiseLevel);
    white.amplitude(0.0f);
    showCurrentParameterPage("Noise Level", "Pink " + String(noiseLevel));
  }
  else if (noiseLevel < 0)
  {
    pink.amplitude(0.0f);
    white.amplitude(abs(noiseLevel));
    showCurrentParameterPage("Noise Level", "White " + String(abs(noiseLevel)));
  }
  else
  {
    pink.amplitude(noiseLevel);
    white.amplitude(noiseLevel);
    showCurrentParameterPage("Noise Level", "Off");
  }
}

void updateFilterFreq()
{
  filter1.frequency(filterFreq);
  filter2.frequency(filterFreq);
  filter3.frequency(filterFreq);
  filter4.frequency(filterFreq);
  filter5.frequency(filterFreq);
  filter6.frequency(filterFreq);
  showCurrentParameterPage("Cutoff", String(int(filterFreq)) + " Hz");
}

void updateFilterRes()
{
  filter1.resonance(filterRes);
  filter2.resonance(filterRes);
  filter3.resonance(filterRes);
  filter4.resonance(filterRes);
  filter5.resonance(filterRes);
  filter6.resonance(filterRes);
  showCurrentParameterPage("Resonance", filterRes);
}

void updateFilterMixer()
{
  float LP = 1.0f;
  float BP = 0;
  float HP = 0;
  String filterStr;
  if (filterMix == LINEAR_FILTERMIXER[127])
  {
    //BP mode
    LP = 0;
    BP = 1.0f;
    HP = 0;
    filterStr = "Band Pass";
  }
  else
  {
    //LP-HP mix mode - a notch filter
    LP = 1.0f - filterMix;
    BP = 0;
    HP = filterMix;
    if (filterMix == LINEAR_FILTERMIXER[0])
    {
      filterStr = "Low Pass";
    }
    else if (filterMix == LINEAR_FILTERMIXER[125])
    {
      filterStr = "High Pass";
    }
    else
    {
      filterStr = "LP " + String(100 - filterMixStr) + " - " + String(filterMixStr) + " HP";
    }
  }
  filterMixer1.gain(0, LP);
  filterMixer1.gain(1, BP);
  filterMixer1.gain(2, HP);
  filterMixer2.gain(0, LP);
  filterMixer2.gain(1, BP);
  filterMixer2.gain(2, HP);
  filterMixer3.gain(0, LP);
  filterMixer3.gain(1, BP);
  filterMixer3.gain(2, HP);
  filterMixer4.gain(0, LP);
  filterMixer4.gain(1, BP);
  filterMixer4.gain(2, HP);
  filterMixer5.gain(0, LP);
  filterMixer5.gain(1, BP);
  filterMixer5.gain(2, HP);
  filterMixer6.gain(0, LP);
  filterMixer6.gain(1, BP);
  filterMixer6.gain(2, HP);
  showCurrentParameterPage("Filter Type", filterStr);
}

void updateFilterEnv()
{
  vcfModMixer1.gain(0, filterEnv);
  vcfModMixer2.gain(0, filterEnv);
  vcfModMixer3.gain(0, filterEnv);
  vcfModMixer4.gain(0, filterEnv);
  vcfModMixer5.gain(0, filterEnv);
  vcfModMixer6.gain(0, filterEnv);
  showCurrentParameterPage("Filter Env.", String(filterEnv));
}

void updatePitchEnv()
{
  vcoModMixer1a.gain(1, pitchEnv);
  vcoModMixer1b.gain(1, pitchEnv);
  vcoModMixer2a.gain(1, pitchEnv);
  vcoModMixer2b.gain(1, pitchEnv);
  vcoModMixer3a.gain(1, pitchEnv);
  vcoModMixer3b.gain(1, pitchEnv);
  vcoModMixer4a.gain(1, pitchEnv);
  vcoModMixer4b.gain(1, pitchEnv);
  vcoModMixer5a.gain(1, pitchEnv);
  vcoModMixer5b.gain(1, pitchEnv);
  vcoModMixer6a.gain(1, pitchEnv);
  vcoModMixer6b.gain(1, pitchEnv);
  showCurrentParameterPage("Pitch Env Amt", String(pitchEnv));
}

void updateKeyTracking()
{
  vcfModMixer1.gain(2, keytrackingAmount);
  vcfModMixer2.gain(2, keytrackingAmount);
  vcfModMixer3.gain(2, keytrackingAmount);
  vcfModMixer4.gain(2, keytrackingAmount);
  vcfModMixer5.gain(2, keytrackingAmount);
  vcfModMixer6.gain(2, keytrackingAmount);
  showCurrentParameterPage("Key Tracking", String(keytrackingAmount));
}

void updateVcoLFOAmt()
{
  vcoLfo.amplitude(vcoLfoAmt);
  char buf[10];
  showCurrentParameterPage("LFO Amount", dtostrf(vcoLfoAmt, 4, 3, buf));
}

void updateModWheel()
{
  vcoLfo.amplitude(vcoLfoAmt);
}

void updateVcoLFORate()
{
  vcoLfo.frequency(vcoLfoRate);
  showCurrentParameterPage("LFO Rate", String(vcoLfoRate) + " Hz");
}

void updateVcoLFOWaveform()
{
  vcoLfo.begin(vcoLFOWaveform);
  showCurrentParameterPage("Pitch LFO", getWaveformStr(vcoLFOWaveform));
}

//MIDI CC only
void updateVcoLFOMidiClkSync()
{
  showCurrentParameterPage("P. LFO Sync", vcoLFOMidiClkSync == 1 ? "On" : "Off");
}

void updateVcfLfoRate()
{
  vcfLfo.frequency(vcfLfoRate);
  if (vcfLFOMidiClkSync)
  {
    showCurrentParameterPage("LFO Time Div", vcfLFOTimeDivStr);
  }
  else
  {
    showCurrentParameterPage("F. LFO Rate", String(vcfLfoRate) + " Hz");
  }
}

void updateVcfLfoAmt()
{
  vcfLfo.amplitude(vcfLfoAmt);
  showCurrentParameterPage("F. LFO Amt", String(vcfLfoAmt));
}

void updateVcfLFOWaveform()
{
  vcfLfo.begin(vcfLfoWaveform);
  showCurrentParameterPage("Filter LFO", getWaveformStr(vcfLfoWaveform));
}

void updateVcoLFORetrig()
{
  showCurrentParameterPage("P. LFO Retrig", vcoLfoRetrig == 1 ? "On" : "Off");
}

void updateVcfLFORetrig()
{
  showCurrentParameterPage("F. LFO Retrig", vcfLfoRetrig == 1 ? "On" : "Off");
  digitalWrite(RETRIG_LED, vcfLfoRetrig == 1 ? HIGH : LOW);  // LED
}

void updateVcfLFOMidiClkSync()
{
  showCurrentParameterPage("Tempo Sync", vcfLFOMidiClkSync == 1 ? "On" : "Off");
  digitalWrite(TEMPO_LED, vcfLFOMidiClkSync == 1 ? HIGH : LOW);  // LED
}

void updateVcfAttack()
{
  vcfEnvelope1.attack(vcfAttack);
  vcfEnvelope2.attack(vcfAttack);
  vcfEnvelope3.attack(vcfAttack);
  vcfEnvelope4.attack(vcfAttack);
  vcfEnvelope5.attack(vcfAttack);
  vcfEnvelope6.attack(vcfAttack);
  if (vcfAttack < 1000)
  {
    showCurrentParameterPage("Filter Attack", String(int(vcfAttack)) + " ms", FILTER_ENV);
  }
  else
  {
    showCurrentParameterPage("Filter Attack", String(vcfAttack * 0.001) + " s", FILTER_ENV);
  }
}

void updateVcfDecay()
{
  vcfEnvelope1.decay(vcfDecay);
  vcfEnvelope2.decay(vcfDecay);
  vcfEnvelope3.decay(vcfDecay);
  vcfEnvelope4.decay(vcfDecay);
  vcfEnvelope5.decay(vcfDecay);
  vcfEnvelope6.decay(vcfDecay);
  if (vcfDecay < 1000)
  {
    showCurrentParameterPage("Filter Decay", String(int(vcfDecay)) + " ms", FILTER_ENV);
  }
  else
  {
    showCurrentParameterPage("Filter Decay", String(vcfDecay * 0.001) + " s", FILTER_ENV);
  }
}

void updateVcfSustain()
{
  vcfEnvelope1.sustain(vcfSustain);
  vcfEnvelope2.sustain(vcfSustain);
  vcfEnvelope3.sustain(vcfSustain);
  vcfEnvelope4.sustain(vcfSustain);
  vcfEnvelope5.sustain(vcfSustain);
  vcfEnvelope6.sustain(vcfSustain);
  showCurrentParameterPage("Filter Sustain", String(vcfSustain), FILTER_ENV);
}

void updateVcfRelease()
{
  vcfEnvelope1.release(vcfRelease);
  vcfEnvelope2.release(vcfRelease);
  vcfEnvelope3.release(vcfRelease);
  vcfEnvelope4.release(vcfRelease);
  vcfEnvelope5.release(vcfRelease);
  vcfEnvelope6.release(vcfRelease);
  if (vcfRelease < 1000)
  {
    showCurrentParameterPage("Filter Release", String(int(vcfRelease)) + " ms", FILTER_ENV);
  }
  else
  {
    showCurrentParameterPage("Filter Release", String(vcfRelease * 0.001) + " s", FILTER_ENV);
  }
}

void updateVcaAttack()
{
  vcaEnvelope1.attack(vcaAttack);
  vcaEnvelope2.attack(vcaAttack);
  vcaEnvelope3.attack(vcaAttack);
  vcaEnvelope4.attack(vcaAttack);
  vcaEnvelope5.attack(vcaAttack);
  vcaEnvelope6.attack(vcaAttack);
  if (vcaAttack < 1000)
  {
    showCurrentParameterPage("Attack", String(int(vcaAttack)) + " ms", AMP_ENV);
  }
  else
  {
    showCurrentParameterPage("Attack", String(vcaAttack * 0.001) + " s", AMP_ENV);
  }
}

void updateVcaDecay()
{
  vcaEnvelope1.decay(vcaDecay);
  vcaEnvelope2.decay(vcaDecay);
  vcaEnvelope3.decay(vcaDecay);
  vcaEnvelope4.decay(vcaDecay);
  vcaEnvelope5.decay(vcaDecay);
  vcaEnvelope6.decay(vcaDecay);
  if (vcaDecay < 1000)
  {
    showCurrentParameterPage("Decay", String(int(vcaDecay)) + " ms", AMP_ENV);
  }
  else
  {
    showCurrentParameterPage("Decay", String(vcaDecay * 0.001) + " s", AMP_ENV);
  }
}

void updateVcaSustain()
{
  vcaEnvelope1.sustain(vcaSustain);
  vcaEnvelope2.sustain(vcaSustain);
  vcaEnvelope3.sustain(vcaSustain);
  vcaEnvelope4.sustain(vcaSustain);
  vcaEnvelope5.sustain(vcaSustain);
  vcaEnvelope6.sustain(vcaSustain);
  showCurrentParameterPage("Sustain", String(vcaSustain), AMP_ENV);
}

void updateVcaRelease()
{
  vcaEnvelope1.release(vcaRelease);
  vcaEnvelope2.release(vcaRelease);
  vcaEnvelope3.release(vcaRelease);
  vcaEnvelope4.release(vcaRelease);
  vcaEnvelope5.release(vcaRelease);
  vcaEnvelope6.release(vcaRelease);
  if (vcaRelease < 1000)
  {
    showCurrentParameterPage("Release", String(int(vcaRelease)) + " ms", AMP_ENV);
  }
  else
  {
    showCurrentParameterPage("Release", String(vcaRelease * 0.001) + " s", AMP_ENV);
  }
}

void updateOscFX()
{
  if (oscFX == 1)
  {
    oscFX1.setCombineMode(AudioEffectDigitalCombine::XOR);
    oscFX2.setCombineMode(AudioEffectDigitalCombine::XOR);
    oscFX3.setCombineMode(AudioEffectDigitalCombine::XOR);
    oscFX4.setCombineMode(AudioEffectDigitalCombine::XOR);
    oscFX5.setCombineMode(AudioEffectDigitalCombine::XOR);
    oscFX6.setCombineMode(AudioEffectDigitalCombine::XOR);
    waveformMixer1.gain(3, (VCOALevel + VCOBLevel) / 2.0); //Osc FX
    waveformMixer2.gain(3, (VCOALevel + VCOBLevel) / 2.0); //Osc FX
    waveformMixer3.gain(3, (VCOALevel + VCOBLevel) / 2.0); //Osc FX
    waveformMixer4.gain(3, (VCOALevel + VCOBLevel) / 2.0); //Osc FX
    waveformMixer5.gain(3, (VCOALevel + VCOBLevel) / 2.0); //Osc FX
    waveformMixer6.gain(3, (VCOALevel + VCOBLevel) / 2.0); //Osc FX
    showCurrentParameterPage("Osc FX", "On");
    digitalWrite(OSC_FX_LED, HIGH);  // LED on
  }
  else
  {
    oscFX1.setCombineMode(AudioEffectDigitalCombine::OFF);
    oscFX2.setCombineMode(AudioEffectDigitalCombine::OFF);
    oscFX3.setCombineMode(AudioEffectDigitalCombine::OFF);
    oscFX4.setCombineMode(AudioEffectDigitalCombine::OFF);
    oscFX5.setCombineMode(AudioEffectDigitalCombine::OFF);
    oscFX6.setCombineMode(AudioEffectDigitalCombine::OFF);
    waveformMixer1.gain(3, 0); //Osc FX
    waveformMixer2.gain(3, 0); //Osc FX
    waveformMixer3.gain(3, 0); //Osc FX
    waveformMixer4.gain(3, 0); //Osc FX
    waveformMixer5.gain(3, 0); //Osc FX
    waveformMixer6.gain(3, 0); //Osc FX
    showCurrentParameterPage("Osc FX", "Off");
    digitalWrite(OSC_FX_LED, LOW);  // LED off
  }
}

void updateFXAmt()
{
  ensemble.lfoRate(fxAmt);
  showCurrentParameterPage("Effect Amt", String(fxAmt) + " Hz");
}

void updateFXMix()
{
  effectMixerL.gain(0, 1.0 - fxMix); //Dry
  effectMixerL.gain(1, fxMix);       //Wet
  effectMixerR.gain(0, 1.0 - fxMix); //Dry
  effectMixerR.gain(1, fxMix);       //Wet
  showCurrentParameterPage("Effect Mix", String(fxMix));
}

void updatePatchname()
{
  showPatchPage(String(patchNo), patchName);
}

void myPitchBend(byte channel, int bend)
{
  pitchBend.amplitude(bend * 0.5 * pitchBendRange * DIV12 * DIV8192); //)0.5 to give 1oct max - spread of mod is 2oct
}

void myControlChange(byte channel, byte control, byte value)
{

  //Serial.println("MIDI: " + String(control) + " : " + String(value));
  switch (control)
  {
    case CCvolume:
      sgtl5000_1.volume(SGTL_MAXVOLUME * LINEAR[value]); //Headphones
      //sgtl5000_1.lineOutLevel(31 - (18 * LINEAR[value])); //Line out, weird inverted values
      updateVolume(LINEAR[value]);
      break;

    case CCunison:
      value > 0 ? unison = 1 : unison = 0;
      updateUnison();
      break;

    case CCglide:
      glideSpeed = LINEAR[value];
      updateGlide();
      break;

    case CCpitchenv:
      pitchEnv = LINEARCENTREZERO[value] * VCOMODMIXERMAX;
      updatePitchEnv();
      break;

    case CCvcowaveformA:
      vcoWaveformA = getVCOWaveformA(value);

      updateWaveformA();
      break;

    case CCvcowaveformB:
      vcoWaveformB = getVCOWaveformB(value);


      updateWaveformB();
      break;

    case CCoctaveA:
      vcoOctaveA = getVCOOctave(value);
      updateOctaveA();
      break;

    case CCoctaveB:
      vcoOctaveB = getVCOOctave(value);
      updateOctaveB();
      break;

    case CCdetune:
      detune = 1 - (MAXDETUNE * LINEAR[value]);
      updateDetune();
      break;

    case CCpwmSource:
      value > 0 ? pwmSource = PWMSOURCEFENV : pwmSource = PWMSOURCELFO;
      updatePWMSource();
      break;

    case CCpwmRate:
      //Uses combination of PWMRate, PWa and PWb
      pwmRate = PWMRATE[value];
      updatePWMRate();
      break;

    case CCpwmAmt:
      //NO FRONT PANEL CONTROL - MIDI CC ONLY
      //Total PWM amount for both oscillators
      pwmAmtA = LINEAR[value];
      pwmAmtB = LINEAR[value];
      updatePWMAmount();
      break;

    case CCpwA:
      pwA = LINEARCENTREZERO[value]; //Bipolar
      pwmAmtA = LINEAR[value];
      updatePWA();
      break;

    case CCpwB:
      pwB = LINEARCENTREZERO[value]; //Bipolar
      pwmAmtB = LINEAR[value];
      updatePWB();
      break;

    case CCvcoLevelA:
      VCOALevel = LINEAR[value];
      updateVcoLevelA();
      break;

    case CCvcoLevelB:
      VCOBLevel = LINEAR[value];
      updateVcoLevelB();
      break;

    case CCnoiseLevel:
      noiseLevel = LINEARCENTREZERO[value];
      updateNoiseLevel();
      break;

    case CCfilterfreq:
      filterFreq = FILTERFREQS[value];
      updateFilterFreq();
      break;

    case CCfilterres:
      filterRes = (13.9f * POWER[value]) + 1.1f; //If <1.1 there is noise at high cutoff freq
      updateFilterRes();
      break;

    case CCfiltermixer:
      filterMix = LINEAR_FILTERMIXER[value];
      filterMixStr = LINEAR_FILTERMIXERSTR[value];
      updateFilterMixer();
      break;

    case CCfilterenv:
      filterEnv = LINEARCENTREZERO[value] * VCFMODMIXERMAX; //Bipolar
      updateFilterEnv();
      break;

    case CCkeytracking:
      keytrackingAmount = KEYTRACKINGAMT[value];
      updateKeyTracking();
      break;

    case CCmodwheel:
      vcoLfoAmt = POWER[value] * modWheelDepth; //Variable LFO amount from mod wheel - Menu Option
      updateModWheel();
      break;

    case CCvcolfoamt:
      vcoLfoAmt = POWER[value];
      updateVcoLFOAmt();
      break;

    case CCvcoLfoRate:
      if (vcoLFOMidiClkSync == 1)
      {
        vcoLfoRate = getLFOTempoRate(value);
        vcoLFOTimeDivStr = LFOTEMPOSTR[value];
      }
      else
      {
        vcoLfoRate = LFOMAXRATE * POWER[value];
      }
      updateVcoLFORate();
      break;

    case CCvcoLfoWaveform:
      vcoLFOWaveform = getLFOWaveform(value);
      updateVcoLFOWaveform();
      break;

    case CCvcolforetrig:
      value > 0 ? vcoLfoRetrig = 1 : vcoLfoRetrig = 0;
      updateVcoLFORetrig();
      break;

    case CCvcfLFOMidiClkSync:
      value > 0 ? vcfLFOMidiClkSync = 1 : vcfLFOMidiClkSync = 0;
      updateVcfLFOMidiClkSync();
      break;

    case CCvcflforate:
      if (vcfLFOMidiClkSync == 1)
      {
        vcfLfoRate = getLFOTempoRate(value);
        vcfLFOTimeDivStr = LFOTEMPOSTR[value];
      }
      else
      {
        vcfLfoRate = LFOMAXRATE * POWER[value];
      }
      updateVcfLfoRate();
      break;

    case CCvcflfoamt:
      vcfLfoAmt = LINEAR[value] * VCFMODMIXERMAX;
      updateVcfLfoAmt();
      break;

    case CCvcflfowaveform:
      vcfLfoWaveform = getLFOWaveform(value);
      updateVcfLFOWaveform();
      break;

    case CCvcflforetrig:
      value > 0 ? vcfLfoRetrig = 1 : vcfLfoRetrig = 0;
      updateVcfLFORetrig();
      break;

    //MIDI Only
    case CCvcoLFOMidiClkSync:
      value > 0 ? vcoLFOMidiClkSync = 1 : vcoLFOMidiClkSync = 0;
      updateVcoLFOMidiClkSync();
      break;

    case CCvcfattack:
      vcfAttack = ENVTIMES[value];
      updateVcfAttack();
      break;

    case CCvcfdecay:
      vcfDecay = ENVTIMES[value];
      updateVcfDecay();
      break;

    case CCvcfsustain:
      vcfSustain = LINEAR[value];
      updateVcfSustain();
      break;

    case CCvcfrelease:
      vcfRelease = ENVTIMES[value];
      updateVcfRelease();
      break;

    case CCvcaattack:
      vcaAttack = ENVTIMES[value];
      updateVcaAttack();
      break;

    case CCvcadecay:
      vcaDecay = ENVTIMES[value];
      updateVcaDecay();
      break;

    case CCvcasustain:
      vcaSustain = LINEAR[value];
      updateVcaSustain();
      break;

    case CCvcarelease:
      vcaRelease = ENVTIMES[value];
      updateVcaRelease();
      break;

    case CCringmod:
      value > 0 ? oscFX = 1 : oscFX = 0;
      updateOscFX();
      break;

    case CCfxamt:
      fxAmt = ENSEMBLE_LFO[value];
      updateFXAmt();
      break;

    case CCfxmix:
      fxMix = LINEAR[value];
      updateFXMix();
      break;

    case CCallnotesoff:
      allNotesOff();
      break;
  }
}

void myProgramChange(byte channel, byte program)
{
  state = PATCH;
  Serial.print("MIDI Pgm Change:");
  Serial.println(String(program + 1));
  patchNo = program + 1;
  recallPatch(String(program + 1));
  state = PARAMETER;
}

void myMIDIClockStart()
{
  //Resync LFOs when MIDI Clock starts.
  //When there's a jump to a different
  //part of a track, such as in a DAW, the DAW must have same
  //rhythmic quantisation as Tempo Div.
  if (vcoLFOMidiClkSync == 1)
  {
    vcoLfo.sync();
  }
  if (vcfLFOMidiClkSync == 1)
  {
    vcfLfo.sync();
  }
}

void myMIDIClock()
{
  //This recalculates theLFO frequencies if the tempo changes (MIDI cLock is 24ppq)
  if ((vcoLFOMidiClkSync == 1 || vcfLFOMidiClkSync == 1) && count > 23)
  {
    float timeNow = millis();
    midiClkTimeInterval = (timeNow - previousMillis);
    lfoSyncFreq = 1000.0 / midiClkTimeInterval;
    previousMillis = timeNow;
    if (vcoLFOMidiClkSync == 1)
      vcoLfo.frequency(lfoSyncFreq * lfoTempoValue); //MIDI CC only
    if (vcfLFOMidiClkSync == 1)
      vcfLfo.frequency(lfoSyncFreq * lfoTempoValue);
    count = 0;
  }
  if (count < 24)
    count++; //prevent eventual overflow
}

void recallPatch(String patchNo)
{
  File patchFile = SD.open(patchNo.c_str());
  if (!patchFile)
  {
    Serial.println("File not found");
  }
  else
  {
    String data[NO_OF_PARAMS]; //Array of data read in
    recallPatchData(patchFile, data);
    setCurrentPatchData(data);
    patchFile.close();
  }
}

void setCurrentPatchData(String data[])
{
  patchName = data[0];
  VCOALevel = data[1].toFloat();
  VCOBLevel = data[2].toFloat();
  noiseLevel = data[3].toFloat();
  unison = data[4].toInt();
  oscFX = data[5].toInt();
  detune = data[6].toFloat();
  lfoSyncFreq = data[7].toInt();
  midiClkTimeInterval = data[8].toInt();
  lfoTempoValue = data[9].toFloat();
  keytrackingAmount = data[10].toFloat();
  glideSpeed = data[11].toFloat();
  vcoOctaveA = data[12].toFloat();
  vcoOctaveB = data[13].toFloat();
  vcoWaveformA = data[14].toInt();
  vcoWaveformB = data[15].toInt();
  pwmSource = data[16].toInt();
  pwmAmtA = data[17].toFloat();
  pwmAmtB = data[18].toFloat();
  pwmRate = data[19].toFloat();
  pwA = data[20].toFloat();
  pwB = data[21].toFloat();
  filterRes = data[22].toFloat();
  filterFreq = data[23].toFloat();
  filterMix = data[24].toFloat();
  filterEnv = data[25].toFloat();
  vcoLfoAmt = data[26].toFloat();
  vcoLfoRate = data[27].toFloat();
  vcoLFOWaveform = data[28].toFloat();
  vcoLfoRetrig = data[29].toInt();
  vcoLFOMidiClkSync = data[30].toFloat(); //MIDI CC Only
  vcfLfoRate = data[31].toFloat();
  vcfLfoRetrig = data[32].toInt();
  vcfLFOMidiClkSync = data[33].toFloat();
  vcfLfoAmt = data[34].toFloat();
  vcfLfoWaveform = data[35].toFloat();
  vcfAttack = data[36].toFloat();
  vcfDecay = data[37].toFloat();
  vcfSustain = data[38].toFloat();
  vcfRelease = data[39].toFloat();
  vcaAttack = data[40].toFloat();
  vcaDecay = data[41].toFloat();
  vcaSustain = data[42].toFloat();
  vcaRelease = data[43].toFloat();
  fxAmt = data[44].toFloat();
  fxMix = data[45].toFloat();
  pitchEnv = data[46].toFloat();

  updatePatchname();
  updateUnison();
  updateWaveformA();
  updateWaveformB();
  updateOctaveA();
  updateOctaveB();
  updateDetune();
  updatePWMSource();
  updatePWMAmount();
  updatePWA();
  updatePWB();
  updatePWMRate();
  updateVcoLevelA();
  updateVcoLevelB();
  updateNoiseLevel();
  updateFilterFreq();
  updateFilterRes();
  updateFilterMixer();
  updateFilterEnv();
  updateKeyTracking();
  updateVcoLFOAmt();
  updateVcoLFORate();
  updateVcoLFOWaveform();
  updateVcoLFOMidiClkSync();
  updateVcfLfoRate();
  updateVcfLfoAmt();
  updateVcfLFOWaveform();
  updateVcfLFOMidiClkSync();
  updateVcfLFORetrig();
  updateVcfAttack();
  updateVcfDecay();
  updateVcfSustain();
  updateVcfRelease();
  updateVcaAttack();
  updateVcaDecay();
  updateVcaSustain();
  updateVcaRelease();
  updateOscFX();
  updateFXAmt();
  updateFXMix();
  updatePitchEnv();
  Serial.print("Set Patch: ");
  Serial.println(patchName);
}

String getCurrentPatchData()
{
  return patchName + "," + String(VCOALevel) + "," + String(VCOBLevel) + "," + String(noiseLevel) + "," + String(unison) + "," + String(oscFX) + "," + String(detune) + "," + String(lfoSyncFreq) + "," + String(midiClkTimeInterval) + "," + String(lfoTempoValue) + "," + String(keytrackingAmount) + "," + String(glideSpeed) + "," + String(vcoOctaveA) + "," + String(vcoOctaveB) + "," + String(vcoWaveformA) + "," + String(vcoWaveformB) + "," +
         String(pwmSource) + "," + String(pwmAmtA) + "," + String(pwmAmtB) + "," + String(pwmRate) + "," + String(pwA) + "," + String(pwB) + "," + String(filterRes) + "," + String(filterFreq) + "," + String(filterMix) + "," + String(filterEnv) + "," + String(vcoLfoAmt) + "," + String(vcoLfoRate) + "," + String(vcoLFOWaveform) + "," + String(vcoLfoRetrig) + "," + String(vcoLFOMidiClkSync) + "," + String(vcfLfoRate) + "," +
         vcfLfoRetrig + "," + vcfLFOMidiClkSync + "," + vcfLfoAmt + "," + vcfLfoWaveform + "," + vcfAttack + "," + vcfDecay + "," + vcfSustain + "," + vcfRelease + "," + vcaAttack + "," + vcaDecay + "," + vcaSustain + "," + vcaRelease + "," +
         String(fxAmt) + "," + String(fxMix) + "," + String(pitchEnv);
}

void checkMux()
{
  mux1Read = analogRead(MUX1_S);
  mux2Read = analogRead(MUX2_S);
  if (mux1Read > (mux1ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux1Read < (mux1ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux1ValuesPrev[muxInput] = mux1Read;
    mux1Read = (mux1Read >> 3); //Change range to 0-127

    switch (muxInput)
    {
      case MUX1_noiseLevel:
        myControlChange(midiChannel, CCnoiseLevel, mux1Read);
        break;
      case MUX1_pitchLfoRate:
        myControlChange(midiChannel, CCvcoLfoRate, mux1Read);
        break;
      case MUX1_pitchLfoWaveform:
        myControlChange(midiChannel, CCvcoLfoWaveform, mux1Read);
        break;
      case MUX1_pitchLfoAmount:
        myControlChange(midiChannel, CCvcolfoamt, mux1Read);
        break;
      case MUX1_detune:
        myControlChange(midiChannel, CCdetune, mux1Read);
        break;
      case MUX1_oscMix:
        myControlChange(midiChannel, CCvcoLevelA, OSCMIXA[mux1Read]);
        myControlChange(midiChannel, CCvcoLevelB, OSCMIXB[mux1Read]);
        break;
      case MUX1_filterAttack:
        myControlChange(midiChannel, CCvcfattack, mux1Read);
        break;
      case MUX1_filterDecay:
        myControlChange(midiChannel, CCvcfdecay, mux1Read);
        break;
      case MUX1_pwmAmountA:
        myControlChange(midiChannel, CCpwA, mux1Read);
        break;
      case MUX1_waveformA:
        myControlChange(midiChannel, CCvcowaveformA, mux1Read);
        break;
      case MUX1_octaveA:
        myControlChange(midiChannel, CCoctaveA, mux1Read);
        break;
      case MUX1_pwmAmountB:
        myControlChange(midiChannel, CCpwB, mux1Read);
        break;
      case MUX1_waveformB:
        myControlChange(midiChannel, CCvcowaveformB, mux1Read);
        break;
      case MUX1_octaveB:
        myControlChange(midiChannel, CCoctaveB, mux1Read);
        break;
      case MUX1_pwmRate:
        myControlChange(midiChannel, CCpwmRate, mux1Read);
        break;
      case MUX1_pitchEnv:
        myControlChange(midiChannel, CCpitchenv, mux1Read);
        break;
    }
  }

  if (mux2Read > (mux2ValuesPrev[muxInput] + QUANTISE_FACTOR) || mux2Read < (mux2ValuesPrev[muxInput] - QUANTISE_FACTOR))
  {
    mux2ValuesPrev[muxInput] = mux2Read;
    mux2Read = (mux2Read >> 3); //Change range to 0-127

    switch (muxInput)
    {
      case MUX2_attack:
        myControlChange(midiChannel, CCvcaattack, mux2Read);
        break;
      case MUX2_decay:
        myControlChange(midiChannel, CCvcadecay, mux2Read);
        break;
      case MUX2_sustain:
        myControlChange(midiChannel, CCvcasustain, mux2Read);
        break;
      case MUX2_release:
        myControlChange(midiChannel, CCvcarelease, mux2Read);
        break;
      case MUX2_filterLFOAmount:
        myControlChange(midiChannel, CCvcflfoamt, mux2Read);
        break;
      case MUX2_FXMix:
        myControlChange(midiChannel, CCfxmix, mux2Read);
        break;
      case MUX2_FXAmount:
        myControlChange(midiChannel, CCfxamt, mux2Read);
        break;
      case MUX2_glide:
        myControlChange(midiChannel, CCglide, mux2Read);
        break;
      case MUX2_filterEnv:
        myControlChange(midiChannel, CCfilterenv, mux2Read);
        break;
      case MUX2_filterRelease:
        myControlChange(midiChannel, CCvcfrelease, mux2Read);
        break;
      case MUX2_filterSustain:
        myControlChange(midiChannel, CCvcfsustain, mux2Read);
        break;
      case MUX2_filterType:
        myControlChange(midiChannel, CCfiltermixer, mux2Read);
        break;
      case MUX2_resonance:
        myControlChange(midiChannel, CCfilterres, mux2Read);
        break;
      case MUX2_cutoff:
        myControlChange(midiChannel, CCfilterfreq, mux2Read);
        break;
      case MUX2_filterLFORate:
        myControlChange(midiChannel, CCvcflforate, mux2Read);
        break;
      case MUX2_filterLFOWaveform:
        myControlChange(midiChannel, CCvcflfowaveform, mux2Read);
        break;
    }
  }
  muxInput++;
  if (muxInput >= MUXCHANNELS)
    muxInput = 0;

  digitalWrite(MUX_0, muxInput & B0001);
  digitalWrite(MUX_1, muxInput & B0010);
  digitalWrite(MUX_2, muxInput & B0100);
  digitalWrite(MUX_3, muxInput & B1000);
}

void checkVolumePot()
{
  volumeRead = analogRead(VOLUME_POT);
  if (volumeRead > (volumePrevious + QUANTISE_FACTOR) || volumeRead < (volumePrevious - QUANTISE_FACTOR))
  {
    volumePrevious = volumeRead;
    volumeRead = (volumeRead >> 3); //Change range to 0-127
    myControlChange(midiChannel, CCvolume, volumeRead);
  }
}

void checkSwitches()
{
  unisonSwitch.update();
  if (unisonSwitch.fallingEdge())
  {
    unison = !unison;
    myControlChange(midiChannel, CCunison, unison);
  }

  oscFXSwitch.update();
  if (oscFXSwitch.fallingEdge())
  {
    oscFX = !oscFX;
    myControlChange(midiChannel, CCringmod, oscFX);
  }

  vcfLFORetrigSwitch.update();
  if (vcfLFORetrigSwitch.fallingEdge())
  {
    vcfLfoRetrig = !vcfLfoRetrig;
    myControlChange(midiChannel, CCvcflforetrig, vcfLfoRetrig);
  }

  tempoSwitch.update();
  if (tempoSwitch.fallingEdge())
  {
    vcfLFOMidiClkSync = !vcfLFOMidiClkSync;
    myControlChange(midiChannel, CCvcfLFOMidiClkSync, vcfLFOMidiClkSync);
  }

  saveButton.update();
  if (saveButton.read() == LOW && saveButton.duration() > HOLD_DURATION)
  {
    switch (state)
    {
      case PARAMETER:
      case PATCH:
        state = DELETE;
        saveButton.write(HIGH); //Come out of this state
        del = true;             //Hack
        break;
    }
  }
  else if (saveButton.risingEdge())
  {
    if (!del)
    {
      switch (state)
      {
        case PARAMETER:
          if (patches.size() < PATCHES_LIMIT)
          {
            getPatches(SD.open("/")); //Reload patches from SD
            patches.push({patches.size() + 1, INITPATCHNAME});
            state = SAVE;
          }
          break;
        case SAVE:
          //Save as new patch with INITIALPATCH or overwrite existing keeping name - bypassing patch renaming
          patchName = patches.last().patchName;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patches.last().patchName);
          getPatches(SD.open("/")); //Get rid of pushed patchNo if it wasn't saved TODO - Also resets circularbuffer
          renamedPatch = "";
          state = PARAMETER;
          break;
        case PATCHNAMING:
          //Save renamed patch
          //sort patches so new patch is saved at end
          patchName = renamedPatch;
          state = PATCH;
          savePatch(String(patches.last().patchNo).c_str(), getCurrentPatchData());
          showPatchPage(patches.last().patchNo, patchName);
          getPatches(SD.open("/")); //Get rid of pushed patchNo if it wasn't saved TODO - Also resets circularbuffer
          renamedPatch = "";
          state = PARAMETER;
          break;
      }
    }
    else
    {
      del = false;
    }
  }

  menuButton.update();
  if (menuButton.read() == LOW && menuButton.duration() > HOLD_DURATION)
  {
    //If recall held, set current patch to match current hardware state
    //Reinitialise all hardware values to force them to be re-read if different
    state = REINITIALISE;
    reinitialiseToPanel();
    menuButton.write(HIGH); //Come out of this state
    reini = true;           //Hack
  }
  else if (menuButton.risingEdge())
  { //cannot be fallingEdge because holding button won't work
    if (!reini)
    {
      switch (state)
      {
        case PARAMETER:
          menuValueIndex = getCurrentIndex(menuOptions.first().currentIndex);
          showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex], MENU);
          state = MENU;
          break;
        case MENU:
          menuOptions.push(menuOptions.shift());
          menuValueIndex = getCurrentIndex(menuOptions.first().currentIndex);
          showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex], MENU);
        case MENUVALUE:
          menuValueIndex = getCurrentIndex(menuOptions.first().currentIndex);
          showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex], MENU);
          state = MENU;
          break;
      }
    }
    else
    {
      reini = false;
    }
  }

  backButton.update();
  if (backButton.read() == LOW && backButton.duration() > HOLD_DURATION)
  {
    //If Back button held, Panic - all notes off
    allNotesOff();
    backButton.write(HIGH); //Come out of this state
    panic = true;           //Hack
  }
  else if (backButton.risingEdge())
  { //cannot be fallingEdge because holding button won't work
    if (!panic)
    {
      switch (state)
      {
        case RECALL:
          state = PARAMETER;
          break;
        case SAVE:
          renamedPatch = "";
          state = PARAMETER;
          getPatches(SD.open("/"));
          break;
        case PATCHNAMING:
          renamedPatch = "";
          state = SAVE;
          break;
        case DELETE:
          state = PARAMETER;
          break;
        case MENU:
          state = PARAMETER;
          break;
        case MENUVALUE:
          menuValueIndex = getCurrentIndex(menuOptions.first().currentIndex);
          showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex], MENU);
          state = MENU;
          break;
      }
    }
    else
    {
      panic = false;
    }
  }

  //Encoder switch
  recallButton.update();
  if (recallButton.read() == LOW && recallButton.duration() > HOLD_DURATION)
  {
    //If Recall button held, return to current patch setting
    //which clears any changes made
    state = PATCH;
    //Recall the current patch
    patchNo = patches.first().patchNo;
    recallPatch(String(patchNo));
    state = PARAMETER;

    recallButton.write(HIGH); //Come out of this state
    recall = true;            //Hack
  }
  else if (recallButton.risingEdge())
  {
    if (!recall)
    {
      switch (state)
      {
        case PARAMETER:
          state = RECALL;//show patch list
          break;
        case RECALL:
          state = PATCH;
          //Recall the current patch
          patchNo = patches.first().patchNo;
          recallPatch(String(patchNo));
          state = PARAMETER;
          break;
        case SAVE:
          showRenamingPage(patches.last().patchName);
          state = PATCHNAMING;
          break;
        case PATCHNAMING:
          if (renamedPatch.length() < 16)
          {
            renamedPatch.concat(String(currentCharacter));
            charIndex = 0;
            currentCharacter = CHARACTERS[charIndex];
            showRenamingPage(renamedPatch);
          }
          break;
        case DELETE:
          state = PATCH;
          if (patches.size() > 0)
          {
            patchNo = patches.first().patchNo;
            deletePatch(String(patchNo).c_str());
            getPatches(SD.open("/"));
            patchNo = patches.first().patchNo;
            recallPatch(String(patchNo));
            sortPatches();
            getPatches(SD.open("/"));
          }
          state = PARAMETER;
          break;
        case MENU:
          //Choose this option and allow value choice
          menuValueIndex = getCurrentIndex(menuOptions.first().currentIndex);
          showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex], MENUVALUE);
          state = MENUVALUE;
          break;
        case MENUVALUE:
          //Store current menu item and go back to options
          menuHandler(menuOptions.first().value[menuValueIndex], menuOptions.first().handler);
          showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex], MENU);
          state = MENU;
          break;
      }
    }
    else
    {
      recall = false;
    }
  }
}

void reinitialiseToPanel()
{
  //This sets the current patch to be the same as the current hardware panel state - all the pots
  //The four button controls stay the same state
  //This reinialises the previous hardware values to force a re-read
  muxInput = 0;
  for (int i = 0; i < MUXCHANNELS; i++)
  {
    mux1ValuesPrev[i] = -9999;
    mux2ValuesPrev[i] = -9999;
  }
  volumePrevious = -9999;
  patchName = INITPATCHNAME;
  showPatchPage("Initial", "Panel Settings");
}

void checkEncoder()
{
  //Encoder works with relative inc and dec values
  //Detent encoder goes up in 4 steps, hence +/-3

  long encRead = encoder.read();
  if (encRead > encPrevious + 3)
  {
    switch (state)
    {
      case PARAMETER:
        state = PATCH;
        patches.push(patches.shift());
        patchNo = patches.first().patchNo;
        recallPatch(String(patchNo));
        state = PARAMETER;
        break;
      case RECALL:
        patches.push(patches.shift());
        break;
      case SAVE:
        patches.push(patches.shift());
        break;
      case PATCHNAMING:
        if (charIndex == TOTALCHARS)
          charIndex = 0;
        currentCharacter = CHARACTERS[charIndex++];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.push(patches.shift());
        break;
      case MENU:
        menuOptions.push(menuOptions.shift());
        menuValueIndex = getCurrentIndex(menuOptions.first().currentIndex);
        showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex] , MENU);
        break;
      case MENUVALUE:
        if (menuOptions.first().value[menuValueIndex + 1] != '\0')
          showMenuPage(menuOptions.first().option, menuOptions.first().value[++menuValueIndex], MENUVALUE);
        break;
    }
    encPrevious = encRead;
  }
  else if (encRead < encPrevious - 3)
  {
    switch (state)
    {
      case PARAMETER:
        state = PATCH;
        patches.unshift(patches.pop());
        patchNo = patches.first().patchNo;
        recallPatch(String(patchNo));
        state = PARAMETER;
        break;
      case RECALL:
        patches.unshift(patches.pop());
        break;
      case SAVE:
        patches.unshift(patches.pop());
        break;
      case PATCHNAMING:
        if (charIndex == -1)
          charIndex = TOTALCHARS - 1;
        currentCharacter = CHARACTERS[charIndex--];
        showRenamingPage(renamedPatch + currentCharacter);
        break;
      case DELETE:
        patches.unshift(patches.pop());
        break;
      case MENU:
        menuOptions.unshift(menuOptions.pop());
        menuValueIndex = getCurrentIndex(menuOptions.first().currentIndex);
        showMenuPage(menuOptions.first().option, menuOptions.first().value[menuValueIndex], MENU);
        break;
      case MENUVALUE:
        if (menuValueIndex > 0)
          showMenuPage(menuOptions.first().option, menuOptions.first().value[--menuValueIndex], MENUVALUE);
        break;
    }
    encPrevious = encRead;
  }
}

void MIDIMonitor() {
  //Monitor MIDI In DIN
  if (Serial4.available() > 0) {
    Serial.print("MIDI DIN: ");
    Serial.println(Serial4.read(), HEX);
  }
}

void CPUMonitor() {
  Serial.print("CPU:");
  Serial.print(AudioProcessorUsageMax());
  Serial.print("  MEM:");
  Serial.println(AudioMemoryUsageMax());
}


void loop()
{
  myusb.Task();
  midi1.read(midiChannel);   //USB HOST MIDI Class Compliant
  usbMIDI.read(midiChannel); //USB Client MIDI
  MIDI.read(midiChannel);    //MIDI 5 Pin DIN

  checkMux();
  checkVolumePot();
  checkSwitches();
  checkEncoder();

  // CPUMonitor();
}
