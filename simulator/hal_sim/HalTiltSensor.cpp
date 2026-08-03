// Simulator implementation of HalTiltSensor: no IMU on the host; reports
// unavailable, exactly like an X4 without the sensor.
#include <HalTiltSensor.h>

HalTiltSensor halTiltSensor;

void HalTiltSensor::begin() { _available = false; }
bool HalTiltSensor::wake() { return false; }
bool HalTiltSensor::deepSleep() { return true; }
void HalTiltSensor::update(uint8_t, uint8_t, bool) {}
bool HalTiltSensor::wasTiltedForward() { return false; }
bool HalTiltSensor::wasTiltedBack() { return false; }
bool HalTiltSensor::hadActivity() { return false; }
void HalTiltSensor::clearPendingEvents() {}
