include $(TOPDIR)/rules.mk

PKG_NAME:=keenetic-pbr
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)-$(PKG_RELEASE)


include $(INCLUDE_DIR)/package.mk

define Package/keenetic-pbr
  SECTION:=utils
  CATEGORY:=Routing
  DEPENDS:=+dnsmasq-full +ipset +iptables +cron +curl
  TITLE:=Tool for downloading, parsing and importing domains/ip/cidr lists into dnsmasq and ipset
  URL:=https://github.com/maksimkurb/keenetic-pbr/
endef

define Package/keenetic-pbr/description
  This package would help you to configure policy-based routing by providing a list of domains, IPs and CIDRs.
  These lists will be downloaded from specified URL and parsed into dnsmasq and ipset.
  Then additional scripts from this package would be used to route traffic based on these lists.
endef

define Build/Prepare
endef
define Build/Configure
endef
define Build/Compile
  GOOS=linux GOARCH=mipsle go build -ldflags "-w -s"
endef


define Package/keenetic-pbr/install
        $(INSTALL_DIR) $(1)/usr/sbin
        $(INSTALL_BIN) $(PKG_BUILD_DIR)/brctl/brctl $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,bridge))
