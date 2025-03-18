// Частоты нот для звуковых эффектов
#define NOTE_C5   523  
#define NOTE_CS5  554  
#define NOTE_D5   587  
#define NOTE_DS5  622  

const byte LED_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9};               // Пины светодиодов
const byte NUM_LEDS   = sizeof(LED_PINS) / sizeof(LED_PINS[0]); // Их количество
const byte BUTTON_PIN = 10;  
const byte BUZZER_PIN = 11;  

volatile bool buttonPressed = false;          // Определяет, нажата ли кнопка
volatile unsigned long lastDebounceTime = 0;  // Время последнего нажатия кнопки
const unsigned long DEBOUNCE_DELAY = 200;     // Защита от ложного срабатывания кнопки (в мс)
int currentLED = 0;     // Текущий светодиод бегущего огня (индекс в LED_PINS)
int targetLED  = 0;     // Светодиод, который нужно поймать (индекс в LED_PINS)


// ---------------- Переменные для асинхронной обработки ----------------

unsigned long previousMillisCycle = 0; // Время начала текущего цикла движения
// Состояние цикла (0 - шаг движения, 1 - ожидание 100 мс, 2 - ожидание до 300 мс)
int cycleState = 0; 
// 0: нет действия, 1: toneUp, 2: toneDown, 3: мигание (flash) после toneDown
int actionMode = 0; 
bool busy = false;        // Флаг, что сейчас выполняется асинхронное действие

// Состояния и переменные для асинхронной реализации toneUp
int toneUpState = 0;  //0 - играем первую ноту, 1 - ждем 100 мс, 2 - ждем до 500 мс
unsigned long toneUpMillis = 0; // время воспроизведения звука

// Состояния и переменные для асинхронной реализации toneDown
int toneDownState = 0;
unsigned long toneDownMillis = 0; // время воспроизведения звука
int toneDownLoop_i = 0;
int toneDownLoop_pitch = 0;

// Состояния и переменные для асинхронной реализации мигания
int flashState = 0;
unsigned long flashMillis = 0;
int flashIteration = 0;



void setup() {
  // Инициализируем диоды как выходы и гасим их
  for (byte i = 0; i < NUM_LEDS; i++) 
  {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  // Инициализация кнопки 
  // INPUT_PULLUP - режим, при котором пин используется как вход,
  // но при этом подтягивается к +5v через внутренний резистор (20-50кОм)
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Включаем прерывание по изменению состояния (PCI)
  // PCICR - регистр, который включает прерывания PCI для всей группы пинов
  // PCIE0 - включает прерывания на группе пинов PORTB (D8-D13)
  PCICR  |= (1 << PCIE0);      
  // Нам нужен D10 (button);
  // Используем маску, которая разрешает прерывание на pcint2, но игнорирует остальные
  PCMSK0 |= (1 << PCINT2);     

  pinMode(BUZZER_PIN, OUTPUT);    // Настраиваем буззер как выход
  digitalWrite(BUZZER_PIN, LOW);  // Выключаем звук

  // Инициализируем начальное состояние генератора случайных чисел
  // (используется для определения диода, который нужно будет поймать)
  randomSeed(analogRead(A0));

  chooseNewTarget();  // Выбираем первый случайный диод

  // Ставим бегущий огонь на начальную позицию 
  currentLED = 0;
  digitalWrite(LED_PINS[currentLED], HIGH);
}


// updateCycle() - управление движением бегущего огня и ожидание нажатия кнопки
// processAction() - асинхронная обработка звуковых эффектов и мигания при нажатии кнопки
void loop() {
  unsigned long currentMillis = millis();
  
  // Если нет выполняющегося асинхронного действия, обновляем цикл движения
  if (!busy) {
    updateCycle(currentMillis);
  }
  else {
    // Если флаг busy установлен, обрабатываем нажатую кнопку через асинхронные функции
    processAction(currentMillis);
  }
}


// Здесь происходит движение светодиода, ожидание 100 мс для нажатия кнопки и задержка до 300 мс
void updateCycle(unsigned long currentMillis) {
  // Перемещаем бегущий огонь и фиксируем время начала цикла
  if (cycleState == 0) {
    moveFireLED();
    previousMillisCycle = currentMillis;
    cycleState = 1;   // состояние ожидания 100 мс
  }
  // Если состояние цикла "ждать 100 мс"
  else if (cycleState == 1) {
    // Если 100 мс прошло, то переходим в состоние "ждать 200 мс"
    if (currentMillis - previousMillisCycle >= 100) {
      cycleState = 2;
    }
    // Если кнопка нажата, фиксируем событие и выбираем режим действия
    if (buttonPressed) 
    {
      buttonPressed = false;  // Сброс флага
      busy = true;  // Устанавливаем флаг "выполнение асинхронного действия"
      if (currentLED == targetLED) {  // Нужный диод пойман
        actionMode = 1;       // Режим toneUp (восходящая мелодия)
        toneUpState = 0;
      }
      else {
        actionMode = 2;       // Режим toneDown (нисходящая мелодия)
        toneDownState = 0;
      }
    }
  }
  // Если состояние цикла "ждать 200 мс"
  else if (cycleState == 2) {
    // Дополнительное ожидание, чтобы суммарное время цикла составило 300 мс
    if (currentMillis - previousMillisCycle >= 300) {
      cycleState = 0;
    }
  }
}


// Функция processAction() направляет выполнение в зависимости от выбранного режима действия:
// 1 - toneUp, 2 - toneDown, 3 - мигание всех светодиодов 
void processAction(unsigned long currentMillis) {
  if (actionMode == 1) {
    processToneUp(currentMillis);
  }
  else if (actionMode == 2) {
    processToneDown(currentMillis);
  }
  else if (actionMode == 3) {
    processFlash(currentMillis);
  }
}


// Реализация восходящей мелодии без delay
void processToneUp(unsigned long currentMillis) {
  // Играем первую ноту
  if (toneUpState == 0) {
    tone(BUZZER_PIN, 800, 200);   // 800 Гц на 200 мс
    toneUpMillis = currentMillis;
    toneUpState = 1;
  }
  // Состояние "ждать 250 мс", чтобы успеть услышать первую ноту
  else if (toneUpState == 1) {
    // Если время прошло, играем следующую
    if (currentMillis - toneUpMillis >= 250) {
      tone(BUZZER_PIN, 1200, 200);  // 1200 Гц на 200 мс
      toneUpMillis = currentMillis;
      toneUpState = 2;  // Переходим в состояние "ждать до 500 мс"
    }
  }
  else if (toneUpState == 2) {
    if (currentMillis - toneUpMillis >= 250) {
      noTone(BUZZER_PIN); // Выключаем звук на D10
      busy = false;       // Завершаем асинхронное действие
      actionMode = 0;     // Сбрасываем действие
      toneUpState = 0;    // Сбрасываем состояние для задержки
    }
  }
}


// Нисходящая мелодия
void processToneDown(unsigned long currentMillis) {
  // Играем первую ноту
  if (toneDownState == 0) {
    tone(BUZZER_PIN, NOTE_DS5);
    toneDownMillis = currentMillis; // Обновляем время воспроизведения
    toneDownState = 1;  // Переходим в состояние "ждать 300 мс"
  }
  // Если состояние "ждать 300 мс"
  else if (toneDownState == 1) {
    // Если 300 мс прошли
    if (currentMillis - toneDownMillis >= 300) {
      tone(BUZZER_PIN, NOTE_D5);      // Играем вторую ноту
      toneDownMillis = currentMillis; // Обновляем время
      toneDownState = 2;  // Обновляем состояние, чтобы прождать еще 300 мс
    }
  }
  // Если состояние "ждать до 600 мс"
  else if (toneDownState == 2) {
    if (currentMillis - toneDownMillis >= 300) {
      tone(BUZZER_PIN, NOTE_CS5);     // Играем третью ноту
      toneDownMillis = currentMillis; // Обновляем время
      toneDownState = 3;  // Переходим в следующее состояние
    }
  }
  // При состоянии 3 ждем 300 мс чтобы не заглушить третью ноту
  // и подготавливаем переменные для состояния 4
  else if (toneDownState == 3) {
    if (currentMillis - toneDownMillis >= 300) {
      toneDownState = 4;
      toneDownLoop_i = 0;
      toneDownLoop_pitch = -10;
      toneDownMillis = currentMillis;
    }
  }
  // Состояние 4 - колебания одной частоты
  else if (toneDownState == 4) {
    // Нужно 10 колебаний
    if (toneDownLoop_i < 10) {
      // Воспроизводим ноты с паузой в 5 мс
      if (currentMillis - toneDownMillis >= 5) {
        tone(BUZZER_PIN, NOTE_C5 + toneDownLoop_pitch);
        toneDownMillis = currentMillis;
        toneDownLoop_pitch++;
        if (toneDownLoop_pitch > 10) {
          toneDownLoop_pitch = -10;
          toneDownLoop_i++;
        }
      }
    }
    // Если 10 уже было, то завершаем звуковой эффект
    else {
      noTone(BUZZER_PIN);
      // После звуковых эффектов запускаем мигание всех светодиодов
      actionMode = 3;
      flashState = 0;
      flashMillis = currentMillis;
      flashIteration = 0;
      toneDownState = 0; // Сбрасываем состояние для toneDown
    }
  }
}


// Мигание диодов перед перезапуском игры без delay
void processFlash(unsigned long currentMillis) {
  // Первое мигание
  if (flashState == 0) {
    // Включаем все светодиоды
    for (byte j = 0; j < NUM_LEDS; j++) 
    {
      digitalWrite(LED_PINS[j], HIGH);
    }
    flashMillis = currentMillis;  // Обновляем время
    flashState = 1; // Переходим в следующее состояние
  }
  // Состояние 1 - "ждать 200 мс", чтобы была пауза перед выключением диодов
  else if (flashState == 1) {
    if (currentMillis - flashMillis >= 200) {
      flashMillis = currentMillis;  // Обновляем время
      flashState = 2; // Переходим в следующее состояние
    }
  }
  // Состояние 2 - выключем все диоды после паузы в 200 мс
  else if (flashState == 2) {
    for (byte j = 0; j < NUM_LEDS; j++) 
    {
      digitalWrite(LED_PINS[j], LOW);
    }
    flashState = 3; // Переходим в следующее состояние
    flashMillis = currentMillis;  // Обновляем время
  }
  // Состояние 3 означает, что до данного момента было выполнено
  // flashIteration полных миганий
  else if (flashState == 3) {
    // Проверяем, что диоды горят уже 200 мс
    if (currentMillis - flashMillis >= 200) {
      flashIteration++; // Увеличиваем счетчик полных миганий
      if (flashIteration < 3) { // Если было меньше трех, то 
        flashState = 0; // устанавливаем состояние 0, чтобы выполнить еще мигание
      }
      else {  // Если три уже было, перезапускам игру
        restartGame();
        busy = false; // Сбрасываем флаг выполнения действия
        actionMode = 0; // Сбрасываем выбранное действие
        flashState = 0; // Сбрасываем состояние мигания
      }
    }
  }
}


// Двигаем бегущий огонь слева направо
// Целевой диод горит всегда
void moveFireLED() {
  // Гасим старый светодиод огня, если он не является целью
  if (currentLED != targetLED) 
  {
    digitalWrite(LED_PINS[currentLED], LOW);
  }

  // Переходим к следующему 
  currentLED++;
  // Если вышли за границы - начинаем сначала
  if (currentLED >= NUM_LEDS) 
  {
    currentLED = 0;
  }

  // Зажигаем новый, если он не совпадает с целью
  if (currentLED != targetLED) {
    digitalWrite(LED_PINS[currentLED], HIGH);
  }
}

// Выбираем новый случайный светодиод-цель и гасим старый
void chooseNewTarget() {
  // Выключаем старую цель
  digitalWrite(LED_PINS[targetLED], LOW);

  // Выбираем случайный индекс [0..NUM_LEDS-1]
  targetLED = random(0, NUM_LEDS);

  // Зажигаем новую цель
  digitalWrite(LED_PINS[targetLED], HIGH);
}


// Перезапуск игры после промаха
void restartGame() {
  // Гасим все диоды
  for (byte i = 0; i < NUM_LEDS; i++) 
  {
    digitalWrite(LED_PINS[i], LOW);
  }

  // Выбираем новую цель
  chooseNewTarget();

  // Ставим бегущий огонь в начало
  currentLED = 0;
  digitalWrite(LED_PINS[currentLED], HIGH);
}



// ISR - автоматически вызывается процессором, 
// когда срабатывает прерывание PCI
ISR(PCINT0_vect) {

  // Проверяем, что прерывание сработало действительно на нажатие кнопки
  if (digitalRead(BUTTON_PIN) == LOW) 
  {
    // Запоминаем время срабатывания
    unsigned long currentTime = millis();

    // Антидребезг - реагируем только если прошло > DEBOUNCE_DELAY
    if (currentTime - lastDebounceTime > DEBOUNCE_DELAY) 
    {
      // Обновляем время последнего нажатия
      lastDebounceTime = currentTime;
      buttonPressed = true;
    }

  }
}
