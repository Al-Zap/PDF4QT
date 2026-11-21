# Code signing of the Windows build

Without a signature, Windows SmartScreen shows "Windows protected your PC" for `PDF4QT-Setup-<version>.exe`
and users must click *More info* > *Run anyway*. A signature names the publisher and, after the signed
installer has been downloaded for a while, SmartScreen stops warning.

Since 2024 no certificate removes the warning on the first day, not even an EV certificate: SmartScreen
builds the reputation of the signing identity over time. What matters is to sign every release with the
same identity.

## Options for a small business (prices of November 2026)

| Option | Cost per year | Works in GitHub Actions | Notes |
| --- | --- | --- | --- |
| **Azure Artifact Signing** (formerly Trusted Signing), Basic plan | **about $120** ($9.99 per month, 5,000 signatures) | Yes, official action, no hardware | Organizations in the US, Canada, EU, UK and a few more countries; individual developers in the US and Canada only. Identity validation takes 1 to 30 business days. Needs an Azure subscription with a credit card (free and trial subscriptions are not accepted). |
| SSL.com OV certificate + eSigner cloud signing | about $310 ($129 certificate + $15 per month eSigner Tier 1, 240 signatures per month) | Yes, through eSigner | Since June 2023 the key of an OV certificate must live in a hardware token or a cloud HSM, a plain `.pfx` file is no longer issued. A USB token ($379) does not work on GitHub runners. |
| Certum Standard Code Signing in the Cloud | about EUR 209 | Awkward | Signing is approved in a mobile app (SimplySign), which is hard to automate. |
| EV certificate | $400 and more | Through cloud HSM only | No SmartScreen advantage over OV since 2024. Not worth the price. |

**Recommendation: Azure Artifact Signing, Basic plan.** It is the cheapest, it needs no hardware, it is the
option this workflow is prepared for, and the signature quota (about 30 signatures per build, only for
pushes to master and manual runs) is far below the 5,000 included each month.

## Setting up Azure Artifact Signing

1. Create an Azure account and a pay-as-you-go subscription (https://portal.azure.com).
2. Register the resource provider `Microsoft.CodeSigning` in the subscription.
3. Create an *Artifact Signing account* (Basic SKU) in a region near you. Note its endpoint, for example
   `https://eus.codesigning.azure.net/` for East US.
4. Assign yourself the role *Artifact Signing Identity Verifier* and submit an identity validation
   (*Public Trust*). The legal name and address must match the business registration (or, for an
   individual, the government ID). Wait for the approval.
5. Create a *certificate profile* of the type *Public Trust* for the approved identity.
6. In Microsoft Entra ID create an *App registration* (for example `pdf4qt-github-signing`) and a client
   secret for it. Note the tenant id, the client id and the secret.
7. Assign the app registration the role *Artifact Signing Certificate Profile Signer* on the signing account.
8. In GitHub, **Settings > Secrets and variables > Actions** of this repository:
   - Secrets: `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`
   - Variables: `AZURE_SIGNING_ENDPOINT` (the endpoint of step 3), `AZURE_SIGNING_ACCOUNT` (name of the
     signing account), `AZURE_SIGNING_PROFILE` (name of the certificate profile)
9. Run the *Windows build* workflow on master (*Actions > Windows build > Run workflow*). The step
   *Signing: Verify signatures* shows the signer of the installer.

Nothing else has to change: the workflow ([windows-build.yml](../.github/workflows/windows-build.yml)) signs
as soon as all six values exist. Pull requests are never signed. When only some of the values are set, the
run shows a warning and the build stays unsigned.

What is signed: the own executables and libraries of PDF4QT (`Pdf4Qt*.exe`, `Pdf4Qt*.dll`, `PdfTool.exe`,
the plugins) before the ZIP archive and the installer are made, then the installer itself. Qt and the
MSVC runtime keep the signatures of their vendors. The certificates of Artifact Signing are valid for three
days, so every signature carries a timestamp of `http://timestamp.acs.microsoft.com`, which keeps it valid
afterwards.
