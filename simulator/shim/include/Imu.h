#pragma once
// Simulator shim: no IMU on the host — begin() fails so tilt page turn is
// reported unavailable, same as an X4 without the sensor.
namespace freeink {

class Imu {
 public:
  struct Sample {
    float gx = 0, gy = 0, gz = 0;
    float ax = 0, ay = 0, az = 0;
  };
  bool begin() { return false; }
  bool read(Sample&) const { return false; }
  bool sleep() { return true; }
  bool wake() { return true; }
};

}  // namespace freeink

using Imu = freeink::Imu;
