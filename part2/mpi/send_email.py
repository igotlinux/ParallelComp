#!/usr/bin/env python3
"""
send_email.py - Send email summary using Gmail SMTP
Usage: python3 send_email.py <from_email> <to_email> <subject> <body>
Requires: EMAIL_APP_PASS environment variable set
"""

import sys
import os
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

def send_email(from_email, to_email, subject, body):
    """Send email using Gmail SMTP with app password"""
    
    # Get app password from environment
    app_password = os.environ.get('EMAIL_APP_PASS')
    
    if not app_password:
        print("Error: EMAIL_APP_PASS environment variable not set", file=sys.stderr)
        return False
    
    try:
        # Create message
        msg = MIMEMultipart()
        msg['From'] = from_email
        msg['To'] = to_email
        msg['Subject'] = subject
        
        # Add body
        msg.attach(MIMEText(body, 'plain'))
        
        # Connect to Gmail SMTP server
        print(f"Connecting to Gmail SMTP server...")
        server = smtplib.SMTP('smtp.gmail.com', 587)
        server.starttls()
        
        # Login
        print(f"Logging in as {from_email}...")
        server.login(from_email, app_password)
        
        # Send email
        print(f"Sending email to {to_email}...")
        text = msg.as_string()
        server.sendmail(from_email, to_email, text)
        
        # Close connection
        server.quit()
        
        print("✅ Email sent successfully!")
        return True
        
    except smtplib.SMTPAuthenticationError:
        print("❌ Authentication failed. Check your email and app password.", file=sys.stderr)
        print("Make sure you're using an App Password, not your regular Gmail password.", file=sys.stderr)
        print("Generate one at: https://myaccount.google.com/apppasswords", file=sys.stderr)
        return False
        
    except smtplib.SMTPException as e:
        print(f"❌ SMTP error occurred: {e}", file=sys.stderr)
        return False
        
    except Exception as e:
        print(f"❌ Failed to send email: {e}", file=sys.stderr)
        return False

if __name__ == "__main__":
    if len(sys.argv) != 5:
        print("Usage: python3 send_email.py <from_email> <to_email> <subject> <body>")
        print("\nExample:")
        print("  export EMAIL_APP_PASS='your-app-password'")
        print("  python3 send_email.py sender@gmail.com recipient@example.com 'Subject' 'Email body'")
        sys.exit(1)
    
    from_email = sys.argv[1]
    to_email = sys.argv[2]
    subject = sys.argv[3]
    body = sys.argv[4]
    
    success = send_email(from_email, to_email, subject, body)
    sys.exit(0 if success else 1)
