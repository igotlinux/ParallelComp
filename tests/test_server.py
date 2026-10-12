"""
Integration tests for KernelGuard Cloud SaaS Control Plane & Licensing API
"""

import os
import unittest
from fastapi.testclient import TestClient

from server.main import app, ADMIN_API_KEY
from python.kernelguard.licensing import verify_license_online, generate_license_token


class TestServerControlPlane(unittest.TestCase):
    def setUp(self):
        self.client = TestClient(app)
        self.headers = {"X-Admin-Key": ADMIN_API_KEY}

    def test_health_check(self):
        response = self.client.get("/health")
        self.assertEqual(response.status_code, 200)
        data = response.json()
        self.assertEqual(data["status"], "healthy")
        self.assertEqual(data["service"], "KernelGuard Cloud Control Plane")

    def test_unauthorized_admin_access(self):
        # Missing header
        res = self.client.post("/v1/issue", json={"customer": "Test", "email": "test@test.com"})
        self.assertEqual(res.status_code, 401)

        # Invalid header
        res = self.client.post(
            "/v1/issue",
            headers={"X-Admin-Key": "wrong_key"},
            json={"customer": "Test", "email": "test@test.com"}
        )
        self.assertEqual(res.status_code, 401)

    def test_issue_verify_and_revoke_lifecycle(self):
        # 1. Issue a new Pro license
        issue_res = self.client.post(
            "/v1/issue",
            headers=self.headers,
            json={
                "customer": "Test Customer Inc",
                "email": "dev@testcustomer.com",
                "plan": "pro",
                "seats": 5,
                "days": 30
            }
        )
        self.assertEqual(issue_res.status_code, 200)
        issued_data = issue_res.json()
        self.assertTrue(issued_data["success"])
        token = issued_data["token"]
        self.assertTrue(token.startswith("KG_PRO_"))

        # 2. Verify active token (both /v1/verify and /v1/auth/verify)
        verify_res = self.client.post("/v1/verify", json={"token": token})
        self.assertEqual(verify_res.status_code, 200)
        v_data = verify_res.json()
        self.assertTrue(v_data["valid"])
        self.assertEqual(v_data["customer"], "Test Customer Inc")
        self.assertEqual(v_data["plan"], "pro")

        # 3. Revoke access via admin killswitch
        revoke_res = self.client.post(
            "/v1/revoke",
            headers=self.headers,
            json={"token": token, "reason": "Customer cancelled subscription"}
        )
        self.assertEqual(revoke_res.status_code, 200)
        self.assertTrue(revoke_res.json()["success"])

        # 4. Verification MUST now fail with 403 Forbidden
        verify_revoked_res = self.client.post("/v1/verify", json={"token": token})
        self.assertEqual(verify_revoked_res.status_code, 403)
        revoked_err = verify_revoked_res.json()["detail"]["error"]
        self.assertIn("License access has been revoked", revoked_err)

        # 5. Reactivate access
        reactivate_res = self.client.post(
            "/v1/reactivate",
            headers=self.headers,
            json={"token": token}
        )
        self.assertEqual(reactivate_res.status_code, 200)

        # 6. Verification succeeds again
        verify_again = self.client.post("/v1/verify", json={"token": token})
        self.assertEqual(verify_again.status_code, 200)
        self.assertTrue(verify_again.json()["valid"])

    def test_stripe_webhook_flow(self):
        # 1. Simulate Stripe checkout.session.completed
        sub_id = "sub_test_stripe_999"
        checkout_payload = {
            "type": "checkout.session.completed",
            "data": {
                "object": {
                    "customer_details": {
                        "name": "Stripe Buyer",
                        "email": "buyer@stripe-user.com"
                    },
                    "subscription": sub_id
                }
            }
        }
        res = self.client.post("/v1/stripe/webhook", json=checkout_payload)
        self.assertEqual(res.status_code, 200)

        # Check that license exists in admin list
        lics_res = self.client.get("/v1/licenses", headers=self.headers)
        self.assertEqual(lics_res.status_code, 200)
        lics = lics_res.json()["licenses"]
        buyer_lic = next((l for l in lics if l["stripe_subscription_id"] == sub_id), None)
        self.assertIsNotNone(buyer_lic)
        self.assertEqual(buyer_lic["status"], "active")

        # 2. Simulate Stripe subscription deletion / cancellation
        cancel_payload = {
            "type": "customer.subscription.deleted",
            "data": {
                "object": {
                    "id": sub_id
                }
            }
        }
        res_cancel = self.client.post("/v1/stripe/webhook", json=cancel_payload)
        self.assertEqual(res_cancel.status_code, 200)

        # Verify token is now revoked in database
        verify_sub_lic = self.client.post("/v1/verify", json={"token": buyer_lic["token"]})
        self.assertEqual(verify_sub_lic.status_code, 403)

    def test_admin_portal_html(self):
        res = self.client.get("/")
        self.assertEqual(res.status_code, 200)
        self.assertIn("text/html", res.headers["content-type"])
        self.assertIn("KernelGuard Cloud Control Plane", res.text)


if __name__ == "__main__":
    unittest.main()
