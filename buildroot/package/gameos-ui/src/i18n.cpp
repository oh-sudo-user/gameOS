// gameOS front-end — translation tables and locale helpers.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.hpp"

#include <cstdio>
#include <cstring>

namespace gameos {

Lang g_lang = LANG_ZH_CN;

// Row order MUST match enum TKey exactly. Columns: zh-CN, zh-TW, ja, en,
// ru, es. Format keys keep their printf specifiers in every language.
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
        "（游戏可放在 TF1 的 GAMEOS 盘，或 TF2 卡的 roms 文件夹）", "封面",
        "↑↓ 选择  Ⓐ 启动  Ⓑ 返回  Ⓧ 收藏  Ⓨ 刷新  Ⓡ 只看收藏",
        " · TF2 卡已插入",
        // bluetooth
        "蓝牙", "已开启", "已关闭", "蓝牙开关", "开", "关",
        "音频", "手柄", "键盘", "鼠标", "设备",
        " · 已连接", " · 已配对",
        "还没有设备 — 轻触扫描添加", "正在处理…",
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
        "确认更新到 %s？", "下载后写入空闲分区，重启生效；游戏和存档不受影响。",
        // boot stages
        "初始化显示", "挂载启动分区", "准备游戏数据", "初始化网络",
        "初始化音频", "准备手柄输入", "即将进入界面",
        // wizard frame
        "首次设置", "欢迎使用 gameOS", "第 %d / %d 步",
        "Ⓐ 下一步    Ⓑ 跳过本步    START 全部跳过",
        "设置完成", "现在可以开始游戏了。\n之后可在 系统设置 里重新运行本向导。",
        "这一步需要联网，可先按 Ⓑ 跳过", "跳过全部设置",
        // connectivity test
        "测试连通性", "正在测试连通性…", "WiFi 关联", "网关路由", "域名解析",
        "互联网访问", "正常", "失败", "等待",
        // timezone
        "选择时区", "↑↓ 选择    Ⓐ 确定    Ⓑ 跳过", "亚洲", "欧洲", "美洲",
        "非洲", "大洋洲", "标准时间", "当前时间 %s",
        // machine name
        "机器名", "Ⓐ 输入    Ⓑ 跳过", "用于在网络上识别这台机器",
        "只能用字母、数字和连字符，1-63 个字符，首尾不能是连字符",
        "机器名已设为 %s",
        // retroachievements
        "RetroAchievements 账号", "登录后即可在支持的游戏中解锁成就并上传进度",
        "用户名", "密码", "硬核模式", "开启后禁用即时存档、读档和金手指",
        "登录", "正在登录…", "登录成功 · %s", "登录失败：%s",
        "已登录 · %s", "退出登录",
        // covers
        "从 libretro 封面库按游戏名匹配下载封面", "开始下载", "正在下载封面…",
        "%d / %d · 已下载 %d · 未匹配 %d", "完成：新增 %d 张，未匹配 %d 张",
        "停止", "没有需要下载封面的游戏", "游戏封面",
        "为没有封面的游戏下载封面", "%d 个游戏待补封面",
        "继续",
        // activity statistics + favorites/history
        "游玩统计", "时长 · 时间线 · 趋势 · 推荐",
        "收藏与历史", "快速找到并启动游戏",
        "概览", "时间线", "趋势", "推荐",
        "总游玩时长", "启动次数", "玩过的游戏", "今日", "近 7 天",
        "最常玩", "还没有游玩记录", "启动一个游戏，退出后就会记录在这里",
        "%d 次", "%zu 款",
        "%d 小时 %d 分", "%d 分钟", "%d 秒",
        "近 14 天游玩时长", "各机种时长", "这段时间没有游玩",
        "收藏了还没玩", "收藏 · 好久没玩", "新游戏", "好久没玩", "最近在玩",
        "没有可推荐的游戏", "先把游戏复制到 TF 卡", "%d 天没玩",
        "收藏", "历史", "还没有收藏", "在游戏列表里按 Ⓧ 收藏",
        "还没有游玩历史", "启动一个游戏后会自动记录", "文件已不在卡上", "%zu 款",
        "←→ 切换分页  ↑↓ 选择  Ⓐ 启动  Ⓧ 收藏  Ⓨ 刷新  Ⓑ 返回",
        "←→ 收藏/历史  ↑↓ 选择  Ⓐ 启动  Ⓧ 收藏  Ⓨ 刷新  Ⓑ 返回",
        "数据分区无法挂载", "未格式化，用户数据已保留。请检查存储卡或重启设备。",
        "开始游戏", "跳过", "继续游玩",
        "轻触打开 · 左右滑动切换", "上下滑动浏览 · 轻触选择",
        "拖动滑杆调节 · 轻触切换", "轻触网络连接 · 上下滑动浏览",
        "检查更新", "重新检查", "下载并安装",
        "扫描网络", "忘记", "附近没有可用网络 · 轻点扫描重试",
        "轻触选择 · 自动继续", "轻触输入机器名 · 点继续",
        "轻触开始下载 · 或点继续跳过", "轻触输入 · 点确定完成",
        "轻触城市即时生效 · 继续保留当前",
        "删除存档", "打开开关后即可搜索并连接蓝牙设备",
        "方向键选择 · Ⓐ 调整/打开 · Ⓑ 返回",
        "◀ ▶ 调节 · Ⓐ 完成 · Ⓑ 返回",
        "扫描失败，请点「扫描」重试", "Wi-Fi 服务启动失败，请重试",
        "确认忘记该连接？", "游戏启动失败",
        "模拟器异常退出。日志：TF 卡 system/retroarch.log\n最后一行：%s",
        // launch animation
        "正在读取卡带", "正在校验数据", "正在启动模拟器",
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
        "（遊戲可放在 TF1 的 GAMEOS 磁碟，或 TF2 卡的 roms 資料夾）", "封面",
        "↑↓ 選擇  Ⓐ 啟動  Ⓑ 返回  Ⓧ 收藏  Ⓨ 重新整理  Ⓡ 只看收藏",
        " · TF2 卡已插入",
        // bluetooth
        "藍牙", "已開啟", "已關閉", "藍牙開關", "開", "關",
        "音訊", "手把", "鍵盤", "滑鼠", "裝置",
        " · 已連線", " · 已配對",
        "還沒有裝置 — 輕觸掃描新增", "正在處理…",
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
        "確認更新到 %s？", "下載後寫入閒置分割區，重啟生效；遊戲和存檔不受影響。",
        // boot stages
        "初始化顯示", "掛載啟動分割區", "準備遊戲資料", "初始化網路",
        "初始化音訊", "準備手把輸入", "即將進入介面",
        // wizard frame
        "首次設定", "歡迎使用 gameOS", "第 %d / %d 步",
        "Ⓐ 下一步    Ⓑ 跳過本步    START 全部跳過",
        "設定完成", "現在可以開始遊戲了。\n之後可在 系統設定 裡重新執行本精靈。",
        "這一步需要連網，可先按 Ⓑ 跳過", "跳過全部設定",
        // connectivity test
        "測試連通性", "正在測試連通性…", "WiFi 關聯", "閘道路由", "網域名稱解析",
        "網際網路存取", "正常", "失敗", "等待",
        // timezone
        "選擇時區", "↑↓ 選擇    Ⓐ 確定    Ⓑ 跳過", "亞洲", "歐洲", "美洲",
        "非洲", "大洋洲", "標準時間", "目前時間 %s",
        // machine name
        "機器名", "Ⓐ 輸入    Ⓑ 跳過", "用於在網路上識別這台機器",
        "只能用字母、數字和連字號，1-63 個字元，首尾不能是連字號",
        "機器名已設為 %s",
        // retroachievements
        "RetroAchievements 帳號", "登入後即可在支援的遊戲中解鎖成就並上傳進度",
        "使用者名稱", "密碼", "硬核模式", "開啟後停用即時存檔、讀檔和金手指",
        "登入", "正在登入…", "登入成功 · %s", "登入失敗：%s",
        "已登入 · %s", "登出",
        // covers
        "從 libretro 封面庫按遊戲名比對下載封面", "開始下載", "正在下載封面…",
        "%d / %d · 已下載 %d · 未比對 %d", "完成：新增 %d 張，未比對 %d 張",
        "停止", "沒有需要下載封面的遊戲", "遊戲封面",
        "為沒有封面的遊戲下載封面", "%d 個遊戲待補封面",
        "繼續",
        // activity statistics + favorites/history
        "遊玩統計", "時長 · 時間線 · 趨勢 · 推薦",
        "收藏與歷史", "快速找到並啟動遊戲",
        "總覽", "時間線", "趨勢", "推薦",
        "總遊玩時長", "啟動次數", "玩過的遊戲", "今日", "近 7 天",
        "最常玩", "還沒有遊玩記錄", "啟動一個遊戲，離開後就會記錄在這裡",
        "%d 次", "%zu 款",
        "%d 小時 %d 分", "%d 分鐘", "%d 秒",
        "近 14 天遊玩時長", "各機種時長", "這段時間沒有遊玩",
        "收藏了還沒玩", "收藏 · 好久沒玩", "新遊戲", "好久沒玩", "最近在玩",
        "沒有可推薦的遊戲", "先把遊戲複製到 TF 卡", "%d 天沒玩",
        "收藏", "歷史", "還沒有收藏", "在遊戲列表裡按 Ⓧ 收藏",
        "還沒有遊玩歷史", "啟動一個遊戲後會自動記錄", "檔案已不在卡上", "%zu 款",
        "←→ 切換分頁  ↑↓ 選擇  Ⓐ 啟動  Ⓧ 收藏  Ⓨ 重新整理  Ⓑ 返回",
        "←→ 收藏/歷史  ↑↓ 選擇  Ⓐ 啟動  Ⓧ 收藏  Ⓨ 重新整理  Ⓑ 返回",
        "資料分割區無法掛載", "未格式化，使用者資料已保留。請檢查記憶卡或重新啟動裝置。",
        "開始遊戲", "跳過", "繼續遊玩",
        "輕觸開啟 · 左右滑動切換", "上下滑動瀏覽 · 輕觸選擇",
        "拖動滑桿調節 · 輕觸切換", "輕觸網絡連線 · 上下滑動瀏覽",
        "檢查更新", "重新檢查", "下載並安裝",
        "掃描網絡", "忘記", "附近沒有可用網絡 · 輕點掃描重試",
        "輕觸選擇 · 自動繼續", "輕觸輸入機器名 · 點繼續",
        "輕觸開始下載 · 或點繼續跳過", "輕觸輸入 · 點確定完成",
        "輕觸城市即時生效 · 繼續保留目前",
        "刪除存檔", "開啟開關後即可搜尋並連接藍牙裝置",
        "方向鍵選擇 · Ⓐ 調整/開啟 · Ⓑ 返回",
        "◀ ▶ 調整 · Ⓐ 完成 · Ⓑ 返回",
        "掃描失敗，請點「掃描」重試", "Wi-Fi 服務啟動失敗，請重試",
        "確認忘記此連線？", "遊戲啟動失敗",
        "模擬器異常退出。日誌：TF 卡 system/retroarch.log\n最後一行：%s",
        // launch animation
        "正在讀取卡帶", "正在驗證資料", "正在啟動模擬器",
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
        "（ゲームは TF1 の GAMEOS ドライブ、または TF2 カードの roms フォルダへ）", "ジャケット",
        "↑↓ 選択  Ⓐ 起動  Ⓑ 戻る  Ⓧ お気に入り  Ⓨ 更新  Ⓡ お気に入りのみ",
        " · TF2 カード挿入中",
        // bluetooth
        "Bluetooth", "オン", "オフ", "Bluetoothスイッチ", "オン", "オフ",
        "オーディオ", "ゲームパッド", "キーボード", "マウス", "デバイス",
        " · 接続済み", " · ペアリング済み",
        "デバイスがありません — タップでスキャン", "処理中…",
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
        "%s に更新しますか？", "空きスロットに書き込み、再起動で適用されます。ゲームとセーブは保持されます。",
        // boot stages
        "画面を初期化", "起動パーティションをマウント", "ゲームデータを準備",
        "ネットワークを初期化", "オーディオを初期化", "ゲームパッド入力を準備",
        "まもなく画面が表示されます",
        // wizard frame
        "初期設定", "gameOS へようこそ", "%d / %d ステップ",
        "Ⓐ 次へ    Ⓑ この手順をスキップ    START すべてスキップ",
        "設定完了", "ゲームを始められます。\nあとで 系統設定 からこのウィザードを再実行できます。",
        "この手順にはネットワークが必要です。Ⓑ でスキップできます",
        "すべての設定をスキップ",
        // connectivity test
        "接続テスト", "接続をテスト中…", "WiFi 関連付け", "ゲートウェイ経路",
        "名前解決", "インターネット接続", "正常", "失敗", "待機",
        // timezone
        "タイムゾーンを選択", "↑↓ 選択    Ⓐ 決定    Ⓑ スキップ", "アジア",
        "ヨーロッパ", "アメリカ", "アフリカ", "オセアニア", "標準時", "現在時刻 %s",
        // machine name
        "マシン名", "Ⓐ 入力    Ⓑ スキップ", "ネットワーク上でこの本体を識別するための名前",
        "英数字とハイフンのみ、1〜63 文字、先頭と末尾はハイフン不可",
        "マシン名を %s に設定しました",
        // retroachievements
        "RetroAchievements アカウント",
        "ログインすると対応ゲームで実績を解除し、進捗をアップロードできます",
        "ユーザー名", "パスワード", "ハードコアモード",
        "オンにするとステートセーブ・ロードとチートが使えなくなります",
        "ログイン", "ログイン中…", "ログイン成功 · %s", "ログイン失敗：%s",
        "ログイン済み · %s", "ログアウト",
        // covers
        "libretro のカバーライブラリからゲーム名で照合してダウンロード",
        "ダウンロード開始", "カバーをダウンロード中…",
        "%d / %d · 取得 %d · 不一致 %d", "完了：%d 件追加、%d 件は不一致",
        "停止", "ダウンロードが必要なゲームがありません", "ゲームカバー",
        "カバーのないゲームのカバーをダウンロード", "カバー未取得 %d 件",
        "次へ",
        // activity statistics + favorites/history
        "プレイ統計", "時間 · タイムライン · 傾向 · おすすめ",
        "お気に入りと履歴", "すばやく見つけて起動",
        "概要", "タイムライン", "傾向", "おすすめ",
        "総プレイ時間", "起動回数", "プレイしたゲーム", "今日", "直近 7 日",
        "よく遊ぶ順", "まだプレイ記録がありません", "ゲームを起動して終了するとここに記録されます",
        "%d 回", "%zu 本",
        "%d 時間 %d 分", "%d 分", "%d 秒",
        "直近 14 日のプレイ時間", "機種別プレイ時間", "この期間はプレイしていません",
        "お気に入り・未プレイ", "お気に入り・久しぶり", "新着ゲーム", "久しぶり", "最近プレイ中",
        "おすすめできるゲームがありません", "先にゲームを TF カードへコピーしてください", "%d 日プレイなし",
        "お気に入り", "履歴", "お気に入りがありません", "ゲーム一覧で Ⓧ を押すと登録できます",
        "プレイ履歴がありません", "ゲームを起動すると自動で記録されます", "ファイルがカードにありません", "%zu 本",
        "←→ タブ切替  ↑↓ 選択  Ⓐ 起動  Ⓧ お気に入り  Ⓨ 更新  Ⓑ 戻る",
        "←→ お気に入り/履歴  ↑↓ 選択  Ⓐ 起動  Ⓧ お気に入り  Ⓨ 更新  Ⓑ 戻る",
        "データパーティションをマウントできません", "初期化されていません。データは保持されています。カードを確認するか、再起動してください。",
        "ゲームを開始", "スキップ", "続きから",
        "タップで開く · 左右スワイプで切替", "上下スワイプでスクロール · タップで選択",
        "スライダーをドラッグ · タップで切替", "タップで接続 · 上下スワイプでスクロール",
        "アップデートを確認", "再確認", "ダウンロードしてインストール",
        "スキャン", "解除", "近くにネットワークがありません · スキャンで再試行",
        "タップで選択 · 自動的に次へ", "タップで名前を入力 · 次へ",
        "タップでダウンロード · 次へでスキップ", "タップで入力 · 決定で完了",
        "タップで即時適用 · 次へで現在のまま",
        "セーブを削除", "スイッチをオンにすると Bluetooth 機器を検索・接続できます",
        "方向キーで選択 · Ⓐ 調整/開く · Ⓑ 戻る",
        "◀ ▶ 調整 · Ⓐ 完了 · Ⓑ 戻る",
        "スキャン失敗 · スキャンで再試行", "Wi-Fi サービスの起動に失敗しました",
        "この接続を解除しますか？", "ゲームを起動できません",
        "エミュレータが異常終了しました。ログ: TFカード system/retroarch.log\n最後の行: %s",
        // launch animation
        "カードを読み込み中", "データを検証中", "エミュレータを起動中",
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
        "(Use the TF1 GAMEOS drive, or the roms folder on a TF2 card)", "Cover",
        "↑↓ Select  Ⓐ Launch  Ⓑ Back  Ⓧ Favorite  Ⓨ Refresh  Ⓡ Favorites only",
        " · TF2 card",
        // bluetooth
        "Bluetooth", "On", "Off", "Bluetooth switch", "On", "Off",
        "Audio", "Gamepad", "Keyboard", "Mouse", "Device",
        " · connected", " · paired",
        "No devices yet — tap Scan to add", "Working…",
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
        "Update to %s?", "Downloads to the idle slot, then restarts to apply. Games and saves are kept.",
        // boot stages
        "Initialising display", "Mounting boot partition", "Preparing game data",
        "Initialising network", "Initialising audio", "Preparing gamepad input",
        "Starting the interface",
        // wizard frame
        "First-time setup", "Welcome to gameOS", "Step %d of %d",
        "Ⓐ Next    Ⓑ Skip this step    START Skip all",
        "Setup complete", "You're ready to play.\nYou can run this wizard again from Settings.",
        "This step needs a network connection — press Ⓑ to skip", "Skip all setup",
        // connectivity test
        "Test connectivity", "Testing connectivity…", "WiFi association",
        "Gateway route", "Name resolution", "Internet access", "OK", "Failed",
        "Waiting",
        // timezone
        "Select time zone", "↑↓ Select    Ⓐ Confirm    Ⓑ Skip", "Asia", "Europe",
        "Americas", "Africa", "Oceania", "Standard time", "Current time %s",
        // machine name
        "Machine name", "Ⓐ Enter    Ⓑ Skip", "Identifies this device on the network",
        "Letters, digits and hyphens only, 1-63 characters, no leading or trailing hyphen",
        "Machine name set to %s",
        // retroachievements
        "RetroAchievements account",
        "Sign in to unlock achievements and upload progress in supported games",
        "Username", "Password", "Hardcore mode",
        "Disables save states, load state and cheats",
        "Sign in", "Signing in…", "Sign-in successful · %s", "Sign-in failed: %s",
        "Signed in · %s", "Sign out",
        // covers
        "Download box art from the libretro thumbnail library, matched by game name",
        "Start download", "Downloading covers…",
        "%d / %d · downloaded %d · unmatched %d", "Done: %d added, %d unmatched",
        "Stop", "No games need a cover", "Game covers",
        "Download covers for games that have none", "%d games without covers",
        "Continue",
        // activity statistics + favorites/history
        "Activity", "Play time · Timeline · Trends · Next up",
        "Favorites & History", "Find and launch a game fast",
        "Overview", "Timeline", "Trends", "Next up",
        "Total play time", "Sessions", "Games played", "Today", "Last 7 days",
        "Most played", "No play history yet",
        "Launch a game and it is recorded here when you quit",
        "%d sessions", "%zu games",
        "%dh %dm", "%d min", "%d sec",
        "Play time, last 14 days", "Time per console", "Nothing played in this period",
        "Favorited, never played", "Favorited, been a while", "New game",
        "Been a while", "Playing lately",
        "Nothing to recommend", "Copy some games to the TF card first", "%d days idle",
        "Favorites", "History", "No favorites yet", "Press Ⓧ in a game list to add one",
        "No play history yet", "Launching a game records it automatically",
        "File is no longer on the card", "%zu games",
        "←→ Tabs  ↑↓ Select  Ⓐ Launch  Ⓧ Favorite  Ⓨ Refresh  Ⓑ Back",
        "←→ Favorites/History  ↑↓ Select  Ⓐ Launch  Ⓧ Favorite  Ⓨ Refresh  Ⓑ Back",
        "Data partition unavailable", "Not reformatted — your data is preserved. Check the card or reboot.",
        "Play", "Skip", "Continue playing",
        "Tap to open · swipe to switch", "Swipe to browse · tap to select",
        "Drag sliders · tap to toggle", "Tap a network · swipe to browse",
        "Check for updates", "Check again", "Download & install",
        "Scan", "Forget", "No networks nearby — tap Scan to retry",
        "Tap to choose · continues automatically",
        "Tap to type a name · tap Continue",
        "Tap to download · or tap Continue to skip",
        "Tap keys · tap OK when done",
        "Tap a city to apply · Continue keeps current",
        "Delete save", "Turn the switch on to find and connect Bluetooth devices",
        "D-pad select · Ⓐ adjust/open · Ⓑ back",
        "◀ ▶ adjust · Ⓐ done · Ⓑ back",
        "Scan failed — tap Scan to retry", "Wi-Fi service failed to start — try again",
        "Forget this connection?", "Game failed to start",
        "The emulator exited unexpectedly. Log: system/retroarch.log on the TF card.\nLast line: %s",
        // launch animation
        "Reading card", "Verifying data", "Starting emulator",
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
        "(Игры — на диск GAMEOS карты TF1 или в папку roms карты TF2)", "Обложка",
        "↑↓ Выбор  Ⓐ Запуск  Ⓑ Назад  Ⓧ Избранное  Ⓨ Обновить  Ⓡ Только избранное",
        " · карта TF2",
        // bluetooth
        "Bluetooth", "Включено", "Выключено", "Переключатель Bluetooth", "Вкл", "Выкл",
        "Аудио", "Геймпад", "Клавиатура", "Мышь", "Устройство",
        " · подключено", " · сопряжено",
        "Устройств нет — нажмите «Поиск»", "Выполняется…",
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
        "Обновить до %s?", "Запись в резервный раздел, затем перезагрузка. Игры и сохранения сохранятся.",
        // boot stages
        "Инициализация экрана", "Монтирование загрузочного раздела",
        "Подготовка данных игр", "Инициализация сети", "Инициализация звука",
        "Подготовка ввода геймпада", "Запуск интерфейса",
        // wizard frame
        "Первоначальная настройка", "Добро пожаловать в gameOS", "Шаг %d из %d",
        "Ⓐ Далее    Ⓑ Пропустить шаг    START Пропустить всё",
        "Настройка завершена", "Можно играть.\nЭтот мастер можно запустить снова в настройках.",
        "Для этого шага нужна сеть — нажмите Ⓑ, чтобы пропустить",
        "Пропустить всю настройку",
        // connectivity test
        "Проверить связь", "Проверка связи…", "Подключение WiFi", "Маршрут к шлюзу",
        "Разрешение имён", "Доступ в интернет", "OK", "Ошибка", "Ожидание",
        // timezone
        "Выберите часовой пояс", "↑↓ Выбор    Ⓐ Подтвердить    Ⓑ Пропустить",
        "Азия", "Европа", "Америка", "Африка", "Океания", "Стандартное время",
        "Текущее время %s",
        // machine name
        "Имя устройства", "Ⓐ Ввести    Ⓑ Пропустить", "Определяет это устройство в сети",
        "Только буквы, цифры и дефисы, 1-63 символа, без дефиса в начале и конце",
        "Имя устройства: %s",
        // retroachievements
        "Учётная запись RetroAchievements",
        "Войдите, чтобы получать достижения и отправлять прогресс в поддерживаемых играх",
        "Имя пользователя", "Пароль", "Хардкорный режим",
        "Отключает сохранения состояний, загрузку и читы",
        "Войти", "Вход…", "Вход выполнен · %s", "Ошибка входа: %s",
        "Вы вошли · %s", "Выйти",
        // covers
        "Скачать обложки из библиотеки libretro по имени игры", "Начать загрузку",
        "Загрузка обложек…", "%d / %d · загружено %d · без совпадения %d",
        "Готово: добавлено %d, без совпадения %d", "Остановить",
        "Нет игр, которым нужна обложка", "Обложки игр",
        "Скачать обложки для игр без них", "Без обложек: %d",
        "Продолжить",
        // activity statistics + favorites/history
        "Статистика", "Время · Хроника · Тренды · Рекомендации",
        "Избранное и история", "Быстро найти и запустить игру",
        "Обзор", "Хроника", "Тренды", "Советуем",
        "Общее время игры", "Запусков", "Игр опробовано", "Сегодня", "За 7 дней",
        "Чаще всего", "Записей об игре пока нет",
        "Запустите игру — после выхода она появится здесь",
        "%d запусков", "%zu игр",
        "%d ч %d мин", "%d мин", "%d сек",
        "Время игры за 14 дней", "Время по консолям", "В этот период игр не было",
        "В избранном, не запускалось", "Избранное, давно не играли", "Новая игра",
        "Давно не играли", "Недавно играли",
        "Нечего рекомендовать", "Сначала скопируйте игры на карту TF", "%d дней без игры",
        "Избранное", "История", "В избранном пусто",
        "Нажмите Ⓧ в списке игр, чтобы добавить",
        "Истории игр пока нет", "Запуск игры записывается автоматически",
        "Файла больше нет на карте", "%zu игр",
        "←→ Вкладки  ↑↓ Выбор  Ⓐ Запуск  Ⓧ Избранное  Ⓨ Обновить  Ⓑ Назад",
        "←→ Избранное/История  ↑↓ Выбор  Ⓐ Запуск  Ⓧ Избранное  Ⓨ Обновить  Ⓑ Назад",
        "Раздел данных недоступен", "Не отформатирован — данные сохранены. Проверьте карту или перезагрузите устройство.",
        "Играть", "Пропустить", "Продолжить игру",
        "Коснитесь, чтобы открыть · свайп для переключения",
        "Свайп для прокрутки · коснитесь для выбора",
        "Перетащите ползунок · коснитесь для переключения",
        "Коснитесь сети · свайп для прокрутки",
        "Проверить обновления", "Проверить снова", "Скачать и установить",
        "Поиск сетей", "Забыть", "Сети не найдены — нажмите «Поиск»",
        "Коснитесь для выбора · далее автоматически",
        "Коснитесь, чтобы ввести имя · Далее",
        "Коснитесь, чтобы скачать · Далее — пропустить",
        "Коснитесь клавиш · OK для завершения",
        "Коснитесь города для применения · Далее — оставить текущий",
        "Удалить сейв", "Включите переключатель, чтобы искать и подключать Bluetooth-устройства",
        "Крестовина — выбор · Ⓐ настроить/открыть · Ⓑ назад",
        "◀ ▶ — настроить · Ⓐ готово · Ⓑ назад",
        "Ошибка поиска — нажмите «Поиск»", "Служба Wi-Fi не запустилась — повторите",
        "Забыть это подключение?", "Не удалось запустить игру",
        "Эмулятор аварийно завершился. Журнал: system/retroarch.log на карте TF.\nПоследняя строка: %s",
        // launch animation
        "Чтение картриджа", "Проверка данных", "Запуск эмулятора",
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
        "(Usa la unidad GAMEOS de TF1 o la carpeta roms de una tarjeta TF2)", "Carátula",
        "↑↓ Elegir  Ⓐ Iniciar  Ⓑ Volver  Ⓧ Favorito  Ⓨ Actualizar  Ⓡ Solo favoritos",
        " · tarjeta TF2",
        // bluetooth
        "Bluetooth", "Activado", "Desactivado", "Interruptor Bluetooth", "Sí", "No",
        "Audio", "Mando", "Teclado", "Ratón", "Dispositivo",
        " · conectado", " · emparejado",
        "Sin dispositivos — toca Escanear", "Procesando…",
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
        "¿Actualizar a %s?", "Se escribe en la ranura libre y se reinicia para aplicar. Juegos y partidas se conservan.",
        // boot stages
        "Iniciando pantalla", "Montando partición de arranque",
        "Preparando datos de juegos", "Iniciando red", "Iniciando audio",
        "Preparando entrada del mando", "Iniciando la interfaz",
        // wizard frame
        "Configuración inicial", "Bienvenido a gameOS", "Paso %d de %d",
        "Ⓐ Siguiente    Ⓑ Omitir este paso    START Omitir todo",
        "Configuración completa", "Ya puedes jugar.\nPuedes volver a ejecutar este asistente desde Ajustes.",
        "Este paso necesita red — pulsa Ⓑ para omitir",
        "Omitir toda la configuración",
        // connectivity test
        "Probar conectividad", "Probando conectividad…", "Asociación WiFi",
        "Ruta de enlace", "Resolución DNS", "Acceso a Internet",
        "OK", "Fallo", "Esperando",
        // timezone
        "Selecciona la zona horaria", "↑↓ Seleccionar    Ⓐ Confirmar    Ⓑ Omitir",
        "Asia", "Europa", "América", "África", "Oceanía", "Hora estándar",
        "Hora actual %s",
        // machine name
        "Nombre del equipo", "Ⓐ Introducir    Ⓑ Omitir",
        "Identifica este dispositivo en la red",
        "Solo letras, dígitos y guiones, 1-63 caracteres, sin guion al principio ni al final",
        "Nombre del equipo: %s",
        // retroachievements
        "Cuenta de RetroAchievements",
        "Inicia sesión para desbloquear logros y subir el progreso en juegos compatibles",
        "Usuario", "Contraseña", "Modo hardcore",
        "Desactiva los estados guardados, la carga y los trucos",
        "Iniciar sesión", "Iniciando sesión…", "Sesión iniciada · %s",
        "Error al iniciar sesión: %s", "Sesión activa · %s", "Cerrar sesión",
        // covers
        "Descarga carátulas de la biblioteca de libretro por nombre de juego",
        "Iniciar descarga", "Descargando carátulas…",
        "%d / %d · descargadas %d · sin coincidencia %d",
        "Listo: %d añadidas, %d sin coincidencia", "Detener",
        "No hay juegos que necesiten carátula", "Carátulas de juegos",
        "Descargar carátulas para juegos sin ellas", "%d juegos sin carátula",
        "Continuar",
        // activity statistics + favorites/history
        "Actividad", "Tiempo · Cronología · Tendencias · Siguiente",
        "Favoritos e historial", "Encuentra y lanza un juego rápido",
        "Resumen", "Cronología", "Tendencias", "Siguiente",
        "Tiempo total de juego", "Sesiones", "Juegos jugados", "Hoy", "Últimos 7 días",
        "Más jugados", "Aún no hay registro de juego",
        "Lanza un juego y se registrará aquí al salir",
        "%d sesiones", "%zu juegos",
        "%d h %d min", "%d min", "%d s",
        "Tiempo de juego, últimos 14 días", "Tiempo por consola",
        "No se jugó en este periodo",
        "Favorito, nunca jugado", "Favorito, hace tiempo", "Juego nuevo",
        "Hace tiempo", "Jugando últimamente",
        "Nada que recomendar", "Copia algunos juegos a la tarjeta TF primero",
        "%d días sin jugar",
        "Favoritos", "Historial", "Aún no hay favoritos",
        "Pulsa Ⓧ en una lista de juegos para añadir",
        "Aún no hay historial", "Al lanzar un juego se registra automáticamente",
        "El archivo ya no está en la tarjeta", "%zu juegos",
        "←→ Pestañas  ↑↓ Elegir  Ⓐ Lanzar  Ⓧ Favorito  Ⓨ Actualizar  Ⓑ Volver",
        "←→ Favoritos/Historial  ↑↓ Elegir  Ⓐ Lanzar  Ⓧ Favorito  Ⓨ Actualizar  Ⓑ Volver",
        "Partición de datos no disponible", "Sin formatear: tus datos se conservan. Revisa la tarjeta o reinicia.",
        "Jugar", "Omitir", "Continuar jugando",
        "Toca para abrir · desliza para cambiar", "Desliza para explorar · toca para elegir",
        "Arrastra los controles · toca para cambiar", "Toca una red · desliza para explorar",
        "Buscar actualizaciones", "Volver a comprobar", "Descargar e instalar",
        "Escanear", "Olvidar", "No hay redes cerca · toca Escanear para reintentar",
        "Toca para elegir · continúa solo",
        "Toca para escribir el nombre · Continuar",
        "Toca para descargar · o Continuar para omitir",
        "Toca las teclas · OK para terminar",
        "Toca una ciudad para aplicar · Continuar mantiene la actual",
        "Eliminar partida", "Activa el interruptor para buscar y conectar dispositivos Bluetooth",
        "Cruceta elegir · Ⓐ ajustar/abrir · Ⓑ volver",
        "◀ ▶ ajustar · Ⓐ listo · Ⓑ volver",
        "Error de búsqueda — toca Escanear", "El servicio Wi-Fi no arrancó — reintenta",
        "¿Olvidar esta conexión?", "No se pudo iniciar el juego",
        "El emulador terminó de forma inesperada. Registro: system/retroarch.log en la tarjeta TF.\nÚltima línea: %s",
        // launch animation
        "Leyendo cartucho", "Verificando datos", "Iniciando emulador",
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
    };
    static const char* ids[5] = { "nds", "gba", "gbc", "ps1", "psp" };
    for (int i = 0; i < 5; i++)
        if (strcmp(id, ids[i]) == 0) return names[g_lang][i];
    return id;
}

const char* theme_name(int idx)
{
    static const char* names[LANG_COUNT][4] = {
        { "霓虹夜景", "像素复古", "清新晨光", "水墨山水" },
        { "霓虹夜景", "像素復古", "清新晨光", "水墨山水" },
        { "ネオン", "ピクセル", "フレッシュ", "水墨" },
        { "Neon", "Pixel", "Fresh", "Ink" },
        { "Неон", "Пиксель", "Свежесть", "Тушь" },
        { "Neón", "Píxel", "Fresco", "Tinta" },
    };
    if (idx < 0 || idx > 3) idx = 0;
    return names[g_lang][idx];
}

const char* theme_key(int idx)
{
    static const char* keys[4] = { "neon", "pixel", "fresh", "ink" };
    if (idx < 0 || idx > 3) idx = 0;
    return keys[idx];
}

ThemePal theme_palette(int idx)
{
    static const ThemePal pals[4] = {
        { { 53, 208, 186 }, { 232, 236, 247 }, { 138, 147, 178 },
          { 20, 27, 51 }, { 11, 16, 32 }, { 28, 38, 58 }, { 214, 255, 247 },
          235, 14, 26 },
        { { 154, 222, 80 }, { 235, 240, 228 }, { 148, 158, 190 },
          { 22, 24, 52 }, { 14, 14, 38 }, { 34, 36, 64 }, { 240, 255, 220 },
          235, 10, 18 },
        { { 16, 150, 140 }, { 28, 62, 72 }, { 96, 125, 133 },
          { 255, 255, 255 }, { 240, 248, 246 }, { 205, 225, 222 }, { 255, 255, 255 },
          215, 0, 0 },
        { { 178, 58, 48 }, { 42, 40, 38 }, { 112, 106, 100 },
          { 250, 247, 240 }, { 242, 238, 229 }, { 210, 204, 192 }, { 255, 252, 245 },
          220, 0, 0 },
    };
    if (idx < 0 || idx > 3) idx = 0;
    return pals[idx];
}

const char* lang_name(int l)
{
    // Autonyms: each language is always listed in its own script.
    static const char* names[LANG_COUNT] = {
        "简体中文", "繁體中文", "日本語", "English", "Русский", "Español",
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
    };
    return codes[g_lang];
}

const char* ppsspp_locale()
{
    // PPSSPP [General] Language values (assets/langregion.ini)
    static const char* codes[LANG_COUNT] = {
        "zh_CN", "zh_TW", "ja_JP", "en_US", "ru_RU", "es_ES",
    };
    return codes[g_lang];
}

} // namespace gameos