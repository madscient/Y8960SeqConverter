# Y8960 Sequence Converter

FM-BIOS の演奏ドライバ（OPLLDRV）と MuSICA の演奏データを、
[Y8960 BASIC Extension](https://github.com/madscient/MsxSoundSuiteExtension) の
`CALL MSAVE` が書き出すのと同じシーケンスデータ（`Y8SQ`）に変換する
コマンドラインツール。Windows / Linux / macOS 向け。

**開発中。**

できたファイルは Y8960 BASIC Extension の `CALL MLOAD` が読み、
[Y8960Sequencer](https://github.com/madscient/Y8960Sequencer) がそのまま鳴らせる。

## 入手

[Releases](https://github.com/madscient/Y8960SeqConverter/releases) から、
Windows（x64）と Linux（x64）の実行ファイルを入手できる。展開した `y8conv` を
そのまま使う。ほかの環境では、下の「ビルド」の手順でソースから作る。

## 使い方

```
y8conv <入力ファイル> [オプション]
```

```sh
y8conv GRAII-1.BGM
#  -> GRAII_1.SQ
```

| オプション | 意味 |
|---|---|
| `-o <基底名>` | 出力の名前（拡張子なし） |
| `--out-dir <フォルダ>` | 出力先。省略時は入力と同じフォルダ |
| `--format opll` / `--format musica` | 入力の形式。省略時はデータから判断する |
| `--base <16進>` | BSAVE の見出しが無いファイルを、元の機械でどの番地に置いていたか（例: `A600`） |
| `--rom-voices <ファイル>` | OPLLDRV の拡張音色に使う、ROM から吸い出した音色表（下記） |
| `--drop <チャンネル>` | そのチャンネルを変換しない。繰り返して指定できる |

出力の名前は MSX-DOS の 8.3 に収まるように付く。英数字以外は `_` になり、
9 文字目以降は切り捨てる。

成功すると、書き出したファイルと、トラックごとの大きさを表示する。変換できない
ものがあったときは警告を出す。誤りがあったときは何も書き出さない。

### 入力

| 形式 | 例 | 備考 |
|---|---|---|
| MuSICA | `.BGM` | MuSICA の `save BGM` が書き出すもの |
| OPLLDRV | `.BIN` `.OPL` | FM-BIOS の `MSTART` に渡すデータ |

どちらも、`BSAVE` の見出し付きのファイルと、見出しの無いファイルを読める。

- **MuSICA のデータは番地を絶対番地で持つ。** 見出しの無いファイルには `--base` が要る
- **OPLLDRV のデータは、ユーザー音色（`83h`）を使うときだけ絶対番地を持つ。**
  そのときは、見出しの無いファイルに `--base` が要る

MuSICA の MML（`.MSD`）と音色ファイル（`.VCD`）は読まない。MuSICA で
`save BGM` したものを渡す。

### チャンネルの名前と割り当て

`--drop` とメッセージは、チャンネルを次の名前で呼ぶ。

| 名前 | 元のチャンネル | Y8960 |
|---|---|---|
| `FM1`-`FM9` | FM 音源のメロディ | OPLLEX1 の CH0-8 |
| `RHY` | FM 音源のリズム | OPLLEX1 の CH10 |
| `PSG1`-`PSG3` | MuSICA の PSG（チャンネル 10-12） | SSGS の CH0-2 |
| `SCC1`-`SCC5` | MuSICA の SCC（チャンネル 13-17） | SCC の CH0-4 |

トラック番号は、使っているチャンネルにこの表の順で頭から振る。

**Y8960 のシーケンスは 16 トラックまで。** MuSICA の 17 チャンネルすべてを使う
曲は、そのままでは変換できない。`--drop` で1つ以上を外す。

### 拡張音色（OPLLDRV の `82h`）

OPLLDRV の拡張音色は ROM に入っている 64 音色で、**中身は FMPAC と
MSX-MUSIC 内蔵の ROM で違う。** どちらの音で作られた曲かはデータからは
分からない。

- 既定では、同じ番号の Y8960 のプリセット音色（`@0`-`@63`）で鳴る。名前の並びは
  同じだが、音は元の ROM のものとは違う。Y8960 のプリセットが持つ移調は掛けない
  （元のドライバは拡張音色に移調を掛けない）
- `--rom-voices` に、自分の機械の ROM から吸い出した 512 バイト（8 バイト × 64、
  OPLL のレジスタ 00h-07h の並び）を渡すと、その音色で鳴る

### 変換の中身

何が何に写るか、写らないものは何かは [`doc/conversion.md`](doc/conversion.md)。

要点:

- 時間は正確に写る。元の 1/60 秒を 1 tick にするため、**テンポは 75 になる**。
  楽譜としての音長（四分音符が何 tick か）は元データに無いので、Y8960 側の
  音長は譜面上の値と対応しない
- 音の高さは、元と同じ周波数で鳴るように写す。MuSICA の FM は元の機械で
  PSG より 1 オクターブ低く鳴るので、Y8960 側ではオクターブの数字が 1 つ小さくなる
- MuSICA の PSG と SCC のソフトウェアエンベロープは、Y8960 のソフトウェア
  エンベロープ（`@E`*n*）に写す
- Y8960 の1トラックは 2048 バイトまで。越えるトラックがあると変換は失敗する
- MuSICA の**ビブラート・ポルタメント・デチューン・LFO 速度は変換しない**

## ビルド

CMake 3.18 以上と C++17 のコンパイラが要る。

```sh
cmake -S . -B build
cmake --build build --config Release
```

`build/bin/Release/y8conv`（Windows では `y8conv.exe`）ができる。試験は

```sh
cd build && ctest -C Release
```

## 権利

コードは MIT ライセンス（[`LICENSE`](LICENSE)）。

`src/` のコードはこのプロジェクトが新規に書いたもので、データ形式の**仕様**を
参照したのであってコードではない。**例外は音色データ1本で、これは MIT
ライセンスの対象ではない。**

`src/core/voicedata.cpp` のプリセット FM 音色64本は
Y8960 BASIC Extension の `src/tab/voicedat.asm` から機械的に写したもので、
そちらは MSX-AUDIO BASIC Extension Lite の `src/vocdat.mac` の写し、さらに
そちらは日本楽器製造株式会社（YAMAHA）および株式会社アスキーの著作物を
フォークしたもの。レコードの形も中身も変えていない。Y8960 BASIC Extension と
MSX-AUDIO BASIC Extension Lite は、どちらも
[MSX Sound Suite Extension](https://github.com/madscient/MsxSoundSuiteExtension) に収められている。

同じファイルの SCC プリセット波形16本は Y8960 BASIC Extension が生成したもので、
上記のいずれにも当たらない。

変換器がこの表を持つのは、シーケンスデータの仕様が「レコードのある音色は
シーケンスが自分で持つ」と定めているため。拡張音色を使う OPLLDRV の曲と、
波形を決める前に音を出す MuSICA の SCC パートのデータには、その音色の
レコードが埋め込まれる。

## 情報リソース

| | |
|---|---|
| OPLLDRV と MuSICA のデータ形式（uniskie 氏による資料） | <https://github.com/uniskie/msx_music_data/tree/master/doc> |
| MSX Sound Suite Extension（Y8960 BASIC Extension を含む） | <https://github.com/madscient/MsxSoundSuiteExtension> |
| Y8960Sequencer | <https://github.com/madscient/Y8960Sequencer> |
| Y8960 MML Compiler | <https://github.com/madscient/Y8960MMLCompiler> |
