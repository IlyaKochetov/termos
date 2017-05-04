/*
    Снимаем температуру с DHT 18s20 и управляем реле
    Простейший темрорегулятор, предназначенный для управления температурой, например воды в бассейне, или масляного радиатора и т.д.
    Использует Arduino R3, DS18b20 (лучше водозащищенный), реле,
    LCD shield
*/
#define   RELAY1          3   //порт реле
#define   ONE_WIRE_BUS    11  //на каком пине датчик температуры
#define   ONE_WIRE_PWR    12  //на каком пине питание датчика температуры (используем internal pull-up)
#define   A_BUTTON        0   //аналоговый пин кнопки
/*
    Указываем, к каким пинам Arduino подключены выводы дисплея:
     RS, E, DB4, DB5, DB6, DB7
*/
#define   LCD_RS          8   //(Data or Signal Display Selection)
#define   LCD_E           9   //Enable
#define   LCD_BACK        10  //LCD backlit
#define   LCD_DB4          4
#define   LCD_DB5          5
#define   LCD_DB6          6
#define   LCD_DB7          7

#define   CODE_NAME     "Termos"
#define   CODE_VERSION  3     //версия программы
#define   serial_speed  9600  //скорость сериального выхода
#define   CYCLE_SPEED   1000  //задержка цикла

int lcd_key     = 0;
int adc_key_in  = 0;
#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5

#include  <Logging.h>         //библиотека для удобной работы с сериальным выводом
#define   LOGLEVEL LOG_LEVEL_DEBUG    //NOOUTPUT,ERRORS,INFOS,DEBUG,VERBOSE

//DS18b20 temperature reading
#include  <DallasTemperature.h>
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
*/
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_DB4, LCD_DB5, LCD_DB6, LCD_DB7);

//температурные константы
#define TARGET_TEMP       25  //сколько хотим держать по-умолчанию
#define TARGET_TEMP_MAX   65  //максимум для целевой температуры (актуально для пластиковых труб, чтобы не было больше 70)
#define TARGET_TEMP_MIN   5   //сколько хотим держать по-умолчанию (сколько можно поставить по-миниумуму)
#define TARGET_TEMP_STEP  1   //шаг изменения при нажатии кнопки
#define TARGET_TEMP_JUMP  10  //шаг изменения при нажатии кнопки

#define TEMP_DELTA        2  //на сколько можем охладится прежде чем включать заново

//статусы прибора
#define STATE_OFF         0  //покой
#define STATE_UP          1  //греем
#define STATE_DOWN        2  //охлаждаем

#define MODE_TARGET       0   //режим выбора целевой темпертуры;

float temp1_real        = 0;  //для реальной темпертуры сенсора
int temp1               = 0;  //она же в виде int

int target_temp         = TARGET_TEMP;   //целевая температура
int relay1_state        = STATE_OFF;    //статус прибора
int select_mode         = MODE_TARGET;  //что мы выбираем кнопками


void setup() {
  // put your setup code here, to run once:

  //стартуем лог
  Log.Init(LOGLEVEL, serial_speed);
  Log.Info("Starting %s v%d"CR, CODE_NAME, CODE_VERSION);

  //инициализуем Ds18b20
  setupTempSensors();

  // Инициализуем дисплей, показываем надпись
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("STARTING");
  lcd.setCursor(0, 1);
  lcd.print(CODE_NAME);
  lcd.print(" v");
  lcd.print(CODE_VERSION);

  //инициализуем реле
  //pinMode(RELAY1, OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:

  lcd_key = read_LCD_buttons();  //читаем статус кнопок шилда
  if (lcd_key == btnSELECT) {
     select_mode   =  selectMode(select_mode);  //выбор вида изменений
     showSelectMode(select_mode);
     return;
  }
  if (select_mode == MODE_TARGET) {
     target_temp    = selectTargetTemp(lcd_key, target_temp);
  }

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
 * показ режима выбора
 */
void showSelectMode(int select_mode)  {
  lcd.setCursor(0, 0);
  lcd.print("Select target   ");
  lcd.setCursor(0, 1);
  lcd.print("temperature     ");  
}

/*
   показ статусной строки
*/
void showStatus(float target, int state) {
  lcd.setCursor(0, 1);
  lcd.print("TARG:");         //+5 = 5
  lcd.print((int)target);     //+2 = 7
  lcd.print("C");             //+1 = 8
  lcd.print(" ST:");          //+4 = 12
  if (state == STATE_OFF)  {
    lcd.print("OFF ");        //+4 = 16
  } else if (state == STATE_DOWN) {
    lcd.print("COOL");        //4+ = 16
  } else if (state == STATE_UP) {
    lcd.print("HEAT");        //4+ = 16
  } else  {
    lcd.print("????");        //4+ = 16
  }
}

/*
   показ температуры
*/
void showTemp(float temp) {
  lcd.setCursor(0, 0);  //верхняя строка
  //ds18b20 вернет -127 при обрыве или другой ошибке
  if (temp != -127) {
    lcd.print("TEMP:");     //+5
    lcd.print(temp);        //+5 = 10
    lcd.print("C     ");    //+6 = 16
  } else  {
    lcd.print("TEMP SENSOR FAIL");  //16
  }
}

/*
   проверка датчиков и возврат адреса первого
*/
void  setupTempSensors()  {
  pinMode(ONE_WIRE_PWR, OUTPUT); // set power pin for DS18B20 to output
  digitalWrite(ONE_WIRE_PWR, HIGH); // turn sensor power on
  delay(500);
  // Start up the library
  sensors.begin();
  if (!sensors.getAddress(mainSensor, 0)) Log.Error("Unable to find address for the main sensor"CR);
}

float getTemp(DeviceAddress sensorAddress) {
  sensors.requestTemperatures(); // Send the command to get temperatures
  float tempC = sensors.getTempC(sensorAddress);
  return tempC;
}

int relayStart(int relay) {
  digitalWrite(relay, HIGH);
  return STATE_UP;
}

int relayStop(int relay) {
  digitalWrite(relay, LOW);
  return STATE_DOWN;
}

int selectMode(int mode) {
  Log.Info("ONLY ONE MODE CURENTLY SUPPORTED. COME BACK LATER");
  if (mode == MODE_TARGET) {
  }
  return mode;
}

int selectTargetTemp(int key, int temp) {
    if (key == btnUP)     temp += TARGET_TEMP_STEP;
    if (key == btnDOWN)   temp -= TARGET_TEMP_STEP;
    if (key == btnLEFT)   temp -= TARGET_TEMP_JUMP;
    if (key == btnRIGHT)  temp += TARGET_TEMP_JUMP;
    temp = (temp > TARGET_TEMP_MAX) ? TARGET_TEMP_MAX : temp;
    temp = (temp < TARGET_TEMP_MIN) ? TARGET_TEMP_MIN : temp;
    return temp;
}

// read the buttons
int read_LCD_buttons()  {
  adc_key_in = analogRead(A_BUTTON);      // read the value from the sensor
  if (adc_key_in > 1000) return btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
  // For V1.1 us this threshold
  if (adc_key_in < 50)   return btnRIGHT;
  if (adc_key_in < 250)  return btnUP;
  if (adc_key_in < 450)  return btnDOWN;
  if (adc_key_in < 650)  return btnLEFT;
  if (adc_key_in < 850)  return btnSELECT;
  return btnNONE;  // when all others fail, return this...
}
