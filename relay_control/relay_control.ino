#include <U8g2lib.h>
#include <Keypad.h>

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
    
    unsigned long startTime;
    unsigned long stopTime;
    bool enabled;
    bool isReady;
    int pin;
};

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

int mode1Relays[4] = {0, 1, 2, 6};
int mode2Relays[4] = {0, 1, 2, 6};
int mode3Relays[4] = {0, 3, 4, 6};
int mode4Relays[4] = {0, 1, 5, 7};


// Задержки включения и выключения реле в последовательности 
// в миллисекундах
const unsigned long turnOnDelay = 15000;
const unsigned long turnOffDelay = 6000;

// Время последнего запуска
unsigned long start;

// Номер текущего окна
int currentWindowId = 0;
// 0 - Выбор режима
// 1 - выбор времени в режиме 1 и 2
// 

// Режим от 1 до 4.
// Если mode == 0, все отключено
int mode = 0;

// Время, которое вводит пользователь в 1 и 2 режиме
int userTimer = 0;

// Нужна ли перерисовка интерфейса
bool redrawRequired = true;

// Работает ли сейчас система
bool running = false;

// Количество оставшегося времени в предыдущей итерации
// Нужно для оптимизации вывода оставшегося времени на дисплей
int prevTimeLeft = 0;

const int width = 127;
const int height = 63;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
}; 

byte rowPins[ROWS] = {11,10, 9, 8}; 
byte colPins[COLS] = {7, 6, 5, 4}; 

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Создаём объект u8g2 для работы с дисплеем, указывая номер вывода CS для аппаратной шины SPI
U8G2_ST7920_128X64_F_HW_SPI u8g2(U8G2_R0, 10);


void drawCap()
{
    u8g2.setFont(u8g2_font_profont11_tn);
    u8g2.setCursor(36, 7);
    u8g2.print("21:10  17.05.19");
    u8g2.drawLine(0, 8, width, 8);
    
//    u8g2.drawLine(width / 2, 0, width / 2, height);
//    u8g2.drawLine(width / 2 + 1, 0, width / 2 + 1, height);
}

void drawStartWindow()
{
    u8g2.clearBuffer();
    
    drawCap();
    u8g2.drawRFrame(4, 14, 118, 20, 10);
    u8g2.drawRFrame(4, 14, 118, 21, 10);
    u8g2.setFont(u8g2_font_8x13_t_cyrillic);
    u8g2.setCursor(8, 28);
    u8g2.print("Выберите режим");
    
    u8g2.setCursor(20, 54);
    u8g2.setFont(u8g2_font_profont17_tn);
    u8g2.print("1  2  3  4");

    u8g2.sendBuffer();
}

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
//    u8g2.setFont();
//    u8g2.print(" мин.");
}

void drawSelectTimeWindow(byte mode)
{
    u8g2.clearBuffer();
    
    drawCap();
    
    {   // Иконка "режим"
        u8g2.drawRFrame(72, 14, 50, 23, 5);
        u8g2.setFont(u8g2_font_8x13_t_cyrillic);
        u8g2.setCursor(77, 28);
        u8g2.print("Режим");
    
        u8g2.setFont(u8g2_font_t0_16_me);
        u8g2.setCursor(92, 41);
        u8g2.print(mode);
    }

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
}

int getTimeLeft()
{
    unsigned long lapsed = millis() - start;
    lapsed /= 1000; // Убираем миллисекунды
    return userTimer * 60 - lapsed;
}

void drawControlWindow(byte mode)
{
    u8g2.clearBuffer();
    
    drawCap();

    {   // Иконка "режим"
        u8g2.drawRFrame(72, 14, 50, 23, 5);
        u8g2.setFont(u8g2_font_8x13_t_cyrillic);
        u8g2.setCursor(77, 28);
        u8g2.print("Режим");
    
        u8g2.setFont(u8g2_font_t0_16_me);
        u8g2.setCursor(92, 41);
        u8g2.print(mode);
    }

    u8g2.setFont(u8g2_font_haxrcorp4089_t_cyrillic );
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
}

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
}

void checkRelay()
{
    bool endOfWork = true;
    
    for (int i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
    {
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

    if (endOfWork)
    {
        running = false;
        redrawRequired = true;
    }
}

void redraw()
{
    redrawRequired = false;
    
    switch(currentWindowId)
    {
        case 0:
            drawStartWindow();
            break;

        case 1:
            drawSelectTimeWindow(mode);
            break;

        case 2:
        case 3:
        case 4:
            drawUnderConstruction();
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
}

void setup()
{
    D_INIT();
    
    u8g2.begin();
    u8g2.enableUTF8Print();

//    running = true;
//    userTimer = -1;
//    currentWindowId = 6;
//    drawControlWindow(2);
//    delay(1000);
//    exit(0);
    
    for (int i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
    {
        int pin = relays[i].pin;
        pinMode(pin, OUTPUT);
        delay(100);
        digitalWrite(pin, HIGH);
        D(pin);
        D_LN(" is OUTPUT");
    }
    
    drawStartWindow();
    
} // void setup()

void loop()
{
    // Читаем нажатую клавишу
    char key = keypad.getKey();
    if (key)
    {
        D_LN(key);

        if (0 == currentWindowId)
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
        else if ('#' == key)
        {
            currentWindowId = 0;
            mode = 0;
            userTimer = 0;
            redrawRequired = true;
            for (int i = 0; i < sizeof(relays) / sizeof(relays[0]); i++)
            {
                digitalWrite(relays[i].pin, HIGH);
                relays[i].enabled = false;
                relays[i].isReady = false;
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
                    userTimer = maxTime;
                }
                redrawRequired = true;
            }
            else if (('A' == key && 1 == mode && userTimer != 0) 
                    || ('B' == key && 2 == mode && userTimer != 0))
            {
                unsigned long workTime = (unsigned long)userTimer * (unsigned long)60000;
                for (int i = 0; i < sizeof(mode1Relays) / sizeof(mode1Relays[0]); i++)
                {
                    int id = mode1Relays[i];
                    
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
//            else if ('B' == key && 2 == mode)
//            {
//                currentWindowId = 6;
//                redrawRequired = true;
//            }
        }
    }

    int timeLeft = getTimeLeft();
    if (timeLeft != prevTimeLeft && running)
    {
        redrawRequired = true;
    }

    checkRelay();

    if (redrawRequired)
    {
        redraw();
    }
    else
    {
        delay(10);
    }
    prevTimeLeft = timeLeft;
    
} // void loop()
