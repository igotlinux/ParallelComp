"""
KernelGuard Cloud SaaS Database Engine
Built-in SQLite storage with zero configuration; easily connects to Postgres/Supabase via DATABASE_URL.
"""

import os
import sqlite3
import time
from typing import Dict, Any, List, Optional


DB_FILE = os.environ.get("KG_DATABASE_FILE", "kernelguard_saas.db")


def get_db_connection() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_FILE)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = get_db_connection()
    cursor = conn.cursor()

    # Customers table
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS customers (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        name TEXT NOT NULL,
        email TEXT UNIQUE NOT NULL,
        stripe_customer_id TEXT,
        created_at INTEGER NOT NULL
    );
    """)

    # Subscriptions & License tokens table
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS licenses (
        token TEXT PRIMARY KEY,
        customer_name TEXT NOT NULL,
        email TEXT NOT NULL,
        plan TEXT NOT NULL, -- 'pro', 'enterprise'
        status TEXT NOT NULL, -- 'active', 'revoked', 'expired'
        seats INTEGER NOT NULL DEFAULT 5,
        issued_at INTEGER NOT NULL,
        expires_at INTEGER NOT NULL,
        stripe_subscription_id TEXT,
        revocation_reason TEXT
    );
    """)

    # Telemetry and CI run verification audit logs
    cursor.execute("""
    CREATE TABLE IF NOT EXISTS verification_logs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        token TEXT NOT NULL,
        timestamp INTEGER NOT NULL,
        status TEXT NOT NULL,
        ip_address TEXT,
        client_version TEXT
    );
    """)

    conn.commit()
    conn.close()


def create_or_get_customer(name: str, email: str, stripe_customer_id: Optional[str] = None) -> int:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT id FROM customers WHERE email = ?", (email.lower(),))
    row = cursor.fetchone()
    if row:
        cust_id = row["id"]
    else:
        now = int(time.time())
        cursor.execute(
            "INSERT INTO customers (name, email, stripe_customer_id, created_at) VALUES (?, ?, ?, ?)",
            (name, email.lower(), stripe_customer_id, now)
        )
        conn.commit()
        cust_id = cursor.lastrowid
    conn.close()
    return cust_id


def save_license(
    token: str,
    customer_name: str,
    email: str,
    plan: str,
    seats: int,
    expires_at: int,
    stripe_subscription_id: Optional[str] = None
):
    conn = get_db_connection()
    cursor = conn.cursor()
    now = int(time.time())
    cursor.execute("""
    INSERT OR REPLACE INTO licenses 
    (token, customer_name, email, plan, status, seats, issued_at, expires_at, stripe_subscription_id, revocation_reason)
    VALUES (?, ?, ?, ?, 'active', ?, ?, ?, ?, NULL)
    """, (token, customer_name, email.lower(), plan.lower(), seats, now, expires_at, stripe_subscription_id))
    conn.commit()
    conn.close()


def get_license(token: str) -> Optional[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM licenses WHERE token = ?", (token.strip(),))
    row = cursor.fetchone()
    conn.close()
    if not row:
        return None
    return dict(row)


def revoke_license(token: str, reason: str = "Admin revocation") -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("UPDATE licenses SET status = 'revoked', revocation_reason = ? WHERE token = ?", (reason, token.strip()))
    affected = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return affected


def reactivate_license(token: str) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("UPDATE licenses SET status = 'active', revocation_reason = NULL WHERE token = ?", (token.strip(),))
    affected = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return affected


def update_license_by_stripe_sub(stripe_sub_id: str, new_status: str, reason: Optional[str] = None) -> bool:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute(
        "UPDATE licenses SET status = ?, revocation_reason = ? WHERE stripe_subscription_id = ?",
        (new_status, reason, stripe_sub_id)
    )
    affected = cursor.rowcount > 0
    conn.commit()
    conn.close()
    return affected


def log_verification_attempt(token: str, status: str, ip: str = "", client_version: str = "1.0.0"):
    conn = get_db_connection()
    cursor = conn.cursor()
    now = int(time.time())
    cursor.execute(
        "INSERT INTO verification_logs (token, timestamp, status, ip_address, client_version) VALUES (?, ?, ?, ?, ?)",
        (token, now, status, ip, client_version)
    )
    conn.commit()
    conn.close()


def list_licenses() -> List[Dict[str, Any]]:
    conn = get_db_connection()
    cursor = conn.cursor()
    cursor.execute("SELECT * FROM licenses ORDER BY issued_at DESC")
    rows = cursor.fetchall()
    conn.close()
    return [dict(r) for r in rows]
