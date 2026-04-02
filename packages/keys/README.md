### usign keypair generation
See https://openwrt.org/docs/guide-user/security/keygen#generate_usign_key_pair

```
./usign -G -c "key comment" usign_private.key -p usign_public.key
```

### apk key generation
```
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:prime256v1 -out apk_private.pem

openssl pkey -in apk_private.pem -pubout -out apk_public.pem
```

### debian key generation
```
cat > debian-keygen.batch <<'EOF'
Key-Type: RSA
Key-Length: 4096
Name-Real: keen-pbr Debian Repository
Name-Email: keen-pbr@kurb.me
Expire-Date: 0
%no-protection
%commit
EOF

gpg --batch --generate-key debian-keygen.batch

gpg --armor --export "keen-pbr Debian Repository" > debian_public.pem

gpg --armor --export-secret-keys "keen-pbr Debian Repository" > debian_private.pem
```
### Environment variables

* `OPENWRT_USIGN_PRIVATE_KEY`
* `OPENWRT_APK_PRIVATE_KEY`
* `DEBIAN_GPG_PRIVATE_KEY`
