include $(TOPDIR)/rules.mk

PKG_NAME:=keenetic-pbr
PKG_VERSION:=1.0.0
PKG_REV:=0f80e414d5868019e8ee4f50cebe30945b97baff
PKG_RELEASE:=1

PKG_SOURCE_URL:=https://github.com/maksimkurb/keenetic-pbr.git
PKG_SOURCE_PROTO:=git
PKG_SOURCE_VERSION:=$(PKG_REV)

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/golang.mk

define Package/keenetic-pbr
	SECTION:=utils
	CATEGORY:=Routing
	EXTRA_DEPENDS:=dnsmasq-full, ipset, iptables, cron
	TITLE:=Tool for downloading, parsing and importing domains/ip/cidr lists into dnsmasq and ipset
	URL:=https://github.com/maksimkurb/keenetic-pbr/
	MAINTAINER:=Maxim Kurbatov <maxim@kurb.me>
	VERSION:=$(PKG_VERSION)-$(BOARD)-$(PKG_RELEASE)
endef

define Package/keenetic-pbr/description
	This package would help you to configure policy-based routing by providing a list of domains, IPs and CIDRs.
	These lists will be downloaded from specified URL and parsed into dnsmasq and ipset.
	Then additional scripts from this package would be used to route traffic based on these lists.
endef

GO_TARGET:=./

define Package/keenetic-pbr/install
		$(INSTALL_DIR) $(1)/opt/etc/cron.daily/
		$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_DIR) $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_DIR) $(1)/opt/etc/init.d/
		$(INSTALL_DIR) $(1)/opt/etc/keenetic-pbr/
		$(INSTALL_DIR) $(1)/opt/etc/dnsmasq.d/
		$(INSTALL_DIR) $(1)/opt/usr/bin/

		$(INSTALL_BIN) opt/etc/cron.daily/50-keenetic-pbr-lists-update.sh $(1)/opt/etc/cron.daily/
		$(INSTALL_BIN) opt/etc/ndm/ifstatechanged.d/50-keenetic-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_BIN) opt/etc/ndm/netfilter.d/50-keenetic-pbr-fwmarks.sh $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_BIN) opt/etc/init.d/S80keenetic-pbr $(1)/opt/etc/init.d
		$(INSTALL_BIN) opt/etc/keenetic-pbr/keenetic-pbr.conf $(1)/opt/etc/keenetic-pbr/
		$(INSTALL_BIN) opt/etc/keenetic-pbr/defaults $(1)/opt/etc/keenetic-pbr/
		$(INSTALL_BIN) opt/etc/dnsmasq.conf.keenetic-pbr $(1)/opt/etc/
		$(INSTALL_BIN) $(PKG_INSTALL_DIR)/bin/keenetic-pbr $(1)/opt/usr/bin/
endef

define Package/keenetic-pbr/postinst

#!/bin/sh

# Essential paths
DNSMASQ_CONF="/opt/etc/dnsmasq.conf"
DNSMASQ_ORIGINAL_BAK="${DNSMASQ_CONF}.keenetic-pbr.orig"
DNSMASQ_KEENETIC_PBR_BAK="${DNSMASQ_CONF}.keenetic-pbr.old"
DNSMASQ_DEFAULT="${DNSMASQ_CONF}.keenetic-pbr"
KEENETIC_CONF="/opt/etc/keenetic-pbr/keenetic-pbr.conf"

echo "Keenetic-pbr installation"
echo "------------------------"

# Backup existing config if present
if [ -f "$DNSMASQ_CONF" ]; then
	echo "Existing dnsmasq.conf found, backing up to '$DNSMASQ_ORIGINAL_BAK'"
    cp "$DNSMASQ_CONF" "$DNSMASQ_ORIGINAL_BAK"

	while true; do
		echo ""
        echo "Select dnsmasq.conf configuration:"
        echo "1. Replace dnsmasq.conf with provided from keenetic-pbr (recommended)"
        echo "2. Keep current dnsmasq.conf"

        if [ -f "$DNSMASQ_KEENETIC_PBR_BAK" ]; then
        	echo "3. Restore dnsmasq.conf from previous keenetic-pbr installation"
        fi

        printf "Please enter your choice: "
		read -r choice
		case $choice in
			1)
				echo "Replacing dnsmasq.conf with provided from keenetic-pbr"
				echo "Copying '$DNSMASQ_DEFAULT' to '$DNSMASQ_CONF'..."
				cp "$DNSMASQ_DEFAULT" "$DNSMASQ_CONF"
				break
				;;
			2)
				echo "Keeping current dnsmasq.conf"
				echo "You can copy keenetic-pbr config later manually, it will be stored at '$DNSMASQ_DEFAULT'"
				break
				;;
			3)
				if [ -f "$DNSMASQ_KEENETIC_PBR_BAK" ]; then
					echo "Restoring dnsmasq.conf from previous keenetic-pbr installation"
					echo "Copying '$DNSMASQ_KEENETIC_PBR_BAK' to '$DNSMASQ_CONF'..."
					cp "$DNSMASQ_KEENETIC_PBR_BAK" "$DNSMASQ_CONF"
					break
				else
					echo "Invalid option."
					continue
				fi
				;;
			*)
				echo "Invalid option."
				continue
				;;
		esac
	done
else
    # No existing config, use default
    cp "$DNSMASQ_DEFAULT" "$DNSMASQ_CONF"
fi

echo "Installation complete!"
echo "Please make sure to:"
echo "1. Configure $KEENETIC_CONF"
echo "2. Configure $DNSMASQ_CONF"
echo "3. (recommended) Install dnscrypt-proxy2 and configure it"
echo "4. Run the following command: keenetic-pbr download"
echo "5. Enable opkg dns-override"
echo "6. Restart OPKG"
echo ""

endef

define Package/keenetic-pbr/postrm

#!/bin/sh

DNSMASQ_CONF="/opt/etc/dnsmasq.conf"
DNSMASQ_ORIGINAL_BAK="${DNSMASQ_CONF}.bak.keenetic-pbr.orig"
DNSMASQ_KEENETIC_PBR_BAK="${DNSMASQ_CONF}.keenetic-pbr.old"

echo "Keenetic-pbr uninstallation"
echo "--------------------------"

if [ -f "$DNSMASQ_CONF" ]; then
    cp "$DNSMASQ_CONF" "$DNSMASQ_KEENETIC_PBR_BAK"
fi

if [ -f "$DNSMASQ_ORIGINAL_BAK" ]; then
    echo "Select action:"
    echo "1. Restore original dnsmasq.conf from backup"
    echo "2. Keep current dnsmasq.conf"

    read -r choice
    case $choice in
        1)
            cp "$DNSMASQ_ORIGINAL_BAK" "$DNSMASQ_CONF"
            echo "Original dnsmasq.conf is restored from backup"
            ;;
        *)
            echo "Keeping current configuration"
            ;;
    esac
else
    echo "No dnsmasq.conf backup configuration found"
fi

echo "Uninstallation complete!"

endef

$(eval $(call BuildPackage,keenetic-pbr))
