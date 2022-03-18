/***********************************************************
 * FZU: This is a FZU version of QCM firmware. Some parts are not changed from the Italien one,
 * because it is only setting up the digital inputs and outputs.
 ***********************************************************/

/******************************* LIBRARIES *****************************/
/** FZU: YOU NEED TO INSTALL BasicLinearAlgebra LIBRARY FROM THE MENU **/
#include <ArduinoJson.h>
#include <BasicLinearAlgebra.h>
#include <Wire.h>
#include "src/Adafruit_MCP9808.h"
#include <ADC.h>
// #include "MedianFilterLib2.h"

/*************************** DEFINE ***************************/
#define FW_NAME "FZU QCM Firmware"
#define FW_VERSION "3.00"
#define FW_DATE "14.01.2022"
#define FW_AUTHOR "Anonymous"

// potentiometer AD5252 I2C address is 0x2C(44)
#define ADDRESS 0x2C
// potentiometer AD5252 default value for compatibility with openQCM Q-1 shield @5VDC
// #define POT_VALUE 240 //254
// reference clock
#define REFCLK 125000000

/*************************** VARIABLE DECLARATION ***************************/
// potentiometer AD5252 default value for compatibility with openQCM Q-1 shield @5VDC
int POT_VALUE = 240; //254
String potval_str = String(POT_VALUE);
// current input frequency
long freq = 0;
// DDS Synthesizer AD9851 pin function
int WCLK = A8;
int DATA = A9;
int FQ_UD = A1;
// frequency tuning word
long FTW;
float temp_FTW; // temporary variable
// phase comparator AD8302 pinout
int AD8302_PHASE = 20;
int AD8302_MAG = 37;
//int AD83202_REF = 17;
int AD83202_REF = 34;

// TODO
double val = 0;

// Create the MCP9808 temperature sensor object
Adafruit_MCP9808 tempsensor = Adafruit_MCP9808();
// init temperature variable
//float temperature = 0;

// LED pin
int ledPin1 = 24;
int ledPin2 = 25;

// ADC init variabl
boolean WAIT = true;
// ADC waiting delay microseconds
// int WAIT_DELAY_US = 300;
int WAIT_DELAY_US = 100;
int AVERAGE_COUNT = 50;

// ADC averaging
boolean AVERAGING = true;
// inint number of averaging
int AVERAGE_SAMPLE = 32;
// teensy ADC averaging init
int ADC_RESOLUTION = 13;

// init output ad8302 measurement (cast to double)
double measure_phase = 0;
double measure_mag = 0;


const int DEFAULT_CALIB_FREQ = 10000000;
const int DEFAULT_RANGE = 65536;

const int SWEEP_STEP = 32;
const int SWEEP_RANGE = 1024;
const int SWEEP_COUNT = SWEEP_RANGE / SWEEP_STEP + 1;
const int SWEEP_REPEAT = 16;

const int DIS_STEP = 256;
const int DIS_RANGE = 16384;
const int DIS_COUNT = DIS_RANGE / DIS_STEP + 1;

BLA::Matrix<3, SWEEP_COUNT> A_dagger;
BLA::Matrix<SWEEP_COUNT, 3> A;
double quad_res;
double res;

StaticJsonDocument<256> fw;
StaticJsonDocument<4096> data;
JsonArray magValues = data.createNestedArray("magnitude");
JsonArray phaseValues = data.createNestedArray("phase");
uint64_t dseq = 0;

/*************************** FUNCTIONS ***************************/

/* AD9851 set frequency fucntion */
void SetFreq(long frequency)
{
  // set to 125 MHz internal clock
  temp_FTW = (frequency * pow(2, 32)) / REFCLK;
  FTW = long(temp_FTW);

  long pointer = 1;
  int pointer2 = 0b10000000;
  // int lastByte = 0b10000000;

  /* 32 bit dds tuning word frequency instructions */
  for (int i = 0; i < 32; i++)
  {
    if ((FTW & pointer) > 0)
      digitalWrite(DATA, HIGH);
    else
      digitalWrite(DATA, LOW);
    digitalWrite(WCLK, HIGH);
    digitalWrite(WCLK, LOW);
    pointer = pointer << 1;
  }

  /* 8 bit dds phase and x6 multiplier refclock*/
  for (int i = 0; i < 8; i++)
  {
    //if ((lastByte & pointer2) > 0) digitalWrite(DATA, HIGH);
    //else digitalWrite(DATA, LOW);
    digitalWrite(DATA, LOW);
    digitalWrite(WCLK, HIGH);
    digitalWrite(WCLK, LOW);
    pointer2 = pointer2 >> 1;
  }

  digitalWrite(FQ_UD, HIGH);
  digitalWrite(FQ_UD, LOW);

  //FTW = 0;
}

/*************************** SETUP ***************************/
void setup()
{
  // Initialise I2C communication as Master
  Wire.begin();
  // Initialise serial communication, set baud rate = 9600
  Serial.begin(115200);

  // set potentiometer value
  // Start I2C transmission
  Wire.beginTransmission(ADDRESS);
  // Send instruction for POT channel-0
  Wire.write(0x01);
  // Input resistance value, 0x80(128)
  Wire.write(POT_VALUE);
  // Stop I2C transmission
  Wire.endTransmission();

  // AD9851 set pin mode
  pinMode(WCLK, OUTPUT);
  pinMode(DATA, OUTPUT);
  pinMode(FQ_UD, OUTPUT);

  // AD9851 enter serial mode
  digitalWrite(WCLK, HIGH);
  digitalWrite(WCLK, LOW);
  digitalWrite(FQ_UD, HIGH);
  digitalWrite(FQ_UD, LOW);

  // AD8302 set pin mode
  pinMode(AD8302_PHASE, INPUT);
  pinMode(AD8302_MAG, INPUT);
  pinMode(AD83202_REF, INPUT);

  // Teensy 3.6 set  adc resolution
  analogReadResolution(ADC_RESOLUTION);

  // begin temperature sensor
  tempsensor.begin();

  // turn on the light
  pinMode(ledPin1, OUTPUT);
  pinMode(ledPin2, OUTPUT);
  digitalWrite(ledPin1, HIGH);
  digitalWrite(ledPin2, HIGH);

  for (int i = 0; i < SWEEP_COUNT; i++)
  {
    A(i, 0) = i * i;
    A(i, 1) = i;
    A(i, 2) = 1;
  }

  A_dagger = ((~A) * A).Inverse() * (~A);

  fw["name"] = FW_NAME;
  fw["version"] = FW_VERSION;
  fw["date"] = FW_DATE;
  fw["author"] = FW_AUTHOR;

  while (!Serial)
  {

    ; // wait for serial port to connect. Needed for native USB port only
  }
}

/**
 * FZU: original Italien code, but optimized for better reading
 */
int legacyRead(String msg)
{
  int d0 = msg.indexOf(';');
  int d1 = msg.indexOf(';', d0 + 1);

  if (d0 == -1 || d1 == -1 || d0 == d1)
  {
    return 1;
  }

  long f0 = msg.substring(0, d0).toInt();
  long f1 = msg.substring(d0 + 1, d1).toInt();
  long f2 = msg.substring(d1 + 1).toInt();

  if (!f0 || !f1 || !f2)
  {
    return 2;
  }

  // 9990000;10010000;40
  double measure_phase = 0;
  double measure_mag = 0;

  for (long f = f0; f <= f1; f += f2)
  {
    SetFreq(f);
    if (WAIT)
      delayMicroseconds(WAIT_DELAY_US);

    int app_phase = 0;
    int app_mag = 0;

    for (int i = 0; i < AVERAGE_SAMPLE; i++)
    {
      app_phase += analogRead(AD8302_PHASE);
      app_mag += analogRead(AD8302_MAG);
    }

    measure_phase = 1.0 * app_phase / AVERAGE_SAMPLE;
    measure_mag = 1.0 * app_mag / AVERAGE_SAMPLE;

    Serial.print(measure_mag);
    Serial.print(";");
    Serial.print(measure_phase);
    Serial.println();
  }

  tempsensor.shutdown_wake(0);
  float temperature = tempsensor.readTempC();

  Serial.print(temperature);
  Serial.print(";");
  Serial.print(POT_VALUE);
  Serial.print(";");
  Serial.println("s");

  return 0;
}

void swap(int *p,int *q) {
   int t;
   
   t=*p; 
   *p=*q; 
   *q=t;
}

void sort(int a[],int n) { 
   int i,j;

   for(i = 0;i < n-1;i++) {
      for(j = 0;j < n-i-1;j++) {
         if(a[j] > a[j+1])
            swap(&a[j],&a[j+1]);
      }
   }
}




double preciseAmpl(long f)
{
  int x = AVERAGE_COUNT;

  SetFreq(f);
  delayMicroseconds(WAIT_DELAY_US);
  
  long double cumsum = 0;
  for (int i = 0; i < x; i++)
  {
    cumsum += analogRead(AD8302_MAG);
    //delayMicroseconds(5);
  }

  return cumsum / (double)x;
}



double precisePhase(long f)
{
  int x = AVERAGE_COUNT;

  SetFreq(f);
  delayMicroseconds(WAIT_DELAY_US);

  double cumsum = 0;
  for (int i = 0; i < x; i++)
  {
    cumsum += analogRead(AD8302_PHASE);
  }

  return cumsum / (double)x;
}
/****************** FZU: HERE THE ORIGINAL FIRMWARE ENDS **************/
// 9999000;10001000;40
/**
 * move in steps towards boundary to the left
 */
long freq_boundary(long rf, double boundary, bool left)
{
  int x = left ? -1 : 1;
  long f = rf;
  int ampl = preciseAmpl(f);
  double b = (double)ampl * boundary;
  int step = 2048;

  // Serial.print("ampl: ");
  // Serial.println((double)ampl);

  // Serial.print("rf: ");
  // Serial.println(rf);

  // Serial.print("Boundary: ");
  // Serial.println((double)ampl * boundary);

  while (step > 3)
  {
    ampl = preciseAmpl(f + (step * x));
    // Serial.print("A: ");
    // Serial.println(ampl);
    // Serial.print("F: ");
    // Serial.println(f + (step * x));
    // delay(500);
    if (ampl > b)
    {
      f += (step * x);
    }
    else
    {
      step /= 2;
    }
  }

  // Serial.print("A: ");
  // Serial.println(ampl);
  // Serial.print("F: ");
  // Serial.println(f);
  return f;
}

const int DIRTY_NUM = 32;
const int DIRTY_RANGE = 16384;

/**
 * FZU: Find "dirty" maximum with really quick algorithm. We start with wide range and big steps. Then
 * we find maximum and start again with smaller interval and smaller steps. Algorithm ends when step is <= 1.
 */
long gradient1(long left_freq, long right_freq)
{
  double step_size = (right_freq - left_freq) / DIRTY_NUM;

  // recursion ends when step is too small
  if (step_size <= 1)
  {
    return left_freq + (DIRTY_NUM / 2) * step_size;
  }

  int a;
  int max_a = 0;
  int max_f = 0;

  // scan interval in given steps
  for (long f = left_freq; f <= right_freq; f += step_size)
  {
    a = preciseAmpl(f);
    // update current maximum
    if (a > max_a)
    {
      max_a = a;
      max_f = f;
    }
  }

  // run recursively again with smaller interval and smaller steps
  return gradient1(max_f -  2* step_size, max_f + 2 * step_size);
}

long gradient2(long left_freq, long right_freq)
{
  double step_size = (right_freq - left_freq) / DIRTY_NUM;

  // recursion ends when step is too small
  if (step_size <= 0.5)
  {
    return left_freq + (DIRTY_NUM / 2) * step_size;
  }

  int a;
  int max_a = 0;
  int max_f = 0;

  // scan interval in given steps
  for (long f = left_freq; f <= right_freq; f += step_size)
  {
    a = precisePhase(f);
    // update current maximum
    if (a > max_a)
    {
      max_a = a;
      max_f = f;
    }
  }

  // run recursively again with smaller interval and smaller steps
  return gradient1(max_f -  2* step_size, max_f + 2 * step_size);
}

long calib_freq = DEFAULT_CALIB_FREQ;

/**
 * FZU: when you know "dirty" resonant frequency, you scan frequencies 
 * around the maximum and from the measured data calculate true resonant
 * frequency using polyfit.
 */
double sweepFrequency(long rf)
{
  // prepare variables
//  String str = String();

  BLA::Matrix<SWEEP_COUNT> b;

  long f = rf - (SWEEP_RANGE / 2);

  // stabilize input, not measures anything
  preciseAmpl(f);

  // sweep around given frequency
  for (int i = 0; i < SWEEP_COUNT; i++, f += SWEEP_STEP)
  { 
    // fill matrices and vectors for polyfit
    // use of preciseAmpl over PreciseAmplMedian is recommeded for 
    b(i) = preciseAmpl(f);

    //cooldown frequency
    preciseAmpl(DEFAULT_CALIB_FREQ-8000 + (DEFAULT_CALIB_FREQ-f)); 
  }

  BLA::Matrix<3> coeffs = A_dagger * b;
  
  quad_res = 0;
  for (int i = 0; i < SWEEP_COUNT; i++)
  { 
     res = b(i) - (coeffs(0)*i*i + coeffs(1)*i + coeffs(2));
     quad_res += res*res;
  }
  quad_res = quad_res/SWEEP_COUNT;
  
  // calculate frequency from indexes
  double result = (0 - coeffs(1) / (2 * coeffs(0))) * (double)SWEEP_STEP + (double)rf - ((double)SWEEP_RANGE / 2);


  calib_freq = (long)result;

  return result;
}

double sweepPhase(long rf)
{
  // not recommended because of phase curve shape - parabola doesn't fit well

  BLA::Matrix<SWEEP_COUNT> b;

  double contraction_factor = 30;
  long f = rf - (SWEEP_RANGE / 4);

  // stabilize input, not measures anything
  preciseAmpl(f);

  // sweep around given frequency
  for (int i = 0; i < SWEEP_COUNT; i++, f += double (SWEEP_STEP/contraction_factor))
  { 
    // fill matrices and vectors for polyfit
    b(i) = precisePhase(f);

    //cooldown frequency
    precisePhase(DEFAULT_CALIB_FREQ-8000 + (DEFAULT_CALIB_FREQ-f)); 
  }

  BLA::Matrix<3> coeffs = A_dagger * b;
  
  quad_res = 0;
  for (int i = 0; i < SWEEP_COUNT; i++)
  { 
     res = b(i) - (coeffs(0)*i*i + coeffs(1)*i + coeffs(2));
     quad_res += res*res;
  }
  quad_res = quad_res/SWEEP_COUNT;
  
  // calculate frequency from indexes
  double result = (0 - coeffs(1) / (2 * coeffs(0))) * (double)SWEEP_STEP + (double)rf - ((double)SWEEP_RANGE / 2);


  calib_freq = (long)result;

  return result;
}

/**
 * FZU: calculate dissipation from given resonant frequency
 */
double dissipation(long rf)
{
  double dis = -1.0;

  // stabilize input
  SetFreq(rf);
  delayMicroseconds(WAIT_DELAY_US * 3);
  analogRead(AD8302_MAG);

  // second stabilization
  SetFreq(rf);
  if (WAIT)
    delayMicroseconds(WAIT_DELAY_US);

  long fl = rf;
  long fr = rf;
  int la = analogRead(AD8302_MAG);
  int ra = la;
  double boundary = (double)la * 0.707;
  int step = 2048;

  // move in steps towards 70.7% boundary for dissipation calc to the left
  while (step > 3 && la > 1000)
  {
    SetFreq(fl - step);
    if (WAIT)
      delayMicroseconds(WAIT_DELAY_US);

    la = analogRead(AD8302_MAG);
    if (la > boundary)
    {
      fl -= step;
    }
    else
    {
      step /= 2;
    }
  }

  // move in steps towards 70.7% boundary for dissipation calc to the right
  step = 2048;
  while (step > 3 && ra > 1000)
  {
    SetFreq(fr + step);
    if (WAIT)
      delayMicroseconds(WAIT_DELAY_US);

    ra = analogRead(AD8302_MAG);
    if (ra > boundary)
    {
      fr += step;
    }
    else
    {
      step /= 2;
    }
  }

  // calculate dissipation (maybe change to quality factor)
  dis = (double)(fr - fl) / (double)rf;
  if (dis * dis > 1)
  {
    dis = -1;
  }
  return dis;
}

/**
 * FZU: only for debugging purposes, don't really look at it.
 */
double sweepDebug(long rf)
{
  String str = String();
  BLA::Matrix<SWEEP_COUNT, 3> A;
  BLA::Matrix<SWEEP_COUNT> b;
  BLA::Matrix<DIS_COUNT> d;

  long f = rf - (DIS_RANGE / 2);
  double cumsum = 0;
  double max = 0;
  double maxf = -1.0;
  double dlf = -1.0;
  double drf = -1.0;
  double dis = -1.0;
  // int maxidx = -1;
  int ldis = -1;
  int rdis = -1;

  SetFreq(f);
  delayMicroseconds(WAIT_DELAY_US * 3);
  analogRead(AD8302_MAG);

  Serial.println(SWEEP_COUNT);
  Serial.println(SWEEP_STEP);

  for (int i = 0; i < DIS_COUNT; i++, f += DIS_STEP)
  {
    SetFreq(f);
    if (WAIT)
      delayMicroseconds(WAIT_DELAY_US);
    cumsum = 0;

    for (int j = 0; j < SWEEP_REPEAT; j++)
    {
      cumsum += analogRead(AD8302_MAG);
    }

    d(i) = cumsum / (double)SWEEP_REPEAT;
    if (d(i) > max)
    {
      max = d(i);
      // maxidx = i;
      maxf = (double)f;
    }
  }

  f = rf - (DIS_RANGE / 2);
  for (int i = 0; i < DIS_COUNT; i++, f += DIS_STEP)
  {
    if (ldis < 0 && d(i) > 0.707 * max)
    {
      ldis = i;
      dlf = (double)f;
    }
    if (ldis > 0 && rdis < 0 && d(i) < 0.707 * max)
    {
      rdis = i - 1;
      drf = (double)f;
      break;
    }
  }
  dis = maxf / (drf - dlf);
  Serial.println(dis);

  f = rf - (SWEEP_RANGE / 2);
  SetFreq(f);
  delayMicroseconds(WAIT_DELAY_US * 3);
  analogRead(AD8302_MAG);

  for (int i = 0; i < SWEEP_COUNT; i++, f += SWEEP_STEP)
  {
    SetFreq(f);
    if (WAIT)
      delayMicroseconds(WAIT_DELAY_US);
    cumsum = 0;

    for (int j = 0; j < SWEEP_REPEAT; j++)
    {
      cumsum += analogRead(AD8302_MAG);
    }

    b(i) = cumsum / (double)SWEEP_REPEAT;
    // A(i, 0) = i * i;
    // A(i, 1) = i;
    // A(i, 2) = 1;

    Serial.println(f);
    Serial.println(b(i));

    // if (b(i) < 3000)
    // {
    //   return -b(i); //-1;
    // }

    str.concat(b(i)).concat(';');
  }

  //  Serial.println(str);

  BLA::Matrix<3> coeffs = A_dagger * b;
  // BLA::Matrix<3> coeffs = ((~A) * A).Inverse() * (~A) * b;
  double result = (0 - coeffs(1) / (2 * coeffs(0))) * (double)SWEEP_STEP + (double)rf - ((double)SWEEP_RANGE / 2);

  Serial.println(coeffs(0));
  Serial.println(coeffs(1));
  Serial.println(coeffs(2));
  //  Serial.println(result);

  calib_freq = (long)result;

  return result;
}

/**
 * FZU: our version of firmware supports multiple commands
 */
int modernRead(String msg)
{
  // long param = msg.substring(2).toInt();
  char cmd = msg.charAt(0);

  long f;
  double rf, dis, x;
  float t;
  int w, e, g;

  double measure_phase = 0;
  double measure_mag = 0;
  int rf_cnt = 128;
  int rf_step = 40;
  long rf_hb = rf_cnt * rf_step / 2;

  switch (cmd)
  {
  
  case 'v': // Viktor's debug
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    
    Serial.println(f);
    
    for (e =-200; e<200; e++){
      g = f+4*e;
      Serial.println(preciseAmpl(g));
      //cooldown frequency
      preciseAmpl(DEFAULT_CALIB_FREQ-8000 + (DEFAULT_CALIB_FREQ-f));
      }
    
  break;
  
  case 'w': // Viktor's debug
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    //f = 10001241;
    Serial.println(f);
    delayMicroseconds(100);
    for (e =-200; e<200; e++){
      SetFreq(f+4*e);
      delayMicroseconds(WAIT_DELAY_US);
        for (w = 0; w< 100; w++){
        Serial.println(analogRead(AD8302_MAG));
          delayMicroseconds(100);
      }
    }
  break;
  
  case 'c': // Viktor's debug
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    rf = sweepFrequency(f);
    delay(150);
    Serial.println(rf);
    tempsensor.shutdown_wake(0);
    t = tempsensor.readTempC();
    Serial.println(t);
    break;

  case 'j': // only frequency measurement for Jakub 'case' algorithm
    //rf = gradient1(calib_freq - DIRTY_RANGE, calib_freq + DIRTY_RANGE);
    x = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    
    rf = sweepFrequency(x);
    Serial.println(rf);
    //dis = dissipation(rf);
    //Serial.println(dis, 9);
    //tempsensor.shutdown_wake(0);
    //t = tempsensor.readTempC();
    //Serial.println(t);
    break;

  case 'm': // main measurement
    // f = gradient1(calib_freq - DIRTY_RANGE, calib_freq + DIRTY_RANGE);
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    rf = sweepFrequency(f);
    Serial.println(rf);
    dis = dissipation(rf);
    Serial.println(dis, 9);
    tempsensor.shutdown_wake(0);
    t = tempsensor.readTempC();
    Serial.println(t);
    break;

  case 'n': // main measurement to json
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    rf = sweepFrequency(f);
    data["id"] = ++dseq;
    data["frequency"] = rf;
    dis = dissipation(rf);
    data["dissipation"] = dis;
    tempsensor.shutdown_wake(0);
    t = tempsensor.readTempC();
    data["temperature"] = t;
    serializeJsonPretty(data,Serial);
    Serial.println();
    break;

  case 'r': // main measurement with resonance curves to json
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    rf = sweepFrequency(f);
    data["id"] = ++dseq;
    data["frequency"] = rf;
    dis = dissipation(rf);
    data["dissipation"] = dis;
    tempsensor.shutdown_wake(0);
    t = tempsensor.readTempC();
    data["temperature"] = t;
    for (long f = rf - rf_hb; f <= rf + rf_hb; f += rf_step)
    {
      SetFreq(f);
      if (WAIT)
        delayMicroseconds(WAIT_DELAY_US);
      int app_phase = 0;
      int app_mag = 0;
      for (int i = 0; i < AVERAGE_SAMPLE; i++)
      {
        app_phase += analogRead(AD8302_PHASE);
        app_mag += analogRead(AD8302_MAG);
      }
      measure_phase = 1.0 * app_phase / AVERAGE_SAMPLE;
      measure_mag = 1.0 * app_mag / AVERAGE_SAMPLE;
      magValues.add(measure_mag);
      phaseValues.add(measure_phase);
    }
    serializeJsonPretty(data,Serial);
    Serial.println();
    magValues.clear();
    phaseValues.clear();
    break;

  case 'i':
    serializeJsonPretty(fw,Serial);
    Serial.println();
    break;

  case 't':
    Serial.println("test");   
    break;

  case 'M': // only for debug
    // f = gradient1(calib_freq - DIRTY_RANGE, calib_freq + DIRTY_RANGE);
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    rf = sweepFrequency(f);
    Serial.println(rf);
    dis = dissipation(rf);
    Serial.println(dis, 9);
    tempsensor.shutdown_wake(0);
    t = tempsensor.readTempC();
    Serial.println(t);
    Serial.println("s");
    break;

  case 'D': // only for debug
    // f = gradient1(calib_freq - DIRTY_RANGE, calib_freq + DIRTY_RANGE);
    f = gradient1(DEFAULT_CALIB_FREQ - DIRTY_RANGE, DEFAULT_CALIB_FREQ + DIRTY_RANGE);
    // rf = sweepFrequency(f);
    Serial.println(calib_freq - DIRTY_RANGE);
    Serial.println(calib_freq + DIRTY_RANGE);
    Serial.println(f);
    rf = sweepDebug(f);
    Serial.println(rf);
    Serial.println(WAIT_DELAY_US);
    Serial.println(AVERAGE_SAMPLE);
    tempsensor.shutdown_wake(0);
    t = tempsensor.readTempC();
    Serial.println(t);
    Serial.println("s");
    break;
    
  case 'R':
    potval_str = msg.substring(1);
    if (potval_str.toInt() >= 0 && potval_str.toInt() < 256)
    {
      POT_VALUE = potval_str.toInt();
      Wire.beginTransmission(ADDRESS);
      Wire.write(0x01);
      Wire.write(POT_VALUE);
      Wire.endTransmission();
    }
    break;
    
  default:
    return 1;
  }

  return 0;
}

int message = 0;
boolean debug = true;
long pre_time = 0;
long last_time = 0;

int byteAtPort = 0;

/*************************** LOOP ***************************/
void loop()
{
  if (!(Serial.available() > 0))
  {
    return;
  }

  String msg = Serial.readStringUntil('\n');
  bool useModern = msg.indexOf(';') == -1;

  int retVal;
  if (useModern)
    {
    retVal = modernRead(msg);
  }
  else
  {
    retVal = legacyRead(msg);
  }

  // Serial.println("Modern: " + String(useModern));
  // Serial.println("RetVal: " + String(retVal));
}