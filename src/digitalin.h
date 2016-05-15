#pragma once

typedef enum { NoIcon = 0, Bluetooth , Heart } BluetoothIcon;
typedef enum { Celsius = 0, Fahrenheit } TemperatureUnit;

typedef enum {
  ConfigKeyBackgroundColor = 0,
  ConfigKeyTimeColor,
  ConfigKeyInfoColor,
  ConfigKeyRefreshRate,
  ConfigKeyTemperatureUnit,
  ConfigKeyMilitaryTime,
} ConfigKey;

#define CONF_SIZE 14

#if defined(PBL_COLOR)
ConfValue CONF_DEFAULTS[CONF_SIZE] = {
  { .key = ConfigKeyBackgroundColor, .type = ColorConf, .value = { .integer = 0x000000 } },
  { .key = ConfigKeyTimeColor, .type = ColorConf, .value = { .integer = 0xaaaaaa } },
  { .key = ConfigKeyInfoColor, .type = ColorConf, .value = { .integer = 0x555555 } },
  { .key = ConfigKeyTemperatureUnit, .type = IntConf, .value = { .integer = Celsius } },
  { .key = ConfigKeyRefreshRate, .type = IntConf, .value = { .integer = 20 } },
  { .key = ConfigKeyMilitaryTime, .type = BoolConf, .value = { .boolean = false } }
};
#elif defined(PBL_BW)
ConfValue CONF_DEFAULTS[CONF_SIZE] = {
  { .key = ConfigKeyBackgroundColor, .type = ColorConf, .value = { .integer = 0x000000 } },
  { .key = ConfigKeyTimeColor, .type = ColorConf, .value = { .integer = 0xffffff } },
  { .key = ConfigKeyInfoColor, .type = ColorConf, .value = { .integer = 0xffffff } },
  { .key = ConfigKeyTemperatureUnit, .type = IntConf, .value = { .integer = Celsius } },
  { .key = ConfigKeyRefreshRate, .type = IntConf, .value = { .integer = 20 } },
  { .key = ConfigKeyMilitaryTime, .type = BoolConf, .value = { .boolean = false } }
};
#endif
