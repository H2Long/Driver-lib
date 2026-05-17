//
// Created by lenovo on 2026/2/21.
//

#include "mpu6050.h"


#define MPU6050_ADD_W 0xD0
#define MPU6050_ADD_R 0xD1
#define  dt 0.01

void bsp_mpu6050_write_byte(mpu6050_handle_t mpu_handle,uint8_t reg_address,uint8_t data) {
    hal_i2c_write_reg(mpu_handle.i2c_config,MPU6050_ADD_W,reg_address,data);
}

uint8_t bsp_mpu6050_read_byte(mpu6050_handle_t mpu_handle,uint8_t reg_address) {
    return hal_i2c_read_reg(mpu_handle.i2c_config,MPU6050_ADD_W,MPU6050_ADD_R,reg_address);
}

mpu6050_handle_t bsp_mpu6050_init(mpu6050_handle_t* mpu_handle, hal_i2c_config_t i2c_config)
{
    mpu_handle->i2c_config = hal_i2c_init(i2c_config); 								//使用调用者传入的I2C配置初始化底层I2C

    /*MPU6050寄存器初始化，需要对照MPU6050手册的寄存器描述配置，此处仅配置了部分重要的寄存器*/
    bsp_mpu6050_write_byte(*mpu_handle,MPU6050_PWR_MGMT_1, 0x01);		//电源管理寄存器1，取消睡眠模式，选择时钟源为X轴陀螺仪
    bsp_mpu6050_write_byte(*mpu_handle,MPU6050_PWR_MGMT_2, 0x00);		//电源管理寄存器2，保持默认值0，所有轴均不待机
    bsp_mpu6050_write_byte(*mpu_handle,MPU6050_SMPLRT_DIV, 0x00);		//采样率分频寄存器，配置采样率
    bsp_mpu6050_write_byte(*mpu_handle,MPU6050_CONFIG, 0x00);			//配置寄存器，配置DLPF
    bsp_mpu6050_write_byte(*mpu_handle,MPU6050_GYRO_CONFIG, 0x00);	//陀螺仪配置寄存器，选择满量程为±250°/s
    bsp_mpu6050_write_byte(*mpu_handle,MPU6050_ACCEL_CONFIG, 0x00);	//加速度计配置寄存器，选择满量程为±16g

    return *mpu_handle;
}

void bsp_mpu6050_read_raw(mpu6050_handle_t* mpu_handle)
{
    uint8_t DataH, DataL;
    int16_t raw_data;

    // 读取加速度计X轴数据
    DataH = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_ACCEL_XOUT_H);
    DataL = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_ACCEL_XOUT_L);
    raw_data = (DataH << 8) | DataL;
    mpu_handle->mpu6050_data.ax = (float)raw_data / 16384.0f;  //

    // 读取加速度计Y轴数据
    DataH = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_ACCEL_YOUT_H);
    DataL = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_ACCEL_YOUT_L);
    raw_data = (DataH << 8) | DataL;
    mpu_handle->mpu6050_data.ay = (float)raw_data /  16384.0f;

    // 读取加速度计Z轴数据
    DataH = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_ACCEL_ZOUT_H);
    DataL = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_ACCEL_ZOUT_L);
    raw_data = (DataH << 8) | DataL;
    mpu_handle->mpu6050_data.az = (float)raw_data /  16384.0f;

    // 读取陀螺仪X轴数据
    DataH = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_GYRO_XOUT_H);
    DataL = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_GYRO_XOUT_L);
    raw_data = (DataH << 8) | DataL;
    mpu_handle->mpu6050_data.gx = (float)raw_data / 131.0f;  // ±2000°/s: 2000/32768 = 0.061

    // 读取陀螺仪Y轴数据
    DataH = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_GYRO_YOUT_H);
    DataL = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_GYRO_YOUT_L);
    raw_data = (DataH << 8) | DataL;
    mpu_handle->mpu6050_data.gy = (float)raw_data / 131.0f;

    // 读取陀螺仪Z轴数据
    DataH = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_GYRO_ZOUT_H);
    DataL = bsp_mpu6050_read_byte(*mpu_handle,MPU6050_GYRO_ZOUT_L);
    raw_data = (DataH << 8) | DataL;
    mpu_handle->mpu6050_data.gz = (float)raw_data / 131.0f;

    mpu_handle->mpu6050_data.accel_magnitude = sqrtf(
        mpu_handle->mpu6050_data.ax * mpu_handle->mpu6050_data.ax +
        mpu_handle->mpu6050_data.ay * mpu_handle->mpu6050_data.ay +
        mpu_handle->mpu6050_data.az * mpu_handle->mpu6050_data.az);
}

void bsp_mpu6050_calibrate(mpu6050_handle_t* mpu_handle) {
    mpu6050_data_t sum = {0};
    for (uint8_t i = 0; i < 25 ; i++ ) {
        bsp_mpu6050_read_raw(mpu_handle);
        sum.ax += mpu_handle->mpu6050_data.ax;
        sum.ay += mpu_handle->mpu6050_data.ay;
        sum.az += mpu_handle->mpu6050_data.az;
        sum.gx += mpu_handle->mpu6050_data.gx;
        sum.gy += mpu_handle->mpu6050_data.gy;
        sum.gz += mpu_handle->mpu6050_data.gz;
    }
    mpu_handle->correct.ax = sum.ax / 25;
    mpu_handle->correct.ay = sum.ay / 25;
    mpu_handle->correct.az = sum.az / 25;
    mpu_handle->correct.gx = sum.gx / 25;
    mpu_handle->correct.gy = sum.gy / 25;
    mpu_handle->correct.gz = sum.gz / 25;
}

void bsp_mpu6050_update_angle(mpu6050_handle_t* mpu_handle) {
    bsp_mpu6050_read_raw(mpu_handle);

    mpu6050_data_t* data = &mpu_handle->mpu6050_data;
    mpu6050_data_t* correct = &mpu_handle->correct;

    float roll_accel = atan2(data->ay, sqrt(data->ax * data->ax + data->az * data->az)) * 180.0f / M_PI;
    float pitch_accel = atan2(-data->ax, sqrt(data->ay * data->ay + data->az * data->az)) * 180.0f / M_PI;

    data->roll  += dt * data->gx;
    data->pitch += dt * data->gy;

    data->ax -= correct->ax;
    data->ay -= correct->ay;
    data->az -= correct->az;
    data->gx -= correct->gx;
    data->gy -= correct->gy;
    data->gz -= correct->gz;

    data->roll = 0.1f * roll_accel + 0.9f * data->roll;
    data->pitch = 0.1f * pitch_accel + 0.9f * data->pitch;
}

