"""Builds the web flasher site in docs/: English at the root, Russian in /ru/, manifest, sitemap, robots.

Run after changing any text here or after tools/release.sh: python3 tools/build_site.py
"""

from __future__ import annotations

from datetime import date
import html
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
BASE = "https://f1devbin.github.io/grbl-wifi-bridge/"
REPO = "https://github.com/f1devbin/grbl-wifi-bridge"
VERSION = re.search(r'#define FW_VERSION "([^"]+)"', (ROOT / "src/main.c").read_text()).group(1)
FACTORY = f"firmware/grbl-wifi-bridge-esp32s3-v{VERSION}-factory.bin"
OTA = f"firmware/grbl-wifi-bridge-esp32s3-v{VERSION}-ota.bin"
ESP_WEB_TOOLS = "https://unpkg.com/esp-web-tools@10.4.0/dist/web/install-button.js?module"
LANGS = {"en": "", "ru": "ru/"}

T = {
    "en": {
        "locale": "en_US",
        "title": "GRBL Wi-Fi Bridge: Wireless LightBurn for Laser Engravers and CNC",
        "desc": "Control any USB GRBL laser engraver or CNC router over Wi-Fi from LightBurn or UGS. "
                "Flash an ESP32-S3 from your browser, plug it into the machine's USB port. Free and open source.",
        "tagline": "Wireless GRBL for laser engravers and CNC",
        "h1": "Control your GRBL laser engraver or CNC over Wi-Fi",
        "lead": "GRBL Wi-Fi Bridge turns an ESP32-S3 board into a Wi-Fi adapter for any GRBL machine with a USB "
                "port. Plug it in where the computer used to be and connect from <b>LightBurn</b>, "
                "<b>Universal Gcode Sender</b> or any sender with a TCP connection. Nothing to open, solder or "
                "reflash on the machine.",
        "switch": "Русский",
        "install": "Install firmware",
        "install_note": "Connect the ESP32-S3 to this computer through its USB-UART port, press the button and "
                        "choose the port. After flashing, enter your Wi-Fi network: the page shows a link to "
                        "the bridge when it is online.",
        "browser": "Chrome or Edge on a computer",
        "unsupported": "Your browser cannot access USB ports. Open this page in Chrome or Edge on a computer.",
        "not_allowed": "USB access needs a secure page. Open the https:// address of this page.",
        "fw_line": f"Firmware {VERSION} · ESP32-S3 · erases the board's flash",
        "ota_link": "Download the update file for the web panel",
        "how": "How it works",
        "steps": [
            ("Flash", "Install the firmware from this page and pick your Wi-Fi network. No toolchain, no drivers."),
            ("Plug in", "Power the ESP32-S3 from a USB adapter and connect the machine to its native USB port."),
            ("Connect", "Add the bridge in LightBurn or UGS as a TCP device on port 23 and send jobs as usual."),
        ],
        "features": "Features",
        "feat": [
            ("USB host", "Works with the machine's own USB chip: CH340/CH341, CP210x, FTDI and CDC-ACM devices."),
            ("Transparent TCP", "Port 23 passes bytes unchanged: streaming, real-time commands and $ settings "
                                "work as over a cable."),
            ("Web panel", "Status, big STOP button, pause and resume, GRBL console, settings and updates "
                          "in any browser, on a phone too."),
            ("Emergency stop", "If the sender disconnects during a job, the bridge sends feed hold and a GRBL "
                               "reset: motion stops, laser and spindle switch off."),
            ("Easy Wi-Fi setup", "From this page, from the setup access point or from the web panel. "
                                 "mDNS name grbl-bridge.local."),
            ("Safe updates", "Over-the-air updates with automatic rollback if a new image does not start."),
        ],
        "software": "Connect your software",
        "sw": [
            ("LightBurn", "Devices → Create Manually → GRBL → Ethernet/TCP → bridge address, port 23."),
            ("Universal Gcode Sender", "Tools → Options → Sender Options → Connection driver: TCP, then "
                                       "connect to the bridge address, port 23."),
            ("Serial-only programs", "Create a virtual serial port on Linux or macOS:"),
        ],
        "hw": "What you need",
        "hw_items": [
            "An ESP32-S3 board with native USB OTG on GPIO19/GPIO20 and at least 8 MB of flash. "
            "Boards in the ESP32-S3-DevKitC-1 style with two USB-C ports are the easiest.",
            "5 V on the native USB port so the machine's USB chip is powered. On many dual-USB-C boards this needs "
            "a solder jumper (often marked USB-OTG). Check first that the machine does not drive 5 V on its own "
            "USB port.",
            "A USB power adapter for the ESP32-S3.",
            "Any machine running GRBL or grblHAL with a USB serial port: diode laser engravers, 3018 CNC routers, "
            "pen plotters.",
        ],
        "faq": "FAQ",
        "faqs": [
            ("Does it work with LightBurn over Wi-Fi?",
             "Yes. Add a GRBL device with the Ethernet/TCP connection type, the bridge address and port 23. "
             "LightBurn then streams jobs over Wi-Fi exactly as over USB."),
            ("Which machines are supported?",
             "Any controller that speaks GRBL or grblHAL through a USB serial chip (CH340, CP210x, FTDI or "
             "CDC-ACM): most hobby diode laser engravers, 3018-style CNC routers and GRBL pen plotters. "
             "Controllers that already have Wi-Fi, such as FluidNC, do not need it."),
            ("Do I have to open or modify the machine?",
             "No. The bridge plugs into the machine's existing USB port. The only soldering may be a jumper on "
             "the ESP32-S3 board that powers its USB port."),
            ("Why ESP32-S3 and not a regular ESP32?",
             "The bridge acts as a USB host for the machine's USB chip. The classic ESP32, ESP32-C3 and ESP32-C6 "
             "have no USB host; the ESP32-S3 does."),
            ("What happens if Wi-Fi drops during a job?",
             "When the connection to the sender breaks while GRBL is running, the bridge sends feed hold and a "
             "GRBL reset, which stops motion and switches the laser or spindle off. The job has to be restarted."),
            ("Is it safe to use on my network?",
             "The web panel has no login, so anyone on your local network can open it. Use the bridge on a "
             "trusted home or workshop network and never expose it to the internet."),
            ("How do I update the firmware?",
             "Upload the update file in the web panel, or flash the latest version from this page again."),
        ],
        "source": "Source code on GitHub",
        "license": "MIT license",
        "made": "Made by",
        "skip": "Skip to content",
    },
    "ru": {
        "locale": "ru_RU",
        "title": "GRBL Wi-Fi Bridge: лазерный гравер и ЧПУ по Wi-Fi из LightBurn",
        "desc": "Управляйте любым USB GRBL лазерным гравером или фрезером ЧПУ по Wi-Fi из LightBurn или UGS. "
                "Прошейте ESP32-S3 прямо в браузере и подключите к USB станка. Бесплатно, открытый код.",
        "tagline": "GRBL по Wi-Fi для лазерных граверов и ЧПУ",
        "h1": "Управляйте GRBL гравером или фрезером по Wi-Fi",
        "lead": "GRBL Wi-Fi Bridge превращает плату ESP32-S3 в Wi-Fi адаптер для любого GRBL-станка с USB-портом. "
                "Подключите её вместо компьютера и работайте из <b>LightBurn</b>, <b>Universal Gcode Sender</b> "
                "или любой программы с подключением по TCP. Станок не нужно вскрывать, паять или перепрошивать.",
        "switch": "English",
        "install": "Установить прошивку",
        "install_note": "Подключите ESP32-S3 к компьютеру через порт USB-UART, нажмите кнопку и выберите порт. "
                        "После прошивки укажите сеть Wi-Fi: страница покажет ссылку на мост, когда он подключится.",
        "browser": "Chrome или Edge на компьютере",
        "unsupported": "Ваш браузер не умеет работать с USB-портами. Откройте страницу в Chrome или Edge на компьютере.",
        "not_allowed": "Для доступа к USB нужна защищённая страница. Откройте адрес страницы через https://.",
        "fw_line": f"Прошивка {VERSION} · ESP32-S3 · flash платы будет стёрта",
        "ota_link": "Скачать файл обновления для веб-панели",
        "how": "Как это работает",
        "steps": [
            ("Прошить", "Установите прошивку с этой страницы и выберите сеть Wi-Fi. Без среды разработки и драйверов."),
            ("Подключить", "Подайте питание на ESP32-S3 от USB-адаптера и подключите станок к её встроенному USB-порту."),
            ("Работать", "Добавьте мост в LightBurn или UGS как TCP-устройство на порту 23 и отправляйте задания как обычно."),
        ],
        "features": "Возможности",
        "feat": [
            ("USB host", "Работает с USB-чипом самого станка: CH340/CH341, CP210x, FTDI и CDC-ACM устройства."),
            ("Прозрачный TCP", "Порт 23 передаёт байты без изменений: потоковая отправка, команды реального "
                               "времени и настройки $ работают как по кабелю."),
            ("Веб-панель", "Состояние, большая кнопка СТОП, пауза и продолжение, консоль GRBL, настройки и "
                           "обновления в любом браузере, в том числе на телефоне."),
            ("Аварийный стоп", "Если программа отключилась во время работы, мост отправляет паузу и сброс GRBL: "
                               "движение останавливается, лазер и шпиндель выключаются."),
            ("Простая настройка Wi-Fi", "С этой страницы, через точку доступа настройки или в веб-панели. "
                                        "Имя в сети grbl-bridge.local."),
            ("Безопасные обновления", "Обновление по воздуху с автоматическим откатом, если новая прошивка не запустилась."),
        ],
        "software": "Подключение программ",
        "sw": [
            ("LightBurn", "Devices → Create Manually → GRBL → Ethernet/TCP → адрес моста, порт 23."),
            ("Universal Gcode Sender", "Tools → Options → Sender Options → Connection driver: TCP, затем "
                                       "подключение к адресу моста, порт 23."),
            ("Программы только с последовательным портом", "Создайте виртуальный порт в Linux или macOS:"),
        ],
        "hw": "Что нужно",
        "hw_items": [
            "Плата ESP32-S3 со встроенным USB OTG на GPIO19/GPIO20 и flash от 8 МБ. Проще всего с платами в "
            "стиле ESP32-S3-DevKitC-1 с двумя разъёмами USB-C.",
            "5 В на встроенном USB-порту, чтобы питался USB-чип станка. На многих платах с двумя USB-C для этого "
            "нужно запаять перемычку (часто подписана USB-OTG). Сначала проверьте, что станок сам не выдаёт 5 В "
            "в свой USB-порт.",
            "USB-адаптер питания для ESP32-S3.",
            "Любой станок на GRBL или grblHAL с USB-портом: диодные лазерные граверы, фрезеры 3018, плоттеры.",
        ],
        "faq": "Вопросы и ответы",
        "faqs": [
            ("Работает ли с LightBurn по Wi-Fi?",
             "Да. Добавьте GRBL-устройство с типом подключения Ethernet/TCP, адресом моста и портом 23. "
             "LightBurn будет отправлять задания по Wi-Fi так же, как по USB."),
            ("Какие станки поддерживаются?",
             "Любые контроллеры на GRBL или grblHAL с USB-UART чипом (CH340, CP210x, FTDI или CDC-ACM): "
             "большинство любительских диодных лазерных граверов, фрезеры типа 3018 и GRBL-плоттеры. "
             "Контроллерам, у которых уже есть Wi-Fi (например, FluidNC), мост не нужен."),
            ("Нужно ли вскрывать или переделывать станок?",
             "Нет. Мост подключается к уже имеющемуся USB-порту станка. Паять, возможно, придётся только "
             "перемычку на плате ESP32-S3, которая подаёт питание на её USB-порт."),
            ("Почему ESP32-S3, а не обычный ESP32?",
             "Мост работает как USB host для USB-чипа станка. У классического ESP32, ESP32-C3 и ESP32-C6 USB host "
             "нет, а у ESP32-S3 есть."),
            ("Что будет, если Wi-Fi пропадёт во время работы?",
             "Если связь с программой оборвалась, пока GRBL работает, мост отправляет паузу и сброс GRBL: "
             "движение останавливается, лазер или шпиндель выключаются. Задание придётся запустить заново."),
            ("Безопасно ли это для моей сети?",
             "У веб-панели нет пароля, её может открыть любой в локальной сети. Используйте мост в доверенной "
             "домашней или мастерской сети и не открывайте его в интернет."),
            ("Как обновить прошивку?",
             "Загрузите файл обновления в веб-панели или снова прошейте последнюю версию с этой страницы."),
        ],
        "source": "Исходный код на GitHub",
        "license": "Лицензия MIT",
        "made": "Автор",
        "skip": "Перейти к содержимому",
    },
}

CSS = """
:root{--bg:#0a0d12;--card:#151b23;--card2:#1b232d;--line:#33404f;--acc:#ffd400;--acc2:#ffb800;--ok:#3ddc84;
--bad:#ff5a5a;--info:#5cc8ff;--r:18px;--shadow:0 14px 36px rgba(0,0,0,.5),0 2px 6px rgba(0,0,0,.35)}
*{box-sizing:border-box}
html{scroll-behavior:smooth;-webkit-text-size-adjust:100%}
body{margin:0;color:#fff;font:19px/1.6 system-ui,-apple-system,"Segoe UI",Roboto,Ubuntu,sans-serif;
background:radial-gradient(1200px 600px at 10% -10%,rgba(255,212,0,.13),transparent 60%),
radial-gradient(900px 600px at 100% 0%,rgba(92,200,255,.12),transparent 55%),var(--bg);background-attachment:fixed}
a{color:var(--acc);text-underline-offset:3px}
a:hover{color:#fff}
.skip{position:absolute;left:-999px;top:8px;background:var(--acc);color:#111;padding:8px 14px;border-radius:8px;z-index:20}
.skip:focus{left:8px}
.ic{width:26px;height:26px;flex:none;fill:none;stroke:currentColor;stroke-width:2.2;stroke-linecap:round;stroke-linejoin:round}
.wrap{max-width:1120px;margin:0 auto;padding:0 18px}
header{position:sticky;top:0;z-index:10;backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);
background:rgba(10,13,18,.8);border-bottom:1px solid var(--line)}
.bar{display:flex;align-items:center;gap:14px;padding:12px 0}
.brand{display:flex;align-items:center;gap:12px;margin-right:auto;color:#fff;text-decoration:none;font-weight:800;font-size:21px}
.mark{width:42px;height:42px;border-radius:12px;display:grid;place-items:center;color:#111;
background:linear-gradient(135deg,var(--acc),var(--acc2));box-shadow:0 6px 18px rgba(255,200,0,.35)}
.lang,.gh{display:inline-flex;align-items:center;gap:8px;padding:8px 14px;border-radius:12px;border:1px solid var(--line);
background:var(--card2);color:#fff;font-weight:700;text-decoration:none;box-shadow:var(--shadow);transition:border-color .2s,transform .2s}
.lang:hover,.gh:hover{border-color:#fff;transform:translateY(-1px);color:#fff}
.hero{padding:56px 0 28px;display:grid;gap:36px;align-items:center}
@media(min-width:960px){.hero{grid-template-columns:1.15fr .85fr}}
.eyebrow{display:inline-flex;align-items:center;gap:8px;padding:6px 14px;border-radius:999px;font-weight:800;font-size:16px;
color:var(--acc);background:rgba(255,212,0,.1);border:1px solid rgba(255,212,0,.45)}
h1{font-size:clamp(34px,5vw,56px);line-height:1.08;margin:18px 0;letter-spacing:-.5px}
h1 em{font-style:normal;background:linear-gradient(90deg,var(--acc),#ffe98a);-webkit-background-clip:text;background-clip:text;color:transparent}
.lead{font-size:21px;margin:0}
.card{background:linear-gradient(180deg,var(--card2),var(--card));border:1px solid var(--line);border-radius:var(--r);
padding:26px;box-shadow:var(--shadow),inset 0 1px 0 rgba(255,255,255,.05)}
.install h2{margin-top:0}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:12px;width:100%;min-height:64px;padding:14px 24px;
border:0;border-radius:14px;font:inherit;font-size:22px;font-weight:900;color:#111;cursor:pointer;
background:linear-gradient(180deg,#ffe14d,var(--acc) 55%,var(--acc2));
box-shadow:0 10px 26px rgba(255,200,0,.35),inset 0 1px 0 rgba(255,255,255,.55);transition:transform .15s,box-shadow .2s,filter .2s;
animation:glow 2.6s ease-in-out infinite}
.btn:hover{transform:translateY(-2px);filter:brightness(1.06)}
.btn:active{transform:none}
.btn:focus-visible{outline:3px solid #fff;outline-offset:3px}
.note{margin:16px 0 0;font-size:17px}
.meta{display:flex;flex-wrap:wrap;gap:8px;margin-top:16px}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 12px;border-radius:999px;font-size:15px;font-weight:700;
border:1px solid var(--line);background:rgba(0,0,0,.3)}
.warn{display:block;padding:14px 16px;border-radius:12px;background:rgba(255,90,90,.12);border:1px solid var(--bad);font-weight:700}
section{padding:30px 0}
h2{font-size:clamp(26px,3.4vw,34px);margin:0 0 20px;line-height:1.2}
h3{font-size:21px;margin:0 0 6px;color:var(--acc)}
.grid{display:grid;gap:18px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}
.item{transition:transform .2s,border-color .2s}
.item:hover{transform:translateY(-3px);border-color:#5a6a7c}
.item p{margin:0}
.num{width:44px;height:44px;border-radius:50%;display:grid;place-items:center;font-weight:900;font-size:20px;color:#111;
background:linear-gradient(135deg,var(--acc),var(--acc2));box-shadow:0 6px 16px rgba(255,200,0,.3);margin-bottom:14px}
.icon{width:48px;height:48px;border-radius:14px;display:grid;place-items:center;margin-bottom:14px;color:var(--acc);
background:rgba(255,212,0,.1);border:1px solid rgba(255,212,0,.35)}
pre{margin:12px 0 0;padding:14px 16px;border-radius:12px;background:#05070a;border:1px solid var(--line);overflow:auto;
font:16px/1.5 ui-monospace,"SFMono-Regular",Consolas,monospace;color:#fff}
ul.list{margin:0;padding:0;list-style:none;display:grid;gap:12px}
ul.list li{position:relative;padding:14px 16px 14px 50px;border-radius:14px;background:rgba(0,0,0,.28);border:1px solid var(--line)}
ul.list li::before{content:"";position:absolute;left:16px;top:18px;width:20px;height:20px;border-radius:50%;
background:var(--ok) url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23111' stroke-width='4' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='M20 6L9 17l-5-5'/%3E%3C/svg%3E") center/13px no-repeat}
details{border-radius:14px;background:rgba(0,0,0,.28);border:1px solid var(--line);margin-bottom:12px;transition:border-color .2s}
details[open]{border-color:var(--acc)}
summary{cursor:pointer;padding:16px 20px;font-weight:800;font-size:20px;list-style:none;display:flex;align-items:center;gap:12px}
summary::-webkit-details-marker{display:none}
summary::after{content:"+";margin-left:auto;font-size:28px;line-height:1;color:var(--acc);transition:transform .25s}
details[open] summary::after{transform:rotate(45deg)}
details p{margin:0;padding:0 20px 18px}
footer{border-top:1px solid var(--line);margin-top:30px;padding:26px 0 40px;font-weight:600}
footer .wrap{display:flex;flex-wrap:wrap;gap:16px 28px;align-items:center}
.reveal{opacity:0;transform:translateY(18px);transition:opacity .6s ease,transform .6s cubic-bezier(.2,.7,.2,1)}
.reveal.in{opacity:1;transform:none}
@keyframes glow{0%,100%{box-shadow:0 10px 26px rgba(255,200,0,.3),inset 0 1px 0 rgba(255,255,255,.55)}
50%{box-shadow:0 10px 40px rgba(255,200,0,.6),inset 0 1px 0 rgba(255,255,255,.55)}}
@keyframes rise{from{opacity:0;transform:translateY(16px)}to{opacity:1;transform:none}}
.hero>*{animation:rise .7s cubic-bezier(.2,.7,.2,1) both}.hero>*:nth-child(2){animation-delay:.12s}
@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation:none!important;transition:none!important}.reveal{opacity:1;transform:none}html{scroll-behavior:auto}}
@media(max-width:560px){body{font-size:18px}.brand span{display:none}.gh span{display:none}.card{padding:20px}}
"""

ICONS = {
    "bolt": '<path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/>',
    "usb": '<path d="M12 2v14M8 6l4-4 4 4M6 12v2a2 2 0 0 0 2 2h8a2 2 0 0 0 2-2v-2M12 16v4"/><circle cx="12" cy="21" r="1"/>',
    "net": '<path d="M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0M12 20h.01"/>',
    "panel": '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18M8 14h3"/>',
    "stop": '<path d="M7.86 2h8.28L22 7.86v8.28L16.14 22H7.86L2 16.14V7.86z"/><path d="M15 9l-6 6M9 9l6 6"/>',
    "wifi": '<path d="M12 20h.01M8.5 16.4a5 5 0 0 1 7 0M5 12.9a10 10 0 0 1 14 0"/>',
    "refresh": '<path d="M23 4v6h-6M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>',
    "gh": '<path d="M9 19c-5 1.5-5-2.5-7-3m14 6v-3.87a3.37 3.37 0 0 0-.94-2.61c3.14-.35 6.44-1.54 6.44-7A5.44 5.44 0 0 0 20 4.77 5.07 5.07 0 0 0 19.91 1S18.73.65 16 2.48a13.38 13.38 0 0 0-7 0C6.27.65 5.09 1 5.09 1A5.07 5.07 0 0 0 5 4.77a5.44 5.44 0 0 0-1.5 3.78c0 5.42 3.3 6.61 6.44 7A3.37 3.37 0 0 0 9 18.13V22"/>',
    "lang": '<circle cx="12" cy="12" r="10"/><path d="M2 12h20M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/>',
    "dl": '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"/>',
}
FEATURE_ICONS = ["usb", "net", "panel", "stop", "wifi", "refresh"]


def icon(name: str) -> str:
    return f'<svg class="ic" viewBox="0 0 24 24" aria-hidden="true">{ICONS[name]}</svg>'


def esc(s: str) -> str:
    return html.escape(s, quote=True)


def plain(s: str) -> str:
    return re.sub(r"<[^>]+>", "", s)


def page(lang: str) -> str:
    t = T[lang]
    url = BASE + LANGS[lang]
    up = "../" if LANGS[lang] else ""
    other = "ru" if lang == "en" else "en"
    alternates = "\n".join(
        f'<link rel="alternate" hreflang="{l}" href="{BASE + p}">' for l, p in LANGS.items()
    ) + f'\n<link rel="alternate" hreflang="x-default" href="{BASE}">'

    ld = [
        {
            "@context": "https://schema.org",
            "@type": "SoftwareApplication",
            "name": "GRBL Wi-Fi Bridge",
            "description": t["desc"],
            "url": url,
            "inLanguage": lang,
            "applicationCategory": "UtilitiesApplication",
            "operatingSystem": "ESP32-S3 firmware",
            "softwareVersion": VERSION,
            "downloadUrl": BASE + FACTORY,
            "codeRepository": REPO,
            "license": "https://opensource.org/licenses/MIT",
            "isAccessibleForFree": True,
            "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"},
            "author": {"@type": "Person", "name": "f1devbin", "url": "https://f1devbin.github.io/"},
            "image": BASE + "og.png",
            "keywords": "GRBL WiFi, LightBurn WiFi, laser engraver WiFi, CNC WiFi, ESP32-S3 USB host, "
                        "wireless GRBL, GRBL telnet, web flasher",
        },
        {
            "@context": "https://schema.org",
            "@type": "FAQPage",
            "mainEntity": [
                {"@type": "Question", "name": q, "acceptedAnswer": {"@type": "Answer", "text": a}}
                for q, a in t["faqs"]
            ],
        },
    ]

    steps = "".join(
        f'<div class="card item reveal"><div class="num">{i}</div><h3>{esc(h)}</h3><p>{esc(p)}</p></div>'
        for i, (h, p) in enumerate(t["steps"], 1)
    )
    feats = "".join(
        f'<div class="card item reveal"><div class="icon">{icon(FEATURE_ICONS[i])}</div><h3>{esc(h)}</h3><p>{esc(p)}</p></div>'
        for i, (h, p) in enumerate(t["feat"])
    )
    sw = "".join(
        f'<div class="card item reveal"><h3>{esc(h)}</h3><p>{esc(p)}</p>'
        + ('<pre>socat pty,link=$HOME/ttyGRBL,raw,echo=0 \\\n  tcp:grbl-bridge.local:23</pre>' if i == 2 else "")
        + "</div>"
        for i, (h, p) in enumerate(t["sw"])
    )
    hw = "".join(f"<li>{esc(x)}</li>" for x in t["hw_items"])
    faqs = "".join(f"<details class=\"reveal\"><summary>{esc(q)}</summary><p>{esc(a)}</p></details>" for q, a in t["faqs"])
    h1 = t["h1"].replace("Wi-Fi", "<em>Wi-Fi</em>")

    return f"""<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(t["title"])}</title>
<meta name="description" content="{esc(t["desc"])}">
<meta name="robots" content="index, follow, max-image-preview:large">
<meta name="theme-color" content="#0a0d12">
<link rel="canonical" href="{url}">
{alternates}
<link rel="icon" type="image/png" href="{up}icon.png">
<link rel="apple-touch-icon" href="{up}icon.png">
<meta property="og:type" content="website">
<meta property="og:site_name" content="GRBL Wi-Fi Bridge">
<meta property="og:title" content="{esc(t["title"])}">
<meta property="og:description" content="{esc(t["desc"])}">
<meta property="og:url" content="{url}">
<meta property="og:image" content="{BASE}og.png">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta property="og:locale" content="{t["locale"]}">
<meta name="twitter:card" content="summary_large_image">
<script type="application/ld+json">{json.dumps(ld, ensure_ascii=False)}</script>
<script type="module" src="{ESP_WEB_TOOLS}"></script>
<style>{CSS.strip()}</style>
</head>
<body>
<a class="skip" href="#main">{esc(t["skip"])}</a>
<header><div class="wrap bar">
<a class="brand" href="{url}"><span class="mark">{icon("bolt")}</span><span>GRBL Wi-Fi Bridge</span></a>
<a class="lang" href="{BASE + LANGS[other]}" hreflang="{other}" lang="{other}">{icon("lang")}{esc(t["switch"])}</a>
<a class="gh" href="{REPO}">{icon("gh")}<span>GitHub</span></a>
</div></header>
<main id="main" class="wrap">
<div class="hero">
<div>
<span class="eyebrow">{icon("bolt")}{esc(t["tagline"])}</span>
<h1>{h1}</h1>
<p class="lead">{t["lead"]}</p>
</div>
<div class="card install">
<esp-web-install-button manifest="{up}manifest.json">
<button slot="activate" class="btn">{icon("usb")}{esc(t["install"])}</button>
<span slot="unsupported" class="warn">{esc(t["unsupported"])}</span>
<span slot="not-allowed" class="warn">{esc(t["not_allowed"])}</span>
</esp-web-install-button>
<p class="note">{esc(t["install_note"])}</p>
<div class="meta"><span class="chip">{esc(t["browser"])}</span><span class="chip">{esc(t["fw_line"])}</span></div>
<p class="note"><a href="{up}{OTA}" download>{esc(t["ota_link"])}</a></p>
</div>
</div>
<section><h2>{esc(t["how"])}</h2><div class="grid">{steps}</div></section>
<section><h2>{esc(t["features"])}</h2><div class="grid">{feats}</div></section>
<section><h2>{esc(t["software"])}</h2><div class="grid">{sw}</div></section>
<section><h2>{esc(t["hw"])}</h2><ul class="list reveal">{hw}</ul></section>
<section><h2>{esc(t["faq"])}</h2>{faqs}</section>
</main>
<footer><div class="wrap">
<a href="{REPO}">{esc(t["source"])}</a>
<a href="{REPO}/blob/main/LICENSE">{esc(t["license"])}</a>
<span>{esc(t["made"])} <a href="https://f1devbin.github.io/">f1devbin</a></span>
</div></footer>
<script>
const io='IntersectionObserver' in window?new IntersectionObserver(es=>es.forEach(e=>{{if(e.isIntersecting){{e.target.classList.add('in');io.unobserve(e.target)}}}}),{{threshold:.12}}):null;
document.querySelectorAll('.reveal').forEach(el=>io?io.observe(el):el.classList.add('in'));
</script>
</body>
</html>
"""


def main() -> None:
    DOCS.mkdir(exist_ok=True)
    (DOCS / ".nojekyll").write_text("")
    for lang, sub in LANGS.items():
        out = DOCS / sub / "index.html"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(page(lang), encoding="utf-8")

    manifest = {
        "name": "GRBL Wi-Fi Bridge",
        "version": VERSION,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 20,
        "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": FACTORY, "offset": 0}]}],
    }
    (DOCS / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    today = date.today().isoformat()
    links = "".join(
        f'<xhtml:link rel="alternate" hreflang="{l}" href="{BASE + p}"/>' for l, p in LANGS.items()
    ) + f'<xhtml:link rel="alternate" hreflang="x-default" href="{BASE}"/>'
    urls = "\n".join(f"<url><loc>{BASE + p}</loc><lastmod>{today}</lastmod>{links}</url>" for p in LANGS.values())
    (DOCS / "sitemap.xml").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">\n'
        f"{urls}\n</urlset>\n"
    )
    (DOCS / "robots.txt").write_text(f"User-agent: *\nAllow: /\n\nSitemap: {BASE}sitemap.xml\n")

    for part in (FACTORY, OTA):
        if not (DOCS / part).exists():
            print(f"warning: {part} missing, run tools/release.sh")
    print(f"site built for firmware {VERSION}")


if __name__ == "__main__":
    main()
