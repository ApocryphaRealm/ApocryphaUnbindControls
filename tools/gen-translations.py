# -*- coding: utf-8 -*-
"""gen-translations.py - builds the eleven ApocryphaUnbindControls_<language>.txt files.

The English key list is extracted from source/UI.cpp by regex on strings::TR("KEY", "text") plus
the parallel kLogLevelKeys/kLogLevelNames arrays, so it can never drift from the code. The other
ten languages are this project's own translations, held below as dictionaries; a key missing from
a language dictionary is an error (rule 66: every string in all eleven), not a silent fallback.

Writes dist/Interface/Translations/ApocryphaUnbindControls_<language>.txt (UTF-16LE with a BOM,
one "$key<TAB>text" per line, CRLF records - the SKSE/SkyUI shape the framework's reader takes).
Control names themselves are NOT here: the page shows the game's own translation of each control.

Run: `python tools/gen-translations.py` from anywhere.
"""
import io
import os
import re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANGS = ["english", "japanese", "korean", "chinese", "russian", "german", "french", "spanish", "italian", "polish", "czech"]
STEM = "ApocryphaUnbindControls"

TR_RE = re.compile(r'strings::TR\(\s*"((?:[^"\\]|\\.)+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')
KEY_ARRAY_RE = re.compile(r'constexpr const char\* kLogLevelKeys\[\]\s*=\s*\{([^}]*)\};', re.S)
NAME_ARRAY_RE = re.compile(r'constexpr const char\* kLogLevelNames\[\]\s*=\s*\{([^}]*)\};', re.S)
STR_LIT_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def unescape(s):
    return s.encode("latin-1", "backslashreplace").decode("unicode_escape") if "\\" in s else s


def read_keys():
    src = io.open(os.path.join(REPO, "source", "UI.cpp"), "r", encoding="utf-8").read()
    keys, order = {}, []
    for m in TR_RE.finditer(src):
        key, text = unescape(m.group(1)), unescape(m.group(2))
        if key in keys and keys[key] != text:
            raise RuntimeError("duplicate key %r with two English texts" % key)
        if key not in keys:
            order.append(key)
        keys[key] = text
    km, nm = KEY_ARRAY_RE.search(src), NAME_ARRAY_RE.search(src)
    if not km or not nm:
        raise RuntimeError("kLogLevelKeys/kLogLevelNames not found")
    ak = [unescape(s) for s in STR_LIT_RE.findall(km.group(1))]
    an = [unescape(s) for s in STR_LIT_RE.findall(nm.group(1))]
    if len(ak) != len(an):
        raise RuntimeError("log level arrays differ in length")
    for k, t in zip(ak, an):
        if k not in keys:
            order.append(k)
        keys[k] = t
    return keys, order


T = {}

T["japanese"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "ゲームプレイ", "UVC_Ctx_MenuMode": "メニュー（共通）", "UVC_Ctx_Console": "コンソール",
    "UVC_Ctx_ItemMenus": "アイテムメニュー", "UVC_Ctx_Inventory": "所持品", "UVC_Ctx_DebugText": "デバッグテキスト",
    "UVC_Ctx_Favorites": "お気に入りメニュー", "UVC_Ctx_Map": "マップ", "UVC_Ctx_Stats": "スキル", "UVC_Ctx_Cursor": "カーソル",
    "UVC_Ctx_Book": "本", "UVC_Ctx_DebugOverlay": "デバッグオーバーレイ", "UVC_Ctx_Journal": "ジャーナル", "UVC_Ctx_TFC": "フリーカメラ",
    "UVC_Ctx_DebugMap": "デバッグマップ", "UVC_Ctx_Lockpicking": "ロックピック", "UVC_Ctx_Marketplace": "クリエイション", "UVC_Ctx_Favor": "従者への指示",
    "UVC_StatusUnbound": "{} - {} のキーを外しました。保存済み。",
    "UVC_StatusRebound": "{} - {} にキーを戻しました。保存済み。",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "このランタイムでは、このコンテキストに操作がありません。",
    "UVC_ColControl": "操作", "UVC_ColKeyboard": "キーボード", "UVC_ColMouse": "マウス", "UVC_ColGamepad": "ゲームパッド",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "この操作をそのデバイスから外すと、そのデバイスからシステムメニューを開けなくなることがあります。フレームワーク自身のキーは引き続き使えます。",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "オン = このデバイスでこの操作にキーがありません。オフにするとキーを戻します。",
    "UVC_TipUnbind": "オンにすると、このデバイスでこの操作をキーなしにします。元のキーは記憶されます。",
    "UVC_DisabledNote": "「一般」タブでMODがオフになっています。ここでの切り替えは記憶されますが、ゲーム内では何も外されません。",
    "UVC_LogLevel": "ログレベル",
    "UVC_HelpLogLevel": "すぐに適用・保存されます。ログは Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log にあります。",
    "UVC_Intro": "ゲーム本来の操作を、別の何かにキーを割り当てることなく、キーボード・マウス・ゲームパッドでキーなしにできます。切り替えはすぐに適用・保存され、元のキーは記憶されるので後で戻せます。",
    "UVC_Title": "バニラ操作のキー解除",
    "UVC_Enabled": "有効",
    "UVC_HelpEnabled": "オフにすると一覧を保ったまま全ての操作にキーを戻します。再びオンにすると選択が復元されます。",
    "UVC_Count": "キーのない操作: %d",
    "UVC_Debug": "デバッグ",
    "UVC_ReloadBtn": "INIから再読み込み",
    "UVC_StatusReloaded": "INIから設定を再読み込みして適用しました。",
    "UVC_StatusReloadFail": "INIを読めませんでした。理由はログを参照してください。",
    "UVC_HelpReload": "全ての操作にキーを戻し、ディスクのINIを読み直して、その一覧を適用します。",
    "UVC_RestoreBtn": "初期設定に戻す",
    "UVC_StatusRestored": "全ての操作にキーが戻り、一覧は空です。保存済み。",
    "UVC_HelpRestore": "全ての操作にキーを戻し、一覧を空にして保存します。",
    "UVC_Note": "ゲーム自身の操作設定メニューでは、キーのない操作は空欄で表示されます。このMODを外す場合、そこで一度「初期設定に戻す」を行うと、キーなしの間にゲームが保存した内容が消えます。",
    "UVC_LogLevel_Trace": "トレース", "UVC_LogLevel_Debug": "デバッグ", "UVC_LogLevel_Info": "情報", "UVC_LogLevel_Warning": "警告",
    "UVC_LogLevel_Error": "エラー", "UVC_LogLevel_Critical": "致命的", "UVC_LogLevel_Off": "オフ",
}

T["korean"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "게임플레이", "UVC_Ctx_MenuMode": "메뉴 (공통)", "UVC_Ctx_Console": "콘솔",
    "UVC_Ctx_ItemMenus": "아이템 메뉴", "UVC_Ctx_Inventory": "소지품", "UVC_Ctx_DebugText": "디버그 텍스트",
    "UVC_Ctx_Favorites": "즐겨찾기 메뉴", "UVC_Ctx_Map": "지도", "UVC_Ctx_Stats": "기술", "UVC_Ctx_Cursor": "커서",
    "UVC_Ctx_Book": "책", "UVC_Ctx_DebugOverlay": "디버그 오버레이", "UVC_Ctx_Journal": "일지", "UVC_Ctx_TFC": "자유 카메라",
    "UVC_Ctx_DebugMap": "디버그 지도", "UVC_Ctx_Lockpicking": "자물쇠 따기", "UVC_Ctx_Marketplace": "크리에이션", "UVC_Ctx_Favor": "동료 명령",
    "UVC_StatusUnbound": "{} - {}의 키를 해제했습니다. 저장됨.",
    "UVC_StatusRebound": "{} - {}에 키를 되돌렸습니다. 저장됨.",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "이 런타임에는 이 컨텍스트에 조작이 없습니다.",
    "UVC_ColControl": "조작", "UVC_ColKeyboard": "키보드", "UVC_ColMouse": "마우스", "UVC_ColGamepad": "게임패드",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "이 조작을 해당 장치에서 해제하면 그 장치로 시스템 메뉴를 열 방법이 없을 수 있습니다. 프레임워크 자체 키는 계속 작동합니다.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "켜짐 = 이 장치에서 이 조작에 키가 없습니다. 끄면 키를 되돌립니다.",
    "UVC_TipUnbind": "켜면 이 장치에서 이 조작을 키 없이 둡니다. 원래 키는 기억됩니다.",
    "UVC_DisabledNote": "일반 탭에서 모드가 꺼져 있습니다. 여기의 스위치는 기억되지만 게임에서는 아무것도 해제되지 않습니다.",
    "UVC_LogLevel": "로그 수준",
    "UVC_HelpLogLevel": "즉시 적용되고 저장됩니다. 로그 위치: Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "게임 고유의 조작을 다른 곳에 키를 주지 않고도 키보드, 마우스, 게임패드에서 키 없이 둘 수 있습니다. 모든 스위치는 즉시 적용되고 저장되며, 원래 키는 기억되어 되돌릴 수 있습니다.",
    "UVC_Title": "바닐라 조작 키 해제",
    "UVC_Enabled": "사용",
    "UVC_HelpEnabled": "끄면 목록은 유지한 채 모든 조작에 키를 되돌립니다. 다시 켜면 선택이 복원됩니다.",
    "UVC_Count": "키 없는 조작: %d",
    "UVC_Debug": "디버그",
    "UVC_ReloadBtn": "INI에서 다시 읽기",
    "UVC_StatusReloaded": "INI에서 설정을 다시 읽어 적용했습니다.",
    "UVC_StatusReloadFail": "INI를 읽을 수 없습니다. 이유는 로그를 보세요.",
    "UVC_HelpReload": "모든 조작에 키를 되돌리고, 디스크의 INI를 다시 읽어 그 목록을 적용합니다.",
    "UVC_RestoreBtn": "기본값 복원",
    "UVC_StatusRestored": "모든 조작에 키가 돌아왔고 목록은 비어 있습니다. 저장됨.",
    "UVC_HelpRestore": "모든 조작에 키를 되돌리고 목록을 비운 뒤 저장합니다.",
    "UVC_Note": "게임의 조작 메뉴에서는 키 없는 조작이 빈칸으로 보입니다. 이 모드를 제거한다면 그곳에서 기본값 복원을 한 번 하면 키가 없던 동안 게임이 저장한 내용이 지워집니다.",
    "UVC_LogLevel_Trace": "추적", "UVC_LogLevel_Debug": "디버그", "UVC_LogLevel_Info": "정보", "UVC_LogLevel_Warning": "경고",
    "UVC_LogLevel_Error": "오류", "UVC_LogLevel_Critical": "치명적", "UVC_LogLevel_Off": "끔",
}

T["chinese"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "游戏操作", "UVC_Ctx_MenuMode": "菜单（通用）", "UVC_Ctx_Console": "控制台",
    "UVC_Ctx_ItemMenus": "物品菜单", "UVC_Ctx_Inventory": "物品栏", "UVC_Ctx_DebugText": "调试文本",
    "UVC_Ctx_Favorites": "收藏菜单", "UVC_Ctx_Map": "地图", "UVC_Ctx_Stats": "技能", "UVC_Ctx_Cursor": "光标",
    "UVC_Ctx_Book": "书籍", "UVC_Ctx_DebugOverlay": "调试叠加层", "UVC_Ctx_Journal": "日志", "UVC_Ctx_TFC": "自由视角",
    "UVC_Ctx_DebugMap": "调试地图", "UVC_Ctx_Lockpicking": "开锁", "UVC_Ctx_Marketplace": "创意内容", "UVC_Ctx_Favor": "随从指令",
    "UVC_StatusUnbound": "{} - {} 已解除按键。已保存。",
    "UVC_StatusRebound": "{} - {} 已恢复按键。已保存。",
    "UVC_StatusRefused": "{} - {}：{}",
    "UVC_NoRows": "在此运行时，该情境下没有任何操作。",
    "UVC_ColControl": "操作", "UVC_ColKeyboard": "键盘", "UVC_ColMouse": "鼠标", "UVC_ColGamepad": "手柄",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "在某设备上解除此操作后，可能无法再从该设备打开系统菜单。框架自身的按键仍然可用。",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "开 = 此操作在此设备上没有按键。关闭即恢复其按键。",
    "UVC_TipUnbind": "开启后，此操作在此设备上将没有按键。原有按键会被记住。",
    "UVC_DisabledNote": "本模组已在“常规”页关闭：这里的开关会被记住，但游戏中不会解除任何按键。",
    "UVC_LogLevel": "日志级别",
    "UVC_HelpLogLevel": "立即生效并保存。日志位于 Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log。",
    "UVC_Intro": "可将游戏自带的任何操作在键盘、鼠标或手柄上设为无按键，而无需把该键分配给别的东西。每个开关立即生效并保存；原有按键会被记住，可随时恢复。",
    "UVC_Title": "解除原版操作按键",
    "UVC_Enabled": "启用",
    "UVC_HelpEnabled": "关闭时保留列表并恢复所有操作的按键；再次开启即还原你的选择。",
    "UVC_Count": "无按键的操作：%d",
    "UVC_Debug": "调试",
    "UVC_ReloadBtn": "从 INI 重新载入",
    "UVC_StatusReloaded": "已从 INI 重新载入设置并应用。",
    "UVC_StatusReloadFail": "无法读取 INI。原因见日志。",
    "UVC_HelpReload": "恢复所有操作的按键，从磁盘重新读取 INI，并应用其中的列表。",
    "UVC_RestoreBtn": "恢复默认",
    "UVC_StatusRestored": "所有操作已恢复按键，列表已清空。已保存。",
    "UVC_HelpRestore": "恢复所有操作的按键，清空列表并保存。",
    "UVC_Note": "游戏自带的操作菜单会把无按键的操作显示为空白。若移除本模组，在那里执行一次“恢复默认”即可清除操作无按键期间游戏保存的内容。",
    "UVC_LogLevel_Trace": "跟踪", "UVC_LogLevel_Debug": "调试", "UVC_LogLevel_Info": "信息", "UVC_LogLevel_Warning": "警告",
    "UVC_LogLevel_Error": "错误", "UVC_LogLevel_Critical": "严重", "UVC_LogLevel_Off": "关闭",
}

T["russian"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "Игровой процесс", "UVC_Ctx_MenuMode": "Меню (общие)", "UVC_Ctx_Console": "Консоль",
    "UVC_Ctx_ItemMenus": "Меню предметов", "UVC_Ctx_Inventory": "Инвентарь", "UVC_Ctx_DebugText": "Отладочный текст",
    "UVC_Ctx_Favorites": "Меню избранного", "UVC_Ctx_Map": "Карта", "UVC_Ctx_Stats": "Навыки", "UVC_Ctx_Cursor": "Курсор",
    "UVC_Ctx_Book": "Книги", "UVC_Ctx_DebugOverlay": "Отладочный оверлей", "UVC_Ctx_Journal": "Журнал", "UVC_Ctx_TFC": "Свободная камера",
    "UVC_Ctx_DebugMap": "Отладочная карта", "UVC_Ctx_Lockpicking": "Взлом замков", "UVC_Ctx_Marketplace": "Creations", "UVC_Ctx_Favor": "Команды спутнику",
    "UVC_StatusUnbound": "{} - {}: клавиша снята. Сохранено.",
    "UVC_StatusRebound": "{} - {}: клавиша возвращена. Сохранено.",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "В этой сборке игры в данном контексте нет элементов управления.",
    "UVC_ColControl": "Действие", "UVC_ColKeyboard": "Клавиатура", "UVC_ColMouse": "Мышь", "UVC_ColGamepad": "Геймпад",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "Без этого действия на устройстве может не остаться способа открыть системное меню с этого устройства. Собственная клавиша фреймворка продолжит работать.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "Вкл = у этого действия нет клавиши на этом устройстве. Выключите, чтобы вернуть клавишу.",
    "UVC_TipUnbind": "Включите, чтобы оставить это действие без клавиши на этом устройстве. Прежняя клавиша запоминается.",
    "UVC_DisabledNote": "Мод выключен на вкладке «Общие»: переключатели здесь запоминаются, но в игре ничего не снимается.",
    "UVC_LogLevel": "Уровень журнала",
    "UVC_HelpLogLevel": "Применяется и сохраняется сразу. Журнал: Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "Оставьте любое стандартное действие игры без клавиши на клавиатуре, мыши или геймпаде, не назначая эту клавишу чему-то ещё. Каждый переключатель применяется и сохраняется сразу; прежняя клавиша запоминается и может быть возвращена.",
    "UVC_Title": "Снятие стандартных клавиш",
    "UVC_Enabled": "Включено",
    "UVC_HelpEnabled": "Выкл возвращает клавиши всем действиям из списка, сохраняя сам список; включение снова восстанавливает ваш выбор.",
    "UVC_Count": "Действий без клавиши: %d",
    "UVC_Debug": "Отладка",
    "UVC_ReloadBtn": "Перечитать INI",
    "UVC_StatusReloaded": "Настройки перечитаны из INI и применены.",
    "UVC_StatusReloadFail": "Не удалось прочитать INI. Причина в журнале.",
    "UVC_HelpReload": "Возвращает клавиши всем действиям, перечитывает INI с диска и применяет его список.",
    "UVC_RestoreBtn": "Сбросить настройки",
    "UVC_StatusRestored": "Все действия получили клавиши обратно, список пуст. Сохранено.",
    "UVC_HelpRestore": "Возвращает клавиши всем действиям, очищает список и сохраняет.",
    "UVC_Note": "В меню управления самой игры действие без клавиши показывается пустым. Если вы удалите мод, один сброс настроек там очистит всё, что игра сохранила, пока действие было без клавиши.",
    "UVC_LogLevel_Trace": "Трассировка", "UVC_LogLevel_Debug": "Отладка", "UVC_LogLevel_Info": "Информация", "UVC_LogLevel_Warning": "Предупреждение",
    "UVC_LogLevel_Error": "Ошибка", "UVC_LogLevel_Critical": "Критический", "UVC_LogLevel_Off": "Выкл",
}

T["german"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "Spiel", "UVC_Ctx_MenuMode": "Menüs (allgemein)", "UVC_Ctx_Console": "Konsole",
    "UVC_Ctx_ItemMenus": "Gegenstandsmenüs", "UVC_Ctx_Inventory": "Inventar", "UVC_Ctx_DebugText": "Debug-Text",
    "UVC_Ctx_Favorites": "Favoritenmenü", "UVC_Ctx_Map": "Karte", "UVC_Ctx_Stats": "Fertigkeiten", "UVC_Ctx_Cursor": "Cursor",
    "UVC_Ctx_Book": "Bücher", "UVC_Ctx_DebugOverlay": "Debug-Overlay", "UVC_Ctx_Journal": "Tagebuch", "UVC_Ctx_TFC": "Freie Kamera",
    "UVC_Ctx_DebugMap": "Debug-Karte", "UVC_Ctx_Lockpicking": "Schlossknacken", "UVC_Ctx_Marketplace": "Creations", "UVC_Ctx_Favor": "Gefährtenbefehle",
    "UVC_StatusUnbound": "{} - {} hat jetzt keine Taste. Gespeichert.",
    "UVC_StatusRebound": "{} - {} hat seine Taste zurück. Gespeichert.",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "Das Spiel hat in diesem Kontext auf dieser Version keine Steuerung.",
    "UVC_ColControl": "Steuerung", "UVC_ColKeyboard": "Tastatur", "UVC_ColMouse": "Maus", "UVC_ColGamepad": "Gamepad",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "Ohne diese Steuerung auf einem Gerät gibt es von diesem Gerät aus womöglich keinen Weg mehr ins Systemmenü. Die eigene Taste des Frameworks funktioniert weiterhin.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "An = diese Steuerung hat auf diesem Gerät keine Taste. Ausschalten gibt ihr die Taste zurück.",
    "UVC_TipUnbind": "Einschalten lässt diese Steuerung auf diesem Gerät ohne Taste. Die bisherige Taste wird gemerkt.",
    "UVC_DisabledNote": "Die Mod ist auf dem Reiter Allgemein ausgeschaltet: Schalter hier werden gemerkt, aber im Spiel wird nichts gelöst.",
    "UVC_LogLevel": "Protokollstufe",
    "UVC_HelpLogLevel": "Gilt sofort und wird gespeichert. Das Protokoll liegt unter Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "Lasse jede Steuerung des Spiels auf Tastatur, Maus oder Gamepad OHNE Taste, ohne die Taste an etwas anderes zu vergeben. Jeder Schalter gilt sofort und wird gespeichert; die bisherige Taste wird gemerkt und kann zurückgegeben werden.",
    "UVC_Title": "Vanilla-Steuerung lösen",
    "UVC_Enabled": "Aktiv",
    "UVC_HelpEnabled": "Aus gibt jeder gelisteten Steuerung ihre Taste zurück und behält die Liste; erneutes Einschalten stellt deine Auswahl wieder her.",
    "UVC_Count": "Steuerungen ohne Taste: %d",
    "UVC_Debug": "Debug",
    "UVC_ReloadBtn": "Aus INI neu laden",
    "UVC_StatusReloaded": "Einstellungen aus der INI neu geladen und angewendet.",
    "UVC_StatusReloadFail": "Die INI konnte nicht gelesen werden. Der Grund steht im Protokoll.",
    "UVC_HelpReload": "Gibt jeder Steuerung ihre Taste zurück, liest die INI neu von der Platte und wendet ihre Liste an.",
    "UVC_RestoreBtn": "Standard wiederherstellen",
    "UVC_StatusRestored": "Jede Steuerung hat ihre Taste zurück und die Liste ist leer. Gespeichert.",
    "UVC_HelpRestore": "Gibt jeder Steuerung ihre Taste zurück, leert die Liste und speichert.",
    "UVC_Note": "Das Steuerungsmenü des Spiels zeigt eine gelöste Steuerung ohne Taste. Wenn du diese Mod entfernst, räumt dort ein einmaliges Zurücksetzen auf Standard auf, was das Spiel gespeichert hat, während eine Steuerung keine Taste hatte.",
    "UVC_LogLevel_Trace": "Trace", "UVC_LogLevel_Debug": "Debug", "UVC_LogLevel_Info": "Info", "UVC_LogLevel_Warning": "Warnung",
    "UVC_LogLevel_Error": "Fehler", "UVC_LogLevel_Critical": "Kritisch", "UVC_LogLevel_Off": "Aus",
}

T["french"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "Jeu", "UVC_Ctx_MenuMode": "Menus (général)", "UVC_Ctx_Console": "Console",
    "UVC_Ctx_ItemMenus": "Menus d'objets", "UVC_Ctx_Inventory": "Inventaire", "UVC_Ctx_DebugText": "Texte de débogage",
    "UVC_Ctx_Favorites": "Menu des favoris", "UVC_Ctx_Map": "Carte", "UVC_Ctx_Stats": "Compétences", "UVC_Ctx_Cursor": "Curseur",
    "UVC_Ctx_Book": "Livres", "UVC_Ctx_DebugOverlay": "Surcouche de débogage", "UVC_Ctx_Journal": "Journal", "UVC_Ctx_TFC": "Caméra libre",
    "UVC_Ctx_DebugMap": "Carte de débogage", "UVC_Ctx_Lockpicking": "Crochetage", "UVC_Ctx_Marketplace": "Creations", "UVC_Ctx_Favor": "Ordres au compagnon",
    "UVC_StatusUnbound": "{} - {} n'a plus de touche. Enregistré.",
    "UVC_StatusRebound": "{} - {} a retrouvé sa touche. Enregistré.",
    "UVC_StatusRefused": "{} - {} : {}",
    "UVC_NoRows": "Le jeu n'a aucune commande dans ce contexte sur cette version.",
    "UVC_ColControl": "Commande", "UVC_ColKeyboard": "Clavier", "UVC_ColMouse": "Souris", "UVC_ColGamepad": "Manette",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "Sans cette commande sur un périphérique, il peut ne plus y avoir de moyen d'ouvrir le menu système depuis ce périphérique. La touche du framework continue de fonctionner.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "Activé = cette commande n'a pas de touche sur ce périphérique. Désactivez pour lui rendre sa touche.",
    "UVC_TipUnbind": "Activez pour laisser cette commande sans touche sur ce périphérique. La touche qu'elle avait est mémorisée.",
    "UVC_DisabledNote": "Le mod est désactivé dans l'onglet Général : les interrupteurs ici sont mémorisés mais rien n'est délié dans le jeu.",
    "UVC_LogLevel": "Niveau de journal",
    "UVC_HelpLogLevel": "S'applique et s'enregistre immédiatement. Le journal est dans Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "Laissez n'importe quelle commande du jeu SANS touche au clavier, à la souris ou à la manette, sans donner cette touche à autre chose. Chaque interrupteur s'applique et s'enregistre aussitôt ; la touche d'origine est mémorisée pour pouvoir être rendue.",
    "UVC_Title": "Délier les commandes vanilla",
    "UVC_Enabled": "Activé",
    "UVC_HelpEnabled": "Désactivé rend sa touche à chaque commande listée en gardant la liste ; réactiver restaure vos choix.",
    "UVC_Count": "Commandes sans touche : %d",
    "UVC_Debug": "Débogage",
    "UVC_ReloadBtn": "Recharger depuis l'INI",
    "UVC_StatusReloaded": "Réglages rechargés depuis l'INI et appliqués.",
    "UVC_StatusReloadFail": "Impossible de lire l'INI. Voir le journal.",
    "UVC_HelpReload": "Rend sa touche à chaque commande, relit l'INI sur le disque et applique sa liste.",
    "UVC_RestoreBtn": "Rétablir les valeurs par défaut",
    "UVC_StatusRestored": "Chaque commande a retrouvé sa touche et la liste est vide. Enregistré.",
    "UVC_HelpRestore": "Rend sa touche à chaque commande, vide la liste et enregistre.",
    "UVC_Note": "Le menu des commandes du jeu affiche une commande déliée sans touche. Si vous retirez ce mod, une réinitialisation par défaut dans ce menu efface ce que le jeu a enregistré pendant qu'une commande n'avait pas de touche.",
    "UVC_LogLevel_Trace": "Trace", "UVC_LogLevel_Debug": "Débogage", "UVC_LogLevel_Info": "Info", "UVC_LogLevel_Warning": "Avertissement",
    "UVC_LogLevel_Error": "Erreur", "UVC_LogLevel_Critical": "Critique", "UVC_LogLevel_Off": "Désactivé",
}

T["spanish"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "Juego", "UVC_Ctx_MenuMode": "Menús (general)", "UVC_Ctx_Console": "Consola",
    "UVC_Ctx_ItemMenus": "Menús de objetos", "UVC_Ctx_Inventory": "Inventario", "UVC_Ctx_DebugText": "Texto de depuración",
    "UVC_Ctx_Favorites": "Menú de favoritos", "UVC_Ctx_Map": "Mapa", "UVC_Ctx_Stats": "Habilidades", "UVC_Ctx_Cursor": "Cursor",
    "UVC_Ctx_Book": "Libros", "UVC_Ctx_DebugOverlay": "Superposición de depuración", "UVC_Ctx_Journal": "Diario", "UVC_Ctx_TFC": "Cámara libre",
    "UVC_Ctx_DebugMap": "Mapa de depuración", "UVC_Ctx_Lockpicking": "Forzar cerraduras", "UVC_Ctx_Marketplace": "Creations", "UVC_Ctx_Favor": "Órdenes al seguidor",
    "UVC_StatusUnbound": "{} - {} ya no tiene tecla. Guardado.",
    "UVC_StatusRebound": "{} - {} recuperó su tecla. Guardado.",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "El juego no tiene controles en este contexto en esta versión.",
    "UVC_ColControl": "Control", "UVC_ColKeyboard": "Teclado", "UVC_ColMouse": "Ratón", "UVC_ColGamepad": "Mando",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "Sin este control en un dispositivo puede no quedar forma de abrir el menú del sistema desde ese dispositivo. La tecla propia del framework sigue funcionando.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "Activado = este control no tiene tecla en este dispositivo. Desactívalo para devolvérsela.",
    "UVC_TipUnbind": "Actívalo para dejar este control sin tecla en este dispositivo. La tecla que tenía se recuerda.",
    "UVC_DisabledNote": "El mod está desactivado en la pestaña General: los interruptores de aquí se recuerdan, pero nada se desvincula en el juego.",
    "UVC_LogLevel": "Nivel de registro",
    "UVC_HelpLogLevel": "Se aplica y se guarda al instante. El registro está en Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "Deja cualquier control propio del juego SIN tecla en el teclado, el ratón o el mando, sin dar esa tecla a otra cosa. Cada interruptor se aplica y se guarda al momento; la tecla original se recuerda para poder devolverla.",
    "UVC_Title": "Desvincular controles vanilla",
    "UVC_Enabled": "Activado",
    "UVC_HelpEnabled": "Desactivado devuelve su tecla a cada control de la lista conservando la lista; volver a activar restaura tus elecciones.",
    "UVC_Count": "Controles sin tecla: %d",
    "UVC_Debug": "Depuración",
    "UVC_ReloadBtn": "Recargar desde el INI",
    "UVC_StatusReloaded": "Ajustes recargados desde el INI y aplicados.",
    "UVC_StatusReloadFail": "No se pudo leer el INI. Consulta el registro.",
    "UVC_HelpReload": "Devuelve su tecla a cada control, vuelve a leer el INI del disco y aplica su lista.",
    "UVC_RestoreBtn": "Restaurar valores predeterminados",
    "UVC_StatusRestored": "Cada control recuperó su tecla y la lista está vacía. Guardado.",
    "UVC_HelpRestore": "Devuelve su tecla a cada control, vacía la lista y guarda.",
    "UVC_Note": "El menú de controles del juego muestra un control desvinculado sin tecla. Si quitas este mod, un solo Restablecer valores predeterminados allí limpia lo que el juego guardó mientras un control no tenía tecla.",
    "UVC_LogLevel_Trace": "Traza", "UVC_LogLevel_Debug": "Depuración", "UVC_LogLevel_Info": "Información", "UVC_LogLevel_Warning": "Aviso",
    "UVC_LogLevel_Error": "Error", "UVC_LogLevel_Critical": "Crítico", "UVC_LogLevel_Off": "Desactivado",
}

T["italian"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "Gioco", "UVC_Ctx_MenuMode": "Menu (generale)", "UVC_Ctx_Console": "Console",
    "UVC_Ctx_ItemMenus": "Menu oggetti", "UVC_Ctx_Inventory": "Inventario", "UVC_Ctx_DebugText": "Testo di debug",
    "UVC_Ctx_Favorites": "Menu preferiti", "UVC_Ctx_Map": "Mappa", "UVC_Ctx_Stats": "Abilità", "UVC_Ctx_Cursor": "Cursore",
    "UVC_Ctx_Book": "Libri", "UVC_Ctx_DebugOverlay": "Overlay di debug", "UVC_Ctx_Journal": "Diario", "UVC_Ctx_TFC": "Telecamera libera",
    "UVC_Ctx_DebugMap": "Mappa di debug", "UVC_Ctx_Lockpicking": "Scasso", "UVC_Ctx_Marketplace": "Creations", "UVC_Ctx_Favor": "Ordini al seguace",
    "UVC_StatusUnbound": "{} - {} non ha più un tasto. Salvato.",
    "UVC_StatusRebound": "{} - {} ha riavuto il suo tasto. Salvato.",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "Il gioco non ha comandi in questo contesto su questa versione.",
    "UVC_ColControl": "Comando", "UVC_ColKeyboard": "Tastiera", "UVC_ColMouse": "Mouse", "UVC_ColGamepad": "Gamepad",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "Senza questo comando su un dispositivo potrebbe non esserci modo di aprire il menu di sistema da quel dispositivo. Il tasto del framework continua a funzionare.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "Attivo = questo comando non ha un tasto su questo dispositivo. Disattiva per restituirgli il tasto.",
    "UVC_TipUnbind": "Attiva per lasciare questo comando senza tasto su questo dispositivo. Il tasto che aveva viene ricordato.",
    "UVC_DisabledNote": "La mod è disattivata nella scheda Generale: gli interruttori qui vengono ricordati ma nel gioco nulla viene slegato.",
    "UVC_LogLevel": "Livello di log",
    "UVC_HelpLogLevel": "Si applica e si salva subito. Il log è in Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "Lascia qualsiasi comando del gioco SENZA tasto su tastiera, mouse o gamepad, senza assegnare quel tasto ad altro. Ogni interruttore si applica e si salva subito; il tasto originale viene ricordato per poterlo restituire.",
    "UVC_Title": "Slega i comandi vanilla",
    "UVC_Enabled": "Attivo",
    "UVC_HelpEnabled": "Disattivo restituisce il tasto a ogni comando elencato mantenendo l'elenco; riattivare ripristina le tue scelte.",
    "UVC_Count": "Comandi senza tasto: %d",
    "UVC_Debug": "Debug",
    "UVC_ReloadBtn": "Ricarica dall'INI",
    "UVC_StatusReloaded": "Impostazioni ricaricate dall'INI e applicate.",
    "UVC_StatusReloadFail": "Impossibile leggere l'INI. Vedi il log.",
    "UVC_HelpReload": "Restituisce il tasto a ogni comando, rilegge l'INI dal disco e applica il suo elenco.",
    "UVC_RestoreBtn": "Ripristina predefiniti",
    "UVC_StatusRestored": "Ogni comando ha riavuto il suo tasto e l'elenco è vuoto. Salvato.",
    "UVC_HelpRestore": "Restituisce il tasto a ogni comando, svuota l'elenco e salva.",
    "UVC_Note": "Il menu comandi del gioco mostra un comando slegato senza tasto. Se rimuovi questa mod, un Ripristina predefiniti lì cancella ciò che il gioco ha salvato mentre un comando non aveva tasto.",
    "UVC_LogLevel_Trace": "Traccia", "UVC_LogLevel_Debug": "Debug", "UVC_LogLevel_Info": "Info", "UVC_LogLevel_Warning": "Avviso",
    "UVC_LogLevel_Error": "Errore", "UVC_LogLevel_Critical": "Critico", "UVC_LogLevel_Off": "Spento",
}

T["polish"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "Rozgrywka", "UVC_Ctx_MenuMode": "Menu (ogólne)", "UVC_Ctx_Console": "Konsola",
    "UVC_Ctx_ItemMenus": "Menu przedmiotów", "UVC_Ctx_Inventory": "Ekwipunek", "UVC_Ctx_DebugText": "Tekst debugowania",
    "UVC_Ctx_Favorites": "Menu ulubionych", "UVC_Ctx_Map": "Mapa", "UVC_Ctx_Stats": "Umiejętności", "UVC_Ctx_Cursor": "Kursor",
    "UVC_Ctx_Book": "Książki", "UVC_Ctx_DebugOverlay": "Nakładka debugowania", "UVC_Ctx_Journal": "Dziennik", "UVC_Ctx_TFC": "Wolna kamera",
    "UVC_Ctx_DebugMap": "Mapa debugowania", "UVC_Ctx_Lockpicking": "Otwieranie zamków", "UVC_Ctx_Marketplace": "Creations", "UVC_Ctx_Favor": "Rozkazy dla towarzysza",
    "UVC_StatusUnbound": "{} - {} nie ma już klawisza. Zapisano.",
    "UVC_StatusRebound": "{} - {} odzyskał klawisz. Zapisano.",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "Gra nie ma sterowania w tym kontekście w tej wersji.",
    "UVC_ColControl": "Sterowanie", "UVC_ColKeyboard": "Klawiatura", "UVC_ColMouse": "Mysz", "UVC_ColGamepad": "Pad",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "Bez tego sterowania na urządzeniu może nie być sposobu, by otworzyć menu systemowe z tego urządzenia. Własny klawisz frameworka nadal działa.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "Wł. = to sterowanie nie ma klawisza na tym urządzeniu. Wyłącz, aby przywrócić klawisz.",
    "UVC_TipUnbind": "Włącz, aby zostawić to sterowanie bez klawisza na tym urządzeniu. Dotychczasowy klawisz zostaje zapamiętany.",
    "UVC_DisabledNote": "Mod jest wyłączony na karcie Ogólne: przełączniki tutaj są zapamiętywane, ale w grze nic nie jest odpinane.",
    "UVC_LogLevel": "Poziom dziennika",
    "UVC_HelpLogLevel": "Działa i zapisuje się od razu. Dziennik: Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "Zostaw dowolne sterowanie gry BEZ klawisza na klawiaturze, myszy lub padzie, nie przypisując tego klawisza niczemu innemu. Każdy przełącznik działa i zapisuje się od razu; dotychczasowy klawisz jest zapamiętany i można go przywrócić.",
    "UVC_Title": "Odepnij sterowanie vanilla",
    "UVC_Enabled": "Włączone",
    "UVC_HelpEnabled": "Wył. przywraca klawisz każdemu sterowaniu z listy, zachowując listę; ponowne włączenie odtwarza twoje wybory.",
    "UVC_Count": "Sterowania bez klawisza: %d",
    "UVC_Debug": "Debugowanie",
    "UVC_ReloadBtn": "Wczytaj ponownie z INI",
    "UVC_StatusReloaded": "Ustawienia wczytane ponownie z INI i zastosowane.",
    "UVC_StatusReloadFail": "Nie udało się odczytać INI. Powód w dzienniku.",
    "UVC_HelpReload": "Przywraca klawisz każdemu sterowaniu, odczytuje INI z dysku i stosuje jego listę.",
    "UVC_RestoreBtn": "Przywróć domyślne",
    "UVC_StatusRestored": "Każde sterowanie odzyskało klawisz, a lista jest pusta. Zapisano.",
    "UVC_HelpRestore": "Przywraca klawisz każdemu sterowaniu, opróżnia listę i zapisuje.",
    "UVC_Note": "Menu sterowania gry pokazuje odpięte sterowanie bez klawisza. Jeśli usuniesz ten mod, jedno Przywróć domyślne tam czyści to, co gra zapisała, gdy sterowanie nie miało klawisza.",
    "UVC_LogLevel_Trace": "Śledzenie", "UVC_LogLevel_Debug": "Debugowanie", "UVC_LogLevel_Info": "Informacje", "UVC_LogLevel_Warning": "Ostrzeżenie",
    "UVC_LogLevel_Error": "Błąd", "UVC_LogLevel_Critical": "Krytyczny", "UVC_LogLevel_Off": "Wyłączone",
}

T["czech"] = {
    "UVC_HelpMark": "(?)",
    "UVC_Ctx_Gameplay": "Hra", "UVC_Ctx_MenuMode": "Nabídky (obecné)", "UVC_Ctx_Console": "Konzole",
    "UVC_Ctx_ItemMenus": "Nabídky předmětů", "UVC_Ctx_Inventory": "Inventář", "UVC_Ctx_DebugText": "Ladicí text",
    "UVC_Ctx_Favorites": "Nabídka oblíbených", "UVC_Ctx_Map": "Mapa", "UVC_Ctx_Stats": "Dovednosti", "UVC_Ctx_Cursor": "Kurzor",
    "UVC_Ctx_Book": "Knihy", "UVC_Ctx_DebugOverlay": "Ladicí překryv", "UVC_Ctx_Journal": "Deník", "UVC_Ctx_TFC": "Volná kamera",
    "UVC_Ctx_DebugMap": "Ladicí mapa", "UVC_Ctx_Lockpicking": "Odemykání zámků", "UVC_Ctx_Marketplace": "Creations", "UVC_Ctx_Favor": "Příkazy společníkovi",
    "UVC_StatusUnbound": "{} - {} už nemá klávesu. Uloženo.",
    "UVC_StatusRebound": "{} - {} má svou klávesu zpět. Uloženo.",
    "UVC_StatusRefused": "{} - {}: {}",
    "UVC_NoRows": "Hra v tomto kontextu na této verzi nemá žádné ovládání.",
    "UVC_ColControl": "Ovládání", "UVC_ColKeyboard": "Klávesnice", "UVC_ColMouse": "Myš", "UVC_ColGamepad": "Gamepad",
    "UVC_WarnMark": "!",
    "UVC_WarnSystem": "Bez tohoto ovládání na zařízení nemusí zbýt způsob, jak z tohoto zařízení otevřít systémovou nabídku. Vlastní klávesa frameworku dál funguje.",
    "UVC_NoKey": "-",
    "UVC_TipRebind": "Zap. = toto ovládání nemá na tomto zařízení klávesu. Vypnutím ji vrátíte.",
    "UVC_TipUnbind": "Zapnutím necháte toto ovládání na tomto zařízení bez klávesy. Původní klávesa se zapamatuje.",
    "UVC_DisabledNote": "Mod je vypnutý na kartě Obecné: přepínače zde se pamatují, ale ve hře se nic neodpojí.",
    "UVC_LogLevel": "Úroveň protokolu",
    "UVC_HelpLogLevel": "Platí a ukládá se ihned. Protokol je v Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log.",
    "UVC_Intro": "Nechte kterékoli vlastní ovládání hry BEZ klávesy na klávesnici, myši nebo gamepadu, aniž byste tu klávesu dali něčemu jinému. Každý přepínač platí a ukládá se ihned; původní klávesa se pamatuje, aby šla vrátit.",
    "UVC_Title": "Odpojení vanilla ovládání",
    "UVC_Enabled": "Zapnuto",
    "UVC_HelpEnabled": "Vypnuto vrátí klávesu každému ovládání v seznamu a seznam zachová; opětovné zapnutí obnoví vaše volby.",
    "UVC_Count": "Ovládání bez klávesy: %d",
    "UVC_Debug": "Ladění",
    "UVC_ReloadBtn": "Znovu načíst z INI",
    "UVC_StatusReloaded": "Nastavení znovu načteno z INI a použito.",
    "UVC_StatusReloadFail": "INI se nepodařilo přečíst. Důvod je v protokolu.",
    "UVC_HelpReload": "Vrátí klávesu každému ovládání, znovu přečte INI z disku a použije jeho seznam.",
    "UVC_RestoreBtn": "Obnovit výchozí",
    "UVC_StatusRestored": "Každé ovládání má svou klávesu zpět a seznam je prázdný. Uloženo.",
    "UVC_HelpRestore": "Vrátí klávesu každému ovládání, vyprázdní seznam a uloží.",
    "UVC_Note": "Nabídka ovládání hry ukazuje odpojené ovládání bez klávesy. Pokud tento mod odstraníte, jedno Obnovit výchozí tam vyčistí, co si hra uložila, zatímco ovládání nemělo klávesu.",
    "UVC_LogLevel_Trace": "Trasování", "UVC_LogLevel_Debug": "Ladění", "UVC_LogLevel_Info": "Informace", "UVC_LogLevel_Warning": "Varování",
    "UVC_LogLevel_Error": "Chyba", "UVC_LogLevel_Critical": "Kritická", "UVC_LogLevel_Off": "Vypnuto",
}


def write_file(lang, keys, order, texts):
    out = os.path.join(REPO, "dist", "Interface", "Translations", "%s_%s.txt" % (STEM, lang))
    lines = []
    for k in order:
        t = texts[k]
        t = t.replace("\r\n", "\n").replace("\n", "\\n")
        lines.append("$%s\t%s" % (k, t))
    data = "\r\n".join(lines) + "\r\n"
    with io.open(out, "wb") as f:
        f.write(b"\xff\xfe")
        f.write(data.encode("utf-16-le"))
    return out, len(lines)


def main():
    keys, order = read_keys()
    for lang in LANGS:
        if lang == "english":
            texts = keys
        else:
            d = T[lang]
            missing = [k for k in order if k not in d]
            extra = [k for k in d if k not in keys]
            if missing or extra:
                raise RuntimeError("%s: missing %r extra %r" % (lang, missing, extra))
            texts = d
        path, n = write_file(lang, keys, order, texts)
        print("%-9s %3d keys -> %s" % (lang, n, os.path.relpath(path, REPO)))


if __name__ == "__main__":
    main()
