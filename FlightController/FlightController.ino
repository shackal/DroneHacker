/*
* DroneHacker (c) 2015 Anibal Gomez
* ---------------------------------
* Software Controlador De Vuelo
* ---------------------------------
*
* Quadcoptero
* -----------
*
*             - Eje Y -
*
*                RX
*            TL      TR
*            |\     /|
*            ( \   / )
*             \ (_) / 
*              ) _ (   - Eje X -
*             / ( ) \ 
*            ( /   \ )
*            |/     \|
*            BL     BR
*       
*
* CONTRIBUCIONES
* ==============
* 
* - Si desea contribuir con el programa, por favor:
* - No comente ninguna linea de codigo
* - Envie su documentacion externa, en la carpeta, /docs/
* - Use estandares de convencion
* - Use variables lo mas descriptivas posibles
* - Use DocBlocks en la parte superior de cada metodo
*
* ---------------------------------
* anibalgomez@icloud.com
* ---------------------------------
*
* LICENSE
*
* This source file is subject to the new BSD license that is bundled
* with this package in the file LICENSE.txt.
*
**/

#include <PinChangeInt.h>
#include <Servo.h>
#include "I2Cdev.h"

#include "MPU6050_6Axis_MotionApps20.h"
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

MPU6050 mpu;

bool dmpReady = false;
uint8_t mpuIntStatus;
uint8_t devStatus;
uint16_t packetSize;
uint16_t fifoCount;
uint8_t fifoBuffer[64];

Quaternion q;
VectorInt16 aa;
VectorInt16 aaReal;
VectorInt16 aaWorld;
VectorFloat gravity;
float euler[3];
float ypr[3];

int yaw, pitch, roll;

#define THROTTLE_IN_PIN 5

#define MOTORTL_OUT_PIN 8
#define MOTORTR_OUT_PIN 9
#define MOTORBR_OUT_PIN 11
#define MOTORBL_OUT_PIN 13

Servo servoMotorTL;
Servo servoMotorTR;
Servo servoMotorBR;
Servo servoMotorBL;

#define THROTTLE_FLAG 1

volatile uint8_t bUpdateFlagsShared;

volatile uint16_t unThrottleInShared;

uint32_t ulThrottleStart;

volatile bool mpuInterrupt = false;
void dmpDataReady() {
    mpuInterrupt = true;
}

void setup(){
  
    #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
        Wire.begin();
        TWBR = 24;
    #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
        Fastwire::setup(400, true);
    #endif
    
  Serial.begin(115200);
  Serial.println("multiChannels");

  servoMotorTL.attach(MOTORTL_OUT_PIN);
  servoMotorTR.attach(MOTORTR_OUT_PIN);
  servoMotorBL.attach(MOTORBL_OUT_PIN);
  servoMotorBR.attach(MOTORBR_OUT_PIN);

  PCintPort::attachInterrupt(THROTTLE_IN_PIN, calcThrottle, CHANGE); 
  
  //arm();
  
  mpu.initialize();
  Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));
  devStatus = mpu.dmpInitialize();
  mpu.setXGyroOffset(220);
  mpu.setYGyroOffset(76);
  mpu.setZGyroOffset(-85);
  mpu.setZAccelOffset(1788);
  
  if (devStatus == 0) {
        mpu.setDMPEnabled(true);
        attachInterrupt(0, dmpDataReady, RISING);
        mpuIntStatus = mpu.getIntStatus();
        dmpReady = true;
        packetSize = mpu.dmpGetFIFOPacketSize();
    } else {
        Serial.print(F("DMP Initialization failed (code "));
        Serial.print(devStatus);
        Serial.println(F(")"));
    }

}

void loop() {
  
  static uint16_t unThrottleIn;

  static uint8_t bUpdateFlags;

  if (!dmpReady) return;
    while (!mpuInterrupt && fifoCount < packetSize) {
    }
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();
    fifoCount = mpu.getFIFOCount();
    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
        mpu.resetFIFO();
    } else if (mpuIntStatus & 0x02) {
        while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
        mpu.getFIFOBytes(fifoBuffer, packetSize);
        fifoCount -= packetSize;
    }

  if(bUpdateFlagsShared) {
    
    noInterrupts();
    bUpdateFlags = bUpdateFlagsShared;
    if(bUpdateFlags & THROTTLE_FLAG) {
      unThrottleIn = unThrottleInShared;
    }
    bUpdateFlagsShared = 0;
    interrupts();
    
  }
  
  if(bUpdateFlags & THROTTLE_FLAG) {
    if(servoMotorTL.readMicroseconds() && servoMotorTR.readMicroseconds() 
        && servoMotorBL.readMicroseconds() && servoMotorBR.readMicroseconds()
        != unThrottleIn) {
          Serial.println(unThrottleIn);
          stabilized(unThrottleIn);
    }
  }
  
  bUpdateFlags = 0;
}

void calcThrottle() {
  if(digitalRead(THROTTLE_IN_PIN) == HIGH) { 
    ulThrottleStart = micros();
  } else{
    unThrottleInShared = (uint16_t)(micros() - ulThrottleStart);
    bUpdateFlagsShared |= THROTTLE_FLAG;
  }
}

void arm() {
  servoMotorTL.writeMicroseconds(900);
  servoMotorTR.writeMicroseconds(900);
  servoMotorBL.writeMicroseconds(900);
  servoMotorBR.writeMicroseconds(900);
}

void stabilized(int unThrottleIn) {
  
  int tl, tr, bl, br;
  
  tl = unThrottleIn;
  tr = unThrottleIn;
  bl = unThrottleIn;
  br = unThrottleIn;
  
  mpu.dmpGetQuaternion(&q, fifoBuffer);
  mpu.dmpGetGravity(&gravity, &q);
  mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
  
  yaw = ypr[0] * 180/M_PI;
  pitch = ypr[1] * 180/M_PI;
  roll = ypr[2] * 180/M_PI;
  
  if(unThrottleIn > 1060) {
  
  if(pitch > 3) {
    tl = tl + 100;
    bl = bl + 100;
  }
  
  if(pitch < -3) {
    tr = tr + 100;
    br = br + 100;
  }
  
  if(roll > 3) {
    bl = bl + 100;
    br = br + 100;
  }
  
  if(roll < -3) {
    tl = tl + 100;
    tr = tr + 100;
  }
  
  }
  
  //Serial.println("Valor a motor");
  //Serial.println(tl);
  //Serial.println(tr);
  //Serial.println(bl);
  //Serial.println(br);
  
  servoMotorTL.writeMicroseconds(tl);
  servoMotorTR.writeMicroseconds(tr);
  servoMotorBL.writeMicroseconds(bl);
  servoMotorBR.writeMicroseconds(br);
  
}

