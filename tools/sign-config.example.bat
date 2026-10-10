@echo off
rem Signing settings for tools\package.bat.
rem Copy this file to tools\sign-config.bat (ignored by git) and adjust it.

rem Thumbprint of the code signing certificate (certmgr.msc > Personal > Certificates, or
rem PowerShell: Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert). Without it, signtool picks
rem the best code signing certificate available.
set "SIGN_CERT_SHA1=0000000000000000000000000000000000000000"

rem RFC 3161 timestamp server of your certificate authority.
set "SIGN_TIMESTAMP_URL=http://time.certum.pl"
