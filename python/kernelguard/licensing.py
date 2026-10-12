"""
KernelGuard Commercial Licensing & Token Management System
Supports offline cryptographically-signed tokens for CI/CD runners.
"""

import base64
import hashlib
import hmac
import json
import os
import time
from dataclasses import dataclass
from typing import Dict, Any, List, Optional, Tuple


# Default master secret salt for token verification
# In production, the vendor can override this via KG_MASTER_SIGNING_KEY
DEFAULT_MASTER_KEY = os.environ.get(
    "KG_MASTER_SIGNING_KEY",
    "kg_sec_99a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4_kernelguard_master"
)


@dataclass
class LicenseInfo:
    is_valid: bool
    plan: str              # "community", "pro", "enterprise"
    customer: str          # e.g. "Apex AI Labs"
    email: str             # e.g. "cto@apex-ai.internal"
    seats: int
    expires_at_timestamp: int
    expires_at_str: str
    is_expired: bool
    features: List[str]
    error_message: Optional[str] = None


def generate_license_token(
    customer: str,
    email: str,
    plan: str = "pro",
    seats: int = 5,
    valid_days: int = 365,
    signing_key: str = DEFAULT_MASTER_KEY
) -> str:
    """
    Generates a cryptographically signed offline commercial token for a customer.
    Format: KG_<PLAN>_<base64_payload>.<signature>
    """
    plan_clean = plan.lower().strip()
    if plan_clean not in ["pro", "enterprise"]:
        plan_clean = "pro"

    now = int(time.time())
    expires_at = now + (valid_days * 86400)

    features = [
        "automated_pr_gate",
        "delta_debugging",
        "repro_generator",
        "vectorizer_tracking",
        "lockfile_regression_gate",
    ]
    if plan_clean == "enterprise":
        features.extend([
            "hardware_gpu_runners",
            "custom_ptx_budgets",
            "sla_support",
            "unlimited_repos"
        ])

    payload = {
        "customer": customer,
        "email": email,
        "plan": plan_clean,
        "seats": seats,
        "issued_at": now,
        "expires_at": expires_at,
        "features": features,
    }

    payload_json = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
    payload_b64 = base64.urlsafe_b64encode(payload_json).decode("utf-8").rstrip("=")

    # HMAC-SHA256 signature
    sig = hmac.new(signing_key.encode("utf-8"), payload_b64.encode("utf-8"), hashlib.sha256).hexdigest()

    prefix = "KG_ENT" if plan_clean == "enterprise" else "KG_PRO"
    return f"{prefix}_{payload_b64}.{sig}"


def verify_license_token(
    token_str: Optional[str],
    signing_key: str = DEFAULT_MASTER_KEY
) -> LicenseInfo:
    """
    Validates a commercial license token string.
    Returns LicenseInfo with plan and validity details.
    """
    if not token_str or not token_str.strip():
        return LicenseInfo(
            is_valid=False,
            plan="community",
            customer="Open Source / Community",
            email="",
            seats=1,
            expires_at_timestamp=0,
            expires_at_str="Permanent",
            is_expired=False,
            features=["double_double_oracle", "cli_matrix", "basic_contracts"],
            error_message="No license key provided (running Community Edition)."
        )

    clean_token = token_str.strip()

    # Format check
    if not (clean_token.startswith("KG_PRO_") or clean_token.startswith("KG_ENT_")):
        return LicenseInfo(
            is_valid=False,
            plan="invalid",
            customer="Unknown",
            email="",
            seats=0,
            expires_at_timestamp=0,
            expires_at_str="",
            is_expired=True,
            features=[],
            error_message="Invalid token format. Token must begin with 'KG_PRO_' or 'KG_ENT_'."
        )

    try:
        # Strip prefix
        rest = clean_token.split("_", 2)[2]
        payload_b64, signature = rest.split(".", 1)

        # Verify signature
        expected_sig = hmac.new(
            signing_key.encode("utf-8"),
            payload_b64.encode("utf-8"),
            hashlib.sha256
        ).hexdigest()

        if not hmac.compare_digest(signature, expected_sig):
            return LicenseInfo(
                is_valid=False,
                plan="invalid",
                customer="Tampered",
                email="",
                seats=0,
                expires_at_timestamp=0,
                expires_at_str="",
                is_expired=True,
                features=[],
                error_message="Cryptographic signature verification failed. Token is tampered or invalid."
            )

        # Decode payload (add padding back if missing)
        padding = "=" * ((4 - len(payload_b64) % 4) % 4)
        payload_raw = base64.urlsafe_b64decode(payload_b64 + padding).decode("utf-8")
        data: Dict[str, Any] = json.loads(payload_raw)

        now = int(time.time())
        expires_at = data.get("expires_at", 0)
        is_expired = now > expires_at

        exp_time_str = time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime(expires_at))

        if is_expired:
            return LicenseInfo(
                is_valid=False,
                plan=data.get("plan", "pro"),
                customer=data.get("customer", "Unknown"),
                email=data.get("email", ""),
                seats=data.get("seats", 1),
                expires_at_timestamp=expires_at,
                expires_at_str=exp_time_str,
                is_expired=True,
                features=data.get("features", []),
                error_message=f"License expired on {exp_time_str}."
            )

        return LicenseInfo(
            is_valid=True,
            plan=data.get("plan", "pro"),
            customer=data.get("customer", "Unknown"),
            email=data.get("email", ""),
            seats=data.get("seats", 1),
            expires_at_timestamp=expires_at,
            expires_at_str=exp_time_str,
            is_expired=False,
            features=data.get("features", []),
            error_message=None
        )

    except Exception as e:
        return LicenseInfo(
            is_valid=False,
            plan="invalid",
            customer="Unknown",
            email="",
            seats=0,
            expires_at_timestamp=0,
            expires_at_str="",
            is_expired=True,
            features=[],
            error_message=f"Failed to parse license token: {str(e)}"
        )


def verify_license_online(
    token_str: Optional[str],
    api_url: Optional[str] = None,
    timeout: float = 3.0,
    signing_key: str = DEFAULT_MASTER_KEY
) -> LicenseInfo:
    """
    Validates a commercial license token online against the KernelGuard Cloud SaaS server.
    Ensures real-time revocation checks (e.g., cancelled subscription, chargeback, or admin ban).
    Falls back gracefully to offline cryptographic check if the cloud is unreachable,
    unless KG_ENFORCE_ONLINE=1 is specified.
    """
    local_info = verify_license_token(token_str, signing_key=signing_key)
    if not local_info.is_valid:
        return local_info

    server_url = api_url or os.environ.get("KG_API_URL")
    if not server_url:
        return local_info

    # Query cloud control plane
    endpoint = f"{server_url.rstrip('/')}/v1/auth/verify"
    import urllib.request
    import urllib.error

    payload = json.dumps({"token": token_str, "client_version": "1.0.0"}).encode("utf-8")
    req = urllib.request.Request(
        endpoint,
        data=payload,
        headers={"Content-Type": "application/json", "User-Agent": "KernelGuard-CLI/1.0.0"},
        method="POST"
    )

    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            if data.get("valid"):
                return local_info
            else:
                return LicenseInfo(
                    is_valid=False,
                    plan=local_info.plan,
                    customer=local_info.customer,
                    email=local_info.email,
                    seats=local_info.seats,
                    expires_at_timestamp=local_info.expires_at_timestamp,
                    expires_at_str=local_info.expires_at_str,
                    is_expired=local_info.is_expired,
                    features=local_info.features,
                    error_message=data.get("error", "Access denied by cloud server.")
                )
    except urllib.error.HTTPError as e:
        # Server explicitly rejected token (e.g. 403 Forbidden because it is revoked)
        error_detail = "License rejected by cloud server."
        try:
            body = json.loads(e.read().decode("utf-8"))
            if isinstance(body.get("detail"), dict):
                error_detail = body["detail"].get("error", error_detail)
            elif isinstance(body.get("detail"), str):
                error_detail = body["detail"]
        except Exception:
            pass

        return LicenseInfo(
            is_valid=False,
            plan=local_info.plan,
            customer=local_info.customer,
            email=local_info.email,
            seats=local_info.seats,
            expires_at_timestamp=local_info.expires_at_timestamp,
            expires_at_str=local_info.expires_at_str,
            is_expired=local_info.is_expired,
            features=local_info.features,
            error_message=f"Cloud verification failed ({e.code}): {error_detail}"
        )
    except Exception as e:
        enforce = os.environ.get("KG_ENFORCE_ONLINE", "0").lower() in ("1", "true", "yes")
        if enforce:
            return LicenseInfo(
                is_valid=False,
                plan=local_info.plan,
                customer=local_info.customer,
                email=local_info.email,
                seats=local_info.seats,
                expires_at_timestamp=local_info.expires_at_timestamp,
                expires_at_str=local_info.expires_at_str,
                is_expired=local_info.is_expired,
                features=local_info.features,
                error_message=f"Cloud server unreachable and online enforcement enabled: {str(e)}"
            )
        # Network unreachable/timeout: fallback to valid cryptographic token
        return local_info


def get_active_license(cli_license_flag: Optional[str] = None) -> LicenseInfo:
    """
    Resolves active license from CLI argument or environment variables
    (KERNELGUARD_TOKEN, KG_LICENSE_KEY).
    If KG_API_URL is configured, performs online verification with real-time revocation checks.
    """
    token = cli_license_flag or os.environ.get("KERNELGUARD_TOKEN") or os.environ.get("KG_LICENSE_KEY")
    if os.environ.get("KG_API_URL"):
        return verify_license_online(token)
    return verify_license_token(token)

