# KernelGuard SaaS Control Plane & Licensing Server

Lightweight, high-performance FastAPI backend for KernelGuard commercial license issuance, online token verification, instant revocation, and automated Stripe webhooks.

---

## ⚡ Quickstart (Local Development)

### 1. Install dependencies
```bash
pip install -r server/requirements.txt
```

### 2. Run the server
```bash
export PYTHONPATH="./python:."
python3 -m uvicorn server.main:app --host 0.0.0.0 --port 8000 --reload
```

Visit the interactive Web Admin UI at: **`http://localhost:8000/`**  
Interactive Swagger OpenAPI Docs: **`http://localhost:8000/docs`**

---

## 🚀 Free Deployment Guide ($0 / Month)

### Option A: Render.com (Recommended Free Hosting)
1. Fork or push this repository to GitHub.
2. Sign in to [Render.com](https://render.com) (free tier).
3. Click **New +** -> **Blueprint**, select your repo (uses `server/render.yaml`).
4. Or create a **New Web Service**:
   - **Root Directory:** leave root or `.`
   - **Environment:** Python 3
   - **Build Command:** `pip install -r server/requirements.txt`
   - **Start Command:** `python3 -m uvicorn server.main:app --host 0.0.0.0 --port $PORT`
   - **Environment Variables:**
     - `PYTHONPATH`: `./python:.`
     - `KG_ADMIN_API_KEY`: *(Choose a strong secret, e.g. `kg_adm_live_secret_4829`)*
5. Render gives you a free HTTPS URL: `https://kernelguard-saas-server.onrender.com`.

---

### Option B: Cloudflare (Free Custom Domain + Edge Proxy + SSL)
To route requests through Cloudflare (e.g. `api.kernelguard.dev`):
1. Add your custom domain to Cloudflare (Free plan).
2. Go to **DNS** -> **Add Record**:
   - **Type:** `CNAME`
   - **Name:** `api`
   - **Target:** `kernelguard-saas-server.onrender.com` (or your Vercel URL)
   - **Proxy status:** **Proxied** (Orange Cloud icon enabled)
3. Cloudflare gives you:
   - Global CDN caching & DDoS mitigation
   - Automatic TLS/SSL certificates
   - Custom branding URL (`https://api.yourdomain.com`)

---

## 📡 API Endpoints Reference

### 1. Verify License (CI/CD Online Check)
- **Method:** `POST /v1/verify` or `POST /v1/auth/verify`
- **Request Body:**
```json
{
  "token": "KG_PRO_eyJjdXN0b21lciI6ICJBY21lIENvcnAiLCAicGxhbiI6ICJwcm8ifQ.abc123sig",
  "client_version": "1.0.0"
}
```
- **Responses:**
  - `200 OK`: Valid and active license.
  - `403 Forbidden`: Token revoked, suspended, expired, or tampered.

---

### 2. Issue License (Admin Only)
- **Method:** `POST /v1/issue` or `POST /v1/admin/issue`
- **Header:** `X-Admin-Key: <KG_ADMIN_API_KEY>`
- **Request Body:**
```json
{
  "customer": "Apex AI Labs",
  "email": "cto@apex-ai.internal",
  "plan": "pro",
  "seats": 10,
  "days": 365
}
```
- **Response:**
```json
{
  "success": true,
  "token": "KG_PRO_...",
  "customer": "Apex AI Labs",
  "email": "cto@apex-ai.internal",
  "plan": "pro",
  "seats": 10,
  "expires_at": "2027-10-12 18:00:00 UTC"
}
```

---

### 3. Instant Revocation Killswitch (Admin Only)
Revokes customer access immediately. Their CI pipelines will fail on the next execution.
- **Method:** `POST /v1/revoke` or `POST /v1/admin/revoke`
- **Header:** `X-Admin-Key: <KG_ADMIN_API_KEY>`
- **Request Body:**
```json
{
  "token": "KG_PRO_...",
  "reason": "Customer cancelled subscription / chargeback initiated"
}
```

---

### 4. Stripe Automated Webhooks
- **Method:** `POST /v1/stripe/webhook`
- Handles automated events from Stripe Billing:
  - `checkout.session.completed`: Automatically creates and stores customer license.
  - `customer.subscription.deleted`: Instantly revokes customer access.
  - `invoice.payment_failed`: Automatically suspends license until payment succeeds.

---

## 🛡️ Customer CLI Runner Integration

To configure customer CI/CD runners to verify against your cloud server:

```yaml
# In customer GitHub Actions workflow:
- name: Run KernelGuard Verification
  env:
    KG_LICENSE_KEY: ${{ secrets.KG_LICENSE_KEY }}
    KG_API_URL: "https://api.yourdomain.com"
  run: |
    ./kg run --strict
```
If the token is revoked on your server, `./kg run` exits with code 1 and displays:
```
[LICENSE] Verification Error: Cloud verification failed (403): License access has been revoked: Account suspended.
```
