/*
    Снимаем температуру с DHT 18s20 и управляем реле
    Простейший темрорегулятор, предназначенный для управления температурой, например воды в бассейне, или масляного радиатора и т.д.
    Использует Arduino R3, DS18b20 (лучше водозащищенный), реле,
    HD44780-совместимый LCD экран и кнопку (для управления целевой температурой)
*/
#define   CODE_NAME     "Termos"
#define   CODE_VERSION  2     //версия программы
#define   RELAY1        6     //порт реле
#define   serial_speed  9600  //скорость сериального выхода
#define   CYCLE_SPEED   2000  //задержка цикла

#include  <Logging.h>         //библиотека для удобной работы с сериальным выводом
#define   LOGLEVEL LOG_LEVEL_DEBUG    //NOOUTPUT,ERRORS,INFOS,DEBUG,VERBOSE

//DS18b20 temperature reading
#include  <DallasTemperature.h>
#define ONE_WIRE_BUS 9          // на каком пине датчик температуры
#define TEMPERATURE_PRECISION 8 //точность измерения

// Библиотека для работы с DS118b20 по OneWire
#include <OneWire.h>
OneWire oneWire(ONE_WIRE_BUS);
// Инициализируем Dallas Temperature.
DallasTemperature sensors(&oneWire);
DeviceAddress mainSensor;     //адрес датчика температуры, для расширения программы другими датчиками

#include <LiquidCrystal.h>
/* Создаём объект LCD-дисплея, используя конструктор класса LiquidCrystal
  с 6ю аргументами. Библиотека по количеству аргументов сама определит,
  что нужно использовать 4-битный интерфейс.
  Указываем, к каким пинам Arduino подключены выводы дисплея:
    RS, E, DB4, DB5, DB6, DB7
*/
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

//температурные константы
#define TARGET_TEMP  25 //сколько хотим держать по-умолчанию
#define TEMP_DELTA   2  //на сколько можем охладится прежде чем включать заново

//статусы прибора
#define STATE_OFF    0  //покой
#define STATE_UP     1  //греем
#define STATE_DOWN   2  //охлаждаем

float temp1_real;       //для реальной темпертуры сенсора
int temp1;              //она же в виде int

int target_temp;        //целевая температура
int relay1_state;       //статус прибора

void setup() {
  // put your setup code here, to run once:

  //стартуем лог
  Log.Init(LOGLEVEL, serial_speed);
  Log.Info("Starting %s v%d"CR, CODE_NAME, CODE_VERSION);

  //инициализуем Ds18b20
  digitalWrite( ONE_WIRE_BUS , LOW );
  pinMode( ONE_WIRE_BUS  , OUTPUT );
  setupTempSensors();

  // Инициализуем дисплей, показываем надпись
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("STARTING");
  lcd.setCursor(0, 1);
  lcd.print(CODE_NAME);
  lcd.print("v");
  lcd.print(CODE_VERSION);

  //инициализуем реле
  pinMode(RELAY1, OUTPUT);
  relay1_state = STATE_OFF;

  target_temp = TARGET_TEMP;
}

void loop() {
  // put your main code here, to run repeatedly:
  
  delay(CYCLE_SPEED);
  temp1_real = getTemp(mainSensor);
  temp1     = (int)temp1_real;
  showTemp(temp1_real);

  if (temp1 != -127) {
    int delta = target_temp - TEMP_DELTA;

    if (temp1 >= target_temp)  {
      Log.Info("Stopping relay as temp (%d) reached %d"CR, temp1, target_temp);
      relay1_state = relayStop(RELAY1);
    }

    if (temp1 < delta)  {
      Log.Info("Starting relay as temp (%d) reached %d - %d"CR, temp1, target_temp, TEMP_DELTA);
      relay1_state = relayStart(RELAY1);
    }
    
    if (relay1_state == STATE_UP) {
      Log.Info("Waiting for as temp (%d) to reach %d"CR, temp1, target_temp);
    } else if (relay1_state == STATE_DOWN) {
      Log.Info("Cooling until temp (%d) falls to %d"CR, temp1, delta);
      if (temp1 < delta)  {
        relay1_state = STATE_OFF;
      }
    } else if  (relay1_state == STATE_OFF)  {
      Log.Info("Idling at %d (target %d)"CR, temp1, target_temp);
      if (temp1 < target_temp) {
        relay1_state = relayStart(RELAY1);
      }
    } else  {
      Log.Info("Wrong state %d, temp %d target %d"CR, relay1_state, temp1, target_temp);
    }
  } else  {
    //relay1_state = relayStart(RELAY1);
  }

  showStatus(target_temp, relay1_state);
}

/*
 * показ статусной строки
 */
void showStatus(float target, int state) {
  lcd.setCursor(0, 1);
  lcd.print("TARG:"); //+3 = 3
  lcd.print((int)target);  //+4 = 7
  lcd.print("C");    //+1 = 8
  lcd.print(" ST:");   //+4 = 12
  if (state == STATE_OFF)  {
    lcd.print("OFF "); //+3 = 15
  } else if (state == STATE_DOWN) {
    lcd.print("COOL"); //4+ = 16
  } else if (state == STATE_UP) {
    lcd.print("HEAT"); //4+ = 16
  } else  {
    lcd.print("???"); //4+ = 16
  }
}

/*
 * показ температуры
 */
void showTemp(float temp) {
  lcd.setCursor(0, 0);  //верхняя строка
  //ds18b20 вернет -127 при обрыве или другой ошибке
  if (temp != -127) {
    lcd.print("TEMP:");  //+8
    lcd.print(temp);        //+4 = 12
    lcd.print("C");         //+1 = 13
  } else  {
    lcd.print("TEMP SENSOR FAIL");
  }
}

/*
 * проверка датчиков и возврат адреса первого
 */
void  setupTempSensors()  {
  sensors.begin();
  if (!sensors.getAddress(mainSensor, 0)) Log.Error("Unable to find address for the main sensor"CR);
}

float getTemp(DeviceAddress sensorAddress) {
  sensors.requestTemperatures(); // Send the command to get temperatures
  float tempC = sensors.getTempC(sensorAddress);
  return tempC;
}

int relayStart(int relay) {
  Log.Info("Starting relay %d"CR, relay);
  digitalWrite(relay, HIGH);
  return STATE_UP;
}

int relayStop(int relay) {
  Log.Info("Stopping relay %d"CR, relay);
  digitalWrite(relay, LOW);
  return STATE_DOWN;
}

