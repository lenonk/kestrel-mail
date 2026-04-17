#!/usr/bin/env python3
"""Send a test email from Gmail to Gmail using OAuth2 XOAUTH2 SMTP auth.

Usage: python3 scripts/send-test-email.py

Reads credentials from:
  - KWallet (kestrel-mail folder) for the refresh token
  - SQLite DB for OAuth client ID/secret and SMTP config
"""

import smtplib
import base64
import json
import subprocess
import sqlite3
import sys
import urllib.request
import urllib.parse
from datetime import datetime
from email.mime.text import MIMEText
from pathlib import Path

DB_PATH = Path.home() / ".local/share/kestrel-mail/mail.db"
EMAIL = "lenon.kitchens@gmail.com"


def kwallet_read(key):
    result = subprocess.run(
        ["kwallet-query", "-r", key, "kdewallet", "-f", "kestrel-mail"],
        capture_output=True, text=True
    )
    return result.stdout.strip()


def get_account_config():
    db = sqlite3.connect(str(DB_PATH))
    row = db.execute(
        "SELECT oauth_token_url, oauth_client_id, oauth_client_secret, smtp_host, smtp_port "
        "FROM accounts WHERE email=?", (EMAIL,)
    ).fetchone()
    db.close()
    if not row:
        raise RuntimeError(f"Account {EMAIL} not found in DB")
    return {
        "token_url": row[0],
        "client_id": row[1],
        "client_secret": row[2],
        "smtp_host": row[3],
        "smtp_port": row[4],
    }


def get_access_token(config, refresh_token):
    data = urllib.parse.urlencode({
        "client_id": config["client_id"],
        "client_secret": config["client_secret"],
        "refresh_token": refresh_token,
        "grant_type": "refresh_token",
    }).encode()
    req = urllib.request.Request(config["token_url"], data)
    resp = json.loads(urllib.request.urlopen(req).read())
    return resp["access_token"]


def main():
    refresh_token = kwallet_read(EMAIL)
    if not refresh_token:
        print("Failed to read refresh token from KWallet", file=sys.stderr)
        sys.exit(1)

    config = get_account_config()
    access_token = get_access_token(config, refresh_token)

    now = datetime.now().strftime("%H:%M:%S")
    msg = MIMEText("Test message from Kestrel dev session")
    msg["Subject"] = f"Kestrel test ({now})"
    msg["From"] = EMAIL
    msg["To"] = EMAIL

    auth_string = f"user={EMAIL}\x01auth=Bearer {access_token}\x01\x01"
    auth_b64 = base64.b64encode(auth_string.encode()).decode()

    with smtplib.SMTP(config["smtp_host"], config["smtp_port"]) as s:
        s.starttls()
        s.ehlo()
        code, resp = s.docmd("AUTH", "XOAUTH2 " + auth_b64)
        if code != 235:
            print(f"Auth failed: {code} {resp}", file=sys.stderr)
            sys.exit(1)
        s.send_message(msg)
        print(f"Sent: {msg['Subject']}")


if __name__ == "__main__":
    main()
