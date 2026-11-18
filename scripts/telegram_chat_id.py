#!/usr/bin/env python3
"""Mostra gli ID delle chat private che hanno scritto al proprio bot."""
import getpass
import json
import ssl
import urllib.error
import urllib.request

token = getpass.getpass("Token del bot (non viene mostrato): ").strip()
if not token or any(c.isspace() for c in token):
    raise SystemExit("Token non valido.")
request = urllib.request.Request(
    "https://api.telegram.org/bot" + token + "/getUpdates?timeout=0&limit=100",
    headers={"User-Agent": "GIGIPONICA-setup"},
)
try:
    with urllib.request.urlopen(request, timeout=15) as response:
        data = json.load(response)
except urllib.error.HTTPError as error:
    if error.code in (401, 404):
        message = "Telegram non riconosce il token. Copialo di nuovo da BotFather, senza virgolette."
    elif error.code == 409:
        message = "Conflitto Telegram: un webhook o un altro programma sta già usando il bot."
    else:
        message = f"Telegram ha risposto con errore HTTP {error.code}. Riprova più tardi."
    raise SystemExit(message) from None
except urllib.error.URLError as error:
    if isinstance(error.reason, ssl.SSLCertVerificationError):
        message = "Il Python in uso non riesce a verificare i certificati HTTPS. Su Mac prova /usr/bin/python3 scripts/telegram_chat_id.py"
    else:
        message = "Connessione a Telegram non disponibile. Controlla Internet e riprova."
    raise SystemExit(message) from None
except (OSError, ValueError):
    raise SystemExit("Connessione o risposta Telegram non disponibile. Controlla Internet e riprova.") from None
if not data.get("ok"):
    raise SystemExit("Telegram non ha accettato la richiesta.")
chats = {}
for update in data.get("result", []):
    chat = update.get("message", {}).get("chat", {})
    if chat.get("type") == "private" and isinstance(chat.get("id"), int):
        chats[chat["id"]] = chat.get("first_name", "Chat privata")
if not chats:
    raise SystemExit("Nessuna chat privata trovata: apri il bot su Telegram, premi Avvia e riprova.")
for chat_id, name in chats.items():
    print(f"{name}: {chat_id}")
