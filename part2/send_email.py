#!/usr/bin/env python3
# send_email.py
# Usage:
#   export EMAIL_APP_PASS="..."; python3 send_email.py from_email to_email "Subject" "Body text..."
#
# This script reads the Gmail app password from the environment variable EMAIL_APP_PASS.

import os, sys, smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

def usage():
    print("Usage: python3 send_email.py from_email to_email \"Subject\" \"Body text...\"")
    sys.exit(1)

if len(sys.argv) != 5:
    usage()

FROM_EMAIL = sys.argv[1]
TO_EMAIL   = sys.argv[2]
SUBJECT    = sys.argv[3]
BODY       = sys.argv[4]

APP_PASS = os.getenv("EMAIL_APP_PASS")
if not APP_PASS:
    print("ERROR: environment variable EMAIL_APP_PASS not set. Create a Gmail App Password and set EMAIL_APP_PASS before running.")
    sys.exit(2)

msg = MIMEMultipart()
msg['From'] = FROM_EMAIL
msg['To'] = TO_EMAIL
msg['Subject'] = SUBJECT
msg.attach(MIMEText(BODY, 'plain', 'utf-8'))

try:
    server = smtplib.SMTP('smtp.gmail.com', 587, timeout=30)
    server.ehlo()
    server.starttls()
    server.login(FROM_EMAIL, APP_PASS)
    server.sendmail(FROM_EMAIL, [TO_EMAIL], msg.as_string())
    server.quit()
    print("Email sent successfully")
except Exception as e:
    print("Failed to send email:", e)
    sys.exit(3)
