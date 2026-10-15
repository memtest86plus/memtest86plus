# Memtest86+ Secure Boot certificates

Starting with v9.00, the Memtest86+ EFI images are signed with the Memtest86+ Secure Boot key.
For now, a machine with UEFI Secure Boot enabled boots a signed Memtest86+ image only after
the Memtest86+ root CA certificate has been added to the firmware's `db`. This directory
contains that certificate and what is needed to verify it.

We are working on a simpler solution: a Microsoft-signed shim for Memtest86+. Once it is
available, Memtest86+ will boot with Secure Boot enabled, without any manual enrollment, on
machines that trust Microsoft's third-party UEFI CA, which is the default on most PCs.

## Files

| File | Contents |
|---|---|
| `mt86plus-ca.cer` | Root CA certificate, DER format. **This is the file to enroll in `db`.** |
| `mt86plus-ca.pem` | The same root CA certificate, PEM format (for `sbverify`) |
| `SHA256SUMS` | SHA-256 of the two files above |

No private key material is, or will ever be, stored in this repository.

## Root CA

```
Subject:     O=Memtest86+, CN=Memtest86+ Secure Boot CA 2026
Serial:      d0:e9:07:d0:fe:1b:22:e4:a6:50:85:3b:1c:92:bf:f8
Valid:       2026-10-11 21:09:27 UTC to 2046-10-11 21:09:27 UTC
Key:         RSA 4096, sha256WithRSAEncryption
Extensions:  Basic Constraints: critical, CA:TRUE, pathlen:0
             Key Usage: critical, Certificate Sign, CRL Sign
SHA-256 fingerprint (= sha256sum of mt86plus-ca.cer):
  b04704005333a771c18aed1b6467711be118cc907ae0ea141bdc1f52457bb155
```

The same fingerprint is published on [memtest.org](https://memtest.org). Check that the two
match before enrolling:

```bash
sha256sum mt86plus-ca.cer
openssl x509 -inform DER -in mt86plus-ca.cer -noout -subject -fingerprint -sha256
```

## Enrolling the certificate

Enrolling the certificate means your machine will trust every binary signed under this CA,
until you remove it again.

- **Add** the certificate to `db`. **Never replace** the existing keys. Removing the Microsoft
  certificates can stop Windows, Linux distributions and, on some machines, the firmware
  drivers of graphics cards and other add-in cards (option ROMs) from loading.
- **Recommended:** use your firmware setup's Secure Boot key management menu (often "Append",
  "Add to db" or "Enroll signature"), and select `mt86plus-ca.cer` from a FAT-formatted USB
  stick.
- **Alternative, for experienced users** (Linux, efitools): put Secure Boot in Setup Mode, then
  run `efi-updatevar -a -c mt86plus-ca.cer db`. Entering Setup Mode deletes the platform key
  (PK). Save it first (`efi-readvar -v PK -o PK.esl`) and restore it afterwards. The firmware's
  "restore factory keys" option also resets `db`, which removes the Memtest86+ certificate
  again.
- `mokutil --import` does not work yet, because it requires a Microsoft-signed shim.

To verify a downloaded image before booting it:

```bash
sbverify --cert mt86plus-ca.pem memtest.efi        # "Signature verification OK"
```

## Signing certificate

Signed images also contain the signing certificate `CN=Memtest86+ Secure Boot Signing 2026`,
issued by this CA. You don't need to enroll it, and its expiry date doesn't matter, because
UEFI firmware doesn't check certificate dates.
