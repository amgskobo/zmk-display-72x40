# ZMK Display 72x40

40×72の縦向き表示は、上から反転したレイヤー名、BLE／USB、Caps Lock、
左右のバッテリー、Studio／Messengerの鍵の順です。文字はMona10由来の
ASCIIビットマップ「Panel Status 10」に揃えています。鍵は9×11ドットで、
塗りつぶした本体と斜めに開く輪を使います。長いレイヤー名は文字の途中で
切らず、見出しに収まるところまで表示します。各項目の位置は固定され、
状態の変化では動きません。チェックはZMKが実際に選択したHID出力先を
示し、最大で一つだけ表示します。接続済みでも出力先でない側はマークなし、
ボンド済みBLEの切断やUSB HIDの未準備はバツ、未ペアリングのBLEはマークなしです。
USB給電はバッテリー横の稲妻で別に示します。
`S`はStudio、`M`はMessengerの新規接続受付時間です。受付時間が終了すると
Mの鍵は閉じますが、すでに許可されたセッションは継続できます。
詳細な配置と必要なMessenger通知APIは英語版の「What the panel shows」を参照してください。

[![Test](https://github.com/amgskobo/zmk-display-72x40/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-display-72x40/actions/workflows/test.yml)

[English](README.md)

AKKB専用の72×40 OLEDへ、40×72の縦向きステータスを直接描画するモジュールです。
取り付け向きと描画形式は固定で、向きや別サイズを選ぶ機能はありません。

- 最上位の有効レイヤーを大きな文字で、ホスト接続、split 両側のバッテリー残量、
  組み込まれていれば Caps Lock と各サービスのロック状態を表示します。
- 再描画のタイマーを持ちません。表示内容が変わるイベントで 1 回だけ再描画し、
  変化した列だけを送ります。表示が変わらなければ何もしません。
- ポインターやタッチ操作で自動的に上がるレイヤーを表示対象から外せるので、
  トラックパッドを操作しても表示の通信は発生しません。
- タッチコントローラーとバスを共有するときは、レポート後に書き込みを予約し、
  バスの競合を抑えます。実際の時刻や待ち時間は実機での測定が必要です。
- 最後のキー・エンコーダー・ポインター操作から 30 秒後(変更可、無効も可)と、
  電源オフの前(`&soft_off` を含む)にパネルを消灯します。


## インストール

### 1. module を追加する

利用側 config の `config/west.yml` に:

```yaml
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-display-72x40
      remote: amgskobo
      revision: main
      path: zmk-display-72x40
```

### 2. パネルを記述する

AKKBの72×40 SSD1306を使います。コントローラーは8ピクセル単位のページ形式
(`SCREEN_INFO_MONO_VTILED`)です。

```dts
&i2c1 {
    oled: ssd1306@3c {
        compatible = "solomon,ssd1306fb";
        reg = <0x3c>;
        width = <72>;
        height = <40>;
        segment-offset = <28>;
        page-offset = <0>;
        display-offset = <0>;
        multiplex-ratio = <39>;
        segment-remap;
        com-invdir;
        prechargep = <0x22>;
        use-internal-iref;
    };
};
```

パネルは72×40固定です。描画座標は40×72で、別サイズはビルド時に拒否します。

### 3. ステータスパネルを宣言する

unibody キーボード、または split の central(keymap を持つ側)に:

```dts
/ {
    status_panel {
        compatible = "zmk,display-72x40";
        display = <&oled>;
        /* 任意 */
        hidden-layers = <6 7 8>;
        sync-input-device = <&trackpad>;
        contrast = <128>;
    };
};
```

このノードで `CONFIG_ZMK_DISPLAY_72X40` が有効になり、ディスプレイドライバの
サブシステムも自動で選択されます。`CONFIG_ZMK_DISPLAY`(ZMK の LVGL 表示)は
オフのままにしてください。両方が同じパネルを持つことになるため、オンの間は
このモジュールはビルドされません。

| プロパティ | 意味 |
| --- | --- |
| `display` | 72×40固定のパネル。40×72の縦向き座標で直接描画します。 |
| `hidden-layers` | パネルに表示しないレイヤー ID。これらの変化ではモジュールは動きません。 |
| `sync-input-device` | 同じバス上の入力デバイス。[バスの共有](#バスの共有)を参照。 |
| `contrast` | 0-255。起動時に 1 回設定します。 |
| `reset-gpios` | パネルのアクティブ Low のリセット線。ディスプレイドライバの初期化前にパルスを出します。[パネルのリセット](#パネルのリセット)を参照。 |

### オプション

| Kconfig | 既定値 | 意味 |
| --- | --- | --- |
| `CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS` | `n` | 下記の設定を Studio クライアントに公開します(zmk-feature-custom-settings が必要)。 |
| `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS` | `30000` | パネルの消灯タイマー。最後のキー・エンコーダー・ポインター操作からこの時間で消灯します。`0` で無効。 |
| `CONFIG_ZMK_DISPLAY_72X40_MESSENGER` | Messenger 有効時は `y` | Messenger の受付状態と通知を表示へ渡す任意のアダプター。別の状態取得関数を使う場合は `n`。 |
| `CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH` | `n` | 公開 API で受け取ったパターン ID・名前・明るさ・速度を一時表示します。LED モジュールは不要です。受け付けた表示要求で消灯中も点灯します。 |
| `CONFIG_ZMK_DISPLAY_72X40_LED_PATTERNS` | LED 一時表示と zmk-led-patterns 有効時は `y` | zmk-led-patterns のイベントを数値 API へ渡す任意のアダプター。別の送信元を使う場合は `n`。 |
| `CONFIG_ZMK_DISPLAY_72X40_LED_SPLASH_MS` | `1800` | 一時表示時間の初期値 (ms)。Studio 連携があれば実行中に変更でき、`0` で一時表示をオフにできます。 |
| `CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS` | `20` | 連続した変化を 1 回の再描画にまとめる待ち時間。 |
| `CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS` | `50` | 同期デバイスがレポート後「レポート中」とみなされる時間。 |
| `CONFIG_ZMK_DISPLAY_72X40_RETRY_MS` | `1000` | 書き込み失敗時の再試行間隔(3 回まで)。 |
| `CONFIG_ZMK_DISPLAY_72X40_PEER_POWER` | `y` | 外部電源で動作中の peripheral に稲妻を描きます(split リレーが必要)。 |
| `CONFIG_ZMK_DISPLAY_72X40_POWER_REPORTER` | `y` | split peripheral 側にビルドされ、自分の電源状態を central へ伝えます。 |
| `CONFIG_ZMK_DISPLAY_72X40_POWER_POLL_MS` | `2000` | その peripheral が VBUS を見る間隔。 |
| `CONFIG_ZMK_DISPLAY_72X40_RESET_INIT_PRIORITY` | `84` | リセットパルスの POST_KERNEL 優先度。ディスプレイドライバより前。 |
| `CONFIG_ZMK_DISPLAY_72X40_RESET_PULSE_MS` | `10` | リセット線を Low に保つ時間。 |
| `CONFIG_ZMK_DISPLAY_72X40_RESET_SETTLE_MS` | `50` | 解除後に待つ時間。 |
| `CONFIG_ZMK_DISPLAY_72X40_INIT_PRIORITY` | `86` | 起動時のパネル消去の POST_KERNEL 優先度。ディスプレイドライバより後。 |
| `CONFIG_ZMK_DISPLAY_72X40_PM_INIT_PRIORITY` | `45` | `&soft_off` 用の電源オフデバイスの POST_KERNEL 優先度。パネルのバスより前。 |
| `CONFIG_ZMK_DISPLAY_72X40_PM_LATE_INIT_PRIORITY` | `55` | 逆順にサスペンドするアイドルスリープ用の同じデバイス。パネルのバスより後。 |

## 表示内容

40×72の固定配置は冒頭で説明したとおりです。本文とLED設定の一時表示には
Panel Status 10を使い、専用記号にはコンパクトな5×7データを使います。
実機でフォントファイルを解析したり、文字を変形したりする処理はありません。
フォント部分は[OFL-1.1](licenses/mona-OFL.txt)で、MITのモジュールコードとは
別です。元のリビジョンと変換元の情報は[scripts/status10.txt](scripts/status10.txt)にあります。

## 低コストの仕組み

フレームはコントローラーと同じ並び(ページごとに 1 列 1 バイト、72x40 で 360
バイト)で RAM に置き、パネルに表示中の内容の写しと並べて持ちます。

1. 購読しているイベントはフラグを立て、ZMK の低優先度ワークキューに 1 回分の
   処理を `CONFIG_ZMK_DISPLAY_72X40_UPDATE_DELAY_MS` 後で予約します。非表示
   レイヤーのイベントはその前に戻ります。
2. 処理は状態を読んでフレーム全体を描きます。数千命令程度なので、描画の途中
   状態は持ちません。
3. ページごとにフレームと写しを比べ、変化した列の連続を 1 回の書き込みで送り
   ます。間隔が 12 列以内の変化はまとめて送ります(2 つ目の区間のアドレス指定
   と同程度のコストのため)。縦向きの数字は複数のページにまたがるので、
   送信量は変化した画素と、その隣の値によって決まります。
4. パネルが消灯中は描画だけを行い、次のキー・エンコーダー・ポインター操作で、
   点灯より先に送ります。

イベントの間に再描画は起きず、消灯タイマーもパネルが点灯している間しか
動きません。使用量は有効な機能によって変わるため、過去のサイズではなく、
現在のファームウェアのリンク時メモリレポートで確認してください。
360バイトの送信予定・送信済みフレームを2つ、合計720バイト保持します。
中間フレームはありません。
ZMK標準の表示はLVGLとフォント・オブジェクトツリーに加え、動作中に
10msごとのティックを使います。

## パネルのリセット

RES# がモジュール上の抵抗とコンデンサだけで駆動されている場合、リセットが
かかるのは電源投入時だけです。リセットボタン、ファームウェアの書き込み、
ウォッチドッグのいずれでもコントローラーは以前の状態のままで、バスの異常で
固まると電池を抜くまで復帰しません。RES# をピンに配線し、このノードの
`reset-gpios` に指定すると、ファームウェア起動のたびにパルスが出ます。

パネル側のノードではなくこのノードに書くのには理由があります。Zephyr の
SSD1306 ドライバは `reset-gpios` を渡されると、解除した直後に最初のコマンドを
送ります。これを NACK で返すパネル(0.42 インチの 72x40 モジュールがそうです)
ではドライバの初期化が `-EIO` で失敗し、再試行もされないため画面は永久に暗い
ままになります。このモジュールはドライバより前
(`CONFIG_ZMK_DISPLAY_72X40_RESET_INIT_PRIORITY`、`CONFIG_DISPLAY_INIT_PRIORITY`
より小さい値)でパルスを出し、`CONFIG_ZMK_DISPLAY_72X40_RESET_PULSE_MS` の間
保持してから `CONFIG_ZMK_DISPLAY_72X40_RESET_SETTLE_MS` 待つので、ドライバは
起動を終えたコントローラーに対して初期化を行えます。パネル側のノードには
リセットピンを書かないでください。

## 固定表示への移行

描画座標は40×72固定で、72×40のコントローラーページへ直接書き込みます。
既存のAKKBの画面配置は維持します。回転プロパティ、向きの選択、全画面変換、
横向き配置は削除しました。古いstatusノードから回転プロパティを削除してください。
ゼロ指定も無視せずビルド時に拒否します。別のパネルサイズも同様です。

## 外部電源の peripheral

central は自分が給電されていることを知っているので、自分の行の稲妻はそのまま
出せます。split の peripheral は違います。USB HID を出せない以上 USB スタック
を積んでおらず、自分の電源が USB かどうかを ZMK から知る方法がありません。

`CONFIG_ZMK_DISPLAY_72X40_POWER_REPORTER` は、パネルではなく peripheral 側に
ビルドされ、SoC の USB VBUS 検出を直接読みます(USB 周辺機能を持つ nRF では
レジスタ 1 ビットで、USB スタックの有無に関係なく読めます)。間隔は
`CONFIG_ZMK_DISPLAY_72X40_POWER_POLL_MS` です。状態が変わるとフォークの split
リレーで central へ送り、`CONFIG_ZMK_DISPLAY_72X40_PEER_POWER` がその半分の
バッテリー残量の隣に稲妻として描きます。

リンクは「確立」と報告された時点ではまだ central がリレー特性を購読しておらず、
その前に送った通知は黙って捨てられます(送信関数はキューに積んだ時点で成功を
返すため、失敗を検出できません)。そこでリンク確立後の数秒間に 4 回送り直します。
重複しても central 側の負担はありません(画素が変わらない再描画は 0 バイトです)。

どちらの設定も可能な場合は既定で有効です(reporter は
`CONFIG_ZMK_SPLIT_RELAY_EVENT` のある nRF の split peripheral、central 側は
リレーがある構成)。リレーを持たない upstream ZMK ではどちらもビルドされず、
peripheral の行に稲妻が出ないだけです。

## バスの共有

IQS7211E などのタッチコントローラーは、データがあると読み取りウィンドウを開き
(ready 線が Low)、次の I2C STOP で閉じます。ウィンドウが開いてからコントロー
ラーの読み取りまでの間にパネルの書き込みが入ると、その読み取りが遅れ、書き込み
の STOP でウィンドウが早く閉じるおそれがあります。

`sync-input-device` にそのコントローラーを指定すると、モジュールはその入力
レポートを監視します。コントローラーが `CONFIG_ZMK_DISPLAY_72X40_SYNC_WINDOW_MS`
以内にレポートしている間、パネルへの書き込みは次のレポートを待ち、その直後に
1 レポートにつき 1 回ずつ送ります。そのときコントローラーの読み取りは終わった
ばかりで、次のサンプルは 1 レポート周期先です。再描画は変化した区間 1 つにつき
1 レポートかかり、全行が変わっても数レポートで終わります。コントローラーが指定
時間レポートしなければすぐに書き込みます。
1 回の書き込みは最大 1 ページ(72 バイトとアドレス指定)です。
400 kHz での線上の転送時間は約 2 ms の見積もりですが、スケジューラー・
ドライバー・バスの追加待ち時間を含まず、実機の RDY から読み取りまでの
待ち時間を保証する値ではありません。

同じデバイスが自動で上げるレイヤーは `hidden-layers` に入れてください。指を
動かしてもパネルの内容は変わらず、何も送られません。

## 消費電力

- `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS`(既定 30 秒)で、最後のキー・
  エンコーダー・ポインター操作(ZMK のアイドル判定と同じ入力)からその時間が
  たつとパネルを消灯し(コントローラーのディスプレイオフコマンド)、次の操作で
  点灯します。消灯中の変化は描画だけ行って点灯しないので、誰も見ていない
  バッテリー表示に電力を使いません。点灯したときにはその状態が描画済みです。
  操作は時刻を記録するだけで、タイマーはパネルが点灯している間しか動きません。
- ZMK のアイドルに合わせて消灯する設定はありません。タイマーで足りるからです。
  ZMK のアイドル時間より短ければ必ず先に消灯し、長ければ(`0` なら無効)指定
  どおり点灯を続け、キーボードの電源オフで必ず消灯します。
- ZMK の電源オフには `&soft_off` と、`CONFIG_ZMK_IDLE_SLEEP_TIMEOUT` による
  アイドルスリープの 2 つの経路があり、どちらも専用のイベントを出しません。
  SoC の電源が切れてもパネルには電源と表示が残ります。そのためモジュールは
  小さな電源管理デバイスを登録します。ZMK は電源オフ前にこれをサスペンドし、
  そこで再描画を止めてパネルを消灯します。2 つの経路はデバイスを逆の順序で
  サスペンドし(`&soft_off` は初期化順、アイドルスリープはその逆)、消灯には
  パネルのバスが必要なので、バスの前後に 1 つずつ置きます。バスより前が
  `CONFIG_ZMK_DISPLAY_72X40_PM_INIT_PRIORITY`、後が
  `CONFIG_ZMK_DISPLAY_72X40_PM_LATE_INIT_PRIORITY` です。先に動いた方が消灯し、
  もう一方は消灯済みのパネルに触れません。
- 起動時は、ドライバの準備ができた時点でパネルを消去するので、コントローラー
  のメモリーに残った内容は表示されません。

## 実行時の設定

キーボードの動作中に変えられる設定は 2 つあり、どちらもビルド時の値から
始まります。

| 設定 | 設定関数 | 既定値 |
| --- | --- | --- |
| パネルの消灯タイマー | `zmk_display_72x40_set_blank_after_ms()` | `CONFIG_ZMK_DISPLAY_72X40_BLANK_AFTER_MS` |
| コントラスト(0-255) | `zmk_display_72x40_set_contrast()` | ノードの `contrast`、なければドライバの値 |

設定関数(`zmk-display-72x40/display_72x40.h`)はどのスレッドから呼んでも安全
です。値を記録してモジュール自身の処理を予約するだけで、パネルに書き込むのは
その処理だけなので、変更も他の書き込みと同じく同期デバイスの読み取りを避けて
送られます。どちらもすぐに反映されます。新しいタイマーは最後のキー操作から
計り直し、`0` にすると以前のタイマーで消灯したパネルが点灯します。

`CONFIG_ZMK_DISPLAY_72X40_CUSTOM_SETTINGS=y` にすると、Studio クライアントが
[zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings)
を通して `amgskobo__display` の見出しの下で編集できます。キーは
`display.blank_after_s`(秒、0-600、0 で無効)、`display.contrast`、LED 一時表示を
組み込んだ場合の `display.led_splash_ms`(0-10000 ms、0 でオフ)です。
パネルは常に 1 つなので、キーはノード名ではなく固定の接頭辞を使い、ノード名を
変えても保存値が残ります。カスタム Studio RPC を持つパッチ版 ZMK が必要で、
upstream ZMK では何もビルドされません。

## 任意の連携とモジュールの境界

表示本体は Messenger や LED モジュールのヘッダーを読み込みません。
描画、消灯・復帰、バスへの書き込みは表示側が担当し、
[Messenger アダプター](src/display_72x40_messenger.c)と
[LED アダプター](src/display_72x40_led_patterns.c)だけが、相手の公開 API を
表示の API へ変換します。Custom Settings のアダプターも従来どおり別ファイルです。
既定値では従来の連携が有効になります。

[公開ヘッダー](include/zmk-display-72x40/display_72x40.h)では次を提供します。

| API | 動作 |
| --- | --- |
| `zmk_display_72x40_get_external_lock_status()` | 既定は利用不可。別の実装を一つ定義すると、`available` と `unlocked` を表示時に取得します。待機する処理を入れず、差し替え時は Messenger アダプターを無効にしてください。第二の鍵のラベルは `M` のままです。 |
| `zmk_display_72x40_status_changed()` | 最新の状態を公開した後に呼ぶ通知。既存のキューへ再描画を予約します。操作時刻を更新せず、消灯中のパネルを点灯せず、呼び出し元でバスに書き込みません。 |
| `zmk_display_72x40_show_led_settings(pattern, brightness, speed)` | LED モジュールなしでも呼べます。初期化前、一時表示時間がゼロ、機能を組み込まない場合は何もしません。それ以外は従来の点灯・一時表示・期限切れの動作です。 |

鍵の元データを表示側へ複製せず、描画時に取得します。消灯中にも更新を RAM へ
描画するため、復帰時にはそのフレームを利用できます。LED の三つの数値は
一つの 32 ビット値として不可分に読み書きし、新しいロック、動的メモリー、
ワーカー、タイマーは追加しません。明るさは 8 ビット、速度は 16 ビットの
パーセント値として表示し、API で範囲を丸めません。パターン ID と表示名の対応は
英語版の「Optional sources and module boundaries」を参照してください。
別の LED 送信元を使う場合は `LED_SPLASH` を有効、`LED_PATTERNS` を無効にして
数値 API を呼んでください。
受け付けた数値 API 呼び出しは、同じ値でも表示時間を開始し直します。
変更を通知するタイミングは送信元が決めます。zmk-led-patterns のアダプターは
そのモジュールの変更イベントを従来どおり利用します。

## 互換性とテスト

upstream ZMK の `main` と、DYA Studio フォーク(`cormoran/zmk` の `main+dya`)
の両方でビルドできます。使うのは Zephyr の display API と input API、ZMK の
公開イベントと状態取得関数だけです。

- `tests/run.sh` は描画コアと LED 一時表示の時間境界をホストで厳しい警告付き、最適化ありと ASan/UBSan
  付きでビルドし、レイアウト・フォント・変化区間の検出を確認し、生成済みの
  フォントヘッダーがソースと一致することも確認します。
  続いてドライバ、給電通知、設定、既定の状態取得、任意の連携アダプターの関数を、パネルのメモリを模した `tests/runtime/` のスタブ付き
  ハーネスに取り出します。全オプション有効(固定縦向き、リセットピン、コントラスト、同期デバイス、
  split の電池と給電、LED 一時表示)と全オプション無効のドライバ、peripheral の給電通知、
  設定ブリッジ、状態取得と連携アダプターです。失敗するページを含む起動時の消去、差分書き込み、再試行、パネルの
  タイムアウト、同期デバイスの 1 レポートにつき 1 回のバス操作、一時表示、電源断を確認します。
  CI は描画コア、一時表示ヘルパー、取り出した各関数の行・分岐 100% を要求します
  (`bash ./tests/run-docker.sh` で ZMK イメージ内でも同じものを実行します)。devicetree からの
  インスタンス生成とイベント登録は下記のファームウェア fixture で確認します。
- `tests/run-integration-docker.sh upstream|dya` は XIAO BLE・SSD1306 の
  ファームウェアをビルドします。DYA 版では zmk-led-patterns も加え、LED 一時表示の
  Studio キー登録を確認します。記録用ディスプレイに対して ZMK のイベントを流す
  native_sim テストで、パネルに届く内容を確認します。Messenger と LED モジュールを
  含めずに公開 API から鍵と LED 表示を更新する構成も確認します。

フォントは `scripts/font5x7.txt` にあり、`scripts/gen_font.py` がそこから
`include/zmk-display-72x40/display_72x40_font.h` を生成します。
縦向き用の文字は `scripts/status10.txt` にあり、
`scripts/gen_status_font.py` が `display_72x40_status_font.h` を生成します。
両方ともPython標準ライブラリだけを使い、`--check`で整合性を確認できます。

描画形式は一つに固定しています。キャンバスに保持するのはバッファへの参照だけで、
サイズ・向き・格納形式の選択状態は持ちません。描画範囲とページ位置は文字や
アイコンごとに一度だけ計算し、行ごとに書き込み位置を進めます。文字データは
950バイトと文字幅95バイトです。鍵は9ピクセル幅の専用書き込みを維持します。
フレームは合計720バイトで、投影用バッファはありません。削除した汎用ケースと
残す検証の対応は[移行表](tests/fixed-layout-migration.md)に記録しています。
実機の描画時間と消費電力は別途測定が必要です。

## License

コードと従来の5×7フォントはMITです。[LICENSE](LICENSE)を参照してください。
Mona10由来のPanel Status 10のビットマップはOFL-1.1です。
[licenses/mona-OFL.txt](licenses/mona-OFL.txt)を参照してください。


## Current test entry points (2026-10-05)

See [the module-owned test guide](tests/README.md). The default host command
executes every behavior scenario once under ASan/UBSan. Set `ZMK_TEST_MODE=full`
for all retained coverage/ABI variants. Legacy simulation has been removed
from the active suite; its source/snapshots remain externally archived.
