#!/usr/bin/env python3
"""Writes the UVCL_ keys into dist/Interface/Translations/UnbindControls_<language>.txt (since UVC 1.4.0; the other keys stay as they are) (UTF-16LE with BOM) from the table below, so the
eleven files cannot drift apart. Run it after adding a string: py -3 tools/make_translations.py"""
import pathlib

LANGS = ["english", "chinese", "czech", "french", "german", "italian", "japanese", "korean", "polish", "russian", "spanish"]

T = {
"UVCL_Title": ["Controller Layout", "手柄布局", "Rozložení ovladače", "Disposition de la manette", "Controller-Belegung", "Layout del controller", "コントローラー配置", "컨트롤러 배치", "Układ kontrolera", "Схема геймпада", "Diseño del mando"],
"UVCL_Close": ["Close", "关闭", "Zavřít", "Fermer", "Schließen", "Chiudi", "閉じる", "닫기", "Zamknij", "Закрыть", "Cerrar"],
"UVCL_More": ["more", "更多", "další", "de plus", "weitere", "altri", "件", "개 더", "więcej", "ещё", "más"],
"UVCL_Move": ["Move", "移动", "Pohyb", "Déplacement", "Bewegen", "Movimento", "移動", "이동", "Ruch", "Движение", "Moverse"],
"UVCL_Look": ["Look", "视角", "Rozhlížení", "Regard", "Umsehen", "Visuale", "視点", "시점", "Rozglądanie", "Обзор", "Mirar"],
"UVCL_Hotkey": ["Hotkey", "快捷键", "Klávesová zkratka", "Raccourci", "Schnelltaste", "Tasto rapido", "ホットキー", "단축키", "Skrót", "Горячая клавиша", "Tecla rápida"],
"UVCL_Gameplay": ["Gameplay", "游戏", "Hra", "Jeu", "Spiel", "Gioco", "ゲームプレイ", "게임플레이", "Rozgrywka", "Игра", "Juego"],
"UVCL_Swimming": ["Swimming", "游泳", "Plavání", "Nage", "Schwimmen", "Nuoto", "水泳", "수영", "Pływanie", "Плавание", "Nadar"],
"UVCL_Horseback": ["On Horseback", "骑马时", "Na koni", "À cheval", "Zu Pferd", "A cavallo", "騎乗中", "말 위에서", "Na koniu", "Верхом", "A caballo"],
"UVCL_MapContext": ["Map", "地图", "Mapa", "Carte", "Karte", "Mappa", "マップ", "지도", "Mapa", "Карта", "Mapa"],
"UVCL_Menus": ["Menus", "菜单", "Nabídky", "Menus", "Menüs", "Menu", "メニュー", "메뉴", "Menu", "Меню", "Menús"],
"UVCL_ItemMenus": ["Item Menus", "物品菜单", "Nabídky předmětů", "Menus d'objets", "Gegenstandsmenüs", "Menu oggetti", "アイテムメニュー", "아이템 메뉴", "Menu przedmiotów", "Меню предметов", "Menús de objetos"],
"UVCL_Inventory": ["Inventory", "物品栏", "Inventář", "Inventaire", "Inventar", "Inventario", "所持品", "소지품", "Ekwipunek", "Инвентарь", "Inventario"],
"UVCL_Favorites": ["Favorites", "收藏", "Oblíbené", "Favoris", "Favoriten", "Preferiti", "お気に入り", "즐겨찾기", "Ulubione", "Избранное", "Favoritos"],
"UVCL_Stats": ["Skills", "技能", "Dovednosti", "Compétences", "Fertigkeiten", "Abilità", "スキル", "기술", "Umiejętności", "Навыки", "Habilidades"],
"UVCL_Cursor": ["Cursor", "光标", "Kurzor", "Curseur", "Mauszeiger", "Cursore", "カーソル", "커서", "Kursor", "Курсор", "Cursor"],
"UVCL_Book": ["Books", "书籍", "Knihy", "Livres", "Bücher", "Libri", "本", "책", "Książki", "Книги", "Libros"],
"UVCL_Journal": ["Journal", "日志", "Deník", "Journal", "Tagebuch", "Diario", "日記", "일지", "Dziennik", "Журнал", "Diario"],
"UVCL_Lockpicking": ["Lockpicking", "开锁", "Páčení zámků", "Crochetage", "Schlossknacken", "Scasso", "開錠", "자물쇠 따기", "Otwieranie zamków", "Взлом", "Forzar cerraduras"],
"UVCL_FreeCamera": ["Free Camera", "自由镜头", "Volná kamera", "Caméra libre", "Freie Kamera", "Telecamera libera", "フリーカメラ", "자유 카메라", "Wolna kamera", "Свободная камера", "Cámara libre"],
"UVCL_Favor": ["Favors", "请求", "Laskavosti", "Services", "Gefallen", "Favori", "頼み事", "부탁", "Przysługi", "Поручения", "Favores"],
"UVCL_Cross": ["Cross", "叉", "Křížek", "Croix", "Kreuz", "Croce", "バツ", "엑스", "Krzyżyk", "Крест", "Equis"],
"UVCL_Circle": ["Circle", "圆", "Kolečko", "Rond", "Kreis", "Cerchio", "マル", "동그라미", "Kółko", "Круг", "Círculo"],
"UVCL_Square": ["Square", "方块", "Čtverec", "Carré", "Quadrat", "Quadrato", "シカク", "네모", "Kwadrat", "Квадрат", "Cuadrado"],
"UVCL_Triangle": ["Triangle", "三角", "Trojúhelník", "Triangle", "Dreieck", "Triangolo", "サンカク", "세모", "Trójkąt", "Треугольник", "Triángulo"],
"UVCL_DUp": ["D-pad up", "方向键上", "D-pad nahoru", "Croix haut", "Steuerkreuz oben", "Croce su", "方向キー上", "방향 패드 위", "D-pad w górę", "Крестовина вверх", "Cruceta arriba"],
"UVCL_DDown": ["D-pad down", "方向键下", "D-pad dolů", "Croix bas", "Steuerkreuz unten", "Croce giù", "方向キー下", "방향 패드 아래", "D-pad w dół", "Крестовина вниз", "Cruceta abajo"],
"UVCL_DLeft": ["D-pad left", "方向键左", "D-pad vlevo", "Croix gauche", "Steuerkreuz links", "Croce sinistra", "方向キー左", "방향 패드 왼쪽", "D-pad w lewo", "Крестовина влево", "Cruceta izquierda"],
"UVCL_DRight": ["D-pad right", "方向键右", "D-pad vpravo", "Croix droite", "Steuerkreuz rechts", "Croce destra", "方向キー右", "방향 패드 오른쪽", "D-pad w prawo", "Крестовина вправо", "Cruceta derecha"],
"UVCL_Touchpad": ["Touchpad", "触控板", "Touchpad", "Pavé tactile", "Touchpad", "Touchpad", "タッチパッド", "터치패드", "Panel dotykowy", "Сенсорная панель", "Panel táctil"],
"UVCL_LeftTrackpad": ["Left trackpad", "左触控板", "Levý trackpad", "Pavé tactile gauche", "Linkes Trackpad", "Trackpad sinistro", "左トラックパッド", "왼쪽 트랙패드", "Lewy trackpad", "Левый трекпад", "Panel táctil izquierdo"],
"UVCL_RightTrackpad": ["Right trackpad", "右触控板", "Pravý trackpad", "Pavé tactile droit", "Rechtes Trackpad", "Trackpad destro", "右トラックパッド", "오른쪽 트랙패드", "Prawy trackpad", "Правый трекпад", "Panel táctil derecho"],
"UVCL_LeftHalf": ["Left half", "左半部", "Levá půlka", "Moitié gauche", "Linke Hälfte", "Metà sinistra", "左半分", "왼쪽 절반", "Lewa połowa", "Левая половина", "Mitad izquierda"],
"UVCL_RightHalf": ["Right half", "右半部", "Pravá půlka", "Moitié droite", "Rechte Hälfte", "Metà destra", "右半分", "오른쪽 절반", "Prawa połowa", "Правая половина", "Mitad derecha"],
"UVCL_PadUp": ["Up", "上", "Nahoru", "Haut", "Oben", "Su", "上", "위", "Góra", "Вверх", "Arriba"],
"UVCL_PadDown": ["Down", "下", "Dolů", "Bas", "Unten", "Giù", "下", "아래", "Dół", "Вниз", "Abajo"],
"UVCL_PadLeft": ["Left", "左", "Vlevo", "Gauche", "Links", "Sinistra", "左", "왼쪽", "Lewo", "Влево", "Izquierda"],
"UVCL_PadRight": ["Right", "右", "Vpravo", "Droite", "Rechts", "Destra", "右", "오른쪽", "Prawo", "Вправо", "Derecha"],
"UVCL_PadClick": ["Click", "按下", "Stisk", "Clic", "Klick", "Clic", "クリック", "클릭", "Kliknięcie", "Нажатие", "Clic"],
"UVCL_PadNothing": ["Nothing bound", "未绑定", "Nic nepřiřazeno", "Rien d'assigné", "Nichts belegt", "Nulla assegnato", "割り当てなし", "할당 없음", "Nic nie przypisano", "Ничего не назначено", "Nada asignado"],
"UVCL_PadNotBound": ["Not bound by the game", "游戏未绑定", "Hra nepřiřazuje", "Non assigné par le jeu", "Vom Spiel nicht belegt", "Non assegnato dal gioco", "ゲームでは未割り当て", "게임에서 할당되지 않음", "Nieprzypisane przez grę", "Игра не использует", "Sin asignar por el juego"],
"UVCL_ModeMouse": ["Mouse", "鼠标", "Myš", "Souris", "Maus", "Mouse", "マウス", "마우스", "Mysz", "Мышь", "Ratón"],
"UVCL_ModeScroll": ["Scroll", "滚动", "Posun", "Défilement", "Scrollen", "Scorrimento", "スクロール", "스크롤", "Przewijanie", "Прокрутка", "Desplazamiento"],
"UVCL_ModeJoystick": ["Joystick", "摇杆", "Joystick", "Joystick", "Joystick", "Joystick", "ジョイスティック", "조이스틱", "Joystick", "Джойстик", "Joystick"],
"UVCL_EvClick": ["Click", "点击", "Kliknout", "Cliquer", "Klicken", "Clic", "クリック", "클릭", "Kliknij", "Нажать", "Clic"],
"UVCL_EvCursor": ["Cursor", "光标", "Kurzor", "Curseur", "Mauszeiger", "Cursore", "カーソル", "커서", "Kursor", "Курсор", "Cursor"],
"UVCL_EvLeftStick": ["Left Stick", "左摇杆", "Levá páčka", "Stick gauche", "Linker Stick", "Levetta sinistra", "左スティック", "왼쪽 스틱", "Lewa gałka", "Левый стик", "Stick izquierdo"],
"UVCL_EvZoomIn": ["Zoom In", "放大", "Přiblížit", "Zoom avant", "Heranzoomen", "Ingrandisci", "ズームイン", "확대", "Przybliż", "Приблизить", "Acercar"],
"UVCL_EvZoomOut": ["Zoom Out", "缩小", "Oddálit", "Zoom arrière", "Herauszoomen", "Riduci", "ズームアウト", "축소", "Oddal", "Отдалить", "Alejar"],
"UVCL_EvItemZoom": ["Zoom Item", "放大物品", "Přiblížit předmět", "Zoom sur l'objet", "Gegenstand zoomen", "Ingrandisci oggetto", "アイテムをズーム", "아이템 확대", "Przybliż przedmiot", "Приблизить предмет", "Acercar objeto"],
"UVCL_EvRotate": ["Rotate", "旋转", "Otočit", "Pivoter", "Drehen", "Ruota", "回転", "회전", "Obróć", "Повернуть", "Girar"],
"UVCL_EvLeftEquip": ["Equip Left", "装备左手", "Vybavit vlevo", "Équiper à gauche", "Links ausrüsten", "Equipaggia a sinistra", "左手に装備", "왼손 장착", "Załóż w lewej", "Экипировать в левую", "Equipar izquierda"],
"UVCL_EvRightEquip": ["Equip Right", "装备右手", "Vybavit vpravo", "Équiper à droite", "Rechts ausrüsten", "Equipaggia a destra", "右手に装備", "오른손 장착", "Załóż w prawej", "Экипировать в правую", "Equipar derecha"],
"UVCL_EvPreviousFocus": ["Previous Focus", "上一焦点", "Předchozí výběr", "Sélection précédente", "Vorheriger Fokus", "Selezione precedente", "前のフォーカス", "이전 포커스", "Poprzedni wybór", "Предыдущий выбор", "Selección anterior"],
"UVCL_EvNextFocus": ["Next Focus", "下一焦点", "Další výběr", "Sélection suivante", "Nächster Fokus", "Selezione successiva", "次のフォーカス", "다음 포커스", "Następny wybór", "Следующий выбор", "Selección siguiente"],
"UVCL_EvTabSwitch": ["Switch Tab", "切换标签", "Přepnout kartu", "Changer d'onglet", "Reiter wechseln", "Cambia scheda", "タブ切替", "탭 전환", "Zmień kartę", "Сменить вкладку", "Cambiar pestaña"],
"UVCL_EvPrevPage": ["Previous Page", "上一页", "Předchozí strana", "Page précédente", "Vorherige Seite", "Pagina precedente", "前のページ", "이전 페이지", "Poprzednia strona", "Предыдущая страница", "Página anterior"],
"UVCL_EvNextPage": ["Next Page", "下一页", "Další strana", "Page suivante", "Nächste Seite", "Pagina successiva", "次のページ", "다음 페이지", "Następna strona", "Следующая страница", "Página siguiente"],
"UVCL_EvPageUp": ["Page Up", "向上翻页", "O stranu výš", "Page haut", "Seite hoch", "Pagina su", "ページアップ", "페이지 위로", "Strona w górę", "Страница вверх", "Página arriba"],
"UVCL_EvPageDown": ["Page Down", "向下翻页", "O stranu níž", "Page bas", "Seite runter", "Pagina giù", "ページダウン", "페이지 아래로", "Strona w dół", "Страница вниз", "Página abajo"],
"UVCL_EvPlaceMarker": ["Place Marker", "放置标记", "Umístit značku", "Placer un marqueur", "Markierung setzen", "Piazza indicatore", "マーカーを置く", "표시 놓기", "Umieść znacznik", "Поставить метку", "Colocar marcador"],
"UVCL_EvPlayerPosition": ["Player Position", "玩家位置", "Poloha hráče", "Position du joueur", "Spielerposition", "Posizione del giocatore", "プレイヤーの位置", "플레이어 위치", "Pozycja gracza", "Позиция игрока", "Posición del jugador"],
"UVCL_EvLocalMapMove": ["Move Local Map", "移动本地地图", "Posun místní mapy", "Déplacer la carte locale", "Lokale Karte bewegen", "Sposta mappa locale", "ローカルマップを移動", "지역 지도 이동", "Przesuń mapę lokalną", "Двигать местную карту", "Mover mapa local"],
"UVCL_EvMapLook": ["Map Look", "地图视角", "Rozhled po mapě", "Vue de la carte", "Kartenansicht", "Visuale mappa", "マップ視点", "지도 보기", "Widok mapy", "Обзор карты", "Vista del mapa"],
"UVCL_EvRotatePick": ["Turn Pick", "转动撬锁器", "Otočit paklíčem", "Tourner le crochet", "Dietrich drehen", "Gira il grimaldello", "ピックを回す", "자물쇠따개 돌리기", "Obróć wytrych", "Поворот отмычки", "Girar ganzúa"],
"UVCL_EvRotateLock": ["Turn Lock", "转动锁", "Otočit zámkem", "Tourner la serrure", "Schloss drehen", "Gira la serratura", "錠を回す", "자물쇠 돌리기", "Obróć zamek", "Поворот замка", "Girar cerradura"],
"UVCL_EvDebugMode": ["Debug Mode", "调试模式", "Ladicí režim", "Mode débogage", "Debug-Modus", "Modalità debug", "デバッグモード", "디버그 모드", "Tryb debugowania", "Режим отладки", "Modo de depuración"],
"UVCL_EvCameraUp": ["Camera Up", "镜头上移", "Kamera nahoru", "Caméra vers le haut", "Kamera hoch", "Telecamera su", "カメラ上", "카메라 위로", "Kamera w górę", "Камера вверх", "Cámara arriba"],
"UVCL_EvCameraDown": ["Camera Down", "镜头下移", "Kamera dolů", "Caméra vers le bas", "Kamera runter", "Telecamera giù", "カメラ下", "카메라 아래로", "Kamera w dół", "Камера вниз", "Cámara abajo"],
"UVCL_EvWorldUp": ["World Up", "世界向上", "Svět nahoru", "Monde vers le haut", "Welt hoch", "Mondo su", "ワールド上", "월드 위로", "Świat w górę", "Мир вверх", "Mundo arriba"],
"UVCL_EvWorldDown": ["World Down", "世界向下", "Svět dolů", "Monde vers le bas", "Welt runter", "Mondo giù", "ワールド下", "월드 아래로", "Świat w dół", "Мир вниз", "Mundo abajo"],
"UVCL_EvLockHeight": ["Lock Height", "锁定高度", "Zamknout výšku", "Verrouiller la hauteur", "Höhe sperren", "Blocca altezza", "高さを固定", "높이 고정", "Zablokuj wysokość", "Закрепить высоту", "Bloquear altura"],
"UVCL_EvScreenshot": ["Screenshot", "截图", "Snímek obrazovky", "Capture d'écran", "Screenshot", "Screenshot", "スクリーンショット", "스크린샷", "Zrzut ekranu", "Снимок экрана", "Captura de pantalla"],
"UVCL_EvMultiScreenshot": ["Multi-Screenshot", "连续截图", "Více snímků", "Captures multiples", "Mehrfach-Screenshot", "Screenshot multiplo", "連続スクリーンショット", "연속 스크린샷", "Wiele zrzutów", "Серия снимков", "Capturas múltiples"],
"UVCL_EvCameraPath": ["Camera Path", "镜头路径", "Dráha kamery", "Trajet de caméra", "Kamerapfad", "Percorso telecamera", "カメラパス", "카메라 경로", "Ścieżka kamery", "Путь камеры", "Ruta de cámara"],
"UVCL_EvConsole": ["Console", "控制台", "Konzole", "Console", "Konsole", "Console", "コンソール", "콘솔", "Konsola", "Консоль", "Consola"],
}

root = pathlib.Path(__file__).resolve().parent.parent.parent / "dist" / "Interface" / "Translations"
for i, lang in enumerate(LANGS):
    path = root / f"UnbindControls_{lang}.txt"
    kept = [l for l in path.read_bytes().decode("utf-16").lstrip("\ufeff").splitlines() if l and not l.startswith("$UVCL_")]
    lines = []
    for key, row in T.items():
        assert len(row) == 11, key
        lines.append(f"${key}\t{row[i]}")
    path.write_bytes(b"\xff\xfe" + ("\r\n".join(kept + lines) + "\r\n").encode("utf-16-le"))
print("wrote", len(LANGS), "files,", len(T), "UVCL keys")
