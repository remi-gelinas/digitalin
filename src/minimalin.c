#include <pebble.h>
#include "config.h"
#include "text_block.h"
#include "messenger.h"
#include "minimalin.h"

// #define d(string, ...) APP_LOG (APP_LOG_LEVEL_DEBUG, string, ##__VA_ARGS__)
// #define e(string, ...) APP_LOG (APP_LOG_LEVEL_ERROR, string, ##__VA_ARGS__)
// #define i(string, ...) APP_LOG (APP_LOG_LEVEL_INFO, string, ##__VA_ARGS__)
static GPoint WEATHER_INFO_CENTER = { .x = 30, .y = 63 };
static GPoint DATE_INFO_CENTER = { .x = 30, .y = 21 };
static GPoint TIME_INFO_CENTER = {.x = 110, .y = 70 };
static GPoint BATTERY_INFO_CENTER = {.x = 30, .y = 105 };
static GPoint HEALTH_INFO_CENTER = {.x = 30, .y = 147 };

typedef enum {
  AppKeyDateDisplayed = 0,
  AppKeyBluetoothIcon,
  AppKeyBackgroundColor,
  AppKeyDateColor,
  AppKeyTimeColor,
  AppKeyInfoColor,
  AppKeyTemperatureUnit,
  AppKeyRefreshRate,
  AppKeyWeatherEnabled,
  AppKeyConfig,
  AppKeyWeatherTemperature,
  AppKeyWeatherIcon,
  AppKeyWeatherFailed,
  AppKeyWeatherRequest,
  AppKeyJsReady,
  AppKeyVibrateOnTheHour,
  AppKeyMilitaryTime
} AppKey;

typedef enum {
  PersistKeyConfig = 0,
  PersistKeyWeather
} PersistKey;

typedef struct {
  int hour;
  int minute;
  int day;
} Time;

typedef struct {
  int32_t timestamp;
  int8_t icon;
  int8_t temperature;
} __attribute__((__packed__)) Weather;

static Window * s_main_window;
static Layer * s_root_layer;
static GRect s_root_layer_bounds;
static GPoint s_center;

static TextBlock * s_weather_info;
static TextBlock * s_date_info;
static TextBlock * s_battery_info;
static TextBlock * s_health_info;
static TextBlock * s_time_text;

static Layer * s_tick_layer;

static Config * s_config;
static Messenger * s_messenger;
static Weather s_weather;

static bool s_bt_connected;
static bool s_health_enabled;

static AppTimer * s_weather_request_timer;
static int s_weather_request_timeout;

static int s_js_ready;

static GFont s_font;
static GFont s_font_big;

static tm * s_current_time;

static void schedule_weather_request(int timeout);
static void update_time();
static void update_date();

static void update_bt();
static void update_weather();
static void update_battery();

static void update_current_time() {
  const time_t temp = time(NULL);
  s_current_time = localtime(&temp);
}

static void send_weather_request_callback(void * context){
  s_weather_request_timer = NULL;
  const int timeout = config_get_int(s_config, ConfigKeyRefreshRate) * 60;
  const int expiration =  s_weather.timestamp + timeout;
  const bool almost_expired = time(NULL) > expiration;
  const bool can_update_weather = almost_expired && s_js_ready;
  if(can_update_weather){
    if(config_get_bool(s_config, ConfigKeyWeatherEnabled)){
      DictionaryIterator *out_iter;
      AppMessageResult result = app_message_outbox_begin(&out_iter);
      if(result == APP_MSG_OK) {
        const int value = 1;
        dict_write_int(out_iter, AppKeyWeatherRequest, &value, sizeof(int), true);
        result = app_message_outbox_send();
        if(result != APP_MSG_OK) {
          schedule_weather_request(5000);
        }
      } else {
        schedule_weather_request(5000);
      }
    }
  }
}

static void schedule_weather_request(int timeout){
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Scheduling weather request");
  int expiration = time(NULL) + timeout;
  if(s_weather_request_timer){
    if(expiration < s_weather_request_timeout){
      s_weather_request_timeout = expiration;
      app_timer_reschedule(s_weather_request_timer, timeout);
    }
  }else{
    s_weather_request_timeout = expiration;
    s_weather_request_timer = app_timer_register(timeout, send_weather_request_callback, NULL);
  }
}

static void config_info_color_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_int(s_config, ConfigKeyInfoColor, tuple->value->int32);
  update_bt();
  update_weather();
  update_battery();
}

static void config_background_color_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_int(s_config, ConfigKeyBackgroundColor, tuple->value->int32);
  window_set_background_color(s_main_window, config_get_color(s_config, ConfigKeyBackgroundColor));
}

static void config_time_color_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_int(s_config, ConfigKeyTimeColor, tuple->value->int32);
  layer_mark_dirty(s_tick_layer);
  update_time();
}

static void config_date_color_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_int(s_config, ConfigKeyDateColor, tuple->value->int32);
  update_date();
}

static void config_refresh_rate_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_int(s_config, ConfigKeyRefreshRate, tuple->value->int32);
}

static void config_temperature_unit_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_int(s_config, ConfigKeyTemperatureUnit, tuple->value->int32);
  update_weather();
}

static void config_bluetooth_icon_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_int(s_config, ConfigKeyBluetoothIcon, tuple->value->int32);
  update_bt();
}

static void config_date_displayed_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_bool(s_config, ConfigKeyDateDisplayed, tuple->value->int8);
  text_block_set_visible(s_date_info, tuple->value->int8);
}

static void config_weather_enabled_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_bool(s_config, ConfigKeyWeatherEnabled, tuple->value->int8);
  update_weather();
}

static void config_hourly_vibrate_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_bool(s_config, ConfigKeyVibrateOnTheHour, tuple->value->int8);
}

static void config_military_time_updated(DictionaryIterator * iter, Tuple * tuple){
  config_set_bool(s_config, ConfigKeyMilitaryTime, tuple->value->int8);
  layer_mark_dirty(s_tick_layer);
  update_time();
}

static void js_ready_callback(DictionaryIterator * iter, Tuple * tuple){
  s_js_ready = true;
  schedule_weather_request(0);
}

static void weather_requested_callback(DictionaryIterator * iter, Tuple * tuple){
  Tuple * icon_tuple = dict_find(iter, AppKeyWeatherIcon);;
  Tuple * temp_tuple = dict_find(iter, AppKeyWeatherTemperature);;
  if(icon_tuple && temp_tuple){
    s_weather.timestamp = time(NULL);
    s_weather.icon = icon_tuple->value->int8;
    s_weather.temperature = temp_tuple->value->int8;
  }
  persist_write_data(PersistKeyWeather, &s_weather, sizeof(Weather));
  update_weather();
}

static void messenger_callback(DictionaryIterator * iter){
  if(dict_find(iter, AppKeyConfig)){
    config_save(s_config, PersistKeyConfig);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Setting timestamp to 0!");
    s_weather.timestamp = 0;
    schedule_weather_request(0);
  }
  layer_mark_dirty(s_root_layer);
}

static void update_time(){
  GColor color = config_get_color(s_config, ConfigKeyTimeColor);
  char buffer[] = "00:00";
  int h = s_current_time->tm_hour;
  if(!config_get_bool(s_config, ConfigKeyMilitaryTime)){
    if (h > 12) {
      h -= 12;
    }else if(h == 0){
      h = 12;
    }
  }
  
  snprintf(buffer, sizeof(buffer), "%d:%02d", h, s_current_time->tm_min);
  text_block_set_text(s_time_text, buffer, color);
  
  text_block_move(s_time_text, TIME_INFO_CENTER);
}

static void update_date(){
  if(config_get_bool(s_config, ConfigKeyDateDisplayed)){
    const GColor date_color = config_get_color(s_config, ConfigKeyDateColor);
    char buffer[] = "00";
    snprintf(buffer, sizeof(buffer), "%d", s_current_time->tm_mday);
    text_block_set_text(s_date_info, buffer, date_color);
  }
}

static void update_bt(){
  char bt_buffer[10] = {0};
  const BluetoothIcon new_icon = config_get_int(s_config, ConfigKeyBluetoothIcon);
  
  if(!s_bt_connected){
    if(new_icon == Bluetooth){
      strncat(bt_buffer, "z", 2);
    }else if(new_icon == Heart){
      strncat(bt_buffer, "Z", 2);
    }
  }
}

static void update_weather(){
  char weather_buffer[10] = {0};

  const int timeout = (config_get_int(s_config, ConfigKeyRefreshRate) + 5) * 60;
  
  const int expiration =  s_weather.timestamp + timeout;
  const bool weather_valid = time(NULL) < expiration;
  
  if(weather_valid && config_get_bool(s_config, ConfigKeyWeatherEnabled)){
    int temp = s_weather.temperature;
    if(config_get_int(s_config, ConfigKeyTemperatureUnit) == Fahrenheit){
      temp = temp * 9 / 5 + 32;
    }
    char temp_buffer[6];
    snprintf(temp_buffer, 6, "%c%d°", s_weather.icon, temp);
    strcat(weather_buffer, temp_buffer);
  }
  const GColor info_color = config_get_color(s_config, ConfigKeyInfoColor);
  text_block_set_text(s_weather_info, weather_buffer, info_color);
}

static void update_battery(BatteryChargeState state) {
  const GColor color = config_get_color(s_config, ConfigKeyInfoColor);
  char buffer[10];
  
  snprintf(buffer, sizeof(buffer), "%d%%", state.charge_percent);
  text_block_set_text(s_battery_info, buffer, color);
}

static void update_health(HealthEventType e, void *context) {
  if (s_health_enabled) {
    const GColor color = config_get_color(s_config, ConfigKeyInfoColor);
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%d", (int)health_service_sum_today(HealthMetricStepCount));
    
    text_block_set_text(s_health_info, buffer, color);
  }
}

static void bt_handler(bool connected){
  s_bt_connected = connected;
  update_bt();
  update_weather();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed){
  if(HOUR_UNIT & units_changed){
    HealthActivity activity = health_service_peek_current_activities();
    if(activity == HealthActivityNone){
      bool vibrate_on_the_hour = config_get_bool(s_config, ConfigKeyVibrateOnTheHour);
      if(vibrate_on_the_hour){
        vibes_short_pulse();
      }
    }
  }
  schedule_weather_request(10000);
  update_current_time();
  update_time();
  if(DAY_UNIT & units_changed){
    update_date();
  }
  layer_mark_dirty(s_tick_layer);
  update_bt();
  update_weather();
}

static void main_window_load(Window *window) {
  s_root_layer = window_get_root_layer(window);
  s_root_layer_bounds = layer_get_bounds(s_root_layer);
  s_center = grect_center_point(&s_root_layer_bounds);
  update_current_time();
  window_set_background_color(window, config_get_color(s_config, ConfigKeyBackgroundColor));


  s_date_info = text_block_create(s_root_layer, DATE_INFO_CENTER, s_font);
  text_block_set_visible(s_date_info, config_get_bool(s_config, ConfigKeyDateDisplayed));

  s_weather_info = text_block_create(s_root_layer, WEATHER_INFO_CENTER, s_font);
  s_battery_info = text_block_create(s_root_layer, BATTERY_INFO_CENTER, s_font);
  s_health_info = text_block_create(s_root_layer, HEALTH_INFO_CENTER, s_font);
  bluetooth_connection_service_subscribe(bt_handler);
  bt_handler(connection_service_peek_pebble_app_connection());
  
  s_time_text = text_block_create(s_root_layer, TIME_INFO_CENTER, s_font_big);

  s_tick_layer = layer_create(s_root_layer_bounds);
  layer_add_child(s_root_layer, s_tick_layer);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(update_battery);
  s_health_enabled = health_service_events_subscribe(update_health, NULL);
  
  
  update_current_time();
  update_time();
  update_date();
  update_battery(battery_state_service_peek());
  
  Message messages[] = {
    { AppKeyJsReady, js_ready_callback },
    { AppKeyBackgroundColor, config_background_color_updated },
    { AppKeyDateColor, config_date_color_updated },
    { AppKeyInfoColor, config_info_color_updated },
    { AppKeyTimeColor, config_time_color_updated },
    { AppKeyDateDisplayed, config_date_displayed_updated },
    { AppKeyBluetoothIcon, config_bluetooth_icon_updated },
    { AppKeyRefreshRate, config_refresh_rate_updated },
    { AppKeyTemperatureUnit, config_temperature_unit_updated },
    { AppKeyWeatherEnabled, config_weather_enabled_updated },
    { AppKeyWeatherTemperature, weather_requested_callback },
    { AppKeyVibrateOnTheHour, config_hourly_vibrate_updated },
    { AppKeyMilitaryTime, config_military_time_updated }
  };
  s_messenger = messenger_create(13, messenger_callback, messages);
}

static void main_window_unload(Window *window) {
  text_block_destroy(s_time_text);

  layer_destroy(s_tick_layer);

  bluetooth_connection_service_unsubscribe();
  text_block_destroy(s_date_info);
  text_block_destroy(s_weather_info);
}

static void init() {
  s_weather_request_timeout = 0;
  s_js_ready = false;
  s_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_NUPE_23));
  s_font_big = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_NUPE_35));
  s_config = config_load(PersistKeyConfig, CONF_SIZE, CONF_DEFAULTS);
  if(persist_exists(PersistKeyWeather)){
    persist_read_data(PersistKeyWeather, &s_weather, sizeof(Weather));
  }
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
      .load = main_window_load,
      .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);
}

static void deinit() {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  health_service_events_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_main_window);
  config_destroy(s_config);
  fonts_unload_custom_font(s_font);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
