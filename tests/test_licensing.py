"""
Unit tests for KernelGuard Commercial Licensing & Token Management Engine
"""

import unittest
from kernelguard.licensing import (
    generate_license_token,
    verify_license_token,
    get_active_license,
    LicenseInfo
)


class TestLicensing(unittest.TestCase):
    def test_default_community_license(self):
        lic = verify_license_token(None)
        self.assertFalse(lic.is_valid)
        self.assertEqual(lic.plan, "community")
        self.assertFalse(lic.is_expired)

    def test_generate_and_verify_pro_token(self):
        token = generate_license_token(
            customer="Apex AI Labs",
            email="cto@apex-ai.internal",
            plan="pro",
            seats=10,
            valid_days=30
        )
        self.assertTrue(token.startswith("KG_PRO_"))

        lic = verify_license_token(token)
        self.assertTrue(lic.is_valid)
        self.assertEqual(lic.plan, "pro")
        self.assertEqual(lic.customer, "Apex AI Labs")
        self.assertEqual(lic.email, "cto@apex-ai.internal")
        self.assertEqual(lic.seats, 10)
        self.assertFalse(lic.is_expired)
        self.assertIn("automated_pr_gate", lic.features)

    def test_generate_and_verify_enterprise_token(self):
        token = generate_license_token(
            customer="DeepCompute GPU Corp",
            email="admin@deepcompute.com",
            plan="enterprise",
            seats=100,
            valid_days=365
        )
        self.assertTrue(token.startswith("KG_ENT_"))

        lic = verify_license_token(token)
        self.assertTrue(lic.is_valid)
        self.assertEqual(lic.plan, "enterprise")
        self.assertIn("hardware_gpu_runners", lic.features)

    def test_tampered_token_fails_cryptographic_verification(self):
        token = generate_license_token("Acme", "acme@test.com", plan="pro")
        tampered = token[:-4] + "dead"
        lic = verify_license_token(tampered)
        self.assertFalse(lic.is_valid)
        self.assertEqual(lic.customer, "Tampered")
        self.assertIn("Cryptographic signature verification failed", lic.error_message)

    def test_expired_token(self):
        # Negative days means expired in the past
        token = generate_license_token("Old Corp", "old@test.com", plan="pro", valid_days=-1)
        lic = verify_license_token(token)
        self.assertFalse(lic.is_valid)
        self.assertTrue(lic.is_expired)
        self.assertIn("expired", lic.error_message.lower())


if __name__ == "__main__":
    unittest.main()
