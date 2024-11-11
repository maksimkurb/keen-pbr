include $(TOPDIR)/rules.mk

PKG_NAME:=keenetic-pbr
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_REV:=$(PKG_VERSION)-$(PKG_RELEASE)

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/golang.mk

define Package/keenetic-pbr
	SECTION:=utils
	CATEGORY:=Routing
	DEPENDS:=+dnsmasq-full +ipset +iptables +cron
	TITLE:=Tool for downloading, parsing and importing domains/ip/cidr lists into dnsmasq and ipset
	URL:=https://github.com/maksimkurb/keenetic-pbr/
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
		$(INSTALL_DIR) $(1)/opt/etc/ndm/fs.d/
		$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_DIR) $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_DIR) $(1)/opt/etc/keenetic-pbr/
		$(INSTALL_DIR) $(1)/opt/etc/dnsmasq.d/
		$(INSTALL_DIR) $(1)/opt/usr/bin/

		$(INSTALL_BIN) opt/etc/cron.daily/50-keenetic-pbr-lists-update.sh $(1)/opt/etc/cron.daily/
		$(INSTALL_BIN) opt/etc/ndm/fs.d/50-keenetic-pbr-disable-hwnat.sh $(1)/opt/etc/ndm/fs.d/
		$(INSTALL_BIN) opt/etc/ndm/ifstatechanged.d/50-keenetic-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_BIN) opt/etc/ndm/netfilter.d/50-keenetic-pbr-fwmarks.sh $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_BIN) opt/etc/keenetic-pbr/keenetic-pbr.conf $(1)/opt/etc/keenetic-pbr/
		$(INSTALL_BIN) $(PKG_INSTALL_DIR)/bin/keenetic-pbr $(1)/opt/usr/bin/
endef

$(eval $(call BuildPackage,keenetic-pbr))
