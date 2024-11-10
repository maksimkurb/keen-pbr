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
        $(INSTALL_DIR) $(1)/opt/etc/cron.daily/
        $(INSTALL_DIR) $(1)/opt/etc/ndm/fs.d/
        $(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d/
        $(INSTALL_DIR) $(1)/opt/etc/ndm/ifstatechanged.d/
        $(INSTALL_DIR) $(1)/opt/etc/keenetic-pbr/
        $(INSTALL_DIR) $(1)/opt/etc/dnsmasq.d/

        $(INSTALL_BIN) opt/etc/cron.daily/50-keenetic-pbr-lists-update.sh $(1)/opt/etc/cron.daily/
		$(INSTALL_BIN) opt/etc/ndm/fs.d/50-keenetic-pbr-disable-hwnat.sh $(1)/opt/etc/ndm/fs.d/
		$(INSTALL_BIN) opt/etc/ndm/ifstatechanged.d/50-keenetic-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_BIN) opt/etc/ndm/netfilter.d/50-keenetic-pbr-fwmarks.sh $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_BIN) opt/etc/keenetic-pbr/keenetic-pbr.conf $(1)/opt/etc/keenetic-pbr/
endef

$(eval $(call BuildPackage,keenetic-pbr))
