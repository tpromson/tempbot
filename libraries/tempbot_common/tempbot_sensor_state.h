#ifndef TEMPBOT_SENSOR_STATE_H
#define TEMPBOT_SENSOR_STATE_H

// Arduino-independent sensor status transition shared by firmware and host
// tests. An invalid sensor reading always takes precedence over Wi-Fi state.
enum TempBotSensorStatusTransition {
  TEMPBOT_SENSOR_STATUS_KEEP,
  TEMPBOT_SENSOR_STATUS_ERROR,
  TEMPBOT_SENSOR_STATUS_CONNECTED
};

inline bool tempbotDs18b20ReadingValid(float temperature,
                                       float disconnectedValue = -127.0f) {
  return temperature != disconnectedValue && temperature >= -55.0f;
}

inline TempBotSensorStatusTransition tempbotSensorStatusTransition(
    bool sensorReadingValid, bool wifiConnected, bool statusCanRecover) {
  if (!sensorReadingValid) return TEMPBOT_SENSOR_STATUS_ERROR;
  if (wifiConnected && statusCanRecover) return TEMPBOT_SENSOR_STATUS_CONNECTED;
  return TEMPBOT_SENSOR_STATUS_KEEP;
}

#endif
