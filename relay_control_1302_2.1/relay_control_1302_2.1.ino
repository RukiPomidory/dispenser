#include <U8g2lib.h>        // Дисплей
#include <Keypad.h>         // 4*4 клавиатура
#include <HX711.h>          // Тензодатчик
#include <EEPROM.h>         // Долговременная память
#include <iarduino_RTC.h>   // Часы реального времени

// Структура управления реле
struct Relay
{
    Relay(int pin) 
    { 
        this->pin = pin; 
        enabled = false;
        isReady = false;
        startTime = 0;
        stopTime = 0;
    }

    // Время включения реле по внутреннему таймеру ардуино
    unsigned long startTime;
    // Аналогично время выключения
    unsigned long stopTime;

    // Состояние - активировано ли сейчас реле
    bool enabled;
    // isReady = true говорит о том, 
    // что реле еще только готовится включиться или уже включено
    bool isReady;

    // manual = true - реле управляется вручную (3 режим)
    bool manual;

    // Непосредственно пин, к которому подключено реле
    int pin;
};


/*
 *  Задержки между реле при включении и выключении
 *  В миллисекундах
 */
const unsigned long turnOnDelay = 200;    // Вкл
const unsigned long turnOffDelay = 100;    // Выкл

/*
 *  Нужна ли диагностика реле при старте программы
 *  
 *  relayTestRequired = true;   - диагностика включена
 *  relayTestRequired = false;  - диагностика отключена
 */
const bool relayTestRequired = false;



#define DEBUG

#ifdef  DEBUG
#   define  D_INIT() { Serial.begin(115200); while(!Serial); }   
#   define  D(x)    { Serial.print(x); }
#   define  D_LN(x) { Serial.println(x); } 
#else
#   define  D_INIT()
#   define  D(x)
#   define  D_LN(x)
#endif  // #ifdef  DEBUG


// Размеры клавиатуры
#define ROWS 4
#define COLS 4

// Пищалка
#define BUZZER 3

// Реле
#define RELAY1  31
#define RELAY2  32
#define RELAY3  33
#define RELAY4  34
#define RELAY5  35
#define RELAY6  36
#define RELAY7  37
#define RELAY8  38

// Контакты тензодатчика
#define SCALE_DT 49
#define SCALE_CLK 48

// Массив объектов всех реле
Relay relays[8] = 
{
    Relay(RELAY1),
    Relay(RELAY2),
    Relay(RELAY3),
    Relay(RELAY4),
    Relay(RELAY5),
    Relay(RELAY6),
    Relay(RELAY7),
    Relay(RELAY8)
};

// Список реле для каждого режима
int mode1Relays[4] = {0, 1, 2, 6};
int mode2Relays[4] = {0, 1, 2, 6};
int mode3Relays[4] = {0, 3, 4, 6};
int mode4Relays[4] = {0, 1, 5, 7};


// Время последнего запуска
unsigned long start;

// Время последней отрисовки
unsigned long lastRedraw;

// Интервал времени обязательной отрисовки в миллисекундах.
// При бездействии системы дисплей будет обновляться с этой периодичностью,
// чтобы "шапка" с датой и временем обновлялась
const unsigned long redrawInterval = 10000;

// Номер текущего окна
int currentWindowId = -1;
// -1 - Автоматический или неавтоматический режим
// 0 - Выбор режима (1-4)
// 1 - выбор времени в режиме 1 и 2
// 2 - режим ручного управления
// 3 - выбор работы с весами
// 4 - калибровка весов
// 5 - работа с весами
// 6 - непосредственно работа с режимом 1 или 2

// Режим от 1 до 4.
// Если mode == 0, все отключено
int mode = 0;

// Текущий этап калибровки
int calibrationStep = 0;

// Введенная пользователем масса груза
long userMass = 0;

// Масса эталонного груза при калибровке по факту
long actualCalibrationMass = 0;

// Время, которое вводит пользователь в 1 и 2 режиме
int userTimer = 0;

// Нужна ли перерисовка интерфейса
bool redrawRequired = true;

// Работает ли сейчас система
bool running = false;

// Количество оставшегося времени в предыдущей итерации
// Нужно для оптимизации вывода оставшегося времени на дисплей
int prevTimeLeft = 0;

// Параметр калибровки тензодатчика
float scaleParameter;

// Коэффициент сглаживания
const float scaleAlpha = 1;

// Предыдущее значение весов
float prevScaleValue = 0;

// Идет ли установка значения веса в рабочем 4 режиме
bool weightSetting = false;

// Последние индексы пикселей дисплея
const int width = 127;
const int height = 63;

// Параметры клавиатуры
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
}; 
byte rowPins[ROWS] = {11,10, 9, 8}; 
byte colPins[COLS] = {7, 6, 5, 4}; 

// Клавиатура 4x4
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Создаём объект u8g2 для работы с дисплеем, указывая номер вывода CS для аппаратной шины SPI
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, 10);

// Объект весов на тензодатчике и преобразователе hx711
HX711 scale;

// Часы реального времени. Модель и подключаемые пины
// модель, RST, CLK, DAT
iarduino_RTC rtc(RTC_DS1302, 22, 23, 24);

// Вывод времени в шапку
void printDataTime()
{
    u8g2.setFont(u8g2_font_profont11_tn);
    u8g2.print(rtc.gettime("H:i  d.m.y"));
    
} // void printDataTime()

// Интеграция экспоненциального сглаживания в измерение веса
long getScaleValue()
{
    float value = scale.get_units(1);
    prevScaleValue = prevScaleValue + scaleAlpha * (value - prevScaleValue);
    return (long) prevScaleValue;
    
} // long getScaleValue()

// Возвращает оставшееся время для 1 и 2 режима
int getTimeLeft()
{
    unsigned long lapsed = millis() - start;
    lapsed /= 1000; // Убираем миллисекунды
    return userTimer * 60 - lapsed;
    
} // int getTimeLeft()

// Звук положительного подкрепления
void toneDone()
{
    tone(BUZZER, 1000, 100);
    delay(200);
    tone(BUZZER, 1000, 100);
    delay(200);
    tone(BUZZER, 1000, 100);
    
} // void toneDone()

// Вывод шапки на дисплей
void drawCap()
{
    u8g2.setFont(u8g2_font_profont11_tn);
    u8g2.setCursor(36, 7);
    printDataTime();
    u8g2.drawLine(0, 8, width, 8);
    
} // void drawCap()

// Вывод иконки режима на дисплей
void drawModeIcon()
{
    // Иконка "режим"
    u8g2.drawRFrame(72, 14, 50, 23, 5);
    u8g2.setFont(u8g2_font_8x13_t_cyrillic);
    u8g2.setCursor(77, 28);
    u8g2.print("Режим");

    // Номер режима поверх нижней стороны
    u8g2.setFont(u8g2_font_t0_16_me);
    u8g2.setCursor(92, 41);
    u8g2.print(mode);
    
} // void drawModeIcon()

// Вывод элемента контроля веса на дисплей
void drawWeightView(int x, int y, long value, int padding=5)
{
    u8g2.setFont(u8g2_font_6x12_me);
    String visualValue;

    if (value < 100000000 && value > -10000000)
    {
        visualValue = String(value);
    }
    else
    {
        visualValue = "unknown";
    }

    int length = visualValue.length();
    int offset = length * 7 / 2;

    u8g2.setCursor(x + padding + 1 - offset, y + 9 + padding);
    u8g2.print(visualValue);

    offset = max(offset, 21);
    u8g2.drawRFrame(x - offset, y, padding * 2 - 5 + offset * 2, 11 + padding * 2, 6);

    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic);
    u8g2.setDrawColor(0);
    u8g2.drawLine(x - 14 + padding, y + 10 + padding * 2, x + 13 + padding, y + 10 + padding * 2);
    u8g2.setDrawColor(1);
    u8g2.setCursor(x - 13 + padding, y + 13 + padding * 2);
    u8g2.print("грамм");
    
} // void drawWeightView(int x, int y, long value, int padding=5)

// Вывод иконок реле на дисплей
/*
 *  int x - координата x угла
 *  int y - координата y угла
 *  int mode - текущий режим, для которого отображаются реле
 */
void drawRelayIcons(int x, int y, int mode)
{   
    for (int i = 0; i < 4; i++)
    {
        bool selected;
        int number;
        
        if (3 == mode)
        {
            selected = relays[mode3Relays[i]].enabled;
            number = mode3Relays[i];
        }
        else if (4 == mode)
        {
            selected = relays[mode4Relays[i]].enabled;
            number = mode4Relays[i];
        }
        else
        {
            return;
        }
        
        u8g2.setDrawColor(1);

        int currentX = x + i*12;
        
        if (selected)
        {
            u8g2.drawDisc(currentX, y - 4, 5, U8G2_DRAW_ALL);
            u8g2.setDrawColor(0);
        }
        else 
        {
            u8g2.drawCircle(currentX, y - 4, 5, U8G2_DRAW_ALL);
        }

        currentX -= 2;

        number += 1;

        // Смещаем расположение цифры 1, 
        // потому что изначально она стоит криво
        // из-за толщины символа в 2 пикселя
        if (1 == number)
        {
            currentX++;
        }
            
        u8g2.setCursor(currentX, y);
        u8g2.print(number);
    }
    u8g2.setDrawColor(1);
    
} // void drawRelayIcons(int x, int y, int mode)

// Окно приветствия
void drawEnterDialogWindow()
{
    u8g2.clearBuffer();
    
    drawCap();
    u8g2.drawRFrame(4, 14, 118, 20, 10);
    u8g2.drawRFrame(4, 14, 118, 21, 10);
    u8g2.setFont(u8g2_font_8x13_t_cyrillic);
    u8g2.setCursor(8, 28);
    u8g2.print("Выберите режим");

    u8g2.setCursor(20, 56);
    u8g2.print("Авто");
    u8g2.setCursor(76, 56);
    u8g2.print("Ручной");

    u8g2.setFont(u8g2_font_profont17_tf);
    u8g2.setCursor(7, 57);
    u8g2.print("A");
    u8g2.setCursor(62, 57);
    u8g2.print("B");

    u8g2.drawRFrame(2, 42, 52, 19, 8);
    u8g2.drawRFrame(2, 42, 52, 20, 8);
    u8g2.drawRFrame(57, 42, 70, 19, 8);
    u8g2.drawRFrame(57, 42, 70, 20, 8);
    u8g2.drawCircle(10, 51, 9);
    u8g2.drawCircle(65, 51, 9);

    u8g2.sendBuffer();
    
} // void drawEnterDialogWindow()

// Окно выбора режима 1-4
void drawStartWindow()
{
    u8g2.clearBuffer();
    
    drawCap();
    u8g2.setFont(u8g2_font_profont12_tf);
    
    u8g2.setCursor(12, 20);
    u8g2.print("1|A");
    u8g2.setCursor(12, 34);
    u8g2.print("2|B");
    u8g2.setCursor(12, 48);
    u8g2.print("3|C");
    u8g2.setCursor(12, 62);
    u8g2.print("4|D");

    u8g2.drawRFrame(8, 10, 25, 12, 5);
    u8g2.drawRFrame(8, 24, 25, 12, 5);
    u8g2.drawRFrame(8, 38, 25, 12, 5);
    u8g2.drawRFrame(8, 52, 25, 12, 5);

    
    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic);
    u8g2.setCursor(36, 20);
    u8g2.print("Таймер 1 .. 130 мин.");
    u8g2.setCursor(36, 34);
    u8g2.print("Таймер 1 .. 35 мин.");
    u8g2.setCursor(36, 48);
    u8g2.print("Ручной режим");
    u8g2.setCursor(36, 62);
    u8g2.print("Управление весом");
    
    u8g2.sendBuffer();
    
} // void drawStartWindow()

// Вывод оставшегося времени в режимах 1-2
void printUserTimer()
{
    int len = 1;
    for (int i = userTimer; i > 9; i /= 10)
    {
        len++;
    }

    u8g2.setFont(u8g2_font_9x15_tf);
    u8g2.setCursor(40 - (int) (len * 4.5), 57);
    u8g2.print(userTimer);
    
} // void printUserTimer()

// Окно выбора времени для режимов 1-2
void drawSelectTimeWindow(byte mode)
{
    u8g2.clearBuffer();
    
    drawCap();
    drawModeIcon();

    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic );
    u8g2.setCursor(14, 19);
    u8g2.print("Укажите");
    u8g2.setCursor(4, 27);
    u8g2.print("Время работы");

    
    // Временные рамки
    if (1 == mode)
    {
        u8g2.setCursor(6, 38);
        u8g2.print("(1 .. 130 мин.)");
    }
    else if (2 == mode)
    {
        u8g2.setCursor(7, 38);
        u8g2.print("(1 .. 35 мин.)");
    }

    
    u8g2.setCursor(86, 59);
    if (1 == mode) u8g2.print('A');
    else if (2 == mode) u8g2.print('B');
    u8g2.drawRFrame(82, 49, 13, 13, 3);
    u8g2.setCursor(98, 58);
    u8g2.print("далее");

    u8g2.drawRFrame(15, 42, 50, 20, 3);
    
    printUserTimer();

    u8g2.sendBuffer();
    
} // void drawSelectTimeWindow(byte mode)

// Окно контроля за происходящим в режимах 1-2
void drawControlWindow(byte mode)
{
    u8g2.clearBuffer();
    
    drawCap();
    drawModeIcon();

    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic);
    u8g2.setCursor(3, 20);
    u8g2.print("Время работы:");
    

    int offset = 0;
    
    
    int timeRemain = getTimeLeft();
    if (timeRemain >= 6000)
    {
        offset = 8;
    }

    u8g2.setCursor(14 - offset / 2, 38);
    if (timeRemain >= 0)
    {
        u8g2.drawRFrame(10 - offset / 2, 25, 48 + offset, 17, 4);
        u8g2.setFont(u8g2_font_t0_16_me);
        
        int minutes = timeRemain / 60;
        int seconds = timeRemain % 60;
    
        if (minutes < 10) u8g2.print('0');
        u8g2.print(minutes);
        u8g2.print(':');
        if (seconds < 10) u8g2.print('0');
        u8g2.print(seconds);
    }
    else if (!running)
    {
        u8g2.drawRFrame(7, 25, 56, 17, 4);
        u8g2.setCursor(10, 37);
        u8g2.print("Завершено.");

        u8g2.setCursor(86, 59);
        u8g2.print('#');
        u8g2.drawRFrame(82, 49, 13, 13, 3);
        u8g2.setCursor(98, 58);
        u8g2.print("выход");

    }
    else
    {
        if (prevTimeLeft >= 0)
        {
            tone(BUZZER, 600, 100);
        }
        
        u8g2.setCursor(4, 36);
        u8g2.print("Завершение");
        int count = abs(timeRemain) % 4;
        for (int i = 0; i < count; i++)
        {
            u8g2.print('.');
        }
    }

    u8g2.sendBuffer();
    
} // void drawControlWindow(byte mode)

// Окно работы 3 режима
void drawManualWindow()
{
    u8g2.clearBuffer();
    
    drawCap();
    drawModeIcon();

    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic);
    u8g2.setCursor(4, 20);

    if (running)
    {
        u8g2.print("Для остановки");
    }
    else
    {
        u8g2.print("Для запуска");
    }
    
    u8g2.setCursor(4, 32);
    u8g2.print("нажмите");
    u8g2.setCursor(54, 32);
    u8g2.print("C");
    u8g2.drawRFrame(50, 22, 13, 13, 3);


    u8g2.setCursor(25, 56);
    u8g2.print("Реле");
    drawRelayIcons(65, 56, 3);

    u8g2.sendBuffer();
    
} // void drawManualWindow()

// Окно приветствия 3 режима
void drawCalibrationQuestion()
{
    u8g2.clearBuffer();
    drawCap();
    drawModeIcon();

    long value = getScaleValue();
    drawWeightView(34, 16, value);

    D("Значение тензодатчика: ");
    D_LN(value);

    u8g2.setCursor(16, 56);
    u8g2.print("калибровка");
    u8g2.drawRFrame(1, 46, 69, 15, 6);
    u8g2.drawDisc(8, 53, 5, U8G2_DRAW_ALL);
    u8g2.drawCircle(8, 53, 7, U8G2_DRAW_UPPER_LEFT);
    u8g2.drawCircle(8, 53, 7, U8G2_DRAW_LOWER_LEFT);
    
    
    u8g2.setCursor(88, 56);
    u8g2.print("работа");
    u8g2.drawRFrame(73, 46, 50, 15, 6);
    u8g2.drawDisc(80, 53, 5, U8G2_DRAW_ALL);
    u8g2.drawCircle(80, 53, 7, U8G2_DRAW_UPPER_LEFT);
    u8g2.drawCircle(80, 53, 7, U8G2_DRAW_LOWER_LEFT);

    u8g2.setDrawColor(0);
    u8g2.setCursor(6, 57);
    u8g2.print("C");
    u8g2.setCursor(78, 57);
    u8g2.print("D");
    u8g2.setDrawColor(1);

    u8g2.sendBuffer();
    
} // void drawCalibrationQuestion()

// Окно выбора нагрузки в 3 режиме
void drawScaleWindow()
{
    u8g2.clearBuffer();
    drawCap();

    drawModeIcon();

    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic);

    if (weightSetting)
    {
        u8g2.setCursor(16, 17);
        u8g2.print("Введите");
        u8g2.setCursor(5, 26);
        u8g2.print("необходимую");
        u8g2.setCursor(6, 35);
        u8g2.print("массу груза:");
    
        drawWeightView(34, 40, userMass, 3);
    }
    else
    {
        long current = getScaleValue();
        
        drawWeightView(31, 41, userMass, 4);
        drawWeightView(94, 41, current, 4);

        u8g2.setCursor(8, 39);
        u8g2.print("Задано:");

        
        drawRelayIcons(12, 26, 4);
    }
    
    u8g2.sendBuffer();
} // void drawScaleWindow()

// Окно калибровки тензодатчика
void drawCalibrationWindow()
{
    u8g2.clearBuffer();
    drawCap();

    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic);
    
    u8g2.setCursor(1, 17);
    u8g2.print("Калибровка");

    if (0 == calibrationStep)
    {
        u8g2.setCursor(25, 35);
        u8g2.print("Освободите весы,");

        u8g2.setCursor(20, 49);
        u8g2.print("затем нажмите");
        u8g2.setCursor(96, 50);
        u8g2.print("D");
    }
    else if (1 == calibrationStep)
    {
        u8g2.setCursor(28, 32);
        u8g2.print("Положите груз");

        u8g2.setCursor(25, 43);
        u8g2.print("известной массы,");
        
        u8g2.setCursor(20, 54);
        u8g2.print("затем нажмите");
        u8g2.setCursor(96, 55);
        u8g2.print("D");
    }
    else if (2 == calibrationStep)
    {
        u8g2.setCursor(20, 32);
        u8g2.print("Введите массу груза:");

        u8g2.setCursor(30, 46);
        u8g2.print(userMass);
    }

    u8g2.sendBuffer();
    
} // void drawCalibrationWindow()

// Окно "в разработке"
void drawUnderConstruction()
{
    u8g2.clearBuffer();

    u8g2.drawRFrame(10, 10, 107, 43, 16);
    u8g2.drawRFrame(8, 8, 111, 47, 12);
    u8g2.drawRFrame(6, 6, 115, 51, 8);

    u8g2.setFont(u8g2_font_8x13_t_cyrillic);
    u8g2.setCursor(15, 37);
    u8g2.print("В РАЗРАБОТКЕ");
    
    u8g2.sendBuffer();
    
} // void drawUnderConstruction()

// Проверка нагрузки на тензодатчик
// и соответствующая реакция реле
void checkWeight()
{
    long currentWeight = getScaleValue();

    if (currentWeight >= userMass && running)
    {
        running = false;
    }
    else if (currentWeight < userMass && !running)
    {
        running = true;
    }
    else
    {
        return;
    }

    D("4 mode is ");
    D_LN(running);
    for (int i = 0; i < sizeof(mode4Relays) / sizeof(mode4Relays[0]); i++)
    {
        int id = mode4Relays[i];

        if (running)
        {
            relays[id].manual = true;
            relays[id].startTime = millis() + turnOnDelay * (unsigned long) i;
        }
        else 
        {
            relays[id].stopTime = millis() + turnOffDelay * (unsigned long) i;
        }
        
        D(id);
        D("\tisReady: ");
        D(relays[id].isReady);
        D("\tstartTime: ");
        D(relays[id].startTime);
        D("\tstopTime: ");
        D_LN(relays[id].stopTime);
    }
} // void checkWeight()

// Проверка всех реле в системе
void checkRelay()
{
    bool endOfWork = true;
    
    for (int i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
    {
        // Ручное управление
        if (relays[i].manual)
        {
            if (running)
            {
                endOfWork = false;
                if (millis() > relays[i].startTime && !relays[i].enabled)
                {
                    digitalWrite(relays[i].pin, LOW);
                    relays[i].enabled = true;
        
                    D("relay ");
                    D(i + 1);
                    D_LN(" now is working");
                    
                    redrawRequired = true;
                }
            }
            else
            {
                if (millis() > relays[i].stopTime && relays[i].enabled)
                {
                    digitalWrite(relays[i].pin, HIGH);
                    relays[i].enabled = false;
                    relays[i].manual = false;
        
                    D("relay ");
                    D(i + 1);
                    D_LN(" deactivated");
                    
                    redrawRequired = true;

                    // <КОСТЫЛЬ!!!>
                    if (7 == i && 5 == currentWindowId)
                    {
                        redraw();
                        toneDone();
                    }
                    // </КОСТЫЛЬ!!!>
                }
            }
            continue;
        }
        
        // Если реле не задействовано, оно нам не интересно
        if (!relays[i].isReady)
        {
            continue;
        }

        endOfWork = false;

        // Проверяем, должно ли реле быть включено прямо сейчас
        bool shouldBeEnabled = millis() > relays[i].startTime && millis() < relays[i].stopTime;

        // Если реальность спорит с нашими предположениями, 
        // изменяем реальность
        if (shouldBeEnabled && !relays[i].enabled)
        {
            digitalWrite(relays[i].pin, LOW);
            relays[i].enabled = true;

            D("relay ");
            D(i + 1);
            D_LN(" started working ...");
        }
        else if (!shouldBeEnabled && relays[i].enabled)
        {
            digitalWrite(relays[i].pin, HIGH);
            relays[i].isReady = false;
            relays[i].enabled = false;

            D("relay ");
            D(i + 1);
            D_LN(" done.");
        }
    }

    if (running && endOfWork)
    {
        running = false;
        redrawRequired = true;
        
        toneDone();
    }
}

// Отрисовка текущего окна в текущем режиме
void redraw()
{
    redrawRequired = false;
    lastRedraw = millis();
    
    switch(currentWindowId)
    {
        case -1:
            drawEnterDialogWindow();
            break;
            
        case 0:
            drawStartWindow();
            break;

        case 1:
            drawSelectTimeWindow(mode);
            break;

        case 2:
            drawManualWindow();
            break;
        
        case 3:
            drawCalibrationQuestion();
            break;
            
        case 4:
            drawCalibrationWindow();
            break;

        case 5:
            drawScaleWindow();
            break;

        case 6:
            drawControlWindow(mode);
            break;
            
        default:
            D_LN("INVALID_WINDOW_ID");
            D("currentWindowId = ");
            D_LN(currentWindowId);
            currentWindowId = 0;
            redrawRequired = true;
            break;
    }
} // void redraw()

// Инициализация всех параметров из долгосрочной памяти
void initDataFromEEPROM()
{
    float offset;
    EEPROM.get(0, scaleParameter);
    EEPROM.get(sizeof(scaleParameter), offset);
    
    D("scale parameter: ");
    D_LN(scaleParameter);
    D("offset: ");
    D_LN(offset);
    
    if (isnan(scaleParameter))
    {
        scaleParameter = 1;
    }
    if (isnan(offset))
    {
        offset = 0;
    }

    scale.set_scale(scaleParameter);
    scale.set_offset(offset);
    
} // void initDataFromEEPROM()

// Сохранение всех параметров в долгосрочную память
void saveDataToEEPROM()
{
    EEPROM.put(0, scaleParameter);
    float offset = scale.get_offset();
    EEPROM.put(sizeof(scaleParameter), offset);
} // void saveDataToEEPROM()

// Обработка нажатия кнопки на клавиатуре
void useKey(char key)
{
    D_LN(key);

    if (-1 == currentWindowId)
    {
        if ('B' == key)
        {
            currentWindowId = 0;
            redrawRequired = true;
        }
    }
    else if ('#' == key)
    {
        redrawRequired = true;
        
        if (0 == currentWindowId)
        {
            currentWindowId = -1;
            tone(BUZZER, 200, 100);
            return;
        }
        
        tone(BUZZER, 200, 30);
        currentWindowId = 0;
        mode = 0;
        userTimer = 0;
        userMass = 0;
        for (int i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
        {
            digitalWrite(relays[i].pin, HIGH);
            relays[i].enabled = false;
            relays[i].isReady = false;
            relays[i].manual = false;
            relays[i].startTime = 0;
            relays[i].stopTime = 0;
        }
    }
    else if (0 == currentWindowId)
    {
        if ('1' == key || 'A' == key)
        {
            currentWindowId = 1;
            mode = 1;
            redrawRequired = true;
        }
        else if ('2' == key || 'B' == key)
        {
            currentWindowId = 1;
            mode = 2;
            redrawRequired = true;
        }
        else if ('3' == key || 'C' == key)
        {
            currentWindowId = 2;
            mode = 3;
            redrawRequired = true;
        }
        else if ('4' == key || 'D' == key)
        {
            currentWindowId = 3;
            mode = 4;
            redrawRequired = true;
        }
        
    }
    else if (1 == currentWindowId)
    {
        if (key >= '0' && key <= '9')
        {
            int maxTime;
            if (1 == mode) maxTime = 130;
            else if (2 == mode) maxTime = 35;
            
            if (maxTime == userTimer)
            {
                userTimer = 0;
            }
            
            userTimer *= 10;
            userTimer += key - '0';

            if (userTimer > maxTime)
            {
                drawSelectTimeWindow(mode);
                tone(BUZZER, 170, 80);
                delay(160);
                tone(BUZZER, 170, 80);
                userTimer = maxTime;
            }
            redrawRequired = true;
        }
        else if (('A' == key && 1 == mode) 
                || ('B' == key && 2 == mode))
        {
            if (0 == userTimer)
            {
                tone(BUZZER, 170, 80);
                delay(160);
                tone(BUZZER, 170, 80);
                return;
            }
            
            unsigned long workTime = (unsigned long)userTimer * (unsigned long)60000;
            int count;
            if (1 == mode)
            {
                count = sizeof(mode1Relays) / sizeof(mode1Relays[0]);
            }
            else
            {
                count = sizeof(mode2Relays) / sizeof(mode2Relays[0]);
            }
            
            for (int i = 0; i < count; i++)
            {
                int id;
                if (1 == mode)
                {
                    id = mode1Relays[i];
                }
                else
                {
                    id = mode2Relays[i];
                }
                
                relays[id].isReady = true;
                relays[id].startTime = millis() + turnOnDelay * (unsigned long) i;
                relays[id].stopTime = millis() + workTime + turnOffDelay * (unsigned long) i;

                D(id);
                D("\tisReady: ");
                D(relays[id].isReady);
                D("\tstartTime: ");
                D(relays[id].startTime);
                D("\tstopTime: ");
                D_LN(relays[id].stopTime);
            }
            
            currentWindowId = 6;
            redrawRequired = true;
            start = millis();
            running = true;
        }
    }
    else if (2 == currentWindowId)
    {
        if ('C' == key)
        {
            running = !running;
            D("3 mode is ");
            D_LN(running);
            for (int i = 0; i < sizeof(mode3Relays) / sizeof(mode3Relays[0]); i++)
            {
                int id = mode3Relays[i];

                if (running)
                {
                    relays[id].manual = true;
                    relays[id].startTime = millis() + turnOnDelay * (unsigned long) i;
                }
                else 
                {
                    relays[id].stopTime = millis() + turnOffDelay * (unsigned long) i;
                }
                
                D(id);
                D("\tisReady: ");
                D(relays[id].isReady);
                D("\tstartTime: ");
                D(relays[id].startTime);
                D("\tstopTime: ");
                D_LN(relays[id].stopTime);
            }
            
        }
    }
    else if (3 == currentWindowId)
    {
        if ('C' == key)
        {
            currentWindowId = 4;
            redrawRequired = true;
        }
        else if ('D' == key)
        {
            currentWindowId = 5;
            weightSetting = true;
            redrawRequired = true;
        }
    }
    else if (4 == currentWindowId)
    {
        if ('D' == key)
        {
            if (0 == calibrationStep)
            {
                calibrationStep = 1;
                redrawRequired = true;

                scale.set_scale();
                scale.tare();
            }
            else if (1 == calibrationStep)
            {
                calibrationStep = 2;
                redrawRequired = true;

                actualCalibrationMass = scale.get_units(10);
            }
            else if (2 == calibrationStep)
            {
                currentWindowId = 3;
                calibrationStep = 0;
                redrawRequired = true;

                scaleParameter = actualCalibrationMass / userMass;
                scale.set_scale(scaleParameter);

                saveDataToEEPROM();

                userMass = 0;
            }
        }
        else if (key >= '0' && key <= '9' && 2 == calibrationStep)
        {
            userMass *= 10;
            userMass += key - '0';
            redrawRequired = true;
        }
    }
    else if (5 == currentWindowId)
    {
        if (key >= '0' && key <= '9' && weightSetting)
        {
            userMass *= 10;
            userMass += key - '0';
            redrawRequired = true;
        }
        else if ('D' == key)
        {
            weightSetting = false;
            redrawRequired = true;
        }
    }
} // void useKey(char key)

void setup()
{
    delay(300);
    
    pinMode(BUZZER, OUTPUT);
    tone(BUZZER, 1300, 300);
    
    D_INIT();
    
    u8g2.begin();
    u8g2.enableUTF8Print();

    // Инициализация и настройка весов
    scale.begin(SCALE_DT, SCALE_CLK);
    initDataFromEEPROM();

    rtc.begin();
    
    for (int i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
    {
        // Пин реле
        int pin = relays[i].pin;

        // Объявляем его как выход
        pinMode(pin, OUTPUT);

        // Если реле нужно проверять, оставляем ненадолго включенным
        if (relayTestRequired)
        {
            delay(100);
        }

        // Отключаем реле
        digitalWrite(pin, HIGH);

        // Сигнализируем в порт об этом
        D(pin);
        D_LN(" is OUTPUT");
    }
} // void setup()

void loop()
{
    // Читаем нажатую клавишу
    char key = keypad.getKey();
    if (key)
    {
        useKey(key);
    }

    // Распихиваем проверки на необходимость отрисовки дисплея
    // в разные блоки, чтобы не нагромождать код
    int timeLeft = getTimeLeft();
    if (timeLeft != prevTimeLeft && (running || timeLeft >= -2 && timeLeft <= 0))
    {
        redrawRequired = true;
    }
    else if (3 == currentWindowId ||
                5 == currentWindowId)
    {
        redrawRequired = true;
    }
    else if (millis() - lastRedraw > redrawInterval)
    {
        redrawRequired = true;
    }

    checkRelay();

    if (5 == currentWindowId && !weightSetting)
    {
        checkWeight();
    }

    if (redrawRequired)
    {
        redraw();
    }
    else
    {
        delay(5);
    }
    prevTimeLeft = timeLeft;
    
} // void loop()

