"""
KernelGuard Cloud SaaS Control Plane & Licensing API
FastAPI backend for token verification, instant revocation, and Stripe webhooks.
Deployable to Render, Cloudflare, Vercel, or Fly.io.
"""

import os
import sys
import time
from typing import Dict, Any, List, Optional
from fastapi import FastAPI, Header, HTTPException, Request, Depends, status
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

# Add parent dir to path to import kernelguard licensing if needed
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "python")))
from kernelguard.licensing import generate_license_token, verify_license_token

from .database import (
    init_db,
    save_license,
    get_license,
    revoke_license,
    reactivate_license,
    update_license_by_stripe_sub,
    log_verification_attempt,
    list_licenses,
    create_or_get_customer
)


# Configuration
ADMIN_API_KEY = os.environ.get("KG_ADMIN_API_KEY", "kg_admin_secret_998877665544332211")
STRIPE_WEBHOOK_SECRET = os.environ.get("STRIPE_WEBHOOK_SECRET", "")

# Initialize DB tables on startup
init_db()

app = FastAPI(
    title="KernelGuard Cloud Control Plane",
    description="SaaS Licensing, Token Verification, and Instant Access Control API",
    version="1.0.0"
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# --- Request Models ---

class IssueTokenRequest(BaseModel):
    customer: str
    email: str
    plan: str = "pro"  # "pro" or "enterprise"
    seats: int = 5
    days: int = 365
    stripe_subscription_id: Optional[str] = None


class VerifyTokenRequest(BaseModel):
    token: str
    client_version: Optional[str] = "1.0.0"


class RevokeTokenRequest(BaseModel):
    token: str
    reason: Optional[str] = "Subscription cancelled by admin"


# --- Dependencies ---

def verify_admin(x_admin_key: Optional[str] = Header(None)):
    if not x_admin_key or x_admin_key != ADMIN_API_KEY:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or missing X-Admin-Key header."
        )
    return True


# --- Endpoints ---

@app.get("/health")
def health_check():
    return {
        "status": "healthy",
        "service": "KernelGuard Cloud Control Plane",
        "version": "1.0.0",
        "timestamp": int(time.time())
    }


@app.post("/v1/verify")
@app.post("/v1/auth/verify")
def verify_token(req: VerifyTokenRequest, request: Request):
    """
    Called in real-time by customer CI/CD runners before executing tests.
    Performs online check against database for instant revocation support.
    """
    token = req.token.strip()
    client_ip = request.client.host if request.client else ""

    # 1. Cryptographic offline check first
    crypto_info = verify_license_token(token)
    if not crypto_info.is_valid:
        log_verification_attempt(token, "invalid_crypto", client_ip, req.client_version or "1.0.0")
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail={
                "valid": False,
                "error": crypto_info.error_message or "Cryptographic signature failed."
            }
        )

    # 2. Database Online Check for instant revocation / chargeback killswitch
    db_record = get_license(token)
    if db_record:
        if db_record["status"] == "revoked":
            log_verification_attempt(token, "revoked", client_ip, req.client_version or "1.0.0")
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail={
                    "valid": False,
                    "error": f"License access has been revoked: {db_record.get('revocation_reason', 'Account suspended')}."
                }
            )

        if int(time.time()) > db_record["expires_at"]:
            log_verification_attempt(token, "expired", client_ip, req.client_version or "1.0.0")
            raise HTTPException(
                status_code=status.HTTP_403_FORBIDDEN,
                detail={
                    "valid": False,
                    "error": "Subscription expired. Please renew at https://igotlinux.github.io/kernal-guard/"
                }
            )

    log_verification_attempt(token, "active", client_ip, req.client_version or "1.0.0")
    return {
        "valid": True,
        "plan": crypto_info.plan,
        "customer": crypto_info.customer,
        "email": crypto_info.email,
        "seats": crypto_info.seats,
        "expires_at": crypto_info.expires_at_str,
        "features": crypto_info.features
    }


@app.post("/v1/issue", dependencies=[Depends(verify_admin)])
@app.post("/v1/admin/issue", dependencies=[Depends(verify_admin)])
def issue_license(req: IssueTokenRequest):
    """
    Admin-only endpoint to generate a new customer license and store in database.
    """
    token = generate_license_token(
        customer=req.customer,
        email=req.email,
        plan=req.plan,
        seats=req.seats,
        valid_days=req.days
    )

    now = int(time.time())
    expires_at = now + (req.days * 86400)

    create_or_get_customer(req.customer, req.email)
    save_license(
        token=token,
        customer_name=req.customer,
        email=req.email,
        plan=req.plan,
        seats=req.seats,
        expires_at=expires_at,
        stripe_subscription_id=req.stripe_subscription_id
    )

    return {
        "success": True,
        "token": token,
        "customer": req.customer,
        "email": req.email,
        "plan": req.plan,
        "seats": req.seats,
        "expires_at": time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime(expires_at))
    }


@app.post("/v1/revoke", dependencies=[Depends(verify_admin)])
@app.post("/v1/admin/revoke", dependencies=[Depends(verify_admin)])
def revoke_customer_access(req: RevokeTokenRequest):
    """
    Admin killswitch: instantly revokes a customer's token.
    Their CI will be blocked on the very next PR run.
    """
    success = revoke_license(req.token, reason=req.reason or "Admin revocation")
    if not success:
        raise HTTPException(status_code=404, detail="Token not found in database.")
    return {
        "success": True,
        "message": f"Token access revoked immediately. Reason: {req.reason}"
    }


@app.post("/v1/reactivate", dependencies=[Depends(verify_admin)])
@app.post("/v1/admin/reactivate", dependencies=[Depends(verify_admin)])
def reactivate_customer_access(req: RevokeTokenRequest):
    """
    Admin command to unban or reactivate a previously revoked token.
    """
    success = reactivate_license(req.token)
    if not success:
        raise HTTPException(status_code=404, detail="Token not found in database.")
    return {
        "success": True,
        "message": "Token access reactivated successfully."
    }


@app.get("/v1/licenses", dependencies=[Depends(verify_admin)])
@app.get("/v1/admin/licenses", dependencies=[Depends(verify_admin)])
def list_all_licenses():
    """
    Lists all issued customer licenses and their current status.
    """
    return {
        "total": len(list_licenses()),
        "licenses": list_licenses()
    }


@app.post("/v1/stripe/webhook")
async def stripe_webhook(request: Request):
    """
    Handles automated Stripe subscription events:
    - checkout.session.completed -> auto-creates license
    - customer.subscription.deleted -> auto-revokes license
    - invoice.payment_failed -> auto-suspends license
    """
    payload = await request.body()
    # In production, verify stripe signature with stripe.Webhook.construct_event
    try:
        import json
        event = json.loads(payload.decode("utf-8"))
        event_type = event.get("type", "")

        if event_type == "checkout.session.completed":
            session = event.get("data", {}).get("object", {})
            customer_email = session.get("customer_details", {}).get("email") or session.get("customer_email", "unknown@customer.com")
            customer_name = session.get("customer_details", {}).get("name") or "Stripe Customer"
            sub_id = session.get("subscription")

            # Generate and save Pro license for 30 days
            token = generate_license_token(customer=customer_name, email=customer_email, plan="pro", seats=5, valid_days=30)
            expires = int(time.time()) + (30 * 86400)
            save_license(token, customer_name, customer_email, "pro", 5, expires, stripe_subscription_id=sub_id)

        elif event_type == "customer.subscription.deleted":
            sub_id = event.get("data", {}).get("object", {}).get("id")
            if sub_id:
                update_license_by_stripe_sub(sub_id, "revoked", reason="Stripe subscription cancelled")

        elif event_type == "invoice.payment_failed":
            sub_id = event.get("data", {}).get("object", {}).get("subscription")
            if sub_id:
                update_license_by_stripe_sub(sub_id, "revoked", reason="Payment failed on Stripe")

        return {"status": "success", "event": event_type}
    except Exception as e:
        return JSONResponse(status_code=400, content={"error": str(e)})


@app.get("/", response_class=HTMLResponse)
def admin_portal():
    """
    Interactive web UI for managing licenses and monitoring active subscriptions.
    """
    all_lics = list_licenses()
    rows = ""
    for l in all_lics:
        status_color = "#3fb950" if l["status"] == "active" else "#f85149"
        exp_date = time.strftime("%Y-%m-%d", time.gmtime(l["expires_at"]))
        rows += f"""
        <tr>
            <td style="padding:10px; border-bottom:1px solid #30363d;"><strong>{l['customer_name']}</strong><br><small style="color:#8b949e">{l['email']}</small></td>
            <td style="padding:10px; border-bottom:1px solid #30363d;"><span style="background:rgba(56,139,253,0.15); color:#58a6ff; padding:2px 8px; border-radius:12px; font-size:12px;">{l['plan'].upper()}</span></td>
            <td style="padding:10px; border-bottom:1px solid #30363d; color:{status_color}; font-weight:600;">{l['status'].upper()}</td>
            <td style="padding:10px; border-bottom:1px solid #30363d;">{exp_date}</td>
            <td style="padding:10px; border-bottom:1px solid #30363d; font-family:monospace; font-size:11px; max-width:200px; overflow:hidden; text-overflow:ellipsis;">{l['token'][:24]}...</td>
        </tr>
        """

    if not rows:
        rows = '<tr><td colspan="5" style="text-align:center; padding:20px; color:#8b949e;">No licenses issued yet. Use POST /v1/admin/issue to create your first customer.</td></tr>'

    html = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <title>KernelGuard Cloud Control Plane</title>
        <style>
            body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0d1117; color: #c9d1d9; margin: 0; padding: 40px 20px; }}
            .card {{ background: #161b22; border: 1px solid #30363d; border-radius: 8px; max-width: 900px; margin: 0 auto; padding: 24px; box-shadow: 0 8px 24px rgba(0,0,0,0.5); }}
            h1 {{ margin-top: 0; color: #58a6ff; font-size: 24px; }}
            table {{ width: 100%; border-collapse: collapse; margin-top: 20px; font-size: 14px; text-align: left; }}
            th {{ padding: 12px 10px; border-bottom: 2px solid #30363d; color: #8b949e; text-transform: uppercase; font-size: 11px; letter-spacing: 0.5px; }}
            .stats {{ display: flex; gap: 20px; margin-top: 20px; }}
            .stat-box {{ background: #21262d; border-radius: 6px; padding: 14px; flex: 1; text-align: center; }}
            .stat-val {{ font-size: 28px; font-weight: 700; color: #f0f6fc; margin-top: 4px; }}
        </style>
    </head>
    <body>
        <div class="card">
            <h1>🛡️ KernelGuard Cloud Control Plane</h1>
            <p style="color:#8b949e;">Live API Endpoint: <code>/v1/auth/verify</code> | Admin Portal & Stripe Webhook Gateway</p>
            
            <div class="stats">
                <div class="stat-box"><div style="color:#8b949e; font-size:12px;">TOTAL CUSTOMERS</div><div class="stat-val">{len(all_lics)}</div></div>
                <div class="stat-box"><div style="color:#8b949e; font-size:12px;">ACTIVE LICENSES</div><div class="stat-val" style="color:#3fb950;">{sum(1 for l in all_lics if l['status'] == 'active')}</div></div>
                <div class="stat-box"><div style="color:#8b949e; font-size:12px;">REVOKED / CANCELLED</div><div class="stat-val" style="color:#f85149;">{sum(1 for l in all_lics if l['status'] == 'revoked')}</div></div>
            </div>

            <h3 style="margin-top: 30px; font-size: 16px;">Active Customer Subscriptions</h3>
            <table>
                <thead>
                    <tr>
                        <th>Customer</th>
                        <th>Plan</th>
                        <th>Status</th>
                        <th>Expires</th>
                        <th>Token Prefix</th>
                    </tr>
                </thead>
                <tbody>
                    {rows}
                </tbody>
            </table>
        </div>
    </body>
    </html>
    """
    return html
