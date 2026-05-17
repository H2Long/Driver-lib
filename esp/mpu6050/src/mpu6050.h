//
// Created by hhlong on 2026/4/26.
//

#ifndef PROJECT_MPU6050_H
#define PROJECT_MPU6050_H


#include "system.h"
#define MPU6050_SMPLRT_DIV      0x19
#define MPU6050_CONFIG          0x1A
#define MPU6050_GYRO_CONFIG     0x1B
#define MPU6050_ACCEL_CONFIG    0x1C

#define MPU6050_ACCEL_XOUT_H    0x3B
#define MPU6050_ACCEL_XOUT_L    0x3C
#define MPU6050_ACCEL_YOUT_H    0x3D
#define MPU6050_ACCEL_YOUT_L    0x3E
#define MPU6050_ACCEL_ZOUT_H    0x3F
#define MPU6050_ACCEL_ZOUT_L    0x40
#define MPU6050_TEMP_OUT_H      0x41
#define MPU6050_TEMP_OUT_L      0x42
#define MPU6050_GYRO_XOUT_H     0x43
#define MPU6050_GYRO_XOUT_L     0x44
#define MPU6050_GYRO_YOUT_H     0x45
#define MPU6050_GYRO_YOUT_L     0x46
#define MPU6050_GYRO_ZOUT_H     0x47
#define MPU6050_GYRO_ZOUT_L     0x48

#define MPU6050_PWR_MGMT_1      0x6B
#define MPU6050_PWR_MGMT_2      0x6C
#define MPU6050_WHO_AM_I        0x75

typedef struct {
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
    float temp;
    float roll;
    float pitch;
    float yaw;
    float accel_magnitude;  // sqrt(ax² + ay² + az²), g
} mpu6050_data_t;

typedef struct {
    hal_i2c_config_t i2c_config;
    mpu6050_data_t mpu6050_data;
    mpu6050_data_t correct;
} mpu6050_handle_t;

void bsp_mpu6050_write_byte(mpu6050_handle_t  mpu6050_handle,uint8_t reg_address,uint8_t data);
uint8_t bsp_mpu6050_read_byte(mpu6050_handle_t  mpu6050_handle,uint8_t reg_address);
mpu6050_handle_t bsp_mpu6050_init(mpu6050_handle_t* mpu_handle, hal_i2c_config_t i2c_config);
void bsp_mpu6050_read_raw(mpu6050_handle_t* mpu_handle);
void bsp_mpu6050_update_angle(mpu6050_handle_t* mpu_handle);
void bsp_mpu6050_calibrate(mpu6050_handle_t* mpu_handle);

#endif //PROJECT_MPU6050_H
