#!/usr/bin/env python3
"""Generate ports/macos/overrides/menu_text.h: settings-panel text in French,
German, Spanish, Italian and Russian. French/German/Italian/Spanish labels
come from TGP's src/common/translations.cpp where it has them; the rest and
all Russian text are maintained here. Run after changing either source."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LANGS = ("fr", "de", "es", "it", "ru")

# TGP order in kEntries: French, German, Italian, Spanish.
tgp = {}
for english, fr, de, it, es in re.findall(
        r'\{"([^"]+)", \{u"((?:[^"\\]|\\.)*)", u"((?:[^"\\]|\\.)*)", u"((?:[^"\\]|\\.)*)", u"((?:[^"\\]|\\.)*)"\}\}',
        (ROOT / "src/common/translations.cpp").read_text()):
    tgp[english.upper()] = {"fr": fr, "de": de, "es": es, "it": it}

# english: (fr, de, es, it) for labels TGP does not have; None leaves English.
LOCAL = {
    "MODS": ("Mods", "Mods", "Mods", "Mod"),
    "LOOK SENSITIVITY": ("Sensibilité de la vue", "Blickempfindlichkeit", "Sensibilidad de la vista", "Sensibilità visuale"),
    "AIMING": ("Visée", "Zielen", "Apuntado", "Mira"),
    "DEADZONES": ("Zones mortes", "Totzonen", "Zonas muertas", "Zone morte"),
    "FEEDBACK": ("Retour", "Feedback", "Respuesta", "Feedback"),
    "DRIVER": ("Pilote", "Treiber", "Controlador", "Driver"),
    "CALIBRATION": ("Calibrage", "Kalibrierung", "Calibración", "Calibrazione"),
    "MOUSE LOOK": ("Vue à la souris", "Mausblick", "Vista con ratón", "Visuale col mouse"),
    "KEY BINDINGS": ("Touches", "Tastenbelegung", "Asignación de teclas", "Assegnazione tasti"),
    "FILMS": ("Films", "Filme", "Vídeos", "Filmati"),
    "PLAYER": ("Joueur", "Spieler", "Jugador", "Giocatore"),
    "UNLOCKS": ("Déblocages", "Freischaltungen", "Desbloqueos", "Sblocchi"),
    "RENDER SCALE": ("Échelle de rendu", "Renderskalierung", "Escala de renderizado", "Scala di rendering"),
    "UPSCALING": ("Mise à l'échelle", "Hochskalierung", "Reescalado", "Upscaling"),
    "ANTI-ALIASING": ("Anticrénelage", "Kantenglättung", "Antialiasing", "Antialiasing"),
    "GAME LANGUAGE": ("Langue du jeu", "Spielsprache", "Idioma del juego", "Lingua del gioco"),
    "MENU LANGUAGE": ("Langue du menu", "Menüsprache", "Idioma del menú", "Lingua del menu"),
    "LEFT STICK DEADZONE": ("Zone morte du stick gauche", "Totzone linker Stick", "Zona muerta stick izquierdo", "Zona morta levetta sinistra"),
    "RIGHT STICK DEADZONE": ("Zone morte du stick droit", "Totzone rechter Stick", "Zona muerta stick derecho", "Zona morta levetta destra"),
    "TRIGGER DEADZONE": ("Zone morte des gâchettes", "Totzone der Trigger", "Zona muerta de gatillos", "Zona morta grilletti"),
    "VIBRATION": ("Vibration", "Vibration", "Vibración", "Vibrazione"),
    "DUALSENSE RAW HID": ("DualSense HID brut", "DualSense Roh-HID", "DualSense HID sin procesar", "DualSense HID grezzo"),
    "MEASURE STICK DRIFT": ("Mesurer la dérive", "Stick-Drift messen", "Medir deriva del stick", "Misura deriva levette"),
    "RESET INPUT": ("Réinitialiser les commandes", "Eingabe zurücksetzen", "Restablecer controles", "Ripristina comandi"),
    "MOUSE HORIZONTAL SENSITIVITY": ("Sensibilité horizontale souris", "Horizontale Mausempfindlichkeit", "Sensibilidad horizontal del ratón", "Sensibilità orizzontale mouse"),
    "MOUSE VERTICAL SENSITIVITY": ("Sensibilité verticale souris", "Vertikale Mausempfindlichkeit", "Sensibilidad vertical del ratón", "Sensibilità verticale mouse"),
    "MOUSE AIM HORIZONTAL SENSITIVITY": ("Visée horizontale souris", "Horizontales Zielen mit Maus", "Apuntado horizontal del ratón", "Mira orizzontale mouse"),
    "MOUSE AIM VERTICAL SENSITIVITY": ("Visée verticale souris", "Vertikales Zielen mit Maus", "Apuntado vertical del ratón", "Mira verticale mouse"),
    "INVERT MOUSE X": ("Inverser la souris X", "Maus X invertieren", "Invertir ratón X", "Inverti mouse X"),
    "INVERT MOUSE Y": ("Inverser la souris Y", "Maus Y invertieren", "Invertir ratón Y", "Inverti mouse Y"),
    "MOUSE HOLD TO AIM": ("Maintenir pour viser (souris)", "Zum Zielen halten (Maus)", "Mantener para apuntar (ratón)", "Tieni premuto per mirare (mouse)"),
    "SKIP INTRO MOVIES": ("Passer les vidéos d'intro", "Intro-Videos überspringen", "Saltar vídeos de introducción", "Salta filmati iniziali"),
    "SKIP MISSION INTROS": ("Passer les briefings", "Missionsbriefings überspringen", "Saltar sesiones informativas", "Salta i briefing"),
    "GOD MODE": ("Mode Dieu", "Gottmodus", "Modo Dios", "Modalità Dio"),
    "INFINITE MEDKITS": ("Trousses de soins infinies", "Unendliche Medikits", "Botiquines infinitos", "Medikit infiniti"),
    "INFINITE GRENADES": ("Grenades infinies", "Unendliche Granaten", "Granadas infinitas", "Granate infinite"),
    "INFINITE AMMO (SILVER WEAPONS)": ("Munitions infinies (armes argentées)", "Unendliche Munition (Silberwaffen)", "Munición infinita (armas plateadas)", "Munizioni infinite (armi d'argento)"),
    "ALWAYS SILENCED": ("Toujours silencieux", "Immer schallgedämpft", "Siempre silenciado", "Sempre silenziato"),
    "UNLOCK ALL MISSIONS": ("Débloquer toutes les missions", "Alle Missionen freischalten", "Desbloquear todas las misiones", "Sblocca tutte le missioni"),
    "UNLOCK BLACK OPS DIFFICULTY": ("Débloquer la difficulté Black Ops", "Schwierigkeit Black Ops freischalten", "Desbloquear dificultad Black Ops", "Sblocca difficoltà Black Ops"),
    "M16A2 AT MISSION START": ("M16A2 au début des missions", "M16A2 zu Missionsbeginn", "M16A2 al empezar la misión", "M16A2 a inizio missione"),
    "BFG (M249) CODE": ("Code BFG (M249)", "BFG-Code (M249)", "Código BFG (M249)", "Codice BFG (M249)"),
    "AUTO": ("Auto", "Auto", "Auto", "Auto"),
    "RESET": ("Réinitialiser", "Zurücksetzen", "Restablecer", "Ripristina"),
    "WAIT": ("Patienter", "Warten", "Espera", "Attendi"),
    "GAME": ("Jeu", "Spiel", "Juego", "Gioco"),
    "N/A": ("N/D", "N/V", "N/D", "N/D"),
    "SAVED": ("Enregistré", "Gespeichert", "Guardado", "Salvato"),
    "CHANGES ARE SAVED AUTOMATICALLY.": ("Les modifications sont enregistrées automatiquement.", "Änderungen werden automatisch gespeichert.", "Los cambios se guardan automáticamente.", "Le modifiche vengono salvate automaticamente."),
    "LIVE STICKS": ("Sticks en direct", "Sticks live", "Sticks en vivo", "Levette dal vivo"),
    "NO CONTROLLER": ("Aucune manette", "Kein Controller", "Sin mando", "Nessun controller"),
    "MOVE": ("Déplacement", "Bewegen", "Mover", "Movimento"),
}

# Russian: every string the panel shows. Format specifiers must be kept.
RU = {
    "SETTINGS": "Настройки", "RECOMP SETTINGS": "Настройки рекомпа", "MOUSE AND KEYBOARD": "Мышь и клавиатура",
    "CONTROLLER": "Геймпад", "MODS": "Моды",
    "DISPLAY": "Экран", "GRAPHICS": "Графика", "GENERAL": "Общие", "LOOK SENSITIVITY": "Чувствительность обзора",
    "AIMING": "Прицеливание", "DEADZONES": "Мёртвые зоны", "FEEDBACK": "Отдача", "DRIVER": "Драйвер",
    "CALIBRATION": "Калибровка", "MOUSE LOOK": "Обзор мышью", "KEY BINDINGS": "Назначение клавиш",
    "FILMS": "Ролики", "PLAYER": "Игрок", "UNLOCKS": "Разблокировки",
    "RENDER SCALE": "Масштаб рендера", "UPSCALING": "Апскейлинг", "FIELD OF VIEW": "Угол обзора",
    "FPS COUNTER": "Счётчик FPS", "ANTI-ALIASING": "Сглаживание", "ANISOTROPIC FILTERING": "Анизотропная фильтрация",
    "GAME LANGUAGE": "Язык игры", "MENU LANGUAGE": "Язык меню",
    "TEXTURES": "Текстуры", "TEXTURE REPLACEMENT": "Замена текстур",
    "HORIZONTAL SENSITIVITY": "Чувствительность по горизонтали", "VERTICAL SENSITIVITY": "Чувствительность по вертикали",
    "AIM HORIZONTAL SENSITIVITY": "Прицел: по горизонтали", "AIM VERTICAL SENSITIVITY": "Прицел: по вертикали",
    "INVERT X AXIS": "Инверсия оси X", "HOLD TO AIM": "Прицел при удержании", "AIM ASSIST": "Помощь в прицеливании",
    "LEFT STICK DEADZONE": "Мёртвая зона левого стика", "RIGHT STICK DEADZONE": "Мёртвая зона правого стика",
    "TRIGGER DEADZONE": "Мёртвая зона курков", "VIBRATION": "Вибрация", "RUMBLE INTENSITY": "Сила вибрации",
    "DUALSENSE RAW HID": "DualSense напрямую (HID)", "MEASURE STICK DRIFT": "Измерить дрифт стиков",
    "RESET INPUT": "Сбросить управление",
    "MOUSE HORIZONTAL SENSITIVITY": "Мышь: по горизонтали", "MOUSE VERTICAL SENSITIVITY": "Мышь: по вертикали",
    "MOUSE AIM HORIZONTAL SENSITIVITY": "Мышь, прицел: по горизонтали", "MOUSE AIM VERTICAL SENSITIVITY": "Мышь, прицел: по вертикали",
    "INVERT MOUSE X": "Инверсия мыши по X", "INVERT MOUSE Y": "Инверсия мыши по Y",
    "MOUSE HOLD TO AIM": "Прицел мышью при удержании", "WALK SPEED": "Скорость шага",
    "MOVE FORWARD": "Вперёд", "MOVE BACK": "Назад", "STRAFE LEFT": "Шаг влево", "STRAFE RIGHT": "Шаг вправо",
    "WALK": "Шаг", "CROUCH": "Присесть", "FIRE": "Огонь", "AIM": "Прицел", "MELEE": "Удар",
    "RELOAD": "Перезарядка", "INTERACT": "Действие", "THROW GRENADE": "Бросить гранату",
    "CHANGE WEAPON UP": "Следующее оружие", "CHANGE WEAPON DOWN": "Предыдущее оружие",
    "TOGGLE SUPPRESSOR": "Глушитель", "FIRING MODE / SCOPE ZOOM": "Режим огня / зум прицела",
    "HEALTH PACK": "Аптечка", "NEXT MUSIC TRACK": "Следующий трек", "OBJECTIVES": "Задачи", "PAUSE MENU": "Пауза",
    "LOOK UP": "Смотреть вверх", "LOOK DOWN": "Смотреть вниз", "LOOK LEFT": "Смотреть влево", "LOOK RIGHT": "Смотреть вправо",
    "SKIP INTRO MOVIES": "Пропуск вступительных роликов", "SKIP MISSION INTROS": "Пропуск брифингов",
    "GOD MODE": "Режим бога", "INFINITE MEDKITS": "Бесконечные аптечки", "INFINITE GRENADES": "Бесконечные гранаты",
    "INFINITE AMMO (SILVER WEAPONS)": "Бесконечные патроны (серебряное оружие)", "ALWAYS SILENCED": "Всегда с глушителем",
    "UNLOCK ALL MISSIONS": "Открыть все миссии", "UNLOCK BLACK OPS DIFFICULTY": "Открыть сложность Black Ops",
    "M16A2 AT MISSION START": "M16A2 в начале миссии", "BFG (M249) CODE": "Код BFG (M249)",
    "ON": "Вкл", "OFF": "Выкл", "GAME": "Игра", "N/A": "Н/Д", "AUTO": "Авто", "WAIT": "Ждите", "START": "Старт",
    "RESET": "Сброс", "PRESS A KEY": "Нажмите клавишу", "GAME 20%": "Игра 20%", "RAW HID": "HID напрямую",
    "PLATFORM": "Системный", "LIVE STICKS": "Стики", "NO CONTROLLER": "Нет геймпада", "MOVE": "Ход",
    "SAVED": "Сохранено", "CHANGES ARE SAVED AUTOMATICALLY.": "Изменения сохраняются автоматически.",
    "COULD NOT SAVE. SESSION ONLY.": "Не удалось сохранить. Только до выхода.",
    "Q / E, LB / RB: PAGE     UP / DOWN: SELECT": "Q / E, LB / RB: страница     Вверх / вниз: выбор",
    "BACK": "Назад", "DEFAULTS": "По умолчанию",
    "LEFT / RIGHT: ADJUST    ENTER / A: CHANGE    ESC / B: BACK": "Влево / вправо: изменить    Enter / A: выбор    Esc / B: назад",
    "GAME: %s%@": "Игра: %s%@", " XBE": " XBE", " - MODS OFF": " - моды выключены",
    "REFILLS YOUR HEALTH EVERY GAME STEP. ONE HUGE HIT CAN STILL KILL.": "Восполняет здоровье каждый шаг игры. Очень сильный удар всё ещё может убить.",
    "LIVE. YOUR CHARACTER ONLY; ENEMIES AND SAVES ARE UNCHANGED.": "Сразу. Только ваш персонаж; враги и сохранения не меняются.",
    "KEEPS THE MEDKIT POUCH FULL: THREE, THE GAME'S OWN LIMIT.": "Держит три аптечки, это предел самой игры.",
    "LIVE. USING ONE STILL HEALS AS USUAL.": "Сразу. Аптечка лечит как обычно.",
    "KEEPS NINE GRENADES.": "Держит девять гранат.",
    "LIVE. THROW AS MANY AS YOU LIKE.": "Сразу. Бросайте сколько угодно.",
    "THE GAME'S SILVER WEAPONS REWARD: MAGAZINES REFILL, 999 SHOWN.": "Награда игры «серебряное оружие»: магазин пополняется, показано 999.",
    "FROM THE NEXT MISSION START OR RESTART. CAMPAIGN MISSIONS ONLY.": "Со следующего старта или рестарта миссии. Только миссии кампании.",
    "FITS A SILENCER EVERYWHERE THE +9 TRAINER DID. EXPERIMENTAL.": "Ставит глушитель там же, где трейнер +9. Экспериментально.",
    "LIVE. AFFECTS EVERY WEAPON THE GAME UPDATES, NOT ONLY YOURS.": "Сразу. Действует на всё оружие в игре, не только на ваше.",
    "OPENS ALL EIGHT MISSIONS ON EASY, NORMAL AND HARD.": "Открывает все восемь миссий на лёгкой, средней и высокой сложности.",
    "ALSO BLACK OPS WHEN THAT MOD IS ON. SAVED IN THE PROFILE; OFF DOES NOT RELOCK.": "С модом Black Ops — и на ней. Сохраняется в профиле; выключение не закрывает.",
    "OPENS THE BLACK OPS DIFFICULTY, NORMALLY EARNED ON HARD.": "Открывает сложность Black Ops, обычно её дают за прохождение на высокой.",
    "FIRST MISSION, OR ALL WITH UNLOCK ALL MISSIONS. SAVED IN THE PROFILE.": "Первая миссия, а с «Открыть все миссии» — все. Сохраняется в профиле.",
    "STARTS EACH MISSION WITH THE M16A2, THE BLACK OPS REWARD.": "Каждая миссия начинается с M16A2, наградой за Black Ops.",
    "M249 IN PLACE OF THE FIRST MISSION'S STARTING WEAPON.": "M249 вместо стартового оружия в первой миссии.",
    "SAME AS A XXXX-XXXX-XXXX-XXXX PROFILE NAME. SAVED; OFF DOES NOT REMOVE IT.": "То же, что имя профиля XXXX-XXXX-XXXX-XXXX. Сохраняется; выключение не убирает.",
    "THIS GAME REVISION HAS NO VERIFIED ADDRESSES. MODS STAY OFF.": "Для этой версии игры нет проверенных адресов. Моды выключены.",
    "PRESS A KEY, MOUSE BUTTON OR WHEEL. ESC CANCELS, DELETE CLEARS.": "Нажмите клавишу, кнопку мыши или колесо. Esc — отмена, Delete — очистить.",
    "LEFT / RIGHT SELECT A SLOT. ENTER / A STARTS KEY CAPTURE.": "Влево / вправо — выбор ячейки. Enter / A — назначить клавишу.",
    "TWO BINDINGS PER ACTION. PHYSICAL KEY POSITIONS; SAVED IMMEDIATELY.": "Две клавиши на действие. Учитывается положение клавиш; сохраняется сразу.",
    "MOUSE SPEED WHILE AIMING OR SCOPED. ORIGINAL: 1.00X.": "Скорость мыши при прицеливании. Исходная: 1.00X.",
    "RELATIVE MOUSE MOVEMENT TURNS THE VIEW DIRECTLY, WITHOUT STICK LIMITS.": "Движение мыши поворачивает камеру напрямую, без ограничений стика.",
    "LIVE. CLICK THE GAME TO CAPTURE THE MOUSE; ESC RELEASES AND PAUSES.": "Сразу. Щелчок по игре захватывает мышь; Esc отпускает и ставит паузу.",
    "REVERSE THIS AXIS OF THE MOUSE. CONTROLLER INVERSION IS SEPARATE.": "Инвертирует эту ось мыши. Инверсия геймпада настраивается отдельно.",
    "LIVE. DOES NOT CHANGE THE GAME'S CONTROLLER INVERT Y OPTION.": "Сразу. Не меняет инверсию Y геймпада в настройках игры.",
    "HOLD THE AIM BUTTON FOR AIMING AND SCOPES WITH MOUSE AND KEYBOARD.": "Прицел и оптика, пока зажата кнопка прицела (мышь и клавиатура).",
    "LIVE. OFF KEEPS THE GAME'S ORIGINAL AIM RULES. CONTROLLER IS SEPARATE.": "Сразу. Выкл. — исходные правила игры. Геймпад настраивается отдельно.",
    "MOVEMENT SPEED WHILE THE WALK ACTION IS HELD. DEFAULT: 50%.": "Скорость, пока зажат шаг. По умолчанию: 50%.",
    "LIVE. WALK IS ALT BY DEFAULT; BOTH KEY SLOTS CAN BE REBOUND.": "Сразу. Шаг по умолчанию на Alt; обе клавиши можно переназначить.",
    "LOOK SPEED WHILE AIMING OR SCOPED. ORIGINAL: 1.00X.": "Скорость обзора при прицеливании. Исходная: 1.00X.",
    "LOOK SPEED AFTER BLACK'S STICK CURVE. ORIGINAL: 1.00X.": "Скорость обзора после кривой стика BLACK. Исходная: 1.00X.",
    "LIVE. X / Y AND AIMING ARE INDEPENDENT. STEP: 0.02X.": "Сразу. X, Y и прицел настраиваются отдельно. Шаг: 0.02X.",
    "REVERSE HORIZONTAL CAMERA MOVEMENT ON THE CONTROLLER.": "Инвертирует горизонтальное движение камеры на геймпаде.",
    "LIVE. VERTICAL INVERSION IS IN THE GAME'S OWN OPTIONS.": "Сразу. Инверсия по вертикали — в настройках самой игры.",
    "HOLD THE AIM BUTTON FOR AIMING AND SCOPES.": "Прицел и оптика, пока зажата кнопка прицела.",
    "LIVE, CONTROLLER ONLY. OFF KEEPS THE GAME'S ORIGINAL AIM RULES.": "Сразу, только геймпад. Выкл. — исходные правила игры.",
    "ENABLE BLACK'S VIEW ATTRACTION AND SHOT ASSISTANCE.": "Включает притяжение прицела и помощь при стрельбе из BLACK.",
    "LIVE. OFF SKIPS BOTH. ASSISTANCE IS DISABLED WITH KEYBOARD INPUT.": "Сразу. Выкл. отключает оба. С клавиатуры помощь не работает.",
    "MOTOR STRENGTH AS A PERCENTAGE OF THE GAME'S VIBRATION.": "Сила моторов в процентах от вибрации игры.",
    "LIVE. ORIGINAL: 100%. ZERO IS SILENT. MAXIMUM: 200%.": "Сразу. Исходная: 100%. Ноль — без вибрации. Максимум: 200%.",
    "MOVEMENT: LOWER FOR SMALLER, EARLIER INPUTS.": "Движение: меньше — отклик на малые отклонения.",
    "CAMERA: LOWER FOR FINER AIM NEAR THE CENTER.": "Камера: меньше — точнее прицел у центра.",
    "LIVE. GAME KEEPS 20%. REPLACES THE INNER ZONE, NOT ADDED TO IT.": "Сразу. В игре 20%. Заменяет внутреннюю зону, а не добавляется к ней.",
    "TRIGGER TRAVEL BEFORE INPUT COUNTS. ZERO KEEPS ORIGINAL.": "Ход курка до срабатывания. Ноль — как в оригинале.",
    "LIVE. REMAINING TRAVEL IS RESCALED TO FULL INPUT.": "Сразу. Оставшийся ход растягивается до полного.",
    "OFF STOPS RUMBLE. ON ALLOWS THE GAME TO DRIVE THE MOTORS.": "Выкл. — без вибрации. Вкл. — моторами управляет игра.",
    "LIVE. SUPPORT DEPENDS ON THE CONTROLLER AND DRIVER.": "Сразу. Поддержка зависит от геймпада и драйвера.",
    "ON: SDL RAW HID. OFF: PLATFORM CONTROLLER DRIVER.": "Вкл.: SDL HID напрямую. Выкл.: системный драйвер геймпада.",
    "RESTART REQUIRED. CURRENT REQUEST: %@.": "Нужен перезапуск. Сейчас: %@.",
    "RELEASE BOTH STICKS FOR TWO SECONDS. ENTER / A STARTS.": "Отпустите оба стика на две секунды. Enter / A — начать.",
    "MEASURING... DO NOT TOUCH THE STICKS. LEFT CANCELS.": "Измерение... Не трогайте стики. Влево — отмена.",
    "SET LEFT %d%% / RIGHT %d%%, WITH A 2%% NOISE MARGIN.": "Левый %d%% / правый %d%%, с запасом 2%% на шум.",
    "TOO MUCH MOVEMENT. NO SETTINGS CHANGED. TRY AGAIN.": "Слишком много движения. Ничего не изменено. Попробуйте ещё раз.",
    "CONNECT THE CONTROLLER AND TRY AGAIN. NO CHANGE.": "Подключите геймпад и попробуйте ещё раз. Без изменений.",
    "MEASUREMENT CANCELLED. NO SETTINGS CHANGED.": "Измерение отменено. Ничего не изменено.",
    "SETS INNER ZONES ONLY. NEVER CHANGES THE STICK CENTER.": "Меняет только внутренние зоны, центр стика не трогает.",
    "RESTORE ORIGINAL LOOK, STICK ZONES AND CONTROLLER DEFAULTS.": "Вернуть исходные обзор, зоны стиков и настройки геймпада.",
    "ENTER / A RESETS INPUT ONLY. DRIVER CHANGES NEED A RESTART.": "Enter / A сбрасывает только управление. Смена драйвера — после перезапуска.",
    "DISPLAYS THE FRAME RATE ON SCREEN, IN THE GAME'S OWN FONT.": "Показывает частоту кадров на экране шрифтом самой игры.",
    "LIVE. GAME FRAMES PER SECOND; BLACK RUNS NEAR 30 BY DESIGN.": "Сразу. Кадры игры в секунду; BLACK задуман примерно на 30.",
    "SMOOTHS JAGGED EDGES. SMAA IS SHARPER, FXAA IS CHEAPER.": "Сглаживает «лесенки». SMAA чётче, FXAA легче.",
    "APPLIES NEXT LAUNCH. CURRENT: %@.": "При следующем запуске. Сейчас: %@.",
    "APPLIES NEXT LAUNCH. CURRENT: %dX.": "При следующем запуске. Сейчас: %dX.",
    "APPLIES NEXT LAUNCH. CURRENT: %d.": "При следующем запуске. Сейчас: %d.",
    "INTERNAL RENDER TARGETS: %d X %d.": "Внутреннее разрешение: %d x %d.",
    "SHARPER TEXTURES ON FLOORS AND WALLS AT AN ANGLE.": "Чётче текстуры пола и стен под углом.",
    "METALFX SHARPENS THE RENDER SCALE UP TO THE WINDOW'S PIXELS.": "MetalFX чётко растягивает рендер до пикселей окна.",
    "CAMERA ANGLE IN DEGREES. ORIGINAL: 70.": "Угол камеры в градусах. Исходный: 70.",
    "SKIP THE BRIEFING FILM BEFORE EACH MISSION.": "Пропускать брифинг перед каждой миссией.",
    "SKIP EA, CRITERION AND STARTUP FILMS.": "Пропускать заставки EA, Criterion и вступление.",
    "MISSION PROGRESS AND ENDING ARE UNCHANGED.": "Прогресс миссий и концовка не меняются.",
    "FILMS PLAY BRIEFLY BEFORE SKIPPING.": "Ролик успевает начаться перед пропуском.",
    "TEXT AND MENUS OF THE GAME ITSELF, FROM YOUR DISC'S STRING BANKS.": "Текст и меню самой игры из языковых файлов вашего диска.",
    "THIS DISC HAS ENGLISH ONLY. THE EUROPEAN DISC ADDS FOUR LANGUAGES.": "На этом диске только английский. На европейском есть ещё четыре языка.",
    "LANGUAGE OF THIS SETTINGS PANEL. AUTO FOLLOWS THE GAME LANGUAGE.": "Язык этой панели настроек. «Авто» — как язык игры.",
    "LIVE. RUSSIAN IS FOR THIS PANEL ONLY: THE GAME HAS NO RUSSIAN TEXT.": "Сразу. Русский — только для этой панели: в игре нет русского текста.",
    "YOUR BLACK HD REIMAGINED PACK IN APPLICATION SUPPORT/BLACK/TEXTURES/REPLACEMENT.": "Ваш пак BLACK HD Reimagined в Application Support/BLACK/Textures/replacement.",
    "LIVE. PORTINGPETE'S MAP FINDS EACH FILE; MISSING ONES KEEP THE GAME'S TEXTURE.": "Сразу. Файлы находит таблица portingpete; без файла остаётся текстура игры.",
    "RAW %+.0f / %+.0f%%": "Сырое %+.0f / %+.0f%%", "OUT %+.0f / %+.0f%%": "Выход %+.0f / %+.0f%%",
}

def c(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

keys = sorted(set(RU) | set(LOCAL) | {k for k in tgp if k in RU or k in LOCAL})
rows = []
for k in keys:
    t = dict(tgp.get(k, {}))
    if k in LOCAL:
        for lang, text in zip(("fr", "de", "es", "it"), LOCAL[k]):
            if text and not t.get(lang): t[lang] = text
    t["ru"] = RU.get(k, "")
    rows.append("    {%s, {%s}}," % (c(k), ", ".join(c(t.get(l, "")) for l in LANGS)))
out = ROOT / "ports/macos/overrides/menu_text.h"
out.write_text(
    "/* Generated by tools/macos/make_menu_text.py; edit the generator. Settings-panel\n"
    " * text by its English, in French, German, Spanish, Italian and Russian. An\n"
    " * empty entry falls back to English. French/German/Italian/Spanish labels\n"
    " * follow TGP's src/common/translations.cpp. */\n"
    "#ifndef BLACK_MENU_TEXT_H\n#define BLACK_MENU_TEXT_H\n"
    "enum { BLACK_TEXT_EN, BLACK_TEXT_FR, BLACK_TEXT_DE, BLACK_TEXT_ES, BLACK_TEXT_IT, BLACK_TEXT_RU, BLACK_TEXT_COUNT };\n"
    "typedef struct { const char *english, *text[BLACK_TEXT_COUNT - 1]; } BlackMenuText;\n"
    "static const BlackMenuText black_menu_text[] = {\n" + "\n".join(rows) + "\n};\n#endif\n")
print(out, len(rows), "entries")
