<p align="center"><img src="docs/logo.png" width="96" alt="NovaBar logo"></p>
<h1 align="center">NovaBar</h1>
<p align="center">Windows 10/11 için hafif, animasyonlu, premium görev çubuğu ve dock.<br>
Tek dosya · ~500 KB · kurulum yok · Direct2D + DirectComposition</p>

<p align="center"><img src="docs/preview-features.png" alt="NovaBar önizleme"></p>
<p align="center"><img src="docs/preview-panels.png" alt="Kontrol Merkezi ve Bugün paneli"></p>

> Görseller tasarım önizlemesidir; gerçek görünüm duvar kâğıdına ve uygulamalarına göre değişir.

## Özellikler

- **Ada tasarımı** — Başlat + sistem bilgisi, müzik çalar, dock ve saat ayrı yüzen adalarda. Adalar duvar kâğıdının bulanık, canlı renkli hâliyle dolar (buzlu cam), ince grain dokusu, parlak kenar ve yumuşak gölgeyle.
- **macOS tarzı dock** — İmlecin altındaki ikon büyüyüp adanın üstüne taşar, komşuları yer açar. Her uygulama kendi renginde parlar; aktif uygulama başlığını gösterir, altındaki gradyan çizgi yaylanarak kayar.
- **Müzik çalar** — Spotify, YouTube, Apple Music vb. (Windows medya kontrollerini kullanan her şey): albüm kapağı, şarkı/sanatçı, önceki/oynat/sonraki, ilerleme halkası.
- **Canlı pencere önizlemeleri** — İkonun üzerinde bekle: açık pencerelerin canlı küçük görüntüleri; tıkla geç, × ile kapat.
- **Kontrol Merkezi** — Ses adasına tıkla: sürüklenebilir ses kaydırıcısı, anlık ağ hızı, karanlık mod anahtarı, odak zamanlayıcısı, ekran alıntısı / pano / emoji kısayolları, Wi-Fi, Bluetooth, Ayarlar ve kilitle · uyku · yeniden başlat · kapat (yeniden başlat ve kapat ikinci tıklamada onay ister).
- **Bugün paneli** — Saate tıkla: büyük saat, hava durumu ve 6 saatlik tahmin, gezinilebilir aylık takvim.
- **Hava durumu** — Sağ adada sıcaklık ve durum ikonu ([open-meteo.com](https://open-meteo.com), anahtar gerektirmez). Şehir: `WeatherCity` ayarı.
- **Odak zamanlayıcısı** — Sol adada geri sayan halka; tıkla duraklat, sağ tıkla iptal.
- **Duvar kâğıdından renk** — Vurgu renkleri duvar kâğıdına göre kendiliğinden seçilir.
- Dakika değişince kayan saat rakamları; açılır paneller açıldıkları yeri gösteren bir uçla gelir.
- **Geri Dönüşüm Kutusu**, **sürükle-bırak ile sıralama ve sabitleme**, **klavye dili göstergesi (TR/EN)**.
- CPU/RAM halkaları, ağ · ses · pil, tekerlekle ses ayarı, saat/tarih, masaüstünü göster.
- Tam ekran oyun/videoda kendini gizler. Boştayken kare çizmez, CPU'yu neredeyse hiç kullanmaz.

## Kurulum

1. **[NovaBar.exe'yi indir](https://github.com/kiralikceset/novabar/raw/main/bin/NovaBar.exe)** ve kalıcı bir klasöre koy (ör. `C:\Araclar\NovaBar`).
2. Çalıştır. İmzasız olduğu için SmartScreen uyarısı çıkabilir: **Ek bilgi → Yine de çalıştır**.
3. Bara sağ tıkla → **Windows ile başlat**.
   NovaBar.exe'yi önce kalıcı klasörüne taşı, sonra bu seçeneği aç. Açılışta çalışmazsa exe'nin yanındaki `NovaBar.log` dosyası ne olduğunu yazar; ayrıca *Ayarlar → Uygulamalar → Başlangıç* listesinde NovaBar'ın açık olduğuna bak.

Ayarlar exe'nin yanındaki `NovaBar.ini` dosyasında (sağ tık → *Ayar dosyasını aç*): konum, yükseklik, kenar boşluğu, ada/tek parça görünüm, vurgu renkleri (`AutoAccent=0` ile elle), hava durumu şehri (`WeatherCity`), odak süresi (`FocusMinutes`) ve tüm özelliklerin aç/kapa anahtarları.

> Hava durumu için NovaBar, ayarladığın şehrin adını open-meteo.com'a gönderir. İstemezsen sağ tık → *Hava durumu* ile kapat.

**Geri alma:** Sağ tık → *NovaBar'dan çık* Windows görev çubuğunu geri getirir. Çökme olursa: `NovaBar.exe --restore` veya Görev Yöneticisi → Windows Gezgini → Yeniden başlat.

## Derleme

- Windows: [MSYS2](https://www.msys2.org/) MinGW64 kabuğunda `build.bat`
- Linux: `sudo apt install mingw-w64` ve `./build.sh`

## Bilinen sınırlar

- Sistem tepsisi ikonları gösterilmez (sağ tık menüsünden Windows görev çubuğu geri açılabilir).
- Yalnızca ana monitör.

## Lisans

[MIT](LICENSE)
