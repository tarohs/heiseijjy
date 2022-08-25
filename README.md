# heiseijjy
a gadjet to make sound of JJY (Japan standard time broadcasting station) in Heisei/Showa era

各種音素材の作り方

1. 時報
時報はESP32のD/Aから出力するので、ソースに埋める。
サイン波の1周期 (またはサンプリング時間の整数倍になる周期) だけ発生させればよいので、makesinwave.py で出力した uint8_t 形式のデータをソースに埋め込む。
makesinwave.py では確認のため matplotlib で波形表示している。

2-a. JJYアナウンス
https://jjy.nict.go.jp/QandA/reference/JJYwav.html
からダウンロードできる、JJY.zipを展開する。
これらはDFPlayerをESP32からトリガして再生するため、所定のファイル名にする (手順3.)。

2-b. モールス信号
makemorse.py にて生成する。
0〜9の数字1桁、jjy、nnnnn・uuuuu・wwwwwの電離層状態の単語である。
これも手順3. で所定のファイル名にする。
gap0_1.wav、gap0_5.wav はAudacityで発生・wav書き出し (1トラック・16000Hzサンプリング・16bit) した無音0.1秒・0.5秒である。

3. ファイル名の変更
2-a.、2-b. のwavファイルは、DFPlayerをESP32からトリガして再生するため、プログラムで指定できる整数4桁を頭に付与する。
2-a.、2-b. のwavファイルをひとつのディレクトリに入れ、シェルスクリプトrenwaves.sh で名前を変更する。

4. 無音区間の結合
DFPlayerでは再生時に各ファイルの頭が0.06秒ほど欠損するため、0.1秒の無音 (0101-gap0_1.wav) をすべてのファイルの頭にsoxで結合する。
すべてのファイルを allwaves/ というディレクトリに入れ、空の gapwaves/ というディレクトリを作ってそれらのひとつ上のディレクトリでaddgap.sh を走らせる。

※ NICTからダウンロードできるJJY.zipの内容は再配布できないので手順3.、4.で処理し『0001-H00.wav』〜『0086-jst.wav』を得る
(3.のシェルスクリプトは足りないファイルについてエラーになるが無視してよい)。
2.で発生させたファイルは配布可能なので、手順3.、4.の処理済みの『0087-c-0.wav』〜『0102-gap0_5.wav』を配布物に含めている(配布物にさらに4.の手順で無音区間を結合しないよう注意)。

5. TFカードへの書き込み
TFカードのルートディレクトリには /MP3/ というディレクトリを作り、そこにすべての gapwaves/ 以下のファイルをコピーする。
