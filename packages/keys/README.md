## Repository signing keys

This repo supports optional repository signing for OpenWrt and Debian.

Private keys are loaded from environment variables:

- `OPENWRT_USIGN_PRIVATE_KEY`
- `OPENWRT_APK_PRIVATE_KEY`
- `DEBIAN_GPG_PRIVATE_KEY`

Published public keys live in:

- `packages/keys/public/openwrt-usign.pub`
- `packages/keys/public/openwrt-apk.pem`
- `packages/keys/public/debian-repo.asc`

## OpenWrt usign keypair

See https://openwrt.org/docs/guide-user/security/keygen#generate_usign_key_pair

```sh
usign -G -s usign_private.key -p usign_public.key -c "keen-pbr OpenWrt signing key"
```

- `OPENWRT_USIGN_PRIVATE_KEY` should contain the raw contents of `usign_private.key`
- publish `usign_public.key` as `packages/keys/public/openwrt-usign.pub`

## OpenWrt apk keypair

```sh
openssl ecparam -name prime256v1 -genkey -noout -out apk_private.pem
openssl ec -in apk_private.pem -pubout > apk_public.pem
```

- `OPENWRT_APK_PRIVATE_KEY` should contain the PEM contents of `apk_private.pem`
- publish `apk_public.pem` as `packages/keys/public/openwrt-apk.pem`

## Debian repository GPG key

```sh
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

- `DEBIAN_GPG_PRIVATE_KEY` should contain the armored secret key export from `debian_private.asc`
- publish `debian_public.asc` as `packages/keys/public/debian-repo.asc`

## Local usage

```sh
OPENWRT_USIGN_PRIVATE_KEY="$(cat /path/to/usign_private.key)" \
OPENWRT_APK_PRIVATE_KEY="$(cat /path/to/apk_private.pem)" \
make openwrt-packages
```

```sh
DEBIAN_GPG_PRIVATE_KEY="$(cat /path/to/debian_private.asc)" \
make deb-packages
```

`make build-repository` does not need private keys. It publishes already-generated signatures and copies the public keys from `packages/keys/public/`.
