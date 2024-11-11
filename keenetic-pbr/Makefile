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

GO_LDFLAGS += \
	-X '$(XIMPORTPATH)/constant.Version=$(PKG_VERSION)' \
	-w -s


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

echo "Keenetic-pbr is installed!"
echo ""
echo "Replacing old dnsmasq configuration file..."
mv /opt/etc/dnsmasq.conf /opt/etc/dnsmasq.conf.orig || true
mv /opt/etc/dnsmasq.conf.keenetic-pbr /opt/etc/dnsmasq.conf || true
echo "Old dnsmasq configuration file is moved to /opt/etc/dnsmasq.conf.orig"
echo ""
echo "Please check the following config files and modify them accordingly:"
echo "1. /opt/etc/keenetic-pbr/keenetic-pbr.conf"
echo "2. /opt/etc/dnsmasq.conf"
echo ""
echo "After modifying these files, try to download lists by running the following command:"
echo "# keenetic-pbr download"
echo ""
echo "If everything is good, please restart OPKG and check if policy based routing works."

endef

define Package/keenetic-pbr/postrm

#!/bin/sh
echo "Keenetic-pbr is uninstalled!"

echo ""
echo "Restoring original dnsmasq configuration file..."
# Check if the original configuration file exists before proceeding
if [ -f /opt/etc/dnsmasq.conf.orig ]; then
# Move current configuration to backup
mv /opt/etc/dnsmasq.conf /opt/etc/dnsmasq.conf.keenetic-pbr || true

# Restore the original configuration
mv /opt/etc/dnsmasq.conf.orig /opt/etc/dnsmasq.conf || true

echo "Original dnsmasq configuration file restored from /opt/etc/dnsmasq.conf.orig"
echo "Old dnsmasq configuration file is moved to /opt/etc/dnsmasq.conf.keenetic-pbr"
else
echo "Original dnsmasq configuration file not found. No changes made."
fi
echo ""

endef

$(eval $(call BuildPackage,keenetic-pbr))
