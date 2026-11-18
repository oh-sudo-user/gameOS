// gameOS front-end — translation tables and locale helpers.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"

#include <cstdio>
#include <cstring>

namespace gameos {

Lang g_lang = LANG_ZH_CN;

// Row order MUST match enum TKey exactly. Columns: zh-CN, zh-TW, ja, en,
// ru, es, ko. Format keys keep their printf specifiers in every language.
static const char* T[LANG_COUNT][T_COUNT] = {
    // --------------------------------------------------------------- zh-CN
    {
        // home
        "系统设置", "亮度 · 音量 · 电源",
        "← → 选择    Ⓐ 进入    START 设置", "%s · %zu 个游戏",
        // list
        "%zu 个游戏", " · 只看收藏", "没有收藏的游戏", "没有找到游戏",
        "在列表里按 Ⓧ 收藏 / 取消收藏",
        "请把游戏文件复制到  /userdata/roms/%s",
        "（把 TF 卡插到电脑上，打开 GAMEOS 盘符即可）", "封面",
        "↑↓ 选择  Ⓐ 启动  Ⓑ 返回  Ⓧ 收藏  Ⓨ 刷新  Ⓡ 只看收藏",
        // bluetooth
        "蓝牙", "已开启", "已关闭", "蓝牙开关", "开", "关",
        "音频", "手柄", "键盘", "鼠标", "设备",
        " · 已连接", " · 已配对",
        "还没有设备 — 按 Ⓧ 搜索附近的设备", "正在处理…",
        "Ⓐ 开关/配对/连接  Ⓧ 搜索  Ⓨ 忽略  Ⓑ 返回",
        "操作超时", "搜索完成", "蓝牙已开启", "开启失败：未找到蓝牙芯片", "蓝牙已关闭",
        "配对成功", "已连接", "已断开", "已忽略该设备", "操作失败",
        "正在关闭…", "正在开启…", "正在配对…（最长 40 秒）", "正在连接…", "正在断开…",
        "正在搜索…", "正在忽略…",
        // settings rows
        "屏幕亮度", "音量", "主题", "语言", "存档管理", "关机", "重启", "关于 gameOS",
        "%zu 个", "↑↓ 选择  ←→ 调节/切换  Ⓐ 执行  Ⓑ 返回",
        // saves
        "%zu 个存档", "还没有存档",
        "游戏里按 SELECT+Ⓛ 快速存档 / SELECT+Ⓡ 读档",
        "↑↓ 选择  Ⓐ 删除  Ⓑ 返回",
        // dialogs
        "Ⓑ 返回", "确认关机？", "确认重启？", "确认删除该存档？",
        "Ⓐ 确认        Ⓑ 取消",
        "需要 NDS BIOS 文件", "缺少：%s",
        "请把你自己的 NDS BIOS 文件复制到 TF 卡的\n"
        "bios 文件夹（GAMEOS 盘符 → bios）。\n"
        "需要 bios7.bin、bios9.bin、firmware.bin 三个文件。",
        "无法启动模拟器", "模拟器程序缺失或损坏，请重新刷写系统。",
        "gameOS 1.0\n"
        "为 Anbernic RG DS Plus 全新打造的开源游戏系统。\n"
        "NDS · GBA · GBC · PS1 · PSP\n"
        "github.com/oh-sudo-user/gameOS",
        // ambient
        "为 Anbernic RG DS Plus 打造",
        // wifi
        "无线网络", "↑↓ 选择  ←→ 切换  Ⓐ 连接  Ⓨ 搜索  Ⓧ 忘记  Ⓑ 返回",
        "没有搜索到网络 — 按 Ⓨ 重新搜索", "正在搜索附近的 WiFi…",
        "输入 WiFi 密码", "正在连接…", "已连接 · %s", "连接失败",
        "未连接", "没有检测到 WiFi 硬件", "已保存", "开放网络", "已忘记该网络",
        "空格", "删除", "确定", "取消",
        // system update
        "系统更新", "←→ 选择  Ⓐ 检查/安装/重启  Ⓑ 返回", "正在检查更新…",
        "已是最新版本", "发现新版本 %s", "下载中 %d%%", "正在写入新系统…",
        "更新已就绪，重启后生效", "更新失败：%s",
        "没有网络连接，请先在设置里连接 WiFi", "重启并应用更新",
        "当前版本 %s", "Ⓐ 检查更新",
    },
    // --------------------------------------------------------------- zh-TW
    {
        // home
        "系統設定", "亮度 · 音量 · 電源",
        "← → 選擇    Ⓐ 進入    START 設定", "%s · %zu 個遊戲",
        // list
        "%zu 個遊戲", " · 只看收藏", "沒有收藏的遊戲", "沒有找到遊戲",
        "在列表裡按 Ⓧ 收藏 / 取消收藏",
        "請把遊戲檔案複製到  /userdata/roms/%s",
        "（把 TF 卡插到電腦上，開啟 GAMEOS 磁碟機即可）", "封面",
        "↑↓ 選擇  Ⓐ 啟動  Ⓑ 返回  Ⓧ 收藏  Ⓨ 重新整理  Ⓡ 只看收藏",
        // bluetooth
        "藍牙", "已開啟", "已關閉", "藍牙開關", "開", "關",
        "音訊", "手把", "鍵盤", "滑鼠", "裝置",
        " · 已連線", " · 已配對",
        "還沒有裝置 — 按 Ⓧ 搜尋附近的裝置", "正在處理…",
        "Ⓐ 開關/配對/連線  Ⓧ 搜尋  Ⓨ 忽略  Ⓑ 返回",
        "操作逾時", "搜尋完成", "藍牙已開啟", "開啟失敗：未找到藍牙晶片", "藍牙已關閉",
        "配對成功", "已連線", "已中斷", "已忽略該裝置", "操作失敗",
        "正在關閉…", "正在開啟…", "正在配對…（最長 40 秒）", "正在連線…", "正在中斷…",
        "正在搜尋…", "正在忽略…",
        // settings rows
        "螢幕亮度", "音量", "主題", "語言", "存檔管理", "關機", "重新啟動", "關於 gameOS",
        "%zu 個", "↑↓ 選擇  ←→ 調整/切換  Ⓐ 執行  Ⓑ 返回",
        // saves
        "%zu 個存檔", "還沒有存檔",
        "遊戲裡按 SELECT+Ⓛ 快速存檔 / SELECT+Ⓡ 讀檔",
        "↑↓ 選擇  Ⓐ 刪除  Ⓑ 返回",
        // dialogs
        "Ⓑ 返回", "確認關機？", "確認重新啟動？", "確認刪除該存檔？",
        "Ⓐ 確認        Ⓑ 取消",
        "需要 NDS BIOS 檔案", "缺少：%s",
        "請把你自己的 NDS BIOS 檔案複製到 TF 卡的\n"
        "bios 資料夾（GAMEOS 磁碟機 → bios）。\n"
        "需要 bios7.bin、bios9.bin、firmware.bin 三個檔案。",
        "無法啟動模擬器", "模擬器程式遺失或損毀，請重新刷寫系統。",
        "gameOS 1.0\n"
        "為 Anbernic RG DS Plus 全新打造的開源遊戲系統。\n"
        "NDS · GBA · GBC · PS1 · PSP\n"
        "github.com/oh-sudo-user/gameOS",
        // ambient
        "為 Anbernic RG DS Plus 打造",
        // wifi
        "無線網路", "↑↓ 選擇  ←→ 切換  Ⓐ 連線  Ⓨ 搜尋  Ⓧ 忘記  Ⓑ 返回",
        "沒有搜尋到網路 — 按 Ⓨ 重新搜尋", "正在搜尋附近的 WiFi…",
        "輸入 WiFi 密碼", "正在連線…", "已連線 · %s", "連線失敗",
        "未連線", "沒有偵測到 WiFi 硬體", "已儲存", "開放網路", "已忘記該網路",
        "空格", "刪除", "確定", "取消",
        // system update
        "系統更新", "←→ 選擇  Ⓐ 檢查/安裝/重啟  Ⓑ 返回", "正在檢查更新…",
        "已是最新版本", "發現新版本 %s", "下載中 %d%%", "正在寫入新系統…",
        "更新已就緒，重啟後生效", "更新失敗：%s",
        "沒有網路連線，請先在設定裡連接 WiFi", "重啟並套用更新",
        "目前版本 %s", "Ⓐ 檢查更新",
    },
    // ------------------------------------------------------------------ ja
    {
        // home
        "システム設定", "輝度 · 音量 · 電源",
        "← → 選択    Ⓐ 決定    START 設定", "%s · ゲーム%zu本",
        // list
        "ゲーム%zu本", " · お気に入りのみ", "お気に入りのゲームがありません",
        "ゲームが見つかりません",
        "リストで Ⓧ を押してお気に入り登録/解除",
        "ゲームファイルを /userdata/roms/%s にコピーしてください",
        "（TFカードをPCに挿し、GAMEOSドライブを開いてください）", "ジャケット",
        "↑↓ 選択  Ⓐ 起動  Ⓑ 戻る  Ⓧ お気に入り  Ⓨ 更新  Ⓡ お気に入りのみ",
        // bluetooth
        "Bluetooth", "オン", "オフ", "Bluetoothスイッチ", "オン", "オフ",
        "オーディオ", "ゲームパッド", "キーボード", "マウス", "デバイス",
        " · 接続済み", " · ペアリング済み",
        "デバイスがありません — Ⓧ で近くのデバイスを検索", "処理中…",
        "Ⓐ 入切/ペア/接続  Ⓧ 検索  Ⓨ 削除  Ⓑ 戻る",
        "タイムアウト", "検索完了", "Bluetoothをオンにしました",
        "オンにできません：Bluetoothチップが見つかりません", "Bluetoothをオフにしました",
        "ペアリング成功", "接続しました", "切断しました", "デバイスを削除しました",
        "操作に失敗しました",
        "オフにしています…", "オンにしています…", "ペアリング中…（最大40秒）",
        "接続中…", "切断中…", "検索中…", "削除中…",
        // settings rows
        "画面の輝度", "音量", "テーマ", "言語", "セーブデータ管理",
        "電源オフ", "再起動", "gameOSについて",
        "%zu件", "↑↓ 選択  ←→ 調整/切替  Ⓐ 実行  Ⓑ 戻る",
        // saves
        "セーブデータ%zu件", "セーブデータがありません",
        "ゲーム中 SELECT+Ⓛ でクイックセーブ / SELECT+Ⓡ でロード",
        "↑↓ 選択  Ⓐ 削除  Ⓑ 戻る",
        // dialogs
        "Ⓑ 戻る", "電源をオフにしますか？", "再起動しますか？",
        "このセーブデータを削除しますか？", "Ⓐ 決定        Ⓑ キャンセル",
        "NDS BIOSファイルが必要です", "不足：%s",
        "自分のNDS BIOSファイルをTFカードの\n"
        "biosフォルダ（GAMEOSドライブ → bios）にコピーしてください。\n"
        "bios7.bin、bios9.bin、firmware.bin の3つが必要です。",
        "エミュレータを起動できません",
        "エミュレータが見つからないか破損しています。システムを書き直してください。",
        "gameOS 1.0\n"
        "Anbernic RG DS Plusのために作られたオープンソースのゲームシステム。\n"
        "NDS · GBA · GBC · PS1 · PSP\n"
        "github.com/oh-sudo-user/gameOS",
        // ambient
        "Anbernic RG DS Plusのために",
        // wifi
        "無線ネットワーク", "↑↓ 選択  ←→ 切替  Ⓐ 接続  Ⓨ 再検索  Ⓧ 削除  Ⓑ 戻る",
        "ネットワークが見つかりません — Ⓨ で再検索", "周辺の WiFi を検索中…",
        "WiFi パスワードを入力", "接続中…", "接続済み · %s", "接続に失敗しました",
        "未接続", "WiFi ハードウェアが見つかりません", "保存済み",
        "オープンネットワーク", "ネットワークを削除しました",
        "スペース", "削除", "OK", "キャンセル",
        // system update
        "システム更新", "←→ 選択  Ⓐ 確認/適用/再起動  Ⓑ 戻る", "アップデートを確認中…",
        "最新です", "新しいバージョン %s", "ダウンロード中 %d%%", "書き込み中…",
        "再起動で適用されます", "失敗：%s",
        "ネットワークがありません — 設定で WiFi に接続してください", "再起動して適用",
        "現在のバージョン %s", "Ⓐ 確認する",
    },
    // ------------------------------------------------------------------ en
    {
        // home
        "Settings", "Brightness · Volume · Power",
        "← → Select    Ⓐ Enter    START Settings", "%s · %zu games",
        // list
        "%zu games", " · favorites only", "No favorite games", "No games found",
        "Press Ⓧ in the list to add/remove favorites",
        "Copy game files to  /userdata/roms/%s",
        "(Insert the TF card into your PC and open the GAMEOS drive)", "Cover",
        "↑↓ Select  Ⓐ Launch  Ⓑ Back  Ⓧ Favorite  Ⓨ Refresh  Ⓡ Favorites only",
        // bluetooth
        "Bluetooth", "On", "Off", "Bluetooth switch", "On", "Off",
        "Audio", "Gamepad", "Keyboard", "Mouse", "Device",
        " · connected", " · paired",
        "No devices yet — press Ⓧ to scan nearby", "Working…",
        "Ⓐ Toggle/Pair/Connect  Ⓧ Scan  Ⓨ Forget  Ⓑ Back",
        "Operation timed out", "Scan complete", "Bluetooth turned on",
        "Failed: no Bluetooth chip found", "Bluetooth turned off",
        "Paired successfully", "Connected", "Disconnected", "Device forgotten",
        "Operation failed",
        "Turning off…", "Turning on…", "Pairing… (up to 40 s)",
        "Connecting…", "Disconnecting…", "Scanning…", "Forgetting…",
        // settings rows
        "Screen brightness", "Volume", "Theme", "Language", "Save management",
        "Power off", "Reboot", "About gameOS",
        "%zu", "↑↓ Select  ←→ Adjust/Switch  Ⓐ Apply  Ⓑ Back",
        // saves
        "%zu saves", "No saves yet",
        "In game: SELECT+Ⓛ quick save / SELECT+Ⓡ load",
        "↑↓ Select  Ⓐ Delete  Ⓑ Back",
        // dialogs
        "Ⓑ Back", "Power off?", "Reboot?", "Delete this save?",
        "Ⓐ Confirm        Ⓑ Cancel",
        "NDS BIOS files required", "Missing: %s",
        "Copy your own NDS BIOS files to the bios folder\n"
        "on the TF card (GAMEOS drive → bios).\n"
        "Needed: bios7.bin, bios9.bin, firmware.bin.",
        "Cannot start emulator",
        "The emulator is missing or corrupted. Please reflash the system.",
        "gameOS 1.0\n"
        "An open-source game system built for the Anbernic RG DS Plus.\n"
        "NDS · GBA · GBC · PS1 · PSP\n"
        "github.com/oh-sudo-user/gameOS",
        // ambient
        "Built for the Anbernic RG DS Plus",
        // wifi
        "Wireless network", "↑↓ select  ←→ switch  Ⓐ connect  Ⓨ rescan  Ⓧ forget  Ⓑ back",
        "No networks found — press Ⓨ to rescan", "Scanning for nearby WiFi…",
        "Enter WiFi password", "Connecting…", "Connected · %s", "Connection failed",
        "Not connected", "No WiFi hardware detected", "Saved", "Open network",
        "Network forgotten",
        "Space", "Delete", "OK", "Cancel",
        // system update
        "System update", "←→ select  Ⓐ check/install/restart  Ⓑ back",
        "Checking for updates…", "Up to date", "New version %s",
        "Downloading %d%%", "Writing the new system…",
        "Ready — restart to apply", "Update failed: %s",
        "No network — connect WiFi in Settings", "Restart and apply",
        "Current version %s", "Ⓐ Check for updates",
    },
    // ------------------------------------------------------------------ ru
    {
        // home
        "Настройки", "Яркость · Громкость · Питание",
        "← → Выбор    Ⓐ Войти    START Настройки", "%s · Игр: %zu",
        // list
        "Игр: %zu", " · только избранное", "Нет избранных игр", "Игры не найдены",
        "Нажмите Ⓧ в списке, чтобы добавить/убрать избранное",
        "Скопируйте файлы игр в /userdata/roms/%s",
        "(Вставьте TF-карту в ПК и откройте диск GAMEOS)", "Обложка",
        "↑↓ Выбор  Ⓐ Запуск  Ⓑ Назад  Ⓧ Избранное  Ⓨ Обновить  Ⓡ Только избранное",
        // bluetooth
        "Bluetooth", "Включено", "Выключено", "Переключатель Bluetooth", "Вкл", "Выкл",
        "Аудио", "Геймпад", "Клавиатура", "Мышь", "Устройство",
        " · подключено", " · сопряжено",
        "Устройств нет — нажмите Ⓧ для поиска", "Выполняется…",
        "Ⓐ Вкл/Сопряжение/Подключение  Ⓧ Поиск  Ⓨ Забыть  Ⓑ Назад",
        "Время операции истекло", "Поиск завершён", "Bluetooth включён",
        "Ошибка: чип Bluetooth не найден", "Bluetooth выключен",
        "Сопряжение выполнено", "Подключено", "Отключено", "Устройство удалено",
        "Операция не удалась",
        "Выключение…", "Включение…", "Сопряжение… (до 40 с)",
        "Подключение…", "Отключение…", "Поиск…", "Удаление…",
        // settings rows
        "Яркость экрана", "Громкость", "Тема", "Язык", "Управление сохранениями",
        "Выключить", "Перезагрузить", "О gameOS",
        "%zu", "↑↓ Выбор  ←→ Настройка  Ⓐ Применить  Ⓑ Назад",
        // saves
        "Сохранений: %zu", "Сохранений пока нет",
        "В игре: SELECT+Ⓛ быстрое сохранение / SELECT+Ⓡ загрузка",
        "↑↓ Выбор  Ⓐ Удалить  Ⓑ Назад",
        // dialogs
        "Ⓑ Назад", "Выключить?", "Перезагрузить?", "Удалить это сохранение?",
        "Ⓐ Подтвердить        Ⓑ Отмена",
        "Нужны файлы BIOS NDS", "Отсутствуют: %s",
        "Скопируйте свои файлы BIOS NDS в папку bios\n"
        "на TF-карте (диск GAMEOS → bios).\n"
        "Нужны: bios7.bin, bios9.bin, firmware.bin.",
        "Не удалось запустить эмулятор",
        "Эмулятор отсутствует или повреждён. Перепрошейте систему.",
        "gameOS 1.0\n"
        "Открытая игровая система для Anbernic RG DS Plus.\n"
        "NDS · GBA · GBC · PS1 · PSP\n"
        "github.com/oh-sudo-user/gameOS",
        // ambient
        "Создано для Anbernic RG DS Plus",
        // wifi
        "Беспроводная сеть", "↑↓ выбор  ←→ переключить  Ⓐ подключить  Ⓨ поиск  Ⓧ забыть  Ⓑ назад",
        "Сети не найдены — нажмите Ⓨ для поиска", "Поиск сетей WiFi…",
        "Введите пароль WiFi", "Подключение…", "Подключено · %s", "Не удалось подключиться",
        "Не подключено", "WiFi-адаптер не найден", "Сохранено", "Открытая сеть",
        "Сеть удалена",
        "Пробел", "Удалить", "ОК", "Отмена",
        // system update
        "Обновление системы", "←→ выбор  Ⓐ проверить/установить/перезагрузка  Ⓑ назад",
        "Проверка обновлений…", "Актуальная версия", "Доступна версия %s",
        "Загрузка %d%%", "Запись системы…",
        "Готово — перезагрузите", "Ошибка обновления: %s",
        "Нет сети — подключите WiFi в настройках", "Перезагрузить и применить",
        "Версия %s", "Ⓐ Проверить",
    },
    // ------------------------------------------------------------------ es
    {
        // home
        "Ajustes", "Brillo · Volumen · Energía",
        "← → Elegir    Ⓐ Entrar    START Ajustes", "%s · %zu juegos",
        // list
        "%zu juegos", " · solo favoritos", "No hay juegos favoritos",
        "No se encontraron juegos",
        "Pulsa Ⓧ en la lista para marcar/desmarcar favoritos",
        "Copia los juegos en /userdata/roms/%s",
        "(Inserta la tarjeta TF en el PC y abre la unidad GAMEOS)", "Carátula",
        "↑↓ Elegir  Ⓐ Iniciar  Ⓑ Volver  Ⓧ Favorito  Ⓨ Actualizar  Ⓡ Solo favoritos",
        // bluetooth
        "Bluetooth", "Activado", "Desactivado", "Interruptor Bluetooth", "Sí", "No",
        "Audio", "Mando", "Teclado", "Ratón", "Dispositivo",
        " · conectado", " · emparejado",
        "Sin dispositivos — pulsa Ⓧ para buscar", "Procesando…",
        "Ⓐ Activar/Emparejar/Conectar  Ⓧ Buscar  Ⓨ Olvidar  Ⓑ Volver",
        "Tiempo de espera agotado", "Búsqueda completada", "Bluetooth activado",
        "Error: no se encontró el chip Bluetooth", "Bluetooth desactivado",
        "Emparejado correctamente", "Conectado", "Desconectado",
        "Dispositivo olvidado", "Operación fallida",
        "Desactivando…", "Activando…", "Emparejando… (hasta 40 s)",
        "Conectando…", "Desconectando…", "Buscando…", "Olvidando…",
        // settings rows
        "Brillo de pantalla", "Volumen", "Tema", "Idioma", "Gestión de partidas",
        "Apagar", "Reiniciar", "Acerca de gameOS",
        "%zu", "↑↓ Elegir  ←→ Ajustar/Cambiar  Ⓐ Aplicar  Ⓑ Volver",
        // saves
        "%zu partidas", "Aún no hay partidas",
        "En el juego: SELECT+Ⓛ guardado rápido / SELECT+Ⓡ cargar",
        "↑↓ Elegir  Ⓐ Borrar  Ⓑ Volver",
        // dialogs
        "Ⓑ Volver", "¿Apagar?", "¿Reiniciar?", "¿Borrar esta partida?",
        "Ⓐ Confirmar        Ⓑ Cancelar",
        "Se necesitan archivos BIOS de NDS", "Faltan: %s",
        "Copia tus propios archivos BIOS de NDS en la carpeta bios\n"
        "de la tarjeta TF (unidad GAMEOS → bios).\n"
        "Se necesitan: bios7.bin, bios9.bin, firmware.bin.",
        "No se pudo iniciar el emulador",
        "Falta el emulador o está dañado. Vuelve a flashear el sistema.",
        "gameOS 1.0\n"
        "Un sistema de juegos de código abierto para Anbernic RG DS Plus.\n"
        "NDS · GBA · GBC · PS1 · PSP\n"
        "github.com/oh-sudo-user/gameOS",
        // ambient
        "Hecho para Anbernic RG DS Plus",
        // wifi
        "Red inalámbrica", "↑↓ elegir  ←→ cambiar  Ⓐ conectar  Ⓨ buscar  Ⓧ olvidar  Ⓑ atrás",
        "No se encontraron redes — pulsa Ⓨ para buscar", "Buscando WiFi cercano…",
        "Introduce la contraseña WiFi", "Conectando…", "Conectado · %s", "Error de conexión",
        "Sin conexión", "No se detecta hardware WiFi", "Guardada", "Red abierta",
        "Red olvidada",
        "Espacio", "Borrar", "OK", "Cancelar",
        // system update
        "Actualización", "←→ elegir  Ⓐ buscar/instalar/reiniciar  Ⓑ volver",
        "Buscando actualizaciones…", "Actualizado", "Nueva versión %s",
        "Descargando %d%%", "Escribiendo el sistema…",
        "Listo — reinicia para aplicar", "Error de actualización: %s",
        "Sin red — conecta WiFi en Ajustes", "Reiniciar y aplicar",
        "Versión actual %s", "Ⓐ Buscar actualizaciones",
    },
    // ------------------------------------------------------------------ ko
    {
        // home
        "시스템 설정", "밝기 · 볼륨 · 전원",
        "← → 선택    Ⓐ 들어가기    START 설정", "%s · 게임 %zu개",
        // list
        "게임 %zu개", " · 즐겨찾기만", "즐겨찾기한 게임이 없습니다",
        "게임을 찾을 수 없습니다",
        "목록에서 Ⓧ를 눌러 즐겨찾기 추가/해제",
        "게임 파일을 /userdata/roms/%s 에 복사하세요",
        "(TF 카드를 PC에 꽂고 GAMEOS 드라이브를 여세요)", "커버",
        "↑↓ 선택  Ⓐ 실행  Ⓑ 뒤로  Ⓧ 즐겨찾기  Ⓨ 새로고침  Ⓡ 즐겨찾기만",
        // bluetooth
        "블루투스", "켜짐", "꺼짐", "블루투스 스위치", "켜기", "끄기",
        "오디오", "게임패드", "키보드", "마우스", "장치",
        " · 연결됨", " · 페어링됨",
        "장치가 없습니다 — Ⓧ를 눌러 주변 장치 검색", "처리 중…",
        "Ⓐ 켜기/페어링/연결  Ⓧ 검색  Ⓨ 삭제  Ⓑ 뒤로",
        "작업 시간 초과", "검색 완료", "블루투스가 켜졌습니다",
        "실패: 블루투스 칩을 찾을 수 없습니다", "블루투스가 꺼졌습니다",
        "페어링 성공", "연결됨", "연결 해제됨", "장치를 삭제했습니다", "작업 실패",
        "끄는 중…", "켜는 중…", "페어링 중… (최대 40초)",
        "연결 중…", "연결 해제 중…", "검색 중…", "삭제 중…",
        // settings rows
        "화면 밝기", "볼륨", "테마", "언어", "저장 관리",
        "전원 끄기", "다시 시작", "gameOS 정보",
        "%zu개", "↑↓ 선택  ←→ 조절/전환  Ⓐ 실행  Ⓑ 뒤로",
        // saves
        "저장 %zu개", "저장 데이터가 없습니다",
        "게임 중 SELECT+Ⓛ 빠른 저장 / SELECT+Ⓡ 불러오기",
        "↑↓ 선택  Ⓐ 삭제  Ⓑ 뒤로",
        // dialogs
        "Ⓑ 뒤로", "전원을 끌까요?", "다시 시작할까요?", "이 저장 데이터를 삭제할까요?",
        "Ⓐ 확인        Ⓑ 취소",
        "NDS BIOS 파일이 필요합니다", "누락: %s",
        "자신의 NDS BIOS 파일을 TF 카드의 bios 폴더에 복사하세요\n"
        "(GAMEOS 드라이브 → bios).\n"
        "bios7.bin, bios9.bin, firmware.bin 세 파일이 필요합니다.",
        "에뮬레이터를 시작할 수 없습니다",
        "에뮬레이터가 없거나 손상되었습니다. 시스템을 다시 플래시하세요.",
        "gameOS 1.0\n"
        "Anbernic RG DS Plus를 위한 오픈 소스 게임 시스템.\n"
        "NDS · GBA · GBC · PS1 · PSP\n"
        "github.com/oh-sudo-user/gameOS",
        // ambient
        "Anbernic RG DS Plus를 위해 제작",
        // wifi
        "무선 네트워크", "↑↓ 선택  ←→ 전환  Ⓐ 연결  Ⓨ 다시 검색  Ⓧ 잊기  Ⓑ 뒤로",
        "네트워크를 찾지 못했습니다 — Ⓨ 로 다시 검색", "주변 WiFi 검색 중…",
        "WiFi 비밀번호 입력", "연결 중…", "연결됨 · %s", "연결 실패",
        "연결 안 됨", "WiFi 하드웨어를 찾지 못했습니다", "저장됨", "개방 네트워크",
        "네트워크를 잊었습니다",
        "공백", "삭제", "확인", "취소",
        // system update
        "시스템 업데이트", "←→ 선택  Ⓐ 확인/설치/재시작  Ⓑ 뒤로",
        "업데이트 확인 중…", "최신 버전", "새 버전 %s",
        "다운로드 중 %d%%", "새 시스템 기록 중…",
        "재시작하면 적용됩니다", "업데이트 실패: %s",
        "네트워크 없음 — 설정에서 WiFi를 연결하세요", "재시작하여 적용",
        "현재 버전 %s", "Ⓐ 업데이트 확인",
    },
};

const char* tr(TKey k)
{
    if (k < 0 || k >= T_COUNT) return "";
    const char* s = T[g_lang][k];
    if (!s || !*s) s = T[LANG_EN][k];   // never show a blank label
    return s ? s : "";
}

// ---------------------------------------------------------------------------
// console / theme / language names
// ---------------------------------------------------------------------------
const char* console_name(const char* id)
{
    // index by Console::id; order matches consoles() in library.cpp
    static const char* names[LANG_COUNT][5] = {
        { "任天堂 DS", "GBA 掌机", "GB / GBC", "索尼 PS1", "索尼 PSP" },
        { "任天堂 DS", "GBA 掌機", "GB / GBC", "索尼 PS1", "索尼 PSP" },
        { "ニンテンドーDS", "ゲームボーイアドバンス", "ゲームボーイ", "Sony PS1", "Sony PSP" },
        { "Nintendo DS", "Game Boy Advance", "Game Boy / GBC", "Sony PS1", "Sony PSP" },
        { "Nintendo DS", "Game Boy Advance", "Game Boy / GBC", "Sony PS1", "Sony PSP" },
        { "Nintendo DS", "Game Boy Advance", "Game Boy / GBC", "Sony PS1", "Sony PSP" },
        { "Nintendo DS", "게임보이 어드밴스", "게임보이 / GBC", "소니 PS1", "소니 PSP" },
    };
    static const char* ids[5] = { "nds", "gba", "gbc", "ps1", "psp" };
    for (int i = 0; i < 5; i++)
        if (strcmp(id, ids[i]) == 0) return names[g_lang][i];
    return id;
}

const char* theme_name(int idx)
{
    static const char* names[LANG_COUNT][4] = {
        { "默认青", "暖橙", "幻紫", "翠绿" },
        { "預設青", "暖橙", "幻紫", "翠綠" },
        { "ティール", "アンバー", "バイオレット", "グリーン" },
        { "Teal", "Amber", "Violet", "Green" },
        { "Бирюзовый", "Янтарный", "Фиолетовый", "Зелёный" },
        { "Turquesa", "Ámbar", "Violeta", "Verde" },
        { "청록", "호박색", "보라", "초록" },
    };
    if (idx < 0 || idx > 3) idx = 0;
    return names[g_lang][idx];
}

const char* lang_name(int l)
{
    // Autonyms: each language is always listed in its own script.
    static const char* names[LANG_COUNT] = {
        "简体中文", "繁體中文", "日本語", "English", "Русский", "Español", "한국어",
    };
    if (l < 0 || l >= LANG_COUNT) l = 0;
    return names[l];
}

// ---------------------------------------------------------------------------
// date formatting (ambient panel)
// ---------------------------------------------------------------------------
std::string format_date(const struct tm& t)
{
    char buf[160];
    switch (g_lang) {
    case LANG_ZH_CN:
    case LANG_ZH_TW: {
        static const char* wd[] = { "日", "一", "二", "三", "四", "五", "六" };
        snprintf(buf, sizeof(buf), "%d 年 %d 月 %d 日  星期%s",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, wd[t.tm_wday]);
        break;
    }
    case LANG_JA: {
        static const char* wd[] = { "日", "月", "火", "水", "木", "金", "土" };
        snprintf(buf, sizeof(buf), "%d年%d月%d日（%s）",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, wd[t.tm_wday]);
        break;
    }
    case LANG_KO: {
        static const char* wd[] = { "일", "월", "화", "수", "목", "금", "토" };
        snprintf(buf, sizeof(buf), "%d년 %d월 %d일 (%s)",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, wd[t.tm_wday]);
        break;
    }
    case LANG_RU: {
        static const char* wd[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
        static const char* mo[] = { "января", "февраля", "марта", "апреля",
                                    "мая", "июня", "июля", "августа",
                                    "сентября", "октября", "ноября", "декабря" };
        snprintf(buf, sizeof(buf), "%s, %d %s %d г.",
                 wd[t.tm_wday], t.tm_mday, mo[t.tm_mon], t.tm_year + 1900);
        break;
    }
    case LANG_ES: {
        static const char* wd[] = { "dom", "lun", "mar", "mié", "jue", "vie", "sáb" };
        static const char* mo[] = { "enero", "febrero", "marzo", "abril",
                                    "mayo", "junio", "julio", "agosto",
                                    "septiembre", "octubre", "noviembre", "diciembre" };
        snprintf(buf, sizeof(buf), "%s, %d de %s de %d",
                 wd[t.tm_wday], t.tm_mday, mo[t.tm_mon], t.tm_year + 1900);
        break;
    }
    case LANG_EN:
    default: {
        static const char* wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        static const char* mo[] = { "January", "February", "March", "April",
                                    "May", "June", "July", "August",
                                    "September", "October", "November", "December" };
        snprintf(buf, sizeof(buf), "%s, %s %d, %d",
                 wd[t.tm_wday], mo[t.tm_mon], t.tm_mday, t.tm_year + 1900);
        break;
    }
    }
    return buf;
}

// ---------------------------------------------------------------------------
// emulator menu languages
// ---------------------------------------------------------------------------
int retro_language()
{
    // enum retro_language from libretro.h (RetroArch 1.22.2)
    static const int codes[LANG_COUNT] = {
        12,  // RETRO_LANGUAGE_CHINESE_SIMPLIFIED
        11,  // RETRO_LANGUAGE_CHINESE_TRADITIONAL
        1,   // RETRO_LANGUAGE_JAPANESE
        0,   // RETRO_LANGUAGE_ENGLISH
        9,   // RETRO_LANGUAGE_RUSSIAN
        3,   // RETRO_LANGUAGE_SPANISH
        10,  // RETRO_LANGUAGE_KOREAN
    };
    return codes[g_lang];
}

const char* ppsspp_locale()
{
    // PPSSPP [General] Language values (assets/langregion.ini)
    static const char* codes[LANG_COUNT] = {
        "zh_CN", "zh_TW", "ja_JP", "en_US", "ru_RU", "es_ES", "ko_KR",
    };
    return codes[g_lang];
}

} // namespace gameos